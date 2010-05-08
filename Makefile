#vim:noet

CFLAGS=-I/usr/include/libusb-1.0
LDFLAGS=-lusb-1.0 -lglut -lnvstusb -L. -lIL -lILUT 

all: extractfw libnvstusb.a 3dv

extractfw: extractfw.c
	gcc extractfw.c -o extractfw

libnvstusb.a: nvstusb.o
	ar rsv libnvstusb.a nvstusb.o

nvstusb.o: nvstusb.c
	gcc $(CFLAGS) -c nvstusb.c -o nvstusb.o

3dv: 3dv.c libnvstusb.a
	gcc main.c $(LDFLAGS) -o 3dv
