#vim:noet

CFLAGS=-I/usr/include/libusb-1.0 -ggdb
LDFLAGS=-lusb-1.0 -lglut -lnvstusb -L. -lIL -lILUT 

all: extractfw libnvstusb.a 3dv

extractfw: extractfw.c
	gcc extractfw.c -o extractfw

libnvstusb.a: nvstusb.o usb_libusb.o
	ar rsv libnvstusb.a $^

%.o: %.c
	gcc $(CFLAGS) -c $< -o $@

3dv: 3dv.c libnvstusb.a
	gcc $^ $(LDFLAGS) -o $@
