// Sketch3D — programme natif (transcription de la version web, fichier unique index.html)
// Lot N3 : import/export OBJ + STL, tout objet est un maillage indexe.
// Base : lot N1 (fenetre, camera, grille) + lot N2 (selection, deplacement, verrouillage X/Y/Z).
//
// Interaction du lot :
//   - I : importer un modele (*.obj, *.stl) via la boite de dialogue Windows ;
//     pose au sol, anti-superposition, invariants affiches en console ;
//   - O : exporter toute la scene en OBJ ; S : exporter en STL binaire ;
//   - double-clic sur un objet : rapprochement ; H : vue d'origine ; F : cadrage ;
//   - clic gauche sur objet : selection + glisser (X/Y/Z tenues = axe verrouille) ;
//   - clic gauche sur vide : deselection + orbite camera ; Echap : quitter.
//
// Rendu : OpenGL fixed-function (1.1) — shaders/PBR a un lot dedie.
// Modele geometrique en double precision.

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

/* ------------------------------------------------------------------ */
/* Petits outils mathematiques (double precision)                      */
/* ------------------------------------------------------------------ */

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double k) const { return {x * k, y * k, z * k}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    double norm() const { return std::sqrt(x * x + y * y + z * z); }
};

static double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static Vec3 normalized(const Vec3& v) {
    const double l = v.norm();
    return (l > 1e-12) ? v * (1.0 / l) : Vec3();
}

static constexpr double kFovDeg = 55.0;

static void setPerspective(double fovYdeg, double aspect, double zNear, double zFar) {
    const double f = 1.0 / std::tan(fovYdeg * 3.14159265358979 / 360.0);
    const double m[16] = {
        f / aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (zFar + zNear) / (zNear - zFar), -1,
        0, 0, (2.0 * zFar * zNear) / (zNear - zFar), 0
    };
    glMultMatrixd(m);
}

static void gluLookAtImpl(const Vec3& eye, const Vec3& tgt) {
    const Vec3 f = normalized(tgt - eye);
    const Vec3 s = normalized(cross(f, Vec3(0, 1, 0)));
    const Vec3 u = cross(s, f);
    const double m[16] = {
        s.x, u.x, -f.x, 0,
        s.y, u.y, -f.y, 0,
        s.z, u.z, -f.z, 0,
        -dot(s, eye), -dot(u, eye), dot(f, eye), 1
    };
    glMultMatrixd(m);
}

/* ------------------------------------------------------------------ */
/* Camera orbitale                                                     */
/* ------------------------------------------------------------------ */

struct Camera {
    Vec3 target{0, 0.5, 0};
    double dist = 9.0;
    double theta = 0.8;
    double phi = 0.5;
    Vec3 pos() const {
        return Vec3(target.x + dist * std::sin(phi) * std::cos(theta),
                    target.y + dist * std::cos(phi),
                    target.z + dist * std::sin(phi) * std::sin(theta));
    }
    Vec3 forward() const { return normalized(target - pos()); }
    Vec3 right() const { return normalized(cross(forward(), Vec3(0, 1, 0))); }
    Vec3 up() const { return cross(right(), forward()); }
};

/* ------------------------------------------------------------------ */
/* Scene : objets = maillages indexes en coordonnees locales           */
/* (pos = translation monde ; la rotation par objet arrive plus tard)  */
/* ------------------------------------------------------------------ */

struct Object {
    std::string name;
    Vec3 pos{0, 0, 0};
    double scl = 1.0;   /* echelle uniforme monde = pos + scl * local */
    double col[3] = {0.55, 0.35, 0.2};
    std::vector<Vec3> v;
    std::vector<unsigned> tri;
    std::vector<Vec3> n;
    std::vector<std::pair<int, int>> edges;
    Vec3 bmin, bmax;
};

static std::vector<Object> g_objs;
static int g_selected = -1;

static void computeObjectDerived(Object& o) {
    o.bmin = Vec3(1e300, 1e300, 1e300);
    o.bmax = Vec3(-1e300, -1e300, -1e300);
    for (const Vec3& p : o.v) {
        if (p.x < o.bmin.x) o.bmin.x = p.x;
        if (p.y < o.bmin.y) o.bmin.y = p.y;
        if (p.z < o.bmin.z) o.bmin.z = p.z;
        if (p.x > o.bmax.x) o.bmax.x = p.x;
        if (p.y > o.bmax.y) o.bmax.y = p.y;
        if (p.z > o.bmax.z) o.bmax.z = p.z;
    }
    if (o.v.empty()) { o.bmin = o.bmax = Vec3(); }
    o.n.clear();
    o.n.reserve(o.tri.size() / 3);
    for (size_t i = 0; i < o.tri.size(); i += 3) {
        const Vec3& a = o.v[o.tri[i]];
        const Vec3& b = o.v[o.tri[i + 1]];
        const Vec3& c = o.v[o.tri[i + 2]];
        o.n.push_back(normalized(cross(b - a, c - a)));
    }
    o.edges.clear();
    std::unordered_set<unsigned long long> seen;
    for (size_t i = 0; i < o.tri.size(); i += 3) {
        for (int k = 0; k < 3; ++k) {
            int a = (int)o.tri[i + k], b = (int)o.tri[i + (k + 1) % 3];
            if (a > b) { const int t = a; a = b; b = t; }
            const unsigned long long key = ((unsigned long long)a << 32) | (unsigned)b;
            if (seen.insert(key).second) o.edges.emplace_back(a, b);
        }
    }
}

static Object makeBoxObject(const std::string& name, Vec3 center, Vec3 half, const double col[3]) {
    Object o;
    o.name = name;
    o.pos = center;
    std::memcpy(o.col, col, sizeof(o.col));
    const double x0 = -half.x, x1 = half.x, y0 = -half.y, y1 = half.y, z0 = -half.z, z1 = half.z;
    o.v = {
        {x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
        {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}
    };
    const unsigned q[12][3] = {
        {0,2,1},{0,3,2},  /* dessous */
        {4,5,6},{4,6,7},  /* dessus  */
        {0,1,5},{0,5,4},  /* Z-      */
        {3,7,6},{3,6,2},  /* Z+      */
        {1,2,6},{1,6,5},  /* X+      */
        {0,4,7},{0,7,3}   /* X-      */
    };
    for (const auto& t : q) { o.tri.push_back(t[0]); o.tri.push_back(t[1]); o.tri.push_back(t[2]); }
    computeObjectDerived(o);
    return o;
}

static void buildScene() {
    g_objs.clear();
    const double cA[3] = {0.55, 0.35, 0.2}, cB[3] = {0.3, 0.5, 0.7}, cC[3] = {0.45, 0.6, 0.35};
    g_objs.push_back(makeBoxObject("Cube A", Vec3(0, 1, 0), Vec3(1, 1, 1), cA));
    g_objs.push_back(makeBoxObject("Pave B", Vec3(3.5, 0.6, 1.5), Vec3(1.5, 0.6, 0.8), cB));
    g_objs.push_back(makeBoxObject("Colonne C", Vec3(-3, 2, -1.5), Vec3(0.5, 2, 0.5), cC));
}

/* ------------------------------------------------------------------ */
/* Etat interaction                                                    */
/* ------------------------------------------------------------------ */

enum Mode { M_None, M_Orbit, M_Drag };
static Mode g_mode = M_None;
static Camera g_cam;
static Camera g_camHome;
static double g_prevX = 0, g_prevY = 0;
static double g_lastClickTime = -1.0;
static int g_winW = 1280, g_winH = 800;
static int g_lockAxis = -1;
static Vec3 g_grabPoint;
static Vec3 g_startPos;

static const char* axisName(int a) { return a == 0 ? "X" : a == 1 ? "Y" : "Z"; }
static Vec3 axisVec(int a) { return a == 0 ? Vec3(1, 0, 0) : a == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1); }

static void goHome() {
    g_cam = g_camHome;
    g_selected = -1;
}

static void frameScene() {
    if (g_objs.empty()) { g_cam.target = Vec3(0, 0.8, 0); g_cam.dist = 9.0; return; }
    Vec3 lo = g_objs[0].pos + g_objs[0].bmin * g_objs[0].scl;
    Vec3 hi = g_objs[0].pos + g_objs[0].bmax * g_objs[0].scl;
    for (size_t i = 1; i < g_objs.size(); ++i) {
        const Vec3 m = g_objs[i].pos + g_objs[i].bmin * g_objs[i].scl;
        const Vec3 M = g_objs[i].pos + g_objs[i].bmax * g_objs[i].scl;
        if (m.x < lo.x) lo.x = m.x;
        if (m.y < lo.y) lo.y = m.y;
        if (m.z < lo.z) lo.z = m.z;
        if (M.x > hi.x) hi.x = M.x;
        if (M.y > hi.y) hi.y = M.y;
        if (M.z > hi.z) hi.z = M.z;
    }
    g_cam.target = (lo + hi) * 0.5;
    const double r = ((hi - lo) * 0.5).norm();
    g_cam.dist = std::max(r / std::tan(kFovDeg * 3.14159265358979 / 360.0), 1.0) * 1.5;
}

static void frameSelected() {
    if (g_selected < 0) { frameScene(); return; }
    const Object& o = g_objs[(size_t)g_selected];
    const Vec3 d = (o.bmax - o.bmin) * (0.5 * o.scl);
    g_cam.target = o.pos + (o.bmin + o.bmax) * 0.5;
    const double r = d.norm();
    if (r > 1e-12) g_cam.dist = 4.0 * r;
}

/* ------------------------------------------------------------------ */
/* Rayons et intersections (tout en doubles)                           */
/* ------------------------------------------------------------------ */

static bool rayFromMouse(GLFWwindow* w, Vec3& org, Vec3& dir) {
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    if (g_winW < 1 || g_winH < 1 || mx < 0 || my < 0) return false;
    const double t = std::tan(kFovDeg * 3.14159265358979 / 360.0);
    const double aspect = double(g_winW) / double(g_winH);
    const double ndcX = 2.0 * mx / double(g_winW) - 1.0;
    const double ndcY = 1.0 - 2.0 * my / double(g_winH);
    dir = normalized(g_cam.right() * (ndcX * t * aspect) + g_cam.up() * (ndcY * t) + g_cam.forward());
    org = g_cam.pos();
    return true;
}

static bool rayAABB(const Vec3& org, const Vec3& dir, const Vec3& lo, const Vec3& hi, double& tHit) {
    double t0 = -1e300, t1 = 1e300;
    for (int a = 0; a < 3; ++a) {
        const double ro = (&org.x)[a], rd = (&dir.x)[a];
        if (std::fabs(rd) < 1e-15) {
            if (ro < (&lo.x)[a] || ro > (&hi.x)[a]) return false;
        } else {
            double ta = ((&lo.x)[a] - ro) / rd, tb = ((&hi.x)[a] - ro) / rd;
            if (ta > tb) { const double tmp = ta; ta = tb; tb = tmp; }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            if (t0 > t1) return false;
        }
    }
    if (t1 < 0) return false;
    tHit = (t0 > 0) ? t0 : t1;
    return true;
}

/* Moller-Trumbore, coordonnees monde (locales + translation) */
static bool rayTri(const Vec3& org, const Vec3& dir, const Vec3& a, const Vec3& b, const Vec3& c, double& tHit) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = cross(dir, e2);
    const double det = dot(e1, p);
    if (std::fabs(det) < 1e-12) return false;
    const double inv = 1.0 / det;
    const Vec3 s = org - a;
    const double u = dot(s, p) * inv;
    if (u < -1e-9 || u > 1.0 + 1e-9) return false;
    const Vec3 q = cross(s, e1);
    const double vv = dot(dir, q) * inv;
    if (vv < -1e-9 || u + vv > 1.0 + 1e-9) return false;
    const double t = dot(e2, q) * inv;
    if (t <= 1e-9) return false;
    tHit = t;
    return true;
}

static bool rayObject(const Vec3& org, const Vec3& dir, const Object& o, double& tHit) {
    double t0;
    if (!rayAABB(org, dir, o.pos + o.bmin * o.scl, o.pos + o.bmax * o.scl, t0)) return false;
    /* rayon ramene en local : orgL + dirL*t = vlocal, meme parametre t (monde) */
    const double inv = (o.scl > 1e-12) ? 1.0 / o.scl : 1.0;
    const Vec3 orgL = (org - o.pos) * inv;
    const Vec3 dirL = dir * inv;
    double best = 1e300;
    for (size_t i = 0; i < o.tri.size(); i += 3) {
        double t;
        if (rayTri(orgL, dirL, o.v[o.tri[i]], o.v[o.tri[i + 1]], o.v[o.tri[i + 2]], t)
            && t < best) best = t;
    }
    if (best >= 1e300) return false;
    tHit = best;
    return true;
}

static int pickAt(GLFWwindow* w) {
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return -1;
    int best = -1;
    double bestT = 1e300;
    for (size_t i = 0; i < g_objs.size(); ++i) {
        double t;
        if (rayObject(org, dir, g_objs[i], t) && t < bestT) { bestT = t; best = (int)i; }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Deplacement                                                         */
/* ------------------------------------------------------------------ */

static void beginDrag(GLFWwindow* w, int idx) {
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return;
    double t;
    if (!rayObject(org, dir, g_objs[(size_t)idx], t)) { g_mode = M_None; return; }
    g_grabPoint = org + dir * t;
    g_startPos = g_objs[(size_t)idx].pos;
    g_lockAxis = -1;
    g_mode = M_Drag;
}

static bool g_axisHeld[3] = {false, false, false};

static void recordAxisKey(int scancode, int action) {
    /* GLFW_KEY_REPEAT arrive en continu pendant une touche tenue : ne pas
       le confondre avec un relachement (il effacait le verrou aussitot). */
    if (action == GLFW_REPEAT) return;
    /* premier parametre = GLFW_KEY_UNKNOWN (-1) : traduction du scancode
       selon le layout courant (AZERTY inclus) ; 0 vaudrait la touche Espace. */
    const char* name = glfwGetKeyName(-1, scancode);
    if (!name || name[0] == '\0') return;
    const char c = (name[0] >= 'A' && name[0] <= 'Z') ? char(name[0] - 'A' + 'a') : name[0];
    if (c == 'x') g_axisHeld[0] = (action == GLFW_PRESS);
    else if (c == 'y') g_axisHeld[1] = (action == GLFW_PRESS);
    else if (c == 'z') g_axisHeld[2] = (action == GLFW_PRESS);
}

static void updateDrag(GLFWwindow* w) {
    if (g_selected < 0) { g_mode = M_None; return; }
    const int axis = (g_axisHeld[0]) ? 0 : (g_axisHeld[1]) ? 1 : (g_axisHeld[2]) ? 2 : -1;
    if (axis != g_lockAxis) {
        g_lockAxis = axis;
        if (axis >= 0) std::printf("deplacement verrouille sur %s\n", axisName(axis));
        else           std::printf("deplacement libre (plan camera)\n");
    }
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return;
    const Vec3 n = g_cam.forward();
    const double denom = dot(dir, n);
    if (std::fabs(denom) < 1e-9) return;
    const double tPlane = dot(g_grabPoint - org, n) / denom;
    if (tPlane < 0) return;
    Vec3 delta = (org + dir * tPlane) - g_grabPoint;
    if (g_lockAxis >= 0) {
        const Vec3 a = axisVec(g_lockAxis);
        delta = a * dot(delta, a);
    }
    g_objs[(size_t)g_selected].pos = g_startPos + delta;
}

/* ------------------------------------------------------------------ */
/* Import OBJ / STL                                                    */
/* ------------------------------------------------------------------ */

static std::string baseNameOf(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    std::string s = (p == std::string::npos) ? path : path.substr(p + 1);
    p = s.find_last_of('.');
    if (p != std::string::npos && p > 0) s = s.substr(0, p);
    return s;
}

static bool loadOBJ(std::istream& in, Object& o, std::string& err) {
    std::vector<Vec3> raw;
    std::vector<unsigned> tris;
    std::string line;
    size_t skipped = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        const size_t h = line.find_first_not_of(" \t");
        if (h == std::string::npos || line[h] == '#') continue;
        std::istringstream ss(line.substr(h));
        std::string tok;
        ss >> tok;
        if (tok == "v") {
            double x, y, z;
            if (ss >> x >> y >> z) raw.push_back(Vec3(x, y, z));
        } else if (tok == "f") {
            std::vector<long long> ids;
            std::string ft;
            while (ss >> ft) {
                const size_t slash = ft.find('/');
                if (slash != std::string::npos) ft = ft.substr(0, slash);
                if (ft.empty()) continue;
                char* end = nullptr;
                const long long idx = std::strtoll(ft.c_str(), &end, 10);
                if (end == ft.c_str() || *end != '\0') continue;
                const long long n = (long long)raw.size();
                if (idx > 0 && idx <= n) ids.push_back(idx - 1);
                else if (idx < 0 && -idx <= n) ids.push_back(n + idx);
            }
            for (size_t k = 2; k < ids.size(); ++k) {
                if (ids[0] >= 0 && ids[k - 1] >= 0 && ids[k] >= 0 && ids[k - 1] != ids[k] && ids[0] != ids[k]) {
                    tris.push_back((unsigned)ids[0]);
                    tris.push_back((unsigned)ids[k - 1]);
                    tris.push_back((unsigned)ids[k]);
                } else {
                    ++skipped;
                }
            }
        }
    }
    if (raw.empty() || tris.empty()) { err = "pas de triangles dans le fichier OBJ"; return false; }
    o.v = std::move(raw);
    o.tri = std::move(tris);
    computeObjectDerived(o);
    if (skipped) std::printf("import OBJ : %zu faces degenerees ou invalides ignorees\n", skipped);
    return true;
}

static bool loadSTL(std::istream& in, Object& o, std::string& err) {
    in.seekg(0, std::ios::end);
    const std::streamoff sz = in.tellg();
    in.seekg(0, std::ios::beg);
    if (sz < 15) { err = "fichier STL vide"; return false; }
    std::vector<char> bytes((size_t)sz);
    in.read(bytes.data(), sz);

    /* STL binaire : 80 octets d'entete + uint32 + 50 octets par triangle */
    if (sz >= 84) {
        unsigned cnt;
        std::memcpy(&cnt, bytes.data() + 80, 4);
        if (84 + (long long)cnt * 50 == sz) {
            size_t off = 84;
            for (unsigned i = 0; i < cnt; ++i) {
                float f[12];
                /* enregistrement : normale (12 o) + 3 sommets (36 o) + attribut (2 o) ignoré */
                std::memcpy(f, bytes.data() + off, 12 * sizeof(float));
                off += 50;
                for (int k = 0; k < 3; ++k)
                    o.v.push_back(Vec3(f[3 + k * 3], f[4 + k * 3], f[5 + k * 3]));
            }
        }
    }
    /* STL ASCII : groupe de 3 lignes "vertex x y z" = un triangle */
    std::istringstream ss(std::string(bytes.begin(), bytes.end()));
    std::string tok;
    double buf[9];
    int nb = 0;
    while (ss >> tok) {
        if (tok != "vertex") continue;
        if (!(ss >> buf[nb * 3] >> buf[nb * 3 + 1] >> buf[nb * 3 + 2])) break;
        if (++nb == 3) {
            for (int k = 0; k < 3; ++k) o.v.push_back(Vec3(buf[k * 3], buf[k * 3 + 1], buf[k * 3 + 2]));
            nb = 0;
        }
    }
    if (o.v.empty()) { err = "pas de facettes dans le fichier STL"; return false; }
    /* 3 sommets consecutifs = 1 triangle (les deux chemins de lecture) */
    o.tri.clear();
    o.tri.reserve(o.v.size() / 3);
    for (size_t i = 0; i + 2 < o.v.size(); i += 3) {
        o.tri.push_back((unsigned)i);
        o.tri.push_back((unsigned)(i + 1));
        o.tri.push_back((unsigned)(i + 2));
    }
    computeObjectDerived(o);
    return true;
}

#ifdef _WIN32
static std::string fileDialog(bool save, const char* filter, const char* defExt, const char* title) {
    char buf[MAX_PATH] = "";
    OPENFILENAMEA of{};
    of.lStructSize = sizeof(of);
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = defExt;
    of.lpstrTitle = title;
    of.Flags = OFN_HIDEREADONLY | (save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameA(&of) : GetOpenFileNameA(&of);
    return ok ? std::string(buf) : std::string();
}
#else
static std::string fileDialog(bool, const char*, const char*, const char* title) {
    std::printf("%s (chemin) : ", title);
    std::string s;
    std::getline(std::cin, s);
    return s;
}
#endif

static bool loadMeshFile(const std::string& path, Object& o, std::string& err) {
    const size_t dot = path.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : path.substr(dot);
    for (char& c : ext) c = (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "fichier illisible"; return false; }
    if (ext == ".stl") return loadSTL(f, o, err);
    if (ext == ".obj") return loadOBJ(f, o, err);
    err = "format non reconnu (extensions attendues : .obj, .stl)";
    return false;
}

/* AABB (monde) avec recouvrement sur les 3 axes = contact */
static bool overlapsWorld(const Object& a, const Object& b) {
    const Vec3 amin = a.pos + a.bmin * a.scl, amax = a.pos + a.bmax * a.scl;
    const Vec3 bmin = b.pos + b.bmin * b.scl, bmax = b.pos + b.bmax * b.scl;
    for (int i = 0; i < 3; ++i) {
        if ((&amax.x)[i] <= (&bmin.x)[i] || (&bmax.x)[i] <= (&amin.x)[i]) return false;
    }
    return true;
}

/* Normalisation d'echelle : l'objet importe doit tenir entre 1 et 4 unites,
   comme la scene de demonstration (le web inserait un facteur 4/3019 sur le
   fichier reel). Le facteur reste object par objet. */
static void normalizeImportScale(Object& o) {
    const double mx = o.bmax.x - o.bmin.x, my = o.bmax.y - o.bmin.y, mz = o.bmax.z - o.bmin.z;
    const double span = std::max(std::max(mx, my), mz);
    if (span > 4.0) o.scl = 4.0 / span;
    else if (span < 1.0) o.scl = 1.0 / span;
}

/* Pose au sol + anti-superposition (transcription du lot web 2.x) */
static void placeImport(Object& o) {
    const Vec3 dim = (o.bmax - o.bmin) * o.scl;
    o.pos = Vec3(0, 0, 0) - Vec3((o.bmin.x + o.bmax.x) * 0.5, o.bmin.y, (o.bmin.z + o.bmax.z) * 0.5) * o.scl;
    double stepX = std::max(dim.x, 1.0) * 1.5;
    Vec3 tried = o.pos;
    for (int tries = 0; tries < 64; ++tries) {
        bool clash = false;
        for (const Object& other : g_objs) {
            if (overlapsWorld(o, other)) { clash = true; break; }
        }
        if (!clash) return;
        tried.x += stepX;
        o.pos = tried;
    }
}

static void importFile() {
    const std::string path = fileDialog(false,
        "Modeles (*.obj;*.stl)\0*.obj;*.stl\0Tous (*.*)\0*.*\0\0",
        nullptr, "Importer un modele");
    if (path.empty()) return;
    Object o;
    std::string err;
    if (!loadMeshFile(path, o, err)) { std::printf("import echoue : %s\n", err.c_str()); return; }
    o.name = baseNameOf(path);
    o.col[0] = 0.35; o.col[1] = 0.55; o.col[2] = 0.35;
    computeObjectDerived(o);
    normalizeImportScale(o);

    /* Diagnostics portes du web : volume signe + invariant de coque fermee
       (chaque arete d'une coque fermee est partagee par exactement 2 tris) */
    double vol = 0.0;
    for (size_t i = 0; i < o.tri.size(); i += 3)
        vol += dot(o.v[o.tri[i]], cross(o.v[o.tri[i + 1]], o.v[o.tri[i + 2]]));
    vol /= 6.0;
    std::unordered_map<unsigned long long, int> arity;
    for (size_t i = 0; i < o.tri.size(); i += 3)
        for (int k = 0; k < 3; ++k) {
            unsigned e0 = o.tri[i + k], e1 = o.tri[i + (k + 1) % 3];
            if (e0 > e1) { const unsigned t = e0; e0 = e1; e1 = t; }
            ++arity[((unsigned long long)e0 << 32) | e1];
        }
    size_t nonManifold = 0;
    for (const auto& kv : arity) if (kv.second != 2) ++nonManifold;

    placeImport(o);
    const Vec3 dim = (o.bmax - o.bmin) * o.scl;
    std::printf("import : %s — %zu sommets, %zu tris, dimensions locales %.4g x %.4g x %.4g, echelle %.6g, dimensions affichees %.4g x %.4g x %.4g, volume local %.6g, volume affiche %.6g, aretes non-manifold %zu\n",
                o.name.c_str(), o.v.size(), o.tri.size(),
                o.bmax.x - o.bmin.x, o.bmax.y - o.bmin.y, o.bmax.z - o.bmin.z, o.scl,
                dim.x, dim.y, dim.z, vol, vol * o.scl * o.scl * o.scl, nonManifold);
    g_objs.push_back(std::move(o));
    g_selected = (int)g_objs.size() - 1;
    frameSelected();
}

/* ------------------------------------------------------------------ */
/* Export OBJ / STL                                                    */
/* ------------------------------------------------------------------ */

static void exportOBJ(const std::string& path) {
    std::ofstream f(path);
    if (!f) { std::printf("export OBJ impossible : %s\n", path.c_str()); return; }
    f << "# Sketch3D natif — export scene\n";
    unsigned base = 1;
    for (const Object& o : g_objs) {
        f << "o " << o.name << "\n";
        for (const Vec3& p : o.v) {
            const Vec3 w = o.pos + p * o.scl;
            f << "v " << w.x << " " << w.y << " " << w.z << "\n";
        }
        for (size_t i = 0; i < o.tri.size(); i += 3)
            f << "f " << base + o.tri[i] << " " << base + o.tri[i + 1] << " " << base + o.tri[i + 2] << "\n";
        base += (unsigned)o.v.size();
    }
    std::printf("export OBJ : %s (%zu objets, en coordonnees monde)\n", path.c_str(), g_objs.size());
}

static void exportSTL(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::printf("export STL impossible : %s\n", path.c_str()); return; }
    size_t total = 0;
    for (const Object& o : g_objs) total += o.tri.size() / 3;
    char header[80] = {0};
    f.write(header, 80);
    const uint32_t cnt = (uint32_t)total;
    f.write((const char*)&cnt, 4);
    for (const Object& o : g_objs) {
        for (size_t i = 0; i < o.tri.size(); i += 3) {
            const Vec3 n = o.n[i / 3];
            float rec[12] = {
                (float)n.x, (float)n.y, (float)n.z,
                0, 0, 0, 0, 0, 0, 0, 0, 0
            };
            for (int k = 0; k < 3; ++k) {
                const Vec3 w = o.pos + o.v[o.tri[i + k]] * o.scl;
                rec[3 + k * 3] = (float)w.x;
                rec[4 + k * 3] = (float)w.y;
                rec[5 + k * 3] = (float)w.z;
            }
            f.write((const char*)rec, sizeof(rec));
            const uint16_t attr = 0;
            f.write((const char*)&attr, 2);
        }
    }
    std::printf("export STL binaire : %s (%zu tris)\n", path.c_str(), total);
}

/* ------------------------------------------------------------------ */
/* Callbacks                                                           */
/* ------------------------------------------------------------------ */

static void onScroll(GLFWwindow*, double, double dy) {
    g_cam.dist *= std::pow(0.9, dy);
    if (g_cam.dist < 0.5) g_cam.dist = 0.5;
    if (g_cam.dist > 2000.0) g_cam.dist = 2000.0;
}

static void onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        const double now = glfwGetTime();
        const bool dbl = (now - g_lastClickTime) < 0.3;
        g_lastClickTime = now;
        const int hit = pickAt(w);
        if (hit != g_selected) {
            g_selected = hit;
            if (hit >= 0) {
                const Object& o = g_objs[(size_t)hit];
                std::printf("selection : %s (%zu tris)\n", o.name.c_str(), o.tri.size() / 3);
            } else {
                std::printf("deselection\n");
            }
        }
        if (hit >= 0) {
            beginDrag(w, hit);
            if (dbl) {
                g_selected = hit;
                frameSelected();
            }
        } else {
            g_mode = M_Orbit;
        }
        glfwGetCursorPos(w, &g_prevX, &g_prevY);
    } else {
        if (g_mode == M_Drag) g_lockAxis = -1;
        g_mode = M_None;
    }
}

static void onKey(GLFWwindow* w, int key, int scancode, int action, int) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
        if (key == GLFW_KEY_F) frameSelected();
        if (key == GLFW_KEY_H) { goHome(); std::printf("vue d'origine\n"); }
        if (key == GLFW_KEY_I) { importFile(); }   /* importer OBJ/STL */
        if (key == GLFW_KEY_O) { const std::string p = fileDialog(true, "Fichier OBJ\0*.obj\0\0", "obj", "Exporter en OBJ"); if (!p.empty()) exportOBJ(p); }
        if (key == GLFW_KEY_S) { const std::string p = fileDialog(true, "Fichier STL\0*.stl\0\0", "stl", "Exporter en STL"); if (!p.empty()) exportSTL(p); }
    }
    recordAxisKey(scancode, action);
}

/* ------------------------------------------------------------------ */
/* Rendu                                                               */
/* ------------------------------------------------------------------ */

static void drawGrid(int span, double step) {
    glDisable(GL_LIGHTING);
    glBegin(GL_LINES);
    glColor3d(0.25, 0.25, 0.28);
    for (int i = -span; i <= span; ++i) {
        glVertex3d(i * step, 0, -span * step);
        glVertex3d(i * step, 0, span * step);
        glVertex3d(-span * step, 0, i * step);
        glVertex3d(span * step, 0, i * step);
    }
    glEnd();
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0);
    glBegin(GL_LINES);
    glColor3d(0.92, 0.25, 0.2);
    glVertex3d(-span * step, 0, 0); glVertex3d(span * step, 0, 0);
    glColor3d(0.2, 0.35, 0.92);
    glVertex3d(0, 0, -span * step); glVertex3d(0, 0, span * step);
    glEnd();
    glLineWidth(1.0);
    glEnable(GL_DEPTH_TEST);
}

static void drawObject(const Object& o, bool selected) {
    glPushMatrix();
    glTranslated(o.pos.x, o.pos.y, o.pos.z);
    glScaled(o.scl, o.scl, o.scl);
    glEnable(GL_LIGHTING);
    glBegin(GL_TRIANGLES);
    for (size_t i = 0, t = 0; i < o.tri.size(); i += 3, ++t) {
        glNormal3dv(&o.n[t].x);
        if (selected) glColor3d(0.45, 0.7, 0.9);
        else glColor3dv(o.col);
        for (int k = 0; k < 3; ++k) glVertex3dv(&o.v[o.tri[i + k]].x);
    }
    glEnd();
    glDisable(GL_LIGHTING);
    glBegin(GL_LINES);
    if (selected) { glColor3d(1, 1, 1); glLineWidth(2.0); }
    else glColor3d(0.2, 0.2, 0.22);
    for (const auto& e : o.edges) {
        glVertex3dv(&o.v[e.first].x);
        glVertex3dv(&o.v[e.second].x);
    }
    glEnd();
    glLineWidth(1.0);
    glPopMatrix();
}

static void render() {
    glViewport(0, 0, g_winW, g_winH);
    glClearColor(0.12f, 0.13f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (g_winH > 0) setPerspective(kFovDeg, double(g_winW) / double(g_winH), 0.05, 5000.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAtImpl(g_cam.pos(), g_cam.target);

    const float pos[4] = {5.0f, 10.0f, 5.0f, 1.0f};
    const float amb[4] = {0.25f, 0.25f, 0.25f, 1.0f};
    const float dif[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);

    drawGrid(50, 1.0);
    for (size_t i = 0; i < g_objs.size(); ++i)
        drawObject(g_objs[i], (int)i == g_selected && g_mode != M_Drag);
}

static void drawAxisGuide() {
    if (g_mode != M_Drag || g_selected < 0 || g_lockAxis < 0) return;
    const Vec3 p = g_objs[(size_t)g_selected].pos;
    const Vec3 a = axisVec(g_lockAxis);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.5);
    glBegin(GL_LINES);
    glColor3d(0.92, 0.75, 0.1);
    glVertex3d(p.x - a.x * 500, p.y - a.y * 500, p.z - a.z * 500);
    glVertex3d(p.x + a.x * 500, p.y + a.y * 500, p.z + a.z * 500);
    glEnd();
    glLineWidth(1.0);
    glEnable(GL_DEPTH_TEST);
    glPointSize(6.0f);
    glBegin(GL_POINTS);
    glColor3d(0.92, 0.75, 0.1);
    glVertex3d(p.x, p.y, p.z);
    glEnd();
}

/* ------------------------------------------------------------------ */
/* Programme principal                                                 */
/* ------------------------------------------------------------------ */

int main() {
    std::printf("Sketch3D natif — lot N3 (import/export OBJ + STL)\n");
    std::printf("I : importer ; O : export OBJ ; S : export STL ; double-clic : rapprochement ; F : cadrage ; H : origine\n");
    buildScene();
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit a echoue\n"); return 1; }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWwindow* win = glfwCreateWindow(g_winW, g_winH, "Sketch3D — build N3", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "creation de fenetre impossible\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    g_camHome = g_cam;
    glfwSetScrollCallback(win, onScroll);
    glfwSetMouseButtonCallback(win, onMouseButton);
    glfwSetKeyCallback(win, onKey);
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow*, int w, int h) { g_winW = w; g_winH = h; });

    while (!glfwWindowShouldClose(win)) {
        if (g_mode == M_Orbit) {
            double x, y;
            glfwGetCursorPos(win, &x, &y);
            g_cam.theta += (x - g_prevX) * 0.01;
            g_cam.phi += (y - g_prevY) * 0.01;
            if (g_cam.phi < 0.02) g_cam.phi = 0.02;
            if (g_cam.phi > 3.12) g_cam.phi = 3.12;
            g_prevX = x; g_prevY = y;
        } else if (g_mode == M_Drag) {
            updateDrag(win);
        }
        render();
        drawAxisGuide();
        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
