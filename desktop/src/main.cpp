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
#include <cstdarg>
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
static const char* BUILD_VERSION = "B3.7";   /* collage generique 2 objets : menu contextuel Coller sur..., accroche intelligente, alignement arete/arete */

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
    /* B2.7e : PARAMETRES DE CONSTRUCTION (source de verite) - la geometrie
       v/tri/edges n'est qu'un derive, regenerable par traceRegen avec les
       MEMES fonctions que le trace (un seul chemin de code). tracePts sont
       LOCAUX (pos + scl*p = monde), comme v. */
    int traceType = 0;          /* 0 = aucun ; 1 = LIGNE ; 2 = SPLINE passage ; 3 = SPLINE controle ;
                                   4 = RECT ; 5 = CERCLE ; 6 = ARC ; 7 = POLY ;
                                   8 = EXTRUSION ; 9 = BALAYAGE (sweep, B3) */
    int traceFerme = 0;         /* 1 = boucle fermee */
    int traceSurface = 0;       /* 1 = feuille fermee en surface (fan, ligne B2.5) */
    std::vector<Vec3> tracePts; /* points de definition */
    int traceProfil = 0;        /* B2.7e E3 : type 8/9 = sous-type du profil (1..7) */
    Vec3 traceVec{0, 0, 0};     /* B2.7e E3 : type 8 = vecteur d extrusion ; type 9 = normale
                                   de la face au balayage (roll) (LOCALES, comme v) */
    int traceCheminType = 0;    /* B3 : type 9 = sous-type du CHEMIN (1, 2/3, 6) */
    int traceCheminFerme = 0;   /* B3 : chemin boucle = solide torique (sans caps) */
    std::vector<Vec3> traceChemin; /* B3 : points de definition du chemin (LOCALES) */
    int traceAccroche = -1;     /* B3.6a : accroche PINCEE (index dans sweepAccroches ;
                                   -1 = collage intelligent au trace) */
    double traceRoll = 0.0;     /* B3.6b : rotation du profil autour du fil, DEGRES
                                   (0 = defaut "coller plat" B3.5, persiste "roll N") */
};

static std::vector<Object> g_objs;
static int g_selected = -1;

/* multi-selection (T1.7) : g_selected = selection principale (clic gauche),
   g_selMulti = objets supplementaires (Ctrl+clic : ajoute/retire) ;
   les actions (export, suppression, duplication) portent sur l'ensemble. */
static std::vector<int> g_selMulti;

/* B2.7e : edition des traces apres coup - etat declare TOT (utilise par
   handleMenuAction comme par les callbacks ; lecon B1.7b : prototype avant
   premiere utilisation) ; le reste du bloc edition est defini apres les
   splines (il reutilise splSample). */
static bool g_editMode = false;         /* mode edition arme (un seul objet) */
static int g_editObj = -1;              /* objet edite (index dans g_objs) */
static bool g_sweepEditChemin = false;  /* B3.3 : cible des ajouts/suppressions sur un balayage (O) */
static bool g_sweepEditAcc = false;     /* B3.6a : cible ACCROCHE - choisir le point de connexion (O) */
static bool g_sweepEditRoll = false;    /* B3.6b : cible ROLL - tourner le profil autour du fil (O) */
static bool g_rollDrag = false;         /* B3.6b : drag de la poignee de roll en cours */
static double g_rollAng0 = 0.0;         /* B3.6b : roll au debut du drag (deg) */
static Vec3 g_rollV0;                   /* B3.6b : direction de reference du drag (perp. a l'axe) */
static Vec3 g_sweepRollAxis;            /* B3.6b : repere du roll POSE PAR LA REGEN (Tv[0]) */
static Vec3 g_sweepRollCenter;          /* B3.6b : centre du roll (accroche, monde) */
static Vec3 g_sweepRollHandle;          /* B3.6b : poignee violette (profil le plus loin de l'accoche) */

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

/* message ephemere affiche dans le bandeau de la FENETRE (retour de test :
   les avis de saisie doivent se voir dans l'application, pas en console) */
static std::string g_bandFlash;
static double g_bandFlashUntil = 0.0;

static void flashBand(const char* msg) {
    g_bandFlash = msg;
    g_bandFlashUntil = glfwGetTime() + 3.0;
}

/* B3.3f (REGLE utilisateur) : console = debug uniquement ; tout message
   utilisateur va DANS LA FENETRE - bandeau + JOURNAL consultable (touche J,
   panneau des derniers messages). L'echo console reste pendant le
   developpement uniquement. */
static std::vector<std::string> g_journal;   /* messages UNIQUES, ordre d'arrivee */
static bool g_journalVisible = false;
static void userMsg(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    /* B3.3h/i (retour test) : pas d'echo console ; un message deja
       present ne rajoute PAS de ligne (ni compteur, inutile) - il
       reanime seulement le bandeau */
    for (size_t i = g_journal.size(); i > 0; --i)
        if (g_journal[i - 1] == buf) { flashBand(buf); return; }
    g_journal.push_back(buf);
    if (g_journal.size() > 200) g_journal.erase(g_journal.begin());
    flashBand(buf);
}
static Mode g_mode = M_None;
static bool g_rightDown = false;   /* B3.6a-bis : clic droit tenu (pan armable meme si Maj arrive apres) */

/* ===== B3.6z : Ctrl+Z annulation / retablissement =====
   Modele : pile d'ETATS AVANT mutation - chaque pas = copie COMPLETE de la
   scene (les objets portent leurs parametres de construction, principe
   B2.7e : la copie restaure tout). UN pas par GESTE (pas par frame). */
struct UndoPas { std::vector<Object> objs; std::string label; };
static const size_t kUndoMax = 100;
static std::vector<UndoPas> g_undoPile;
static std::vector<UndoPas> g_redoPile;
static UndoPas g_undoEnCours;          /* geste continu : etat capture au debut */
static bool g_undoOuvert = false;      /* un geste continu est capture */
static bool g_undoSale = false;        /* le geste ouvert a vraiment mute */
static void editExit();                /* lecon B1.7b : prototype avant usage */

static void undoPousse(UndoPas p) {
    g_undoPile.push_back(std::move(p));
    if (g_undoPile.size() > kUndoMax) g_undoPile.erase(g_undoPile.begin());
    g_redoPile.clear();
}
/* one-shot : APPELER AVANT la mutation (pose, suppression, edition ponctuelle) */
static void undoSnapshot(const char* label) {
    UndoPas p; p.objs = g_objs; p.label = label;
    undoPousse(std::move(p));
}
/* geste continu : capture au debut ; valide au relachement seulement si sale */
static void undoOuvre(const char* label) {
    if (g_undoOuvert) return;           /* deja capture (double evenement) */
    g_undoEnCours.objs = g_objs; g_undoEnCours.label = label;
    g_undoOuvert = true; g_undoSale = false;
}
static void undoValide() {
    if (g_undoOuvert && g_undoSale) undoPousse(std::move(g_undoEnCours));
    g_undoOuvert = false; g_undoSale = false; g_undoEnCours = UndoPas();
}
/* gestes qui RESTAURENT deja la base au cancel (ppCancel, rotCancel) */
static void undoAbandonne() {
    g_undoOuvert = false; g_undoSale = false; g_undoEnCours = UndoPas();
}
static void undoVide() {
    g_undoPile.clear(); g_redoPile.clear(); undoAbandonne();
}
static void undoRestore(const std::vector<Object>& objs) {
    g_objs = objs;
    clearSelection();
    if (g_editMode && (g_editObj < 0 || g_editObj >= (int)g_objs.size()))
        editExit();                     /* l'objet edite n'existe plus */
}
static void undoAnnule() {
    if (g_undoOuvert) { flashBand("terminez d'abord le geste en cours"); return; }
    if (g_undoPile.empty()) { flashBand("rien a annuler"); return; }
    UndoPas p = std::move(g_undoPile.back()); g_undoPile.pop_back();
    UndoPas cur; cur.objs = g_objs; cur.label = p.label;
    g_redoPile.push_back(std::move(cur));
    undoRestore(p.objs);
    userMsg("annule : %s", p.label.c_str());
}
static void undoRetablit() {
    if (g_undoOuvert) { flashBand("terminez d'abord le geste en cours"); return; }
    if (g_redoPile.empty()) { flashBand("rien a retablir"); return; }
    UndoPas p = std::move(g_redoPile.back()); g_redoPile.pop_back();
    UndoPas cur; cur.objs = g_objs; cur.label = p.label;
    g_undoPile.push_back(std::move(cur));
    undoRestore(p.objs);
    userMsg("retabli : %s", p.label.c_str());
}
/* ===== fin B3.6z ===== */
static Camera g_cam;
static Camera g_camHome;
static double g_prevX = 0, g_prevY = 0;
static double g_lastClickTime = -1.0;
static int g_winW = 1280, g_winH = 800;

/* B3.3h : geometrie du panneau journal, partagee par le dessin et la
   detection de clic (un seul chemin de code) ; LARGEUR FIXE (retour test :
   la largeur ne doit plus varier selon les messages), ancre en HAUT a gauche */
static void journalRect(float& xj, float& yj, float& wj, float& hj, size_t& debut, size_t nAff) {
    const size_t nj = g_journal.size();
    debut = nj > nAff ? nj - nAff : 0;
    xj = 8.0f;
    wj = (float)g_winW * 0.35f;
    if (wj < 220.0f) wj = 220.0f;
    hj = (float)(nj - debut) * 15.0f + 12.0f;
    yj = (float)g_winH - 8.0f - hj;
}
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
    int best = -1, bestF = -1;
    double bestT = 1e300, bestTf = 1e300;
    for (size_t i = 0; i < g_objs.size(); ++i) {
        double t;
        if (rayObject(org, dir, g_objs[i], t) && t < bestT) { bestT = t; best = (int)i; }
        const double tf = filRayHit(org, dir, g_objs[i], filPickTol());
        if (tf >= 0.0 && tf < bestTf) { bestTf = tf; bestF = (int)i; }
    }
    /* B3.6a-bis (retour test : "les splines par controle ne sont pas
       selectionnables") : le fil VISIBLE gagne sur un solide DERRIERE lui.
       Avant, tout solide touche par le rayon masquait TOUS les fils (une
       spline de controle pres d'un tube etait donc incaptable). La marge
       compense l'echantillonnage du rayon (128 pas, erreur < tol). */
    if (bestF >= 0 && (best < 0 || bestTf <= bestT + filPickTol())) return bestF;
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
        userMsg("deplacement multi : %zu objets", g_dragMulti.size());
    }
    g_lockAxis = -1;
    g_mode = M_Drag;
    undoOuvre("deplacement");               /* B3.6z : geste continu capture */
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
        if (axis >= 0) userMsg("deplacement verrouille sur %s", axisName(axis));
        else           userMsg("deplacement libre (plan camera)");
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
    if (delta.norm() > 1e-12) g_undoSale = true;   /* B3.6z : un vrai mouvement = un pas (clic sans bouger = rien) */
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
        userMsg("import OBJ : %zu sommets soudes (%zu restants)", rawCount - o.v.size(), o.v.size());
    if (skipped) userMsg("import OBJ : %zu faces degenerees ou invalides ignorees", skipped);
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
        userMsg("import STL : %zu sommets soudes (%zu restants)", rawCount - o.v.size(), o.v.size());
    return true;
}

#ifdef _WIN32
static std::string fileDialog(bool save, const char* filter, const char* defExt, const char* title,
                              const char* defPath = nullptr) {
    char buf[MAX_PATH] = "";
    /* B3.2c : nom de sauvegarde PRE-REMPLI (dernier projet enregistre/ouvert)
       - sur un gros projet on sauvegarde souvent, un nom vide pousse a
       ecraser le mauvais fichier */
    if (defPath && *defPath) strncpy_s(buf, defPath, _TRUNCATE);
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
static std::string fileDialog(bool, const char*, const char*, const char* title, const char* = nullptr) {
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
    userMsg("attention : pas d'emplacement libre trouve, objet laisse en surimpression");
    return shifts;
}

static void importFile() {
    const std::string path = fileDialog(false,
        "Modeles (*.obj;*.stl)\0*.obj;*.stl\0Tous (*.*)\0*.*\0\0",
        nullptr, "Importer un modele");
    if (path.empty()) return;
    Object o;
    std::string err;
    if (!loadMeshFile(path, o, err)) { userMsg("import echoue : %s", err.c_str()); return; }
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
    userMsg("import : %s — %zu sommets, %zu tris, dimensions locales %.4g x %.4g x %.4g, echelle %.6g, dimensions affichees %.4g x %.4g x %.4g, volume local %.6g, volume affiche %.6g, aretes non-manifold %zu",
                o.name.c_str(), o.v.size(), o.tri.size(),
                o.bmax.x - o.bmin.x, o.bmax.y - o.bmin.y, o.bmax.z - o.bmin.z, o.scl,
                dim.x, dim.y, dim.z, vol, vol * o.scl * o.scl * o.scl, nonManifold);
    if (shifts > 0)
        userMsg("superposition evitee : objet decale de %d pas vers +X, pose a x = %.3f (comme le lot web 2.x)",
                    shifts, o.pos.x);
    undoSnapshot("import");
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
            userMsg("superposition evitee : %s decale de %d pas vers +X (x = %.3f)", o.name.c_str(), shifts, o.pos.x);
        if (made == 0) undoSnapshot("duplication");   /* AVANT la 1re copie */
        g_objs.push_back(std::move(o));
        ++made;
    }
    userMsg("duplication : %s x%d (copies posees vers +X, pas de %.3f)", src.name.c_str(), made, step);
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
    if (rem > 0) undoSnapshot("retrait de copies");
    for (int r = 0; r < rem; ++r) {
        g_objs.erase(g_objs.begin() + hits[(size_t)r].first);
        renumberSelectionAfterDelete(hits[(size_t)r].first);
    }
    if (rem > 0)
        userMsg("retrait : %d copie(s) supprimee(s) de la serie %s (%d instance(s) restante(s))",
                    rem, g_objs[(size_t)idx].name.c_str(), (int)hits.size() - rem);
    return rem;
}

/* suppression d'un objet (menu contextuel) : l'original comme une copie ;
   la selection suit si possible (objet suivant a la meme place).        */
static void deleteObject(int idx) {
    if (idx < 0 || idx >= (int)g_objs.size()) return;
    const std::string nm = g_objs[(size_t)idx].name;
    undoSnapshot("suppression");
    g_objs.erase(g_objs.begin() + idx);
    renumberSelectionAfterDelete(idx);
    userMsg("suppression : %s (%zu objet(s) restant(s))", nm.c_str(), g_objs.size());
}

/* suppression de TOUTE la selection (T1.7 : 1 objet comme plusieurs) ;
   reconstruction en une passe, selection videe apres coup.              */
static size_t deleteSelectedSet() {
    const std::vector<int> idxs = selectedIndices();
    if (idxs.empty()) return 0;
    if (idxs.size() == 1) { deleteObject(idxs[0]); return 1; }
    undoSnapshot("suppression");
    std::set<int> kill(idxs.begin(), idxs.end());
    std::vector<Object> keep;
    keep.reserve(g_objs.size() - kill.size());
    for (size_t i = 0; i < g_objs.size(); ++i) {
        if (kill.count((int)i)) continue;
        keep.push_back(std::move(g_objs[i]));
    }
    g_objs = std::move(keep);
    clearSelection();
    userMsg("suppression : %d objet(s) supprimes (%zu restant(s))", (int)idxs.size(), g_objs.size());
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
    userMsg("Dupliquer : tapez le nombre d'instances puis Entree (vide = 1, maximum 999 ; negatif = retire des copies ; Echap pour annuler)");
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
            userMsg("retrait : aucune copie numerotee dans la selection");
        return;
    }
    userMsg("Nombre invalide : entrez de -999 a 999 puis Entree (negatif = retire des copies)");
}


/* B0 : fichier de setup - INI editable a cote de l exe (sketch3d.ini) ;
   charge au lancement, sauvegarde a la fermeture + a chaque changement de
   preference (choix valide). Contenu v1 : geometrie de fenetre, dernier
   projet, formats d'export reels (memorises au fil des exports). */
static const size_t kProjetRecents = 8;   /* B3.2d : historique des derniers projets */
struct Setup {
    int winX = 100, winY = 60;
    int winW = 0, winH = 0;      /* 0 = garder les valeurs par defaut du programme */
    int maximized = 0;
    std::string dernierProjet;
    std::vector<std::string> historique;   /* B3.2d : les N derniers, les plus recents d'abord */
    int nbRecents = 8;           /* B3.2e : taille de l'historique, editable dans l'INI (0 = aucun) */
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
    if (!f) { userMsg("setup : ecriture impossible (%s)", g_setupPath.c_str()); return; }
    f << "[fenetre]\n"
      << "x = " << g_setup.winX << "\n"
      << "y = " << g_setup.winY << "\n"
      << "w = " << g_setup.winW << "\n"
      << "h = " << g_setup.winH << "\n"
      << "max = " << g_setup.maximized << "\n"
      << "\n[projet]\n"
      << "dernier = " << g_setup.dernierProjet << "\n"
      << "historique = ";
    for (size_t i = 0; i < g_setup.historique.size(); ++i)
        f << (i ? ";" : "") << g_setup.historique[i];
    f << "\n"
      << "recents = " << g_setup.nbRecents << "\n"
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
            else if (k == "historique") {           /* B3.2d : chemins separes par ';' */
                g_setup.historique.clear();
                size_t deb = 0;
                while (deb <= v.size()) {
                    const size_t pt = v.find(';', deb);
                    const std::string morceau = setupTrim(
                        v.substr(deb, (pt == std::string::npos ? v.size() : pt) - deb));
                    if (!morceau.empty()) g_setup.historique.push_back(morceau);
                    if (pt == std::string::npos) break;
                    deb = pt + 1;
                }
                if (g_setup.historique.size() > kProjetRecents)
                    g_setup.historique.resize(kProjetRecents);
            }
            else if (k == "recents") {              /* B3.2e : taille editable, bornee 0..8 */
                const int n = std::atoi(v.c_str());
                g_setup.nbRecents = (n < 0) ? 0 : (n > (int)kProjetRecents ? (int)kProjetRecents : n);
                if (g_setup.historique.size() > (size_t)g_setup.nbRecents)
                    g_setup.historique.resize((size_t)g_setup.nbRecents);
            }
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
    /* B3.2e : l'INI peut etre edit a la main dans n'importe quel ordre de
       cles - l'historique est borne par recents APRES lecture complete */
    if (g_setup.historique.size() > (size_t)g_setup.nbRecents)
        g_setup.historique.resize((size_t)g_setup.nbRecents);
}

#ifdef _WIN32
static void recentsMenuRefresh();   /* B3.2d : defini plus bas (menus natifs) - PROTOTYPE AVANT USAGE */
#endif
/* chaque changement de preference : sauvegarde immediate ; B3.2d : mise a
   jour de l'historique (deduplique, les plus recents d'abord, borne a N) */
static void noteProjectPath(const std::string& p) {
    if (p.empty()) return;
    for (size_t i = 0; i < g_setup.historique.size(); ++i)
        if (g_setup.historique[i] == p) { g_setup.historique.erase(g_setup.historique.begin() + i); break; }
    g_setup.historique.insert(g_setup.historique.begin(), p);
    if (g_setup.historique.size() > (size_t)g_setup.nbRecents)
        g_setup.historique.resize((size_t)g_setup.nbRecents);
    if (g_setup.dernierProjet != p) g_setup.dernierProjet = p;
    saveSetup();
#ifdef _WIN32
    recentsMenuRefresh();
#endif
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
    userMsg("copie : %zu objet(s) dans le presse-papiers", g_clipboard.size());
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
    undoSnapshot("collage");
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
    userMsg("colle : %zu objet(s) poses vers +X (decalage %.3f)", g_clipboard.size(), step);
}

/* ------------------------------------------------------------------ */
/* Export OBJ / STL                                                    */
/* ------------------------------------------------------------------ */

static void exportOBJ(const std::string& path, const std::vector<int>& only = {}) {
    std::ofstream f(path);
    if (!f) { userMsg("export OBJ impossible : %s", path.c_str()); return; }
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
        userMsg("export OBJ : %s (objet : %s, en coordonnees monde)", path.c_str(), g_objs[(size_t)cible[0]].name.c_str());
    else if (cible.size() > 1)
        userMsg("export OBJ : %s (selection : %d objet(s), en coordonnees monde)", path.c_str(), (int)cible.size());
    else
        userMsg("export OBJ : %s (%zu objets, en coordonnees monde)", path.c_str(), g_objs.size());
}

static void exportSTL(const std::string& path, const std::vector<int>& only = {}) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { userMsg("export STL impossible : %s", path.c_str()); return; }
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
        userMsg("export STL binaire : %s (objet : %s, %zu tris)", path.c_str(), g_objs[(size_t)cible[0]].name.c_str(), total);
    else if (cible.size() > 1)
        userMsg("export STL binaire : %s (selection : %d objet(s), %zu tris)", path.c_str(), (int)cible.size(), total);
    else
        userMsg("export STL binaire : %s (%zu tris)", path.c_str(), total);
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
        /* B2.7e : parametres de construction (source de verite) - bloc
           optionnel : les vieux projets (sans trace) restent relisibles */
        if (o.traceType != 0) {
            f << "trace " << o.traceType << " " << o.traceFerme << " " << o.traceSurface
              << " " << o.tracePts.size();
            if (o.traceType == 8)                       /* E3 : profil + vecteur d extrusion */
                f << " " << o.traceProfil << " " << o.traceVec.x << " " << o.traceVec.y
                  << " " << o.traceVec.z;
            if (o.traceType == 9)                       /* B3 : profil + normale (roll) + chemin */
                f << " " << o.traceProfil << " " << o.traceVec.x << " " << o.traceVec.y
                  << " " << o.traceVec.z << " " << o.traceCheminType << " "
                  << o.traceCheminFerme << " " << o.traceChemin.size();
            f << "\n";
            for (const Vec3& p : o.tracePts) f << p.x << " " << p.y << " " << p.z << "\n";
            if (o.traceType == 9)
                for (const Vec3& p : o.traceChemin) f << p.x << " " << p.y << " " << p.z << "\n";
            if (o.traceType == 9 && o.traceAccroche >= 0)  /* B3.6a : accroche pincee */
                f << "accroche " << o.traceAccroche << "\n";
            if (o.traceType == 9 && std::fabs(o.traceRoll) > 1e-9) {   /* B3.6b : roll (deg) */
                char rl[48];
                std::snprintf(rl, sizeof(rl), "%.10g", o.traceRoll);
                f << "roll " << rl << "\n";
            }
        }
    }
    if (!f) { err = "ecriture interrompue"; return false; }
    userMsg("projet enregistre : %s (%zu objets)", path.c_str(), g_objs.size());
    return true;
}

static bool loadProject(const std::string& path, std::string& err);
static void traceRegen(Object& o);   /* B2.7e : regeneration par les MEMES fonctions que le trace */

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
        /* B2.7e : bloc optionnel des parametres de construction (source de
           verite). Peek par LIGNE avec RETOUR ARRIERE du flux si la ligne
           suivante n'est pas "trace" (jamais f >> tok : le jeton "objet"
           suivant serait consomme et la boucle casserait) ; le bloc est
           absent dans les vieux projets = retrocompatibles. */
        {
            const std::streampos avant = f.tellg();
            std::string finLigne, probe;
            std::getline(f, finLigne);              /* reste de la ligne des tris */
            std::getline(f, probe);                 /* ligne suivante (peek) */
            if (!f.eof() && trimStr(probe).rfind("trace ", 0) == 0) {
                std::istringstream is(probe);
                std::string tag;
                if (!(is >> tag >> o.traceType >> o.traceFerme >> o.traceSurface)
                    || o.traceType < 0 || o.traceType > 9) {
                    err = "fichier corrompu (entete de trace)"; return false;
                }
                o.traceProfil = 0;
                o.traceVec = Vec3();
                o.traceCheminType = 0;
                o.traceCheminFerme = 0;
                o.traceChemin.clear();
                size_t ntp = 0, nch = 0;
                if (!(is >> ntp) || ntp > 20000000ull) { err = "fichier corrompu (compteur de trace)"; return false; }
                if (o.traceType == 8) {                 /* E3 : sous-type + vecteur */
                    if (!(is >> o.traceProfil) || o.traceProfil < 1 || o.traceProfil > 7
                        || !(is >> o.traceVec.x >> o.traceVec.y >> o.traceVec.z)) {
                        err = "fichier corrompu (entete d extrusion)"; return false;
                    }
                }
                if (o.traceType == 9) {                 /* B3 : profil + normale + chemin */
                    if (!(is >> o.traceProfil) || o.traceProfil < 1 || o.traceProfil > 7
                        || !(is >> o.traceVec.x >> o.traceVec.y >> o.traceVec.z)
                        || !(is >> o.traceCheminType) || o.traceCheminType < 1
                        || o.traceCheminType > 7
                        || !(is >> o.traceCheminFerme)
                        || !(is >> nch) || nch > 20000000ull) {
                        err = "fichier corrompu (entete de balayage)"; return false;
                    }
                }
                o.tracePts.resize(ntp);
                for (Vec3& p : o.tracePts)
                    if (!(f >> p.x >> p.y >> p.z)) { err = "fichier corrompu (points du trace)"; return false; }
                if (o.traceType == 9) {
                    o.traceChemin.resize(nch);
                    for (Vec3& p : o.traceChemin)
                        if (!(f >> p.x >> p.y >> p.z)) { err = "fichier corrompu (points du chemin)"; return false; }
                    /* B3.6a : ligne optionnelle "accroche N" (vieux projets = -1,
                       collage intelligent) - peek par ligne avec retour arriere */
                    const std::streampos avantAcc = f.tellg();
                    std::string finL, probeAcc;
                    std::getline(f, finL);
                    std::getline(f, probeAcc);
                    bool accLue = false;
                    if (!f.eof() && trimStr(probeAcc).rfind("accroche ", 0) == 0) {
                        accLue = true;      /* B3.6b : la ligne accroche est CONSUMEE
                                               (le peek roll doit savoir ou il en est) */
                        std::istringstream ia(probeAcc);
                        std::string tagA;
                        if (!(ia >> tagA >> o.traceAccroche) || o.traceAccroche < -1) {
                            err = "fichier corrompu (accroche)"; return false;
                        }
                    } else {
                        f.clear();
                        f.seekg(avantAcc);
                        o.traceAccroche = -1;
                    }
                    /* B3.6b : ligne optionnelle "roll N" (degres) - peek APRES
                       l'eventuel accroche, meme motif : les deux ordres
                       (roll seul, accroche + roll) fonctionnent */
                    const std::streampos avantRoll = f.tellg();
                    std::string finR, probeR;
                    if (accLue)
                        std::getline(f, probeR);        /* aligne : la ligne accroche est deja finie */
                    else {
                        std::getline(f, finR);          /* milieu de ligne : fin puis peek */
                        std::getline(f, probeR);
                    }
                    if (!f.eof() && trimStr(probeR).rfind("roll ", 0) == 0) {
                        std::istringstream ir(probeR);
                        std::string tagR;
                        if (!(ir >> tagR >> o.traceRoll)) {
                            err = "fichier corrompu (roll)"; return false;
                        }
                    } else {
                        f.clear();
                        f.seekg(avantRoll);
                        o.traceRoll = 0.0;
                    }
                }
            } else {
                f.clear();                          /* lever eof/fail avant le retour arriere */
                f.seekg(avant);                     /* le jeton "objet" suivant reste a lire */
            }
        }
        /* B3.2b (retour test) : un objet portant ses parametres de
           construction (principe B2.7e) regenere SA geometrie par les MEMES
           fonctions que le trace - le fil ferme retrouve son arete de
           cloture, le balayage ses anneaux. Un objet sans trace garde le
           calcul derive simple. */
        if (o.traceType != 0 && o.tracePts.size() >= 2) traceRegen(o);
        else computeObjectDerived(o);
        objs.push_back(std::move(o));
    }
    g_objs = std::move(objs);
    selectOnly(g_objs.empty() ? -1 : 0);
    userMsg("projet ouvert : %s (%zu objets)", path.c_str(), g_objs.size());
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
    IDM_TOOL_CERCLE,
    IDM_TOOL_ARC,
    IDM_TOOL_POLY,
    IDM_TOOL_SPL,
    IDM_TOOL_SPLC,
    IDM_TOOL_EDIT,             /* B2.7e : edition des traces apres coup (E) */
    IDM_TOOL_SWEEP,            /* B3 : balayage le long d'un chemin (Maj+W) */
    IDM_VIEW_JOURNAL,          /* B3.3f : journal des messages utilisateur (J) */
    IDM_ANNULER,               /* B3.6z : annuler (Ctrl+Z) */
    IDM_RETABLIR,              /* B3.6z : retablir (Ctrl+Y / Ctrl+Maj+Z) */
    IDM_COLLER_SUR,            /* B3.7 : collage generique 2 objets (menu contextuel) */
    IDM_PROJET_RECENT,         /* B3.2d : base des fichiers recents (+0..7, contigus) ;
                                  B3.6zb : DEPLACE en fin d'enum - la base etait au
                                  milieu (9) et empietait sur INFO/DUPLICATE/DELETE
                                  (10/11/12) : choisir Dupliquer dans le menu
                                  contextuel chargeait historique[2] (vieux projet,
                                  retour de test), Supprimer/Infos pareil, et les
                                  slots 4..8 du sous-menu declenchaient
                                  Select/PushPull/Tourner au lieu d'ouvrir */
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
    /* B3.2d : fichiers recents (contigus a partir d'IDM_PROJET_RECENT) -
       teste AVANT la delegation : la base est APRES les commandes dans
       l'enum depuis B3.6zb, sans ce test en tete les slots seraient
       avales par runToolCommand (lecon du retour de test : Dupliquer
       chargeait un vieux projet) */
    if (id >= IDM_PROJET_RECENT && id < IDM_PROJET_RECENT + (int)kProjetRecents) {
        const size_t i = (size_t)(id - IDM_PROJET_RECENT);
        if (i < g_setup.historique.size()) {
            const std::string p = g_setup.historique[i];
            std::string err;
            if (!loadProject(p, err)) { userMsg("ouverture impossible : %s", err.c_str()); return; }
            g_editMode = false; g_editObj = -1;
            noteProjectPath(p);
            frameSelected();
        }
        return;
    }
    /* commandes (outils/affichage/preferences) : deleguees au dispatcher */
    if (id >= kIdCommandStart) { runToolCommand(id); return; }
    switch (id) {
    case IDM_NEW_PROJECT:
        g_objs.clear();
        undoVide();                           /* B3.6z : les piles ne survivent pas au projet */
        g_editMode = false; g_editObj = -1;   /* B2.7e : defense */
        g_setup.dernierProjet.clear(); saveSetup();   /* B3.2c : ne pas pre-remplir avec un AUTRE projet */
        clearSelection();
        userMsg("nouveau projet : scene vide");
        return;
    case IDM_OPEN_PROJECT: {
        const std::string p = fileDialog(false,
            "Projets (*.s3projet)\0*.s3projet\0Tous (*.*)\0*.*\0\0",
            nullptr, "Ouvrir un projet");
        if (p.empty()) return;
        std::string err;
        if (!loadProject(p, err)) { userMsg("ouverture impossible : %s", err.c_str()); return; }
        undoVide();                           /* B3.6z : annuler ne doit pas traverser les projets */
        g_editMode = false; g_editObj = -1;   /* B2.7e : defense */
        noteProjectPath(p);
        frameSelected();
        return;
    }
    case IDM_SAVE_PROJECT: {
        const std::string p = fileDialog(true,
            "Projets (*.s3projet)\0*.s3projet\0Tous (*.*)\0*.*\0\0",
            "s3projet", "Enregistrer le projet", g_setup.dernierProjet.c_str());
        if (p.empty()) return;
        std::string err;
        if (!saveProject(p, err)) userMsg("enregistrement impossible : %s", err.c_str());
        else noteProjectPath(p);
        return;
    }
    case IDM_CLOSE_PROJECT:
        g_objs.clear();
        undoVide();                           /* B3.6z */
        g_editMode = false; g_editObj = -1;   /* B2.7e : defense */
        g_setup.dernierProjet.clear(); saveSetup();   /* B3.2c : ne pas pre-remplir avec un AUTRE projet */
        clearSelection();
        userMsg("projet ferme");
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
            userMsg("%s", objectInfo(g_objs[(size_t)g_selected]).c_str());
        return;
    default:
        return;
    }
}

static void showContextMenu(GLFWwindow* w) {
    /* le menu contextuel ne concerne qu'un objet deja selectionne (clic gauche) */
    if (g_selected < 0 || g_selected >= (int)g_objs.size()) {
        userMsg("menu contextuel : selectionnez d'abord un objet (clic gauche)");
        return;
    }
    (void)w;
    const std::string info = objectInfo(g_objs[(size_t)g_selected]);
    userMsg("%s", info.c_str());
#ifdef _WIN32
    HMENU m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, IDM_INFO_OBJECT, "&Infos objet");
    AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(m, MF_STRING, IDM_DELETE_OBJECT, "Suppri&mer\tSuppr");
    AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(m, MF_STRING, IDM_DUPLICATE, "&Dupliquer...");
    AppendMenuA(m, MF_STRING, IDM_COLLER_SUR, "&Coller sur...");   /* B3.7 */
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
    if (msg == WM_COMMAND) { handleMenuAction((int)LOWORD(wp)); SetFocus(h); return 0; }   /* B2.4c : focus clavier rendu a la vue */
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
static HWND g_toolBtn[16];   /* B2.4b : dimension = nombre de boutons (l'ancien [9] debordait = corruption memoire) ; B3 : 16 */
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

/* B1.8 : style partage (THICKFRAME = poignees de redimensionnement) */
static const DWORD kToolStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;

static void toolLayout(HWND tw) {
    const int n = (int)(sizeof(g_toolBtn) / sizeof(g_toolBtn[0]));   /* synchro automatique avec la creation */
    const int bw = 86, bh = 26, pad = 6;
    const int cols = g_setup.toolVertical ? 1 : n;   /* B1.8 : UNE rangee par defaut (l'etalement 4 colonnes fixes erait corrige) */
    const int rows = (n + cols - 1) / cols;
    for (int i = 0; i < n; ++i)
        SetWindowPos(g_toolBtn[i], nullptr, pad + (i % cols) * (bw + pad),
                     pad + (i / cols) * (bh + pad), bw, bh, SWP_NOZORDER);
    const int cw = pad + cols * (bw + pad);
    const int ch = pad + rows * (bh + pad);
    /* B2.4c : SetWindowPos dimensionne la fenetre EXTERIEURE ; sans
       AdjustWindowRect, la barre de titre rognait la derniere rangee
       (Rectangle, Cercle, Arc invisibles depuis B2.3 - retour de test) */
    RECT r = {0, 0, cw, ch};
    AdjustWindowRect(&r, kToolStyle, FALSE);
    SetWindowPos(tw, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

static LRESULT CALLBACK toolWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED) {
        runToolCommand((int)LOWORD(wp));
        SetFocus(g_mainHwnd);   /* B2.4c : un clic bouton vole le focus clavier a la vue 3D */
        return 0;
    }
    if (msg == WM_SIZE && wp != SIZE_MINIMIZED) {   /* B1.8b : le parametre s'appelle wp (wParam = C2065, section _WIN32 jamais compilee en sandbox) */
        /* B1.8 : barre redimensionnable - horizontal : le nombre de colonnes
           suit la largeur client (1 rangee par defaut, l'utilisateur retrecit
           a sa main) ; vertical : le nombre de rangees suit la hauteur.
           La dimension se reajuste sur l'autre axe (hauteur / largeur). */
        static bool enCours = false;
        const int n = (int)(sizeof(g_toolBtn) / sizeof(g_toolBtn[0]));
        const int bw = 86, bh = 26, pad = 6;
        if (!enCours && n > 0 && g_toolBtn[0] != nullptr) {
            RECT c;
            GetClientRect(h, &c);
            int cols, rows;
            if (g_setup.toolVertical) {
                rows = (c.bottom - c.top - pad) / (bh + pad);
                if (rows < 1) rows = 1;
                if (rows > n) rows = n;
                cols = (n + rows - 1) / rows;
            } else {
                cols = (c.right - c.left - pad) / (bw + pad);
                if (cols < 1) cols = 1;
                if (cols > n) cols = n;
                rows = (n + cols - 1) / cols;
            }
            for (int i = 0; i < n; ++i) {
                const int col = g_setup.toolVertical ? i / rows : i % cols;
                const int row = g_setup.toolVertical ? i % rows : i / cols;
                SetWindowPos(g_toolBtn[i], nullptr, pad + col * (bw + pad),
                             pad + row * (bh + pad), bw, bh, SWP_NOZORDER);
            }
            const int cw = g_setup.toolVertical ? pad + cols * (bw + pad) : c.right - c.left;
            const int ch = g_setup.toolVertical ? c.bottom - c.top : pad + rows * (bh + pad);
            RECT w = {0, 0, cw, ch};
            AdjustWindowRect(&w, kToolStyle, FALSE);
            enCours = true;   /* anti-recursion : notre propre SetWindowPos */
            SetWindowPos(h, nullptr, 0, 0, w.right - w.left, w.bottom - w.top, SWP_NOMOVE | SWP_NOZORDER);
            enCours = false;
        }
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
        kToolStyle, o.x + g_setup.toolX, o.y + g_setup.toolY, 0, 0,
        mainHwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    static const char* labels[] = { "Selection", "P", "R", "F cadrer", "H origine", "Copier", "Coller", "Ligne", "Rectangle", "Cercle", "Arc", "Polygone", "Spline", "Spline ctrl", "Editer", "Balayer" };
    static const int ids[] = { IDM_TOOL_SELECT, IDM_TOOL_PP, IDM_TOOL_ROT, IDM_TOOL_FRAME,
                               IDM_TOOL_HOME, IDM_TOOL_COPY, IDM_TOOL_PASTE, IDM_TOOL_LINE, IDM_TOOL_RECT, IDM_TOOL_CERCLE, IDM_TOOL_ARC, IDM_TOOL_POLY, IDM_TOOL_SPL, IDM_TOOL_SPLC, IDM_TOOL_EDIT, IDM_TOOL_SWEEP };
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
    userMsg("barre d'outils : %s", vis ? "visible" : "cachee (Affichage pour la rappeler)");
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
    userMsg("barre d'outils : %s", vert ? "verticale" : "horizontale");
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
    userMsg("preferences : export %s en %s", scene ? "scene" : "selection", stl ? "STL" : "OBJ");
}
#endif

#ifdef _WIN32
/* B3.2d : sous-menu Projets recents - 8 slots fixes, les vides grises ;
   rempli/rafraichi par recentsMenuRefresh (a la creation et a chaque
   ouverture/enregistrement via noteProjectPath) */
static HMENU g_recentsMenu = nullptr;
static void recentsMenuRefresh() {
    if (!g_recentsMenu) return;
    for (size_t i = 0; i < kProjetRecents; ++i) {
        const UINT id = (UINT)(IDM_PROJET_RECENT + (int)i);
        if (i < g_setup.historique.size()) {
            std::string echappe;
            for (char c : g_setup.historique[i]) { if (c == '&') echappe += "&&"; else echappe += c; }
            char txt[MAX_PATH + 16];
            std::snprintf(txt, sizeof(txt), "&%zu  %s", i + 1, echappe.c_str());
            ModifyMenuA(g_recentsMenu, id, MF_BYCOMMAND | MF_STRING, id, txt);
        } else {
            ModifyMenuA(g_recentsMenu, id, MF_BYCOMMAND | MF_STRING | MF_GRAYED, id, "(vide)");
        }
    }
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
    g_recentsMenu = CreatePopupMenu();
    for (size_t i = 0; i < kProjetRecents; ++i)
        AppendMenuA(g_recentsMenu, MF_STRING | MF_GRAYED, IDM_PROJET_RECENT + (int)i, "(vide)");
    AppendMenuA(file, MF_POPUP, (UINT_PTR)g_recentsMenu, "Projets &recents");
    recentsMenuRefresh();
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
    AppendMenuA(aff, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(aff, MF_STRING, IDM_VIEW_JOURNAL, "&Journal des messages\tJ");   /* B3.3f */
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
    AppendMenuA(outils, MF_STRING, IDM_ANNULER, "&Annuler\tCtrl+Z");   /* B3.6z : en tete */
    AppendMenuA(outils, MF_STRING, IDM_RETABLIR, "&Retablir\tCtrl+Y");
    AppendMenuA(outils, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(outils, MF_STRING, IDM_TOOL_SELECT, "&Selection\tEchap");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_EDIT, "&Editer un trace\tE");   /* B2.7e */
    AppendMenuA(outils, MF_SEPARATOR, 0, nullptr);   /* B2.7c : groupes de variantes separes */
    AppendMenuA(outils, MF_STRING, IDM_TOOL_PP, "&Extraire une face\tP");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_ROT, "&Tourner\tR");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_SWEEP, "Bal&ayer le long d'un chemin\tMaj+W");   /* B3 */
    AppendMenuA(outils, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(outils, MF_STRING, IDM_TOOL_LINE, "&Ligne (fermee en boucle)\tL");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_LIGNE_FIL, "Ligne (&fil ouvert)\tMaj+L");
    AppendMenuA(outils, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(outils, MF_STRING, IDM_TOOL_RECT, "&Rectangle\tMaj+R");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_CERCLE, "&Cercle\tMaj+C");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_ARC, "&Arc\tMaj+A");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_POLY, "&Polygone\tMaj+P");
    AppendMenuA(outils, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(outils, MF_STRING, IDM_TOOL_SPL, "&Spline (passage)\tMaj+S");
    AppendMenuA(outils, MF_STRING, IDM_TOOL_SPLC, "Spline (points de &controle)\tMaj+B");
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
    std::printf("menus natifs : barre Fichier indisponible hors Windows");
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
    /* garde = 1 tri complet : une feuille (2 tris, B2.2 rectangle) est une
       face valide pour le Push/Pull (bug retour de test : < 9 refusait toute
       feuille) ; tout le reste (BFS, butee, recolation) gere deja triCount */
    if (o.tri.size() < 3 || clickedTri >= triCount || clickedTri >= o.n.size()) return false;
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
            /* B3.6zb : REWIND au lieu de rejeter - makeExtrude oriente la
               normale du cote de la camera ; une feuille a winding oppose
               (polygone trace clockwise, rectangle aux coins inverses) etait
               VIDEE au premier tirage (o.tri = 0, feuille disparue). Au butee,
               les doublons a deux windings restent dedoublonnes par seenT. */
            if (dot(cr, n) <= 0.0) std::swap(ids[1], ids[2]);
            std::array<unsigned, 3> key = ids;
            std::sort(key.begin(), key.end());
            if (!seenT.insert(key).second) continue;
            const Vec3 q0 = rep[ids[0]], q1 = rep[ids[1]], q2 = rep[ids[2]];
            sgn += dot(cross(q1 - q0, q2 - q0), dl);   /* winding REWINDUE */
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
    undoAbandonne();                    /* B3.6z : ppCancel restaure deja la base */
    if (!g_ppActive || g_pp.obj < 0 || g_pp.obj >= (int)g_objs.size()) { g_ppActive = false; return; }
    Object& o = g_objs[(size_t)g_pp.obj];
    o.v = g_pp.base;
    o.tri = g_pp.tri;
    computeObjectDerived(o);
    g_ppActive = false;
    g_pp = Extrude();
    userMsg("extrusion annulee");
}

/* B2.7e E3 : un pull pose sur une feuille issue d un profil (TraceData, patch
   = tout l objet) CONVERTIT l objet en EXTRUSION parametrique (type 8) : le
   profil (sous-type + tracePts + ferme/surface) et le vecteur survivent, la
   geometrie est regeneree par les MEMES fonctions que le trace. Les coques
   (cubes, imports, extrusions en place) restent non parametriques pour
   l instant (arbitrage utilisateur). */
static void traceRegen(Object& o);            /* defini plus bas (bloc edition) */
static bool extrudeConvertTo(Object& o, const Extrude& dd, double d) {
    if (o.traceType == 0 || o.traceType == 8) return false;
    if (o.tri.empty()) return false;                             /* fil : pas une feuille */
    if (dd.patchTris.size() * 3 != dd.tri.size()) return false;  /* patch != tout l objet */
    if (std::fabs(d) < 1e-12) return false;
    o.traceProfil = o.traceType;
    o.traceType = 8;
    o.traceVec = dd.normal * (d / o.scl);
    traceRegen(o);
    return true;
}

/* B3 : BALAYAGE (extrusion le long d'un chemin) - une feuille a TraceData
   balayee le long d'un fil (ligne, spline, arc). Chemin CONSUMME (comme le
   profil en E3) : ses points sont absorbes dans les parametres (l'info
   reste dans l'objet - extraire le chemin = backlog B5+). */
static int g_sweepStage = 0;            /* 0 = inactif ; 1 = attend la FACE ; 2 = attend le FIL */
static int g_sweepProfil = -1;          /* index de l'objet face (clic 1) */
static void sweepCancel() {
    g_sweepStage = 0;
    g_sweepProfil = -1;
    userMsg("mode selection");
}
/* B3.3d (retour test) : une FACE de balayage = feuille tracee avec surface.
   Avant, le clic 1 acceptait N'IMPORTE QUEL solide (ex. le tube d'un balayage
   deja pose) puis l'etape 2 devenait impossible a satisfaire (le fil etait
   deja consomme) : l'utilisateur restait piege en attente du fil. */
static bool sweepFaceValide(const Object& o) {
    return o.traceType != 0 && o.traceType != 8 && o.traceType != 9
        && !o.tri.empty();
}
static bool sweepConvertTo(Object& profil, Object& chemin) {
    if (&profil == &chemin) {
        flashBand("Balayage : le chemin doit etre un AUTRE objet (un fil)");
        userMsg("balayage : le chemin doit etre un AUTRE objet (un fil)");
        return false;
    }
    if (!sweepFaceValide(profil)) {
        flashBand("Balayage : la face visee n'est pas une feuille tracee");
        userMsg("balayage : la face visee n'est pas une feuille tracee (ligne, rect, cercle, arc, polygone, spline)");
        return false;
    }
    if (chemin.traceType != 1 && chemin.traceType != 2 && chemin.traceType != 3
        && chemin.traceType != 6) {
        flashBand("Balayage : le chemin doit etre un fil (ligne, spline, arc)");
        userMsg("balayage : le chemin doit etre un fil (ligne, spline ou arc)");
        return false;
    }
    if (chemin.tracePts.size() < (chemin.traceType == 6 ? (size_t)3 : (size_t)2)) {
        flashBand("Balayage : le fil chemin est vide ou trop court");
        userMsg("balayage : le fil chemin est vide ou trop court");
        return false;
    }
    /* le chemin est absorbe : locales du fil -> locales du profil (pos + scl*v = monde) */
    undoSnapshot("balayage");               /* B3.6z : avant la mutation reelle */
    std::vector<Vec3> chem;
    chem.reserve(chemin.tracePts.size());
    for (const Vec3& p : chemin.tracePts)
        chem.push_back((chemin.pos + p * chemin.scl - profil.pos) * (1.0 / profil.scl));
    profil.traceProfil = profil.traceType;
    profil.traceCheminType = chemin.traceType;
    profil.traceCheminFerme = (chemin.traceType == 1) ? chemin.traceFerme : 0;
    profil.traceChemin = std::move(chem);
    profil.traceVec = profil.n.empty() ? Vec3(0, 1, 0) : profil.n[0];   /* roll : normale de la face */
    profil.traceType = 9;
    traceRegen(profil);
    return true;
}
/* clic gauche pendant le mode Balayage : clic 1 = la face, clic 2 = le fil */
static void sweepClick(GLFWwindow* w) {
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return;
    if (g_sweepStage == 1) {
        int hit = -1;
        double bestT = 1e300;
        for (size_t i = 0; i < g_objs.size(); ++i) {
            double t; size_t ti;
            if (rayObjectTri(org, dir, g_objs[i], t, &ti) && t < bestT) { bestT = t; hit = (int)i; }
        }
        if (hit < 0) {
            userMsg("balayage : cliquez une face (l'objet vise doit etre une feuille tracee)");
            return;
        }
        /* B3.3d : refus IMMEDIAT si l'objet vise n'est pas une feuille tracee
           (ex. le tube d'un balayage) - on ne passe PAS a l'etape 2 */
        if (!sweepFaceValide(g_objs[(size_t)hit])) {
            flashBand("Balayage : la face visee n'est pas une feuille tracee");
            userMsg("balayage : \"%s\" n'est pas une feuille tracee - cliquez une feuille (ligne, rect, cercle, arc, polygone, spline) ; Echap = annuler",
                        g_objs[(size_t)hit].name.c_str());
            return;
        }
        g_sweepProfil = hit;
        g_sweepStage = 2;
        userMsg("balayage : %s capte - cliquez le FIL chemin (ligne, spline, arc) ; Echap = annuler",
                    g_objs[(size_t)hit].name.c_str());
        return;
    }
    /* stage 2 : le fil chemin */
    double bestT = 1e300;
    int hit = -1;
    for (size_t i = 0; i < g_objs.size(); ++i) {
        if (i == (size_t)g_sweepProfil) continue;
        const double t = filRayHit(org, dir, g_objs[i], filPickTol());
        if (t >= 0.0 && t < bestT) { bestT = t; hit = (int)i; }
    }
    if (hit < 0) {
        flashBand("Balayage : aucun fil vise - cliquez un fil (ligne, spline, arc)");
        userMsg("balayage : aucun fil vise - cliquez un fil (ligne, spline, arc) ; Echap = annuler");
        return;
    }
    const int ip = g_sweepProfil;
    Object& profil = g_objs[(size_t)ip];
    if (!sweepConvertTo(profil, g_objs[(size_t)hit])) return;
    const std::string nomFil = g_objs[(size_t)hit].name;
    g_objs.erase(g_objs.begin() + hit);            /* chemin consomme */
    selectOnly(ip < hit ? ip : ip - 1);
    g_sweepStage = 0;
    g_sweepProfil = -1;
    userMsg("balayage pose : %s (le fil \"%s\" est consomme - ses points restent dans les parametres)",
                profil.name.c_str(), nomFil.c_str());
    userMsg("mode selection");
}

static void ppCommit() {
    undoValide();                       /* B3.6z : le geste (s'il a mute) devient un pas */
    if (g_ppActive && g_pp.obj >= 0 && g_pp.obj < (int)g_objs.size())
        extrudeConvertTo(g_objs[(size_t)g_pp.obj], g_pp, g_pp.d);
    userMsg("face posee — extrusion : %.3f", g_pp.d);
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
    if (std::fabs(d) > 1e-12) g_undoSale = true;    /* B3.6z */
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
    if (std::fabs(angDeg) > 1e-12) g_undoSale = true;   /* B3.6z */
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
        userMsg("rotation : selectionnez d'abord un objet (clic gauche)");
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
    undoOuvre("rotation");                  /* B3.6z : capture AVANT toute mutation */
    g_rotActive = true;
    if (!g_rotMulti.empty()) {
        userMsg("rotation multi %s de %zu objets autour du %s : souris pour tourner, B = pivot barycentre/centre, chiffres + Entree (tapez dans la fenetre ; Entree pose et quitte)",
                    axisName(axis), g_rotMulti.size(), rotPivotName());
    } else {
        userMsg("rotation %s de %s autour de son centre : souris pour tourner, ou chiffres + Entree (tapez dans la fenetre ; Entree pose et quitte)",
                    axisName(axis), o.name.c_str());
    }
}

static void rotCancel() {
    undoAbandonne();                    /* B3.6z : rotCancel restaure deja la base */
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
    userMsg("rotation annulee");
}

static void rotCommit() {
    undoValide();                       /* B3.6z : le geste (s'il a mute) devient un pas */
    if (!g_rotActive) { return; }
    if (!g_rotMulti.empty()) {
        userMsg("rotation posee : %+.2f deg sur %zu objets", g_rotAngle, g_rotMulti.size());
        g_rotMulti.clear();
    } else {
        if (g_rotObj < 0 || g_rotObj >= (int)g_objs.size()) { g_rotActive = false; return; }
        userMsg("rotation posee : %+.2f deg sur %s", g_rotAngle, g_objs[(size_t)g_rotObj].name.c_str());
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
static bool g_lineFerme = true;         /* B2.5b : pose par la COMMANDE d'armement (L = boucle fermee, Maj+L = fil ouvert) ; rien de memorise ; B3.1b : jamais de surface (le polygone ferme en surface) */
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
    userMsg("ligne : %s", pourquoi);
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
/* B3.6a-bis (retour test : "l'altitude n'est pas maitrisee correctement") :
   intersection du rayon avec le plan HORIZONTAL passant par yPlan - le drag
   d'une poignee en hauteur conserve son altitude (avant : tout retombait au
   sol) ; au sol (yPlan = 0) le comportement est identique a lineGroundPoint */
static void linePointY(GLFWwindow* w, double yPlan, bool& ok, Vec3& p) {
    ok = false;
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return;
    if (std::fabs(dir.y) < 1e-12) return;
    const double t = (yPlan - org.y) / dir.y;
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
    userMsg("ligne : %zu point(s) (clic = prolonger ; clic droit = fin ; Echap = tout annuler)", g_linePts.size());
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
    /* B2.7e : les points de definition survivent a la pose (edition apres coup) */
    o.traceType = 1;
    o.tracePts = o.v;
    /* B2.1 : les aretes (chaine du fil) sont DERIVEES par computeObjectDerived
       (objet sans triangle = chaine 0-1, ...), donc survives au save/load */
    computeObjectDerived(o);
    const size_t nseg = o.edges.size();     /* capture AVANT le move (o move-assigne) */
    undoSnapshot("ligne");
    g_objs.push_back(std::move(o));
    const int idx = (int)g_objs.size() - 1;
    selectOnly(idx);
    userMsg("ligne posee : %s, %zu segment(s)", g_objs.back().name.c_str(), nseg);
    lineReset();
    g_lineMode = false;
    userMsg("mode selection");
}
/* B2.5 : fermeture de la boucle - clic sur le depart (>= 3 sommets).
   B3.1b (retour test) : la ligne ferme en BOUCLE (fil), JAMAIS en surface -
   fermer en surface est le role du POLYGONE (difference ligne/polygone).
   L = boucle fermee, Maj+L = fil ouvert ; les vieux projets avec une feuille
   de ligne fermee restent LUS (la regen type 1 garde la branche surface). */
static void lineCloseLoop() {
    Object o;
    o.name = lineNewName();
    o.pos = g_linePts[0];
    o.scl = 1.0;
    o.v.resize(g_linePts.size());        /* LOCAL : pos + v = monde (bug relocalisation corrige) */
    for (size_t i = 0; i < o.v.size(); ++i) o.v[i] = g_linePts[i] - o.pos;
    o.tri.clear();
    o.n.clear();
    /* B2.7e : les points de definition survivent a la pose (edition apres coup) */
    o.traceType = 1;
    o.traceFerme = 1;
    o.traceSurface = 0;
    o.tracePts = o.v;
    computeObjectDerived(o);
    o.edges.emplace_back((int)o.v.size() - 1, 0);   /* boucle fermee */
    const size_t nseg = o.edges.size();     /* capture AVANT le move */
    undoSnapshot("ligne fermee");
    g_objs.push_back(std::move(o));
    selectOnly((int)g_objs.size() - 1);
    userMsg("ligne fermee : %zu cotes, fil en boucle (pas de surface - le polygone ferme en surface)", nseg);
    lineReset();
    g_lineMode = false;
    userMsg("mode selection");
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
        userMsg("longueur tapee : pointez d'abord une direction (curseur sur le sol)");
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
static std::string g_rectNumBuf;        /* dimensions tapees "L;H" */
static Vec3 g_rectCursor(0, 0, 0);      /* point sol sous le curseur (apercu) */
static bool g_rectCursorOk = false;

static void rectReset() {
    g_rectActive = false;
    g_rectNumBuf.clear();
}
static void rectCancel(const char* pourquoi) {
    rectReset();
    g_rectMode = false;
    userMsg("rectangle : %s", pourquoi);
}
/* suivi du curseur chaque tour (apercu + orientation de la saisie L;H) */
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
    /* B2.7e : les 2 coins de definition survivent a la pose (edition) */
    o.traceType = 4;
    o.tracePts = { a - o.pos, b - o.pos };
    computeObjectDerived(o);
    undoSnapshot("rectangle");
    g_objs.push_back(std::move(o));
    selectOnly((int)g_objs.size() - 1);
    userMsg("rectangle pose : %s, %.3g x %.3g", g_objs.back().name.c_str(), x1 - x0, z1 - z0);
    rectReset();
    g_rectMode = false;
    userMsg("mode selection");
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
        userMsg("rectangle : premier coin (%.3f, %.3f) - clic = coin oppose ; chiffres L;H + Entree = dimensions exactes", p.x, p.z);
        return;
    }
    if (std::fabs(p.x - g_rectCorner.x) < 1e-9 || std::fabs(p.z - g_rectCorner.z) < 1e-9) {
        userMsg("rectangle : coin oppose aligne avec le premier - rien a poser");
        return;
    }
    rectPose(g_rectCorner, p);
}
/* chiffres + Entree : "L;H" ; le quadrant du curseur par rapport au
   premier coin oriente le rectangle (fidele SketchUp : la diagonale
   dirige), puis pose + sortie du mode (meme contrat que la ligne) */
static void rectPoseByDims(const std::string& txt) {
    if (!g_rectCursorOk) {
        userMsg("dimensions tapees : pointez d'abord la direction (curseur sur le sol)");
        return;
    }
    std::string t = txt;
    /* ';' = separateur de champs ; ',' = decimale FR, convertie en '.'
       (convention B2.2b : jamais de virgule comme separateur) */
    for (char& c : t) { if (c == ';') c = ' '; else if (c == ',') c = '.'; }
    double L = 0.0, H = 0.0;
    std::istringstream is(t);
    if (!(is >> L >> H)
        || !std::isfinite(L) || !std::isfinite(H) || L <= 0.0 || H <= 0.0) {
        userMsg("Dimensions invalides - tapez L;H (deux nombres positifs) puis Entree.");
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
/* B2.3 : outil Cercle - clic centre / clic rayon, pose au sol.         */
/* Feuille fan de 24 tris (centre + boucle de 24 sommets) : UN objet,   */
/* etirable au push/pull. Maj+C arme ; clic droit/Echap annule.         */
/* ------------------------------------------------------------------ */
static const int CERCLE_SEG = 24;
static bool g_cercleMode = false;       /* outil arme */
static bool g_cercleActive = false;     /* centre pose */
static Vec3 g_cercleCentre(0, 0, 0);    /* centre (monde, y = 0) */
static std::string g_cercleNumBuf;      /* rayon tape */
static Vec3 g_cercleCursor(0, 0, 0);    /* point sol sous le curseur (apercu) */
static bool g_cercleCursorOk = false;

static void cercleReset() {
    g_cercleActive = false;
    g_cercleNumBuf.clear();
}
static void cercleCancel(const char* pourquoi) {
    cercleReset();
    g_cercleMode = false;
    userMsg("cercle : %s", pourquoi);
}
/* suivi du curseur chaque tour (apercu du rayon) */
static void cercleTrackCursor(GLFWwindow* w) {
    Vec3 p;
    bool ok;
    lineGroundPoint(w, ok, p);
    g_cercleCursor = p;
    g_cercleCursorOk = ok;
}
/* construction : centre + rayon ; fan de 24 tris, normale +y ; v LOCALES
   (pos + v = monde, convention commune - cf. bug corrige en B2.5b).
   Boucle en sens horaire vu de dessus (cos, -sin) : (v1-v0) x (v2-v0) = +y. */
static void cerclePose(const Vec3& c, double r) {
    Object o;
    o.name = uniqueName("Cercle");
    o.pos = Vec3(c.x, 0.0, c.z);
    o.scl = 1.0;
    o.v.reserve(1 + (size_t)CERCLE_SEG);
    o.v.push_back(Vec3(0, 0, 0));
    for (int i = 0; i < CERCLE_SEG; ++i) {
        const double a = 6.283185307179586 * double(i) / double(CERCLE_SEG);
        o.v.push_back(Vec3(r * std::cos(a), 0.0, -r * std::sin(a)));
    }
    o.tri.clear();
    o.tri.reserve(3 * (size_t)CERCLE_SEG);
    for (int i = 0; i < CERCLE_SEG; ++i) {
        o.tri.push_back(0);
        o.tri.push_back(1 + i);
        o.tri.push_back(1 + (i + 1) % CERCLE_SEG);
    }
    o.n.clear();
    /* B2.7e : centre + point du rayon (sur le cercle, angle 0) survivent a la pose */
    o.traceType = 5;
    o.tracePts = { Vec3(0, 0, 0), Vec3(r, 0, 0) };
    computeObjectDerived(o);
    undoSnapshot("cercle");
    g_objs.push_back(std::move(o));
    selectOnly((int)g_objs.size() - 1);
    userMsg("cercle pose : %s, rayon %.3g", g_objs.back().name.c_str(), r);
    cercleReset();
    g_cercleMode = false;
    userMsg("mode selection");
}
static void cercleTakePoint(GLFWwindow* w) {
    bool ok;
    Vec3 p;
    lineGroundPoint(w, ok, p);
    if (!ok) return;                                /* pointe le ciel : rien */
    if (!g_cercleActive) {
        g_cercleCentre = p;
        g_cercleActive = true;
        g_cercleNumBuf.clear();
        userMsg("cercle : centre (%.3f, %.3f) - clic = rayon pose ; chiffres + Entree = rayon exact", p.x, p.z);
        return;
    }
    const double r = (p - g_cercleCentre).norm();
    if (r < 1e-9) {
        userMsg("cercle : rayon nul - rien a poser");
        return;
    }
    cerclePose(g_cercleCentre, r);
}
/* rayon tape + Entree : un seul nombre (pas de direction a prendre : le
   cercle est invariant par rotation) ; decimales FR acceptees (',', ';' et
   '.' = decimale, convention B2.2b/B2.2d) */
static void cerclePoseByRayon(const std::string& txt) {
    std::string t = txt;
    for (char& c : t) { if (c == ';' || c == ',') c = '.'; }
    double r = 0.0;
    std::istringstream is(t);
    if (!(is >> r) || !std::isfinite(r) || r <= 0.0) {
        userMsg("Rayon invalide - tapez un nombre positif puis Entree.");
        return;
    }
    cerclePose(g_cercleCentre, r);
}
/* apercu : cercle jaune au rayon courant + segment centre-curseur */
static void drawCerclePreview() {
    if (!g_cercleMode || !g_cercleActive || !g_cercleCursorOk) return;
    const double r = (g_cercleCursor - g_cercleCentre).norm();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3d(0.92, 0.75, 0.1);
    glLineWidth(2.0);
    if (r > 1e-9) {
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < CERCLE_SEG; ++i) {
            const double a = 6.283185307179586 * double(i) / double(CERCLE_SEG);
            glVertex3d(g_cercleCentre.x + r * std::cos(a), 0.0, g_cercleCentre.z - r * std::sin(a));
        }
        glEnd();
    }
    glBegin(GL_LINES);
    glVertex3d(g_cercleCentre.x, 0.0, g_cercleCentre.z);
    glVertex3d(g_cercleCursor.x, 0.0, g_cercleCursor.z);
    glEnd();
    glLineWidth(1.0);
    glEnable(GL_DEPTH_TEST);
}

/* ------------------------------------------------------------------ */
/* B2.4 : outil Arc (« Arc 2 points » de SketchUp) - 3 clics : depart,  */
/* fin (corde), bombement. Les 3 points definissent LE cercle unique ;  */
/* l'arc pose est un FIL OUVERT (sans triangle, comme la ligne).        */
/* Maj+A arme ; clic droit/Echap annule.                                */
/* ------------------------------------------------------------------ */
static const int ARC_SEG = 12;
static bool g_arcMode = false;          /* outil arme */
static int g_arcState = 0;              /* 0 = attente depart, 1 = attente fin, 2 = attente bombement */
static Vec3 g_arcP1(0, 0, 0);           /* depart (monde, y = 0) */
static Vec3 g_arcP2(0, 0, 0);           /* fin de la corde (monde) */
static std::string g_arcNumBuf;         /* corde ou fleche tapee */
static Vec3 g_arcCursor(0, 0, 0);       /* point sol sous le curseur */
static bool g_arcCursorOk = false;

static void arcReset() {
    g_arcState = 0;
    g_arcNumBuf.clear();
}
static void arcCancel(const char* pourquoi) {
    arcReset();
    g_arcMode = false;
    userMsg("arc : %s", pourquoi);
}
/* suivi du curseur chaque tour (corde a venir + cote du bombement) */
static void arcTrackCursor(GLFWwindow* w) {
    Vec3 p;
    bool ok;
    lineGroundPoint(w, ok, p);
    g_arcCursor = p;
    g_arcCursorOk = ok;
}
/* cercle unique passant par 3 points (plan y = 0) ; faux si alignes */
static bool arcCircle3(const Vec3& a, const Vec3& b, const Vec3& c, Vec3& C, double& r) {
    const double d = 2.0 * (a.x * (b.z - c.z) + b.x * (c.z - a.z) + c.x * (a.z - b.z));
    if (std::fabs(d) < 1e-12) return false;
    const double sa = a.x * a.x + a.z * a.z, sb = b.x * b.x + b.z * b.z, sc = c.x * c.x + c.z * c.z;
    const double cx = (sa * (b.z - c.z) + sb * (c.z - a.z) + sc * (a.z - b.z)) / d;
    const double cz = (sa * (c.x - b.x) + sb * (a.x - c.x) + sc * (b.x - a.x)) / d;
    C = Vec3(cx, 0.0, cz);
    r = std::sqrt((a.x - cx) * (a.x - cx) + (a.z - cz) * (a.z - cz));
    return r > 1e-12;
}
static double wrapAngle(double a) {      /* dans (-pi, pi] */
    while (a > 3.14159265358979323846) a -= 6.28318530717958647692;
    while (a <= -3.14159265358979323846) a += 6.28318530717958647692;
    return a;
}
/* coeur commun (arcPose + edition B2.7e) : les ARC_SEG+1 points de l'arc,
   EXACTS sur le cercle, dans le MEME espace que les 3 points d'entree
   (invariant par translation) ; faux si alignes / arc nul ou complet */
static bool arcComputePts(const Vec3& p1, const Vec3& b, const Vec3& p2, std::vector<Vec3>& out) {
    Vec3 C;
    double r;
    if (!arcCircle3(p1, b, p2, C, r)) return false;
    const double t1 = std::atan2(p1.z - C.z, p1.x - C.x);
    const double t2 = std::atan2(p2.z - C.z, p2.x - C.x);
    const double tb = std::atan2(b.z - C.z, b.x - C.x);
    const double db = wrapAngle(tb - t1);
    double sweep = wrapAngle(t2 - t1);
    if (db >= 0.0) { if (sweep < 0.0) sweep += 6.28318530717958647692; }
    else           { if (sweep > 0.0) sweep -= 6.28318530717958647692; }
    if (std::fabs(sweep) < 1e-9 || std::fabs(sweep) > 6.28318530717958647692 - 1e-9) return false;
    out.clear();
    out.reserve((size_t)ARC_SEG + 1);
    for (int i = 0; i <= ARC_SEG; ++i) {
        const double a = t1 + sweep * double(i) / double(ARC_SEG);
        out.push_back(Vec3(C.x + r * std::cos(a), 0.0, C.z + r * std::sin(a)));
    }
    return true;
}
/* construction : les N+1 points sont recalcules EXACTS sur le cercle ;
   v LOCALES (pos = depart, pos + v = monde, convention commune) */
static void arcPose(const Vec3& p1, const Vec3& b, const Vec3& p2) {
    std::vector<Vec3> pts;
    if (!arcComputePts(p1, b, p2, pts)) {
        userMsg("arc : bombement invalide (aligne avec la corde, arc nul ou cercle complet) - deplacez le bombement");
        return;
    }
    Object o;
    o.name = uniqueName("Arc");
    o.pos = p1;
    o.scl = 1.0;
    o.v.reserve(pts.size());
    for (const Vec3& p : pts) o.v.push_back(p - p1);
    o.tri.clear();     /* fil ouvert : SANS triangle, chaine derivee (ligne B2.1) */
    o.n.clear();
    /* B2.7e : les 3 points de definition survivent a la pose (edition) */
    o.traceType = 6;
    o.tracePts = { Vec3(0, 0, 0), b - p1, p2 - p1 };
    computeObjectDerived(o);
    const size_t nseg = o.edges.size();
    undoSnapshot("arc");
    g_objs.push_back(std::move(o));
    selectOnly((int)g_objs.size() - 1);
    userMsg("arc pose : %s, %zu segment(s)", g_objs.back().name.c_str(), nseg);   /* B2.7e2 : le rayon vit dans arcComputePts */
    arcReset();
    g_arcMode = false;
    userMsg("mode selection");
}
static void arcTakePoint(GLFWwindow* w) {
    bool ok;
    Vec3 p;
    lineGroundPoint(w, ok, p);
    if (!ok) return;                                /* pointe le ciel : rien */
    if (g_arcState == 0) {
        g_arcP1 = p;
        g_arcState = 1;
        g_arcNumBuf.clear();
        userMsg("arc : depart (%.3f, %.3f) - clic = fin de la corde ; chiffres + Entree = longueur de corde", p.x, p.z);
        return;
    }
    if (g_arcState == 1) {
        if ((p - g_arcP1).norm() < 1e-9) {
            userMsg("arc : corde nulle - rien a poser");
            return;
        }
        g_arcP2 = p;
        g_arcState = 2;
        g_arcNumBuf.clear();
        userMsg("arc : corde posee - clic = bombement pose ; chiffres + Entree = fleche exacte");
        return;
    }
    arcPose(g_arcP1, p, g_arcP2);                   /* etat 2 : le clic = bombement pose */
}
/* corde tapee + Entree : pose la fin dans la direction du curseur */
static void arcPoseByCorde(double len) {
    Vec3 dir = g_arcCursor - g_arcP1;
    if (!g_arcCursorOk || dir.norm() < 1e-9) {
        userMsg("corde tapee : pointez d'abord la direction (curseur sur le sol)");
        return;
    }
    dir = dir * (1.0 / dir.norm());
    g_arcP2 = g_arcP1 + dir * len;
    g_arcState = 2;
    g_arcNumBuf.clear();
    userMsg("arc : corde posee - clic = bombement pose ; chiffres + Entree = fleche exacte");
}
/* fleche tapee + Entree : pose l'arc ; cote donne par le curseur ;
   B2.4d : toute fleche > 0 est valide - au-dela de la moitie de la corde
   l'arc est MAJEUR (> 180 deg) ; fleche 5-10x la corde = proche du cercle */
static void arcPoseByFleche(double f) {
    if (f <= 0.0) {
        userMsg("Fleche invalide - tapez un nombre strictement positif puis Entree.");
        return;
    }
    const Vec3 mid = (g_arcP1 + g_arcP2) * 0.5;
    Vec3 perp((g_arcP2 - g_arcP1).z, 0.0, -(g_arcP2 - g_arcP1).x);
    perp = perp * (1.0 / perp.norm());
    if (g_arcCursorOk && dot(g_arcCursor - mid, perp) < 0.0) perp = perp * -1.0;
    arcPose(g_arcP1, mid + perp * f, g_arcP2);
}
/* apercu : etat 1 = corde a venir vers le curseur ; etat 2 = corde +
   arc fléchissant vers le curseur (bombement candidat) */
static void drawArcPreview() {
    if (!g_arcMode || g_arcState == 0 || !g_arcCursorOk) return;
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3d(0.92, 0.75, 0.1);
    glLineWidth(2.0);
    if (g_arcState == 1) {
        glBegin(GL_LINES);
        glVertex3d(g_arcP1.x, 0.0, g_arcP1.z);
        glVertex3d(g_arcCursor.x, 0.0, g_arcCursor.z);
        glEnd();
    } else {
        glBegin(GL_LINES);
        glVertex3d(g_arcP1.x, 0.0, g_arcP1.z);
        glVertex3d(g_arcP2.x, 0.0, g_arcP2.z);
        glEnd();
        const Vec3 mid = (g_arcP1 + g_arcP2) * 0.5;
        Vec3 perp((g_arcP2 - g_arcP1).z, 0.0, -(g_arcP2 - g_arcP1).x);
        const double n = perp.norm();
        if (n > 1e-9) {
            perp = perp * (1.0 / n);
            const double f = dot(g_arcCursor - mid, perp);
            Vec3 C;
            double r;
            if (std::fabs(f) > 1e-9 && std::fabs(f) < 0.5 * n * 1e300
                && arcCircle3(g_arcP1, mid + perp * f, g_arcP2, C, r)) {
                const double t1 = std::atan2(g_arcP1.z - C.z, g_arcP1.x - C.x);
                const double t2 = std::atan2(g_arcP2.z - C.z, g_arcP2.x - C.x);
                const double tb = std::atan2((mid + perp * f).z - C.z, (mid + perp * f).x - C.x);
                const double db = wrapAngle(tb - t1);
                double sweep = wrapAngle(t2 - t1);
                if (db >= 0.0) { if (sweep < 0.0) sweep += 6.28318530717958647692; }
                else           { if (sweep > 0.0) sweep -= 6.28318530717958647692; }
                glBegin(GL_LINE_STRIP);
                for (int i = 0; i <= ARC_SEG; ++i) {
                    const double a = t1 + sweep * double(i) / double(ARC_SEG);
                    glVertex3d(C.x + r * std::cos(a), 0.0, C.z + r * std::sin(a));
                }
                glEnd();
            }
        }
    }
    glLineWidth(1.0);
    glEnable(GL_DEPTH_TEST);
}
/* ------------------------------------------------------------------ */
/* B2.6 : outil Polygone quelconque (Maj+P) - N clics = sommets au sol
   (y = 0), fermeture = clic sur le depart (>= 3 sommets, tolerance
   filPickTol) ; SURFACE SEULE (choix utilisateur) : feuille fan (0,i,i+1)
   en UN objet, etirable au push/pull ; pas de saisie numerique.
   v1 : eventail correct pour polygones convexes (concaves = reprise avec
   le vrai solide B6) ; aretes derivees = bords + diagonales du fan. */
static bool g_polyMode = false;         /* outil arme */
static bool g_polyActive = false;       /* au moins un sommet clique */
static std::vector<Vec3> g_polyPts;     /* sommets cliques, monde (y = 0) */
static Vec3 g_polyCursor(0, 0, 0);      /* point sol sous le curseur (apercu) */
static bool g_polyCursorOk = false;

static void polyReset() {
    g_polyActive = false;
    g_polyPts.clear();
}
static void polyCancel(const char* pourquoi) {
    polyReset();
    g_polyMode = false;
    userMsg("polygone : %s", pourquoi);
}
/* suivi du curseur chaque tour (apercu du contour + rubber band) */
static void polyTrackCursor(GLFWwindow* w) {
    Vec3 p;
    bool ok;
    lineGroundPoint(w, ok, p);
    g_polyCursor = p;
    g_polyCursorOk = ok;
}
/* pose : feuille fan en UN objet ; ancre locale = premier sommet
   (convention o.pos + v = monde, commune a tous les profils B2) */
static void polyPose() {
    Object o;
    o.name = uniqueName("Polygone");
    o.pos = g_polyPts[0];
    o.scl = 1.0;
    o.v.resize(g_polyPts.size());
    for (size_t i = 0; i < o.v.size(); ++i) o.v[i] = g_polyPts[i] - o.pos;
    o.n.clear();
    /* B2.7e : les sommets de definition survivent a la pose (edition) */
    o.traceType = 7;
    o.traceFerme = 1;                    /* surface seule (choix utilisateur B2.6) */
    o.tracePts = o.v;
    for (size_t i = 1; i + 1 < o.v.size(); ++i) {
        o.tri.push_back(0); o.tri.push_back((int)i); o.tri.push_back((int)(i + 1));
    }
    computeObjectDerived(o);
    const size_t nc = o.v.size(), ntri = o.tri.size() / 3;   /* capture AVANT le move */
    undoSnapshot("polygone");
    g_objs.push_back(std::move(o));
    selectOnly((int)g_objs.size() - 1);
    userMsg("polygone pose : %s, %zu cotes, %zu triangle(s) (feuille etirable)",
                g_objs.back().name.c_str(), nc, ntri);
    polyReset();
    g_polyMode = false;
    userMsg("mode selection");
}
/* B2.6b (retour de test) : clic droit ou Entree = VALIDER la surface, comme
   un clic sur le depart ; Echap seul sort sans rien poser */
static void polyFinish() {
    if (g_polyPts.size() >= 3) polyPose();
    else userMsg("polygone : au moins 3 sommets pour fermer la surface (clic = sommet suivant ; Echap = tout annuler)");
}
/* chaque clic pose un sommet ; clic sur le depart (>= 3 sommets) = fermeture */
static void polyTakePoint(GLFWwindow* w) {
    bool ok;
    Vec3 p;
    lineGroundPoint(w, ok, p);
    if (!ok) return;                                /* pointe le ciel : rien */
    if (g_polyPts.size() >= 3 && (p - g_polyPts[0]).norm() <= filPickTol()) {
        polyPose();                                 /* fermeture (surface seule) */
        return;
    }
    if (!g_polyPts.empty() && (p - g_polyPts.back()).norm() < 1e-9)
        return;                                     /* meme point : rien */
    g_polyPts.push_back(p);
    g_polyActive = true;
    userMsg("polygone : %zu sommet(s) (clic = sommet suivant ; clic sur le depart, clic droit ou Entree = ferme la surface ; Echap = tout annuler)", g_polyPts.size());
}
/* apercu : contour deja clique (ferme) + rubber band vers le curseur */
static void drawPolyPreview() {
    if (!g_polyMode || !g_polyCursorOk || g_polyPts.empty()) return;
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3d(0.92, 0.75, 0.1);
    glLineWidth(2.0);
    if (g_polyPts.size() >= 2) {                    /* contour (retour au depart visible) */
        glBegin(GL_LINE_LOOP);
        for (const Vec3& pt : g_polyPts) glVertex3d(pt.x, pt.y, pt.z);
        glEnd();
    }
    glBegin(GL_LINE_STRIP);                         /* rubber band vers le curseur */
    for (const Vec3& pt : g_polyPts) glVertex3d(pt.x, pt.y, pt.z);
    glVertex3d(g_polyCursor.x, g_polyCursor.y, g_polyCursor.z);
    glEnd();
    glLineWidth(1.0);
    glEnable(GL_DEPTH_TEST);
}

/* ------------------------------------------------------------------ */
/* B2.7 : outil Spline (Maj+S) - courbe cubique par POINTS DE PASSAGE   */
/* (choix utilisateur) ; parametrisation centripete (sqrt des cordes)   */
/* : robuste aux espacements irreguliers. Fil discretise en UN objet,   */
/* etirable au deplacement. Boucle fermee = clic sur le depart (>= 3    */
/* points) ; clic droit ou Entree = valider ouverte (>= 2 points) ;     */
/* Echap sort sans rien poser. Pas de saisie numerique v1.              */
/* ------------------------------------------------------------------ */
static bool g_splMode = false;          /* outil arme */
static bool g_splActive = false;        /* au moins un point clique */
static std::vector<Vec3> g_splPts;      /* points de passage cliques, monde (y = 0) */
static Vec3 g_splCursor(0, 0, 0);       /* point sol sous le curseur (apercu) */
static bool g_splCursorOk = false;
static bool g_splCtrl = false;          /* B2.7b : variante POINTS DE CONTROLE (Maj+B) */
static const int kSplSub = 16;          /* sous-segments par intervalle (v1 fixe) */

static void splReset() {
    g_splActive = false;
    g_splPts.clear();
}
static void splCancel(const char* pourquoi) {
    splReset();
    g_splMode = false;
    userMsg("spline : %s", pourquoi);
}
/* suivi du curseur chaque tour (apercu) */
static void splTrackCursor(GLFWwindow* w) {
    Vec3 p;
    bool ok;
    lineGroundPoint(w, ok, p);
    g_splCursor = p;
    g_splCursorOk = ok;
}
/* tangentes centripetes du segment [q1, q2] (formule Barry-Goldman) :
   le sqrt des cordes evite boucles et pics quand les clics sont
   irreguliers (defaut du Catmull-Rom uniforme) */
static void splTangents(const Vec3& q0, const Vec3& q1, const Vec3& q2, const Vec3& q3,
                        Vec3& tm1, Vec3& tm2) {
    const double dt0 = std::sqrt((q1 - q0).norm());
    const double dt1 = std::sqrt((q2 - q1).norm());
    const double dt2 = std::sqrt((q3 - q2).norm());
    if (dt0 < 1e-12 || dt1 < 1e-12)
        tm1 = (q2 - q1) * 0.5;
    else
        tm1 = (q2 - q1 + (q1 - q0) * (dt1 / dt0) - (q2 - q0) * (dt1 / (dt0 + dt1))) * (dt1 / (dt0 + dt1));
    if (dt1 < 1e-12 || dt2 < 1e-12)
        tm2 = (q2 - q1) * 0.5;
    else
        tm2 = (q2 - q1) * (dt2 / dt1) - (q3 - q1) * (dt2 / (dt1 + dt2)) + (q3 - q2);
}
/* base B-spline generale (Cox-de Boor recursif) */
static double splBasisV(int i, int p, const std::vector<double>& U, double u) {
    if (p == 0) return (u >= U[i] && u < U[i + 1]) ? 1.0 : 0.0;
    const double a = (U[i + p] > U[i] + 1e-12) ? (u - U[i]) / (U[i + p] - U[i]) : 0.0;
    const double b = (U[i + p + 1] > U[i + 1] + 1e-12) ? (U[i + p + 1] - u) / (U[i + p + 1] - U[i + 1]) : 0.0;
    return a * splBasisV(i, p - 1, U, u) + b * splBasisV(i + 1, p - 1, U, u);
}
/* noeuds serres uniformes : courbe ouverte = depart et arrivee EXACTS */
static std::vector<double> splKnotsOpen(int m, int p) {
    const int n = m - 1;
    std::vector<double> U(n + p + 2, 0.0);
    const double fin = (double)(n + 1 - p);
    for (int i = n + 1; i <= n + p + 1; ++i) U[i] = fin;
    for (int j = 1; j <= n - p; ++j) U[j + p] = (double)j;
    return U;
}
/* span du noeud (NURBS Book A2.1) : dernier k tel que U[k] <= u.
   OBLIGATOIRE pour les noeuds serres (repetes aux extremites) : un simple
   floor(u) y est FAUX et reduisait la courbe a des segments droits (bug
   B2.7d, retour de test). Les noeuds uniformes (ferme) s'y pretent : k = floor(u). */
static int splFindSpan(int n, int p, const std::vector<double>& U, double u) {
    if (u >= U[n + 1]) return n;                /* fin exacte */
    int lo = p, hi = n + 1;
    int k = (lo + hi) / 2;
    while (u < U[k] || u >= U[k + 1]) {
        if (u < U[k]) hi = k; else lo = k;
        k = (lo + hi) / 2;
    }
    return k;
}
/* evaluation de la courbe (controle) au parametre u ; fermee = indices modulo */
static Vec3 splEvalCtrl(const std::vector<Vec3>& P, int p, const std::vector<double>& U,
                        double u, bool fermee) {
    const int m = (int)P.size();
    Vec3 c;
    if (fermee) {
        const int k = (int)std::floor(u);       /* noeuds uniformes distincts */
        for (int j = k - p; j <= k; ++j) {
            const int i = ((j % m) + m) % m;
            c += P[i] * splBasisV(j, p, U, u);
        }
    } else {
        const int k = splFindSpan(m - 1, p, U, u);   /* noeuds serres : k >= p */
        for (int j = k - p; j <= k; ++j)
            c += P[j] * splBasisV(j, p, U, u);
    }
    return c;
}
/* variante POINTS DE CONTROLE (B2.7b, demande utilisateur) : les clics
   tracent le polygone de controle, la courbe glisse a l'interieur */
static std::vector<Vec3> splSampleCtrl(bool fermee) {
    const int m = (int)g_splPts.size();
    std::vector<Vec3> out;
    if (m < 2) return out;
    const int p = std::min(3, m - 1);
    std::vector<double> U;
    double u0 = 0.0;
    int span = 0;
    if (fermee) {
        span = m;                                   /* une periode */
        U.resize(m + 2 * p + 1);
        for (int i = 0; i < (int)U.size(); ++i) U[i] = (double)i;
        u0 = (double)p;
    } else {
        U = splKnotsOpen(m, p);
        span = m - p;                               /* domaine serre [0, m - p] */
    }
    const int total = span * kSplSub;
    out.reserve((size_t)total + 1);
    for (int j = 0; j < total; ++j)
        out.push_back(splEvalCtrl(g_splPts, p, U, u0 + (double)j / (double)kSplSub, fermee));
    if (fermee)
        out.push_back(out[0]);                      /* arete de cloture naturelle */
    else
        out.push_back(g_splPts.back());             /* arrivee exacte (noeud serre) */
    return out;
}
/* discretisation : polyline cubique par les points de passage
   (fermee = indices modulo, dernier sommet = depart) */
static std::vector<Vec3> splSample(bool fermee) {
    if (g_splCtrl) return splSampleCtrl(fermee);    /* B2.7b : variante points de controle */
    const size_t m = g_splPts.size();
    std::vector<Vec3> out;
    if (m < 2) return out;
    const size_t nseg = fermee ? m : m - 1;
    out.reserve(nseg * (size_t)kSplSub + 1);
    for (size_t s = 0; s < nseg; ++s) {
        const Vec3& q1 = g_splPts[s];
        const Vec3& q2 = g_splPts[(s + 1) % m];
        const Vec3 q0 = fermee ? g_splPts[(s + m - 1) % m]
                               : (s == 0 ? g_splPts[0] : g_splPts[s - 1]);
        const Vec3 q3 = fermee ? g_splPts[(s + 2) % m]
                               : (s + 2 == m ? g_splPts[m - 1] : g_splPts[s + 2]);
        Vec3 tm1, tm2;
        splTangents(q0, q1, q2, q3, tm1, tm2);
        for (int j = 0; j < kSplSub; ++j) {
            const double t = (double)j / (double)kSplSub;
            const double t2 = t * t, t3 = t2 * t;
            out.push_back(q1 * (2.0 * t3 - 3.0 * t2 + 1.0)
                          + tm1 * (t3 - 2.0 * t2 + t)
                          + q2 * (-2.0 * t3 + 3.0 * t2)
                          + tm2 * (t3 - t2));
        }
    }
    if (fermee)
        out.push_back(g_splPts[0]);                 /* arete de cloture naturelle */
    else
        out.push_back(g_splPts[m - 1]);             /* arrivee exacte */
    return out;
}
/* pose : fil discretise en UN objet ; ancre locale = premier point
   (convention pos + v = monde, commune a tous les profils B2) */
static void splPose(bool fermee) {
    std::vector<Vec3> pts = splSample(fermee);
    if (pts.size() < 2) return;
    Object o;
    o.name = uniqueName("Spline");
    o.pos = pts[0];
    o.scl = 1.0;
    o.v.resize(pts.size());
    for (size_t i = 0; i < o.v.size(); ++i) o.v[i] = pts[i] - o.pos;
    o.tri.clear();
    o.n.clear();                                    /* fil = objet sans triangle */
    /* B2.7e : les points de definition survivent a la pose (edition apres coup) */
    o.traceType = g_splCtrl ? 3 : 2;
    o.traceFerme = fermee ? 1 : 0;
    o.tracePts.resize(g_splPts.size());            /* capture AVANT le reset */
    for (size_t i = 0; i < o.tracePts.size(); ++i) o.tracePts[i] = g_splPts[i] - o.pos;
    computeObjectDerived(o);                        /* aretes = chaine derivee */
    const size_t np = g_splPts.size();              /* capture AVANT le reset */
    const size_t nv = o.v.size();
    undoSnapshot("spline");
    g_objs.push_back(std::move(o));
    selectOnly((int)g_objs.size() - 1);
    userMsg("spline posee : %s, %zu %s, %zu sommets discretises%s (fil etirable au deplacement)",
                g_objs.back().name.c_str(), np, g_splCtrl ? "point(s) de controle" : "point(s) de passage", nv, fermee ? ", boucle fermee" : "");
    splReset();
    g_splMode = false;
    userMsg("mode selection");
}
/* B2.6b generalisee : clic droit ou Entree = VALIDER (courbe ouverte) */
static void splFinish() {
    if (g_splPts.size() >= 2) splPose(false);
    else userMsg("spline : au moins 2 points pour poser la courbe (clic = point suivant ; Echap = tout annuler)");
}
/* chaque clic pose un point ; clic sur le depart (>= 3) = boucle fermee */
static void splTakePoint(GLFWwindow* w) {
    bool ok;
    Vec3 p;
    lineGroundPoint(w, ok, p);
    if (!ok) return;                                /* pointe le ciel : rien */
    if (g_splPts.size() >= 3 && (p - g_splPts[0]).norm() <= filPickTol()) {
        splPose(true);                              /* boucle fermee lissee */
        return;
    }
    if (!g_splPts.empty() && (p - g_splPts.back()).norm() < 1e-9)
        return;                                     /* meme point : rien */
    g_splPts.push_back(p);
    g_splActive = true;
    userMsg("spline : %zu point(s) (clic = point suivant ; clic sur le depart = boucle fermee ; clic droit ou Entree = valider ; Echap = tout annuler)", g_splPts.size());
}
/* apercu : courbe deja passee par les points + droite vers le curseur */
static void drawSplinePreview() {
    if (!g_splMode || g_splPts.empty()) return;
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3d(0.92, 0.75, 0.1);
    glLineWidth(2.0);
    if (g_splCtrl && g_splPts.size() >= 2) {        /* B2.7b : polygone de controle visible */
        glColor3d(0.55, 0.55, 0.35);
        glBegin(GL_LINE_STRIP);
        for (const Vec3& pt : g_splPts) glVertex3d(pt.x, pt.y, pt.z);
        glEnd();
        glColor3d(0.92, 0.75, 0.1);
    }
    if (g_splPts.size() >= 2) {                     /* courbe lisse des points cliques */
        glBegin(GL_LINE_STRIP);
        for (const Vec3& pt : splSample(false)) glVertex3d(pt.x, pt.y, pt.z);
        glEnd();
    }
    if (g_splCursorOk) {                            /* rubber band vers le curseur */
        glBegin(GL_LINE_STRIP);
        glVertex3d(g_splPts.back().x, g_splPts.back().y, g_splPts.back().z);
        glVertex3d(g_splCursor.x, g_splCursor.y, g_splCursor.z);
        glEnd();
    }
    glLineWidth(1.0);
    glEnable(GL_DEPTH_TEST);
}

/* ------------------------------------------------------------------ */
/* B2.7e : EDITION DES TRACES APRES COUP - la source de verite est     */
/* TraceData (type + points de definition + ouvert/ferme) ; la         */
/* geometrie est regeneree par LES MEMES fonctions que le trace        */
/* (un seul chemin de code). E = entrer/sortir, poignees drag au sol,  */
/* clic sur la courbe = ajouter un point, poignee + Suppr = supprimer, */
/* Maj+S/Maj+B = variante spline, O = ouvert/ferme.                    */
/* ------------------------------------------------------------------ */
static int g_editGrab = -1;             /* poignee saisie (drag en cours) */
static int g_editSel = -1;              /* poignee selectionnee (cible de Suppr) */
/* B2.7e E3 : drag de la poignee de HAUTEUR (extrusion type 8) - plan vertical
   face camera, capte a la saisie ; g_editH0 = longueur locale au depart */
static Vec3 g_editHPt{0, 0, 0};         /* centre du cap superieur (monde) a la saisie */
static Vec3 g_editHN{0, 0, 0};          /* normale du plan vertical (monde, horizontale) */
static Vec3 g_editHDir{0, 1, 0};        /* direction du vecteur d extrusion (unitaire) */
static double g_editH0 = 0.0;           /* longueur locale du vecteur a la saisie */

/* distance point-rayon (capture des poignees) */
static double pointRayDist(const Vec3& org, const Vec3& dir, const Vec3& p) {
    const double t = dot(p - org, dir);
    if (t <= 0.0) return (p - org).norm();
    return (org + dir * t - p).norm();
}
/* regeneration de la geometrie depuis les parametres (source de verite) :
   MEMES fonctions que le trace ; tracePts LOCAUX -> v LOCAUX, la convention
   pos + scl*v = monde est preservee (la translation ne change rien aux
   echantillonnages, centripete incluse : longueurs de cordes). */
/* E3b (retour test) : un point ajoute EXACTEMENT sur un bord (clic sur un
   mur = projection verticale) est colineaire a ses voisins -> le fan du cap
   produirait un tri de surface NULLE (normale nulle = "Face non plane" au
   push/pull). Ce helper pousse le tri seulement s il a une surface. */
static void pushFanTri(std::vector<unsigned>& tri, const std::vector<Vec3>& v,
                       unsigned a, unsigned b, unsigned c) {
    if (a >= v.size() || b >= v.size() || c >= v.size()) return;
    const Vec3 n = cross(v[b] - v[a], v[c] - v[a]);
    if (dot(n, n) < 1e-14) return;                  /* colineaire : rien */
    tri.push_back(a); tri.push_back(b); tri.push_back(c);
}

static void traceRegenBase(Object& o);   /* E2 : formes de base (defini plus bas) */
static void traceRegenSweep(Object& o);  /* B3 : balayage (defini plus bas) */
static void traceRegen(Object& o) {
    if (o.traceType == 0 || o.tracePts.size() < 2) return;
    if (o.traceType == 9) { traceRegenSweep(o); return; }  /* B3 : balayage */
    if (o.traceType >= 4) { traceRegenBase(o); return; }   /* E2 : rectangle/cercle/arc/polygone */
    if (o.traceType == 1) {                     /* LIGNE : v = points */
        const bool surface = o.traceFerme && o.traceSurface && !o.tri.empty();
        o.v = o.tracePts;
        o.tri.clear();
        o.n.clear();
        if (surface)
            for (size_t i = 1; i + 1 < o.v.size(); ++i)
                pushFanTri(o.tri, o.v, 0, (unsigned)i, (unsigned)(i + 1));
        computeObjectDerived(o);
        if (o.traceFerme && o.tri.empty())      /* fil fermé : arete de cloture */
            o.edges.emplace_back((int)o.v.size() - 1, 0);
        return;
    }
    /* SPLINE (passage ou controle) : MEME code que le trace - g_splPts sert
       de tampon (le mode de trace est force sorti avant toute edition) ;
       points LOCAUX -> courbe LOCALE, posee telle quelle dans v */
    g_splPts = o.tracePts;
    const bool oldCtrl = g_splCtrl;
    g_splCtrl = (o.traceType == 3);
    const std::vector<Vec3> pts = splSample(o.traceFerme != 0);
    g_splCtrl = oldCtrl;
    g_splPts.clear();
    if (pts.size() < 2) return;
    o.v = pts;
    o.tri.clear();
    o.n.clear();
    computeObjectDerived(o);
    return;
}

/* E3 : chaine du profil d une extrusion (LOCALES) - UN SEUL CHEMIN DE CODE,
   reutilise par la regen, l index d insertion et la garde du clic-sol ;
   MEMES constructions que le trace (ligne, spline, rect, cercle, arc, poly) */
static bool traceProfilChain(const Object& o, std::vector<Vec3>& ch) {
    const int ptype = o.traceProfil;
    if (ptype == 1) {
        ch = o.tracePts;
    } else if (ptype == 2 || ptype == 3) {
        g_splPts = o.tracePts;
        const bool oldCtrl = g_splCtrl;
        g_splCtrl = (ptype == 3);
        ch = splSample(o.traceFerme != 0);
        g_splCtrl = oldCtrl;
        g_splPts.clear();
    } else if (ptype == 4) {
        const Vec3& a = o.tracePts[0];
        const Vec3& b = o.tracePts[1];
        const double x0 = (a.x < b.x) ? a.x : b.x, x1 = (a.x < b.x) ? b.x : a.x;
        const double z0 = (a.z < b.z) ? a.z : b.z, z1 = (a.z < b.z) ? b.z : a.z;
        ch = { Vec3(x0, 0, z0), Vec3(x1, 0, z0), Vec3(x1, 0, z1), Vec3(x0, 0, z1) };
    } else if (ptype == 5) {
        const Vec3 c = o.tracePts[0];
        const double r = (o.tracePts[1] - c).norm();
        if (r < 1e-9) return false;
        ch.reserve((size_t)CERCLE_SEG);
        for (int i = 0; i < CERCLE_SEG; ++i) {
            const double a = 6.283185307179586 * double(i) / double(CERCLE_SEG);
            ch.push_back(c + Vec3(r * std::cos(a), 0.0, -r * std::sin(a)));
        }
    } else if (ptype == 6) {
        if (!arcComputePts(o.tracePts[0], o.tracePts[1], o.tracePts[2], ch)) return false;
    } else if (ptype == 7) {
        ch = o.tracePts;
    } else {
        return false;
    }
    return ch.size() >= 2;
}
/* E3 : distance d un point local a la chaine du profil (segments, cloture
   incluse si ferme) - garde du clic-sol pour l ajout de point */
static double traceProfilDist(const Object& o, const Vec3& pLocal) {
    std::vector<Vec3> ch;
    if (!traceProfilChain(o, ch)) return 1e300;
    const size_t n = ch.size();
    const bool ferme = o.traceFerme != 0 || o.traceProfil == 4
                    || o.traceProfil == 5 || o.traceProfil == 7;
    const size_t nseg = ferme ? n : n - 1;
    double best = 1e300;
    for (size_t i = 0; i < nseg; ++i) {
        const Vec3 a = ch[i], b = ch[(i + 1) % n];
        const Vec3 ab = b - a;
        const double l2 = dot(ab, ab);
        double t = l2 > 1e-18 ? dot(pLocal - a, ab) / l2 : 0.0;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        const double d = (a + ab * t - pLocal).norm();
        if (d < best) best = d;
    }
    return best;
}

/* B3 : chaine du CHEMIN d'un balayage (LOCALES) - MEMES constructions que
   le trace du fil (ligne, spline, arc) */
static bool traceCheminChain(const Object& o, std::vector<Vec3>& ch) {
    const int ct = o.traceCheminType;
    if (ct == 1) {
        ch = o.traceChemin;
    } else if (ct == 2 || ct == 3) {
        g_splPts = o.traceChemin;
        const bool oldCtrl = g_splCtrl;
        g_splCtrl = (ct == 3);
        ch = splSample(o.traceCheminFerme != 0);
        g_splCtrl = oldCtrl;
        g_splPts.clear();
    } else if (ct == 6) {
        if (o.traceChemin.size() < 3) return false;
        if (!arcComputePts(o.traceChemin[0], o.traceChemin[1], o.traceChemin[2], ch)) return false;
    } else {
        return false;
    }
    return ch.size() >= 2;
}

/* B3.5 : rotation de v d un angle t autour de l axe unitaire ax (Rodrigues) */
static Vec3 vecRotate(const Vec3& v, const Vec3& ax, double t) {
    return v * std::cos(t) + cross(ax, v) * std::sin(t)
         + ax * (dot(ax, v) * (1.0 - std::cos(t)));
}

/* ===== B3.7 : collage generique 2 objets (coin/coin, arete/arete) =====
   Principe (generalise de B3.4/B3.5) : chaque forme possede des POINTS
   D'ACCROCHE = sommets + milieux d'aretes. Selectionner A (un objet), menu
   contextuel "Coller sur...", clic sur la cible d'un AUTRE objet B :
   l'accroche de A la plus proche de la cible (collage intelligent) arrive
   dessus ; si la cible est sur une arete, A tourne autour de l'accroche
   pour aligner son arete la mieux orientee (arbitrage 3b). Pose definitive
   (arbitrage 4a) : un pas "collage" (B3.6z). Cibles : sommet (snap exact),
   arete (point le plus proche, tolerance filPickTol), point de face.     */
static int g_collStage = 0;     /* 0 = inactif ; 1 = attend la cible */
static int g_collSrc = -1;      /* objet A a coller */
static void collCancel(const char* pourquoi) {
    g_collStage = 0; g_collSrc = -1;
    userMsg("%s", pourquoi);
}
static void collStart() {
    if (g_collStage) { collCancel("collage annule"); return; }   /* toggle */
    if (selectedCount() != 1) { flashBand("collage : selectionnez UN objet a coller (v1)"); return; }
    g_collSrc = g_selected;
    g_collStage = 1;
    userMsg("collage : cliquez la cible sur un AUTRE objet (sommet, arete, face) ; Echap = annuler");
}
/* accroches d'un objet (MONDE) : sommets + milieux d'aretes derives */
struct CollAcc { Vec3 p; int va, vb; };   /* vb < 0 = sommet ; sinon milieu de l'arete va-vb */
static void collAccroches(const Object& o, std::vector<CollAcc>& out) {
    out.clear();
    for (size_t i = 0; i < o.v.size(); ++i)
        out.push_back({ o.pos + o.v[i] * o.scl, (int)i, -1 });
    for (const auto& e : o.edges)
        out.push_back({ o.pos + (o.v[(size_t)e.first] + o.v[(size_t)e.second]) * (0.5 * o.scl),
                        e.first, e.second });
}
/* rotation de l'objet autour de l'accroche (monde) : pos ET donnees locales
   tournent ensemble (B2.7e : les parametres de construction suivent la
   geometrie, une regen retrouve la meme forme) */
static void collTourne(Object& o, const Vec3& pivotW, const Vec3& axe, double ang) {
    /* rotation rigide autour du pivot monde P : pos tourne autour de P et
       les donnees LOCALES tournent autour de l'ORIGINE du repere local
       (p_new = R(p)) : monde_i = pos_new + R(p_i)*scl = P + R(monde_i - P).
       (une premiere version tournait les locales autour de pl = (P - pos)/scl,
       ce qui ajoutait une erreur de translation R(pos-P) - (pos-P) : l'accroche
       collee sautait hors de la cible des que l'objet etait loin du pivot) */
    o.pos = pivotW + vecRotate(o.pos - pivotW, axe, ang);
    for (Vec3& p : o.tracePts)    p = vecRotate(p, axe, ang);
    for (Vec3& p : o.traceChemin) p = vecRotate(p, axe, ang);
    o.traceVec = vecRotate(o.traceVec, axe, ang);
    if (o.traceType == 0)         /* mesh pur (import) : les sommets directement */
        for (Vec3& p : o.v) p = vecRotate(p, axe, ang);
    computeObjectDerived(o);
}
/* clic de cible : face (rayon/tri) en priorite (les solides gagnent, regle
   de selection B3.6a-bis), sinon fil/arete (point le plus proche du rayon,
   projete sur le segment) ; puis accroche intelligente + alignement 3b */
static void collClick(GLFWwindow* w) {
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return;
    if (g_collSrc < 0 || g_collSrc >= (int)g_objs.size()) { collCancel("collage : objet source perdu"); return; }
    int hit = -1; double bestT = 1e300;
    for (size_t i = 0; i < g_objs.size(); ++i) {
        if ((int)i == g_collSrc) continue;
        double t; size_t ti;
        if (rayObjectTri(org, dir, g_objs[i], t, &ti) && t < bestT) { bestT = t; hit = (int)i; }
    }
    int hitFil = -1; double bestTf = 1e300;
    if (hit < 0)
        for (size_t i = 0; i < g_objs.size(); ++i) {
            if ((int)i == g_collSrc) continue;
            const double t = filRayHit(org, dir, g_objs[i], filPickTol());
            if (t >= 0.0 && t < bestTf) { bestTf = t; hitFil = (int)i; }
        }
    if (hit < 0 && hitFil < 0) { flashBand("collage : aucune cible visee - cliquez un autre objet"); return; }
    /* point cible + arete cible eventuelle (snap sommet, sinon arete dans la tolerance) */
    Vec3 cible; int cObj; std::pair<int, int> areteB(-1, -1);
    if (hit >= 0) {
        cObj = hit; cible = org + dir * bestT;
        const Object& b = g_objs[(size_t)cObj];
        const double tol = filPickTol();
        double dv = 1e300; unsigned iv = 0;
        for (size_t i = 0; i < b.v.size(); ++i) {
            const double d = (b.pos + b.v[i] * b.scl - cible).norm();
            if (d < dv) { dv = d; iv = (unsigned)i; }
        }
        double de = 1e300; Vec3 pe = cible;
        for (const auto& e : b.edges) {
            const Vec3 a = b.pos + b.v[(size_t)e.first] * b.scl;
            const Vec3 ab = b.pos + b.v[(size_t)e.second] * b.scl - a;
            const double L2 = dot(ab, ab);
            if (L2 < 1e-18) continue;
            double u = dot(cible - a, ab) / L2;
            u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
            const double d = (a + ab * u - cible).norm();
            if (d < de) { de = d; pe = a + ab * u; areteB = e; }
        }
        if (dv <= tol) { cible = b.pos + b.v[iv] * b.scl; areteB = std::make_pair(-1, -1); }   /* coin exact */
        else if (de <= tol) cible = pe;                                                        /* arete */
    } else {
        cObj = hitFil;
        const Object& f = g_objs[(size_t)cObj];
        const Vec3 pr = org + dir * bestTf;
        double de = 1e300; Vec3 pe = pr;
        for (const auto& e : f.edges) {
            const Vec3 a = f.pos + f.v[(size_t)e.first] * f.scl;
            const Vec3 ab = f.pos + f.v[(size_t)e.second] * f.scl - a;
            const double L2 = dot(ab, ab);
            if (L2 < 1e-18) continue;
            double u = dot(pr - a, ab) / L2;
            u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
            const double d = (a + ab * u - pr).norm();
            if (d < de) { de = d; pe = a + ab * u; areteB = e; }
        }
        cible = pe;
    }
    /* accroche de A la plus proche de la cible (collage intelligent, B3.5) */
    std::vector<CollAcc> acc;
    collAccroches(g_objs[(size_t)g_collSrc], acc);
    if (acc.empty()) { collCancel("collage : l'objet source n'a aucune accroche"); return; }
    size_t bi = 0; double bd = 1e300;
    for (size_t i = 0; i < acc.size(); ++i) {
        const double d = (acc[(size_t)i].p - cible).norm();
        if (d < bd) { bd = d; bi = i; }
    }
    undoSnapshot("collage");
    Object& o = g_objs[(size_t)g_collSrc];
    o.pos = o.pos + (cible - acc[bi].p);
    /* 3b : alignement arete/arete - l'arete de A passant par l'accroche la
       mieux orientee s'aligne sur l'arete cible (angle <= 90 deg) */
    bool aligne = false;
    if (areteB.first >= 0) {
        const Object& b = g_objs[(size_t)cObj];
        Vec3 db = (b.v[(size_t)areteB.second] - b.v[(size_t)areteB.first]) * b.scl;
        const double Lb = db.norm();
        if (Lb > 1e-12) {
            db = db * (1.0 / Lb);
            Vec3 da; double Ld = -1.0;
            if (acc[bi].vb >= 0) {
                Vec3 d2 = (o.v[(size_t)acc[bi].vb] - o.v[(size_t)acc[bi].va]) * o.scl;
                const double L2 = d2.norm();
                if (L2 > 1e-12) { da = d2 * (1.0 / L2); Ld = 1.0; }
            } else {
                for (const auto& e : o.edges) {
                    if (e.first != acc[bi].va && e.second != acc[bi].va) continue;
                    Vec3 d2 = (o.v[(size_t)e.second] - o.v[(size_t)e.first]) * o.scl;
                    const double L2 = d2.norm();
                    if (L2 < 1e-12) continue;
                    d2 = d2 * (1.0 / L2);
                    if (std::fabs(dot(d2, db)) > Ld) { Ld = std::fabs(dot(d2, db)); da = d2; }
                }
            }
            if (Ld > 0.0) {
                const Vec3 s = da * (dot(da, db) >= 0.0 ? 1.0 : -1.0);   /* angle <= 90 deg */
                double cd = dot(s, db); cd = cd > 1.0 ? 1.0 : (cd < -1.0 ? -1.0 : cd);
                const double ang = std::acos(cd);
                if (ang > 1e-9) {
                    const Vec3 axe = cross(s, db);
                    const double Ln = axe.norm();
                    if (Ln > 1e-12) { collTourne(o, cible, axe * (1.0 / Ln), ang); aligne = true; }
                }
            }
        }
    }
    computeObjectDerived(o);
    selectOnly(g_collSrc);
    const std::string nomCible = g_objs[(size_t)cObj].name;
    g_collStage = 0; g_collSrc = -1;
    userMsg("collage : %s colle sur %s (accroche : %s%s)", o.name.c_str(), nomCible.c_str(),
                acc[bi].vb < 0 ? "sommet" : "milieu", aligne ? ", arete alignee" : "");
    userMsg("mode selection");
}

/* B3.6a : profil FERME d'un balayage (regle commune regen / accroches) */
static bool sweepProfFerme(const Object& o) {
    return o.traceFerme != 0 || o.traceProfil == 4
        || o.traceProfil == 5 || o.traceProfil == 7;
}
/* B3.6a : ACCROCHES du profil d'un balayage (LOCALES, chaine du profil) -
   sommets + MILIEUX de segments (+ centre pour le cercle) ; la MEME liste
   sert au collage intelligent (regen) et aux poignees d'edition (UN SEUL
   CHEMIN DE CODE) */
static bool sweepAccroches(const Object& o, std::vector<Vec3>& acc) {
    std::vector<Vec3> prof;
    if (!traceProfilChain(o, prof)) return false;
    const size_t n = prof.size();
    for (size_t k = 0; k < n; ++k) acc.push_back(prof[k]);
    const size_t nsegA = sweepProfFerme(o) ? n : n - 1;
    for (size_t k = 0; k < nsegA; ++k)
        acc.push_back((prof[k] + prof[(k + 1) % n]) * 0.5);
    if (o.traceProfil == 5 && !o.tracePts.empty())
        acc.push_back(o.tracePts[0]);           /* centre du cercle (tore) */
    return true;
}

/* B3 : REGEN du balayage - anneaux du profil transportes le long du chemin
   (transport parallele projete = pas de torsion, arbitrage v1) ; caps si le
   chemin est ouvert et le profil plein ; chemin ferme = solide torique sans
   caps ; orientation globale corrigee par le volume signe (normales
   exterieures, quel que soit le sens du profil ou du chemin) */
static void traceRegenSweep(Object& o) {
    std::vector<Vec3> prof, chem;
    if (!traceProfilChain(o, prof) || !traceCheminChain(o, chem)) return;
    const size_t n = prof.size();
    const bool aCentre = (o.traceProfil == 5);
    const bool profFerme = sweepProfFerme(o);   /* B3.6a : regle commune */
    const bool plein = profFerme && ((o.traceFerme != 0 && o.traceSurface != 0)
                    || o.traceProfil == 4 || o.traceProfil == 5 || o.traceProfil == 7);
    const size_t rimBase = aCentre ? 1 : 0;
    const size_t nBase = aCentre ? n + 1 : n;
    const size_t nRings = chem.size();
    const bool boucle = o.traceCheminFerme != 0;
    const size_t nsegPath = boucle ? nRings : nRings - 1;
    /* normale de reference du roll (traceVec = normale de la face au balayage) */
    Vec3 Nv = o.traceVec;
    if (Nv.norm() < 1e-9) Nv = Vec3(0, 1, 0);
    Nv = Nv * (1.0 / Nv.norm());
    /* tangentes (differences centrees, repli aux bouts et sur points doubles) */
    std::vector<Vec3> Tv(nRings), Nn(nRings), Bb(nRings);
    for (size_t i = 0; i < nRings; ++i) {
        Vec3 t = ((i + 1 < nRings) ? chem[i + 1] : (boucle ? chem[0] : chem[i]))
               - ((i > 0) ? chem[i - 1] : (boucle ? chem[nRings - 1] : chem[i]));
        if (t.norm() < 1e-12) t = (i > 0) ? Tv[i - 1] : Vec3(0, 0, 1);
        if (t.norm() < 1e-12) return;
        Tv[i] = t * (1.0 / t.norm());
    }
    Vec3 n0 = Nv - Tv[0] * dot(Nv, Tv[0]);
    if (n0.norm() < 1e-9) {                         /* face parallele au depart du chemin : repli */
        n0 = (std::fabs(Tv[0].y) < 0.9) ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        n0 = n0 - Tv[0] * dot(n0, Tv[0]);
    }
    if (n0.norm() < 1e-9) return;
    n0 = n0 * (1.0 / n0.norm());
    Nn[0] = n0;
    Bb[0] = cross(Tv[0], n0);
    for (size_t i = 1; i < nRings; ++i) {           /* transport parallele (projete) */
        Vec3 ni = Nn[i - 1] - Tv[i] * dot(Nn[i - 1], Tv[i]);
        if (ni.norm() < 1e-12) ni = Nn[i - 1];      /* tangente retournee : garder la frame */
        ni = ni * (1.0 / ni.norm());
        Nn[i] = ni;
        Bb[i] = cross(Tv[i], ni);
    }
    o.v.clear(); o.tri.clear(); o.n.clear();
    o.v.reserve(nBase * nRings);
    /* B3.5 (retours test, conception B3.4) : COLLAGE INTELLIGENT - l ACCROCHE
       du profil (sommets + MILIEUX de segments) la plus proche du fil (TOUT le
       fil, cloture incluse) est posee SUR le fil ; elle suit le chemin a
       toutes les stations (la forme glisse le long du fil par son point de
       connexion, fidele Follow Me). L'ancienne regle (point du profil le plus
       proche du DEPART du chemin) imposait le point de connexion. */
    std::vector<Vec3> acc;
    if (!sweepAccroches(o, acc) || acc.empty()) return;
    Vec3 A = acc[0];
    double dA = 1e300;
    const size_t nch = chem.size();
    const size_t nsegC = boucle ? nch : nch - 1;
    if (o.traceAccroche >= 0 && (size_t)o.traceAccroche < acc.size()) {
        A = acc[(size_t)o.traceAccroche];       /* B3.6a : accroche PINCEE */
    } else {
        for (size_t j = 0; j < acc.size(); ++j) {
            for (size_t i = 0; i < nsegC; ++i) {
                const Vec3 p0 = chem[i], p1 = chem[(i + 1) % nch];
                const Vec3 ab = p1 - p0;
                const double l2 = dot(ab, ab);
                double t = l2 > 1e-18 ? dot(acc[j] - p0, ab) / l2 : 0.0;
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
                const double d = (p0 + ab * t - acc[j]).norm();
                if (d < dA) { dA = d; A = acc[j]; }
            }
        }
    }
    /* B3.5 : ROLL par defaut = COLLER PLAT - la face garde son orientation
       tracee et se releve du PLUS COURT chemin pour faire face au depart du
       chemin (rotation minimale amenant la normale de la face sur la
       tangente) : les directions de la face tracee sont conservees, plus
       d'inclinaison arbitraire (avant, le repere 2D partait de prof[0],
       un coin quelconque : un carre pose devenait un losange incline) */
    Vec3 ax = cross(Nv, Tv[0]);
    const double sn = ax.norm();
    double ang = 0.0;
    if (sn > 1e-12) {
        ax = ax * (1.0 / sn);
        ang = std::atan2(sn, dot(Nv, Tv[0]));
    } else if (dot(Nv, Tv[0]) < 0.0) {
        /* face opposee a la tangente : rotation a 180 deg autour d un axe
           perpendiculaire (repli canonique) */
        ax = cross(Nv, Vec3(0, 1, 0));
        if (ax.norm() < 1e-12) ax = Vec3(1, 0, 0);
        ax = ax * (1.0 / ax.norm());
        ang = 3.14159265358979;
    }
    std::vector<double> aa(n), bb(n);
    for (size_t k = 0; k < n; ++k) {
        const Vec3 wv = vecRotate(prof[k] - A, ax, ang);
        aa[k] = dot(wv, Nn[0]);
        bb[k] = dot(wv, Bb[0]);
    }
    double aC = 0.0, bC = 0.0;
    if (aCentre) {
        const Vec3 wv = vecRotate(o.tracePts[0] - A, ax, ang);
        aC = dot(wv, Nn[0]);
        bC = dot(wv, Bb[0]);
    }
    /* B3.6b : ROLL utilisateur - rotation du profil dans SON PLAN (autour de
       Tv[0], a l'accoche A) ; 0 degre = collage plat (defaut B3.5 intact) */
    if (std::fabs(o.traceRoll) > 1e-12) {
        const double R = o.traceRoll * 3.14159265358979 / 180.0;
        const double cr = std::cos(R), sr = std::sin(R);
        for (size_t k = 0; k < n; ++k) {
            const double na = aa[k] * cr - bb[k] * sr;
            const double nb = aa[k] * sr + bb[k] * cr;
            aa[k] = na; bb[k] = nb;
        }
        if (aCentre) {
            const double na = aC * cr - bC * sr;
            const double nb = aC * sr + bC * cr;
            aC = na; bC = nb;
        }
    }
    for (size_t r = 0; r < nRings; ++r) {
        if (aCentre) o.v.push_back(chem[r] + Nn[r] * aC + Bb[r] * bC);
        for (size_t k = 0; k < n; ++k)
            o.v.push_back(chem[r] + Nn[r] * aa[k] + Bb[r] * bb[k]);
    }
    /* B3.6b : la REGEN POSE le repere du roll (UN SEUL CHEMIN DE CODE) -
       poignee et drag ne font que le lire ; poignee = point du profil le
       plus LOIN de l'accoche (rayon non nul meme accroche sur un sommet) */
    g_sweepRollAxis = Tv[0];
    g_sweepRollCenter = o.pos + chem[0] * o.scl;   /* image de l accroche sur le fil */
    {
        double dFar = -1.0; size_t kFar = 0;
        for (size_t k = 0; k < n; ++k) {
            const double d = (prof[k] - A).norm();
            if (d > dFar) { dFar = d; kFar = k; }
        }
        g_sweepRollHandle = o.pos + o.v[rimBase + kFar] * o.scl;
    }
    /* murs : quads entre anneaux consecutifs (segments du profil, cloture incluse si ferme) */
    const size_t nsegProf = profFerme ? n : n - 1;
    for (size_t r = 0; r < nsegPath; ++r) {
        const size_t r2 = (r + 1) % nRings;
        const size_t b0 = r * nBase, t0 = r2 * nBase;
        for (size_t k = 0; k < nsegProf; ++k) {
            const unsigned bi = (unsigned)(b0 + rimBase + k);
            const unsigned bj = (unsigned)(b0 + rimBase + (k + 1) % n);
            const unsigned ti = (unsigned)(t0 + rimBase + k);
            const unsigned tj = (unsigned)(t0 + rimBase + (k + 1) % n);
            o.tri.push_back(bi); o.tri.push_back(bj); o.tri.push_back(tj);
            o.tri.push_back(bi); o.tri.push_back(tj); o.tri.push_back(ti);
        }
    }
    /* caps : chemin OUVERT et profil plein seulement (pushFanTri ecarte les
       tris degenere - meme garde que E3c) */
    if (plein && !boucle) {
        const unsigned rf = (unsigned)((nRings - 1) * nBase);
        if (aCentre) {
            for (size_t k = 0; k < n; ++k) {
                const unsigned s0 = (unsigned)(rimBase + k);
                const unsigned s1 = (unsigned)(rimBase + (k + 1) % n);
                pushFanTri(o.tri, o.v, 0, s1, s0);                                  /* cap depart (inverse) */
                pushFanTri(o.tri, o.v, rf, (unsigned)(rf + s0), (unsigned)(rf + s1));  /* cap fin (direct) */
            }
        } else {
            for (size_t k = 1; k + 1 < n; ++k) {
                const unsigned a0 = (unsigned)k, a1 = (unsigned)(k + 1);
                pushFanTri(o.tri, o.v, 0, a1, a0);
                pushFanTri(o.tri, o.v, rf, (unsigned)(rf + a0), (unsigned)(rf + a1));
            }
        }
    }
    /* orientation globale : volume signe >= 0 (normales exterieures) */
    double v6 = 0.0;
    for (size_t i = 0; i + 2 < o.tri.size(); i += 3) {
        const Vec3& a = o.v[o.tri[i]];
        const Vec3& b = o.v[o.tri[i + 1]];
        const Vec3& cc = o.v[o.tri[i + 2]];
        v6 += dot(a, cross(b, cc));
    }
    if (v6 < 0.0)
        for (size_t i = 0; i + 2 < o.tri.size(); i += 3) std::swap(o.tri[i + 1], o.tri[i + 2]);
    computeObjectDerived(o);
}

/* E2 (B2.7e) : formes de base - MEMES constructions que les poses
   (rectPose, cerclePose, arcComputePts, fan du polygone) */
static void traceRegenBase(Object& o) {
    if (o.traceType == 4) {                     /* RECTANGLE : 2 coins (locales) */
        const Vec3& a = o.tracePts[0];
        const Vec3& b = o.tracePts[1];
        const double x0 = (a.x < b.x) ? a.x : b.x, x1 = (a.x < b.x) ? b.x : a.x;
        const double z0 = (a.z < b.z) ? a.z : b.z, z1 = (a.z < b.z) ? b.z : a.z;
        o.v = { Vec3(x0, 0, z0), Vec3(x1, 0, z0), Vec3(x1, 0, z1), Vec3(x0, 0, z1) };
        o.tri = { 0, 2, 1, 0, 3, 2 };           /* normale +y (comme rectPose) */
        o.n.clear();
        computeObjectDerived(o);
        return;
    }
    if (o.traceType == 5) {                     /* CERCLE : centre + point du rayon */
        const Vec3 c = o.tracePts[0];
        const double r = (o.tracePts[1] - c).norm();
        if (r < 1e-9) return;                   /* rayon nul : garder la derniere geometrie */
        o.v.clear();
        o.v.reserve(1 + (size_t)CERCLE_SEG);
        o.v.push_back(c);
        for (int i = 0; i < CERCLE_SEG; ++i) {
            const double a = 6.283185307179586 * double(i) / double(CERCLE_SEG);
            o.v.push_back(c + Vec3(r * std::cos(a), 0.0, -r * std::sin(a)));
        }
        o.tri.clear();
        for (int i = 0; i < CERCLE_SEG; ++i) {
            o.tri.push_back(0);
            o.tri.push_back(1 + i);
            o.tri.push_back(1 + (i + 1) % CERCLE_SEG);
        }
        o.n.clear();
        computeObjectDerived(o);
        return;
    }
    if (o.traceType == 6) {                     /* ARC : depart, fin, bombement */
        std::vector<Vec3> pts;
        if (!arcComputePts(o.tracePts[0], o.tracePts[1], o.tracePts[2], pts))
            return;                             /* alignes/arc nul : garder la derniere geometrie */
        o.v = pts;   /* B2.7e2b : memes LOCALES que tracePts (pos + v = pos + tracePts[i]
                        = monde) - rebaser sur tracePts[0] detacherait les poignees du
                        des que le depart est deplace (tracePts[0] != 0) */
        o.tri.clear();
        o.n.clear();
        computeObjectDerived(o);
        return;
    }
    if (o.traceType == 7) {                     /* POLYGONE : fan (surface seule) */
        o.v = o.tracePts;
        o.tri.clear();
        o.n.clear();
        for (size_t i = 1; i + 1 < o.v.size(); ++i)
            pushFanTri(o.tri, o.v, 0, (unsigned)i, (unsigned)(i + 1));
        computeObjectDerived(o);
        return;
    }
    if (o.traceType == 8) {                     /* EXTRUSION : profil + vecteur (E3) */
        std::vector<Vec3> ch;
        if (!traceProfilChain(o, ch)) return;
        const size_t n = ch.size();
        const Vec3 h = o.traceVec;
        /* sub 5 (cercle) : structure CENTRE + bord conservee (comme
           cerclePose) - bug retour de test : un fan centre sur un point du
           bord rend le cap malsain (jonction sur une intersection) */
        const bool aCentre = (o.traceProfil == 5);
        const Vec3 c0 = aCentre ? o.tracePts[0] : Vec3(0, 0, 0);
        /* rect/cercle/polygone : profils FERMES par nature (leurs poses ne
           renseignent pas traceFerme) ; ligne/spline : selon le drapeau */
        const bool ferme = o.traceFerme != 0 || o.traceProfil == 4
                        || o.traceProfil == 5 || o.traceProfil == 7;
        const bool plein = ferme && ((o.traceFerme != 0 && o.traceSurface != 0)
                        || o.traceProfil == 4 || o.traceProfil == 5 || o.traceProfil == 7);
        const size_t rimBase = aCentre ? 1 : 0;         /* debut du bord dans v */
        const size_t nBase = aCentre ? n + 1 : n;       /* nb sommets d une couche */
        o.v.clear();
        o.v.reserve(nBase * 2);
        if (aCentre) o.v.push_back(c0);
        for (const Vec3& p : ch) o.v.push_back(p);
        if (h.norm() > 1e-9) {
            if (aCentre) o.v.push_back(c0 + h);
            for (const Vec3& p : ch) o.v.push_back(p + h);
        }
        o.tri.clear();
        o.n.clear();
        if (o.v.size() == nBase) {              /* hauteur nulle : feuille (ou fil) */
            if (plein) {
                if (aCentre)
                    for (size_t i = 0; i < n; ++i)
                        pushFanTri(o.tri, o.v, 0, (unsigned)(rimBase + i), (unsigned)(rimBase + (i + 1) % n));
                else
                    for (size_t i = 1; i + 1 < n; ++i)
                        pushFanTri(o.tri, o.v, 0, (unsigned)i, (unsigned)(i + 1));
            }
            computeObjectDerived(o);
            if (ferme && o.tri.empty())
                o.edges.emplace_back((int)(n - 1 + rimBase), (int)rimBase);
            return;
        }
        /* orientation du profil : fan de reference -> windings des murs et caps
           (normales EXTERIEURES, volume signe positif, quel que soit le sens
           du profil ou le signe du vecteur) */
        Vec3 nProf;
        double bestA = 0.0;
        for (size_t i = 1; i + 1 < n; ++i) {
            const Vec3 c3 = cross(ch[i] - ch[0], ch[i + 1] - ch[0]);
            const double a = c3.norm();
            if (a > bestA) { bestA = a; nProf = c3; }
        }
        const bool flip = dot(nProf, h) > 0.0;
        const size_t nseg = ferme ? n : n - 1;          /* ferme : cloture incluse */
        for (size_t i = 0; i < nseg; ++i) {
            const unsigned bi = (unsigned)(rimBase + i), bj = (unsigned)(rimBase + (i + 1) % n);
            const unsigned ti = (unsigned)(nBase + rimBase + i), tj = (unsigned)(nBase + rimBase + (i + 1) % n);
            if (flip) {
                o.tri.push_back(bi); o.tri.push_back(bj); o.tri.push_back(tj);
                o.tri.push_back(bi); o.tri.push_back(tj); o.tri.push_back(ti);
            } else {
                o.tri.push_back(bi); o.tri.push_back(ti); o.tri.push_back(tj);
                o.tri.push_back(bi); o.tri.push_back(tj); o.tri.push_back(bj);
            }
        }
        if (plein) {
            if (aCentre) {      /* caps en fan CENTRE (0 = bas, nBase = haut) */
                for (size_t i = 0; i < n; ++i) {
                    const unsigned r0 = (unsigned)(rimBase + i), r1 = (unsigned)(rimBase + (i + 1) % n);
                    if (flip) {
                        pushFanTri(o.tri, o.v, (unsigned)nBase, (unsigned)(nBase + r0), (unsigned)(nBase + r1));
                        pushFanTri(o.tri, o.v, 0, r1, r0);
                    } else {
                        pushFanTri(o.tri, o.v, (unsigned)nBase, (unsigned)(nBase + r1), (unsigned)(nBase + r0));
                        pushFanTri(o.tri, o.v, 0, r0, r1);
                    }
                }
            } else {
                for (size_t i = 1; i + 1 < n; ++i) {
                    const unsigned a0 = (unsigned)i, a1 = (unsigned)(i + 1);
                    if (flip) {   /* cap haut = fan direct translate, cap bas = inverse */
                        pushFanTri(o.tri, o.v, (unsigned)nBase, (unsigned)(nBase + a0), (unsigned)(nBase + a1));
                        pushFanTri(o.tri, o.v, 0, a1, a0);
                    } else {
                        pushFanTri(o.tri, o.v, (unsigned)nBase, (unsigned)(nBase + a1), (unsigned)(nBase + a0));
                        pushFanTri(o.tri, o.v, 0, a0, a1);
                    }
                }
            }
        }
        computeObjectDerived(o);
        return;
    }
}
/* formes a points FIXES (pas d'ajout/suppression : les poignees se deplacent) */
static bool tracePointsFixes(const Object& o) {
    if (o.traceType == 8)                       /* E3 : selon le profil extrude */
        return o.traceProfil == 4 || o.traceProfil == 5 || o.traceProfil == 6;
    return o.traceType == 4 || o.traceType == 5 || o.traceType == 6;
}
/* minimum de points de definition par forme (garde de suppression/fermeture) */
static size_t traceMinPoints(const Object& o) {
    if (o.traceType == 7) return 3;             /* polygone : surface seule fermee */
    if (o.traceType == 8) {                     /* E3 : regles du profil extrude */
        if (o.traceProfil == 7) return 3;
        return o.traceFerme ? 3 : 2;
    }
    return o.traceFerme ? 3 : 2;
}
/* index d'insertion dans tracePts : apres l'intervalle le plus proche du clic
   (ligne = projection sur les segments ; spline = intervalle de discretisation
   le plus proche, kSplSub sous-segments par intervalle, cf. splSample) */
static int traceInsertIndex(const Object& o, const Vec3& pLocal) {
    const size_t n = o.tracePts.size();
    if (o.traceType == 8) {                     /* E3 : comme le profil extrude */
        if (o.traceProfil == 1 && o.traceFerme) {   /* ligne fermee : cloture incluse */
            double best = 1e300; int bi = 1;
            for (size_t i = 0; i < n; ++i) {
                const Vec3 a = o.tracePts[i], b = o.tracePts[(i + 1) % n];
                const Vec3 ab = b - a;
                const double l2 = dot(ab, ab);
                double t = l2 > 1e-18 ? dot(pLocal - a, ab) / l2 : 0.0;
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
                const double d = (a + ab * t - pLocal).norm();
                if (d < best) { best = d; bi = (int)i + 1; }
            }
            return bi;
        }
        if (o.traceProfil == 2 || o.traceProfil == 3) {   /* spline : echantillons */
            g_splPts = o.tracePts;
            const bool oldCtrl = g_splCtrl;
            g_splCtrl = (o.traceProfil == 3);
            const std::vector<Vec3> pts = splSample(o.traceFerme != 0);
            g_splCtrl = oldCtrl;
            g_splPts.clear();
            double best = 1e300; int bi = 1;
            for (size_t j = 0; j < pts.size(); ++j) {
                const double d = (pts[j] - pLocal).norm();
                if (d < best) { best = d; bi = (int)(j / kSplSub) + 1; }
            }
            if (bi < 1) bi = 1;
            if (bi > (int)n - 1) bi = (int)n - 1;
            return bi;
        }
        /* lignes ouvertes et polygones : branches 1 / 7 plus bas, memes regles */
    }
    if (o.traceType == 1) {
        double best = 1e300; int bi = 1;
        for (size_t i = 0; i + 1 < n; ++i) {
            const Vec3 a = o.tracePts[i], b = o.tracePts[i + 1];
            const Vec3 ab = b - a;
            const double l2 = dot(ab, ab);
            double t = l2 > 1e-18 ? dot(pLocal - a, ab) / l2 : 0.0;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            const double d = (a + ab * t - pLocal).norm();
            if (d < best) { best = d; bi = (int)i + 1; }
        }
        return bi;
    }
    if (o.traceType == 7) {                     /* polygone ferme : segments + cloture */
        double best = 1e300; int bi = 1;
        for (size_t i = 0; i < n; ++i) {
            const Vec3 a = o.tracePts[i], b = o.tracePts[(i + 1) % n];
            const Vec3 ab = b - a;
            const double l2 = dot(ab, ab);
            double t = l2 > 1e-18 ? dot(pLocal - a, ab) / l2 : 0.0;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            const double d = (a + ab * t - pLocal).norm();
            if (d < best) { best = d; bi = (int)i + 1; }   /* i = n-1 : fin du vecteur = avant le depart */
        }
        return bi;
    }
    double best = 1e300; int bi = 1;
    for (size_t j = 0; j < o.v.size(); ++j) {
        const double d = (o.v[j] - pLocal).norm();
        if (d < best) { best = d; bi = (int)(j / kSplSub) + 1; }
    }
    if (bi < 1) bi = 1;
    if (bi > (int)n - 1) bi = (int)n - 1;
    return bi;
}
/* B3.3 : formes de sous-type rect/cercle/arc = points fixes (profil) */
static bool sweepProfilFixes(const Object& o) {
    return o.traceProfil == 4 || o.traceProfil == 5 || o.traceProfil == 6;
}
/* B3.3 : idem pour le chemin du balayage */
static bool sweepCheminFixes(const Object& o) {
    return o.traceCheminType == 4 || o.traceCheminType == 5 || o.traceCheminType == 6;
}
/* B3.3 : plan du profil d'un balayage (centroide, normale traceVec = roll) */
static bool sweepProfilPlan(const Object& o, Vec3& cPt, Vec3& n) {
    if (o.tracePts.empty()) return false;
    Vec3 cG{0, 0, 0};
    for (const Vec3& q : o.tracePts) cG = cG + q;
    cG = cG * (1.0 / (double)o.tracePts.size());
    n = (o.traceVec.norm() > 1e-12) ? o.traceVec * (1.0 / o.traceVec.norm()) : Vec3(0.0, 1.0, 0.0);
    cPt = o.pos + cG * o.scl;
    return true;
}
/* B3.3 : index d'insertion sur une chaine (ouverte ou fermee) au point le
   plus proche ; tol < 0 = SANS garde (le point vient d'un clic sur le
   solide), sinon refus si le point est trop loin de la chaine */
static int sweepChainInsert(const std::vector<Vec3>& pts, bool ferme, const Vec3& p, double tol) {
    const size_t n = pts.size();
    if (n < 2) return -1;
    double best = 1e300; int bi = -1;
    const size_t nseg = ferme ? n : n - 1;
    for (size_t i = 0; i < nseg; ++i) {
        const Vec3 a = pts[i], b = pts[(i + 1) % n];
        const Vec3 ab = b - a;
        const double l2 = dot(ab, ab);
        double t = l2 > 1e-18 ? dot(p - a, ab) / l2 : 0.0;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        const double d = (a + ab * t - p).norm();
        if (d < best) { best = d; bi = (int)i + 1; }
    }
    if (bi < 1) bi = 1;
    if (bi > (int)n - 1) bi = (int)n - 1;
    return (tol < 0.0 || best <= tol) ? bi : -1;
}
/* clic sur la courbe = ajouter un point de definition ; retourne l'index
   insere (-1 = refuse : doublon ou trace inedit) */
static int traceAddPoint(Object& o, const Vec3& monde, bool gardeProfil = false) {
    if (o.traceType == 0 || o.tracePts.size() < 2) return -1;
    if (tracePointsFixes(o)) {
        userMsg("edition : cette forme a des points fixes (rectangle, cercle, arc) - deplacez les poignees");
        return -1;
    }
    if (o.traceType == 9) {                     /* B3.3 : edition du balayage */
        const Vec3 pLocal = (monde - o.pos) * (1.0 / o.scl);
        if (g_sweepEditChemin) {
            if (sweepCheminFixes(o)) {
                userMsg("edition : le chemin a des points fixes (rect, cercle, arc) - deplacez les poignees");
                return -1;
            }
            for (const Vec3& q : o.traceChemin)
                if ((q - pLocal).norm() < 1e-9) return -1;      /* meme point : rien */
            const int idx = sweepChainInsert(o.traceChemin, o.traceCheminFerme != 0, pLocal,
                                             gardeProfil ? filPickTol() : -1.0);
            if (idx < 0) { userMsg("edition : cliquez SUR le tube ou PRES du chemin pour ajouter un point"); return -1; }
            undoSnapshot("edition : point ajoute");
            o.traceChemin.insert(o.traceChemin.begin() + idx, pLocal);
            traceRegen(o);
            return (int)o.tracePts.size() + idx;
        }
        if (sweepProfilFixes(o)) {
            userMsg("edition : le profil a des points fixes (rect, cercle, arc) - deplacez les poignees");
            return -1;
        }
        Vec3 cPt, nn;
        Vec3 proj = pLocal;                     /* projection sur le plan du profil */
        if (sweepProfilPlan(o, cPt, nn))
            proj = pLocal - nn * dot(pLocal - (cPt - o.pos) * (1.0 / o.scl), nn);
        for (const Vec3& q : o.tracePts)
            if ((q - proj).norm() < 1e-9) return -1;            /* meme point : rien */
        const int idx = sweepChainInsert(o.tracePts, o.traceFerme != 0, proj, filPickTol());
        if (idx < 0) { userMsg("edition : cliquez PRES du profil pour ajouter un point"); return -1; }
        undoSnapshot("edition : point ajoute");
        o.tracePts.insert(o.tracePts.begin() + idx, proj);
        traceRegen(o);
        return idx;
    }
    const Vec3 pLocal = (monde - o.pos) * (1.0 / o.scl);
    /* E3 (retour test) : sur une extrusion, un clic-sol LOIN du profil ne doit
       rien ajouter (fini les points parasites sur les murs) */
    if (gardeProfil && o.traceType == 8
        && traceProfilDist(o, Vec3(pLocal.x, 0.0, pLocal.z)) > filPickTol()) {
        userMsg("edition : cliquez sur une face du solide ou pres du profil pour ajouter un point");
        return -1;
    }
    for (const Vec3& q : o.tracePts)
        if ((q - pLocal).norm() < 1e-9) return -1;      /* meme point : rien */
    const int idx = traceInsertIndex(o, pLocal);
    undoSnapshot("edition : point ajoute");
    o.tracePts.insert(o.tracePts.begin() + idx, pLocal);
    traceRegen(o);
    return idx;
}
/* poignee + Suppr = supprimer (garde : minimum par forme) ; B3.3 : sur un
   balayage, idx >= npts designe une poignee du CHEMIN (npts = nombre de
   points du profil) */
static bool traceDelPoint(Object& o, int idx) {
    if (o.traceType == 0 || idx < 0) return false;
    if (o.traceType != 9 && idx >= (int)o.tracePts.size()) return false;
    if (tracePointsFixes(o)) {
        userMsg("edition : cette forme a des points fixes (rectangle, cercle, arc) - deplacez les poignees");
        return false;
    }
    if (o.traceType == 9) {                     /* B3.3 : edition du balayage */
        const size_t npts = o.tracePts.size();
        if ((size_t)idx >= npts) {              /* poignee du CHEMIN */
            const size_t ic = (size_t)idx - npts;
            if (ic >= o.traceChemin.size()) return false;
            if (sweepCheminFixes(o)) {
                userMsg("edition : le chemin a des points fixes (rect, cercle, arc) - deplacez les poignees");
                return false;
            }
            if (o.traceChemin.size() <= (o.traceCheminFerme ? (size_t)3 : (size_t)2)) {
                userMsg("edition : minimum %zu point(s) pour le chemin du balayage",
                            (size_t)(o.traceCheminFerme ? 3 : 2));
                return false;
            }
            undoSnapshot("edition : point supprime");
            o.traceChemin.erase(o.traceChemin.begin() + ic);
            if (g_editSel == idx) g_editSel = -1;
            traceRegen(o);
            return true;
        }
        if (sweepProfilFixes(o)) {
            userMsg("edition : le profil a des points fixes (rect, cercle, arc) - deplacez les poignees");
            return false;
        }
        const size_t mini = (o.traceProfil == 7) ? (size_t)3 : (o.traceFerme ? (size_t)3 : (size_t)2);
        if (o.tracePts.size() <= mini) {
            userMsg("edition : minimum %zu point(s) pour le profil", mini);
            return false;
        }
        undoSnapshot("edition : point supprime");
        o.tracePts.erase(o.tracePts.begin() + idx);
        if (g_editSel == idx) g_editSel = -1;
        traceRegen(o);
        return true;
    }
    if (o.tracePts.size() <= traceMinPoints(o)) {
        userMsg("edition : minimum %zu point(s) pour ce trace", traceMinPoints(o));
        return false;
    }
    undoSnapshot("edition : point supprime");
    o.tracePts.erase(o.tracePts.begin() + idx);
    if (g_editSel == idx) g_editSel = -1;
    traceRegen(o);
    return true;
}
/* O pendant l'edition : bascule ouvert/ferme (feuille <-> fil pour la ligne) */
static void traceToggleFerme(Object& o) {
    if (o.traceType == 0) return;
    if (o.traceType == 8) {                     /* E3 : bascule du profil extrude */
        if (o.traceProfil != 1 && o.traceProfil != 2 && o.traceProfil != 3) {
            userMsg("edition : le profil de cette extrusion est toujours ferme");
            return;
        }
        if (!o.traceFerme && o.tracePts.size() < 3) {
            userMsg("edition : au moins 3 points pour fermer le trace");
            return;
        }
        undoSnapshot("edition : ouvert/ferme");
        o.traceFerme = o.traceFerme ? 0 : 1;
        traceRegen(o);
        userMsg("edition : extrusion du profil %s", o.traceFerme ? "FERME" : "ouvert");
        return;
    }
    if (o.traceType == 9) {   /* B3.3/B3.6a/B3.6b : O = PROFIL -> CHEMIN -> ACCROCHE -> ROLL */
        if (g_sweepEditChemin) {
            g_sweepEditChemin = false;
            g_sweepEditAcc = true;
            userMsg("edition balayage : cible ACCROCHE - cliquez un coin, un milieu de segment (ou le centre du cercle) = point colle au fil");
        } else if (g_sweepEditAcc) {
            g_sweepEditAcc = false;
            g_sweepEditRoll = true;
            traceRegen(o);      /* la regen repose le repere du roll (UN SEUL CHEMIN DE CODE) */
            userMsg("edition balayage : cible ROLL - glissez la poignee violette pour tourner le profil autour du fil");
        } else if (g_sweepEditRoll) {
            g_sweepEditRoll = false;
            userMsg("edition balayage : ajouts/suppressions sur le PROFIL");
        } else {
            g_sweepEditChemin = true;
            userMsg("edition balayage : ajouts/suppressions sur le CHEMIN");
        }
        return;
    }
    if (o.traceType != 1 && o.traceType != 2 && o.traceType != 3) {
        userMsg("edition : ouvert/ferme concerne la ligne et la spline");
        return;
    }
    if (!o.traceFerme && o.tracePts.size() < 3) {
        userMsg("edition : au moins 3 points pour fermer le trace");
        return;
    }
    undoSnapshot("edition : ouvert/ferme");
    o.traceFerme = o.traceFerme ? 0 : 1;
    traceRegen(o);
    userMsg("edition : trace %s", o.traceFerme ? "FERME" : "ouvert");
}
/* Maj+S / Maj+B pendant l'edition d'une spline : bascule variante a chaud
   (regle des variantes : points conserves) */
static void traceSetVariante(Object& o, bool ctrl) {
    if (o.traceType == 8 && (o.traceProfil == 2 || o.traceProfil == 3)) {
        undoSnapshot("edition : variante");
        o.traceProfil = ctrl ? 3 : 2;           /* E3 : variante du profil, points conserves */
        traceRegen(o);
        userMsg("edition : profil spline %s (points conserves)", ctrl ? "POINTS DE CONTROLE" : "passage");
        return;
    }
    if (o.traceType != 2 && o.traceType != 3) {
        userMsg("edition : la bascule passage/controle concerne les splines");
        return;
    }
    undoSnapshot("edition : variante");
    o.traceType = ctrl ? 3 : 2;
    traceRegen(o);
    userMsg("edition : variante spline %s (points conserves)", ctrl ? "POINTS DE CONTROLE" : "passage");
}
static void editExit() {
    g_editMode = false;
    g_editObj = -1;
    g_editGrab = -1;
    g_editSel = -1;
    g_sweepEditAcc = false;                 /* B3.6a */
    g_sweepEditRoll = false;                /* B3.6b */
    g_rollDrag = false;                     /* B3.6b */
    userMsg("mode selection");
}
/* B3.3c (retour test) : en edition, un clic qui ne saisit aucune poignee et
   n'ajoute aucun point = ORBITE - tourner autour de l'objet pendant
   l'edition (pan molette et zoom deja actifs) ;
   B3.6a-ter (retour test : maj + clic G = pan demande) : Maj tenue au clic
   = PAN (meme regle que la selection : deplacer la vue), sans Maj = orbite */
static void editOrbit(GLFWwindow* w, bool maj) {
    if (maj) {
        g_mode = M_Pan;
        flashBand("pan : deplacez la vue ; relachez pour poser");
    } else {
        g_mode = M_Orbit;
    }
    glfwGetCursorPos(w, &g_prevX, &g_prevY);
}
/* clic gauche en edition : saisir une poignee (drag) ou ajouter un point ;
   clic sans effet (vide, ciel, refuse) = orbite (B3.3c) */
static void editClick(GLFWwindow* w, int mods) {
    const bool majClic = (mods & GLFW_MOD_SHIFT) != 0;   /* B3.6a-ter */
    if (g_editObj < 0 || g_editObj >= (int)g_objs.size()) { editExit(); return; }
    Object& o = g_objs[(size_t)g_editObj];
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return;
    /* B3.6b : cible ROLL - clic sur la poignee violette = drag de rotation
       du profil autour du fil (axe = tangente au depart, pose par la regen) */
    if (o.traceType == 9 && g_sweepEditRoll) {
        const double dR = pointRayDist(org, dir, g_sweepRollHandle);
        if (dR <= filPickTol() * 2.0) {
            const Vec3 rh = g_sweepRollHandle - g_sweepRollCenter;
            Vec3 rv = rh - g_sweepRollAxis * dot(rh, g_sweepRollAxis);
            if (rv.norm() > 1e-9) {
                undoOuvre("edition : roll");       /* B3.6z : capture au debut du drag */
                g_rollV0 = rv * (1.0 / rv.norm());
                g_rollAng0 = o.traceRoll;
                g_rollDrag = true;
            }
            return;
        }
        editOrbit(w, majClic);                  /* B3.3c/B3.6a-ter : clic sans effet */
        return;
    }
    /* B3.6a : cible ACCROCHE - clic sur une poignee orange (sommet, milieu de
       segment, centre du cercle) = elle devient le point de connexion au fil */
    if (o.traceType == 9 && g_sweepEditAcc) {
        std::vector<Vec3> acc;
        if (!sweepAccroches(o, acc) || acc.empty()) return;
        double bestA = 1e300; int ai = -1;
        for (size_t j = 0; j < acc.size(); ++j) {
            const Vec3 wpt = o.pos + acc[j] * o.scl;
            const double d = pointRayDist(org, dir, wpt);
            if (d < bestA) { bestA = d; ai = (int)j; }
        }
        if (ai >= 0 && bestA <= filPickTol() * 2.0) {
            undoSnapshot("edition : accroche");
            o.traceAccroche = ai;
            traceRegen(o);
            userMsg("edition balayage : l accroche %d/%d est collee au fil",
                        ai + 1, (int)acc.size());
        } else {
            editOrbit(w, majClic);                       /* B3.3c : clic sans effet = orbite */
        }
        return;
    }
    double best = 1e300; int bi = -1;
    for (size_t i = 0; i < o.tracePts.size(); ++i) {
        const Vec3 wpt = o.pos + o.tracePts[i] * o.scl;
        const double d = pointRayDist(org, dir, wpt);
        if (d < best) { best = d; bi = (int)i; }
    }
    /* B3.3 : poignees du CHEMIN d'un balayage (index = npts + i) */
    if (o.traceType == 9)
        for (size_t i = 0; i < o.traceChemin.size(); ++i) {
            const Vec3 wpt = o.pos + o.traceChemin[i] * o.scl;
            const double d = pointRayDist(org, dir, wpt);
            if (d < best) { best = d; bi = (int)(o.tracePts.size() + i); }
        }
    /* E3 : la poignee de HAUTEUR (index = npts) au centre du cap superieur */
    Vec3 cG{0, 0, 0};
    if (o.traceType == 8 && !o.tracePts.empty()) {
        for (const Vec3& p : o.tracePts) cG = cG + p;
        cG = cG * (1.0 / (double)o.tracePts.size());
        const Vec3 wpt = o.pos + (cG + o.traceVec) * o.scl;
        const double d = pointRayDist(org, dir, wpt);
        if (d < best) { best = d; bi = (int)o.tracePts.size(); }
    }
    if (bi >= 0 && best <= filPickTol() * 2.0) {
        g_editSel = bi;
        undoOuvre("edition : poignee deplacee");    /* B3.6z : capture au grab */
        g_editGrab = bi;                            /* drag jusqu'au relachement */
        if (o.traceType == 8 && bi == (int)o.tracePts.size()) {
            /* plan VERTICAL face camera, passant par le cap superieur :
               le drag vertical pilote la hauteur sans dependre du sol */
            Vec3 fwd = g_cam.target - g_cam.pos();
            fwd.y = 0.0;
            if (fwd.norm() > 1e-9) {
                fwd = fwd * (1.0 / fwd.norm());
                g_editHN = Vec3(-fwd.z, 0.0, fwd.x);   /* perpendiculaire, horizontale */
            } else {
                g_editHN = Vec3(1.0, 0.0, 0.0);
            }
            g_editHPt = o.pos + (cG + o.traceVec) * o.scl;
            g_editHDir = (o.traceVec.norm() > 1e-9) ? o.traceVec * (1.0 / o.traceVec.norm())
                                                    : Vec3(0.0, 1.0, 0.0);
            g_editH0 = o.traceVec.norm();
        }
        return;
    }
    if (o.traceType == 9 && g_sweepEditChemin) {    /* B3.3b : clic sur le tube = nouvelle station */
        double tHit;
        if (rayObjectTri(org, dir, o, tHit, nullptr)) {
            const Vec3 hw = org + dir * tHit;
            const int idx = traceAddPoint(o, hw, false);   /* sans garde : station la plus proche */
            if (idx >= 0) {
                g_editSel = idx;
                userMsg("edition : point ajoute au chemin (%zu au total)", o.traceChemin.size());
            }
            return;
        }
        /* pas sur le solide : clic-sol pres du chemin (regle existante ci-dessous) */
    }
    if (o.traceType == 9 && !g_sweepEditChemin) {   /* B3.3 : ajout dans le plan du profil */
        Vec3 cPt, nn;
        if (!sweepProfilPlan(o, cPt, nn)) return;
        const double denom = dot(dir, nn);
        if (std::fabs(denom) < 1e-9) { editOrbit(w, majClic); return; }
        const double t = dot(cPt - org, nn) / denom;
        if (t <= 0.0) { editOrbit(w, majClic); return; }
        const int idx = traceAddPoint(o, org + dir * t, false);
        if (idx >= 0) {
            g_editSel = idx;
            userMsg("edition : point ajoute au profil (%zu au total)", o.tracePts.size());
        } else {
            editOrbit(w, majClic);
        }
        return;
    }
    bool ok; Vec3 p;
    bool surSolide = false;
    if (o.traceType == 8) {
        /* E3 (retour test) : un clic sur le SOLIDE lui-meme est prioritaire -
           le point de profil = projection VERTICALE du point touche (les murs
           sont verticaux, la projection au sol est exacte) ; sinon clic-sol
           avec garde de distance (traceAddPoint, gardeProfil = true) */
        double tHit;
        if (rayObjectTri(org, dir, o, tHit, nullptr)) {
            const Vec3 hw = org + dir * tHit;
            p = Vec3(hw.x, 0.0, hw.z);
            surSolide = true;
        } else {
            lineGroundPoint(w, ok, p);
            if (!ok) { editOrbit(w, majClic); return; }      /* pointe le ciel : orbite */
        }
    } else {
        /* B3.6a-bis : le point s'ajoute dans le plan HORIZONTAL passant par
           le point de trace (ou de chemin) LE PLUS PROCHE du rayon -
           l'altitude suit la courbe ; au sol, plan y = 0 = identique a avant */
        double yPlan = 0.0;
        if (bi >= 0 && bi < (int)o.tracePts.size())
            yPlan = o.pos.y + o.tracePts[(size_t)bi].y * o.scl;
        else if (o.traceType == 9 && bi >= (int)o.tracePts.size()) {
            const size_t ic = (size_t)bi - o.tracePts.size();
            if (ic < o.traceChemin.size()) yPlan = o.pos.y + o.traceChemin[ic].y * o.scl;
        }
        linePointY(w, yPlan, ok, p);
        if (!ok) { editOrbit(w, majClic); return; }          /* pointe le ciel : orbite */
    }
    const int idx = traceAddPoint(o, p, (o.traceType == 8 && !surSolide) || o.traceType == 9);
    if (idx >= 0) {
        g_editSel = idx;
        if (o.traceType == 9 && g_sweepEditChemin)
            userMsg("edition : point ajoute au chemin (%zu au total)", o.traceChemin.size());
        else
            userMsg("edition : point ajoute (%zu au total)", o.tracePts.size());
    } else {
        editOrbit(w, majClic);
    }
}
/* drag d'une poignee au sol (recalcul CONTINU de la courbe) */
static void editDrag(GLFWwindow* w) {
    if (g_editObj < 0 || g_editObj >= (int)g_objs.size()) { g_editGrab = -1; return; }
    Object& o = g_objs[(size_t)g_editObj];
    /* B3.6a-bis : le drag se fait dans le plan HORIZONTAL passant par la
       poignee saisie (altitude conservee ; au sol y = 0, identique a avant) */
    double yPlan = 0.0;
    if (g_editGrab >= 0) {
        const int npts = (int)o.tracePts.size();
        if (o.traceType == 9 && g_editGrab >= npts) {
            const size_t ic = (size_t)g_editGrab - (size_t)npts;
            if (ic < o.traceChemin.size()) yPlan = o.pos.y + o.traceChemin[ic].y * o.scl;
        } else if (g_editGrab < npts) {
            yPlan = o.pos.y + o.tracePts[(size_t)g_editGrab].y * o.scl;
        }
    }
    bool ok; Vec3 p;
    linePointY(w, yPlan, ok, p);
    if (!ok) return;
    if (o.traceType == 9) {                     /* B3.3 : drag d'une poignee du balayage */
        if (g_editGrab >= (int)o.tracePts.size()) {          /* chemin : drag au sol */
            const size_t ic = (size_t)g_editGrab - o.tracePts.size();
            if (ic >= o.traceChemin.size()) return;
            const Vec3 oldPt = o.traceChemin[ic];
            o.traceChemin[ic] = (p - o.pos) * (1.0 / o.scl);
            if ((o.traceChemin[ic] - oldPt).norm() > 1e-12) g_undoSale = true;   /* B3.6z */
        } else if (g_editGrab >= 0) {                        /* profil : drag DANS son plan */
            Vec3 org, dir;
            if (!rayFromMouse(w, org, dir)) return;
            Vec3 cPt, nn;
            if (!sweepProfilPlan(o, cPt, nn)) return;
            const double denom = dot(dir, nn);
            if (std::fabs(denom) < 1e-9) return;
            const double t = dot(cPt - org, nn) / denom;
            const Vec3 oldPt = o.tracePts[(size_t)g_editGrab];
            o.tracePts[(size_t)g_editGrab] = (org + dir * t - o.pos) * (1.0 / o.scl);
            if ((o.tracePts[(size_t)g_editGrab] - oldPt).norm() > 1e-12) g_undoSale = true;   /* B3.6z */
        } else {
            return;
        }
        traceRegen(o);
        return;
    }
    /* E3 : poignee de HAUTEUR (index = npts) - drag dans le plan vertical
       capte a la saisie ; la longueur suit le curseur, la direction garde */
    if (o.traceType == 8 && g_editGrab == (int)o.tracePts.size()) {
        Vec3 org, dir;
        if (!rayFromMouse(w, org, dir)) return;
        const double denom = dot(dir, g_editHN);
        if (std::fabs(denom) < 1e-9) return;
        const double t = dot(g_editHPt - org, g_editHN) / denom;
        const Vec3 p2 = org + dir * t;
        const double len = g_editH0 + dot(p2 - g_editHPt, g_editHDir) / o.scl;
        if ((g_editHDir * len - o.traceVec).norm() > 1e-12) g_undoSale = true;   /* B3.6z */
        o.traceVec = g_editHDir * len;
        traceRegen(o);
        return;
    }
    const Vec3 oldPt = o.tracePts[(size_t)g_editGrab];
    o.tracePts[(size_t)g_editGrab] = (p - o.pos) * (1.0 / o.scl);
    if ((o.tracePts[(size_t)g_editGrab] - oldPt).norm() > 1e-12) g_undoSale = true;   /* B3.6z */
    traceRegen(o);
}
/* B3.6b : angle (DEGRES) entre deux directions autour d'un axe - helper
   isole et testable, le drag de roll ne fait que le consommer */
static double rollDelta(const Vec3& v0, const Vec3& v, const Vec3& axe) {
    return std::atan2(dot(cross(v0, v), axe), dot(v0, v)) * (180.0 / 3.14159265358979);
}
static double rollWrap(double a) {          /* B3.6b : wrap dans [-180, 180[ */
    a = std::fmod(a + 180.0, 360.0);
    if (a < 0.0) a += 360.0;
    return a - 180.0;
}
/* B3.6b : drag de la poignee de ROLL - le point du rayon le plus proche de
   l'AXE (droite par g_sweepRollCenter) donne la direction tournee ; le
   delta d'angle s'ajoute au roll de depart, regen continue */
static void rollDragUpdate(GLFWwindow* w) {
    if (g_editObj < 0 || g_editObj >= (int)g_objs.size()) { g_rollDrag = false; return; }
    Object& o = g_objs[(size_t)g_editObj];
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return;
    const Vec3& T = g_sweepRollAxis;
    const Vec3& C = g_sweepRollCenter;
    const Vec3 w0 = C - org;
    const double dT = dot(dir, T);
    const double den = 1.0 - dT * dT;
    if (den < 1e-9) return;                     /* rayon presque parallele a l'axe */
    const double t = (dot(w0, dir) - dot(w0, T) * dT) / den;
    if (t <= 0.0) return;                       /* point derriere la camera */
    const Vec3 p = org + dir * t;
    Vec3 v = (p - C) - T * dot(p - C, T);
    if (v.norm() < 1e-9) return;
    v = v * (1.0 / v.norm());
    o.traceRoll = rollWrap(g_rollAng0 + rollDelta(g_rollV0, v, T));
    if (std::fabs(o.traceRoll - g_rollAng0) > 1e-12) g_undoSale = true;   /* B3.6z */
    traceRegen(o);
}
/* poignees dessinees : petits carres au sol sur les points de definition */
static void drawEditHandles() {
    if (!g_editMode || g_editObj < 0 || g_editObj >= (int)g_objs.size()) return;
    const Object& o = g_objs[(size_t)g_editObj];
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    const double s = 0.07;
    const bool accM = (o.traceType == 9 && g_sweepEditAcc);   /* B3.6a */
    const bool rollM = (o.traceType == 9 && g_sweepEditRoll); /* B3.6b */
    if (!accM && !rollM)
    for (size_t i = 0; i < o.tracePts.size(); ++i) {
        const Vec3 w = o.pos + o.tracePts[i] * o.scl;
        if ((int)i == g_editSel) glColor3d(0.95, 0.5, 0.2);       /* selectionnee : orange */
        else if ((int)i == g_editGrab) glColor3d(0.95, 0.75, 0.1);
        else glColor3d(0.2, 0.85, 0.2);                            /* verte */
        glBegin(GL_QUADS);
        glVertex3d(w.x - s, w.y, w.z - s);
        glVertex3d(w.x + s, w.y, w.z - s);
        glVertex3d(w.x + s, w.y, w.z + s);
        glVertex3d(w.x - s, w.y, w.z + s);
        glEnd();
    }
    /* B3.3b : le chemin du balayage dessine EN EDITION (cyan, visible a
       travers le solide : il est enterre dans le tube, invisible sinon) */
    if (o.traceType == 9 && o.traceChemin.size() >= 2) {
        glColor3d(0.2, 0.7, 0.95);
        glLineWidth(2.0);
        glBegin(o.traceCheminFerme ? GL_LINE_LOOP : GL_LINE_STRIP);
        for (const Vec3& q : o.traceChemin) {
            const Vec3 w3 = o.pos + q * o.scl;
            glVertex3d(w3.x, w3.y, w3.z);
        }
        glEnd();
    }
    /* B3.3 : poignees du CHEMIN d'un balayage (cyans) - index = npts + i */
    if (o.traceType == 9 && !g_sweepEditAcc && !rollM) {
        for (size_t i = 0; i < o.traceChemin.size(); ++i) {
            const Vec3 w2 = o.pos + o.traceChemin[i] * o.scl;
            const int idx = (int)(o.tracePts.size() + i);
            if (idx == g_editSel) glColor3d(0.95, 0.5, 0.2);
            else if (idx == g_editGrab) glColor3d(0.95, 0.75, 0.1);
            else glColor3d(0.2, 0.7, 0.95);
            glBegin(GL_QUADS);
            glVertex3d(w2.x - s, w2.y, w2.z - s);
            glVertex3d(w2.x + s, w2.y, w2.z - s);
            glVertex3d(w2.x + s, w2.y, w2.z + s);
            glVertex3d(w2.x - s, w2.y, w2.z + s);
            glEnd();
        }
    }
    /* B3.6a : poignees d'ACCROCHE (orange) - sommets + milieux du profil
       (+ centre du cercle) ; l'accroche collee au fil est dessinee plus grande */
    if (accM) {
        std::vector<Vec3> acc;
        if (sweepAccroches(o, acc))
            for (size_t j = 0; j < acc.size(); ++j) {
                const Vec3 w2 = o.pos + acc[j] * o.scl;
                const double s2 = ((int)j == o.traceAccroche) ? s * 1.8 : s;
                glColor3d(1.0, 0.55, 0.1);
                glBegin(GL_QUADS);
                glVertex3d(w2.x - s2, w2.y, w2.z - s2);
                glVertex3d(w2.x + s2, w2.y, w2.z - s2);
                glVertex3d(w2.x + s2, w2.y, w2.z + s2);
                glVertex3d(w2.x - s2, w2.y, w2.z + s2);
                glEnd();
            }
    }
    /* B3.6b : poignee de ROLL (violette) - le profil tourne autour du fil */
    if (rollM) {
        glColor3d(0.75, 0.3, 0.95);
        glBegin(GL_QUADS);
        glVertex3d(g_sweepRollHandle.x - s, g_sweepRollHandle.y, g_sweepRollHandle.z - s);
        glVertex3d(g_sweepRollHandle.x + s, g_sweepRollHandle.y, g_sweepRollHandle.z - s);
        glVertex3d(g_sweepRollHandle.x + s, g_sweepRollHandle.y, g_sweepRollHandle.z + s);
        glVertex3d(g_sweepRollHandle.x - s, g_sweepRollHandle.y, g_sweepRollHandle.z + s);
        glEnd();
    }
    /* E3 : poignee de HAUTEUR (bleue) au centre du cap superieur */
    if (o.traceType == 8 && !o.tracePts.empty()) {
        Vec3 cG{0, 0, 0};
        for (const Vec3& p : o.tracePts) cG = cG + p;
        cG = cG * (1.0 / (double)o.tracePts.size());
        const Vec3 wt = o.pos + (cG + o.traceVec) * o.scl;
        const int idx = (int)o.tracePts.size();
        if (idx == g_editSel) glColor3d(0.95, 0.5, 0.2);
        else if (idx == g_editGrab) glColor3d(0.95, 0.75, 0.1);
        else glColor3d(0.2, 0.7, 0.95);                            /* bleue = hauteur */
        glBegin(GL_QUADS);
        glVertex3d(wt.x - s, wt.y, wt.z - s);
        glVertex3d(wt.x + s, wt.y, wt.z - s);
        glVertex3d(wt.x + s, wt.y, wt.z + s);
        glVertex3d(wt.x - s, wt.y, wt.z + s);
        glEnd();
    }
    glEnable(GL_DEPTH_TEST);
}

/* ------------------------------------------------------------------ */
/* Callbacks                                                           */
/* ------------------------------------------------------------------ */

static void onScroll(GLFWwindow*, double, double dy) {
    /* B3.6a-bis (retour test : "zoom toujours tres insuffisant") : pas par
       cran porte de 11 % (0.9) a 20 % (0.8) - 100 fois plus pres en 20 crans
       au lieu de 44 ; les bornes ne changent pas */
    g_cam.dist *= std::pow(0.8, dy);
    /* B3.3e : niveaux de zoom DEFINIS - tres pres (0.02) a tres loin (4000,
       plan lointain 5000) ; B3.6a-ter (retour test : borne pas assez
       petite) : le rapprochement passe de 0.1 a 0.02 (5 fois plus pres) */
    if (g_cam.dist < 0.02) g_cam.dist = 0.02;
    if (g_cam.dist > 4000.0) g_cam.dist = 4000.0;
}

static void onMouseButton(GLFWwindow* w, int button, int action, int mods) {
    /* B3.3h (retour test) : un clic DANS le panneau journal est avale
       (il ne doit pas etre interprete comme un clic dans la scene) */
    if (g_journalVisible && !g_journal.empty()) {
        float xj, yj, wj, hj; size_t debut;
        journalRect(xj, yj, wj, hj, debut, 12);
        double mx, my;
        glfwGetCursorPos(w, &mx, &my);
        const float fy = (float)g_winH - (float)my;   /* GLFW : origine en haut */
        if ((float)mx >= xj && (float)mx <= xj + wj && fy >= yj && fy <= yj + hj) return;
    }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) return;   /* B3.3e : pan = Maj + clic droit (UN seul moyen de se deplacer) */
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        g_rightDown = (action == GLFW_PRESS);   /* B3.6a-bis : etat du clic droit */
        /* B3.3e (retour test) : Maj + clic droit (glisser) = deplacer la vue
           (pan), DANS TOUS les modes (edition, creation, orbite...) - le meme
           geste partout, on ne change pas de moyen de deplacement en cours
           d'execution. Sans Maj : regle generale des transformations, clic
           droit = retour au neutre (le deplacement en vol ne peut pas etre
           annule ainsi : le clic droit exige de relacher le bouton gauche). */
        if (action == GLFW_PRESS && (mods & GLFW_MOD_SHIFT)) {
            if (!g_ppActive && !g_rotActive
                && (g_mode == M_None || g_mode == M_Orbit || g_mode == M_Pan)) {
                g_mode = M_Pan;
                glfwGetCursorPos(w, &g_prevX, &g_prevY);
                /* B3.6a-bis : retour VISIBLE (diagnostic : si ce bandeau ne
                   s'affiche pas, le geste n'arrive pas au programme) */
                flashBand("pan : deplacez la vue ; relachez le clic droit pour poser");
            }
            return;
        }
        if (action != GLFW_PRESS) {
            if (g_mode == M_Pan) g_mode = M_None;   /* relachement : fin du pan */
            /* B3.6a-bis : la sortie d'edition passe AU RELACHEMENT - un clic
               droit presse sans Maj peut encore devenir un pan (Maj arrivee
               pendant le geste) ; sans pan, le relachement quitte l'edition
               (regle conservee, l'utilisateur ne voit aucune difference) */
            else if (g_editMode) editExit();
            return;
        }
        if (g_rectMode) { rectCancel("dessin annule"); return; }   /* B2.2 : clic droit = annule (2 clics suffisent) */
        if (g_cercleMode) { cercleCancel("dessin annule"); return; }   /* B2.3 : clic droit = annule */
        if (g_arcMode) { arcCancel("dessin annule"); return; }      /* B2.4 : clic droit = annule */
        if (g_polyMode) { polyFinish(); return; }   /* B2.6b : clic droit = valide (ferme la surface) */
        if (g_splMode) { splFinish(); return; }   /* B2.7 : clic droit = valide (courbe ouverte) */
        if (g_editMode) return;   /* B3.6a-bis : la sortie d'edition passe au relachement (le presse peut devenir un pan si Maj arrive) */
        if (g_sweepStage) { sweepCancel(); return; }   /* B3 : clic droit = annule le balayage */
        if (g_collStage) { collCancel("collage annule"); return; }   /* B3.7 : clic droit = annule */
        if (g_lineMode) { lineFinish(); return; }   /* B2.1 : le clic droit FINIT (segments deja cliques poses) */
        if (g_ppActive) {
            ppCancel();                 /* restaure la piece */
            g_ppMode = false;
            userMsg("mode selection");
        } else if (g_rotActive) {
            rotCancel();                /* restaure la geometrie */
            g_rotMode = false;
            userMsg("mode selection");
        } else if (g_mode == M_Drag) {
            g_mode = M_None;
            g_lockAxis = -1;
        } else if (g_ppMode || g_rotMode) {
            g_ppMode = false;
            g_rotMode = false;
            userMsg("mode selection");
        } else {
            showContextMenu(w);
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        if (g_collStage) { collClick(w); return; }   /* B3.7 : le clic pose la cible du collage */
        if (g_lineMode) { lineTakePoint(w); return; }   /* B2.1 : chaque clic pose un sommet */
        if (g_rectMode) { rectTakePoint(w); return; }   /* B2.2 : clic 1 = coin, clic 2 = coin oppose pose */
        if (g_cercleMode) { cercleTakePoint(w); return; }   /* B2.3 : clic 1 = centre, clic 2 = rayon pose */
        if (g_arcMode) { arcTakePoint(w); return; }         /* B2.4 : clic depart, clic fin, clic bombement pose */
        if (g_polyMode) { polyTakePoint(w); return; }       /* B2.6 : chaque clic pose un sommet ; clic sur le depart = ferme */
        if (g_splMode) { splTakePoint(w); return; }       /* B2.7 : chaque clic pose un point ; clic sur le depart = boucle */
        if (g_sweepStage) { sweepClick(w); return; }   /* B3 : clic 1 = la face, clic 2 = le fil */
        if (g_editMode) { editClick(w, mods); return; }   /* B2.7e : poignee = drag ; clic utile = ajouter ; sans effet = orbite/pan (B3.3c, B3.6a-ter) */   /* B2.7e : poignee = drag ; clic utile = ajouter ; sans effet = orbite (B3.3c) */
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
                userMsg("Face non plane — Push/Pull indisponible sur cette surface.");
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
            undoOuvre("extrusion (push/pull)");     /* B3.6z : capture AVANT toute mutation */
            g_ppActive = true;
            userMsg("face capturee — deplacez le long de la normale, cliquez pour poser la face (clic droit ou Echap : annuler ; chiffres + Entree : dimension exacte)");
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
                    userMsg("selection : %s retire (%zu objet(s) selectionnes)", g_objs[(size_t)hit].name.c_str(), tot);
                else
                    userMsg("selection : %s ajoute (%zu objet(s) selectionnes)", g_objs[(size_t)hit].name.c_str(), tot);
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
                    userMsg("selection : %s (%zu tris)", o.name.c_str(), o.tri.size() / 3);
                } else {
                    userMsg("deselection");
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
            flashBand("pan : deplacez la vue ; relachez pour poser");   /* B3.6a-ter : retour visible */
        } else {
            g_mode = M_Orbit;
        }
        glfwGetCursorPos(w, &g_prevX, &g_prevY);
    } else {
        if (g_mode == M_Drag) { g_lockAxis = -1; g_dragMulti.clear(); }
        /* B3.6zb (retour de test : "un seul Ctrl+Z supprime le pull ET le
           rectangle") : le push/pull est un geste CLIC (capture) ->
           deplacement -> CLIC (pose) - le relachement du clic de capture
           passait ICI et undoValide refermait le geste avant qu'il ait
           mute : le pas d'extrusion n'etait jamais pousse. Le push/pull
           se clot a ppCommit (pose) ou ppCancel - pas au relachement. */
        if (!g_ppActive && !g_rotActive)
            undoValide();                           /* B3.6z : le geste relache (s'il a mute) devient un pas */
        g_editGrab = -1;                            /* B2.7e : fin du drag de poignee */
        g_rollDrag = false;                         /* B3.6b : fin du drag de roll */
        g_mode = M_None;
    }
}

/* B2.4e : GLFW rapporte les touches par POSITION QWERTY (codes US) - sur
   AZERTY la touche etiquetee A arrive en GLFW_KEY_Q (c'etait le Maj+A mort,
   retour de test ; R, C, L, P... sont aux memes positions sur les deux
   dispositions, d'ou les autres raccourcis fonctionnels). Les raccourcis
   suivent desormais l'ETIQUETTE reelle (regle B2.2d de la saisie) :
   comparaison par glfwGetKeyName, repli par code si le nom manque
   (tests, disposition inconnue). */
static bool keyIsLetter(int key, int scancode, char letter) {
    const char* nm = glfwGetKeyName(key, scancode);
    if (nm && nm[0] && nm[1] == '\0') {
        char a = nm[0], b = letter;
        if (a >= 'a' && a <= 'z') a = char(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = char(b - 'a' + 'A');
        return a == b;
    }
    return key == (int)(unsigned char)letter;
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
            userMsg("duplication annulee");
        }
        return;
    }
    /* B3.6a-bis (retour test : "Maj + clic droit ne donne toujours pas de
       pan") : Maj pressee PENDANT le clic droit tenu = pan aussi - l'ordre
       du geste ne doit pas compter ; couvre l'edition (le clic droit presse
       sans Maj n'y sort plus de l'edition avant le relachement) */
    if ((key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) && action == GLFW_PRESS
        && g_rightDown && !g_ppActive && !g_rotActive
        && (g_mode == M_None || g_mode == M_Orbit || g_mode == M_Pan)) {
        g_mode = M_Pan;
        glfwGetCursorPos(w, &g_prevX, &g_prevY);
        flashBand("pan : deplacez la vue ; relachez le clic droit pour poser");
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
                userMsg("Angle invalide — tapez un nombre (degres, signe autorise) puis Entree.");
            } else {
                g_rotAngle = a;
                rotApplyCurrent(g_rotAxis, a);
                g_rotNumBuf.clear();
                rotCommit();           /* Entree = pose, comme le clic gauche */
                g_rotMode = false;     /* sortie du mode (meme contrat que l'extrusion) */
                userMsg("mode selection");
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            rotCancel();                /* Echap : annule la rotation ET quitte le mode (sortie de l'etat en cours) */
            g_rotMode = false;
            userMsg("mode selection");
        } else if (keyIsLetter(key, scancode, 'R')) {
            runToolCommand(IDM_TOOL_ROT);  /* R disponible a tout moment : quitte le mode */
        } else if (keyIsLetter(key, scancode, 'B') && !g_rotMulti.empty()) {
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
                userMsg("Valeur invalide — tapez un nombre non nul puis Entree (signe : tiret devant = enfoncement).");
            } else {
                /* butees (demande de test) : une valeur au-dela de la matiere
                   se cale a la butee au lieu de traverser et ressortir ; si
                   tout l enfoncement est consomme, la face devient feuille. */
                if (g_pp.hasLow && v < g_pp.dLow) {
                    userMsg("Butee atteinte : valeur calee a %+.3f.", g_pp.dLow);
                    v = g_pp.dLow;
                }
                if (g_pp.hasHigh && v > g_pp.dHigh) {
                    userMsg("Butee atteinte : valeur calee a %+.3f.", g_pp.dHigh);
                    v = g_pp.dHigh;
                }
                applyExtrude(g_objs[(size_t)g_pp.obj], g_pp, v);
                g_pp.d = v;
                g_undoSale = true;     /* B3.6z : pose numerique = vrai changement (v != 0 garanti) */
                g_pp.numBuf.clear();
                ppCommit();            /* Entree = pose, comme le second clic gauche */
                g_ppMode = false;      /* sortie du mode P apres application (demande de test) */
                userMsg("mode selection");
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            ppCancel();                 /* Echap : annule la face ET quitte le mode P (sortie de l'etat en cours) */
            g_ppMode = false;
            userMsg("mode selection");
        } else if (keyIsLetter(key, scancode, 'P')) {
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
                    userMsg("Longueur invalide - tapez un nombre strictement positif puis Entree.");
                else
                    linePoseByLength(len);
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            lineCancel("dessin annule");   /* Echap : tout annule et quitte le mode */
        } else if (keyIsLetter(key, scancode, 'L')) {
            runToolCommand(IDM_TOOL_LINE); /* L disponible a tout moment */
        }
        return;
    }
    if (g_rectActive) {   /* B2.2 : saisie des dimensions exactes "L;H" (meme contrat) */
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        const bool topRowR = key >= GLFW_KEY_0 && key <= GLFW_KEY_9;
        const bool padRowR = key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9;
        if (topRowR || padRowR || isSeparatorKey(key, scancode)) {
            /* caractere = ETIQUETTE REELLE de la touche (bug retour de test,
               clavier FR : sur AZERTY le ';' est NON majuscule - rangee basse
               , ; : ! - donc pas de Maj dans l'evenement). glfwGetKeyName
               renvoie ';', ',' ou '.' selon la disposition : ce qui est tape
               s'affiche. Repli par code de touche si le nom manque. */
            char c;
            if (padRowR) c = char('0' + (key - GLFW_KEY_KP_0));
            else if (topRowR) c = char(key);
            else {
                const char* nm = glfwGetKeyName(key, scancode);
                if (nm && nm[0] && nm[1] == '\0'
                    && (nm[0] == ';' || nm[0] == ',' || nm[0] == '.'))
                    c = nm[0];               /* etiquette de la disposition (FR : ';' sur la touche) */
                else if (key == GLFW_KEY_SEMICOLON) c = ';';
                else if (key == GLFW_KEY_KP_DECIMAL) c = '.';
                else if (key == GLFW_KEY_PERIOD) c = '.';
                else if (key == GLFW_KEY_COMMA) c = (mods & GLFW_MOD_SHIFT) ? ';' : ',';
                else c = '.';
            }
            if (g_rectNumBuf.size() < 16) g_rectNumBuf += c;
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            if (g_rectNumBuf.size() < 16) g_rectNumBuf.insert(g_rectNumBuf.begin(), '-');
        } else if (key == GLFW_KEY_BACKSPACE) {
            if (!g_rectNumBuf.empty()) g_rectNumBuf.pop_back();
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            if (g_rectNumBuf.empty()) {
                userMsg("Dimensions vides - tapez L;H puis Entree (clic = coin oppose, Echap = tout annuler).");
            } else {
                rectPoseByDims(g_rectNumBuf);
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            rectCancel("dessin annule");   /* Echap : tout annule et quitte le mode */
        } else if (keyIsLetter(key, scancode, 'R')) {
            runToolCommand(IDM_TOOL_RECT); /* R disponible a tout moment */
        }
        return;
    }
    if (g_cercleActive) {   /* B2.3 : saisie du rayon exact (meme contrat) */
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        const bool topRowC = key >= GLFW_KEY_0 && key <= GLFW_KEY_9;
        const bool padRowC = key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9;
        if (topRowC || padRowC || isSeparatorKey(key, scancode)) {
            /* chiffres = chiffres ; un seul nombre : tout separateur
               (etiquette reelle de la touche B2.2d, ou code) = decimale,
               comme la longueur de ligne */
            char c;
            if (padRowC) c = char('0' + (key - GLFW_KEY_KP_0));
            else if (topRowC) c = char(key);
            else c = '.';
            if (g_cercleNumBuf.size() < 16) g_cercleNumBuf += c;
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            if (g_cercleNumBuf.size() < 16) g_cercleNumBuf.insert(g_cercleNumBuf.begin(), '-');
        } else if (key == GLFW_KEY_BACKSPACE) {
            if (!g_cercleNumBuf.empty()) g_cercleNumBuf.pop_back();
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            if (g_cercleNumBuf.empty()) {
                userMsg("Rayon vide - tapez un nombre puis Entree (clic = rayon pose, Echap = tout annuler).");
            } else {
                cerclePoseByRayon(g_cercleNumBuf);
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            cercleCancel("dessin annule");   /* Echap : tout annule et quitte le mode */
        } else if (keyIsLetter(key, scancode, 'C') && (mods & GLFW_MOD_SHIFT)) {
            runToolCommand(IDM_TOOL_CERCLE); /* Maj+C disponible a tout moment */
        }
        return;
    }
    if (g_arcMode && g_arcState > 0) {   /* B2.4 : saisie corde puis fleche (meme contrat) */
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        const bool topRowA = key >= GLFW_KEY_0 && key <= GLFW_KEY_9;
        const bool padRowA = key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9;
        if (topRowA || padRowA || isSeparatorKey(key, scancode)) {
            char c;
            if (padRowA) c = char('0' + (key - GLFW_KEY_KP_0));
            else if (topRowA) c = char(key);
            else c = '.';                  /* un seul nombre : separateur = decimale */
            if (g_arcNumBuf.size() < 16) g_arcNumBuf += c;
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            if (g_arcNumBuf.size() < 16) g_arcNumBuf.insert(g_arcNumBuf.begin(), '-');
        } else if (key == GLFW_KEY_BACKSPACE) {
            if (!g_arcNumBuf.empty()) g_arcNumBuf.pop_back();
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            if (g_arcNumBuf.empty()) {
                userMsg("Valeur vide - tapez un nombre puis Entree (corde apres le depart, fleche apres la fin).");
            } else {
                std::string t = g_arcNumBuf;
                for (char& cc : t) { if (cc == ',' || cc == ';') cc = '.'; }   /* decimale FR */
                const double val = std::atof(t.c_str());
                if (!std::isfinite(val) || val <= 0.0) {
                    userMsg("Valeur invalide - tapez un nombre positif puis Entree.");
                } else if (g_arcState == 1) {
                    arcPoseByCorde(val);
                } else {
                    arcPoseByFleche(val);
                }
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            arcCancel("dessin annule");    /* Echap : tout annule et quitte le mode */
        } else if (keyIsLetter(key, scancode, 'A') && (mods & GLFW_MOD_SHIFT)) {
            runToolCommand(IDM_TOOL_ARC);  /* Maj+A disponible a tout moment */
        }
        return;
    }
    if (g_polyMode) {   /* B2.6b : Entree/clic droit = valide ; Echap = tout annuler */
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            polyFinish();                   /* B2.6b : Entree = valide (ferme la surface) */
        } else if (key == GLFW_KEY_ESCAPE) {
            polyCancel("dessin annule");
        } else if (keyIsLetter(key, scancode, 'P') && (mods & GLFW_MOD_SHIFT)) {
            runToolCommand(IDM_TOOL_POLY);  /* Maj+P disponible a tout moment */
        }
        return;
    }
    if (g_splMode) {    /* B2.7 : pas de saisie numerique - Entree/clic droit = valide ; Echap = tout annuler */
        if (action != GLFW_PRESS) { recordAxisKey(scancode, action); return; }
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            splFinish();                    /* B2.7 : Entree = valide (courbe ouverte) */
        } else if (key == GLFW_KEY_ESCAPE) {
            splCancel("dessin annule");
        } else if (keyIsLetter(key, scancode, 'S') && (mods & GLFW_MOD_SHIFT)) {
            runToolCommand(IDM_TOOL_SPL);   /* Maj+S disponible a tout moment */
        } else if (keyIsLetter(key, scancode, 'B') && (mods & GLFW_MOD_SHIFT)) {
            runToolCommand(IDM_TOOL_SPLC);  /* Maj+B = points de controle (B2.7b) */
        }
        return;
    }
    if (action == GLFW_PRESS) {
        /* Ctrl+S = enregistrer le projet (retour utilisateur) ; position : avant les exports scene par O/S */
        if ((mods & GLFW_MOD_CONTROL) && keyIsLetter(key, scancode, 'S')) {
            handleMenuAction(IDM_SAVE_PROJECT);
            return;
        }
        /* Ctrl+C / Ctrl+V : copier / coller la selection (T1.12) ;
           C sans Ctrl reste le recentrage sur le point vise */
        if ((mods & GLFW_MOD_CONTROL) && keyIsLetter(key, scancode, 'C')) { copySelected(); return; }
        if ((mods & GLFW_MOD_CONTROL) && keyIsLetter(key, scancode, 'V')) { pasteClipboard(); return; }
        /* B3.6z : Ctrl+Z = annuler (Maj tenue = retablir), Ctrl+Y = retablir ;
           place APRES Ctrl+C/V et AVANT les blocs de modes, donc actif aussi
           pendant l'edition (les bascules d'edition viennent plus bas) */
        if ((mods & GLFW_MOD_CONTROL) && keyIsLetter(key, scancode, 'Z')) {
            if (mods & GLFW_MOD_SHIFT) undoRetablit(); else undoAnnule();
            return;
        }
        if ((mods & GLFW_MOD_CONTROL) && keyIsLetter(key, scancode, 'Y')) { undoRetablit(); return; }
        if (key == GLFW_KEY_ESCAPE) {
            /* Echelle Echap : d'abord sortir de l'etat en cours (mode P/R en attente,
               puis deselection) ; Echap ne ferme JAMAIS l'application (rebonds de
               clavier) : fermeture volontaire par Fichier > Quitter uniquement. */
            if (g_editMode) { editExit(); return; }   /* B2.7e : Echap = quitter l'edition */
            if (g_sweepStage) { sweepCancel(); return; }   /* B3 : Echap = annule le balayage */
            if (g_collStage) { collCancel("collage annule"); return; }   /* B3.7 : Echap = annule */
            if (g_lineMode) { lineCancel("dessin annule"); return; }   /* B2.1 */
            if (g_rectMode) { rectCancel("dessin annule"); return; }   /* B2.2 */
            if (g_cercleMode) { cercleCancel("dessin annule"); return; }   /* B2.3 */
            if (g_arcMode) { arcCancel("dessin annule"); return; }      /* B2.4 */
            if (g_polyMode) { polyCancel("dessin annule"); return; }   /* B2.6 */
            if (g_ppMode || g_rotMode) { g_ppMode = false; g_rotMode = false; userMsg("mode selection"); return; }
            if (selectedCount() > 0) { clearSelection(); userMsg("selection annulee"); return; }
        }
        if (g_editMode) {   /* B2.7e : edition d'un trace - bascules a chaud, le reste est avale */
            if (g_editObj < 0 || g_editObj >= (int)g_objs.size()) { editExit(); return; }   /* defense : scene videe */
            if ((mods & GLFW_MOD_SHIFT) && keyIsLetter(key, scancode, 'S')) { traceSetVariante(g_objs[(size_t)g_editObj], false); return; }
            if ((mods & GLFW_MOD_SHIFT) && keyIsLetter(key, scancode, 'B')) { traceSetVariante(g_objs[(size_t)g_editObj], true); return; }
            if (keyIsLetter(key, scancode, 'O')) { traceToggleFerme(g_objs[(size_t)g_editObj]); return; }
            if (key == GLFW_KEY_DELETE) {   /* poignee + Suppr = supprimer le point */
                if (g_editSel < 0) userMsg("edition : cliquez d'abord une poignee (Suppr = supprimer le point saisi)");
                else traceDelPoint(g_objs[(size_t)g_editObj], g_editSel);
                return;
            }
            if (keyIsLetter(key, scancode, 'E')) { runToolCommand(IDM_TOOL_EDIT); return; }   /* E = quitter */
            return;   /* mode exclusif : les autres touches sont ignorees */
        }
        if (keyIsLetter(key, scancode, 'E') && !(mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending) runToolCommand(IDM_TOOL_EDIT);   /* B2.7e : E = editer le trace selectionne */
        if (keyIsLetter(key, scancode, 'J') && !(mods & GLFW_MOD_SHIFT)) runToolCommand(IDM_VIEW_JOURNAL);   /* B3.3f : journal des messages */
        if (keyIsLetter(key, scancode, 'P') && g_mode == M_None && !g_dupPending && !g_lineMode) runToolCommand(IDM_TOOL_PP);
        if (keyIsLetter(key, scancode, 'R') && !(mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending && !g_lineMode) runToolCommand(IDM_TOOL_ROT);
        if (keyIsLetter(key, scancode, 'R') && (mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending && !g_rectActive)
            runToolCommand(IDM_TOOL_RECT);   /* B2.2 : Maj+R = rectangle (R seul = Tourner) */
        if (keyIsLetter(key, scancode, 'C') && (mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending && !g_lineActive && !g_rectActive) {
            runToolCommand(IDM_TOOL_CERCLE);   /* B2.3 : Maj+C = cercle (C seul = centrer, Ctrl+C = copie) */
            return;                            /* ne pas retomber sur le recentrage C */
        }
        if (keyIsLetter(key, scancode, 'A') && (mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending && !g_lineActive && !g_rectActive) {
            runToolCommand(IDM_TOOL_ARC);      /* B2.4 : Maj+A = arc (convention Maj+lettre des formes) */
            return;
        }
        if (keyIsLetter(key, scancode, 'P') && (mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending && !g_lineActive && !g_rectActive) {
            runToolCommand(IDM_TOOL_POLY);     /* B2.6 : Maj+P = polygone (P seul = Push/Pull) */
            return;
        }
        if (keyIsLetter(key, scancode, 'S') && (mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending && !g_lineActive && !g_rectActive) {
            runToolCommand(IDM_TOOL_SPL);      /* B2.7 : Maj+S = spline (Ctrl+S = enregistrer) */
            return;
        }
        if (keyIsLetter(key, scancode, 'B') && (mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending && !g_lineActive && !g_rectActive) {
            runToolCommand(IDM_TOOL_SPLC);     /* B2.7b : Maj+B = spline par points de controle */
            return;
        }
        if (keyIsLetter(key, scancode, 'W') && (mods & GLFW_MOD_SHIFT) && g_mode == M_None && !g_dupPending && !g_lineActive && !g_rectActive) {
            runToolCommand(IDM_TOOL_SWEEP);    /* B3 : Maj+W = balayage le long d'un chemin */
            return;
        }
        if (keyIsLetter(key, scancode, 'L') && g_mode == M_None && !g_dupPending && !g_lineActive)
            runToolCommand((mods & GLFW_MOD_SHIFT) ? IDM_TOOL_LIGNE_FIL : IDM_TOOL_LINE);  /* B2.5b : L = surface, Maj+L = fil ouvert */
        if (g_rotMode && !g_rotActive) {
            const char c = axisLetterFromScancode(scancode);
            if (c) { rotBegin(w, c - 'x'); return; }
        }
        if (key == GLFW_KEY_DELETE && g_mode == M_None) {   /* Suppr : supprime toute la selection (1 objet comme plusieurs) */
            deleteSelectedSet();
            return;
        }
        if (keyIsLetter(key, scancode, 'F')) frameSelected();
        if (keyIsLetter(key, scancode, 'H')) { goHome(); userMsg("vue d'origine"); }
        if (keyIsLetter(key, scancode, 'C') && !(mods & GLFW_MOD_CONTROL)) {  /* centrer sur le point vise */
            Vec3 org, dir, p;
            if (rayFromMouse(w, org, dir) && pickPointFromRay(org, dir, p)) {
                g_cam.target = p;
                userMsg("vue centree sur le point vise");
            } else {
                userMsg("C : aucun point vise (pointez un objet ou le sol)");
            }
        }
        if (keyIsLetter(key, scancode, 'I')) { importFile(); }   /* importer OBJ/STL */
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
        if (g_editMode) { editExit(); }   /* B2.7e */
        else if (g_lineMode) { lineCancel("dessin annule"); }
        else if (g_ppActive) { ppCancel(); g_ppMode = false; userMsg("mode selection"); }
        else if (g_rotActive) { rotCancel(); g_rotMode = false; userMsg("mode selection"); }
        else if (g_ppMode || g_rotMode) { g_ppMode = false; g_rotMode = false; userMsg("mode selection"); }
        else if (selectedCount() > 0) { clearSelection(); userMsg("selection annulee"); }
        return;
    case IDM_TOOL_PP:          /* = touche P */
        if (g_ppMode) {
            ppCancel();          /* annule la face en suspens si active, sans effet sinon */
            g_ppMode = false;
            userMsg("mode selection");
        } else if (g_mode == M_None && !g_dupPending && !g_lineMode) {
            g_ppMode = true;
            g_rotMode = false;
            userMsg("mode Push/Pull : cliquez une face (dessus ou cote), deplacez, puis re-cliquez pour poser");
        }
        return;
    case IDM_TOOL_ROT:         /* = touche R */
        if (g_rotMode) {
            rotCancel();         /* annule la rotation en suspens si active */
            g_rotMode = false;
            userMsg("mode selection");
        } else if (g_mode == M_None && !g_dupPending && !g_lineMode) {
            g_rotMode = true;
            g_ppMode = false;
            userMsg("mode Rotation : selectionnez un objet (Ctrl+clic pour en selectionner plusieurs), puis X, Y ou Z (lettre) pour fixer l'axe");
        }
        return;
    case IDM_TOOL_FRAME:      /* = F */
        frameSelected();
        return;
    case IDM_TOOL_HOME:       /* = H */
        goHome();
        userMsg("vue d'origine");
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
            userMsg(id == IDM_TOOL_LINE
                ? "mode Ligne (fermee en boucle) : clics = points du fil ; clic sur le depart = fermeture en boucle (sans surface) ; clic droit = fin ; Echap = tout annuler\n"
                : "mode Ligne (fil ouvert) : clics = points du fil ; clic sur le depart = boucle sans surface ; clic droit = fin ; Echap = tout annuler");
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
            userMsg("mode Rectangle : clic = premier coin ; clic = coin oppose pose ; chiffres L;H + Entree = dimensions exactes ; Echap = tout annuler");
        } else {
            flashBand("rectangle : terminez d'abord le mode en cours (Echap)");
            userMsg("rectangle : terminez d'abord le mode en cours (Echap)");
        }
        return;
    case IDM_TOOL_CERCLE:      /* = Maj+C (B2.3) */
        if (g_cercleMode) {
            cercleCancel("mode cercle quitte");
        } else if (g_mode == M_None && !g_dupPending && !g_lineActive && !g_lineMode && !g_rectActive && !g_cercleActive) {
            g_cercleMode = true;
            cercleReset();
            g_ppMode = false;
            g_rotMode = false;
            userMsg("mode Cercle : clic = centre ; clic = rayon pose ; chiffres + Entree = rayon exact ; Echap = tout annuler");
        } else {
            flashBand("cercle : terminez d'abord le mode en cours (Echap)");
            userMsg("cercle : terminez d'abord le mode en cours (Echap)");
        }
        return;
    case IDM_TOOL_ARC:         /* = Maj+A (B2.4) */
        if (g_arcMode) {
            arcCancel("mode arc quitte");
        } else if (g_mode == M_None && !g_dupPending && !g_lineActive && !g_lineMode && !g_rectActive && !g_cercleActive) {
            g_arcMode = true;
            arcReset();
            g_ppMode = false;
            g_rotMode = false;
            userMsg("mode Arc : clic = depart ; clic = fin de la corde ; clic = bombement pose ; chiffres + Entree = corde puis fleche ; Echap = tout annuler");
        } else {
            flashBand("arc : terminez d'abord le mode en cours (Echap)");
            userMsg("arc : terminez d'abord le mode en cours (Echap)");
        }
        return;
    case IDM_TOOL_POLY:        /* = Maj+P (B2.6) */
        if (g_polyMode) {
            polyCancel("mode polygone quitte");
        } else if (g_mode == M_None && !g_dupPending && !g_lineActive && !g_lineMode
                   && !g_rectActive && !g_cercleActive && !g_arcMode) {
            g_polyMode = true;
            polyReset();
            g_ppMode = false;
            g_rotMode = false;
            userMsg("mode Polygone : clic = sommet ; clic sur le depart, clic droit ou Entree = ferme la surface ; Echap = tout annuler");
        } else {
            flashBand("polygone : terminez d'abord le mode en cours (Echap)");
            userMsg("polygone : terminez d'abord le mode en cours (Echap)");
        }
        return;
    case IDM_TOOL_SPL:         /* = Maj+S (B2.7) */
        if (g_splMode && !g_splCtrl) {
            splCancel("mode spline quitte");
        } else if (g_splMode) {
            g_splCtrl = false;               /* B2.7b : bascule variante, points conserves */
            userMsg("mode Spline (points de passage) : clic = point ; clic sur le depart = boucle fermee ; clic droit ou Entree = valide ; Echap = tout annuler");
        } else if (g_mode == M_None && !g_dupPending && !g_lineActive && !g_lineMode
                   && !g_rectActive && !g_cercleActive && !g_arcMode && !g_polyMode) {
            g_splMode = true;
            g_splCtrl = false;
            splReset();
            g_ppMode = false;
            g_rotMode = false;
            userMsg("mode Spline (points de passage) : clic = point ; clic sur le depart = boucle fermee ; clic droit ou Entree = valide (ouverte) ; Echap = tout annuler");
        } else {
            flashBand("spline : terminez d'abord le mode en cours (Echap)");
            userMsg("spline : terminez d'abord le mode en cours (Echap)");
        }
        return;
    case IDM_ANNULER:          /* = Ctrl+Z (B3.6z) */
        undoAnnule();
        return;
    case IDM_COLLER_SUR:       /* B3.7 : collage generique 2 objets (toggle) */
        if (g_collStage) { collCancel("collage annule"); }
        else if (g_mode == M_None && !g_dupPending && !g_lineMode && !g_rectMode
                 && !g_cercleMode && !g_arcMode && !g_polyMode && !g_splMode
                 && !g_ppMode && !g_rotMode && !g_editMode && !g_ppActive
                 && !g_rotActive && !g_sweepStage) {
            collStart();
        }
        return;
    case IDM_RETABLIR:         /* = Ctrl+Y (B3.6z) */
        undoRetablit();
        return;
    case IDM_TOOL_SWEEP:       /* = Maj+W (B3) : balayage le long d'un chemin */
        if (g_sweepStage) {
            sweepCancel();
        } else if (g_mode == M_None && !g_dupPending && !g_lineMode && !g_rectMode
                   && !g_cercleMode && !g_arcMode && !g_polyMode && !g_splMode
                   && !g_ppMode && !g_rotMode && !g_editMode && !g_ppActive && !g_rotActive) {
            g_sweepStage = 1;
            g_sweepProfil = -1;
            g_ppMode = false;
            g_rotMode = false;
            userMsg("mode Balayage : cliquez la FACE a extruder le long d'un chemin (feuille tracee) ; Echap = annuler");
        } else {
            flashBand("balayage : terminez d'abord le mode en cours (Echap)");
            userMsg("balayage : terminez d'abord le mode en cours (Echap)");
        }
        return;
    case IDM_TOOL_EDIT:        /* = E (B2.7e) : edition du trace selectionne */
        if (g_editMode) {
            editExit();
        } else if (g_sweepStage) {   /* B3.3d : ne pas armer l'edition pendant
                                        un balayage arme (Echap d'abord) */
            flashBand("balayage : terminez d'abord le mode en cours (Echap)");
            userMsg("balayage : terminez d'abord le mode en cours (Echap)");
        } else if (g_mode == M_None && !g_dupPending && !g_lineMode && !g_rectMode
                   && !g_cercleMode && !g_arcMode && !g_polyMode && !g_splMode
                   && !g_ppMode && !g_rotMode) {
            const int idx = (selectedCount() == 1 && g_selected >= 0) ? g_selected : -1;
            if (idx < 0 || g_objs[(size_t)idx].traceType == 0) {
                flashBand("Editer : selectionnez d'abord UNE forme tracee (E)");
                userMsg("editer : selectionnez d'abord UNE forme tracee (ligne, spline, rectangle, cercle, arc, polygone)");
            } else {
                g_editMode = true;
                g_editObj = idx;
                g_editGrab = -1;
                g_editSel = -1;
                g_sweepEditChemin = false;
                g_sweepEditAcc = false;
                g_sweepEditRoll = false;
                if (g_objs[(size_t)idx].traceType == 9) {   /* B3.3/B3.6a */
                    userMsg("mode Edition : %s - PROFIL (%zu pts, verts) + CHEMIN (%zu pts, cyans) - glissez une poignee ; clic sur le tube = point de chemin ; O = cible (profil, chemin, accroche, ROLL = rotation du profil) ; poignee + Suppr = supprimer ; Echap ou clic droit = quitter",
                                g_objs[(size_t)idx].name.c_str(), g_objs[(size_t)idx].tracePts.size(),
                                g_objs[(size_t)idx].traceChemin.size());
                    return;
                }
                userMsg("mode Edition : %s (%zu points) - glissez une poignee ; clic sur la courbe = ajouter un point (ligne, spline, polygone) ; poignee + Suppr = supprimer ; Maj+S/Maj+B = variante spline ; O = ouvert/ferme ; Echap ou clic droit = quitter",
                            g_objs[(size_t)idx].name.c_str(), g_objs[(size_t)idx].tracePts.size());
            }
        }
        return;
    case IDM_TOOL_SPLC:        /* = Maj+B (B2.7b) */
        if (g_splMode && g_splCtrl) {
            splCancel("mode spline quitte");
        } else if (g_splMode) {
            g_splCtrl = true;                /* B2.7b : bascule variante, points conserves */
            userMsg("mode Spline (points de controle) : chaque clic = un COIN du polygone de controle ; la courbe jaune glisse a l'interieur et ne touche que ses extremites ; ajoutez des points pour serrer la courbe ; clic sur le depart = boucle fermee ; clic droit ou Entree = valide ; Echap = tout annuler");
        } else if (g_mode == M_None && !g_dupPending && !g_lineActive && !g_lineMode
                   && !g_rectActive && !g_cercleActive && !g_arcMode && !g_polyMode) {
            g_splMode = true;
            g_splCtrl = true;
            splReset();
            g_ppMode = false;
            g_rotMode = false;
            userMsg("mode Spline (points de controle) : chaque clic = un COIN du polygone de controle ; la courbe jaune glisse a l'interieur et ne touche que ses extremites ; ajoutez des points pour serrer la courbe ; clic sur le depart = boucle fermee ; clic droit ou Entree = valide (ouverte) ; Echap = tout annuler");
        } else {
            flashBand("spline : terminez d'abord le mode en cours (Echap)");
            userMsg("spline : terminez d'abord le mode en cours (Echap)");
        }
        return;
    case IDM_VIEW_JOURNAL:     /* = J (B3.3f) : panneau des messages utilisateur */
        g_journalVisible = !g_journalVisible;
        userMsg(g_journalVisible ? "journal des messages affiche (J pour masquer)"
                                 : "journal des messages masque");
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
    /* B3.6a-ter : plan proche ADAPTATIF - sous dist 5 le plan passe de
       0.05 a 0.005 (le zoom rapproche sous l'ancienne borne sans z-fighting
       a distance : au-dela de dist 5, la precision 1:100000 est conservee) */
    if (g_winH > 0) setPerspective(kFovDeg, double(g_winW) / double(g_winH),
                                   (g_cam.dist < 5.0) ? 0.005 : 0.05, 5000.0);

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
    if (g_editMode) {
        if (g_editObj >= 0 && g_editObj < (int)g_objs.size()
            && g_objs[(size_t)g_editObj].traceType == 9)
            return g_sweepEditRoll
                ? "Edition du balayage : ROLL - glissez la poignee violette pour tourner le profil autour du fil | O = cible PROFIL | Echap = quitter"
                : g_sweepEditAcc
                ? "Edition du balayage : accroche (orange) - cliquez un coin, un milieu de segment ou le centre = point colle au fil | O = cible ROLL | Echap = quitter"
                : g_sweepEditChemin
                ? "Edition du balayage : chemin (cyans) - glissez une poignee | clic SUR LE TUBE = nouvelle station | O = cible ACCROCHE | poignee + Suppr = supprimer | Echap = quitter"
                : "Edition du balayage : profil (verts) - glissez une poignee | clic dans le plan = ajouter | O = cible CHEMIN | poignee + Suppr = supprimer | Echap = quitter";
        if (g_editObj >= 0 && g_editObj < (int)g_objs.size()
            && g_objs[(size_t)g_editObj].traceType == 8)
            return "Edition de l extrusion : glissez une poignee (bleue = hauteur) | clic sur une face = ajouter un point au profil | Echap ou clic droit = quitter";
        return "Edition du trace : glissez une poignee | clic sur la courbe = ajouter un point (ligne, spline, polygone) | poignee + Suppr = supprimer | Maj+S/Maj+B = variante spline | O = ouvert/ferme | Echap ou clic droit = quitter";
    }
    if (g_sweepStage == 1)
        return "Balayage : cliquez la FACE a extruder le long d'un chemin (feuille tracee) | Echap = annuler";
    if (g_sweepStage == 2)
        return "Balayage : cliquez le FIL chemin (ligne, spline, arc) | Echap = annuler";
    if (g_collStage)
        return "Collage : cliquez la cible sur un AUTRE objet (sommet, arete, face) | Echap ou clic droit = annuler";   /* B3.7 */
    if (g_lineActive)
        return "Ligne : clic = prolonger | clic droit = fin | chiffres + Entree = longueur exacte | Echap = tout annuler";
    if (g_rectActive)
        return "Rectangle : clic = coin oppose (pose) | chiffres L;H + Entree = dimensions exactes | Echap = tout annuler";
    if (g_cercleActive)
        return "Cercle : clic = rayon (pose) | chiffres + Entree = rayon exact | Echap = tout annuler";
    if (g_polyActive)
        return "Polygone : clic = sommet suivant | clic sur le depart, clic droit ou Entree = fermer (surface) | Echap = tout annuler";
    if (g_splActive)
        return g_splCtrl
            ? "Spline (controle) : clic = coin du polygone | la courbe glisse a l'interieur | clic droit ou Entree = valider | Echap = tout annuler"
            : "Spline : clic = point suivant | clic sur le depart = boucle fermee | clic droit ou Entree = valider | Echap = tout annuler";
    if (g_arcMode && g_arcState == 1)
        return "Arc : clic = fin de la corde | chiffres + Entree = longueur de corde | Echap = tout annuler";
    if (g_arcMode && g_arcState == 2)
        return "Arc : clic = bombement (pose) | chiffres + Entree = fleche exacte | Echap = tout annuler";
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
    if (g_rectActive) { *label = "L;H";       *buf = &g_rectNumBuf; return true; }
    if (g_cercleActive) { *label = "rayon";   *buf = &g_cercleNumBuf; return true; }
    if (g_arcMode && g_arcState == 1) { *label = "corde";  *buf = &g_arcNumBuf; return true; }
    if (g_arcMode && g_arcState == 2) { *label = "fleche"; *buf = &g_arcNumBuf; return true; }
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
    if (g_journalVisible) {   /* B3.3f : panneau des derniers messages (J) */
        const size_t nAff = 12;
        size_t debut;
        float xj, yj, lj, hj;
        journalRect(xj, yj, lj, hj, debut, nAff);   /* B3.3h : haut gauche, largeur fixe */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.97f, 0.97f, 0.99f, 0.92f);
        glBegin(GL_QUADS);
        glVertex2f(xj, yj); glVertex2f(xj + lj, yj);
        glVertex2f(xj + lj, yj + hj); glVertex2f(xj, yj + hj);
        glEnd();
        glDisable(GL_BLEND);
        glColor3f(0.6f, 0.6f, 0.65f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(xj + 0.5f, yj + 0.5f); glVertex2f(xj + lj - 0.5f, yj + 0.5f);
        glVertex2f(xj + lj - 0.5f, yj + hj - 0.5f); glVertex2f(xj + 0.5f, yj + hj - 0.5f);
        glEnd();
        glColor3f(0.08f, 0.09f, 0.12f);
        for (size_t i = debut; i < g_journal.size(); ++i)
            drawText2D(xj + 8.0f, yj + hj - 16.0f - (float)(i - debut) * 15.0f,
                       g_journal[i].c_str());
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
    userMsg("Sketch3D - build %s", BUILD_VERSION);
    userMsg("Outils  : P extraire une face | R tourner (X/Y/Z = axe, B pivot, chiffres+Entree) | L ligne (clic sur le depart = fermer ; Maj+L fil) | Maj+R rectangle (clics = coins, L;H = exact) | Maj+C cercle (centre, rayon) | Maj+A arc (depart, fin, bombement) | Maj+P polygone (clic sur le depart, clic droit ou Entree = ferme) | Maj+S spline (passage) | Maj+B spline (controle, clic depart = boucle) | E editer un trace | Maj+W balayage (face puis fil) | Ctrl+C/V | Ctrl+Z annuler, Ctrl+Y retablir | Suppr");
    userMsg("Vue     : orbite = clic gauche dans le vide | zoom = molette | panoramique = molette-clic ou Shift+glisser | C centrer | F cadrer | H origine");
    userMsg("Projets : Ctrl+S enregistrer | clic droit = infos objet + duplication | I importer");
    userMsg("Formats : export scene/selection = menu Fichier, format defini dans Preferences");
    g_setupPath = setupFilePath();
    loadSetup();                     /* B0 : preferences de la derniere session */
    if (g_setup.winW > 0) g_winW = g_setup.winW;
    if (g_setup.winH > 0) g_winH = g_setup.winH;
    /* B3.2d : persistence du projet en cours - rouvrir le dernier projet ;
       scene de demo seulement si aucun projet ou fichier illisible */
    {
        std::string err;
        if (g_setup.dernierProjet.empty() || !loadProject(g_setup.dernierProjet, err)) {
            if (!g_setup.dernierProjet.empty())
                userMsg("projet precedent illisible (%s) : scene de demo", err.c_str());
            buildScene();
        }
    }
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit a echoue"); return 1; }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_SAMPLES, 4);
    char titre[64];
    std::snprintf(titre, sizeof(titre), "Sketch3D — build %s", BUILD_VERSION);
    GLFWwindow* win = glfwCreateWindow(g_winW, g_winH, titre, nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "creation de fenetre impossible"); glfwTerminate(); return 1; }
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
        } else if (g_editGrab >= 0) {
            editDrag(win);           /* B2.7e : poignee saisie = drag + recalcul continu */
        } else if (g_rollDrag) {
            rollDragUpdate(win);     /* B3.6b : poignee de roll = tourner le profil */
        } else if (g_ppActive) {
            updatePP(win);
        } else if (g_rotActive) {
            updateRot(win);           /* B2.1b : branche RESTAUREE (remplacee par erreur
                                         dans B2.1 : la rotation souris ne seguait plus) */
        } else if (g_lineMode) {
            lineTrackCursor(win);     /* B2.1 : apercu du fil vers le curseur */
        } else if (g_rectMode) {
            rectTrackCursor(win);     /* B2.2 : apercu du rectangle vers le curseur */
        } else if (g_cercleMode) {
            cercleTrackCursor(win);   /* B2.3 : apercu du cercle vers le curseur */
        } else if (g_arcMode) {
            arcTrackCursor(win);      /* B2.4 : apercu de l'arc vers le curseur */
        } else if (g_polyMode) {
            polyTrackCursor(win);     /* B2.6 : apercu du polygone vers le curseur */
        } else if (g_splMode) {
            splTrackCursor(win);      /* B2.7 : apercu de la spline vers le curseur */
        }
        updateWindowTitle(win);
        render();
        drawAxisGuide();
        drawLinePreview();
        drawRectPreview();
        drawCerclePreview();
        drawArcPreview();
        drawPolyPreview();
        drawSplinePreview();
        drawEditHandles();       /* B2.7e : poignees du trace en edition */
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
