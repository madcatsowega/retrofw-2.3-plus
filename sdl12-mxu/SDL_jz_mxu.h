#ifndef _SDL_jz_mxu_h
#define _SDL_jz_mxu_h

#include "SDL_types.h"
#include "SDL_video.h"
#include "SDL_blit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if kernel advertises JZ4760B MXU context-switch support, 0 otherwise */
int SDL_JZ_HasMXU(void);

/* Assembly routines (SDL_jz_mxu_asm.S) */
void sdl_jz_mxu_enable(void);
void sdl_mips_memset4_cached(Uint32 *dst, Uint32 val, int count);
void sdl_mips_memset4_uncached(Uint32 *dst, Uint32 val, int count);
void sdl_mxu_memcpy32_cached(void *dst, const void *src, unsigned int nbytes);
void sdl_mxu_mix_s16_lsb(Sint16 *dst, const Sint16 *src, int sample_count, int volume);

/* Fast MIPS32/MXU video blitters (SDL_jz_mxu.c) */
void SDL_JZ_BlitCopy(SDL_BlitInfo *info);
void SDL_JZ_Blit565to565SurfaceAlpha(SDL_BlitInfo *info);
void SDL_JZ_BlitARGBto565PixelAlpha(SDL_BlitInfo *info);
void SDL_JZ_Blit2to2Key(SDL_BlitInfo *info);

#ifdef __cplusplus
}
#endif

#endif /* _SDL_jz_mxu_h */
