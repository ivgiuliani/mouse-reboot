/* enable the use of GNU extensions */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

#include <libkmod.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

/* disable module reloading when this USB pointer is attached */
#define USB_MATCHING_STRING "Logitech"

/* how often to check when the mouse state has changed */
#define SLEEP_INTERVAL 1000000

/* reload the driver if the mouse stays MOTION for at least 10 seconds */
#define MOTION_SECONDS_THRESHOLD 10

/* force module reload every 100 seconds */
#define MOTION_SECONDS_THRESHOLD_FORCED 100

/* name of the module to reload */
#define MODULE_NAME "psmouse"

struct coord {
  int x;
  int y;
};

#ifdef DEBUG
inline static void clean_line() {
  /*
   * this magic code cleans any previously written line on the
   * terminal so that when overwritten, we are not left with garbage
   */
  fprintf(stderr, "%c[2K", 27);
}

#  define LOG(msg, ...) fprintf(stderr, msg "\n", ##__VA_ARGS__);
#  define LOGL(msg, ...) { clean_line(); fprintf(stderr, msg "\r", ##__VA_ARGS__); }
#endif

bool
has_xi() {
  Display *display = XOpenDisplay(NULL);
  int opcode, event, error;
  int major = 2;
  int minor = 0;
  bool ret = true;

  if (!XQueryExtension(display, "XInputExtension", &opcode, &event, &error) ||
      XIQueryVersion(display, &major, &minor) == BadRequest) {
    ret = false;
  }

  XCloseDisplay(display);
  return ret;
}

bool
usb_is_plugged_in(Display* display) {
  XIDeviceInfo *devices, device;
  int i, ndevices;
  bool ret = false;

  devices = XIQueryDevice(display, XIAllDevices, &ndevices);
  for (i = 0; i < ndevices; i++) {
    device = devices[i];

    if (device.use == XISlavePointer &&
        device.enabled &&
        strcasestr(device.name, "USB") != NULL &&
        strcasestr(device.name, USB_MATCHING_STRING) != NULL) {
#ifdef DEBUG
      LOGL("%s (id:%d) attached", device.name, device.deviceid);
#endif
      ret = true;
      break;
    }
  }

  XIFreeDeviceInfo(devices);
  return ret;
}

bool
reload_module() {
  short int err, ret = true;
  struct kmod_ctx *ctx;
  struct kmod_module *mod;

  const char *null_config = NULL;
  const char *opts = NULL;

  ctx = kmod_new(NULL, &null_config);
  if (!ctx) {
    return false;
  }

  err = kmod_module_new_from_name(ctx, MODULE_NAME, &mod);
  if (err < 0) {
    ret = false;
    goto clean_ctx;
  }

#ifdef DEBUG
  LOG("unloading module %s", MODULE_NAME);
#endif
  err = kmod_module_remove_module(mod, KMOD_REMOVE_NOWAIT);
  if (err < 0) {
    ret = false;
    goto clean_mod;
  }

  /* wait for a bit before reloading the module */
  usleep(1000);

#ifdef DEBUG
  LOG("loading module %s", MODULE_NAME);
#endif
  err = kmod_module_insert_module(mod, 0, opts);
  if (err < 0) {
    ret = false;
    goto clean_mod;
  }

clean_mod:
  kmod_module_unref(mod);
clean_ctx:
  kmod_unref(ctx);

  return ret;
}


int main(void) {
  Display *display;
  Window window;

  int i, timediff;
  struct coord pointer, win_coord;
  struct coord old = {
    .x = -1,
    .y= -1
  };
  bool pointer_found;
  bool reloaded = false;
  unsigned int mask;
  time_t last_movement = time(NULL);

  if (!has_xi()) {
    /* we can't work properly without xinput support */
    fprintf(stderr, "Xinput 2.0 not found\n");
    return EXIT_FAILURE;
  }

  while (true) {
    display = XOpenDisplay(NULL);
    if (!display) {
      fprintf(stderr, "Cannot open display\n");
      sleep(10);
      continue;
    }

    if (usb_is_plugged_in(display)) {
      goto noop;
    }

    pointer_found = false;
    for (i = 0; i < XScreenCount(display) && !pointer_found; i++) {
      pointer_found = XQueryPointer(display,
                                    XRootWindow(display, i),
                                    &window, &window,
                                    &pointer.x, &pointer.y,
                                    &win_coord.x, &win_coord.y,
                                    &mask);
    }

    if (!pointer_found) {
      fprintf(stderr, "No mouse found.\n");
      goto noop;
    }

    if (pointer.x == old.x && pointer.y == old.y) {
      timediff = time(NULL) - last_movement;
#ifdef DEBUG
      LOGL("motionless mouse for %d seconds", (int)timediff);
#endif
      if ((timediff % MOTION_SECONDS_THRESHOLD_FORCED == 0) ||
          (timediff >= MOTION_SECONDS_THRESHOLD && !reloaded)) {
#ifdef DEBUG
        LOG("%d seconds threshold passed, reloading module", timediff);
#endif
        reloaded = reload_module();
      }
    } else {
      reloaded = false;
      last_movement = time(NULL);
#ifdef DEBUG
      LOGL("mouse position: x=%d y=%d mask=%d",
           pointer.x, pointer.y, mask);
#endif
    }

    old.x = pointer.x;
    old.y = pointer.y;

noop:
    XCloseDisplay(display);
    usleep(SLEEP_INTERVAL);
  }

  return EXIT_SUCCESS;
}
