#pragma once
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef double GLdouble;
#define GL_TRIANGLES 0x0004
#define GL_QUADS 0x0007
#define GL_LINES 0x0001
#define GL_POINTS 0x0000
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_LIGHTING 0x0B50
#define GL_LIGHT0 0x4000
#define GL_POSITION 0x1203
#define GL_AMBIENT 0x1200
#define GL_DIFFUSE 0x1201
#define GL_DEPTH_TEST 0x0B71
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
void glBegin(GLenum);
void glEnd(void);
void glVertex3d(GLdouble, GLdouble, GLdouble);
void glVertex3dv(const GLdouble*);
void glNormal3dv(const GLdouble*);
void glColor3d(GLdouble, GLdouble, GLdouble);
void glColor3dv(const GLdouble*);
void glColor3f(GLfloat, GLfloat, GLfloat);
void glEnable(GLenum);
void glDisable(GLenum);
void glClear(GLuint);
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat);
void glViewport(GLint, GLint, GLsizei, GLsizei);
void glMatrixMode(GLenum);
void glLoadIdentity(void);
void glMultMatrixd(const GLdouble*);
void glTranslated(GLdouble, GLdouble, GLdouble);
void glLightfv(GLenum, GLenum, const GLfloat*);
void glPointSize(GLfloat);
void glLineWidth(GLfloat);
void glPushMatrix(void);
void glPopMatrix(void);
void glScaled(GLdouble, GLdouble, GLdouble);
