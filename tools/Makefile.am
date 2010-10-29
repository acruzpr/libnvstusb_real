bin_PROGRAMS = extractfw test_vsync
extractfw_SOURCES = extractfw.c
extractfw_CFLAGS = -I@top_srcdir@/include 
test_vsync_SOURCES = test_vsync.c
test_vsync_CFLAGS = -I@top_srcdir@/include ${GL_CFLAGS}
test_vsync_LDADD = ${GL_LIBS} -lglut
