/*
  Simple DirectMedia Layer — RetroFW /dev/fb0 video driver
  Copyright (C) 2026 RetroFW 2.3+ Contributors

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#ifndef SDL_retrofwvideo_h_
#define SDL_retrofwvideo_h_

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

#include <linux/fb.h>

typedef struct SDL_VideoData
{
    int fb_fd;
    int tty_fd;
    int saved_kd_mode;
    SDL_bool kd_mode_set;

    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo saved_vinfo;
    struct fb_var_screeninfo vinfo;
    SDL_bool mode_set;

    Uint8 *fb_mem;
    size_t fb_mmap_len;
    size_t fb_line_length;
    size_t fb_page_bytes;
    int num_bufs;
    int cur_page;

    SDL_Surface *shadow_surface;
} SDL_VideoData;

#endif /* SDL_retrofwvideo_h_ */

/* vi: set ts=4 sw=4 expandtab: */
