bin_PROGRAMS = example nvstusb-quad
example_SOURCES = 3dv.c
example_CFLAGS = -I@top_srcdir@/include ${ILUT_CFLAGS} ${IL_CFLAGS}
example_LDADD = @top_builddir@/src/libnvstusb.la ${ILUT_LIBS} ${IL_LIBS} -lglut ${LIBUSB_LIBS} ${X11_LIBS}
nvstusb_quad_SOURCES = nvstusb_quad.c
nvstusb_quad_CFLAGS = -I@top_srcdir@/include ${ILUT_CFLAGS} ${IL_CFLAGS}
nvstusb_quad_LDADD = @top_builddir@/src/libnvstusb.la ${ILUT_LIBS} ${IL_LIBS} -lglut ${LIBUSB_LIBS} ${X11_LIBS}