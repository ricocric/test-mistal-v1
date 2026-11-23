// Sketch3D — programme natif (transcription de la version web, fichier unique index.html)
// Lot N2 : selection au clic + deplacement d'objets, libre ou verrouille sur axe (X/Y/Z).
// Base (lot N1) : fenetre, camera orbitale, grille, cube de demo, cadrage F, Echap = quitter.
//
// Interaction du lot :
//   - clic gauche sur un objet : selection, puis glisser = deplacer
//     (par defaut sur le plan face a la camera, comme le dragPlane web) ;
//   - touches X / Y / Z MAINTENUES pendant le glisser : deplace seulement sur l'axe
//     (transcription du verrouillage d'axe fluide du lot web 2.1) ;
//   - clic gauche sur le vide : deselection ; glisser dans le vide : orbite camera ;
//   - F : cadrage (sur l'objet selectionne si possible) ;
//   - Echap : quitter.
//
// Rendu : OpenGL fixed-function (1.1) — le pipeline par shaders/PBR arrive a un lot
// dedie. Modele geometrique en double precision.

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

/* Matrice perspective en ordre colonne-major (evite la dependance a GLU) */
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

/* LookAt sans dependance GLU : calcule la matrice (translation incluse) et la multiplie */
static void gluLookAtImpl(const Vec3& eye, const Vec3& tgt) {
    const Vec3 f = normalized(tgt - eye);
    const Vec3 up0(0, 1, 0);
    const Vec3 s = normalized(cross(f, up0));
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
    double theta = 0.8;   // azimut (tour autour de Y)
    double phi = 0.5;     // elevation
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
/* Scene : boites alignees sur les axes (la rotation arrive plus tard) */
/* ------------------------------------------------------------------ */

struct Object {
    std::string name;
    Vec3 pos;      // centre
    Vec3 half;     // demi-dimensions
    double col[3];
};

static std::vector<Object> g_objs;
static int g_selected = -1;

static void buildScene() {
    g_objs.clear();
    g_objs.push_back({"Cube A", Vec3(0, 1.0, 0), Vec3(1, 1, 1), {0.55, 0.35, 0.2}});
    g_objs.push_back({"Pave B", Vec3(3.5, 0.6, 1.5), Vec3(1.5, 0.6, 0.8), {0.3, 0.5, 0.7}});
    g_objs.push_back({"Colonne C", Vec3(-3, 2, -1.5), Vec3(0.5, 2, 0.5), {0.45, 0.6, 0.35}});
}

/* ------------------------------------------------------------------ */
/* Etat interaction                                                    */
/* ------------------------------------------------------------------ */

enum Mode { M_None, M_Orbit, M_Drag };
static Mode g_mode = M_None;
static Camera g_cam;
static double g_prevX = 0, g_prevY = 0;
static int g_winW = 1280, g_winH = 800;
static int g_lockAxis = -1;            // 0=X, 1=Y, 2=Z pendant un drag
static Vec3 g_grabPoint;               // point de saisie (monde)
static Vec3 g_startPos;                // position de l'objet au moment de la saisie

static const char* axisName(int a) { return a == 0 ? "X" : a == 1 ? "Y" : "Z"; }
static Vec3 axisVec(int a) { return a == 0 ? Vec3(1, 0, 0) : a == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1); }

static void frameScene() {
    if (g_objs.empty()) { g_cam.target = Vec3(0, 0.8, 0); g_cam.dist = 9.0; return; }
    Vec3 lo = g_objs[0].pos - g_objs[0].half;
    Vec3 hi = g_objs[0].pos + g_objs[0].half;
    for (size_t i = 1; i < g_objs.size(); ++i) {
        const Vec3 m = g_objs[i].pos - g_objs[i].half;
        const Vec3 M = g_objs[i].pos + g_objs[i].half;
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
    g_cam.target = o.pos;
    const double r = o.half.norm();
    if (r > 1e-12) g_cam.dist = 4.0 * r;
}

/* ------------------------------------------------------------------ */
/* Rayon souris + intersection avec une boite (tout en doubles)        */
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

static bool rayBox(const Vec3& org, const Vec3& dir, const Object& b, double& tHit) {
    double t0 = -1e300, t1 = 1e300;
    for (int a = 0; a < 3; ++a) {
        const double ro = (&org.x)[a], rd = (&dir.x)[a];
        const double lo = (&b.pos.x)[a] - (&b.half.x)[a];
        const double hi = (&b.pos.x)[a] + (&b.half.x)[a];
        if (std::fabs(rd) < 1e-15) {
            if (ro < lo || ro > hi) return false;
        } else {
            double ta = (lo - ro) / rd, tb = (hi - ro) / rd;
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

static int pickAt(GLFWwindow* w) {
    Vec3 org, dir;
    if (!rayFromMouse(w, org, dir)) return -1;
    int best = -1;
    double bestT = 1e300;
    for (size_t i = 0; i < g_objs.size(); ++i) {
        double t;
        if (rayBox(org, dir, g_objs[i], t) && t < bestT) { bestT = t; best = (int)i; }
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
    if (!rayBox(org, dir, g_objs[(size_t)idx], t)) return;
    g_grabPoint = org + dir * t;
    g_startPos = g_objs[(size_t)idx].pos;
    g_lockAxis = -1;
    g_mode = M_Drag;
}

/* Glisser : le point saisi reste sous le curseur sur un plan face a la
   camera (copie du dragPlane web). Touche X/Y/Z maintenue : restriction. */
/* Etat des touches d'axe : remplies par le callback clavier a partir du
   scancode, car glfwGetKey donne la position physique QWERTY US — sur un
   clavier AZERTY la touche etiquetee Z arrive en GLFW_KEY_W et le lock Z
   ne se declenchait jamais. glfwGetKeyName traduit le scancode selon le
   layout courant, donc X/Y/Z marchent sur AZERTY comme QWERTY. */
static bool g_axisHeld[3] = {false, false, false};

static void recordAxisKey(int scancode, int action) {
    /* premier parametre = GLFW_KEY_UNKNOWN (-1) : on demunde la traduction
       du scancode selon le layout courant ; passer 0 reviendrait a demander
       le nom de la touche Espace pour toute touche. */
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
/* Callbacks                                                           */
/* ------------------------------------------------------------------ */

static void onScroll(GLFWwindow*, double, double dy) {
    g_cam.dist *= std::pow(0.9, dy);
    if (g_cam.dist < 0.5) g_cam.dist = 0.5;
    if (g_cam.dist > 200.0) g_cam.dist = 200.0;
}

static void onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        const int hit = pickAt(w);
        if (hit != g_selected) {
            g_selected = hit;
            if (hit >= 0) {
                const Object& o = g_objs[(size_t)hit];
                std::printf("selection : %s — centre (%.3f, %.3f, %.3f), dimensions %.3f x %.3f x %.3f\n",
                            o.name.c_str(), o.pos.x, o.pos.y, o.pos.z,
                            o.half.x * 2, o.half.y * 2, o.half.z * 2);
            } else {
                std::printf("deselection\n");
            }
        }
        if (hit >= 0) { beginDrag(w, hit); }
        else { g_mode = M_Orbit; }
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
    /* axes : X rouge, Z bleu — sans test de profondeur ni ligne coplanaire :
       sinon le quadrillage masque les axes par egalite de profondeur */
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

static void drawBox(const Object& o, bool selected) {
    const Vec3 m = o.pos - o.half, M = o.pos + o.half;
    /* winding CCW vus de l'exterieur (normale sortante = produit vectoriel) */
    const double q[6][4][3] = {
        {{m.x,m.y,m.z},{M.x,m.y,m.z},{M.x,m.y,M.z},{m.x,m.y,M.z}},  /* dessous */
        {{m.x,M.y,m.z},{m.x,M.y,M.z},{M.x,M.y,M.z},{M.x,M.y,m.z}},  /* dessus  */
        {{m.x,m.y,m.z},{m.x,m.y,M.z},{m.x,M.y,M.z},{m.x,M.y,m.z}},  /* X-      */
        {{M.x,m.y,m.z},{M.x,M.y,m.z},{M.x,M.y,M.z},{M.x,m.y,M.z}},  /* X+      */
        {{m.x,m.y,M.z},{M.x,m.y,M.z},{M.x,M.y,M.z},{m.x,M.y,M.z}},  /* Z+      */
        {{m.x,m.y,m.z},{m.x,M.y,m.z},{M.x,M.y,m.z},{M.x,m.y,m.z}}   /* Z-      */
    };
    static const double normals[6][3] = {
        {0,-1,0},{0,1,0},{-1,0,0},{1,0,0},{0,0,1},{0,0,-1}
    };
    glEnable(GL_LIGHTING);
    glBegin(GL_QUADS);
    for (int f = 0; f < 6; ++f) {
        glNormal3dv(normals[f]);
        if (selected) glColor3d(0.45, 0.7, 0.9);
        else glColor3dv(o.col);
        for (int k = 0; k < 4; ++k) glVertex3dv(q[f][k]);
    }
    glEnd();
    /* aretes : blanches si selectionne, sombres sinon — esprit SketchUp */
    glDisable(GL_LIGHTING);
    glBegin(GL_LINES);
    if (selected) { glColor3d(1, 1, 1); glLineWidth(2.0); }
    else glColor3d(0.2, 0.2, 0.22);
    for (int f = 0; f < 6; ++f)
        for (int k = 0; k < 4; ++k) {
            glVertex3dv(q[f][k]);
            glVertex3dv(q[f][(k + 1) % 4]);
        }
    glEnd();
    glLineWidth(1.0);
}

static void render() {
    glViewport(0, 0, g_winW, g_winH);
    glClearColor(0.12, 0.13, 0.16, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (g_winH > 0) setPerspective(kFovDeg, double(g_winW) / double(g_winH), 0.05, 1000.0);

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

    drawGrid(20, 1.0);
    for (size_t i = 0; i < g_objs.size(); ++i)
        drawBox(g_objs[i], (int)i == g_selected && g_mode != M_Drag);
}

/* Projection de l'axe selectionne : guide visuel d'un trait qui traverse
   la scene — allume seulement pendant un deplacement verrouille */
static void drawAxisGuide() {
    if (g_mode != M_Drag || g_selected < 0 || g_lockAxis < 0) return;
    const Vec3 p = g_objs[(size_t)g_selected].pos;
    const Vec3 a = axisVec(g_lockAxis);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.5);
    glBegin(GL_LINES);
    glColor3d(0.92, 0.75, 0.1);
    glVertex3d(p.x - a.x * 50, p.y - a.y * 50, p.z - a.z * 50);
    glVertex3d(p.x + a.x * 50, p.y + a.y * 50, p.z + a.z * 50);
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
    std::printf("Sketch3D natif — lot N2 (selection au clic + deplacement, verrouillage X/Y/Z)\n");
    buildScene();
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit a echoue\n"); return 1; }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWwindow* win = glfwCreateWindow(g_winW, g_winH, "Sketch3D — build N2", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "creation de fenetre impossible\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
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
