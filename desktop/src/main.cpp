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
#include <array>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <GLFW/glfw3native.h>
#endif

/* Version affichee dans la console et le titre de fenetre : maj a chaque
   lot ou correctif (ex. T1.3) pour verifier la build utilisee.            */
static const char* BUILD_VERSION = "T1.5.6";

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
    /* cible en coordonnees MONDE : pos est deja monde, bmin/bmax sont locaux
       donc l'echelle par objet s'applique (sinon la camera vise un point situe
       a des milliers d'unites du vrai centre pour un import en millimetres) */
    g_cam.target = o.pos + (o.bmin + o.bmax) * (0.5 * o.scl);
    const double r = d.norm();
    if (r > 1e-12)
        g_cam.dist = std::max(r / std::tan(kFovDeg * 3.14159265358979 / 360.0), 1.0) * 1.5;
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

static bool rayObjectTri(const Vec3& org, const Vec3& dir, const Object& o, double& tHit, size_t* triIdx) {
    double t0;
    if (!rayAABB(org, dir, o.pos + o.bmin * o.scl, o.pos + o.bmax * o.scl, t0)) return false;
    /* rayon ramene en local : orgL + dirL*t = vlocal, meme parametre t (monde) */
    const double inv = (o.scl > 1e-12) ? 1.0 / o.scl : 1.0;
    const Vec3 orgL = (org - o.pos) * inv;
    const Vec3 dirL = dir * inv;
    double best = 1e300;
    for (size_t i = 0; i < o.tri.size(); i += 3) {
        double t;
        if (rayTri(orgL, dirL, o.v[o.tri[i]], o.v[o.tri[i + 1]], o.v[o.tri[i + 2]], t) && t < best) {
            best = t;
            if (triIdx) *triIdx = i / 3;
        }
    }
    if (best >= 1e300) return false;
    tHit = best;
    return true;
}

static bool rayObject(const Vec3& org, const Vec3& dir, const Object& o, double& tHit) {
    return rayObjectTri(org, dir, o, tHit, nullptr);
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

/* Soudure des sommets coincidents a l'import (lot web 3.6) : grille spatiale
   a tolerance relative, les sommets juxtaposes (STL, triangulations de coque)
   partagent ensuite leurs indices — indispensable pour le Push/Pull. */
static void weldObject(Object& o) {
    computeObjectDerived(o);
    const double span = std::max(std::max(o.bmax.x - o.bmin.x, o.bmax.y - o.bmin.y), o.bmax.z - o.bmin.z);
    if (o.v.empty() || span <= 0.0) return;
    const double tol = span * 1e-6;
    const double t2 = tol * tol;
    auto cellKey = [](long long cx, long long cy, long long cz) {
        return ((unsigned long long)(cx + (1LL << 20)) << 42)
             | ((unsigned long long)(cy + (1LL << 20)) << 21)
             |  (unsigned long long)(cz + (1LL << 20));
    };
    std::unordered_map<unsigned long long, std::vector<int>> cells;
    std::vector<unsigned> remap(o.v.size(), 0);
    std::vector<Vec3> out;
    out.reserve(o.v.size());
    for (size_t i = 0; i < o.v.size(); ++i) {
        const Vec3 p = o.v[i];
        const long long cx = (long long)std::floor(p.x / tol), cy = (long long)std::floor(p.y / tol), cz = (long long)std::floor(p.z / tol);
        int rep = -1;
        for (long long u = -1; u <= 1 && rep < 0; ++u)
            for (long long w = -1; w <= 1 && rep < 0; ++w)
                for (long long q = -1; q <= 1 && rep < 0; ++q) {
                    auto it = cells.find(cellKey(cx + u, cy + w, cz + q));
                    if (it == cells.end()) continue;
                    for (int r : it->second) {
                        const Vec3 d = out[(size_t)r] - p;
                        if (dot(d, d) <= t2) { rep = r; break; }
                    }
                }
        if (rep < 0) {
            rep = (int)out.size();
            out.push_back(p);
            cells[cellKey(cx, cy, cz)].push_back(rep);
        }
        remap[i] = (unsigned)rep;
    }
    o.v = std::move(out);
    std::vector<unsigned> tris;
    tris.reserve(o.tri.size());
    for (size_t i = 0; i + 2 < o.tri.size(); i += 3) {
        const unsigned a = remap[o.tri[i]], b = remap[o.tri[i + 1]], c = remap[o.tri[i + 2]];
        if (a == b || b == c || a == c) continue;
        tris.push_back(a); tris.push_back(b); tris.push_back(c);
    }
    o.tri = std::move(tris);
}

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
    const size_t rawCount = o.v.size();
    weldObject(o);
    computeObjectDerived(o);
    if (o.v.size() < rawCount)
        std::printf("import OBJ : %zu sommets soudes (%zu restants)\n", rawCount - o.v.size(), o.v.size());
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
    const size_t rawCount = o.v.size();
    weldObject(o);
    computeObjectDerived(o);
    if (o.v.size() < rawCount)
        std::printf("import STL : %zu sommets soudes (%zu restants)\n", rawCount - o.v.size(), o.v.size());
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

/* Pose au sol + anti-superposition (transcription du lot web 2.x :
   placeOnGround + placeWithoutOverlap). Comme le web, on pose au sol a
   l'origine, puis on decale en X juste apres le bord de la boite en collision
   avec une marge de 0.25, jusqu'a un emplacement libre. */
/* renvoie le nombre de decalages effectues pour eviter les superpositions */
static int placeImport(Object& o) {
    o.pos = Vec3(0, 0, 0) - Vec3((o.bmin.x + o.bmax.x) * 0.5, o.bmin.y, (o.bmin.z + o.bmax.z) * 0.5) * o.scl;
    int shifts = 0;
    Vec3 amin, amax;
    for (int tries = 0; tries < 64; ++tries) {
        amin = o.pos + o.bmin * o.scl;
        amax = o.pos + o.bmax * o.scl;
        const Object* hit = nullptr;
        for (const Object& other : g_objs) {
            if (overlapsWorld(o, other)) { hit = &other; break; }
        }
        if (!hit) return shifts;
        const Vec3 hmax = hit->pos + hit->bmax * hit->scl;
        o.pos.x += (hmax.x - amin.x) + 0.25;
        ++shifts;
    }
    std::printf("attention : pas d'emplacement libre trouve, objet laisse en surimpression\n");
    return shifts;
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

    const int shifts = placeImport(o);
    const Vec3 dim = (o.bmax - o.bmin) * o.scl;
    std::printf("import : %s — %zu sommets, %zu tris, dimensions locales %.4g x %.4g x %.4g, echelle %.6g, dimensions affichees %.4g x %.4g x %.4g, volume local %.6g, volume affiche %.6g, aretes non-manifold %zu\n",
                o.name.c_str(), o.v.size(), o.tri.size(),
                o.bmax.x - o.bmin.x, o.bmax.y - o.bmin.y, o.bmax.z - o.bmin.z, o.scl,
                dim.x, dim.y, dim.z, vol, vol * o.scl * o.scl * o.scl, nonManifold);
    if (shifts > 0)
        std::printf("superposition evitee : objet decale de %d pas vers +X, pose a x = %.3f (comme le lot web 2.x)\n",
                    shifts, o.pos.x);
    g_objs.push_back(std::move(o));
    g_selected = (int)g_objs.size() - 1;
    frameSelected();
}

/* Duplication multi-instances (menu contextuel) : copies identiques posees
   vers +X, meme anti-superposition que l'import (decalage juste apres le bord
   de l'objet en collision, marge 0.25). */
static void duplicateObject(int idx, int n) {
    if (idx < 0 || idx >= (int)g_objs.size() || n <= 0) return;
    const Object src = g_objs[(size_t)idx];
    const double spanX = (src.bmax.x - src.bmin.x) * src.scl;
    const double step = spanX + 0.25;
    int made = 0;
    for (int k = 1; k <= n; ++k) {
        Object o = src;
        o.name = src.name + " (" + std::to_string(k) + ")";
        o.pos = Vec3(src.pos.x + step * k, src.pos.y, src.pos.z);
        int shifts = 0;
        for (int tries = 0; tries < 128; ++tries) {
            bool clash = false;
            for (const Object& other : g_objs)
                if (overlapsWorld(o, other)) { clash = true; break; }
            if (!clash) break;
            o.pos.x += step;
            ++shifts;
        }
        if (shifts > 0)
            std::printf("superposition evitee : %s decale de %d pas vers +X (x = %.3f)\n", o.name.c_str(), shifts, o.pos.x);
        g_objs.push_back(std::move(o));
        ++made;
    }
    std::printf("duplication : %s x%d (copies posees vers +X, pas de %.3f)\n", src.name.c_str(), made, step);
}

/* retrait d'instances (retour de test : corriger une sur-duplication sans
   refaire le geste) : -k au champ nombre retire les k dernieres copies
   numerotees de la meme serie que l'objet source (nom "Src (k)"), l'original
   n'est jamais retire ; au-dela du disponible, on retire tout.             */
static int removeInstances(int idx, int k) {
    if (idx < 0 || idx >= (int)g_objs.size() || k <= 0) return 0;
    const std::string base = g_objs[(size_t)idx].name + " (";
    std::vector<std::pair<int, int>> hits;
    for (int i = 0; i < (int)g_objs.size(); ++i) {
        const std::string& nm = g_objs[(size_t)i].name;
        if (nm.size() > base.size() + 1 && nm.compare(0, base.size(), base) == 0 && nm[nm.size() - 1] == ')') {
            const std::string digits = nm.substr(base.size(), nm.size() - base.size() - 1);
            if (!digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos)
                hits.push_back(std::make_pair(i, std::stoi(digits)));
        }
    }
    std::sort(hits.begin(), hits.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
        return a.second != b.second ? a.second > b.second : a.first > b.first;
    });
    const int rem = std::min(k, (int)hits.size());
    for (int r = 0; r < rem; ++r)
        g_objs.erase(g_objs.begin() + hits[(size_t)r].first);
    if (g_selected >= (int)g_objs.size())
        g_selected = (int)g_objs.size() - 1;
    if (rem > 0)
        std::printf("retrait : %d copie(s) supprimee(s) de la serie %s (%d instance(s) restante(s))\n",
                    rem, g_objs[(size_t)idx].name.c_str(), (int)hits.size() - rem);
    return rem;
}

/* saisie du nombre d'instances dans la FENETRE au clavier (modele Push/Pull :
   chiffres puis Entree) — jamais de getline bloquant dans la boucle de jeu */
static bool g_dupPending = false;
static int g_dupSrc = -1;
static std::string g_dupNumBuf;

static void dupStartPrompt() {
    g_dupPending = true;
    g_dupSrc = g_selected;
    g_dupNumBuf.clear();
    std::printf("Dupliquer : tapez le nombre d'instances puis Entree (vide = 1, maximum 999 ; negatif = retire des copies ; Echap pour annuler)\n");
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
/* Fichier projet (.s3projet, texte) + infos objet (menus)             */
/* ------------------------------------------------------------------ */

static std::string trimStr(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool saveProject(const std::string& path, std::string& err) {
    std::ofstream f(path);
    if (!f) { err = "fichier non creable"; return false; }
    f.precision(17);
    f << "s3projet 1\n";
    for (const Object& o : g_objs) {
        f << "objet " << o.name << "\n";
        f << "pos " << o.pos.x << " " << o.pos.y << " " << o.pos.z << "\n";
        f << "scl " << o.scl << "\n";
        f << "col " << o.col[0] << " " << o.col[1] << " " << o.col[2] << "\n";
        f << "verts " << o.v.size() << "\n";
        for (const Vec3& p : o.v) f << p.x << " " << p.y << " " << p.z << "\n";
        f << "tris " << o.tri.size() / 3 << "\n";
        for (size_t i = 0; i + 2 < o.tri.size(); i += 3)
            f << o.tri[i] << " " << o.tri[i + 1] << " " << o.tri[i + 2] << "\n";
    }
    if (!f) { err = "ecriture interrompue"; return false; }
    std::printf("projet enregistre : %s (%zu objets)\n", path.c_str(), g_objs.size());
    return true;
}

static bool loadProject(const std::string& path, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "fichier illisible"; return false; }
    std::string magic;
    int version = 0;
    if (!(f >> magic >> version) || magic != "s3projet" || version != 1) {
        err = "ce fichier n'est pas un projet Sketch3D (ou version non geree)";
        return false;
    }
    std::vector<Object> objs;
    std::string tok;
    while (f >> tok) {
        if (tok != "objet") { err = "fichier corrompu (attendu : objet)"; return false; }
        std::string line;
        std::getline(f, line);
        Object o;
        o.name = trimStr(line);
        if (o.name.empty()) { err = "fichier corrompu (nom d'objet manquant)"; return false; }
        if (!(f >> tok) || tok != "pos" || !(f >> o.pos.x >> o.pos.y >> o.pos.z)
            || !(f >> tok) || tok != "scl" || !(f >> o.scl)
            || !(f >> tok) || tok != "col" || !(f >> o.col[0] >> o.col[1] >> o.col[2])) {
            err = "fichier corrompu (entete d'objet)"; return false;
        }
        size_t nv = 0, nt = 0;
        if (!(f >> tok) || tok != "verts" || !(f >> nv)) {
            err = "fichier corrompu (compte des sommets)"; return false;
        }
        if (nv > 20000000ull) { err = "fichier corrompu (compteurs absurdes)"; return false; }
        o.v.resize(nv);
        for (Vec3& p : o.v)
            if (!(f >> p.x >> p.y >> p.z)) { err = "fichier corrompu (sommets)"; return false; }
        if (!(f >> tok) || tok != "tris" || !(f >> nt)) {
            err = "fichier corrompu (compte des triangles)"; return false;
        }
        if (nt > 40000000ull) { err = "fichier corrompu (compteurs absurdes)"; return false; }
        o.tri.resize(nt * 3);
        for (unsigned& t : o.tri)
            if (!(f >> t)) { err = "fichier corrompu (triangles)"; return false; }
        for (unsigned t : o.tri)
            if (t >= o.v.size()) { err = "fichier corrompu (index de sommet hors bornes)"; return false; }
        computeObjectDerived(o);
        objs.push_back(std::move(o));
    }
    g_objs = std::move(objs);
    g_selected = g_objs.empty() ? -1 : 0;
    std::printf("projet ouvert : %s (%zu objets)\n", path.c_str(), g_objs.size());
    return true;
}

static std::string objectInfo(const Object& o) {
    const Vec3 dim = (o.bmax - o.bmin) * o.scl;
    std::ostringstream s;
    s << o.name << "\n"
      << "position : (" << o.pos.x << ", " << o.pos.y << ", " << o.pos.z << ")\n"
      << "dimensions monde : " << dim.x << " x " << dim.y << " x " << dim.z << "\n"
      << "echelle : " << o.scl << "\n"
      << o.v.size() << " sommets, " << o.tri.size() / 3 << " triangles";
    return s.str();
}

static void showInfoDialog(const std::string& title, const std::string& text) {
#ifdef _WIN32
    MessageBoxA(nullptr, text.c_str(), title.c_str(), MB_OK);
#else
    (void)title;
    std::printf("%s\n", text.c_str());
#endif
}

/* ------------------------------------------------------------------ */
/* Menus natifs : barre Fichier + menu contextuel du clic droit        */
/* ------------------------------------------------------------------ */

enum {
    IDM_NEW_PROJECT = 1,
    IDM_OPEN_PROJECT,
    IDM_SAVE_PROJECT,
    IDM_CLOSE_PROJECT,
    IDM_IMPORT_OBJECT,
    IDM_EXPORT_OBJ,
    IDM_EXPORT_STL,
    IDM_QUIT,
    IDM_INFO_OBJECT,
    IDM_DUPLICATE,
};

static GLFWwindow* g_mainWin = nullptr;

static void handleMenuAction(int id) {
    switch (id) {
    case IDM_NEW_PROJECT:
        g_objs.clear();
        g_selected = -1;
        std::printf("nouveau projet : scene vide\n");
        return;
    case IDM_OPEN_PROJECT: {
        const std::string p = fileDialog(false,
            "Projets (*.s3projet)\0*.s3projet\0Tous (*.*)\0*.*\0\0",
            nullptr, "Ouvrir un projet");
        if (p.empty()) return;
        std::string err;
        if (!loadProject(p, err)) { std::printf("ouverture impossible : %s\n", err.c_str()); return; }
        frameSelected();
        return;
    }
    case IDM_SAVE_PROJECT: {
        const std::string p = fileDialog(true,
            "Projets (*.s3projet)\0*.s3projet\0Tous (*.*)\0*.*\0\0",
            "s3projet", "Enregistrer le projet");
        if (p.empty()) return;
        std::string err;
        if (!saveProject(p, err)) std::printf("enregistrement impossible : %s\n", err.c_str());
        return;
    }
    case IDM_CLOSE_PROJECT:
        g_objs.clear();
        g_selected = -1;
        std::printf("projet ferme\n");
        return;
    case IDM_IMPORT_OBJECT:
        importFile();
        return;
    case IDM_EXPORT_OBJ: {
        const std::string p = fileDialog(true, "Fichier OBJ\0*.obj\0\0", "obj", "Exporter en OBJ");
        if (!p.empty()) exportOBJ(p);
        return;
    }
    case IDM_EXPORT_STL: {
        const std::string p = fileDialog(true, "Fichier STL\0*.stl\0\0", "stl", "Exporter en STL");
        if (!p.empty()) exportSTL(p);
        return;
    }
    case IDM_QUIT:
        if (g_mainWin) glfwSetWindowShouldClose(g_mainWin, GLFW_TRUE);
        return;
    case IDM_INFO_OBJECT:
        if (g_selected >= 0 && g_selected < (int)g_objs.size())
            std::printf("%s\n", objectInfo(g_objs[(size_t)g_selected]).c_str());
        return;
    default:
        return;
    }
}

static void showContextMenu(GLFWwindow* w) {
    /* le menu contextuel ne concerne qu'un objet deja selectionne (clic gauche) */
    if (g_selected < 0 || g_selected >= (int)g_objs.size()) {
        std::printf("menu contextuel : selectionnez d'abord un objet (clic gauche)\n");
        return;
    }
    (void)w;
    const std::string info = objectInfo(g_objs[(size_t)g_selected]);
    std::printf("%s\n", info.c_str());
#ifdef _WIN32
    HMENU m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, IDM_INFO_OBJECT, "&Infos objet");
    AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(m, MF_STRING, IDM_DUPLICATE, "&Dupliquer...");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(glfwGetWin32Window(w));
    const int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, glfwGetWin32Window(w), nullptr);
    DestroyMenu(m);
    if (id == IDM_INFO_OBJECT) showInfoDialog("Infos objet", info);
    else if (id == IDM_DUPLICATE) dupStartPrompt();
#else
    (void)w;
#endif
}

#ifdef _WIN32
static WNDPROC g_baseWndProc = nullptr;

static LRESULT CALLBACK menuWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND) { handleMenuAction((int)LOWORD(wp)); return 0; }
    return CallWindowProc(g_baseWndProc, h, msg, wp, lp);
}
#endif

#ifdef _WIN32
static unsigned g_fontBase = 0;   /* liste d'affichage : fonte bitmap GDI 32..127 */
static HFONT g_hFont = nullptr;   /* fonte conservee : mesure de largeur du texte */

static void buildFont(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    HFONT hf = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                           OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           FF_DONTCARE | DEFAULT_PITCH, "Segoe UI");
    HFONT old = (HFONT)SelectObject(hdc, hf);
    g_fontBase = glGenLists(96);
    if (g_fontBase && g_fontBase != (unsigned)-1) wglUseFontBitmapsA(hdc, 32, 96, g_fontBase);
    SelectObject(hdc, old);
    g_hFont = hf;               /* pas de DeleteObject : sert a textWidthPx */
    ReleaseDC(hwnd, hdc);
}

static int textWidthPx(const char* s) {
    if (!g_hFont) return 0;
    HDC hdc = wglGetCurrentDC();
    if (!hdc) return 0;
    HGDIOBJ old = SelectObject(hdc, g_hFont);
    SIZE sz = {0, 0};
    const int n = (int)std::strlen(s);
    const int ok = n > 0 ? GetTextExtentPoint32A(hdc, s, n, &sz) : 1;
    SelectObject(hdc, old);
    return ok ? sz.cx : 0;
}
#endif

static void drawText2D(float x, float y, const char* s) {
#ifdef _WIN32
    if (!g_fontBase) return;
    glRasterPos2f(x, y);
    for (const char* p = s; *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        glCallList(g_fontBase + (c >= 32 && c < 128 ? c - 32 : 32));
    }
#else
    (void)x; (void)y; (void)s;
#endif
}

static void attachMenus(GLFWwindow* win) {
    g_mainWin = win;
#ifdef _WIN32
    const HWND hwnd = glfwGetWin32Window(win);
    buildFont(hwnd);
    HMENU file = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, IDM_NEW_PROJECT, "&Nouveau projet");
    AppendMenuA(file, MF_STRING, IDM_OPEN_PROJECT, "&Ouvrir un projet...");
    AppendMenuA(file, MF_STRING, IDM_SAVE_PROJECT, "&Enregistrer le projet...");
    AppendMenuA(file, MF_STRING, IDM_CLOSE_PROJECT, "&Fermer le projet");
    AppendMenuA(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(file, MF_STRING, IDM_IMPORT_OBJECT, "&Importer un objet...");
    AppendMenuA(file, MF_STRING, IDM_EXPORT_OBJ, "Exporter en &OBJ...");
    AppendMenuA(file, MF_STRING, IDM_EXPORT_STL, "Exporter en &STL...");
    AppendMenuA(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(file, MF_STRING, IDM_QUIT, "&Quitter");
    HMENU bar = CreateMenu();
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&Fichier");
    SetMenu(hwnd, bar);
    g_baseWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)menuWndProc);
#else
    std::printf("menus natifs : barre Fichier indisponible hors Windows\n");
#endif
}

/* ------------------------------------------------------------------ */
/* Push/Pull (transcription des lots web 3.x, etat final 3.10.3)       */
/* ------------------------------------------------------------------ */

struct Extrude {
    int obj = -1;                    /* index dans g_objs */
    Vec3 normal, planePoint;         /* monde ; normal pointe hors de la matiere */
    Vec3 pn;                         /* normale du plan de souris (projetee camera) */
    std::vector<Vec3> base;          /* copie des sommets locaux a la capture */
    std::vector<unsigned> tri;       /* copie des tris a la capture */
    std::vector<unsigned> vset;      /* sommets de la face (patch) */
    std::vector<size_t> patchTris;   /* tris du patch */
    std::vector<std::pair<unsigned, unsigned>> boundary; /* perimetre du patch */
    std::vector<char> inCompVert;    /* sommets de la composante connexe du patch (vide = tout le mesh) */
    std::vector<char> inCompTri;     /* tris de cette composante : butee, pli et feuille ne la concernent qu'elle */
    double dLow = 0.0, dHigh = 0.0;  /* butees : epaisseur de matiere disponible */
    bool hasLow = false, hasHigh = false;
    double d = 0.0;                  /* dernier deplacement applique (relatif a la capture) */
    double dRef = 0.0;               /* d brut mesure au rayon de capture : ancre du drag */
    std::string numBuf;              /* saisie de dimension exacte */
};
static bool g_ppMode = false;        /* touche P : mode Push/Pull */
static bool g_ppActive = false;      /* une face est capturee, en attente de pose */
static Extrude g_pp;
static double g_ppStatusTick = 0.0;

static bool makeExtrude(const Object& o, size_t clickedTri, Extrude& dd) {
    const size_t triCount = o.tri.size() / 3;
    if (o.tri.size() < 9 || clickedTri >= triCount) return false;
    const Vec3 n0raw = o.n[clickedTri];
    if (!(dot(n0raw, n0raw) > 0.5)) return false;
    const Vec3 n0 = n0raw;
    auto worldV = [&o](unsigned i) { return o.pos + o.v[i] * o.scl; };
    const Vec3 spanW = (o.bmax - o.bmin) * o.scl;
    const double sizeW = std::max(std::max(spanW.x, spanW.y), spanW.z);
    const double epsW = 1e-6 * std::max(1.0, sizeW);
    const Vec3 planePoint = worldV(o.tri[clickedTri * 3]);

    auto ekey = [](unsigned a, unsigned b) {
        return a < b ? ((unsigned long long)a << 32) | b : ((unsigned long long)b << 32) | a;
    };
    std::unordered_map<unsigned long long, std::vector<size_t>> edgeTris;
    for (size_t t = 0; t < triCount; ++t)
        for (int k = 0; k < 3; ++k)
            edgeTris[ekey(o.tri[t * 3 + k], o.tri[t * 3 + (k + 1) % 3])].push_back(t);

    /* patch = tris coplanaires connectes par aretes (BFS, comme le web) */
    std::vector<char> inPatch(triCount, 0);
    std::vector<size_t> pile{clickedTri};
    inPatch[clickedTri] = 1;
    while (!pile.empty()) {
        const size_t t = pile.back();
        pile.pop_back();
        for (int k = 0; k < 3; ++k)
            for (size_t t2 : edgeTris[ekey(o.tri[t * 3 + k], o.tri[t * 3 + (k + 1) % 3])]) {
                if (inPatch[t2]) continue;
                if (std::fabs(dot(o.n[t2], n0)) < 0.999) continue;
                inPatch[t2] = 1;
                pile.push_back(t2);
            }
    }
    for (size_t t = 0; t < triCount; ++t)
        if (inPatch[t])
            for (int k = 0; k < 3; ++k)
                if (std::fabs(dot(worldV(o.tri[t * 3 + k]) - planePoint, n0)) > epsW) return false;

    std::vector<unsigned> vset;
    std::vector<char> seen(o.v.size(), 0);
    std::vector<size_t> patchTris;
    for (size_t t = 0; t < triCount; ++t) {
        if (!inPatch[t]) continue;
        patchTris.push_back(t);
        for (int k = 0; k < 3; ++k) {
            const unsigned id = o.tri[t * 3 + k];
            if (!seen[id]) { seen[id] = 1; vset.push_back(id); }
        }
    }
    std::unordered_map<unsigned long long, int> ecount;
    for (size_t t : patchTris)
        for (int k = 0; k < 3; ++k) ++ecount[ekey(o.tri[t * 3 + k], o.tri[t * 3 + (k + 1) % 3])];
    std::vector<std::pair<unsigned, unsigned>> boundary;
    for (const auto& kv : ecount)
        if (kv.second == 1)
            boundary.emplace_back((unsigned)(kv.first >> 32), (unsigned)(kv.first & 0xffffffffULL));

    /* composante connexe du patch : tous les tris joignables par aretes, meme
       non coplanaires (un voisin a pu etre plie par une extrusion precedente).
       Les aretes sont reconnues par POSITION (quantification 1e-6 relatif) et
       non par index : une face a coins dupliques (STL non soude) ou une feuille
       a deux windings index-disjoints restent rattachees a leur coque ; une coque
       disjointe (silhouette dans un mesh SketchUp) ne l'est pas. Butee, pli
       rentrant, feuille et recreation ne touchent que cette composante. */
    std::vector<char> inCompTri(triCount, 0);
    {
        std::unordered_map<unsigned long long, unsigned> gcells;
        std::vector<unsigned> gv(o.v.size());
        const double tolG = 1e-6 * std::max(1.0, sizeW);
        for (size_t i = 0; i < o.v.size(); ++i) {
            const Vec3 wx = worldV((unsigned)i);
            const long long cx = (long long)std::floor(wx.x / tolG);
            const long long cy = (long long)std::floor(wx.y / tolG);
            const long long cz = (long long)std::floor(wx.z / tolG);
            const unsigned long long ckey = ((unsigned long long)(cx + (1LL << 20)) << 42)
                                          | ((unsigned long long)(cy + (1LL << 20)) << 21)
                                          |  (unsigned long long)(cz + (1LL << 20));
            auto it = gcells.find(ckey);
            if (it == gcells.end()) { gv[i] = (unsigned)gcells.size(); gcells.emplace(ckey, gv[i]); }
            else gv[i] = it->second;
        }
        std::unordered_map<unsigned long long, std::vector<size_t>> edgeTrisG;
        for (size_t t = 0; t < triCount; ++t)
            for (int k = 0; k < 3; ++k)
                edgeTrisG[ekey(gv[o.tri[t * 3 + k]], gv[o.tri[t * 3 + (k + 1) % 3]])].push_back(t);
        std::vector<size_t> cpile(patchTris.begin(), patchTris.end());
        for (size_t t : patchTris) inCompTri[t] = 1;
        while (!cpile.empty()) {
            const size_t t = cpile.back();
            cpile.pop_back();
            for (int k = 0; k < 3; ++k)
                for (size_t t2 : edgeTrisG[ekey(gv[o.tri[t * 3 + k]], gv[o.tri[t * 3 + (k + 1) % 3]])]) {
                    if (inCompTri[t2]) continue;
                    inCompTri[t2] = 1;
                    cpile.push_back(t2);
                }
        }
    }
    std::vector<char> inCompVert(o.v.size(), 0);
    for (size_t t = 0; t < triCount; ++t) {
        if (!inCompTri[t]) continue;
        for (int k = 0; k < 3; ++k) inCompVert[o.tri[t * 3 + k]] = 1;
    }

    double dLow = 0.0, dHigh = 0.0;
    bool hasLow = false, hasHigh = false;
    const double tiny = 1e-7 * std::max(1.0, sizeW);
    for (unsigned i = 0; i < o.v.size(); ++i) {
        if (seen[i] || !inCompVert[i]) continue;   /* butee = epaisseur de la coque capturee, pas celle des objets voisins */
        const double along = dot(worldV(i) - planePoint, n0);
        if (along < -tiny) { if (!hasLow || along < dLow) { dLow = along; hasLow = true; } }
        if (along >  tiny) { if (!hasHigh || along > dHigh) { dHigh = along; hasHigh = true; } }
    }
    bool flip = hasHigh && !hasLow;
    if (!hasLow && !hasHigh && dot(n0, g_cam.pos() - planePoint) < 0.0) {
        /* feuille : les deux couches coïncident, le rayon capture l'une des deux
           windings indifféremment ; on oriente la normale du cote de la camera
           pour que le pull redeplie la matiere du cote visible (le push passe
           de l'autre cote, comme SketchUp) */
        flip = true;
    }

    dd.normal = flip ? n0 * -1.0 : n0;
    dd.planePoint = planePoint;
    dd.base = o.v;
    dd.tri = o.tri;
    dd.vset = std::move(vset);
    dd.patchTris = std::move(patchTris);
    dd.boundary = std::move(boundary);
    dd.dLow = flip ? -dHigh : dLow;   dd.hasLow  = flip ? hasHigh : hasLow;
    dd.dHigh = flip ? -dLow : dHigh;  dd.hasHigh = flip ? hasLow : hasHigh;
    if (!hasLow && !hasHigh) {
        /* feuille a la capture : aucune matiere des deux cotes, tout est
           consomme. Le push n'a rien a enfoncer : bloque au plan (SketchUp
           cale le push-pull a la surface opposee), seul le pull redeplie la
           matiere du cote camera. */
        dd.dLow = 0.0;
        dd.hasLow = true;
    }
    dd.inCompVert = std::move(inCompVert);
    dd.inCompTri = std::move(inCompTri);
    dd.d = 0.0;
    return true;
}

static void applyExtrude(Object& o, const Extrude& dd, double d) {
    if (dd.hasLow  && d < dd.dLow)  d = dd.dLow;
    if (dd.hasHigh && d > dd.dHigh) d = dd.dHigh;
    const Vec3 n = dd.normal;
    const Vec3 dl = n * (d / o.scl);
    const Vec3 ppL = (dd.planePoint - o.pos) * (1.0 / o.scl);

    /* aMin/aMax sur la seule composante capturee : la detection de feuille et
       les butees ne doivent pas dependre d'une coque disjointe du meme mesh.
       La bbox w0/w1 reste globale (taille de reference pour les seuils). */
    double aMin = 1e300, aMax = -1e300;
    Vec3 w0(1e300, 1e300, 1e300), w1(-1e300, -1e300, -1e300);
    for (size_t i = 0; i < dd.base.size(); ++i) {
        const Vec3& p = dd.base[i];
        if (dd.inCompVert.empty() || dd.inCompVert[i]) {
            const double al = dot(p - ppL, n);
            if (al < aMin) aMin = al;
            if (al > aMax) aMax = al;
        }
        const Vec3 w = p * o.scl;
        if (w.x < w0.x) w0.x = w.x;
        if (w.y < w0.y) w0.y = w.y;
        if (w.z < w0.z) w0.z = w.z;
        if (w.x > w1.x) w1.x = w.x;
        if (w.y > w1.y) w1.y = w.y;
        if (w.z > w1.z) w1.z = w.z;
    }
    const double sizeW = std::max(std::max(w1.x - w0.x, w1.y - w0.y), w1.z - w0.z);

    if ((aMax - aMin) <= 1e-3 * std::max(sizeW, 1e-12)) {
        /* feuille aplatie a la butee : le pull recree du volume. Fond = une seule
           couche a la position d'origine (sommets regroupes en clusters, doublons
           coplanaires fondus, winding normalise oppose au deplacement), couche
           deplacee = copie inversee translatée, jupe = perimetre de la couche. */
        double mx = 1e-9;
        for (const Vec3& p : dd.base)
            mx = std::max(mx, std::max(std::fabs(p.x), std::max(std::fabs(p.y), std::fabs(p.z))));
        const double tol = 1e-6 * mx, t2 = tol * tol;   /* |floor(p/tol)| <= 1e6 < 2^20 : codage cellKey garanti */
        auto cellKey = [](long long cx, long long cy, long long cz) {
            return ((unsigned long long)(cx + (1LL << 20)) << 42)
                 | ((unsigned long long)(cy + (1LL << 20)) << 21)
                 |  (unsigned long long)(cz + (1LL << 20));
        };
        std::unordered_map<unsigned long long, std::vector<unsigned>> cells;
        std::vector<unsigned> cid(dd.base.size());
        std::vector<Vec3> rep;
        for (size_t i = 0; i < dd.base.size(); ++i) {
            if (!dd.inCompVert.empty() && !dd.inCompVert[i]) continue;   /* hors composante : pas regroupe, reemis tel quel */
            const Vec3 p = dd.base[i];
            const long long cx = (long long)std::floor(p.x / tol), cy = (long long)std::floor(p.y / tol), cz = (long long)std::floor(p.z / tol);
            unsigned repId = 0;
            bool found = false;
            for (long long u = -1; u <= 1 && !found; ++u)
                for (long long w = -1; w <= 1 && !found; ++w)
                    for (long long q = -1; q <= 1 && !found; ++q) {
                        auto it = cells.find(cellKey(cx + u, cy + w, cz + q));
                        if (it == cells.end()) continue;
                        for (unsigned r : it->second) {
                            const Vec3 d3 = rep[r] - p;
                            if (dot(d3, d3) <= t2) { repId = r; found = true; break; }
                        }
                    }
            if (!found) {
                repId = (unsigned)rep.size();
                rep.push_back(p);
                cells[cellKey(cx, cy, cz)].push_back(repId);
            }
            cid[i] = repId;
        }
        std::vector<std::array<unsigned, 3>> trisB;
        std::set<std::array<unsigned, 3>> seenT;
        double sgn = 0.0;
        for (size_t t = 0; t < dd.tri.size(); t += 3) {
            if (!dd.inCompTri.empty() && !dd.inCompTri[t / 3]) continue;   /* coques disjointes : pas dans la feuille */
            std::array<unsigned, 3> ids = {cid[dd.tri[t]], cid[dd.tri[t + 1]], cid[dd.tri[t + 2]]};
            const Vec3 p0 = rep[ids[0]], p1 = rep[ids[1]], p2 = rep[ids[2]];
            const Vec3 e1 = p1 - p0, e2 = p2 - p0, e3 = p0 - p2;
            const Vec3 cr = cross(e1, e2);
            const double m1 = dot(e1, e1), m2 = dot(e2, e2), m3 = dot(e3, e3);
            const double mm = std::max(m1, std::max(m2, m3));
            if (dot(cr, cr) <= 1e-7 * mm * mm) continue;
            if (dot(cr, n) <= 0.0) continue;
            std::array<unsigned, 3> key = ids;
            std::sort(key.begin(), key.end());
            if (!seenT.insert(key).second) continue;
            sgn += dot(cross(e1, e2), dl);
            trisB.push_back(ids);
        }
        if (sgn > 0.0)
            for (auto& ids : trisB) std::swap(ids[1], ids[2]);
        /* reconstruction : les coques hors composante survivent telles quelles
           (remap identite sur leurs sommets), la composante capturee est
           reconstituee en coque fermee : fond + face deplacee + jupe. */
        std::vector<unsigned> remap(dd.base.size(), 0);
        o.tri.clear();
        if (dd.inCompVert.empty()) {
            for (size_t i = 0; i < cid.size(); ++i) remap[i] = cid[i];
            o.v = rep;
        } else {
            o.v.clear();
            o.v.reserve(dd.base.size() + rep.size());
            for (size_t i = 0; i < dd.base.size(); ++i)
                if (!dd.inCompVert[i]) { remap[i] = (unsigned)o.v.size(); o.v.push_back(dd.base[i]); }
            const unsigned repOff = (unsigned)o.v.size();
            for (const Vec3& p : rep) o.v.push_back(p);
            for (size_t i = 0; i < dd.base.size(); ++i)
                if (dd.inCompVert[i]) remap[i] = repOff + cid[i];
            for (auto& ids : trisB) { ids[0] += repOff; ids[1] += repOff; ids[2] += repOff; }
        }
        for (size_t t = 0; t < dd.tri.size(); t += 3) {
            if (!dd.inCompTri.empty() && dd.inCompTri[t / 3]) continue;
            o.tri.push_back(remap[dd.tri[t]]);
            o.tri.push_back(remap[dd.tri[t + 1]]);
            o.tri.push_back(remap[dd.tri[t + 2]]);
        }
        if (std::fabs(d) < 1e-9) {
            /* couche seule : les deux windings, comme l'etat de butee (le pli
               rentrant laisse 2 windings coplanaires) — volume exactement nul,
               feuille visible des deux cotes, pas de pop visuel en cours de drag */
            for (const auto& ids : trisB) {
                o.tri.push_back(ids[0]); o.tri.push_back(ids[1]); o.tri.push_back(ids[2]);
            }
            for (const auto& ids : trisB) {
                o.tri.push_back(ids[2]); o.tri.push_back(ids[1]); o.tri.push_back(ids[0]);
            }
            computeObjectDerived(o);
            return;
        }
        std::unordered_map<unsigned, unsigned> topId;
        auto topOf = [&](unsigned b) {   /* b : index du sommet du fond dans o.v */
            auto it = topId.find(b);
            if (it != topId.end()) return it->second;
            const unsigned id = (unsigned)o.v.size();
            o.v.push_back(o.v[b] + dl);
            topId.emplace(b, id);
            return id;
        };
        for (const auto& ids : trisB) {
            o.tri.push_back(ids[0]); o.tri.push_back(ids[1]); o.tri.push_back(ids[2]);
        }
        for (const auto& ids : trisB) {
            const unsigned ta = topOf(ids[2]), tb = topOf(ids[1]), tc = topOf(ids[0]);
            o.tri.push_back(ta); o.tri.push_back(tb); o.tri.push_back(tc);
        }
        std::unordered_map<unsigned long long, std::pair<unsigned, unsigned>> edir;
        for (const auto& ids : trisB)
            for (int k = 0; k < 3; ++k) {
                const unsigned a = ids[k], b = ids[(k + 1) % 3];
                const unsigned long long key = a < b ? ((unsigned long long)a << 32) | b : ((unsigned long long)b << 32) | a;
                auto it = edir.find(key);
                if (it == edir.end()) edir.emplace(key, std::make_pair(a, b));
                else it->second = std::make_pair(0u, 0u);
            }
        for (const auto& kv : edir) {
            if (kv.second.first == 0 && kv.second.second == 0) continue;
            const unsigned a = kv.second.first, b = kv.second.second;
            const unsigned ta = topOf(a), tb = topOf(b);
            o.tri.push_back(b); o.tri.push_back(a); o.tri.push_back(ta);
            o.tri.push_back(b); o.tri.push_back(ta); o.tri.push_back(tb);
        }
        computeObjectDerived(o);
        return;
    }

    if (d <= 0.0 && !dd.hasHigh) {
        /* push rentrant : pas d'interpolation. Tout ce qui est au-dela du plan
           est replie exactement sur le plan (translation ne dependant que du
           sommet), les tris du patch restent, les aretesjointes s'alignent : coque
           fermee par construction. */
        const double tiny = 1e-7 * std::max(1.0, sizeW);
        std::vector<Vec3> nv(dd.base.size());
        std::vector<char> out(dd.base.size(), 0);
        for (size_t i = 0; i < dd.base.size(); ++i) {
            const bool comp = dd.inCompVert.empty() || dd.inCompVert[i] != 0;   /* jamais plier une coque disjointe */
            const double al = dot(dd.base[i] * o.scl - (dd.planePoint - o.pos), n);
            out[i] = (comp && al > d + tiny) ? 1 : 0;
            nv[i] = out[i] ? dd.base[i] + n * ((d - al) / o.scl) : dd.base[i];
        }
        std::vector<char> isPatch(dd.tri.size() / 3, 0);   /* indexe par les positions de dd.tri, pas par o.tri (rappels a chaque frame) */
        for (size_t t : dd.patchTris) isPatch[t] = 1;
        o.tri.clear();
        o.tri.reserve(dd.tri.size());
        std::unordered_map<unsigned, unsigned> cloneOf;
        for (size_t t = 0; t < dd.tri.size(); t += 3) {
            const bool compTri = dd.inCompTri.empty() || dd.inCompTri[t / 3];
            const bool all = out[dd.tri[t]] && out[dd.tri[t + 1]] && out[dd.tri[t + 2]];
            if (all && !isPatch[t / 3] && compTri) continue;
            if (!compTri) {
                /* coque hors composante : geometrie figee. Un sommet partage avec
                   la coque padsee (soudure du fichier source) est clone a sa
                   position d'origine, jamais plie : sinon le tri voisin se
                   deformait jusqu'a devenir degenere et disparaissait. */
                unsigned c[3];
                for (int k = 0; k < 3; ++k) {
                    const unsigned v = dd.tri[t + k];
                    if (!out[v]) { c[k] = v; continue; }
                    auto it = cloneOf.find(v);
                    if (it == cloneOf.end()) {
                        const unsigned id = (unsigned)nv.size();
                        nv.push_back(dd.base[v]);
                        it = cloneOf.emplace(v, id).first;
                    }
                    c[k] = it->second;
                }
                o.tri.push_back(c[0]); o.tri.push_back(c[1]); o.tri.push_back(c[2]);
                continue;
            }
            const Vec3 v0 = nv[dd.tri[t]], v1 = nv[dd.tri[t + 1]], v2 = nv[dd.tri[t + 2]];
            const Vec3 e1 = v1 - v0, e2 = v2 - v0, e3 = v0 - v2;
            const Vec3 cr = cross(e1, e2);
            const double mm = std::max(dot(e1, e1), std::max(dot(e2, e2), dot(e3, e3)));
            if (dot(cr, cr) <= 1e-7 * mm * mm) continue;
            o.tri.push_back(dd.tri[t]); o.tri.push_back(dd.tri[t + 1]); o.tri.push_back(dd.tri[t + 2]);
        }
        o.v = std::move(nv);
        computeObjectDerived(o);
        return;
    }

    if (std::fabs(d) < 1e-9) {
        o.v = dd.base;
        o.tri = dd.tri;
        computeObjectDerived(o);
        return;
    }

    /* tirage : la face bouge ; les aretes de son perimetre partagees avec une
       paroi voisine etirent celle-ci (indices partages, rien a creer) ; une
       arete de bord non partagee recoit une jupe (double couche de la source) */
    std::unordered_map<unsigned long long, int> globalEcount;
    for (size_t t = 0; t < dd.tri.size(); t += 3)
        for (int k = 0; k < 3; ++k) {
            unsigned a = dd.tri[t + k], b = dd.tri[t + (k + 1) % 3];
            const unsigned long long key = a < b ? ((unsigned long long)a << 32) | b : ((unsigned long long)b << 32) | a;
            ++globalEcount[key];
        }
    o.v = dd.base;
    for (unsigned i : dd.vset) o.v[i] = o.v[i] + dl;
    o.tri = dd.tri;
    for (const auto& e : dd.boundary) {
        const unsigned long long key = e.first < e.second
            ? ((unsigned long long)e.first << 32) | e.second
            : ((unsigned long long)e.second << 32) | e.first;
        auto it = globalEcount.find(key);
        if (it != globalEcount.end() && it->second > 1) continue;
        const unsigned ba = (unsigned)o.v.size(); o.v.push_back(dd.base[e.first]);
        const unsigned bb = (unsigned)o.v.size(); o.v.push_back(dd.base[e.second]);
        o.tri.push_back(ba); o.tri.push_back(bb); o.tri.push_back(e.second);
        o.tri.push_back(ba); o.tri.push_back(e.second); o.tri.push_back(e.first);
    }
    computeObjectDerived(o);
}

static void ppCancel() {
    if (!g_ppActive || g_pp.obj < 0 || g_pp.obj >= (int)g_objs.size()) { g_ppActive = false; return; }
    Object& o = g_objs[(size_t)g_pp.obj];
    o.v = g_pp.base;
    o.tri = g_pp.tri;
    computeObjectDerived(o);
    g_ppActive = false;
    g_pp = Extrude();
    std::printf("\nextrusion annulee\n");
}

static void ppCommit() {
    std::printf("\nface posee — extrusion : %.3f\n", g_pp.d);
    g_ppActive = false;
    g_pp = Extrude();
}

/* d relatif au rayon de capture : d = 0 au curseur de la capture, positif
   vers la normale (tirage). Pur : lit g_pp, ne touche pas au mesh.
   NaN si le rayon perce ailleurs sans croiser le plan utile.              */
static double ppDragDelta(const Vec3& org, const Vec3& dir) {
    const double denom = dot(dir, g_pp.pn);
    if (std::fabs(denom) < 1e-9) return std::numeric_limits<double>::quiet_NaN();
    const double t = dot(g_pp.planePoint - org, g_pp.pn) / denom;
    if (t < 0.0) return std::numeric_limits<double>::quiet_NaN();
    return dot((org + dir * t) - g_pp.planePoint, g_pp.normal) - g_pp.dRef;
}

static void updatePP(GLFWwindow* w) {
    if (!g_ppActive || g_pp.obj < 0 || g_pp.obj >= (int)g_objs.size()) return;
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return;
    /* ancre dRef : d = 0 au rayon de capture, la face suit ensuite le curseur.
       Une dimension tapee + Entree pose ET termine (voir onKey) : pas besoin
       de verrou, le curseur n'ecrase jamais une valeur posee.              */
    const double d = ppDragDelta(org, dir);
    if (!std::isfinite(d)) return;
    applyExtrude(g_objs[(size_t)g_pp.obj], g_pp, d);
    g_pp.d = d;
    const double now = glfwGetTime();
    if (now - g_ppStatusTick > 0.12) {
        g_ppStatusTick = now;
        std::printf("\rExtrusion : %+.3f (clic pour poser, clic droit pour annuler)   ", d);
        std::fflush(stdout);
    }
}

/* ------------------------------------------------------------------ */
/* Rotation (outillage T1) : autour d'un axe monde passant par le      */
/* centre de la piece ; mouvement souris en direct ou angle au clavier */
/* ------------------------------------------------------------------ */

static bool g_rotMode = false;        /* touche R : mode Rotation */
static bool g_rotActive = false;      /* axe choisi, angle en cours */
static int g_rotObj = -1;
static int g_rotAxis = -1;            /* 0 = X, 1 = Y, 2 = Z */
static Vec3 g_rotPivot;               /* centre (monde) de la piece a la capture */
static std::vector<Vec3> g_rotBase;   /* sommets locaux a la capture */
static double g_rotAngle = 0.0;       /* angle courant (degres) */
static double g_rotLastX = 0.0;       /* curseur a la capture (0.25 deg/pixel) */
static std::string g_rotNumBuf;
static double g_rotStatusTick = 0.0;

/* rotation d'angle (degres) autour de l'axe monde 'axis' passant par pivot */
static Vec3 rotateAroundAxis(const Vec3& pw, const Vec3& pivot, int axis, double angDeg) {
    const double a = angDeg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    Vec3 p = pw - pivot;
    Vec3 r;
    if (axis == 0)      r = Vec3(p.x, c * p.y - s * p.z, s * p.y + c * p.z);
    else if (axis == 1) r = Vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    else                r = Vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
    return r + pivot;
}

/* applique l'angle : v local = (pivot + R*(pos + base*scl - pivot) - pos)/scl */
static void applyRotationAt(Object& o, const std::vector<Vec3>& base, int axis, double angDeg) {
    if (o.scl <= 1e-12) return;
    const double inv = 1.0 / o.scl;
    o.v.resize(base.size());
    for (size_t i = 0; i < base.size(); ++i) {
        const Vec3 w = o.pos + base[i] * o.scl;
        o.v[i] = (rotateAroundAxis(w, g_rotPivot, axis, angDeg) - o.pos) * inv;
    }
    computeObjectDerived(o);
}

static char axisLetterFromScancode(int scancode) {
    const char* name = glfwGetKeyName(-1, scancode);
    if (!name || name[0] == '\0') return 0;
    const char c = (name[0] >= 'A' && name[0] <= 'Z') ? char(name[0] - 'A' + 'a') : name[0];
    return (c == 'x' || c == 'y' || c == 'z') ? c : 0;
}

static void rotBegin(GLFWwindow* w, int axis) {
    if (g_selected < 0 || g_selected >= (int)g_objs.size()) {
        std::printf("rotation : selectionnez d'abord un objet (clic gauche)\n");
        return;
    }
    Object& o = g_objs[(size_t)g_selected];
    g_rotObj = g_selected;
    g_rotAxis = axis;
    g_rotBase = o.v;
    g_rotPivot = o.pos + (o.bmin + o.bmax) * (0.5 * o.scl);
    g_rotAngle = 0.0;
    g_rotNumBuf.clear();
    double dummy = 0.0;
    glfwGetCursorPos(w, &g_rotLastX, &dummy);
    g_rotActive = true;
    std::printf("rotation %s de %s autour de son centre : souris pour tourner, ou chiffres + Entree (tapez dans la fenetre ; Entree pose et quitte)\n",
                axisName(axis), o.name.c_str());
}

static void rotCancel() {
    if (!g_rotActive || g_rotObj < 0 || g_rotObj >= (int)g_objs.size()) { g_rotActive = false; return; }
    Object& o = g_objs[(size_t)g_rotObj];
    o.v = g_rotBase;
    computeObjectDerived(o);
    g_rotActive = false;
    g_rotAxis = -1;
    g_rotNumBuf.clear();
    std::printf("\nrotation annulee\n");
}

static void rotCommit() {
    if (!g_rotActive || g_rotObj < 0 || g_rotObj >= (int)g_objs.size()) { g_rotActive = false; return; }
    std::printf("\nrotation posee : %+.2f deg sur %s\n", g_rotAngle, g_objs[(size_t)g_rotObj].name.c_str());
    g_rotActive = false;
    g_rotAxis = -1;
    g_rotNumBuf.clear();
}

static void updateRot(GLFWwindow* w) {
    if (!g_rotActive || g_rotObj < 0 || g_rotObj >= (int)g_objs.size()) return;
    /* saisie au clavier en cours : la piece est figee (le moindre mouvement
       de souris ne modifie plus l'angle accumule) ; Entree pose et quitte. */
    if (!g_rotNumBuf.empty()) return;
    double x, y;
    glfwGetCursorPos(w, &x, &y);
    g_rotAngle += (x - g_rotLastX) * 0.25;
    g_rotLastX = x;
    applyRotationAt(g_objs[(size_t)g_rotObj], g_rotBase, g_rotAxis, g_rotAngle);
    const double now = glfwGetTime();
    if (now - g_rotStatusTick > 0.12) {
        g_rotStatusTick = now;
        std::printf("\rRotation %s : %+.1f deg (clic pour poser, clic droit pour annuler)   ", axisName(g_rotAxis), g_rotAngle);
        std::fflush(stdout);
    }
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
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        /* regle generale des transformations : clic droit = retour au neutre.
           Le deplacement en vol ne peut pas etre annule ainsi (le clic droit
           exige de relacher le bouton gauche, donc l'objet est deja pose). */
        if (action != GLFW_PRESS) return;
        if (g_ppActive) {
            ppCancel();                 /* restaure la piece */
            g_ppMode = false;
            std::printf("\nmode selection\n");
        } else if (g_rotActive) {
            rotCancel();                /* restaure la geometrie */
            g_rotMode = false;
            std::printf("\nmode selection\n");
        } else if (g_mode == M_Drag) {
            g_mode = M_None;
            g_lockAxis = -1;
        } else if (g_ppMode || g_rotMode) {
            g_ppMode = false;
            g_rotMode = false;
            std::printf("\nmode selection\n");
        } else {
            showContextMenu(w);
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        if (g_ppActive) { ppCommit(); return; }   /* second clic gauche : pose la face */
        if (g_rotActive) { rotCommit(); return; }  /* clic gauche : pose la rotation */
        if (g_ppMode) {
            Vec3 org, dir;
            if (!rayFromMouse(w, org, dir)) return;
            int hit = -1;
            double bestT = 1e300;
            size_t bestTri = 0;
            for (size_t i = 0; i < g_objs.size(); ++i) {
                double t;
                size_t ti;
                if (rayObjectTri(org, dir, g_objs[i], t, &ti) && t < bestT) {
                    bestT = t; hit = (int)i; bestTri = ti;
                }
            }
            if (hit < 0) return;
            g_selected = hit;
            Extrude dd;
            if (!makeExtrude(g_objs[(size_t)hit], bestTri, dd)) {
                std::printf("Face non plane — Push/Pull indisponible sur cette surface.\n");
                return;
            }
            dd.obj = hit;
            const Vec3 camDir = g_cam.forward();
            Vec3 pn = camDir - dd.normal * dot(camDir, dd.normal);
            if (dot(pn, pn) < 0.04) {
                if (std::fabs(dd.normal.y) > 0.9) pn = Vec3(1, 0, 0);
                else pn = Vec3(dd.normal.z, 0, -dd.normal.x);
            }
            dd.pn = pn;      /* plan de souris : passe par la face, face a l ecran */
            /* ancre du drag : d brut mesure au rayon de capture (le curseur
               vient de cliquer, la face doit rester en place tant qu'il bouge
               a peine) ; sinon le d initial, non nul, etait clampé a la butee */
            {
                const double den = dot(dir, dd.pn);
                if (std::fabs(den) > 1e-9) {
                    const double ts = dot(dd.planePoint - org, dd.pn) / den;
                    if (ts > 0.0) dd.dRef = dot((org + dir * ts) - dd.planePoint, dd.normal);
                }
            }
            dd.d = 0.0;
            g_pp = std::move(dd);
            g_ppActive = true;
            std::printf("face capturee — deplacez le long de la normale, cliquez pour poser la face (clic droit ou Echap : annuler ; chiffres + Entree : dimension exacte)\n");
            return;
        }
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
    if (g_dupPending) {
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        const bool topRow = key >= GLFW_KEY_0 && key <= GLFW_KEY_9;
        const bool padRow = key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9;
        if (topRow || padRow) {
            if (g_dupNumBuf.size() < 4)
                g_dupNumBuf += padRow ? char('0' + (key - GLFW_KEY_KP_0)) : char(key);
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            if (g_dupNumBuf.size() < 4) g_dupNumBuf.insert(g_dupNumBuf.begin(), '-');
        } else if (key == GLFW_KEY_PERIOD || key == GLFW_KEY_COMMA || key == GLFW_KEY_KP_DECIMAL) {
            std::printf("\nchamp nombre entier : separateur ignore (nombre negatif = retire des copies si besoin)\n");
        } else if (key == GLFW_KEY_BACKSPACE) {
            if (!g_dupNumBuf.empty()) g_dupNumBuf.pop_back();
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            const bool vide = g_dupNumBuf.empty();
            const long n = vide ? 1 : std::atol(g_dupNumBuf.c_str());
            g_dupPending = false;
            g_dupNumBuf.clear();
            if (vide || (n >= 1 && n <= 999)) {
                duplicateObject(g_dupSrc, (int)n);
            } else if (n <= -1 && n >= -999) {
                const int rem = removeInstances(g_dupSrc, (int)-n);
                if (rem == 0 && g_dupSrc >= 0 && g_dupSrc < (int)g_objs.size())
                    std::printf("\nretrait : aucune copie numerotee de %s\n", g_objs[(size_t)g_dupSrc].name.c_str());
            } else {
                std::printf("\nNombre invalide : entrez de -999 a 999 puis Entree (negatif = retire des copies)\n");
            }
            return;
        } else if (key == GLFW_KEY_ESCAPE) {
            g_dupPending = false;
            g_dupNumBuf.clear();
            std::printf("\nduplication annulee\n");
        }
        return;
    }
    if (g_rotActive) {
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        const bool topRowR = key >= GLFW_KEY_0 && key <= GLFW_KEY_9;
        const bool padRowR = key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9;
        if (topRowR || padRowR || key == GLFW_KEY_PERIOD || key == GLFW_KEY_COMMA || key == GLFW_KEY_KP_DECIMAL) {
            if (g_rotNumBuf.size() < 16)
                g_rotNumBuf += (padRowR ? char('0' + (key - GLFW_KEY_KP_0))
                              : (topRowR ? char(key) : char('.')));
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            if (g_rotNumBuf.size() < 16) g_rotNumBuf.insert(g_rotNumBuf.begin(), '-');
        } else if (key == GLFW_KEY_BACKSPACE) {
            if (!g_rotNumBuf.empty()) g_rotNumBuf.pop_back();
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            const double a = std::atof(g_rotNumBuf.c_str());
            if (!std::isfinite(a)) {
                std::printf("\nAngle invalide — tapez un nombre (degres, signe autorise) puis Entree.\n");
            } else {
                g_rotAngle = a;
                applyRotationAt(g_objs[(size_t)g_rotObj], g_rotBase, g_rotAxis, a);
                g_rotNumBuf.clear();
                rotCommit();           /* Entree = pose, comme le clic gauche */
                g_rotMode = false;     /* sortie du mode (meme contrat que l'extrusion) */
                std::printf("mode selection\n");
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            rotCancel();                /* premier Echap : annule la rotation, reste en mode */
        } else if (key == GLFW_KEY_R) {
            rotCancel();                /* R disponible a tout moment : quitte le mode */
            g_rotMode = false;
            std::printf("\nmode selection\n");
        } else {
            const char c = axisLetterFromScancode(scancode);   /* autre lettre = pivoter d'axe direct */
            if (c) {
                rotCommit();            /* pose la rotation en cours */
                rotBegin(w, c - 'x');   /* repart de la geometrie courante, angle a zero */
            }
        }
        return;
    }
    if (g_ppActive) {
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        const bool topRowP = key >= GLFW_KEY_0 && key <= GLFW_KEY_9;
        const bool padRowP = key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9;
        if (topRowP || padRowP || key == GLFW_KEY_PERIOD || key == GLFW_KEY_COMMA || key == GLFW_KEY_KP_DECIMAL) {
            if (g_pp.numBuf.size() < 16)
                g_pp.numBuf += (padRowP ? char('0' + (key - GLFW_KEY_KP_0))
                              : (topRowP ? char(key) : char('.')));
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            if (g_pp.numBuf.size() < 16) g_pp.numBuf.insert(g_pp.numBuf.begin(), '-');
        } else if (key == GLFW_KEY_BACKSPACE) {
            if (!g_pp.numBuf.empty()) g_pp.numBuf.pop_back();
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            double v = std::atof(g_pp.numBuf.c_str());
            if (!std::isfinite(v) || v == 0.0) {
                std::printf("\nValeur invalide — tapez un nombre non nul puis Entree (signe : tiret devant = enfoncement).\n");
            } else {
                /* butees (demande de test) : une valeur au-dela de la matiere
                   se cale a la butee au lieu de traverser et ressortir ; si
                   tout l enfoncement est consomme, la face devient feuille. */
                if (g_pp.hasLow && v < g_pp.dLow) {
                    std::printf("\nButee atteinte : valeur calee a %+.3f.\n", g_pp.dLow);
                    v = g_pp.dLow;
                }
                if (g_pp.hasHigh && v > g_pp.dHigh) {
                    std::printf("\nButee atteinte : valeur calee a %+.3f.\n", g_pp.dHigh);
                    v = g_pp.dHigh;
                }
                applyExtrude(g_objs[(size_t)g_pp.obj], g_pp, v);
                g_pp.d = v;
                g_pp.numBuf.clear();
                ppCommit();            /* Entree = pose, comme le second clic gauche */
                g_ppMode = false;      /* sortie du mode P apres application (demande de test) */
                std::printf("mode selection\n");
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            ppCancel();                 /* premier Echap : annule la face, reste en mode */
        } else if (key == GLFW_KEY_P) {
            ppCancel();                 /* P disponible a tout moment : quitte le mode */
            g_ppMode = false;
            std::printf("\nmode selection\n");
        }
        return;
    }
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) {
            if (g_ppMode || g_rotMode) { g_ppMode = false; g_rotMode = false; std::printf("mode selection\n"); return; }
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
        if (key == GLFW_KEY_P && g_mode == M_None && !g_dupPending) {
            g_ppMode = !g_ppMode;
            if (g_ppMode) g_rotMode = false;
            std::printf(g_ppMode
                ? "mode Push/Pull : cliquez une face (dessus ou cote), deplacez, puis re-cliquez pour poser\n"
                : "mode selection\n");
        }
        if (key == GLFW_KEY_R && g_mode == M_None && !g_dupPending) {
            g_rotMode = !g_rotMode;
            if (g_rotMode) g_ppMode = false;
            std::printf(g_rotMode
                ? "mode Rotation : selectionnez un objet, puis X, Y ou Z (lettre) pour fixer l'axe\n"
                : "mode selection\n");
        }
        if (g_rotMode && !g_rotActive) {
            const char c = axisLetterFromScancode(scancode);
            if (c) { rotBegin(w, c - 'x'); return; }
        }
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

/* bandeau de statut en bas de fenetre (petite barre d'infos, façon SketchUp) :
   affiche l'invite du mode courant et la valeur en cours — rien dans la
   console de saisie, tout dans la fenetre */
static std::string statusPrompt() {
    char buf[256];
    if (g_dupPending)
        return "Dupliquer : tapez le nombre d instances puis Entree (vide = 1, negatif = retrait, Echap pour annuler)";
    else if (g_ppActive)
        std::snprintf(buf, sizeof(buf), "Extrusion : %+.3f  (clic gauche : poser ; chiffres puis Entree : dimension exacte, + = tirer / - = pousser ; clic droit ou Echap : annuler)", g_pp.d);
    else if (g_rotActive)
        std::snprintf(buf, sizeof(buf), "Rotation %s : %+.1f deg  (clic gauche : poser ; X/Y/Z : changer d axe ; chiffres puis Entree : angle exact pose et quitte le mode ; clic droit ou Echap : annuler)", axisName(g_rotAxis), g_rotAngle);
    else if (g_ppMode) {
        return "Push/Pull : cliquez une face d'un objet";
    } else if (g_rotMode) {
        return "Rotation : selectionnez un objet, puis la lettre de l'axe (X, Y ou Z)";
    } else {
        return "P : Push/Pull   R : Rotation   clic gauche : selection/deplacement (X/Y/Z tenus = axe)   clic droit : menu   F : cadrer   H : origine   roulette : zoom";
    }
    return buf;
}

/* zone de saisie du bandeau : (libelle, buffer tape) - la meme boite pour
   chaque cas qui accepte une valeur clavier (duplication, extrusion, rotation) */
static bool inputZoneState(const char** label, const std::string** buf) {
    if (g_dupPending) { *label = "nombre";    *buf = &g_dupNumBuf; return true; }
    if (g_ppActive)   { *label = "dimension"; *buf = &g_pp.numBuf; return true; }
    if (g_rotActive)  { *label = "angle";     *buf = &g_rotNumBuf; return true; }
    return false;
}

static void drawTextClipped(float x, float y, const char* s, float maxW) {
#ifdef _WIN32
    const std::string str(s);
    if (textWidthPx(str.c_str()) <= maxW) { drawText2D(x, y, s); return; }
    std::string txt(s);
    while (txt.size() > 1) {
        txt = str.substr(0, txt.size() - 1);
        txt += "...";
        if (textWidthPx(txt.c_str()) <= maxW) break;
    }
    if (txt == "...") txt.clear();
    drawText2D(x, y, txt.c_str());
#else
    (void)x; (void)y; (void)s; (void)maxW;
#endif
}

static void drawStatusBand() {
#ifdef _WIN32
    if (!g_fontBase) return;
    const float h = 26.0f;
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    if (g_winH > 0) glOrtho(0, g_winW, 0, g_winH, -1, 11);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glBegin(GL_QUADS);
    glColor3f(0.85f, 0.85f, 0.88f);
    glVertex2f(0.0f, 0.0f);
    glVertex2f((float)g_winW, 0.0f);
    glVertex2f((float)g_winW, h);
    glVertex2f(0.0f, h);
    glEnd();
    glColor3f(0.08f, 0.09f, 0.12f);
    /* boite de saisie (alignee a droite, adaptée au texte tape) ; le texte
       d'invite du bandeau est rogné pour ne jamais la recouvrir */
    const char* label = nullptr;
    const std::string* buf = nullptr;
    const bool inputOn = inputZoneState(&label, &buf);
    const bool caret = std::fmod(glfwGetTime(), 1.0) < 0.6;
    char inTxt[96];
    float boxX = (float)g_winW, boxW = 0.0f;
    std::string prompt = statusPrompt();
    if (inputOn) {
        std::snprintf(inTxt, sizeof(inTxt), "%s : %s%s", label, buf->c_str(), caret ? "_" : " ");
        boxW = (float)(textWidthPx(inTxt) + 16);
        if (boxW < 130.0f) boxW = 130.0f;
        boxX = (float)g_winW - boxW - 6.0f;
        if (boxX < 80.0f) boxX = 80.0f;
    }
    const float maxTxtW = (inputOn ? boxX - 14.0f : (float)g_winW - 16.0f) - 8.0f;
    drawTextClipped(8.0f, 8.0f, prompt.c_str(), maxTxtW);
    if (inputOn) {
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(boxX, 3.5f);
        glVertex2f(boxX + boxW, 3.5f);
        glVertex2f(boxX + boxW, 22.5f);
        glVertex2f(boxX, 22.5f);
        glEnd();
        glColor3f(0.08f, 0.09f, 0.12f);
        glLineWidth(1.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(boxX + 0.5f, 4.0f);
        glVertex2f(boxX + boxW - 0.5f, 4.0f);
        glVertex2f(boxX + boxW - 0.5f, 22.0f);
        glVertex2f(boxX + 0.5f, 22.0f);
        glEnd();
        glColor3f(0.08f, 0.09f, 0.12f);
        drawText2D(boxX + 8.0f, 8.0f, inTxt);
    }
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
#endif
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
    std::printf("Sketch3D natif — build %s (outillages T1 : duplication multi-instances, rotation autour des axes + menus du lot N5)\n", BUILD_VERSION);
    std::printf("menu Fichier : nouveau/ouvrir/enregistrer/fermer projet, importer, exporter OBJ/STL, quitter ; menu contextuel (clic droit) : infos objet, duplication\n");
    std::printf("P : mode Push/Pull ; R : mode Rotation (X/Y/Z = axe, souris ou chiffres+Entree) ; I : import ; F : cadrage ; H : origine\n");
    buildScene();
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit a echoue\n"); return 1; }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_SAMPLES, 4);
    char titre[64];
    std::snprintf(titre, sizeof(titre), "Sketch3D — build %s", BUILD_VERSION);
    GLFWwindow* win = glfwCreateWindow(g_winW, g_winH, titre, nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "creation de fenetre impossible\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    g_camHome = g_cam;
    glfwSetScrollCallback(win, onScroll);
    glfwSetMouseButtonCallback(win, onMouseButton);
    glfwSetKeyCallback(win, onKey);
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow*, int w, int h) { g_winW = w; g_winH = h; });
    attachMenus(win);

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
        } else if (g_ppActive) {
            updatePP(win);
        } else if (g_rotActive) {
            updateRot(win);
        }
        render();
        drawAxisGuide();
        drawStatusBand();
        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
