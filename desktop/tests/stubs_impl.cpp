#include <GLFW/glfw3.h>
#include <GL/gl.h>
#define UNUSED(x) (void)(x);
int glfwInit(void){return 1;} void glfwTerminate(void){}
void glfwWindowHint(int,int){UNUSED(0);} 
GLFWwindow* glfwCreateWindow(int,int,const char*,GLFWwindow*,GLFWwindow*){
  UNUSED(0); UNUSED(0); UNUSED((const int)0); return nullptr;}
void glfwMakeContextCurrent(GLFWwindow*){}
int glfwWindowShouldClose(GLFWwindow*){return 1;}
void glfwSetWindowShouldClose(GLFWwindow*,int){}
void glfwSwapBuffers(GLFWwindow*){}
void glfwSetWindowTitle(GLFWwindow*, const char*){UNUSED(0);}
/* B0 : position/taille de fenetre scriptables (remontees a la fermeture) */
int g_stubWinX = 100, g_stubWinY = 60, g_stubWinW2 = 1280, g_stubWinH2 = 800, g_stubWinMax = 0;
void glfwSetWindowPos(GLFWwindow*, int x, int y){g_stubWinX=x; g_stubWinY=y;}
void glfwGetWindowPos(GLFWwindow*, int* x, int* y){*x=g_stubWinX; *y=g_stubWinY;}
void glfwGetWindowSize(GLFWwindow*, int* w, int* h){*w=g_stubWinW2; *h=g_stubWinH2;}
void glfwMaximizeWindow(GLFWwindow*){g_stubWinMax=1;}
int glfwGetWindowAttrib(GLFWwindow*, int){return g_stubWinMax;}
void glfwPollEvents(){}
void glfwSwapInterval(int){}
/* curseur scriptable : position vue par l'application (le harnais et le
   simulateur d'evenements la pilotent ; 0 par defaut, jamais non initialise) */
double g_stubCursorX = 0.0, g_stubCursorY = 0.0;
void glfwGetCursorPos(GLFWwindow*,double* x,double* y){*x=g_stubCursorX;*y=g_stubCursorY;}
void glfwSetScrollCallback(GLFWwindow*,GLFWscrollfun){}
void glfwSetMouseButtonCallback(GLFWwindow*,GLFWmousebuttonfun){}
void glfwSetKeyCallback(GLFWwindow*,GLFWkeyfun){}
void glfwSetFramebufferSizeCallback(GLFWwindow*,GLFWframebuffersizefun){}
/* horloge scriptable : avancee par le simulateur (double-clic 0.3 s exerce) */
double g_stubTime = 0.0;
double glfwGetTime(void){return g_stubTime;}
/* B2.4e : simule une disposition AZERTY - la touche etiquetee A (position
   QWERTY du Q) arrive en GLFW_KEY_Q avec scancode 0x10, et son NOM est "A" ;
   nullptr ailleurs (comportement anterieur, repli par code). */
const char* glfwGetKeyName(int key, int scancode){
    static const char azertyA[] = "A";
    if (key == GLFW_KEY_Q && scancode == 0x10) return azertyA;
    (void)scancode;
    return nullptr;
}
void glBegin(GLenum){} void glEnd(void){}
void glVertex3d(GLdouble,GLdouble,GLdouble){}
void glVertex3dv(const GLdouble*){}
void glNormal3dv(const GLdouble*){}
void glColor3d(GLdouble,GLdouble,GLdouble){}
void glColor3dv(const GLdouble*){}
void glColor3f(GLfloat,GLfloat,GLfloat){}
void glEnable(GLenum){} void glDisable(GLenum){}
void glClear(unsigned int){}
void glClearColor(GLfloat,GLfloat,GLfloat,GLfloat){}
void glViewport(int,int,int,int){}
void glMatrixMode(GLenum){}
void glLoadIdentity(void){}
void glMultMatrixd(const GLdouble*){}
void glTranslated(GLdouble,GLdouble,GLdouble){}
void glLightfv(GLenum,GLenum,const GLfloat*){}
void glPointSize(GLfloat){}
void glLineWidth(GLfloat){}
void glPushMatrix(void){} void glPopMatrix(void){}
void glScaled(GLdouble,GLdouble,GLdouble){}

#ifdef _WIN32
#include <windows.h>
HWND glfwGetWin32Window(GLFWwindow*){return nullptr;}
#endif
void glOrtho(double,double,double,double,double,double){}
void glRasterPos2f(float,float){}
void glVertex2f(float,float){}
unsigned int glGenLists(int){return 1000;}
void glCallList(unsigned int){}
void glPolygonOffset(GLfloat, GLfloat){}
void glColor4f(GLfloat,GLfloat,GLfloat,GLfloat){}
void glBlendFunc(GLenum,GLenum){}
