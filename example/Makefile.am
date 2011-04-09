bin_PROGRAMS = example quadstereo
example_SOURCES = 3dv.c
example_CFLAGS = -I@top_srcdir@/include ${ILUT_CFLAGS} ${IL_CFLAGS}
example_LDADD = @top_builddir@/src/libnvstusb.la ${ILUT_LIBS} ${IL_LIBS} -lglut ${LIBUSB_LIBS} ${X11_LIBS}
quadstereo_SOURCES = quadstereo.c
quadstereo_CFLAGS = -I@top_srcdir@/include ${ILUT_CFLAGS} ${IL_CFLAGS}
quadstereo_LDADD = @top_builddir@/src/libnvstusb.la ${ILUT_LIBS} ${IL_LIBS} -lglut ${LIBUSB_LIBS} ${X11_LIBS}