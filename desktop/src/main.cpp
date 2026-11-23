// Sketch3D — programme natif (transcription de la version web, fichier unique index.html)
// Lot N1 : fenetre + camera orbitale + grille + cube — equivalence du lot web 1.
// Rotation oblique : clic gauche glisser ; zoom : molette ; cadrage : F ; quitter : Echap.
//
// Rendu : OpenGL fixed-function (1.1) pour le lot N1 — zero dependance de chargement
// d'extensions. Le pipeline par shaders (PBR, photorealisme) arrive a un lot dedie ;
// le modele geometrique est deja en double precision (la, pas de Float32).

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <cstring>

/* ------------------------------------------------------------------ */
/* Petits outils mathematiques (double precision)                      */
/* ------------------------------------------------------------------ */

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(double k) const { return Vec3(x * k, y * k, z * k); }
    double norm() const { return std::sqrt(x * x + y * y + z * z); }
};

struct Camera {
    Vec3 target{0, 1, 0};
    double dist = 8.0;
    double theta = 0.8;   // azimut (tour autour de Y)
    double phi = 0.5;     // elevation
    Vec3 pos() const {
        double cy = dist * std::cos(phi);
        return Vec3(target.x + dist * std::sin(phi) * std::cos(theta),
                    target.y + cy,
                    target.z + dist * std::sin(phi) * std::sin(theta));
    }
};

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
    Vec3 f = tgt - eye; { const double l = f.norm(); if (l > 1e-12) f = f * (1.0 / l); }
    Vec3 up(0, 1, 0);
    Vec3 s = Vec3(f.y * up.z - f.z * up.y, f.z * up.x - f.x * up.z, f.x * up.y - f.y * up.x);
    { const double l = s.norm(); if (l > 1e-12) s = s * (1.0 / l); }
    Vec3 u = Vec3(s.y * f.z - s.z * f.y, s.z * f.x - s.x * f.z, s.x * f.y - s.y * f.x);
    const double m[16] = {
        s.x, u.x, -f.x, 0,
        s.y, u.y, -f.y, 0,
        s.z, u.z, -f.z, 0,
        -(s.x * eye.x + s.y * eye.y + s.z * eye.z),
        -(u.x * eye.x + u.y * eye.y + u.z * eye.z),
         (f.x * eye.x + f.y * eye.y + f.z * eye.z),
        1
    };
    glMultMatrixd(m);
}

/* ------------------------------------------------------------------ */
/* Etat interaction                                                    */
/* ------------------------------------------------------------------ */

static Camera g_cam;
static double g_prevX = 0, g_prevY = 0;
static bool g_orbiting = false;
static int g_winW = 1280, g_winH = 800;

static void frameAll() {
    g_cam.target = Vec3(0, 1, 0);
    g_cam.dist = 8.0;
}

static void onScroll(GLFWwindow*, double, double dy) {
    g_cam.dist *= std::pow(0.9, dy);
    if (g_cam.dist < 1.0) g_cam.dist = 1.0;
    if (g_cam.dist > 200.0) g_cam.dist = 200.0;
}

static void onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        g_orbiting = (action == GLFW_PRESS);
        glfwGetCursorPos(w, &g_prevX, &g_prevY);
    }
}

static void onKey(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
    if (key == GLFW_KEY_F) frameAll();
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
    /* axes : X rouge, Z bleu */
    glBegin(GL_LINES);
    glColor3d(0.85, 0.2, 0.2);
    glVertex3d(-span * step, 0, 0); glVertex3d(span * step, 0, 0);
    glColor3d(0.2, 0.2, 0.85);
    glVertex3d(0, 0, -span * step); glVertex3d(0, 0, span * step);
    glEnd();
}

static void drawCube(double s) {
    const double h = s / 2.0;
    const double q[6][4][3] = {
        {{-h,0,-h},{-h,0,h},{h,0,h},{h,0,-h}},          /* dessous */
        {{-h,s,-h},{h,s,-h},{h,s,h},{-h,s,h}},          /* dessus  */
        {{-h,0,-h},{-h,s,-h},{-h,s,h},{-h,0,h}},        /* X-      */
        {{h,0,-h},{h,0,h},{h,s,h},{h,s,-h}},            /* X+      */
        {{-h,0,h},{h,0,h},{h,s,h},{-h,s,h}},            /* Z+      */
        {{-h,0,-h},{-h,s,-h},{h,s,-h},{h,0,-h}}         /* Z-      */
    };
    const double c[6][3] = {
        {0.55,0.35,0.2},{0.75,0.48,0.28},{0.6,0.4,0.24},
        {0.6,0.4,0.24},{0.68,0.44,0.26},{0.68,0.44,0.26}
    };
    glEnable(GL_LIGHTING);
    glBegin(GL_QUADS);
    for (int f = 0; f < 6; ++f) {
        const double a[3] = {q[f][1][0]-q[f][0][0], q[f][1][1]-q[f][0][1], q[f][1][2]-q[f][0][2]};
        const double b[3] = {q[f][2][0]-q[f][0][0], q[f][2][1]-q[f][0][1], q[f][2][2]-q[f][0][2]};
        double n[3] = {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
        const double len = std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]) ;
        if (len > 1e-12) { n[0]/=len; n[1]/=len; n[2]/=len; }
        glNormal3dv(n);
        glColor3dv(c[f]);
        for (int k = 0; k < 4; ++k) glVertex3dv(q[f][k]);
    }
    glEnd();
    /* aretes blanches, esprit SketchUp */
    glDisable(GL_LIGHTING);
    glBegin(GL_LINES);
    glColor3d(1, 1, 1);
    for (int f = 0; f < 6; ++f)
        for (int k = 0; k < 4; ++k) {
            glVertex3dv(q[f][k]);
            glVertex3dv(q[f][(k + 1) % 4]);
        }
    glEnd();
}

static void render() {
    glViewport(0, 0, g_winW, g_winH);
    glClearColor(0.12, 0.13, 0.16, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (g_winH > 0) setPerspective(55.0, double(g_winW) / double(g_winH), 0.05, 1000.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    const Vec3 eye = g_cam.pos();
    gluLookAtImpl(eye, g_cam.target);

    /* lumiere fixe a la scene */
    const float pos[4] = {5.0f, 10.0f, 5.0f, 1.0f};
    const float amb[4] = {0.25f, 0.25f, 0.25f, 1.0f};
    const float dif[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);

    drawGrid(20, 1.0);
    drawCube(4.0);

    /* tapis au sol pour reperer la cible de la camera */
    glPointSize(6.0f);
    glBegin(GL_POINTS);
    glColor3d(0.2, 0.8, 0.3);
    glVertex3d(g_cam.target.x, 0.01, g_cam.target.z);
    glEnd();
}

/* ------------------------------------------------------------------ */
/* Programme principal                                                 */
/* ------------------------------------------------------------------ */

int main() {
    std::printf("Sketch3D natif — lot N1 (fenetre + camera orbitale + cube)\n");
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit a echoue\n"); return 1; }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWwindow* win = glfwCreateWindow(g_winW, g_winH, "Sketch3D — build N1", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "creation de fenetre impossible\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetScrollCallback(win, onScroll);
    glfwSetMouseButtonCallback(win, onMouseButton);
    glfwSetKeyCallback(win, onKey);
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow*, int w, int h) { g_winW = w; g_winH = h; });

    while (!glfwWindowShouldClose(win)) {
        if (g_orbiting) {
            double x, y;
            glfwGetCursorPos(win, &x, &y);
            g_cam.theta += (x - g_prevX) * 0.01;
            g_cam.phi += (y - g_prevY) * 0.01;
            if (g_cam.phi < 0.02) g_cam.phi = 0.02;
            if (g_cam.phi > 3.12) g_cam.phi = 3.12;
            g_prevX = x; g_prevY = y;
        }
        render();
        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
