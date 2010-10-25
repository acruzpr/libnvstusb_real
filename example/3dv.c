/* example program Copyright (C) 2010 Bjoern Paetzel
 *
 * This program comes with ABSOLUTELY NO WARRANTY.
 * This is free software, and you are welcome to redistribute it
 * under certain conditions. See the file COPYING for details
 * */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "nvstusb.h"

#include <GL/glut.h>

#include <IL/il.h>
#include <IL/ilu.h>
#include <IL/ilut.h>

ILuint image = 0;
GLuint texture = 0;

struct nvstusb_context *ctx = 0;
int eye = 0;
float depth = 0.0;
int inverteyes = 1;

extern float nvstusb_x;

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
  nvstusb_swap(ctx, eye);
  eye = 1-eye;

  struct nvstusb_keys k;
  nvstusb_get_keys(ctx, &k);
  if (k.toggled3D) {
    inverteyes = !inverteyes;
  }

  if (k.deltaWheel) {
    depth += 0.01 * k.deltaWheel;

    nvstusb_x += 0.1 * k.deltaWheel;
    printf("%f\n", nvstusb_x);
    nvstusb_set_rate(ctx, 120);
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
  nvstusb_swap(ctx, eye);
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
  
  ctx = nvstusb_init(glutSwapBuffers);

  if (0 == ctx) {
    fprintf(stderr, "could not initialize NVIDIA 3D Stereo Controller, aborting\n");
    exit(EXIT_FAILURE);
  }

  nvstusb_set_rate(ctx, 120);
  
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
