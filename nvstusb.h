/* nvstusb.h Copyright (C) 2010 Bjoern Paetzel
 *
 * This program comes with ABSOLUTELY NO WARRANTY.
 * This is free software, and you are welcome to redistribute it
 * under certain conditions. See the file COPYING for details
 * */

struct nvstusb_context;

enum nvstusb_eye {
  nvstusb_left = 0,
  nvstusb_right
};

struct nvstusb_keys {
  char deltaWheel;
  int  toggled3D;
};

struct nvstusb_context *nvstusb_init(void (*swapfunc)());
void nvstusb_deinit(struct nvstusb_context *ctx);
void nvstusb_set_rate(struct nvstusb_context *ctx, int rate);
void nvstusb_swap(struct nvstusb_context *ctx, enum nvstusb_eye eye);
void nvstusb_get_keys(struct nvstusb_context *ctx, struct nvstusb_keys *keys);
