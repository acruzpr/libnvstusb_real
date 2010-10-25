/* example program Copyright (C) 2010 Bjoern Paetzel
 *
 * This program comes with ABSOLUTELY NO WARRANTY.
 * This is free software, and you are welcome to redistribute it
 * under certain conditions. See the file COPYING for details
 * */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "nvstusb.h"

#include <GL/glut.h>

#define ILUT_USE_OPENGL
#include <IL/il.h>
#include <IL/ilu.h>
#include <IL/ilut.h>

#include<X11/Xlib.h>
#include<X11/extensions/xf86vmode.h>

ILuint image = 0;
GLuint texture = 0;

struct nvstusb_context *ctx = 0;
int eye = 0;
float depth = 0.0;
int inverteyes = 1;

extern float nvstusb_x;

void print_refresh_rate(void)
{
  static int i_it = 0;
  static uint64_t i_last = 0;

  if(i_it == 0) {
    struct timespec s_tmp;
    clock_gettime(CLOCK_REALTIME, &s_tmp);
    i_last = (double)s_tmp.tv_sec*1000.0+(double)s_tmp.tv_nsec/1000000.0;
  }
		
  if(i_it % 512 == 0) {
    struct timespec s_tmp;
    clock_gettime(CLOCK_REALTIME, &s_tmp);
    uint64_t i_new = (double)s_tmp.tv_sec*1000.0+(double)s_tmp.tv_nsec/1000000.0;
    printf("%f %f\n",1000.0/((float)(i_new-i_last)/(i_it)), (float)(i_new-i_last)-((1000/75)*(i_it)));
  }

  i_it++;
}


void draw() {
  glClearColor(0.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);

  float offset;
  if (inverteyes) {
    offset = 0.5*(1-eye);
  } else {
    offset = 0.5*eye;
  }

  float f = depth * (eye*2-1);
  
  glBegin(GL_QUADS);
  glTexCoord2f(0.0+offset, 0.0);
  glVertex2f(-1.0+f, -1.0);
  glTexCoord2f(0.5+offset, 0.0);
  glVertex2f(1.0+f, -1.0);
  glTexCoord2f(0.5+offset, 1.0);
  glVertex2f(1.0+f, 1.0);
  glTexCoord2f(0.0+offset, 1.0);
  glVertex2f(-1.0+f, 1.0);
  glEnd();

  glFinish();
  nvstusb_swap(ctx, eye, glutSwapBuffers);
  eye = 1-eye;

  struct nvstusb_keys k;
  nvstusb_get_keys(ctx, &k);
  if (k.toggled3D) {
    inverteyes = !inverteyes;
  }

  if (k.deltaWheel) {
    depth += 0.01 * k.deltaWheel;
  }
}

void drawNoImage() {
  glClearColor(0.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);

  glBegin(GL_QUADS);
  if (eye == nvstusb_left) {
    glVertex2f(-1.0, -1.0);
    glVertex2f( 0.0, -1.0);
    glVertex2f( 0.0,  1.0);
    glVertex2f(-1.0,  1.0);
  } else {
    glVertex2f( 0.0, -1.0);
    glVertex2f( 1.0, -1.0);
    glVertex2f( 1.0,  1.0);
    glVertex2f( 0.0,  1.0);
  }
  glEnd();

  glFinish();
  nvstusb_swap(ctx, eye, glutSwapBuffers);
  eye = 1-eye;

  struct nvstusb_keys k;
  nvstusb_get_keys(ctx, &k);
  if (k.toggled3D) {
    inverteyes = !inverteyes;
  }

  if (k.deltaWheel) {
    depth += 0.01 * k.deltaWheel;
  }
}

int main(int argc, char **argv) {

  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGB|GLUT_DOUBLE);
  
  ctx = nvstusb_init();

  if (0 == ctx) {
    fprintf(stderr, "could not initialize NVIDIA 3D Stereo Controller, aborting\n");
    exit(EXIT_FAILURE);
  }

  /* Get Vsync rate from X11 */
  {
    static Display *dpy;
    dpy = XOpenDisplay(0);
    double displayNumber=DefaultScreen(dpy);
    XF86VidModeModeLine modeline;
    int pixelclock;
    XF86VidModeGetModeLine( dpy, displayNumber, &pixelclock, &modeline );
    double frameRate=(double) pixelclock*1000/modeline.htotal/modeline.vtotal;
    printf("Vertical Refresh rate:%f Hz\n",frameRate);
    nvstusb_set_rate(ctx, frameRate);
  }
  
  if (argc > 1) {
    ilInit();
    ilGenImages(1, &image);
    ilBindImage(image);

    printf("%s\n", argv[1]);
    ilLoad(IL_JPG, argv[1]);

    float w = ilGetInteger(IL_IMAGE_WIDTH)/2;
    float h = ilGetInteger(IL_IMAGE_HEIGHT);
    float aspect = w/h;

    glutInitWindowSize(512*aspect, 512);

    glutCreateWindow("3dv");
    glutIdleFunc(draw);
  
    ilutRenderer(ILUT_OPENGL);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    ilutGLBindTexImage();
    ilutGLBuildMipmaps();
    glEnable(GL_TEXTURE);

    glutMainLoop();

    argv++;
    argc--;
  } else {
    glutInitWindowSize(512, 512);

    glutCreateWindow("3dv");
    glutIdleFunc(drawNoImage);

    glutMainLoop();
  }
  nvstusb_deinit(ctx);
  return EXIT_SUCCESS;
}
