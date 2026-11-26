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
static const char* BUILD_VERSION = "B2.2";   /* fil visible sans selection + capture fil au clic */   /* B2.1 : outil Ligne (fil multi-segments pose au sol) */

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

/* multi-selection (T1.7) : g_selected = selection principale (clic gauche),
   g_selMulti = objets supplementaires (Ctrl+clic : ajoute/retire) ;
   les actions (export, suppression, duplication) portent sur l'ensemble. */
static std::vector<int> g_selMulti;

static bool isSelectedRaw(int i) {
    if (i == g_selected) return true;
    return std::find(g_selMulti.begin(), g_selMulti.end(), i) != g_selMulti.end();
}
static void selectOnly(int i) { g_selected = i; g_selMulti.clear(); }
static void clearSelection() { g_selected = -1; g_selMulti.clear(); }
static size_t selectedCount() {
    if (g_selected < 0) return g_selMulti.size();
    if (std::find(g_selMulti.begin(), g_selMulti.end(), g_selected) != g_selMulti.end())
        return g_selMulti.size();
    return g_selMulti.size() + 1;
}
static std::vector<int> selectedIndices() {
    std::vector<int> r = g_selMulti;
    if (g_selected >= 0) r.push_back(g_selected);
    std::sort(r.begin(), r.end());
    r.erase(std::unique(r.begin(), r.end()), r.end());
    r.erase(std::remove_if(r.begin(), r.end(),
                [](int i) { return i < 0 || i >= (int)g_objs.size(); }), r.end());
    return r;
}
static void toggleSelect(int i) {
    if (i < 0 || i >= (int)g_objs.size()) return;
    if (i == g_selected) {
        if (g_selMulti.empty()) { clearSelection(); return; }  /* dernier : deselection totale */
        g_selected = g_selMulti.front();                        /* un autre devient principal */
        g_selMulti.erase(g_selMulti.begin());
        std::sort(g_selMulti.begin(), g_selMulti.end());
        /* (T1.7.1) defense : l' indice promu doit appartenir a la scene ;
           un multi perime est elague, le premier valide devient principal */
        if (g_selected < 0 || g_selected >= (int)g_objs.size()) {
            g_selMulti.erase(std::remove_if(g_selMulti.begin(), g_selMulti.end(),
                    [](int j) { return j < 0 || j >= (int)g_objs.size(); }), g_selMulti.end());
            g_selected = g_selMulti.empty() ? -1 : g_selMulti.front();
            if (!g_selMulti.empty()) g_selMulti.erase(g_selMulti.begin());
        }
        return;
    }
    auto it = std::find(g_selMulti.begin(), g_selMulti.end(), i);
    if (it != g_selMulti.end()) g_selMulti.erase(it);
    else g_selMulti.push_back(i);
}
static void renumberSelectionAfterDelete(int erasedIdx) {
    if (g_selected > erasedIdx) --g_selected;
    else if (g_selected >= (int)g_objs.size()) g_selected = (int)g_objs.size() - 1;
    std::vector<int> nm;
    nm.reserve(g_selMulti.size());
    for (int j : g_selMulti) {
        if (j == erasedIdx) continue;
        nm.push_back(j > erasedIdx ? j - 1 : j);
    }
    g_selMulti = nm;
    g_selMulti.erase(std::remove(g_selMulti.begin(), g_selMulti.end(), g_selected), g_selMulti.end());
}

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
    if (o.tri.empty()) {
        /* B2.1 : fil sans triangle (outil Ligne) = chaine 0-1, 1-2, ... ;
           topologie derivable des sommets, donc reconstruite ici a l'identique
           apres un save/load, une rotation, une duplication... */
        for (size_t i = 0; i + 1 < o.v.size(); ++i)
            o.edges.emplace_back((int)i, (int)(i + 1));
        return;
    }
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

enum Mode { M_None, M_Orbit, M_Drag, M_Pan };
static Mode g_mode = M_None;
static Camera g_cam;
static Camera g_camHome;
static double g_prevX = 0, g_prevY = 0;
static double g_lastClickTime = -1.0;
static int g_winW = 1280, g_winH = 800;
static int g_lockAxis = -1;
static Vec3 g_grabPoint;
static Vec3 g_startPos;
/* T1.11 : capture par membre pour le deplacement de groupe (vide =
   deplacement mono, chemin historique strictement inchange) */
struct DragMember { int idx; Vec3 startPos; };
static std::vector<DragMember> g_dragMulti;

static const char* axisName(int a) { return a == 0 ? "X" : a == 1 ? "Y" : "Z"; }
static Vec3 axisVec(int a) { return a == 0 ? Vec3(1, 0, 0) : a == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1); }

static void goHome() {
    g_cam = g_camHome;
    clearSelection();
}

static void cameraPan(Camera& c, double dxPx, double dyPx, double winH) {
    if (winH < 1.0) return;
    const double scale = 2.0 * c.dist * std::tan(kFovDeg * 3.14159265358979 / 360.0) / winH;
    c.target = c.target - c.right() * (dxPx * scale) + c.up() * (dyPx * scale);
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
    if (g_selected < 0 || g_selected >= (int)g_objs.size()) { frameScene(); return; }
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
        /* (T1.7.1) defense : un tri dont les indices depassent les sommets est ignore */
        if (o.tri[i] >= o.v.size() || o.tri[i + 1] >= o.v.size() || o.tri[i + 2] >= o.v.size()) continue;
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

static bool pickPointFromRay(const Vec3& org, const Vec3& dir, Vec3& p) {
    double best = 1e300;
    for (size_t i = 0; i < g_objs.size(); ++i) {
        double t;
        if (rayObject(org, dir, g_objs[i], t) && t < best) best = t;
    }
    if (best < 1e300) { p = org + dir * best; return true; }
    if (std::fabs(dir.y) > 1e-12) {
        const double t = -org.y / dir.y;
        if (t > 1e-9) { p = org + dir * t; return true; }
    }
    return false;
}

/* B2.1c : un fil (objet sans triangle) n'est pas touchable par rayon ;
   capture par distance minimale rayon-segments (echantillonnage le long du
   rayon, distance point-segment standard). Retourne le t du point le plus
   proche, ou -1. La tolerance est en unites monde, proportionnelle a la
   distance de la camera (2% + marge), comme la precision visuelle du picking. */
static double filRayHit(const Vec3& org, const Vec3& dir, const Object& o, double tol) {
    if (!o.tri.empty() || o.v.size() < 2 || tol <= 0.0) return -1.0;
    const Vec3 d = normalized(dir);
    const int N = 128;
    const double maxT = g_cam.dist * 2.0 + 1.0;
    double bestD = tol, bestT = -1.0;
    for (int i = 1; i <= N; ++i) {
        const double t = maxT * (double)i / (double)N;
        const Vec3 p = org + d * t;
        for (const auto& e : o.edges) {
            if (e.first < 0 || e.first >= (int)o.v.size()
                || e.second < 0 || e.second >= (int)o.v.size()) continue;
            const Vec3 a = o.pos + o.v[(size_t)e.first] * o.scl;
            const Vec3 b = o.pos + o.v[(size_t)e.second] * o.scl;
            const Vec3 ab = b - a;
            const double len2 = dot(ab, ab);
            double sFrac = 0.0;
            if (len2 > 1e-24) {
                sFrac = dot(p - a, ab) / len2;
                if (sFrac < 0.0) sFrac = 0.0;
                if (sFrac > 1.0) sFrac = 1.0;
            }
            const double dd = (p - (a + ab * sFrac)).norm();
            if (dd < bestD) { bestD = dd; bestT = t; }
        }
    }
    return bestT;
}
/* tolerance de capture des fils, partagee par pickAt et beginDrag */
static double filPickTol() { return g_cam.dist * 0.02 + 0.05; }

static int pickAt(GLFWwindow* w) {
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return -1;
    int best = -1;
    double bestT = 1e300;
    for (size_t i = 0; i < g_objs.size(); ++i) {
        double t;
        if (rayObject(org, dir, g_objs[i], t) && t < bestT) { bestT = t; best = (int)i; }
    }
    if (best < 0) {   /* B2.1c : aucun solide touche -> tenter les fils */
        double bestTf = 1e300;
        for (size_t i = 0; i < g_objs.size(); ++i) {
            const double tf = filRayHit(org, dir, g_objs[i], filPickTol());
            if (tf >= 0.0 && tf < bestTf) { bestTf = tf; best = (int)i; }
        }
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
    if (!rayObject(org, dir, g_objs[(size_t)idx], t)) {
        /* B2.1c : fil sans triangle = capture par tolerance (meme seuil) */
        const double tf = filRayHit(org, dir, g_objs[(size_t)idx], filPickTol());
        if (tf < 0.0) { g_mode = M_None; return; }
        t = tf;
    }
    g_grabPoint = org + dir * t;
    g_startPos = g_objs[(size_t)idx].pos;
    g_dragMulti.clear();
    /* T1.11 : clic gauche sur un objet DEJA membre d'une selection de plus
       d'un objet = tout le groupe suit le meme delta (verrou d'axe commun) ;
       sinon chemin mono historique. */
    if (idx >= 0 && isSelectedRaw(idx) && selectedCount() > 1) {
        for (int j : selectedIndices()) {
            DragMember m;
            m.idx = j;
            m.startPos = g_objs[(size_t)j].pos;
            g_dragMulti.push_back(m);
        }
        std::printf("deplacement multi : %zu objets\n", g_dragMulti.size());
    }
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
    if (g_selected < 0 || g_selected >= (int)g_objs.size()) {
        g_mode = M_None;              /* duree : selection perimee = retour neutre, jamais d'acces */
        return;
    }
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
    if (!g_dragMulti.empty()) {
        for (const DragMember& m : g_dragMulti) {
            if (m.idx < 0 || m.idx >= (int)g_objs.size()) continue;
            g_objs[(size_t)m.idx].pos = m.startPos + delta;
        }
        return;
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
    selectOnly((int)g_objs.size() - 1);
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
    for (int r = 0; r < rem; ++r) {
        g_objs.erase(g_objs.begin() + hits[(size_t)r].first);
        renumberSelectionAfterDelete(hits[(size_t)r].first);
    }
    if (rem > 0)
        std::printf("retrait : %d copie(s) supprimee(s) de la serie %s (%d instance(s) restante(s))\n",
                    rem, g_objs[(size_t)idx].name.c_str(), (int)hits.size() - rem);
    return rem;
}

/* suppression d'un objet (menu contextuel) : l'original comme une copie ;
   la selection suit si possible (objet suivant a la meme place).        */
static void deleteObject(int idx) {
    if (idx < 0 || idx >= (int)g_objs.size()) return;
    const std::string nm = g_objs[(size_t)idx].name;
    g_objs.erase(g_objs.begin() + idx);
    renumberSelectionAfterDelete(idx);
    std::printf("suppression : %s (%zu objet(s) restant(s))\n", nm.c_str(), g_objs.size());
}

/* suppression de TOUTE la selection (T1.7 : 1 objet comme plusieurs) ;
   reconstruction en une passe, selection videe apres coup.              */
static size_t deleteSelectedSet() {
    const std::vector<int> idxs = selectedIndices();
    if (idxs.empty()) return 0;
    if (idxs.size() == 1) { deleteObject(idxs[0]); return 1; }
    std::set<int> kill(idxs.begin(), idxs.end());
    std::vector<Object> keep;
    keep.reserve(g_objs.size() - kill.size());
    for (size_t i = 0; i < g_objs.size(); ++i) {
        if (kill.count((int)i)) continue;
        keep.push_back(std::move(g_objs[i]));
    }
    g_objs = std::move(keep);
    clearSelection();
    std::printf("suppression : %d objet(s) supprimes (%zu restant(s))\n", (int)idxs.size(), g_objs.size());
    return idxs.size();
}

/* saisie du nombre d'instances dans la FENETRE au clavier (modele Push/Pull :
   chiffres puis Entree) — jamais de getline bloquant dans la boucle de jeu */
static bool g_dupPending = false;
static int g_dupSrc = -1;
static std::vector<std::string> g_dupSrcNames;
static std::string g_dupNumBuf;

static void dupStartPrompt() {
    g_dupPending = true;
    g_dupSrc = g_selected;
    g_dupSrcNames.clear();
    for (int i : selectedIndices()) g_dupSrcNames.push_back(g_objs[(size_t)i].name);
    g_dupNumBuf.clear();
    std::printf("Dupliquer : tapez le nombre d'instances puis Entree (vide = 1, maximum 999 ; negatif = retire des copies ; Echap pour annuler)\n");
}

static int findObjectByName(const std::string& nm) {
    for (size_t i = 0; i < g_objs.size(); ++i)
        if (g_objs[i].name == nm) return (int)i;
    return -1;
}

/* execution de la duplication / du retrait (T1.7) : le nombre tape s'applique
   a chaque objet de la selection capturee au demarrage de la demande ;
   on re-resout les noms de sources a chaque coup (les indices glissent). */
static void dupExecute(long n) {
    if (n >= 1 && n <= 999) {
        for (const std::string& snm : g_dupSrcNames)
            duplicateObject(findObjectByName(snm), (int)n);
        return;
    }
    if (n <= -1 && n >= -999) {
        int rem = 0;
        for (const std::string& snm : g_dupSrcNames)
            rem += removeInstances(findObjectByName(snm), (int)-n);
        if (rem == 0)
            std::printf("\nretrait : aucune copie numerotee dans la selection\n");
        return;
    }
    std::printf("\nNombre invalide : entrez de -999 a 999 puis Entree (negatif = retire des copies)\n");
}

/* message ephemere affiche dans le bandeau de la FENETRE (retour de test :
   les avis de saisie doivent se voir dans l'application, pas en console) */
static std::string g_bandFlash;
static double g_bandFlashUntil = 0.0;

static void flashBand(const char* msg) {
    g_bandFlash = msg;
    g_bandFlashUntil = glfwGetTime() + 3.0;
}

/* B0 : fichier de setup - INI editable a cote de l exe (sketch3d.ini) ;
   charge au lancement, sauvegarde a la fermeture + a chaque changement de
   preference (choix valide). Contenu v1 : geometrie de fenetre, dernier
   projet, formats d'export reels (memorises au fil des exports). */
struct Setup {
    int winX = 100, winY = 60;
    int winW = 0, winH = 0;      /* 0 = garder les valeurs par defaut du programme */
    int maximized = 0;
    std::string dernierProjet;
    std::string formatScene = "obj";
    std::string formatSelection = "obj";
    int toolVisible = 1;         /* B1.1 : barre d'outils flottante */
    int toolVertical = 0;
    int toolX = 140, toolY = 140;
};
static Setup g_setup;
static std::string g_setupPath = "sketch3d.ini";

static std::string setupFilePath() {
#ifdef _WIN32
    char exe[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const std::string p = (n > 0 && n < MAX_PATH) ? std::string(exe, (size_t)n) : std::string();
    const size_t slash = p.find_last_of("\\/");
    return (slash == std::string::npos) ? "sketch3d.ini" : p.substr(0, slash + 1) + "sketch3d.ini";
#else
    return "sketch3d.ini";       /* sandbox / tests : repertoire courant */
#endif
}

static std::string setupTrim(const std::string& t) {
    const size_t a = t.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const size_t b = t.find_last_not_of(" \t\r\n");
    return t.substr(a, b - a + 1);
}

static void saveSetup() {
    std::ofstream f(g_setupPath.c_str());
    if (!f) { std::printf("setup : ecriture impossible (%s)\n", g_setupPath.c_str()); return; }
    f << "[fenetre]\n"
      << "x = " << g_setup.winX << "\n"
      << "y = " << g_setup.winY << "\n"
      << "w = " << g_setup.winW << "\n"
      << "h = " << g_setup.winH << "\n"
      << "max = " << g_setup.maximized << "\n"
      << "\n[projet]\n"
      << "dernier = " << g_setup.dernierProjet << "\n"
      << "\n[exports]\n"
      << "format_scene = " << g_setup.formatScene << "\n"
      << "format_selection = " << g_setup.formatSelection << "\n"
      << "\n[outils]\n"
      << "visible = " << g_setup.toolVisible << "\n"
      << "vertical = " << g_setup.toolVertical << "\n"
      << "x = " << g_setup.toolX << "\n"
      << "y = " << g_setup.toolY << "\n";
}

static void loadSetup() {
    std::ifstream f(g_setupPath.c_str());
    if (!f) return;              /* premiere utilisation : defauts du programme */
    std::string line, section;
    while (std::getline(f, line)) {
        const std::string t = setupTrim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') { section = setupTrim(t.substr(1, t.size() - 2)); continue; }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = setupTrim(t.substr(0, eq));
        const std::string v = setupTrim(t.substr(eq + 1));
        /* cles inconnues ou valeurs aberrantes : ignorees (setup tolerant) */
        if (section == "fenetre") {
            if (k == "x") g_setup.winX = std::atoi(v.c_str());
            else if (k == "y") g_setup.winY = std::atoi(v.c_str());
            else if (k == "w" && std::atoi(v.c_str()) > 0) g_setup.winW = std::atoi(v.c_str());
            else if (k == "h" && std::atoi(v.c_str()) > 0) g_setup.winH = std::atoi(v.c_str());
            else if (k == "max") g_setup.maximized = (std::atoi(v.c_str()) != 0) ? 1 : 0;
        } else if (section == "projet") {
            if (k == "dernier") g_setup.dernierProjet = v;
        } else if (section == "exports") {
            if (k == "format_scene" && (v == "obj" || v == "stl")) g_setup.formatScene = v;
            else if (k == "format_selection" && (v == "obj" || v == "stl")) g_setup.formatSelection = v;
        } else if (section == "outils") {
            if (k == "visible") g_setup.toolVisible = (std::atoi(v.c_str()) != 0) ? 1 : 0;
            else if (k == "vertical") g_setup.toolVertical = (std::atoi(v.c_str()) != 0) ? 1 : 0;
            else if (k == "x") g_setup.toolX = std::atoi(v.c_str());
            else if (k == "y") g_setup.toolY = std::atoi(v.c_str());
        }
    }
}

/* chaque changement de preference : sauvegarde immediate */
static void noteProjectPath(const std::string& p) {
    if (g_setup.dernierProjet != p) { g_setup.dernierProjet = p; saveSetup(); }
}
#ifdef _WIN32
/* B1.6 : coches du menu Preferences (definies avec la barre d'outils) ;
   le prototype DOIT preceder noteExportFormat, qui l'appelle des B1.6 */
static void prefSyncCoches();
#endif

static void noteExportFormat(bool scene, bool stl) {
    std::string& cible = scene ? g_setup.formatScene : g_setup.formatSelection;
    const std::string voulu = stl ? "stl" : "obj";
    if (cible != voulu) { cible = voulu; saveSetup(); }
#ifdef _WIN32
    /* B1.6 : coches du menu Preferences = etat reel, TOUS les ecrivains du
       format passent ici (export menu Fichier, raccourcis O/S, radios) */
    prefSyncCoches();
#endif
}

/* T1.12 : presse-papiers interne (copier-coller clavier Ctrl+C / Ctrl+V) ;
   Ctrl+X (couper) : plus tard, copie + suppression existent deja separement. */
static std::vector<Object> g_clipboard;   /* copies entieres de la selection au Ctrl+C */
static int g_pasteRepeat = 0;             /* serie : chaque Ctrl+V successif decale encore d'un pas */

static void copySelected() {
    const std::vector<int> idxs = selectedIndices();
    if (idxs.empty()) {
        flashBand("rien a copier : selectionnez d'abord (clic gauche ou Ctrl+clic)");
        return;
    }
    g_clipboard.clear();
    g_pasteRepeat = 0;              /* nouvelle copie = nouvelle serie */
    for (int i : idxs) g_clipboard.push_back(g_objs[(size_t)i]);
    std::printf("copie : %zu objet(s) dans le presse-papiers\n", g_clipboard.size());
}

static void pasteClipboard() {
    if (g_clipboard.empty()) {
        flashBand("presse-papiers vide : Ctrl+C sur une selection d'abord");
        return;
    }
    /* boite englobante du groupe COPIE (monde) : pas = largeur X + petit espace ;
       chaque Ctrl+V successif decale encore d'un pas (serie visible, facon
       duplication), l'espacement interne du groupe reste identique. */
    double lo[3], hi[3];
    {
        const Vec3 p0lo = g_clipboard[0].pos + g_clipboard[0].bmin * g_clipboard[0].scl;
        const Vec3 p0hi = g_clipboard[0].pos + g_clipboard[0].bmax * g_clipboard[0].scl;
        lo[0] = p0lo.x; lo[1] = p0lo.y; lo[2] = p0lo.z;
        hi[0] = p0hi.x; hi[1] = p0hi.y; hi[2] = p0hi.z;
    }
    for (size_t i = 1; i < g_clipboard.size(); ++i) {
        const Vec3 blo = g_clipboard[i].pos + g_clipboard[i].bmin * g_clipboard[i].scl;
        const Vec3 bhi = g_clipboard[i].pos + g_clipboard[i].bmax * g_clipboard[i].scl;
        const double blos[3] = { blo.x, blo.y, blo.z };
        const double bhis[3] = { bhi.x, bhi.y, bhi.z };
        for (int a = 0; a < 3; ++a) {
            if (blos[a] < lo[a]) lo[a] = blos[a];
            if (bhis[a] > hi[a]) hi[a] = bhis[a];
        }
    }
    const double step = (hi[0] - lo[0]) + 0.25;
    const Vec3 off(step * (g_pasteRepeat + 1), 0.0, 0.0);
    std::vector<int> newIdxs;
    for (size_t i = 0; i < g_clipboard.size(); ++i) {
        Object o = g_clipboard[i];
        /* nommage type duplication : Nom (k), k = rang de ce nom dans la copie */
        int k = 1;
        for (size_t j = 0; j < i; ++j)
            if (g_clipboard[j].name == g_clipboard[i].name) ++k;
        o.name = g_clipboard[i].name + " (" + std::to_string(k) + ")";
        o.pos = g_clipboard[i].pos + off;
        g_objs.push_back(std::move(o));
        newIdxs.push_back((int)g_objs.size() - 1);
    }
    /* le groupe colle devient la selection (principal + multi) */
    if (!newIdxs.empty()) {
        selectOnly(newIdxs[0]);
        for (size_t i = 1; i < newIdxs.size(); ++i) g_selMulti.push_back(newIdxs[i]);
    }
    g_pasteRepeat += 1;
    std::printf("colle : %zu objet(s) poses vers +X (decalage %.3f)\n", g_clipboard.size(), step);
}

/* ------------------------------------------------------------------ */
/* Export OBJ / STL                                                    */
/* ------------------------------------------------------------------ */

static void exportOBJ(const std::string& path, const std::vector<int>& only = {}) {
    std::ofstream f(path);
    if (!f) { std::printf("export OBJ impossible : %s\n", path.c_str()); return; }
    std::vector<int> cible(only);
    std::sort(cible.begin(), cible.end());
    cible.erase(std::unique(cible.begin(), cible.end()), cible.end());
    cible.erase(std::remove_if(cible.begin(), cible.end(),
                [](int i) { return i < 0 || i >= (int)g_objs.size(); }), cible.end());
    f << "# Sketch3D natif - export "
      << (cible.empty() ? "scene" : (cible.size() == 1 ? "objet" : "selection")) << "\n";
    unsigned base = 1;
    for (int i = 0; i < (int)g_objs.size(); ++i) {
        if (!cible.empty() && std::find(cible.begin(), cible.end(), i) == cible.end()) continue;
        const Object& o = g_objs[(size_t)i];
        f << "o " << o.name << "\n";
        for (const Vec3& p : o.v) {
            const Vec3 w = o.pos + p * o.scl;
            f << "v " << w.x << " " << w.y << " " << w.z << "\n";
        }
        for (size_t t = 0; t < o.tri.size(); t += 3)
            f << "f " << base + o.tri[t] << " " << base + o.tri[t + 1] << " " << base + o.tri[t + 2] << "\n";
        base += (unsigned)o.v.size();
    }
    if (cible.size() == 1)
        std::printf("export OBJ : %s (objet : %s, en coordonnees monde)\n", path.c_str(), g_objs[(size_t)cible[0]].name.c_str());
    else if (cible.size() > 1)
        std::printf("export OBJ : %s (selection : %d objet(s), en coordonnees monde)\n", path.c_str(), (int)cible.size());
    else
        std::printf("export OBJ : %s (%zu objets, en coordonnees monde)\n", path.c_str(), g_objs.size());
}

static void exportSTL(const std::string& path, const std::vector<int>& only = {}) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::printf("export STL impossible : %s\n", path.c_str()); return; }
    std::vector<int> cible(only);
    std::sort(cible.begin(), cible.end());
    cible.erase(std::unique(cible.begin(), cible.end()), cible.end());
    cible.erase(std::remove_if(cible.begin(), cible.end(),
                [](int i) { return i < 0 || i >= (int)g_objs.size(); }), cible.end());
    size_t total = 0;
    auto want = [&](int i) { return cible.empty() || std::find(cible.begin(), cible.end(), i) != cible.end(); };
    for (int i = 0; i < (int)g_objs.size(); ++i)
        if (want(i)) total += g_objs[(size_t)i].tri.size() / 3;
    char header[80] = {0};
    f.write(header, 80);
    const uint32_t cnt = (uint32_t)total;
    f.write((const char*)&cnt, 4);
    for (int i = 0; i < (int)g_objs.size(); ++i) {
        if (!want(i)) continue;
        const Object& o = g_objs[(size_t)i];
        for (size_t t = 0; t < o.tri.size(); t += 3) {
            const Vec3 n = o.n[t / 3];
            float rec[12] = {
                (float)n.x, (float)n.y, (float)n.z,
                0, 0, 0, 0, 0, 0, 0, 0, 0
            };
            for (int k = 0; k < 3; ++k) {
                const Vec3 w = o.pos + o.v[o.tri[t + k]] * o.scl;
                rec[3 + k * 3] = (float)w.x;
                rec[4 + k * 3] = (float)w.y;
                rec[5 + k * 3] = (float)w.z;
            }
            f.write((const char*)rec, sizeof(rec));
            const uint16_t attr = 0;
            f.write((const char*)&attr, 2);
        }
    }
    if (cible.size() == 1)
        std::printf("export STL binaire : %s (objet : %s, %zu tris)\n", path.c_str(), g_objs[(size_t)cible[0]].name.c_str(), total);
    else if (cible.size() > 1)
        std::printf("export STL binaire : %s (selection : %d objet(s), %zu tris)\n", path.c_str(), (int)cible.size(), total);
    else
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
    selectOnly(g_objs.empty() ? -1 : 0);
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
    IDM_EXPORT_SCENE,
    IDM_EXPORT_SEL,
    IDM_QUIT,
    IDM_INFO_OBJECT,
    IDM_DUPLICATE,
    IDM_DELETE_OBJECT,
    IDM_TOOL_SELECT,
    IDM_TOOL_PP,
    IDM_TOOL_ROT,
    IDM_TOOL_FRAME,
    IDM_TOOL_HOME,
    IDM_TOOL_COPY,
    IDM_TOOL_PASTE,
    IDM_TOOL_LINE,
    IDM_VIEW_TOOLBAR,
    IDM_VIEW_TOOLBAR_VERT,
    IDM_PREF_SCENE_OBJ,
    IDM_PREF_SCENE_STL,
    IDM_PREF_SEL_OBJ,
    IDM_PREF_SEL_STL,
    IDM_VIEW_TOOLBAR_HOR,
    IDM_TOOL_LIGNE_FIL,
    IDM_TOOL_RECT,
};

/* Homogeneite ("UN SEUL CHEMIN DE CODE", regle systematique) : tous les
   identifiants de COMMANDE (outils, Affichage, Preferences) sont derniers et
   contigus dans l'enum ; tout producteur (menu, bouton, clavier, et plus tard
   persistance ou macro) les emet via runToolCommand exclusivement. */
static const int kIdCommandStart = IDM_TOOL_SELECT;

static GLFWwindow* g_mainWin = nullptr;

/* B1 : dispatcher des boutons de la barre d'outils flottante - un seul
   chemin de code : bouton = EXACTEMENT le raccourci (modes exclusifs). */
static void runToolCommand(int id);
#ifdef _WIN32
/* B1.1 : toggles du menu Affichage (definis avec la barre d'outils) */
static void toolbarSetVisible(bool vis);
static void toolbarSetVertical(bool vert);
/* B1.3 : la barre suit les mouvements de la fenetre principale */
static void toolFollow();
/* B1.4 : menu Preferences - formats d'export editables au vol */
static void prefSetExportFormat(bool scene, bool stl);
#endif

/* B1.7 : un seul lieu de choix du format = le menu Preferences (un seul
   chemin). Le dialogue s'ouvre filtre sur le FORMAT PREFERE ; le format reel
   applique est celui de l'extension du fichier choisi (grafe via
   noteExportFormat, le goulot unique). */
static bool exportPathIsStl(const std::string& p) {
    const size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = p.substr(dot + 1);
    for (char& c : e) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return e == "stl";
}
static std::string exportFileChooser(bool scene) {
    const bool stl = scene ? (g_setup.formatScene == "stl") : (g_setup.formatSelection == "stl");
    const char* titre = scene ? "Exporter la scene" : "Exporter la selection";
    if (stl) return fileDialog(true, "Fichier STL\0*.stl\0Fichier OBJ\0*.obj\0\0", "stl", titre);
    return fileDialog(true, "Fichier OBJ\0*.obj\0Fichier STL\0*.stl\0\0", "obj", titre);
}

static void handleMenuAction(int id) {
    /* commandes (outils/affichage/preferences) : deleguees au dispatcher */
    if (id >= kIdCommandStart) { runToolCommand(id); return; }
    switch (id) {
    case IDM_NEW_PROJECT:
        g_objs.clear();
        clearSelection();
        std::printf("nouveau projet : scene vide\n");
        return;
    case IDM_OPEN_PROJECT: {
        const std::string p = fileDialog(false,
            "Projets (*.s3projet)\0*.s3projet\0Tous (*.*)\0*.*\0\0",
            nullptr, "Ouvrir un projet");
        if (p.empty()) return;
        std::string err;
        if (!loadProject(p, err)) { std::printf("ouverture impossible : %s\n", err.c_str()); return; }
        noteProjectPath(p);
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
        else noteProjectPath(p);
        return;
    }
    case IDM_CLOSE_PROJECT:
        g_objs.clear();
        clearSelection();
        std::printf("projet ferme\n");
        return;
    case IDM_IMPORT_OBJECT:
        importFile();
        return;
    /* B1.7 : UN SEUL lieu de choix du format (menu Preferences) ; le dialogue
       s'ouvre filtre sur le format prefere, le format reel = extension choisie */
    case IDM_EXPORT_SCENE: {
        const std::string p = exportFileChooser(true);
        if (p.empty()) return;
        const bool stl = exportPathIsStl(p);
        if (stl) exportSTL(p); else exportOBJ(p);
        noteExportFormat(true, stl);
        return;
    }
    case IDM_EXPORT_SEL: {
        const std::vector<int> sel = selectedIndices();
        if (sel.empty()) { flashBand("selectionnez d'abord un objet (clic gauche ou Ctrl+clic)"); return; }
        const std::string p = exportFileChooser(false);
        if (p.empty()) return;
        const bool stl = exportPathIsStl(p);
        if (stl) exportSTL(p, sel); else exportOBJ(p, sel);
        noteExportFormat(false, stl);
        return;
    }
    case IDM_DUPLICATE:
        dupStartPrompt();
        return;
    case IDM_DELETE_OBJECT:
        deleteSelectedSet();
        return;
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
    AppendMenuA(m, MF_STRING, IDM_DELETE_OBJECT, "Suppri&mer\tSuppr");
    AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(m, MF_STRING, IDM_DUPLICATE, "&Dupliquer...");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(glfwGetWin32Window(w));
    const int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, glfwGetWin32Window(w), nullptr);
    DestroyMenu(m);
    if (id == 0) return;
    if (id == IDM_INFO_OBJECT) showInfoDialog("Infos objet", info);
    else handleMenuAction(id);
#else
    (void)w;
#endif
}

#ifdef _WIN32
static WNDPROC g_baseWndProc = nullptr;

static LRESULT CALLBACK menuWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND) { handleMenuAction((int)LOWORD(wp)); return 0; }
    if (msg == WM_MOVE || msg == WM_SIZE) toolFollow();   /* B1.3 : la barre suit */
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

#ifdef _WIN32
/* B1.1 : barre d'outils flottante - fenetre outil (WS_EX_TOOLWINDOW,
   deplacable par la barre de titre = 2 ecrans inclus), boutons = modes
   exclusifs identiques aux raccourcis, via runToolCommand. La croix cache
   la barre et decoche Affichage > Barre d'outils (le menu la rappelle). Orientation
   verticale ou horizontale. B1.3 : la position stockee (toolX/toolY) est un
   DECALAGE par rapport au coin client de la fenetre principale - la barre
   suit ses mouvements, et l'aimantation (24 px) se fait sur ses 4 bords.
   Tout est persiste dans la section [outils] du sketch3d.ini. */
static HWND g_toolWin = nullptr;
static HWND g_mainHwnd = nullptr;    /* fenetre principale : suivi + aimantation */
static HWND g_toolBtn[9];
static HMENU g_viewMenu = nullptr;   /* menu Affichage : coches tenus a jour */
static HMENU g_prefSceneMenu = nullptr;  /* menu Preferences : export scene */
static HMENU g_prefSelMenu = nullptr;    /* menu Preferences : export selection */

/* replace la barre a mainHwnd + decalage persiste (appelee au WM_MOVE et
   WM_SIZE de la fenetre principale) */
static void toolFollow() {
    if (!g_toolWin || !g_mainHwnd) return;
    POINT o = {0, 0};
    ClientToScreen(g_mainHwnd, &o);
    SetWindowPos(g_toolWin, nullptr, o.x + g_setup.toolX, o.y + g_setup.toolY,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static void toolLayout(HWND tw) {
    const int n = 9;
    const int bw = 86, bh = 26, pad = 6;
    const int cols = g_setup.toolVertical ? 1 : 4;
    const int rows = (n + cols - 1) / cols;
    for (int i = 0; i < n; ++i)
        SetWindowPos(g_toolBtn[i], nullptr, pad + (i % cols) * (bw + pad),
                     pad + (i / cols) * (bh + pad), bw, bh, SWP_NOZORDER);
    const int mx = pad + cols * (bw + pad);
    const int my = pad + rows * (bh + pad);
    SetWindowPos(tw, nullptr, 0, 0, mx, my, SWP_NOMOVE | SWP_NOZORDER);
}

static LRESULT CALLBACK toolWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED) {
        runToolCommand((int)LOWORD(wp));
        return 0;
    }
    if (msg == WM_CLOSE) {
        /* croix = meme chemin que le menu Affichage : coche decochee,
           preference gravee -> le rappel par menu reste synchrone */
        toolbarSetVisible(false);
        return 0;
    }
    if (msg == WM_EXITSIZEMOVE) {
        /* fin de deplacement : decalage par rapport a la fenetre principale
           + aimantation sur ses 4 bords (G/D/H/B) si proche (24 px) */
        RECT r;
        GetWindowRect(h, &r);
        RECT c;
        GetClientRect(g_mainHwnd, &c);
        POINT o = {0, 0};
        ClientToScreen(g_mainHwnd, &o);
        const int tw = r.right - r.left, th = r.bottom - r.top;
        const int mw = c.right - c.left, mh = c.bottom - c.top;
        const int kMagnet = 24;
        int ox = r.left - o.x, oy = r.top - o.y;
        if (std::abs(ox) <= kMagnet || ox < 0) ox = 0;                     /* bord gauche */
        else if (mw - (ox + tw) <= kMagnet) ox = mw - tw;                  /* bord droit */
        if (std::abs(oy) <= kMagnet || oy < 0) oy = 0;                     /* bord haut */
        else if (mh - (oy + th) <= kMagnet) oy = mh - th;                  /* bord bas */
        g_setup.toolX = ox; g_setup.toolY = oy;
        saveSetup();
        toolFollow();
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void attachToolbar(HWND mainHwnd) {
    const char* cls = "Sketch3DToolbar";
    static bool clsDone = false;
    if (!clsDone) {
        WNDCLASSA wc;
        std::memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = toolWndProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        RegisterClassA(&wc);
        clsDone = true;
    }
    g_mainHwnd = mainHwnd;
    POINT o = {0, 0};
    ClientToScreen(mainHwnd, &o);
    const HWND tw = CreateWindowExA(WS_EX_TOOLWINDOW, cls, "Outils",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, o.x + g_setup.toolX, o.y + g_setup.toolY, 0, 0,
        mainHwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    static const char* labels[] = { "Selection", "P", "R", "F cadrer", "H origine", "Copier", "Coller", "Ligne", "Rectangle" };
    static const int ids[] = { IDM_TOOL_SELECT, IDM_TOOL_PP, IDM_TOOL_ROT, IDM_TOOL_FRAME,
                               IDM_TOOL_HOME, IDM_TOOL_COPY, IDM_TOOL_PASTE, IDM_TOOL_LINE, IDM_TOOL_RECT };
    for (int i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); ++i) {
        g_toolBtn[i] = CreateWindowExA(0, "BUTTON", labels[i],
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 10, 10, tw, (HMENU)(INT_PTR)ids[i], GetModuleHandleA(nullptr), nullptr);
        SendMessageA(g_toolBtn[i], WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
    toolLayout(tw);              /* position boutons + taille selon orientation */
    g_toolWin = tw;
    if (g_setup.toolVisible) ShowWindow(tw, SW_SHOW);
}

/* toggles du menu Affichage : etat + coche + sauvegarde immediate */
static void toolbarSetVisible(bool vis) {
    g_setup.toolVisible = vis ? 1 : 0;
    saveSetup();
    if (g_toolWin) ShowWindow(g_toolWin, vis ? SW_SHOW : SW_HIDE);
    if (g_viewMenu) CheckMenuItem(g_viewMenu, IDM_VIEW_TOOLBAR,
                                  vis ? MF_CHECKED : MF_UNCHECKED);
    std::printf("barre d'outils : %s\n", vis ? "visible" : "cachee (Affichage pour la rappeler)");
}
static void toolbarSetVertical(bool vert) {
    g_setup.toolVertical = vert ? 1 : 0;
    saveSetup();
    if (g_toolWin) toolLayout(g_toolWin);
    /* B1.7c (retour test) : la coche horizontale aussi (sinon elle ne
       revient jamais une fois decochee) */
    if (g_viewMenu) {
        CheckMenuItem(g_viewMenu, IDM_VIEW_TOOLBAR_HOR,
                      vert ? MF_UNCHECKED : MF_CHECKED);
        CheckMenuItem(g_viewMenu, IDM_VIEW_TOOLBAR_VERT,
                      vert ? MF_CHECKED : MF_UNCHECKED);
    }
    std::printf("barre d'outils : %s\n", vert ? "verticale" : "horizontale");
}

/* B1.6 : recoherence des coches avec l'etat reel - appelee par
   noteExportFormat, donc automatiquement a chaque export (menu Fichier,
   raccourcis O/S) comme a chaque radio du menu Preferences. */
static void prefSyncCoches() {
    if (g_prefSceneMenu) {
        CheckMenuItem(g_prefSceneMenu, IDM_PREF_SCENE_OBJ,
                      g_setup.formatScene != "stl" ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(g_prefSceneMenu, IDM_PREF_SCENE_STL,
                      g_setup.formatScene == "stl" ? MF_CHECKED : MF_UNCHECKED);
    }
    if (g_prefSelMenu) {
        CheckMenuItem(g_prefSelMenu, IDM_PREF_SEL_OBJ,
                      g_setup.formatSelection != "stl" ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(g_prefSelMenu, IDM_PREF_SEL_STL,
                      g_setup.formatSelection == "stl" ? MF_CHECKED : MF_UNCHECKED);
    }
}

/* B1.4 : menu Preferences - formats d'export editables au vol. UN SEUL
   CHEMIN DE CODE : passe par noteExportFormat (le meme que les exports
   reels) -> memoire gravee immediatement, coches coherentes en cascade. */
static void prefSetExportFormat(bool scene, bool stl) {
    noteExportFormat(scene, stl);
    std::printf("preferences : export %s en %s\n", scene ? "scene" : "selection", stl ? "STL" : "OBJ");
}
#endif

static void attachMenus(GLFWwindow* win) {
    g_mainWin = win;
#ifdef _WIN32
    const HWND hwnd = glfwGetWin32Window(win);
    buildFont(hwnd);
    HMENU file = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, IDM_NEW_PROJECT, "&Nouveau projet");
    AppendMenuA(file, MF_STRING, IDM_OPEN_PROJECT, "&Ouvrir un projet...");
    AppendMenuA(file, MF_STRING, IDM_SAVE_PROJECT, "&Enregistrer le projet...\tCtrl+S");
    AppendMenuA(file, MF_STRING, IDM_CLOSE_PROJECT, "&Fermer le projet");
    AppendMenuA(file, MF_SEPARATOR, 0, nullptr);
    /* B1.7 : export SANS choix de format dans Fichier (un seul lieu = menu
       Preferences) : le format prefere pre-filtre le dialogue */
    AppendMenuA(file, MF_STRING, IDM_IMPORT_OBJECT, "&Importer un objet...\tI");
    AppendMenuA(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(file, MF_STRING, IDM_EXPORT_SCENE, "Exporter la &scene...");
    AppendMenuA(file, MF_STRING, IDM_EXPORT_SEL, "Exporter la selec&tion...");
    AppendMenuA(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(file, MF_STRING, IDM_QUIT, "&Quitter");
    HMENU bar = CreateMenu();
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&Fichier");
    /* menu Affichage : rappel + orientation de la barre d'outils (B1.1) ;
       le menu Preferences reprendra ce role pour tous les choix au vol */
    HMENU aff = CreatePopupMenu();
    AppendMenuA(aff, MF_STRING | (g_setup.toolVisible ? MF_CHECKED : MF_UNCHECKED),
                IDM_VIEW_TOOLBAR, "&Barre d'outils");
    AppendMenuA(aff, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(aff, MF_STRING | (g_setup.toolVertical ? MF_UNCHECKED : MF_CHECKED),
                IDM_VIEW_TOOLBAR_HOR, "Barre &horizontale");
    AppendMenuA(aff, MF_STRING | (g_setup.toolVertical ? MF_CHECKED : MF_UNCHECKED),
                IDM_VIEW_TOOLBAR_VERT, "Barre &verticale");
    g_viewMenu = aff;
    /* menu Preferences (B1.4) : les choix persistes de B0, editables au vol ;
       il ira en s'etoilant (formats, unites, preferences d'outils...) */
    HMENU pref = CreatePopupMenu();
    g_prefSceneMenu = CreatePopupMenu();
    AppendMenuA(g_prefSceneMenu,
                MF_STRING | (g_setup.formatScene != "stl" ? MF_CHECKED : MF_UNCHECKED),
                IDM_PREF_SCENE_OBJ, "&OBJ");
    AppendMenuA(g_prefSceneMenu,
                MF_STRING | (g_setup.formatScene == "stl" ? MF_CHECKED : MF_UNCHECKED),
                IDM_PREF_SCENE_STL, "S&TL");
    g_prefSelMenu = CreatePopupMenu();
    AppendMenuA(g_prefSelMenu,
                MF_STRING | (g_setup.formatSelection != "stl" ? MF_CHECKED : MF_UNCHECKED),
                IDM_PREF_SEL_OBJ, "&OBJ");
    AppendMenuA(g_prefSelMenu,
                MF_STRING | (g_setup.formatSelection == "stl" ? MF_CHECKED : MF_UNCHECKED),
                IDM_PREF_SEL_STL, "S&TL");
    AppendMenuA(pref, MF_POPUP, (UINT_PTR)g_prefSceneMenu, "Export de la &scene");
    AppendMenuA(pref, MF_POPUP, (UINT_PTR)g_prefSelMenu, "Export de la se&lection");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)pref, "&Preferences");
    /* B2.1 : menu Outils - les commandes raccourcies rappelees dans le menu
       qui les propose (regle B1.7c) ; les 5 profils B2 y entreront au fil
       des lots ; delegation automatique : id >= kIdCommandStart */
    HMENU outils = CreatePopupMenu();
    AppendMenuA(outils, MF_STRING, IDM_TOOL_SELECT, "&Selection\tEchap");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_PP, "&Extraire une face\tP");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_ROT, "&Tourner\tR");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_LINE, "&Ligne (fermee en surface)\tL");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_LIGNE_FIL, "Ligne (&fil ouvert)\tMaj+L");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_RECT, "&Rectangle\tMaj+R");
    AppendMenuA(outils, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(outils, MF_STRING, IDM_TOOL_FRAME, "&Cadrer la selection\tF");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_HOME, "Vue d'&origine\tH");
    AppendMenuA(outils, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(outils, MF_STRING, IDM_TOOL_COPY, "&Copier\tCtrl+C");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_PASTE, "Co&ller\tCtrl+V");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)outils, "&Outils");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)aff, "&Affichage");
    SetMenu(hwnd, bar);
    g_baseWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)menuWndProc);
    attachToolbar(hwnd);
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
    if (o.tri.size() < 9 || clickedTri >= triCount || clickedTri >= o.n.size()) return false;
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

/* rotation multi (T1.9, cas 1) : chaque objet de la selection tourne autour
   de son PROPRE centre, avec un angle et un axe communs ; les cas suivants
   (pivot barycentre / pivot utilisateur) en decouleront : la capture stocke
   deja une base + un pivot PAR objet, seul le calcul du pivot changera. */
struct RotCapture {
    int idx;                     /* objet tourne */
    std::vector<Vec3> base;      /* sommets locaux a la capture */
    Vec3 pivot;                  /* pivot propre (monde) : centre de la piece */
};
static std::vector<RotCapture> g_rotMulti;   /* vide = rotation mono inchangee */

/* T1.10 (cas 2) : pivot du GROUPE = centre de la boite englobant tous les
   objets selectionnes a la capture (calculee sur les BASES : stable, les
   bases ne bougent pas pendant la rotation) ; B bascule a chaud entre
   centre propre (cas 1, defaut) et barycentre du groupe (cas 2), l'angle
   courant est conserve (l'application part toujours de la base capturee). */
static int g_rotPivotKind = 0;          /* 0 = centre propre, 1 = barycentre */
static Vec3 g_rotGroupPivot;            /* pivot du groupe (cas 2), fige a la capture */
static const char* rotPivotName() { return g_rotPivotKind == 1 ? "barycentre du groupe" : "centre propre"; }

/* centre de la boite englobant toutes les geometries CAPTUREES (monde) */
static Vec3 groupPivotFromCaptures(const std::vector<RotCapture>& caps) {
    Vec3 mn(1e300, 1e300, 1e300), mx(-1e300, -1e300, -1e300);
    bool any = false;
    for (size_t k = 0; k < caps.size(); ++k) {
        const RotCapture& rc = caps[k];
        if (rc.idx < 0 || rc.idx >= (int)g_objs.size()) continue;
        const Object& o = g_objs[(size_t)rc.idx];
        for (size_t i = 0; i < rc.base.size(); ++i) {
            const Vec3 w = o.pos + rc.base[i] * o.scl;
            if (w.x < mn.x) mn.x = w.x;
            if (w.y < mn.y) mn.y = w.y;
            if (w.z < mn.z) mn.z = w.z;
            if (w.x > mx.x) mx.x = w.x;
            if (w.y > mx.y) mx.y = w.y;
            if (w.z > mx.z) mx.z = w.z;
            any = true;
        }
    }
    if (!any) return Vec3();
    return Vec3(0.5 * (mn.x + mx.x), 0.5 * (mn.y + mx.y), 0.5 * (mn.z + mx.z));
}
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
static void applyRotationAt(Object& o, const std::vector<Vec3>& base, const Vec3& pivot,
                            int axis, double angDeg) {
    if (o.scl <= 1e-12) return;
    const double inv = 1.0 / o.scl;
    o.v.resize(base.size());
    for (size_t i = 0; i < base.size(); ++i) {
        const Vec3 w = o.pos + base[i] * o.scl;
        o.v[i] = (rotateAroundAxis(w, pivot, axis, angDeg) - o.pos) * inv;
    }
    computeObjectDerived(o);
}

/* applique l'angle courant a la selection tournee (mono OU multi) */
static void rotApplyCurrent(int axis, double angDeg) {
    if (!g_rotMulti.empty()) {
        for (size_t k = 0; k < g_rotMulti.size(); ++k) {
            RotCapture& rc = g_rotMulti[k];
            if (rc.idx < 0 || rc.idx >= (int)g_objs.size()) continue;
            applyRotationAt(g_objs[(size_t)rc.idx], rc.base,
                            g_rotPivotKind == 1 ? g_rotGroupPivot : rc.pivot, axis, angDeg);
        }
    } else if (g_rotObj >= 0 && g_rotObj < (int)g_objs.size()) {
        applyRotationAt(g_objs[(size_t)g_rotObj], g_rotBase, g_rotPivot, axis, angDeg);
    }
}

/* pivot propre (monde) : centre de la piece = meme formule que le mono */
static Vec3 objectRotationPivot(const Object& o) {
    return o.pos + (o.bmin + o.bmax) * (0.5 * o.scl);
}

static char axisLetterFromScancode(int scancode) {
    const char* name = glfwGetKeyName(-1, scancode);
    if (!name || name[0] == '\0') return 0;
    const char c = (name[0] >= 'A' && name[0] <= 'Z') ? char(name[0] - 'A' + 'a') : name[0];
    return (c == 'x' || c == 'y' || c == 'z') ? c : 0;
}

/* separateur decimal, independant de la disposition (retour de test : sur
   AZERTY les codes OEM arrivent contrairement aux attentes - le ';' est
   arrive en GLFW_KEY_SEMICOLON mais la virgule s'est perdue ailleurs) :
   les touches listees sont acceptees d'office, et sinon on interroge le
   NOM de la touche (",", ".", ";" = separateur, quelle que soit la disposition). */
static bool isSeparatorKey(int key, int scancode) {
    if (key == GLFW_KEY_PERIOD || key == GLFW_KEY_COMMA
        || key == GLFW_KEY_KP_DECIMAL || key == GLFW_KEY_SEMICOLON) return true;
    const char* name = glfwGetKeyName(key, scancode);
    return name != 0 && name[1] == '\0'
        && (name[0] == ',' || name[0] == '.' || name[0] == ';');
}

static void rotBegin(GLFWwindow* w, int axis) {
    const std::vector<int> sel = selectedIndices();
    if (sel.empty()) {
        std::printf("rotation : selectionnez d'abord un objet (clic gauche)\n");
        return;
    }
    g_rotMulti.clear();
    if (sel.size() > 1) {   /* cas 1 : rotation individuelle de chaque membre, angle commun */
        g_rotMulti.reserve(sel.size());
        for (size_t k = 0; k < sel.size(); ++k) {
            const int i = sel[k];
            if (i < 0 || i >= (int)g_objs.size()) continue;
            const Object& o = g_objs[(size_t)i];
            RotCapture rc;
            rc.idx = i;
            rc.base = o.v;
            rc.pivot = objectRotationPivot(o);
            g_rotMulti.push_back(std::move(rc));
        }
        if (g_rotMulti.empty()) return;
        g_rotObj = g_rotMulti[0].idx;   /* objet de reference (messages, signe) */
        g_rotPivotKind = 0;             /* defaut T1.9 : centre propre (cas 1) */
        g_rotGroupPivot = groupPivotFromCaptures(g_rotMulti);
    } else {
        g_rotObj = g_selected;          /* mono : objet tourne = selection principale */
    }
    Object& o = g_objs[(size_t)(sel.size() > 1 ? g_rotObj : g_selected)];
    g_rotAxis = axis;
    g_rotBase = o.v;
    g_rotPivot = objectRotationPivot(o);
    g_rotAngle = 0.0;
    g_rotNumBuf.clear();
    double dummy = 0.0;
    glfwGetCursorPos(w, &g_rotLastX, &dummy);
    g_rotActive = true;
    if (!g_rotMulti.empty()) {
        std::printf("rotation multi %s de %zu objets autour du %s : souris pour tourner, B = pivot barycentre/centre, chiffres + Entree (tapez dans la fenetre ; Entree pose et quitte)\n",
                    axisName(axis), g_rotMulti.size(), rotPivotName());
    } else {
        std::printf("rotation %s de %s autour de son centre : souris pour tourner, ou chiffres + Entree (tapez dans la fenetre ; Entree pose et quitte)\n",
                    axisName(axis), o.name.c_str());
    }
}

static void rotCancel() {
    if (!g_rotActive) { return; }
    if (!g_rotMulti.empty()) {          /* multi : restaure chaque base capturee */
        for (size_t k = 0; k < g_rotMulti.size(); ++k) {
            const RotCapture& rc = g_rotMulti[k];
            if (rc.idx < 0 || rc.idx >= (int)g_objs.size()) continue;
            Object& o = g_objs[(size_t)rc.idx];
            o.v = rc.base;
            computeObjectDerived(o);
        }
        g_rotMulti.clear();
    } else {
        if (g_rotObj < 0 || g_rotObj >= (int)g_objs.size()) { g_rotActive = false; return; }
        Object& o = g_objs[(size_t)g_rotObj];
        o.v = g_rotBase;
        computeObjectDerived(o);
    }
    g_rotActive = false;
    g_rotAxis = -1;
    g_rotPivotKind = 0;
    g_rotNumBuf.clear();
    std::printf("\nrotation annulee\n");
}

static void rotCommit() {
    if (!g_rotActive) { return; }
    if (!g_rotMulti.empty()) {
        std::printf("\nrotation posee : %+.2f deg sur %zu objets\n", g_rotAngle, g_rotMulti.size());
        g_rotMulti.clear();
    } else {
        if (g_rotObj < 0 || g_rotObj >= (int)g_objs.size()) { g_rotActive = false; return; }
        std::printf("\nrotation posee : %+.2f deg sur %s\n", g_rotAngle, g_objs[(size_t)g_rotObj].name.c_str());
    }
    g_rotActive = false;
    g_rotAxis = -1;
    g_rotPivotKind = 0;
    g_rotNumBuf.clear();
}

static void updateRot(GLFWwindow* w) {
    if (!g_rotActive) return;
    if (g_rotMulti.empty() && (g_rotObj < 0 || g_rotObj >= (int)g_objs.size())) return;
    /* saisie au clavier en cours : la piece est figee (le moindre mouvement
       de souris ne modifie plus l'angle accumule) ; Entree pose et quitte. */
    if (!g_rotNumBuf.empty()) return;
    double x, y;
    glfwGetCursorPos(w, &x, &y);
    g_rotAngle += (x - g_rotLastX) * 0.25;
    g_rotLastX = x;
    rotApplyCurrent(g_rotAxis, g_rotAngle);
    const double now = glfwGetTime();
    if (now - g_rotStatusTick > 0.12) {
        g_rotStatusTick = now;
        std::printf("\rRotation %s : %+.1f deg (clic pour poser, clic droit pour annuler)   ", axisName(g_rotAxis), g_rotAngle);
        std::fflush(stdout);
    }
}

/* ------------------------------------------------------------------ */
/* B2.1 : outil Ligne - fil multi-segments pose au sol                 */
/* ------------------------------------------------------------------ */
/* Conception B2 validee utilisateur : clics successifs = sommets lies
   (un SEUL objet fil a la pose, les segments partagent les sommets :
   indispensable pour que B3 suive le chemin entier) ; fin = clic droit
   ou Entree (moins de 2 points = rien de pose) ; Echap = tout annuler ;
   chiffres + Entree = longueur exacte du dernier segment (direction du
   dernier point vers le curseur), puis pose et sortie du mode.
   Exception assumee a la regle "clic droit = neutre" : ICI le clic droit
   FINIT (les segments deja cliques restent poses). */
static bool g_lineMode = false;         /* outil arme */
static bool g_lineFerme = true;         /* B2.5b : pose par la COMMANDE d'armement (L = surface, Maj+L = fil) ; rien de memorise */
static bool g_lineActive = false;       /* au moins un point clique */
static std::vector<Vec3> g_linePts;     /* points cliques, monde (y = 0) */
static std::string g_lineNumBuf;        /* longueur tapee du segment courant */
static Vec3 g_lineCursor(0, 0, 0);      /* point sol sous le curseur (apercu) */
static bool g_lineCursorOk = false;

static void lineReset() {
    g_lineActive = false;
    g_linePts.clear();
    g_lineNumBuf.clear();
}
static void lineCancel(const char* pourquoi) {
    lineReset();
    g_lineMode = false;
    std::printf("\nligne : %s\n", pourquoi);
}
/* point d'impact du rayon sur le plan du sol (y = 0 monde), v1 */
static void lineGroundPoint(GLFWwindow* w, bool& ok, Vec3& p) {
    ok = false;
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return;
    if (std::fabs(dir.y) < 1e-12) return;
    const double t = -org.y / dir.y;
    if (t <= 1e-9) return;
    p = org + dir * t;
    ok = true;
}
/* suivi du curseur chaque tour (apercu + direction de la longueur tapee) */
static void lineTrackCursor(GLFWwindow* w) {
    Vec3 p;
    bool ok;
    lineGroundPoint(w, ok, p);
    g_lineCursor = p;
    g_lineCursorOk = ok;
}
static void lineCloseLoop();   /* B2.5 : fermeture de la boucle sur le depart */
static void lineTakePoint(GLFWwindow* w) {
    bool ok;
    Vec3 p;
    lineGroundPoint(w, ok, p);
    if (!ok) return;                                /* pointe le ciel : rien */
    if (g_linePts.size() >= 3 && (p - g_linePts[0]).norm() <= filPickTol()) {
        lineCloseLoop();                            /* B2.5 : clic sur le depart = fermeture */
        return;
    }
    if (!g_linePts.empty() && (p - g_linePts.back()).norm() < 1e-9)
        return;                                     /* meme point : rien */
    g_linePts.push_back(p);
    g_lineActive = true;
    g_lineNumBuf.clear();
    std::printf("ligne : %zu point(s) (clic = prolonger ; clic droit = fin ; Echap = tout annuler)\n", g_linePts.size());
}
/* nommage unique "Base", "Base (2)", ... (commun a tous les profils B2) */
static std::string uniqueName(const char* base) {
    if (findObjectByName(base) < 0) return base;
    for (int k = 2; ; ++k) {
        const std::string n = std::string(base) + " (" + std::to_string(k) + ")";
        if (findObjectByName(n) < 0) return n;
    }
}
static std::string lineNewName() { return uniqueName("Ligne"); }
/* pose : fil multi-segments en UN objet (sommets partages) ; ancre locale =
   premier point (convention o.pos, meme base que la rotation) ; aucun tri */
static void linePose() {
    Object o;
    o.name = lineNewName();
    o.pos = g_linePts[0];
    o.scl = 1.0;
    o.v.resize(g_linePts.size());
    for (size_t i = 0; i < g_linePts.size(); ++i) o.v[i] = g_linePts[i] - o.pos;
    o.tri.clear();
    o.n.clear();
    /* B2.1 : les aretes (chaine du fil) sont DERIVEES par computeObjectDerived
       (objet sans triangle = chaine 0-1, ...), donc survives au save/load */
    computeObjectDerived(o);
    const size_t nseg = o.edges.size();     /* capture AVANT le move (o move-assigne) */
    g_objs.push_back(std::move(o));
    const int idx = (int)g_objs.size() - 1;
    selectOnly(idx);
    std::printf("ligne posee : %s, %zu segment(s)\n", g_objs.back().name.c_str(), nseg);
    lineReset();
    g_lineMode = false;
    std::printf("mode selection\n");
}
/* B2.5 : fermeture de la boucle - clic sur le depart (>= 3 sommets).
   Choix par la COMMANDE d armement (regle utilisateur : rien de memorise) :
   L "Ligne (fermee en surface)" = feuille FUSEE au fil en UN objet - fan
   (0,i,i+1), etirable au push/pull - ou Maj+L "Ligne (fil ouvert)" = boucle
   fermee sans triangle (arete n-1 -> 0 ajoutee apres la derive). */
static void lineCloseLoop() {
    const bool surface = g_lineFerme;
    Object o;
    o.name = lineNewName();
    o.pos = g_linePts[0];
    o.scl = 1.0;
    o.v.resize(g_linePts.size());        /* LOCAL : pos + v = monde (bug relocalisation corrige) */
    for (size_t i = 0; i < o.v.size(); ++i) o.v[i] = g_linePts[i] - o.pos;
    o.tri.clear();
    o.n.clear();
    if (surface)
        for (size_t i = 1; i + 1 < o.v.size(); ++i) {
            o.tri.push_back(0); o.tri.push_back((int)i); o.tri.push_back((int)(i + 1));
        }
    computeObjectDerived(o);
    if (!surface)
        o.edges.emplace_back((int)o.v.size() - 1, 0);   /* boucle fermee */
    const size_t nseg = o.edges.size();     /* capture AVANT le move */
    g_objs.push_back(std::move(o));
    selectOnly((int)g_objs.size() - 1);
    std::printf(surface
        ? "ligne fermee : %zu cotes, feuille posee (etirable)\n"
        : "ligne fermee : %zu cotes, fil simple (pas de surface)\n", nseg);
    lineReset();
    g_lineMode = false;
    std::printf("mode selection\n");
}
static void lineFinish() {           /* clic droit ou Entree vide : FIN */
    if (g_linePts.size() >= 2) linePose();
    else lineCancel("rien a poser : moins de 2 points");
}
/* longueur tapee + Entree : pose un point a la distance demandee dans la
   direction du dernier point vers le curseur, puis pose et sortie */
static void linePoseByLength(double len) {
    if (!g_lineActive || g_linePts.empty()) return;
    if (!g_lineCursorOk || (g_lineCursor - g_linePts.back()).norm() < 1e-9) {
        std::printf("longueur tapee : pointez d'abord une direction (curseur sur le sol)\n");
        return;
    }
    const Vec3 toCur = g_lineCursor - g_linePts.back();
    g_linePts.push_back(g_linePts.back() + toCur * (len / toCur.norm()));
    linePose();
}
/* apercu du fil : chainon deja clique + rubber band vers le curseur */
static void drawLinePreview() {
    if (!g_lineMode || !g_lineCursorOk || g_linePts.empty()) return;
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3d(0.92, 0.75, 0.1);
    glLineWidth(2.0);
    glBegin(GL_LINE_STRIP);
    for (const Vec3& pt : g_linePts) glVertex3d(pt.x, pt.y, pt.z);
    glVertex3d(g_lineCursor.x, g_lineCursor.y, g_lineCursor.z);
    glEnd();
    glLineWidth(1.0);
    glEnable(GL_DEPTH_TEST);
}
/* ------------------------------------------------------------------ */
/* B2.2 : outil Rectangle - clic coin / clic coin oppose, pose au sol.  */
/* Feuille pleine de 2 tris (coherente avec la ligne fermee B2.5) : UN  */
/* objet, etirable au push/pull. Maj+R arme ; clic droit/Echap annule.  */
/* ------------------------------------------------------------------ */
static bool g_rectMode = false;         /* outil arme */
static bool g_rectActive = false;       /* premier coin pose */
static Vec3 g_rectCorner(0, 0, 0);      /* premier coin (monde, y = 0) */
static std::string g_rectNumBuf;        /* dimensions tapees "L,H" */
static Vec3 g_rectCursor(0, 0, 0);      /* point sol sous le curseur (apercu) */
static bool g_rectCursorOk = false;

static void rectReset() {
    g_rectActive = false;
    g_rectNumBuf.clear();
}
static void rectCancel(const char* pourquoi) {
    rectReset();
    g_rectMode = false;
    std::printf("\nrectangle : %s\n", pourquoi);
}
/* suivi du curseur chaque tour (apercu + orientation de la saisie L,H) */
static void rectTrackCursor(GLFWwindow* w) {
    Vec3 p;
    bool ok;
    lineGroundPoint(w, ok, p);
    g_rectCursor = p;
    g_rectCursorOk = ok;
}
/* construction : 2 coins monde quelconques ; coins canoniques (x0<x1,
   z0<z1) pour une normale +y ; v LOCALES (pos + v = monde, convention
   commune - cf. bug de relocalisation corrige en B2.5b) */
static void rectPose(const Vec3& a, const Vec3& b) {
    const double x0 = (a.x < b.x) ? a.x : b.x, x1 = (a.x < b.x) ? b.x : a.x;
    const double z0 = (a.z < b.z) ? a.z : b.z, z1 = (a.z < b.z) ? b.z : a.z;
    Object o;
    o.name = uniqueName("Rectangle");
    o.pos = Vec3(x0, 0.0, z0);
    o.scl = 1.0;
    o.v = { Vec3(0, 0, 0), Vec3(x1 - x0, 0, 0), Vec3(x1 - x0, 0, z1 - z0), Vec3(0, 0, z1 - z0) };
    o.tri = { 0, 2, 1, 0, 3, 2 };    /* normale +y (CCW vu de dessus) */
    o.n.clear();
    computeObjectDerived(o);
    g_objs.push_back(std::move(o));
    selectOnly((int)g_objs.size() - 1);
    std::printf("rectangle pose : %s, %.3g x %.3g\n", g_objs.back().name.c_str(), x1 - x0, z1 - z0);
    rectReset();
    g_rectMode = false;
    std::printf("mode selection\n");
}
static void rectTakePoint(GLFWwindow* w) {
    bool ok;
    Vec3 p;
    lineGroundPoint(w, ok, p);
    if (!ok) return;                                /* pointe le ciel : rien */
    if (!g_rectActive) {
        g_rectCorner = p;
        g_rectActive = true;
        g_rectNumBuf.clear();
        std::printf("rectangle : premier coin (%.3f, %.3f) - clic = coin oppose ; chiffres L,H + Entree = dimensions exactes\n", p.x, p.z);
        return;
    }
    if (std::fabs(p.x - g_rectCorner.x) < 1e-9 || std::fabs(p.z - g_rectCorner.z) < 1e-9) {
        std::printf("rectangle : coin oppose aligne avec le premier - rien a poser\n");
        return;
    }
    rectPose(g_rectCorner, p);
}
/* chiffres + Entree : "L,H" ; le quadrant du curseur par rapport au
   premier coin oriente le rectangle (fidele SketchUp : la diagonale
   dirige), puis pose + sortie du mode (meme contrat que la ligne) */
static void rectPoseByDims(const std::string& txt) {
    if (!g_rectCursorOk) {
        std::printf("dimensions tapees : pointez d'abord la direction (curseur sur le sol)\n");
        return;
    }
    std::string t = txt;
    for (char& c : t) if (c == ',' || c == ';') c = ' ';
    double L = 0.0, H = 0.0;
    if (std::sscanf(t.c_str(), "%lf %lf", &L, &H) != 2
        || !std::isfinite(L) || !std::isfinite(H) || L <= 0.0 || H <= 0.0) {
        std::printf("\nDimensions invalides - tapez L,H (deux nombres positifs) puis Entree.\n");
        return;
    }
    const double sx = (g_rectCursor.x >= g_rectCorner.x) ? 1.0 : -1.0;
    const double sz = (g_rectCursor.z >= g_rectCorner.z) ? 1.0 : -1.0;
    rectPose(g_rectCorner, g_rectCorner + Vec3(sx * L, 0.0, sz * H));
}
/* apercu : contour jaune du premier coin au curseur (aligne aux axes) */
static void drawRectPreview() {
    if (!g_rectMode || !g_rectActive || !g_rectCursorOk) return;
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3d(0.92, 0.75, 0.1);
    glLineWidth(2.0);
    glBegin(GL_LINE_LOOP);
    glVertex3d(g_rectCorner.x, 0, g_rectCorner.z);
    glVertex3d(g_rectCursor.x, 0, g_rectCorner.z);
    glVertex3d(g_rectCursor.x, 0, g_rectCursor.z);
    glVertex3d(g_rectCorner.x, 0, g_rectCursor.z);
    glEnd();
    glLineWidth(1.0);
    glEnable(GL_DEPTH_TEST);
}

/* ------------------------------------------------------------------ */
/* Callbacks                                                           */
/* ------------------------------------------------------------------ */

static void onScroll(GLFWwindow*, double, double dy) {
    g_cam.dist *= std::pow(0.9, dy);
    if (g_cam.dist < 0.5) g_cam.dist = 0.5;
    if (g_cam.dist > 2000.0) g_cam.dist = 2000.0;
}

static void onMouseButton(GLFWwindow* w, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {   /* clic molette + glisser = deplacer la vue (pan) */
        if (!g_ppActive && !g_rotActive) {
            if (action == GLFW_PRESS) {
                if (g_mode == M_None || g_mode == M_Pan) {
                    g_mode = M_Pan;
                    glfwGetCursorPos(w, &g_prevX, &g_prevY);
                }
            } else if (g_mode == M_Pan) {
                g_mode = M_None;
            }
        }
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        /* regle generale des transformations : clic droit = retour au neutre.
           Le deplacement en vol ne peut pas etre annule ainsi (le clic droit
           exige de relacher le bouton gauche, donc l'objet est deja pose). */
        if (action != GLFW_PRESS) return;
        if (g_rectMode) { rectCancel("dessin annule"); return; }   /* B2.2 : clic droit = annule (2 clics suffisent) */
        if (g_lineMode) { lineFinish(); return; }   /* B2.1 : le clic droit FINIT (segments deja cliques poses) */
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
        if (g_lineMode) { lineTakePoint(w); return; }   /* B2.1 : chaque clic pose un sommet */
        if (g_rectMode) { rectTakePoint(w); return; }   /* B2.2 : clic 1 = coin, clic 2 = coin oppose pose */
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
            selectOnly(hit);
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
        if (mods & GLFW_MOD_CONTROL) {              /* Ctrl+clic : multi-selection */
            if (hit >= 0) {
                const bool was = isSelectedRaw(hit);
                toggleSelect(hit);
                const size_t tot = selectedCount();
                if (was)
                    std::printf("selection : %s retire (%zu objet(s) selectionnes)\n", g_objs[(size_t)hit].name.c_str(), tot);
                else
                    std::printf("selection : %s ajoute (%zu objet(s) selectionnes)\n", g_objs[(size_t)hit].name.c_str(), tot);
            }
            glfwGetCursorPos(w, &g_prevX, &g_prevY);
            return;                                 /* Ctrl+clic purement selection : pas de drag */
        }
        /* T1.11 : clic sur un objet DEJA membre d'une multi de plus d'un objet
           = garder la selection (pas de selectOnly) : le drag deplace le groupe */
        const bool inMulti = hit >= 0 && isSelectedRaw(hit) && selectedCount() > 1;
        if (!inMulti) {
            const int prev = g_selected;
            const bool hadMulti = !g_selMulti.empty();
            selectOnly(hit);
            if (hit != prev || hadMulti) {
                if (hit >= 0) {
                    const Object& o = g_objs[(size_t)hit];
                    std::printf("selection : %s (%zu tris)\n", o.name.c_str(), o.tri.size() / 3);
                } else {
                    std::printf("deselection\n");
                }
            }
        }
        if (hit >= 0) {
            beginDrag(w, hit);
            if (dbl && !inMulti) {
                selectOnly(hit);
                frameSelected();
            }
        } else if (mods & GLFW_MOD_SHIFT) {
            g_mode = M_Pan;              /* Shift+glisser = deplacer la vue */
        } else {
            g_mode = M_Orbit;
        }
        glfwGetCursorPos(w, &g_prevX, &g_prevY);
    } else {
        if (g_mode == M_Drag) { g_lockAxis = -1; g_dragMulti.clear(); }
        g_mode = M_None;
    }
}

static void onKey(GLFWwindow* w, int key, int scancode, int action, int mods) {
    if (g_dupPending) {
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        const bool topRow = key >= GLFW_KEY_0 && key <= GLFW_KEY_9;
        const bool padRow = key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9;
        if (topRow || padRow) {
            if (g_dupNumBuf.size() < 4)
                g_dupNumBuf += padRow ? char('0' + (key - GLFW_KEY_KP_0)) : char(key);
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            if (g_dupNumBuf.size() < 4) g_dupNumBuf.insert(g_dupNumBuf.begin(), '-');
        } else if (isSeparatorKey(key, scancode)) {
            flashBand("champ nombre entier : separateur ignore (nombre negatif = retire des copies si besoin)");
        } else if (key == GLFW_KEY_BACKSPACE) {
            if (!g_dupNumBuf.empty()) g_dupNumBuf.pop_back();
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            const bool vide = g_dupNumBuf.empty();
            const long n = vide ? 1 : std::atol(g_dupNumBuf.c_str());
            g_dupPending = false;
            g_dupNumBuf.clear();
            dupExecute(vide ? 1 : n);
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
        if (topRowR || padRowR || isSeparatorKey(key, scancode)) {
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
                rotApplyCurrent(g_rotAxis, a);
                g_rotNumBuf.clear();
                rotCommit();           /* Entree = pose, comme le clic gauche */
                g_rotMode = false;     /* sortie du mode (meme contrat que l'extrusion) */
                std::printf("mode selection\n");
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            rotCancel();                /* Echap : annule la rotation ET quitte le mode (sortie de l'etat en cours) */
            g_rotMode = false;
            std::printf("\nmode selection\n");
        } else if (key == GLFW_KEY_R) {
            runToolCommand(IDM_TOOL_ROT);  /* R disponible a tout moment : quitte le mode */
        } else if (key == GLFW_KEY_B && !g_rotMulti.empty()) {
            g_rotPivotKind = (g_rotPivotKind == 1) ? 0 : 1;   /* T1.10 : centre propre <-> barycentre du groupe, angle conserve */
            rotApplyCurrent(g_rotAxis, g_rotAngle);
            std::printf("\rpivot : %s   \n\rRotation %s (multi) : %+.1f deg   ",
                        rotPivotName(), axisName(g_rotAxis), g_rotAngle);
            std::fflush(stdout);
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
        if (topRowP || padRowP || isSeparatorKey(key, scancode)) {
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
            ppCancel();                 /* Echap : annule la face ET quitte le mode P (sortie de l'etat en cours) */
            g_ppMode = false;
            std::printf("\nmode selection\n");
        } else if (key == GLFW_KEY_P) {
            runToolCommand(IDM_TOOL_PP);  /* P disponible a tout moment : quitte le mode */
        }
        return;
    }
    if (g_lineActive) {   /* B2.1 : saisie de la longueur exacte (meme contrat) */
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        const bool topRowL = key >= GLFW_KEY_0 && key <= GLFW_KEY_9;
        const bool padRowL = key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9;
        if (topRowL || padRowL || isSeparatorKey(key, scancode)) {
            if (g_lineNumBuf.size() < 16)
                g_lineNumBuf += (padRowL ? char('0' + (key - GLFW_KEY_KP_0))
                              : (topRowL ? char(key) : char('.')));
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            if (g_lineNumBuf.size() < 16) g_lineNumBuf.insert(g_lineNumBuf.begin(), '-');
        } else if (key == GLFW_KEY_BACKSPACE) {
            if (!g_lineNumBuf.empty()) g_lineNumBuf.pop_back();
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            if (g_lineNumBuf.empty()) {
                lineFinish();          /* Entree vide = fin, comme le clic droit */
            } else {
                const double len = std::atof(g_lineNumBuf.c_str());
                if (!std::isfinite(len) || len <= 0.0)
                    std::printf("\nLongueur invalide - tapez un nombre strictement positif puis Entree.\n");
                else
                    linePoseByLength(len);
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            lineCancel("dessin annule");   /* Echap : tout annule et quitte le mode */
        } else if (key == GLFW_KEY_L) {
            runToolCommand(IDM_TOOL_LINE); /* L disponible a tout moment */
        }
        return;
    }
    if (g_rectActive) {   /* B2.2 : saisie des dimensions exactes "L,H" (meme contrat) */
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        const bool topRowR = key >= GLFW_KEY_0 && key <= GLFW_KEY_9;
        const bool padRowR = key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9;
        if (topRowR || padRowR || isSeparatorKey(key, scancode)) {
            if (g_rectNumBuf.size() < 16)
                g_rectNumBuf += (padRowR ? char('0' + (key - GLFW_KEY_KP_0))
                              : (topRowR ? char(key) : char(key)));
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            if (g_rectNumBuf.size() < 16) g_rectNumBuf.insert(g_rectNumBuf.begin(), '-');
        } else if (key == GLFW_KEY_BACKSPACE) {
            if (!g_rectNumBuf.empty()) g_rectNumBuf.pop_back();
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            if (g_rectNumBuf.empty()) {
                std::printf("\nDimensions vides - tapez L,H puis Entree (clic = coin oppose, Echap = tout annuler).\n");
            } else {
                rectPoseByDims(g_rectNumBuf);
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            rectCancel("dessin annule");   /* Echap : tout annule et quitte le mode */
        } else if (key == GLFW_KEY_R) {
            runToolCommand(IDM_TOOL_RECT); /* R disponible a tout moment */
        }
        return;
    }
    if (action == GLFW_PRESS) {
        /* Ctrl+S = enregistrer le projet (retour utilisateur) ; position : avant les exports scene par O/S */
        if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_S) {
            handleMenuAction(IDM_SAVE_PROJECT);
            return;
        }
        /* Ctrl+C / Ctrl+V : copier / coller la selection (T1.12) ;
           C sans Ctrl reste le recentrage sur le point vise */
        if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_C) { copySelected(); return; }
        if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_V) { pasteClipboard(); return; }
        if (key == GLFW_KEY_ESCAPE) {
            /* Echelle Echap : d'abord sortir de l'etat en cours (mode P/R en attente,
               puis deselection) ; Echap ne ferme JAMAIS l'application (rebonds de
               clavier) : fermeture volontaire par Fichier > Quitter uniquement. */
            if (g_lineMode) { lineCancel("dessin annule"); return; }   /* B2.1 */
            if (g_rectMode) { rectCancel("dessin annule"); return; }   /* B2.2 */
            if (g_ppMode || g_rotMode) { g_ppMode = false; g_rotMode = false; std::printf("\nmode selection\n"); return; }
            if (selectedCount() > 0) { clearSelection(); std::printf("\nselection annulee\n"); return; }
        }
        if (key == GLFW_KEY_P && g_mode == M_None && !g_dupPending && !g_lineMode) runToolCommand(IDM_TOOL_PP);
        if (key == GLFW_KEY_R && !(mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending && !g_lineMode) runToolCommand(IDM_TOOL_ROT);
        if (key == GLFW_KEY_R && (mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending && !g_rectActive)
            runToolCommand(IDM_TOOL_RECT);   /* B2.2 : Maj+R = rectangle (R seul = Tourner) */
        if (key == GLFW_KEY_L && g_mode == M_None && !g_dupPending && !g_lineActive)
            runToolCommand((mods & GLFW_MOD_SHIFT) ? IDM_TOOL_LIGNE_FIL : IDM_TOOL_LINE);  /* B2.5b : L = surface, Maj+L = fil ouvert */
        if (g_rotMode && !g_rotActive) {
            const char c = axisLetterFromScancode(scancode);
            if (c) { rotBegin(w, c - 'x'); return; }
        }
        if (key == GLFW_KEY_DELETE && g_mode == M_None) {   /* Suppr : supprime toute la selection (1 objet comme plusieurs) */
            deleteSelectedSet();
            return;
        }
        if (key == GLFW_KEY_F) frameSelected();
        if (key == GLFW_KEY_H) { goHome(); std::printf("vue d'origine\n"); }
        if (key == GLFW_KEY_C && !(mods & GLFW_MOD_CONTROL)) {  /* centrer sur le point vise */
            Vec3 org, dir, p;
            if (rayFromMouse(w, org, dir) && pickPointFromRay(org, dir, p)) {
                g_cam.target = p;
                std::printf("vue centree sur le point vise\n");
            } else {
                std::printf("C : aucun point vise (pointez un objet ou le sol)\n");
            }
        }
        if (key == GLFW_KEY_I) { importFile(); }   /* importer OBJ/STL */
        /* B1.7 : touches O/S supprimees (collisions avec le format prefere) ;
           export par Fichier > Exporter la scene / la selection uniquement */
    }
    recordAxisKey(scancode, action);
}

/* B1 : boutons de la barre d'outils flottante = memes actions que les
   raccourcis (les branches P/R de onKey appellent ceci : un seul chemin
   de code, le bouton ne peut pas diverger du raccourci). */
static void runToolCommand(int id) {
    switch (id) {
    case IDM_TOOL_SELECT:      /* = Echap, sans jamais fermer l'application */
        if (g_lineMode) { lineCancel("dessin annule"); }
        else if (g_ppActive) { ppCancel(); g_ppMode = false; std::printf("\nmode selection\n"); }
        else if (g_rotActive) { rotCancel(); g_rotMode = false; std::printf("\nmode selection\n"); }
        else if (g_ppMode || g_rotMode) { g_ppMode = false; g_rotMode = false; std::printf("\nmode selection\n"); }
        else if (selectedCount() > 0) { clearSelection(); std::printf("\nselection annulee\n"); }
        return;
    case IDM_TOOL_PP:          /* = touche P */
        if (g_ppMode) {
            ppCancel();          /* annule la face en suspens si active, sans effet sinon */
            g_ppMode = false;
            std::printf("\nmode selection\n");
        } else if (g_mode == M_None && !g_dupPending && !g_lineMode) {
            g_ppMode = true;
            g_rotMode = false;
            std::printf("mode Push/Pull : cliquez une face (dessus ou cote), deplacez, puis re-cliquez pour poser\n");
        }
        return;
    case IDM_TOOL_ROT:         /* = touche R */
        if (g_rotMode) {
            rotCancel();         /* annule la rotation en suspens si active */
            g_rotMode = false;
            std::printf("\nmode selection\n");
        } else if (g_mode == M_None && !g_dupPending && !g_lineMode) {
            g_rotMode = true;
            g_ppMode = false;
            std::printf("mode Rotation : selectionnez un objet (Ctrl+clic pour en selectionner plusieurs), puis X, Y ou Z (lettre) pour fixer l'axe\n");
        }
        return;
    case IDM_TOOL_FRAME:      /* = F */
        frameSelected();
        return;
    case IDM_TOOL_HOME:       /* = H */
        goHome();
        std::printf("vue d'origine\n");
        return;
    case IDM_TOOL_COPY:       /* = Ctrl+C */
        copySelected();
        return;
    case IDM_TOOL_PASTE:      /* = Ctrl+V */
        pasteClipboard();
        return;
    case IDM_TOOL_LINE:        /* = touche L : ligne fermee en surface (B2.5) */
    case IDM_TOOL_LIGNE_FIL:   /* = Maj+L : ligne en fil ouvert (B2.5b) */
        if (g_lineMode) {
            lineCancel("mode ligne quitte");
        } else if (g_mode == M_None && !g_dupPending && !g_lineActive && !g_rectMode) {
            g_lineFerme = (id == IDM_TOOL_LINE);   /* choix par la commande, rien de memorise */
            g_lineMode = true;
            lineReset();
            g_ppMode = false;
            g_rotMode = false;
            std::printf(id == IDM_TOOL_LINE
                ? "mode Ligne (fermee en surface) : clics = points du fil ; clic sur le depart = fermeture ; clic droit = fin ; Echap = tout annuler\n"
                : "mode Ligne (fil ouvert) : clics = points du fil ; clic sur le depart = boucle sans surface ; clic droit = fin ; Echap = tout annuler\n");
        }
        return;
    case IDM_TOOL_RECT:        /* = Maj+R (B2.2) */
        if (g_rectMode) {
            rectCancel("mode rectangle quitte");
        } else if (g_mode == M_None && !g_dupPending && !g_lineActive && !g_lineMode && !g_rectActive) {
            g_rectMode = true;
            rectReset();
            g_ppMode = false;
            g_rotMode = false;
            std::printf("mode Rectangle : clic = premier coin ; clic = coin oppose pose ; chiffres L,H + Entree = dimensions exactes ; Echap = tout annuler\n");
        }
        return;
#ifdef _WIN32
    /* B1.5 : commandes Affichage / Preferences, a present dans le dispatcher */
    case IDM_VIEW_TOOLBAR:
        toolbarSetVisible(g_setup.toolVisible == 0);
        return;
    case IDM_VIEW_TOOLBAR_HOR:
        toolbarSetVertical(false);
        return;
    case IDM_VIEW_TOOLBAR_VERT:
        toolbarSetVertical(true);
        return;
    case IDM_PREF_SCENE_OBJ:
        prefSetExportFormat(true, false);
        return;
    case IDM_PREF_SCENE_STL:
        prefSetExportFormat(true, true);
        return;
    case IDM_PREF_SEL_OBJ:
        prefSetExportFormat(false, false);
        return;
    case IDM_PREF_SEL_STL:
        prefSetExportFormat(false, true);
        return;
#endif
    default:
        return;
    }
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
    /* les faces sont reculees d'un poil (polygon offset) pour que les aretes
       posees dessus restent visibles, surtout quand la piece est claire */
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glEnable(GL_LIGHTING);
    glBegin(GL_TRIANGLES);
    for (size_t i = 0, t = 0; i < o.tri.size(); i += 3, ++t) {
        /* (T1.7.1) defense : un tri dont les indices depassent les donnees est ignore */
        if (t >= o.n.size() || o.tri[i] >= o.v.size() || o.tri[i + 1] >= o.v.size() || o.tri[i + 2] >= o.v.size())
            continue;
        glNormal3dv(&o.n[t].x);
        if (selected) glColor3d(0.45, 0.7, 0.9);
        else glColor3dv(o.col);
        for (int k = 0; k < 3; ++k) glVertex3dv(&o.v[o.tri[i + k]].x);
    }
    glEnd();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_LIGHTING);
    glBegin(GL_LINES);
    if (selected) {
        /* surlignage marque (retour de test : eviter d'agir sur la piece
           d'a cote) : jaune vif et epais, lisible sur tout fond */
        glColor3d(1.0, 0.85, 0.0);
        glLineWidth(2.5);
    }
    else if (o.tri.empty()) glColor3dv(o.col);   /* B2.1c : fil = couleur propre (le gris sombre etait invisible sur le fond) */
    else glColor3d(0.2, 0.2, 0.22);
    for (const auto& e : o.edges) {
        if (e.first < 0 || e.first >= (int)o.v.size() || e.second < 0 || e.second >= (int)o.v.size()) continue;
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
        drawObject(g_objs[i], isSelectedRaw((int)i) && g_mode != M_Drag);
}

/* bandeau de statut en bas de fenetre (petite barre d'infos, façon SketchUp) :
   affiche l'invite du mode courant et la valeur en cours — rien dans la
   console de saisie, tout dans la fenetre */
static std::string statusPrompt() {
    char buf[256];
    if (!g_bandFlash.empty()) {
        if (glfwGetTime() < g_bandFlashUntil) return g_bandFlash;
        g_bandFlash.clear();
    }
    if (g_lineActive)
        return "Ligne : clic = prolonger | clic droit = fin | chiffres + Entree = longueur exacte | Echap = tout annuler";
    if (g_rectActive)
        return "Rectangle : clic = coin oppose (pose) | chiffres L,H + Entree = dimensions exactes | Echap = tout annuler";
    if (g_dupPending)
        return "Dupliquer : tapez le nombre d instances puis Entree (vide = 1, negatif = retrait, Echap pour annuler)";
    else if (g_ppActive)
        std::snprintf(buf, sizeof(buf), "Extrusion : %+.3f  (clic gauche : poser ; chiffres puis Entree : dimension exacte, + = tirer / - = pousser ; clic droit ou Echap : annuler)", g_pp.d);
    else if (g_rotActive && !g_rotMulti.empty())
        std::snprintf(buf, sizeof(buf), "Rot multi %s %zu objets (%s) : %+.1f deg  (B : pivot ; clic : poser ; chiffres + Entree : angle ; clic droit : annuler)", axisName(g_rotAxis), g_rotMulti.size(), rotPivotName(), g_rotAngle);
    else if (g_rotActive)
        std::snprintf(buf, sizeof(buf), "Rotation %s : %+.1f deg  (clic gauche : poser ; X/Y/Z : changer d axe ; chiffres puis Entree : angle exact pose et quitte le mode ; clic droit ou Echap : annuler)", axisName(g_rotAxis), g_rotAngle);
    else if (g_ppMode) {
        return "Push/Pull : cliquez une face d'un objet";
    } else if (g_rotMode) {
        return "Rotation : selectionnez un objet, puis la lettre de l'axe (X, Y ou Z)";
    } else {
        /* B1.7c (retour utilisateur) : le pied de page ne rappelle PLUS les
           commandes (il y en aura trop). Idle = vide ; il n'affiche que les
           options / infos de la commande en cours. Les raccourcis sont
           rappelles dans les menus qui les proposent et dans l'entete console. */
        return "";
    }
    return buf;
}

/* zone de saisie du bandeau : (libelle, buffer tape) - la meme boite pour
   chaque cas qui accepte une valeur clavier (duplication, extrusion, rotation) */
static bool inputZoneState(const char** label, const std::string** buf) {
    if (g_lineActive) { *label = "longueur";  *buf = &g_lineNumBuf; return true; }
    if (g_rectActive) { *label = "L,H";       *buf = &g_rectNumBuf; return true; }
    if (g_dupPending) { *label = "nombre";    *buf = &g_dupNumBuf; return true; }
    if (g_ppActive)   { *label = "dimension"; *buf = &g_pp.numBuf; return true; }
    if (g_rotActive)  { *label = "angle";     *buf = &g_rotNumBuf; return true; }
    return false;
}

static void drawTextClipped(float x, float y, const char* s, float maxW) {
#ifdef _WIN32
    const std::string str(s);
    if (textWidthPx(str.c_str()) <= maxW) { drawText2D(x, y, s); return; }
    /* reduction REELLE du texte : la longueur de reference diminue d'un pas a
       chaque tour. L'ancien code repartait de la taille GONFLEE par les "..."
       rajoutes (substr sur txt.size() - 1, puis + "...") : aucun tour ne
       pouvait passer sous la largeur une fois le message trop long, la boucle
       tournait a l'infini = fenetre grisee "ne repond pas" (retour de test
       T1.9 : bandeau rotation multi a chaque essai). */
    std::string txt(s);
    size_t n = txt.size();
    while (n > 1) {
        txt = str.substr(0, n - 1) + "...";
        if (textWidthPx(txt.c_str()) <= maxW) break;
        --n;
    }
    if (textWidthPx(txt.c_str()) > maxW) txt.clear();   /* rien ne rentre : plus rien */
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

/* compteur de selection en temps reel dans le TITRE de la fenetre (retour de
   test : detecter les erreurs de selection) — maj seulement au changement. */
static void updateWindowTitle(GLFWwindow* w) {
    static std::string last;
    char titre[192];
    const std::vector<int> sel = selectedIndices();
    if (sel.size() > 1)
        std::snprintf(titre, sizeof(titre), "Sketch3D - build %s - %zu objet(s) - selection : %d/%zu objets",
                      BUILD_VERSION, g_objs.size(), (int)sel.size(), g_objs.size());
    else if (sel.size() == 1)
        /* sel[0] : la liste a deja ete elaguee des indices invalides (jamais d'acces perime) */
        std::snprintf(titre, sizeof(titre), "Sketch3D - build %s - %zu objet(s) - selection : %s (%d/%zu)",
                      BUILD_VERSION, g_objs.size(), g_objs[(size_t)sel[0]].name.c_str(),
                      sel[0] + 1, g_objs.size());
    else
        std::snprintf(titre, sizeof(titre), "Sketch3D - build %s - %zu objet(s) - selection : aucune",
                      BUILD_VERSION, g_objs.size());
    const std::string t(titre);
    if (t != last) { last = t; glfwSetWindowTitle(w, t.c_str()); }
}

int main() {
    /* entete console : court, groupe par theme (retour utilisateur : trop de
       verbiage noie les informations importantes) */
    std::printf("Sketch3D - build %s\n", BUILD_VERSION);
    std::printf("Outils  : P extraire une face | R tourner (X/Y/Z = axe, B pivot, chiffres+Entree) | L ligne (clic sur le depart = fermer ; Maj+L fil) | Maj+R rectangle (clics = coins, L,H = exact) | Ctrl+C/V | Suppr\n");
    std::printf("Vue     : orbite = clic gauche dans le vide | zoom = molette | panoramique = molette-clic ou Shift+glisser | C centrer | F cadrer | H origine\n");
    std::printf("Projets : Ctrl+S enregistrer | clic droit = infos objet + duplication | I importer\n");
    std::printf("Formats : export scene/selection = menu Fichier, format defini dans Preferences\n");
    g_setupPath = setupFilePath();
    loadSetup();                     /* B0 : preferences de la derniere session */
    if (g_setup.winW > 0) g_winW = g_setup.winW;
    if (g_setup.winH > 0) g_winH = g_setup.winH;
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
    /* B0 : geometrie de fenetre sauvegardee (position 2 ecrans incluse) */
    glfwSetWindowPos(win, g_setup.winX, g_setup.winY);
    if (g_setup.maximized) glfwMaximizeWindow(win);

    while (!glfwWindowShouldClose(win)) {
        if (g_mode == M_Orbit) {
            double x, y;
            glfwGetCursorPos(win, &x, &y);
            g_cam.theta += (x - g_prevX) * 0.01;
            g_cam.phi += (y - g_prevY) * 0.01;
            if (g_cam.phi < 0.02) g_cam.phi = 0.02;
            if (g_cam.phi > 3.12) g_cam.phi = 3.12;
            g_prevX = x; g_prevY = y;
        } else if (g_mode == M_Pan) {
            double x, y;
            glfwGetCursorPos(win, &x, &y);
            cameraPan(g_cam, x - g_prevX, y - g_prevY, (double)g_winH);
            g_prevX = x; g_prevY = y;
        } else if (g_mode == M_Drag) {
            updateDrag(win);
        } else if (g_ppActive) {
            updatePP(win);
        } else if (g_rotActive) {
            updateRot(win);           /* B2.1b : branche RESTAUREE (remplacee par erreur
                                         dans B2.1 : la rotation souris ne seguait plus) */
        } else if (g_lineMode) {
            lineTrackCursor(win);     /* B2.1 : apercu du fil vers le curseur */
        } else if (g_rectMode) {
            rectTrackCursor(win);     /* B2.2 : apercu du rectangle vers le curseur */
        }
        updateWindowTitle(win);
        render();
        drawAxisGuide();
        drawLinePreview();
        drawRectPreview();
        drawStatusBand();
        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    {   /* B0 : geometrie de la fenetre au moment de la fermeture */
        int px, py, w2, h2;
        glfwGetWindowPos(win, &px, &py);
        glfwGetWindowSize(win, &w2, &h2);
        g_setup.winX = px; g_setup.winY = py; g_setup.winW = w2; g_setup.winH = h2;
        g_setup.maximized = (glfwGetWindowAttrib(win, GLFW_MAXIMIZED) == GLFW_TRUE) ? 1 : 0;
    }
    saveSetup();
    glfwTerminate();
    return 0;
}
