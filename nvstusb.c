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
#include <time.h>
#include <assert.h>
#include <libusb.h>

#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include "nvstusb.h"

static PFNGLXGETVIDEOSYNCSGIPROC glXGetVideoSyncSGI = 0;
static PFNGLXWAITVIDEOSYNCSGIPROC glXWaitVideoSyncSGI = 0;
static PFNGLXSWAPINTERVALSGIPROC glXSwapInterval = 0;

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

#define NVSTUSB_CMD_WRITE       (0x01)  /* write data */
#define NVSTUSB_CMD_READ        (0x02)  /* read data */
#define NVSTUSB_CMD_CLEAR       (0x40)  /* set data to 0 */

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
  assert(handle != 0);
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
  assert(handle != 0);
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
  int res;
  
  assert(handle != 0);
  
  res = libusb_bulk_transfer(handle, endpoint | LIBUSB_ENDPOINT_IN,  data, size, &recvd, 0);
  return recvd;
}

/* upload firmware file */
static int
nvstusb_load_firmware(
  struct libusb_device_handle *handle,
  const char *filename
) {
  assert(handle != 0);

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
  assert(handle != 0);

  struct libusb_device *dev = libusb_get_device(handle);
  struct libusb_config_descriptor *cfgDesc = 0;
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
  assert(ctx != 0);

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
    if (res != 0) {
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
  
  glXGetVideoSyncSGI = (PFNGLXGETVIDEOSYNCSGIPROC)glXGetProcAddress("glXGetVideoSyncSGI");
  glXWaitVideoSyncSGI = (PFNGLXWAITVIDEOSYNCSGIPROC)glXGetProcAddress("glXWaitVideoSyncSGI");
  if (0 == glXWaitVideoSyncSGI) {
    glXGetVideoSyncSGI = 0;
  }

  if (0 != glXGetVideoSyncSGI ) {
    fprintf(stderr, "nvstusb: GLX_SGI_video_sync supported!\n");
  }

  glXSwapInterval = (PFNGLXSWAPINTERVALSGIPROC)glXGetProcAddress("glXSwapIntervalSGI");

  if (0 != glXSwapInterval) {
    fprintf(stderr, "nvstusb: forcing vsync\n");
    glXSwapInterval(1);
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
  assert(ctx != 0);
  assert(ctx->handle != 0);
  assert(rate > 60);
  
  /* send some magic data to device, this function is mainly black magic */

  /* some timing voodoo */
  uint32_t t = NVSTUSB_CLOCK / rate;
  uint32_t w = t * NVSTUSB_CONST_X;
  uint32_t x = t * NVSTUSB_CONST_PHASE;
  uint32_t y = t * NVSTUSB_CONST_DARK;
  uint32_t z = t * NVSTUSB_CONST_Y;
  
  // fprintf(stderr, "nvstusb: %08x %08x %08x %08x %08x\n", t,w,x,y,z);
  
  uint8_t cmdTimings[] = { 
    NVSTUSB_CMD_WRITE,      /* write data */
    0x00,                   /* to address 0x2007 (0x2007+0x00) = ?? */
    0x18, 0x00,             /* 24 bytes follow */

    w, w>>8, ~w>>16, ~w>>24,  /* 2007: ?? */ 
    x, x>>8, ~x>>16, ~x>>24,  /* 200b: ?? */
    y, y>>8, ~y>>16, ~y>>24,  /* 200f: ?? */
    0x30, 0x28, 0x24, 0x22,   /* 2013: ?? */
    0x0a, 0x08, 0x05, 0x04,   /* 2017: ?? */
    z, z>>8,                  /* 201b: timer 2 reload value */
    ~z>>16, ~z>>24            /* 201d: stored at 0x67, 0x68*/
  }; 
  nvstusb_write_usb_bulk(ctx->handle, 2, cmdTimings, sizeof(cmdTimings));

  /* timer 2 reload value in the snoop was 0x7961 = 31073
   * if i understand the trm correctly, timer 2 is clocked by T2 pin at an unknown rate.
   * when there is a high to low transition on T2EX, the current timer 
   * value is saved to or loaded from RCAP2H/L depending on CPRL2 and
   * a timer interrupt is issued.
   * */

  uint8_t cmd0x1c[] = {
    NVSTUSB_CMD_WRITE,      /* write data */
    0x1c,                   /* to address 0x2023 (0x2007+0x1c) = ?? */
    0x02, 0x00,             /* 2 bytes follow */
    
    0x02, 0x00              /* ?? seems to be the start value of some 
                               counter. runs up to 6, some things happen
                               when it is lower, that will stop if when
                               it reaches 6 */
  };
  nvstusb_write_usb_bulk(ctx->handle, 2, cmd0x1c, sizeof(cmd0x1c));
    
  uint8_t cmdRate[] = {
    NVSTUSB_CMD_WRITE,      /* write data */
    0x1e,                   /* to address 0x2025 (0x2007+0x1e) = rate? */
    0x02, 0x00,             /* 2 bytes follow */

    rate<<1, rate>>7        /* rate * 2 */
    /* if the word at 0x5f, 0x60 becomes greater than this value+20
     * all timers are stopped. otherwise pin 2 of port d will be toggled
     * and pin 2 of port c will be set to high if the word is larger 
     * than 20 and the word will be increased by one. this happens
     * every time timer2 overflows. */
  };
  nvstusb_write_usb_bulk(ctx->handle, 2, cmdRate, sizeof(cmdRate));

  uint8_t cmd0x1b[] = {
    NVSTUSB_CMD_WRITE,      /* write data */
    0x1b,                   /* to address 0x2022 (0x2007+0x1b) = ?? */
    0x01, 0x00,             /* 1 byte follows */
    
    0x07                    /* ?? seems to be the start value of some 
                               counter */
  };
  nvstusb_write_usb_bulk(ctx->handle, 2, cmd0x1b, sizeof(cmd0x1b));
    
  ctx->rate = rate;
}

/* set currently open eye */
static void
nvstusb_set_eye(
  struct nvstusb_context *ctx,
  enum nvstusb_eye eye,
  uint16_t r
) {
  assert(ctx != 0);
  assert(ctx->handle != 0);
  assert(eye == nvstusb_left || eye == nvstusb_right);

  uint8_t buf[8] = { 
    0xAA, (eye%2)?0xFE:0xFF,  /* set shutter state */
    0x00, 0x00,               /* unused? */
    r, r>>8, 0xFF, 0xFF       /* still a mystery */
  };
  nvstusb_write_usb_bulk(ctx->handle, 1, buf, 8);
}

/* perform swap and toggle eyes hopefully with correct timing */
void
nvstusb_swap(
  struct nvstusb_context *ctx,
  enum nvstusb_eye eye
) {
  assert(ctx != 0);
  assert(ctx->handle != 0);
  assert(eye == nvstusb_left || eye == nvstusb_right);

  if (0 != glXGetVideoSyncSGI) {
    /* if we have the GLX_SGI_video_sync extension, we just wait
     * for vertical blanking, then issue swap. */
    unsigned int count;
    glXGetVideoSyncSGI(&count);
    glXWaitVideoSyncSGI(2, (count+1)%2, &count);
    nvstusb_set_eye(ctx, eye, 0);
    ctx->swap();
    return;
  }

  /* otherwise issue buffer swap, then read from front buffer.
   * this operation can only finish after swapping is complete. 
   * (seems like it won't work if page flipping is disabled) */
 
  ctx->swap();

  uint8_t pixels[4] = { 255, 0, 255, 255 };
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
  assert(ctx  != 0);
  assert(keys != 0);

  uint8_t cmd1[] = { 
    NVSTUSB_CMD_READ |      /* read and clear data */
    NVSTUSB_CMD_CLEAR,

    0x18,                   /* from address 0x201F (0x2007+0x18) = status? */
    0x03, 0x00              /* read/clear 3 bytes */
  };
  nvstusb_write_usb_bulk(ctx->handle, 2, cmd1, sizeof(cmd1));

  uint8_t readBuf[4+cmd1[2]];
  nvstusb_read_usb(ctx->handle, 4, readBuf, sizeof(readBuf));

  /* readBuf[0] contains the offset (0x18),
   * readBuf[1] contains the number of read bytes (0x03),
   * readBuf[2] (msb) and readBuf[3] (lsb) of the bytes sent (sizeof(cmd1)) 
   * readBuf[4] and following contain the requested data */

  /* from address 0x201F:
   * signed 8 bit integer: amount the wheel was turned without the button pressed
   */
  keys->deltaWheel = readBuf[4];
  
  /* from address 0x2020:
   * signed 8 bit integer: amount the wheel was turned with the button pressed
   */
  keys->pressedDeltaWheel = readBuf[5];
  
  /* from address 0x2021:
   * bit 0: front button was pressed since last time (presumably fom pin 4 on port C)
   * bit 1: logic state of pin 7 on port E
   * bit 2: logic state of pin 2 on port C
   */
  keys->toggled3D  = readBuf[6] & 0x01;     
}

