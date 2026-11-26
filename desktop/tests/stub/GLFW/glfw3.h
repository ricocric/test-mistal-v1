/* Stub GLFW minimal pour le harnais de tests (aucune librairie reelle requise). */
#pragma once
#include <cstddef>
#include <GL/gl.h>
struct GLFWwindow;
typedef struct GLFWwindow GLFWwindow;
typedef void (*GLFWscrollfun)(GLFWwindow*, double, double);
typedef void (*GLFWmousebuttonfun)(GLFWwindow*, int, int, int);
typedef void (*GLFWkeyfun)(GLFWwindow*, int, int, int, int);
typedef void (*GLFWframebuffersizefun)(GLFWwindow*, int, int);
#define GLFW_TRUE  1
#define GLFW_MAXIMIZED 0x00020004
#define GLFW_FALSE 0
#define GLFW_PRESS    1
#define GLFW_RELEASE  0
#define GLFW_REPEAT   2
#define GLFW_KEY_UNKNOWN     -1
#define GLFW_MOD_CONTROL     0x0002
#define GLFW_MOD_SHIFT       0x0001
#define GLFW_KEY_BACKSPACE   259
#define GLFW_KEY_ENTER       257
#define GLFW_KEY_ESCAPE      256
#define GLFW_KEY_COMMA       44
#define GLFW_KEY_MINUS       45
#define GLFW_KEY_PERIOD      46
#define GLFW_KEY_SEMICOLON   59
#define GLFW_KEY_KP_SUBTRACT 333
#define GLFW_KEY_KP_ENTER    335
#define GLFW_KEY_KP_DECIMAL  330
#define GLFW_KEY_KP_0        320
#define GLFW_KEY_KP_2        322
#define GLFW_KEY_KP_5        325
#define GLFW_KEY_KP_9        329
#define GLFW_KEY_0           48
#define GLFW_KEY_9           57
#define GLFW_KEY_F 70
#define GLFW_KEY_B 66
#define GLFW_KEY_C 67
#define GLFW_KEY_V 86
#define GLFW_KEY_H 72
#define GLFW_KEY_I 73
#define GLFW_KEY_L 76
#define GLFW_KEY_O 79
#define GLFW_KEY_P 80
#define GLFW_KEY_R 82
#define GLFW_KEY_S 83
#define GLFW_KEY_X 88
#define GLFW_KEY_DELETE 261
#define GLFW_KEY_Y 89
#define GLFW_KEY_Z 90
#define GL_LINE_STRIP 0x0003
#define GLFW_MOUSE_BUTTON_LEFT  0
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GLFW_MOUSE_BUTTON_MIDDLE 2
#define GLFW_CLIENT_API            0x00022001
#define GLFW_OPENGL_API            0x00030001
#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003
#define GLFW_SAMPLES               0x0002100D
int glfwInit(void);
void glfwTerminate(void);
void glfwWindowHint(int, int);
GLFWwindow* glfwCreateWindow(int, int, const char*, GLFWwindow*, GLFWwindow*);
void glfwMakeContextCurrent(GLFWwindow*);
int glfwWindowShouldClose(GLFWwindow*);
void glfwSetWindowShouldClose(GLFWwindow*, int);
void glfwSwapBuffers(GLFWwindow*);
void glfwSetWindowTitle(GLFWwindow*, const char*);
/* B0 : fonctions fenetre */
void glfwSetWindowPos(GLFWwindow*, int, int);
void glfwGetWindowPos(GLFWwindow*, int*, int*);
void glfwGetWindowSize(GLFWwindow*, int*, int*);
void glfwMaximizeWindow(GLFWwindow*);
int glfwGetWindowAttrib(GLFWwindow*, int);
void glfwPollEvents(void);
void glfwSwapInterval(int);
void glfwGetCursorPos(GLFWwindow*, double*, double*);
void glfwSetScrollCallback(GLFWwindow*, GLFWscrollfun);
void glfwSetMouseButtonCallback(GLFWwindow*, GLFWmousebuttonfun);
void glfwSetKeyCallback(GLFWwindow*, GLFWkeyfun);
void glfwSetFramebufferSizeCallback(GLFWwindow*, GLFWframebuffersizefun);
const char* glfwGetKeyName(int, int);
double glfwGetTime(void);
