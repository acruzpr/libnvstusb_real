/* nvstusb.c Copyright (C) 2010 Bjoern Paetzel
 *
 * This program comes with ABSOLUTELY NO WARRANTY.
 * This is free software, and you are welcome to redistribute it
 * under certain conditions. See the file COPYING for details
 * */

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>
#include <libusb.h>
#include <time.h>

#include <GL/gl.h>
#include <GL/glx.h>

#ifndef _WIN32
#include <GL/glx.h>
#include <GL/glxext.h>

static PFNGLXGETVIDEOSYNCSGIPROC glXGetVideoSyncSGI = 0;
static PFNGLXWAITVIDEOSYNCSGIPROC glXWaitVideoSyncSGI = 0;

static PFNGLXSWAPINTERVALSGIPROC xSwapInterval = 0;
#else
static PFNWGLSWAPINTERVALEXTPROC xSwapInterval = 0;
#endif



#include "nvstusb.h"

#define NVSTUSB_CLOCK 24000000LL
#define NVSTUSB_CONST_ORIGINAL 0

#if NVSTUSB_CONST_ORIGINAL
/* these create roughly the same values as the original driver does */
#define NVSTUSB_CONST_X     (0.053520)  /* ? */
#define NVSTUSB_CONST_PHASE (0.232200)  /* phase */
#define NVSTUSB_CONST_DARK  (0.286090)  /* off time */
#define NVSTUSB_CONST_Y     (0.481768)  /* ? */
#else
/* these have been tuned to look correct */
#define NVSTUSB_CONST_X     (0.053520)  /* ? */
#define NVSTUSB_CONST_PHASE (0.300000)  /* phase */
#define NVSTUSB_CONST_DARK  (0.298100)  /* off time */
#define NVSTUSB_CONST_Y     (0.482100)  /* ? */
#endif

#define NVSTUSB_CMD_1           (0x01)
#define NVSTUSB_CMD_GETSTATUS   (0x02)
#define NVSTUSB_CMD_CLEAR       (0x40)

/* state of the controller */
struct nvstusb_context {
  /* currently selected refresh rate */
  int rate;

  /* currently active eye */
  enum nvstusb_eye eye;

  /* libusb connection */
  struct libusb_context *usbctx;

  /* device handle */
  struct libusb_device_handle *handle;

  /* swap function */
  void (*swap)();
};

/* convert a libusb error to a readable string */
static const char *
libusb_error_to_string(
  enum libusb_error error
) {
  switch(error) {
    case LIBUSB_SUCCESS:              return "Success (no error)";
    case LIBUSB_ERROR_IO:             return "Input/output error";
    case LIBUSB_ERROR_INVALID_PARAM:  return "Invalid parameter";
    case LIBUSB_ERROR_ACCESS:         return "Access denied (insufficient permissions)";
    case LIBUSB_ERROR_NO_DEVICE:      return "No such device(it may have been disconnected)";
    case LIBUSB_ERROR_NOT_FOUND:      return "Entity not found";
    case LIBUSB_ERROR_BUSY:           return "Resource busy";
    case LIBUSB_ERROR_TIMEOUT:        return "Operation timed out";
    case LIBUSB_ERROR_OVERFLOW:       return "Overflow";
    case LIBUSB_ERROR_PIPE:           return "Pipe error";
    case LIBUSB_ERROR_INTERRUPTED:    return "System call interrupted (perhaps due to signal)";
    case LIBUSB_ERROR_NO_MEM:         return "Insufficient memory";
    case LIBUSB_ERROR_NOT_SUPPORTED:  return "Operation not supported or unimplemented on this platform";
    case LIBUSB_ERROR_OTHER:          return "Other error";
  }
  return "Unknown error";
}

/* initialize usb */
static struct libusb_context *
nvstusb_init_usb(
  int debug
) {
  struct libusb_context *ctx = 0;
  libusb_init(&ctx);
  if (0 == ctx) {
    fprintf(stderr, "nvstusb: Could not initialize libusb\n");
    return 0;
  }

  libusb_set_debug(ctx, debug);
  fprintf(stderr, "nvstusb: libusb initialized, debug level %d\n", debug);
  return ctx;
}

/* shutdown usb */
static void
nvstusb_deinit_usb(
  struct libusb_context *ctx
) {
  if (ctx) return;
  libusb_exit(ctx);
  fprintf(stderr, "nvstusb: libusb deinitialized\n");
}

/* send data to an endpoint, bulk transfer */
static int
nvstusb_write_usb_bulk(
  struct libusb_device_handle *handle,
  int endpoint,
  void *data,
  int size
) {
  int sent = 0;
  return libusb_bulk_transfer(handle, endpoint | LIBUSB_ENDPOINT_OUT, data, size, &sent, 0);
}

/* send data to an endpoint, interrupt transfer */
static int
nvstusb_write_usb_interrupt(
  struct libusb_device_handle *handle,
  int endpoint,
  void *data,
  int size
) {
  int sent = 0;
  return libusb_interrupt_transfer(handle, endpoint | LIBUSB_ENDPOINT_OUT, data, size, &sent, 0);
}

/* receive data from an endpoint */
static int
nvstusb_read_usb(
  struct libusb_device_handle *handle,
  int endpoint,
  void *data,
  int size
) {
  int recvd = 0;
  int res = libusb_bulk_transfer(handle, endpoint | LIBUSB_ENDPOINT_IN,  data, size, &recvd, 0);
  /*
  int i;
  fprintf(stderr, "tmpRes = %d\n", recvd); 
  for (i = 0; i<recvd; i++) {
    fprintf(stderr, " %02x", ((unsigned char*)data)[i]);
  }
  fprintf(stderr, "\n");

  if (res < 0) return res;*/
  return recvd;
}

/* upload firmware file */
static int
nvstusb_load_firmware(
  struct libusb_device_handle *handle,
  const char *filename
) {
  FILE *fw = fopen(filename, "rb");
  if (!fw) { perror(filename); return LIBUSB_ERROR_OTHER; }
  
  fprintf(stderr, "nvstusb: Loading firmware...\n");

  uint8_t lenPos[4];
  uint8_t buf[1024];

  while(fread(lenPos, 4, 1, fw) == 1) {
    uint16_t length = (lenPos[0]<<8) | lenPos[1];
    uint16_t pos    = (lenPos[2]<<8) | lenPos[3];
  
    if (fread(buf, length, 1, fw) != 1) { 
      perror(filename); 
      return LIBUSB_ERROR_OTHER; 
    }

    int res = libusb_control_transfer(
      handle,
      LIBUSB_REQUEST_TYPE_VENDOR, 
      0xA0, /* 'Firmware load' */
      pos, 0x0000,
      buf, length,
      0
    );
    if (res < 0) {
      fprintf(stderr, "nvstusb: Error uploading firmware... Error %d: %s\n", res, libusb_error_to_string(res));
      return res;
    }
  }

  fclose(fw);
  return 0;
}

/* get the number of endpoints on a device */
static int
nvstusb_get_numendpoints(
  struct libusb_device_handle *handle
) {
  struct libusb_device *dev = libusb_get_device(handle);
  struct libusb_config_descriptor *cfgDesc;
  int res = libusb_get_active_config_descriptor(dev, &cfgDesc);
  if (res < 0) {
    fprintf(stderr, "nvstusb: Could not determine the number of endpoints... Error %d: %s\n", res, libusb_error_to_string(res));
    return res;
  }

  int num = cfgDesc->interface->altsetting->bNumEndpoints;
  libusb_free_config_descriptor(cfgDesc);
  fprintf(stderr, "nvstusb: Found %d endpoints...\n", num);
  return num;
}

/* open 3d controller device */
static struct libusb_device_handle *
nvstusb_open_device(
  struct libusb_context *ctx
) {
  int res;
  struct libusb_device_handle *handle = 
    libusb_open_device_with_vid_pid(ctx, 0x0955, 0x0007);

  if (0 == handle) {
    fprintf(stderr, "nvstusb: No NVIDIA 3d stereo controller found...\n");
    return 0;
  }

  fprintf(stderr, "nvstusb: Found NVIDIA 3d stereo controller...\n");

  /* figure out if firmware is already loaded */
  int endpoints = nvstusb_get_numendpoints(handle);
  if (endpoints < 0) {
    libusb_close(handle);
    return 0;
  } 

  /* if it's not, try to load it */
  if (endpoints == 0) {
    res = nvstusb_load_firmware(handle, "nvstusb.fw");
    libusb_reset_device(handle);
    libusb_close(handle);
    if (res < 0) {
      return 0;
    }
    handle = 0;
    while (0 == handle) {
      usleep(1000);
      handle = libusb_open_device_with_vid_pid(ctx, 0x0955, 0x0007);
    }
  } else {
    fprintf(stderr, "nvstusb: Firmware already loaded...\n");
    libusb_reset_device(handle);
  }

  /* choose configuration 1 */
  res = libusb_set_configuration(handle, 1);
  if (res < 0) {
    fprintf(stderr, "nvstusb: Error choosing configuration... Error %d: %s\n", res, libusb_error_to_string(res));
    libusb_close(handle);
    return 0;
  }
  fprintf(stderr, "nvstusb: Configuration set\n");

  /* claim interface 0 */
  res = libusb_claim_interface(handle, 0);
  if (res < 0) {
    fprintf(stderr, "nvstusb: Could not claim interface... Error %d: %s\n", res, libusb_error_to_string(res));
  }
  fprintf(stderr, "nvstusb: Interface claimed\n");
  
  return handle;
}

/* initialize controller */
struct nvstusb_context *
nvstusb_init(
  void (*swapfunc)()
) {
  /* check argument */
  if (0 == swapfunc) {
    fprintf(stderr, "nvstusb: no swap function supplied!\n");
    return 0;
  }
  
  /* initialize usb */
  struct libusb_context *usbctx = nvstusb_init_usb(3);
  if (0 == usbctx) return 0;
  
  /* open device */
  struct libusb_device_handle *handle = nvstusb_open_device(usbctx);
  if (0 == handle) return 0;

  /* allocate context */
  struct nvstusb_context *ctx = malloc(sizeof(*ctx));
  if (0 == ctx) {
    fprintf(stderr, "nvstusb: Could not allocate %u bytes for nvstusb_context...\n", sizeof(*ctx));
    libusb_close(handle);
    libusb_exit(usbctx);
    return 0;
  }
  ctx->rate = 0;
  ctx->eye = 0;
  ctx->usbctx = usbctx;
  ctx->handle = handle;
  ctx->swap = swapfunc;

#ifndef _WIN32
  glXGetVideoSyncSGI = (PFNGLXGETVIDEOSYNCSGIPROC)glXGetProcAddress("glXGetVideoSyncSGI");
  glXWaitVideoSyncSGI = (PFNGLXWAITVIDEOSYNCSGIPROC)glXGetProcAddress("glXWaitVideoSyncSGI");
  if (0 == glXWaitVideoSyncSGI) {
    glXGetVideoSyncSGI = 0;
  }

  if (0 != glXGetVideoSyncSGI ) {
    fprintf(stderr, "nvstusb: GLX_SGI_video_sync supported!\n");
  }

  xSwapInterval = (PFNGLXSWAPINTERVALSGIPROC)glXGetProcAddress("glXSwapIntervalSGI");
#else
  xSwapInterval = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
#endif

  if (0 != xSwapInterval) {
    fprintf(stderr, "nvstusb: forcing vsync\n");
    xSwapInterval(1);
  }

  return ctx;
}

/* deinitialize controller */
void
nvstusb_deinit(
  struct nvstusb_context *ctx
) {
  if (0 == ctx) return;

  /* close device */
  if (0 != ctx->handle) libusb_close(ctx->handle);
  ctx->handle = 0;
  
  /* close usb */
  if (0 != ctx->usbctx) nvstusb_deinit_usb(ctx->usbctx);
  ctx->usbctx = 0;

  /* free context */
  memset(ctx, 0, sizeof(*ctx));
  free(ctx);
}

/* set controller refresh rate (should be monitor refresh rate) */
void
nvstusb_set_rate(
  struct nvstusb_context *ctx,
  int rate
) {
  if (0 == ctx || 0 == ctx->handle) return;
  
  /* send some magic data to device, this function is mainly black magic */

  /* some timing voodoo */
  uint32_t t = NVSTUSB_CLOCK / rate;
  uint32_t w = t * NVSTUSB_CONST_X;
  uint32_t x = t * NVSTUSB_CONST_PHASE;
  uint32_t y = t * NVSTUSB_CONST_DARK;
  uint32_t z = t * NVSTUSB_CONST_Y;
  
  fprintf(stderr, "nvstusb: %08x %08x %08x %08x %08x\n", t,w,x,y,z);
  
  uint8_t cmdTimings[] = { 
    0x03,                   /* set? */
    0x00,                   /* 0 == timings? */
    0x18, 0x00,             /* 24 bytes follow */

    w, w>>8, ~w>>16, ~w>>24, 
    x, x>>8, ~x>>16, ~x>>24, 
    y, y>>8, ~y>>16, ~y>>24, 
    0x30, 0x28, 0x24, 0x22,
    0x0a, 0x08, 0x05, 0x04,
    z, z>>8, ~z>>16, ~z>>24
  }; 
  nvstusb_write_usb_bulk(ctx->handle, 2, cmdTimings, sizeof(cmdTimings));

  uint8_t tmp[64];
  int tmpRes = nvstusb_read_usb(ctx->handle, 4, tmp, 64);


  uint8_t cmd0x1c[] = {
    0x01,                   /* set? */
    0x1c,                   /* 28 == ?? */
    0x02, 0x00,             /* 2 bytes follow */
    
    0x02, 0x00              /* ?? */
  };
  nvstusb_write_usb_bulk(ctx->handle, 2, cmd0x1c, sizeof(cmd0x1c));
    
  uint8_t cmdRate[] = {
    0x01,                   /* set? */
    0x1e,                   /* 30 == rate */
    0x02, 0x00,             /* 2 bytes follow */

    rate<<1, rate>>7        /* rate * 2 */
  };
  nvstusb_write_usb_bulk(ctx->handle, 2, cmdRate, sizeof(cmdRate));

  uint8_t cmd0x1b[] = {
    0x01,                   /* set? */
    0x1b,                   /* 29 == ?? */
    0x01, 0x00,             /* 1 byte follows */
    
    0x07                    /* ?? */
  };
  nvstusb_write_usb_bulk(ctx->handle, 2, cmd0x1b, sizeof(cmd0x1b));
    
  ctx->rate = rate;
}

static inline void
nvstusb_diff_timespec(
  const struct timespec *a,
  const struct timespec *b,
  struct timespec *c
) {
  c->tv_sec = a->tv_sec - b->tv_sec;
  if (a->tv_nsec < b->tv_nsec) {
    c->tv_nsec = a->tv_nsec + 1000000000L - b->tv_nsec;
    c->tv_sec--;
  } else {
    c->tv_nsec = a->tv_nsec - b->tv_nsec;
  }
}

static uint64_t
nvstusb_get_ticks(
) {
  struct timeval tv;
  gettimeofday(&tv, 0);
  return tv.tv_sec*1000000 + tv.tv_usec;
}

/* set currently open eye */
static void
nvstusb_set_eye(
  struct nvstusb_context *ctx,
  enum nvstusb_eye eye,
  uint32_t r
) {
  if (0 == ctx || 0 == ctx->handle) return;

  uint8_t buf[8] = { 
    0xAA, (eye%2)?0xFE:0xFF,  /* set shutter state */
    0x00, 0x00,               /* unused? */
    r, r>>8, ~r>>16, ~r>>24
  };
  nvstusb_write_usb_bulk(ctx->handle, 1, buf, 8);
}

void
nvstusb_swap(
  struct nvstusb_context *ctx,
  enum nvstusb_eye eye
) {
  if (0 == ctx || 0 == ctx->handle) return;

#ifndef _WIN32
  if (0 != glXGetVideoSyncSGI) {
    unsigned int count;
    glXGetVideoSyncSGI(&count);
    glXWaitVideoSyncSGI(2, (count+1)%2, &count);
    nvstusb_set_eye(ctx, eye, 0);
    ctx->swap();
    return;
  }
#endif

  uint8_t pixels[4] = { 255, 0, 255, 255 };
 
  ctx->swap();

  /* read from front buffer, this operation can only finish after 
   * swapping is complete. (won't work if page flipping is disabled) */
  glReadBuffer(GL_FRONT);
  glReadPixels(1,1,1,1,GL_RGB, GL_UNSIGNED_BYTE, pixels);
  nvstusb_set_eye(ctx, eye, 0);
}

/* get key status from controller */
void
nvstusb_get_keys(
  struct nvstusb_context *ctx,
  struct nvstusb_keys *keys
) {
  uint8_t cmd1[] = { 
    NVSTUSB_CMD_GETSTATUS | NVSTUSB_CMD_CLEAR,
    0x18, 0x03, 0x00 };
  nvstusb_write_usb_bulk(ctx->handle, 2, cmd1, sizeof(cmd1));

  uint8_t readBuf[7];
  nvstusb_read_usb(ctx->handle, 4, readBuf, sizeof(readBuf));

  keys->deltaWheel = readBuf[4];
  keys->toggled3D  = readBuf[6] & 0x01;
}

