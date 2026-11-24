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
void glfwPollEvents(){}
void glfwSwapInterval(int){}
void glfwGetCursorPos(GLFWwindow*,double*,double*){}
void glfwSetScrollCallback(GLFWwindow*,GLFWscrollfun){}
void glfwSetMouseButtonCallback(GLFWwindow*,GLFWmousebuttonfun){}
void glfwSetKeyCallback(GLFWwindow*,GLFWkeyfun){}
void glfwSetFramebufferSizeCallback(GLFWwindow*,GLFWframebuffersizefun){}
double glfwGetTime(void){return 0.0;}
const char* glfwGetKeyName(int,int){return nullptr;}
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
