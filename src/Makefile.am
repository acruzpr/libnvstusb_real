lib_LTLIBRARIES = libnv3dvision.la
libnv3dvision_la_SOURCES = nvstusb.c usb_libusb.c
libnv3dvision_la_CPPFLAGS = -I@top_srcdir@/include ${LIBUSB_CFLAGS}
libnv3dvision_la_LIBS = ${LIBUSB_LIBS} ${X11_LIBS}
libnv3dvision_HEADERS = @top_srcdir@/include/usb.h @top_srcdir@/include/nvstusb.h
