/*
  Simple DirectMedia Layer — RetroFW /dev/fb0 video driver
  Copyright (C) 2026 RetroFW 2.3+ Contributors

  Native Linux framebuffer (/dev/fb0) backend for Ingenic JZ4760 / RetroFW 2.3+:
   - Hardware IPU scaling via FBIOPUT_VSCREENINFO (up to 800x600 RGB565)
   - Triple-buffered tear-free page flipping via FBIOPAN_DISPLAY
   - Cached RAM shadow surface -> sequential burst copy to uncached /dev/fb0
   - Supports both SDL_GetWindowSurface() and SDL_CreateRenderer() (software)
   - Native Linux evdev input (/dev/input/event*) + VT KD_GRAPHICS handling
*/

#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_RETROFW

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "SDL_hints.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#ifdef SDL_INPUT_LINUXEV
#include "../../core/linux/SDL_evdev.h"
#endif

#include "SDL_retrofwvideo.h"

#define RETROFW_DRIVER_NAME "retrofw"
#define RETROFW_FB_SURFACE  "_SDL_RetroFWSurface"

/* Initialization/Query functions */
static int RETROFW_VideoInit(_THIS);
static void RETROFW_VideoQuit(_THIS);
static void RETROFW_GetDisplayModes(_THIS, SDL_VideoDisplay *display);
static int RETROFW_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode);
static int RETROFW_CreateWindow(_THIS, SDL_Window *window);
static void RETROFW_DestroyWindow(_THIS, SDL_Window *window);
static void RETROFW_PumpEvents(_THIS);
static int RETROFW_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch);
static int RETROFW_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects);
static void RETROFW_DestroyWindowFramebuffer(_THIS, SDL_Window *window);

static const char *RETROFW_GetFBDevPath(void)
{
    const char *env = SDL_getenv("SDL_FBDEV");
    return (env && *env) ? env : "/dev/fb0";
}

static int RETROFW_Available(void)
{
    return (access(RETROFW_GetFBDevPath(), R_OK | W_OK) == 0) ? 1 : 0;
}

static void RETROFW_DeleteDevice(SDL_VideoDevice *device)
{
    if (device) {
        SDL_free(device->driverdata);
        SDL_free(device);
    }
}

static SDL_VideoDevice *RETROFW_CreateDevice(void)
{
    SDL_VideoDevice *device;
    SDL_VideoData *data;

    if (!RETROFW_Available()) {
        return NULL;
    }

    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }

    data = (SDL_VideoData *)SDL_calloc(1, sizeof(SDL_VideoData));
    if (!data) {
        SDL_free(device);
        SDL_OutOfMemory();
        return NULL;
    }

    data->fb_fd = -1;
    data->tty_fd = -1;
    data->saved_kd_mode = KD_TEXT;

    device->driverdata = data;

    /* Set function pointers */
    device->VideoInit = RETROFW_VideoInit;
    device->VideoQuit = RETROFW_VideoQuit;
    device->GetDisplayModes = RETROFW_GetDisplayModes;
    device->SetDisplayMode = RETROFW_SetDisplayMode;
    device->CreateSDLWindow = RETROFW_CreateWindow;
    device->DestroyWindow = RETROFW_DestroyWindow;
    device->PumpEvents = RETROFW_PumpEvents;
    device->CreateWindowFramebuffer = RETROFW_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = RETROFW_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = RETROFW_DestroyWindowFramebuffer;

    device->free = RETROFW_DeleteDevice;

    return device;
}

VideoBootStrap RETROFW_bootstrap = {
    RETROFW_DRIVER_NAME, "RetroFW Linux framebuffer (/dev/fb0) video driver",
    RETROFW_CreateDevice,
    NULL /* no ShowMessageBox implementation */
};

static void RETROFW_EnterGraphicsTTY(SDL_VideoData *data)
{
    int fd = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
    }
    if (fd < 0) {
        return;
    }

    if (ioctl(fd, KDGETMODE, &data->saved_kd_mode) == 0) {
        if (data->saved_kd_mode != KD_GRAPHICS) {
            if (ioctl(fd, KDSETMODE, KD_GRAPHICS) == 0) {
                data->kd_mode_set = SDL_TRUE;
            }
        }
        data->tty_fd = fd;
    } else {
        close(fd);
    }
}

static void RETROFW_LeaveGraphicsTTY(SDL_VideoData *data)
{
    if (data->tty_fd >= 0) {
        if (data->kd_mode_set) {
            ioctl(data->tty_fd, KDSETMODE, data->saved_kd_mode);
            data->kd_mode_set = SDL_FALSE;
        }
        close(data->tty_fd);
        data->tty_fd = -1;
    }
}

static void RETROFW_AddModeIfNew(SDL_VideoDisplay *display, int w, int h)
{
    SDL_DisplayMode mode;
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = w;
    mode.h = h;
    mode.refresh_rate = 60;
    mode.driverdata = NULL;
    SDL_AddDisplayMode(display, &mode);
}

static int RETROFW_VideoInit(_THIS)
{
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    SDL_DisplayMode mode;
    const char *fbdev = RETROFW_GetFBDevPath();

    data->fb_fd = open(fbdev, O_RDWR | O_CLOEXEC);
    if (data->fb_fd < 0) {
        return SDL_SetError("Unable to open %s: %s", fbdev, strerror(errno));
    }

    if (ioctl(data->fb_fd, FBIOGET_FSCREENINFO, &data->finfo) < 0) {
        close(data->fb_fd);
        data->fb_fd = -1;
        return SDL_SetError("FBIOGET_FSCREENINFO failed: %s", strerror(errno));
    }

    if (ioctl(data->fb_fd, FBIOGET_VSCREENINFO, &data->saved_vinfo) < 0) {
        close(data->fb_fd);
        data->fb_fd = -1;
        return SDL_SetError("FBIOGET_VSCREENINFO failed: %s", strerror(errno));
    }

    data->vinfo = data->saved_vinfo;

    /* Default desktop mode: current framebuffer size (e.g. 320x240 or 320x480) */
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = (data->saved_vinfo.xres > 0) ? (int)data->saved_vinfo.xres : 320;
    mode.h = (data->saved_vinfo.yres > 0) ? (int)data->saved_vinfo.yres : 240;
    mode.refresh_rate = 60;
    mode.driverdata = NULL;

    if (SDL_AddBasicVideoDisplay(&mode) < 0) {
        close(data->fb_fd);
        data->fb_fd = -1;
        return -1;
    }

    RETROFW_GetDisplayModes(_this, &_this->displays[0]);

    /* Hide fbcon text cursor / console output while active */
    RETROFW_EnterGraphicsTTY(data);

#ifdef SDL_INPUT_LINUXEV
    if (SDL_EVDEV_Init() < 0) {
        RETROFW_LeaveGraphicsTTY(data);
        close(data->fb_fd);
        data->fb_fd = -1;
        return -1;
    }
#endif

    return 0;
}

static void RETROFW_GetDisplayModes(_THIS, SDL_VideoDisplay *display)
{
    static const struct { int w; int h; } standard_modes[] = {
        { 320, 240 },
        { 320, 480 },
        { 400, 240 },
        { 480, 272 },
        { 480, 320 },
        { 640, 480 },
        { 800, 600 },
    };
    size_t i;

    RETROFW_AddModeIfNew(display, display->desktop_mode.w, display->desktop_mode.h);
    for (i = 0; i < SDL_arraysize(standard_modes); ++i) {
        RETROFW_AddModeIfNew(display, standard_modes[i].w, standard_modes[i].h);
    }
}

static int RETROFW_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    (void)_this;
    (void)display;
    (void)mode;
    /* Actual fbdev mode is applied in CreateWindowFramebuffer with the window size. */
    return 0;
}

static int RETROFW_CreateWindow(_THIS, SDL_Window *window)
{
    (void)_this;
    window->flags |= SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS;
    SDL_SetKeyboardFocus(window);
    SDL_SetMouseFocus(window);
    return 0;
}

static void RETROFW_DestroyWindow(_THIS, SDL_Window *window)
{
    RETROFW_DestroyWindowFramebuffer(_this, window);
}

static void RETROFW_PumpEvents(_THIS)
{
    (void)_this;
#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Poll();
#endif
}

static int RETROFW_ConfigureFB(SDL_VideoData *data, int w, int h)
{
    struct fb_var_screeninfo vinfo;
    size_t single_bytes;
    int want_bufs = 3;
    const char *env_bufs;

    if (w < 16) {
        w = 16;
    }
    if (h < 16) {
        h = 16;
    }

    env_bufs = SDL_getenv("SDL_RETROFW_BUFFERS");
    if (env_bufs && *env_bufs) {
        int v = SDL_atoi(env_bufs);
        if (v >= 1 && v <= 3) {
            want_bufs = v;
        }
    }

    /* Check VRAM capacity from finfo.smem_len */
    single_bytes = (size_t)w * (size_t)h * 2;
    if (data->finfo.smem_len > 0) {
        while (want_bufs > 1 && single_bytes * (size_t)want_bufs > (size_t)data->finfo.smem_len) {
            want_bufs--;
        }
    }

    vinfo = data->saved_vinfo;
    vinfo.xres = (Uint32)w;
    vinfo.yres = (Uint32)h;
    vinfo.xres_virtual = (Uint32)w;
    vinfo.yres_virtual = (Uint32)(h * want_bufs);
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;
    vinfo.bits_per_pixel = 16;
    vinfo.red.offset = 11;
    vinfo.red.length = 5;
    vinfo.red.msb_right = 0;
    vinfo.green.offset = 5;
    vinfo.green.length = 6;
    vinfo.green.msb_right = 0;
    vinfo.blue.offset = 0;
    vinfo.blue.length = 5;
    vinfo.blue.msb_right = 0;
    vinfo.transp.offset = 0;
    vinfo.transp.length = 0;
    vinfo.transp.msb_right = 0;
    vinfo.activate = FB_ACTIVATE_NOW;

    if (ioctl(data->fb_fd, FBIOPUT_VSCREENINFO, &vinfo) < 0) {
        /* Fallback to single buffer if multi-buffer rejected */
        if (want_bufs > 1) {
            want_bufs = 1;
            vinfo.yres_virtual = (Uint32)h;
            if (ioctl(data->fb_fd, FBIOPUT_VSCREENINFO, &vinfo) < 0) {
                return SDL_SetError("FBIOPUT_VSCREENINFO (%dx%d 16bpp) failed: %s",
                                    w, h, strerror(errno));
            }
        } else {
            return SDL_SetError("FBIOPUT_VSCREENINFO (%dx%d 16bpp) failed: %s",
                                w, h, strerror(errno));
        }
    }

    if (ioctl(data->fb_fd, FBIOGET_VSCREENINFO, &data->vinfo) < 0) {
        data->vinfo = vinfo;
    }
    ioctl(data->fb_fd, FBIOGET_FSCREENINFO, &data->finfo);

    data->mode_set = SDL_TRUE;

    /* Map framebuffer memory once (or remap if smem_len grew) */
    if (!data->fb_mem || data->fb_mmap_len != (size_t)data->finfo.smem_len) {
        if (data->fb_mem) {
            munmap(data->fb_mem, data->fb_mmap_len);
            data->fb_mem = NULL;
            data->fb_mmap_len = 0;
        }
        data->fb_mmap_len = (size_t)data->finfo.smem_len;
        if (data->fb_mmap_len == 0) {
            data->fb_mmap_len = (size_t)data->vinfo.xres_virtual *
                                (size_t)data->vinfo.yres_virtual * 2;
        }
        data->fb_mem = (Uint8 *)mmap(NULL, data->fb_mmap_len,
                                     PROT_READ | PROT_WRITE, MAP_SHARED,
                                     data->fb_fd, 0);
        if (data->fb_mem == MAP_FAILED) {
            data->fb_mem = NULL;
            data->fb_mmap_len = 0;
            return SDL_SetError("mmap(/dev/fb0) failed: %s", strerror(errno));
        }
    }

    data->fb_line_length = (data->finfo.line_length > 0)
                               ? (size_t)data->finfo.line_length
                               : (size_t)data->vinfo.xres * 2;
    data->fb_page_bytes = data->fb_line_length * (size_t)data->vinfo.yres;

    data->num_bufs = (data->vinfo.yres > 0)
                         ? (int)(data->vinfo.yres_virtual / data->vinfo.yres)
                         : 1;
    if (data->num_bufs < 1) {
        data->num_bufs = 1;
    }
    if (data->num_bufs > 3) {
        data->num_bufs = 3;
    }
    while (data->num_bufs > 1 &&
           data->fb_page_bytes * (size_t)data->num_bufs > data->fb_mmap_len) {
        data->num_bufs--;
    }

    data->cur_page = 0;

    /* Clear all active pages to black on mode set */
    if (!SDL_getenv("SDL_FBCON_DONT_CLEAR")) {
        size_t clear_len = data->fb_page_bytes * (size_t)data->num_bufs;
        if (clear_len > data->fb_mmap_len) {
            clear_len = data->fb_mmap_len;
        }
        SDL_memset(data->fb_mem, 0, clear_len);
    }

    /* Reset display pan to page 0 */
    data->vinfo.xoffset = 0;
    data->vinfo.yoffset = 0;
    ioctl(data->fb_fd, FBIOPAN_DISPLAY, &data->vinfo);

    return 0;
}

static int RETROFW_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    SDL_Surface *surface;
    int w, h;

    RETROFW_DestroyWindowFramebuffer(_this, window);

    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0) {
        w = 320;
        h = 240;
    }

    if (RETROFW_ConfigureFB(data, w, h) < 0) {
        return -1;
    }

    /* Allocate a cached RAM shadow surface in RGB565 so all SDL2 rendering,
     * alpha blending, and blits run at full L1-cached CPU speed rather than
     * incurring uncached /dev/fb0 read-modify-write stalls. */
    surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 0, SDL_PIXELFORMAT_RGB565);
    if (!surface) {
        return -1;
    }

    data->shadow_surface = surface;
    SDL_SetWindowData(window, RETROFW_FB_SURFACE, surface);

    *format = SDL_PIXELFORMAT_RGB565;
    *pixels = surface->pixels;
    *pitch = surface->pitch;
    return 0;
}

static int RETROFW_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    SDL_Surface *surface;
    int next_page;
    Uint8 *dst;
    const Uint8 *src;

    (void)rects;
    (void)numrects;

    surface = (SDL_Surface *)SDL_GetWindowData(window, RETROFW_FB_SURFACE);
    if (!surface || !data->fb_mem) {
        return SDL_SetError("RetroFW framebuffer not initialized");
    }

    if (data->num_bufs >= 2) {
        next_page = (data->cur_page + 1) % data->num_bufs;
    } else {
        next_page = 0;
    }

    dst = data->fb_mem + (size_t)next_page * data->fb_page_bytes;
    src = (const Uint8 *)surface->pixels;

    if ((size_t)surface->pitch == data->fb_line_length &&
        surface->h == (int)data->vinfo.yres) {
        /* Contiguous sequential burst write from cached RAM to uncached /dev/fb0 */
        SDL_memcpy(dst, src, (size_t)surface->h * (size_t)surface->pitch);
    } else {
        int y;
        int copy_h = (surface->h < (int)data->vinfo.yres) ? surface->h : (int)data->vinfo.yres;
        size_t copy_w = ((size_t)surface->pitch < data->fb_line_length)
                            ? (size_t)surface->pitch
                            : data->fb_line_length;
        for (y = 0; y < copy_h; ++y) {
            SDL_memcpy(dst + (size_t)y * data->fb_line_length,
                       src + (size_t)y * (size_t)surface->pitch,
                       copy_w);
        }
    }

    if (data->num_bufs >= 2) {
        /* On Ingenic jz4760_lcd, FBIOPAN_DISPLAY atomically waits for vsync
         * and swaps the hardware IPU source address to the new page. */
        data->vinfo.xoffset = 0;
        data->vinfo.yoffset = (Uint32)(next_page * (int)data->vinfo.yres);
        ioctl(data->fb_fd, FBIOPAN_DISPLAY, &data->vinfo);
        data->cur_page = next_page;
    }

    return 0;
}

static void RETROFW_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    SDL_Surface *surface;

    surface = (SDL_Surface *)SDL_SetWindowData(window, RETROFW_FB_SURFACE, NULL);
    if (surface) {
        if (data->shadow_surface == surface) {
            data->shadow_surface = NULL;
        }
        SDL_FreeSurface(surface);
    }
}

static void RETROFW_VideoQuit(_THIS)
{
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    const char *dont_clear = SDL_getenv("SDL_FBCON_DONT_CLEAR");

#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Quit();
#endif

    if (data->fb_mem) {
        if (!dont_clear && data->fb_page_bytes > 0) {
            size_t clear_len = data->fb_page_bytes * (size_t)data->num_bufs;
            if (clear_len > data->fb_mmap_len) {
                clear_len = data->fb_mmap_len;
            }
            SDL_memset(data->fb_mem, 0, clear_len);
        }
        munmap(data->fb_mem, data->fb_mmap_len);
        data->fb_mem = NULL;
        data->fb_mmap_len = 0;
    }

    if (data->fb_fd >= 0) {
        if (data->mode_set) {
            if (dont_clear) {
                struct fb_var_screeninfo cur;
                if (ioctl(data->fb_fd, FBIOGET_VSCREENINFO, &cur) == 0) {
                    cur.xoffset = 0;
                    cur.yoffset = 0;
                    ioctl(data->fb_fd, FBIOPAN_DISPLAY, &cur);
                }
            } else {
                data->saved_vinfo.xoffset = 0;
                data->saved_vinfo.yoffset = 0;
                data->saved_vinfo.activate = FB_ACTIVATE_NOW;
                ioctl(data->fb_fd, FBIOPUT_VSCREENINFO, &data->saved_vinfo);
            }
            data->mode_set = SDL_FALSE;
        }
        close(data->fb_fd);
        data->fb_fd = -1;
    }

    RETROFW_LeaveGraphicsTTY(data);
}

/* Zero-copy /dev/fb0 backbuffer helpers for GC200 hardware 2D renderer & native ports */
__attribute__((visibility("default")))
int SDLCALL SDL_RetroFW_PrepareBackbuffer(SDL_Window *window,
                                                   Uint32 *out_phys,
                                                   void **out_virt,
                                                   int *out_w,
                                                   int *out_h,
                                                   int *out_stride)
{
    SDL_VideoDevice *_this = SDL_GetVideoDevice();
    SDL_VideoData *data;
    int w, h, next_page;

    if (!_this || !_this->driverdata || !window) {
        return SDL_SetError("RetroFW video device not active");
    }
    data = (SDL_VideoData *)_this->driverdata;
    if (data->fb_fd < 0) {
        return SDL_SetError("RetroFW framebuffer device not open");
    }

    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0) {
        w = 320;
        h = 240;
    }

    if (!data->mode_set || !data->fb_mem || (int)data->vinfo.xres != w || (int)data->vinfo.yres != h) {
        if (RETROFW_ConfigureFB(data, w, h) < 0) {
            return -1;
        }
    }

    if (!data->fb_mem) {
        return SDL_SetError("RetroFW framebuffer not mapped");
    }

    next_page = (data->num_bufs >= 2) ? ((data->cur_page + 1) % data->num_bufs) : 0;

    if (out_phys)   *out_phys   = (Uint32)(data->finfo.smem_start + (size_t)next_page * data->fb_page_bytes);
    if (out_virt)   *out_virt   = (void *)(data->fb_mem + (size_t)next_page * data->fb_page_bytes);
    if (out_w)      *out_w      = (int)data->vinfo.xres;
    if (out_h)      *out_h      = (int)data->vinfo.yres;
    if (out_stride) *out_stride = (int)data->fb_line_length;
    return 0;
}

__attribute__((visibility("default")))
int SDLCALL SDL_RetroFW_PrepareFrontbuffer(SDL_Window *window,
                                           Uint32 *out_phys,
                                           void **out_virt,
                                           int *out_w,
                                           int *out_h,
                                           int *out_stride)
{
    SDL_VideoDevice *_this = SDL_GetVideoDevice();
    SDL_VideoData *data;
    int w, h;

    if (!_this || !_this->driverdata || !window) {
        return SDL_SetError("RetroFW video device not active");
    }
    data = (SDL_VideoData *)_this->driverdata;
    if (data->fb_fd < 0) {
        return SDL_SetError("RetroFW framebuffer device not open");
    }

    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0) {
        w = 320;
        h = 240;
    }

    if (!data->mode_set || !data->fb_mem || (int)data->vinfo.xres != w || (int)data->vinfo.yres != h) {
        if (RETROFW_ConfigureFB(data, w, h) < 0) {
            return -1;
        }
    }

    if (!data->fb_mem) {
        return SDL_SetError("RetroFW framebuffer not mapped");
    }

    if (out_phys)   *out_phys   = (Uint32)(data->finfo.smem_start + (size_t)data->cur_page * data->fb_page_bytes);
    if (out_virt)   *out_virt   = (void *)(data->fb_mem + (size_t)data->cur_page * data->fb_page_bytes);
    if (out_w)      *out_w      = (int)data->vinfo.xres;
    if (out_h)      *out_h      = (int)data->vinfo.yres;
    if (out_stride) *out_stride = (int)data->fb_line_length;
    return 0;
}

__attribute__((visibility("default")))
int SDLCALL SDL_RetroFW_FlipBackbuffer(SDL_Window *window)
{
    SDL_VideoDevice *_this = SDL_GetVideoDevice();
    SDL_VideoData *data;
    int next_page;

    (void)window;
    if (!_this || !_this->driverdata) {
        return -1;
    }
    data = (SDL_VideoData *)_this->driverdata;
    if (data->fb_fd < 0) {
        return -1;
    }

    if (data->num_bufs >= 2) {
        next_page = (data->cur_page + 1) % data->num_bufs;
        data->vinfo.xoffset = 0;
        data->vinfo.yoffset = (Uint32)(next_page * (int)data->vinfo.yres);
        ioctl(data->fb_fd, FBIOPAN_DISPLAY, &data->vinfo);
        data->cur_page = next_page;
    }
    return 0;
}

#endif /* SDL_VIDEO_DRIVER_RETROFW */

/* vi: set ts=4 sw=4 expandtab: */
