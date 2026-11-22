#include "SDL_config.h"
#include "SDL_video.h"
#include "SDL_blit.h"
#include "SDL_jz_mxu.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int jz_mxu_available = -1;

static void sdl_jz_detect_mxu(void)
{
	FILE *f;
	char line[256];
	int found = 0;

	if (jz_mxu_available >= 0) {
		return;
	}

	f = fopen("/proc/cpuinfo", "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			if (strstr(line, "ASEs implemented") && strstr(line, "mxu")) {
				found = 1;
				break;
			}
		}
		fclose(f);
	}

	jz_mxu_available = found;
	if (found) {
		sdl_jz_mxu_enable();
	}
}

__attribute__((constructor))
static void sdl_jz_mxu_ctor(void)
{
	sdl_jz_detect_mxu();
}

int SDL_JZ_HasMXU(void)
{
	if (jz_mxu_available < 0) {
		sdl_jz_detect_mxu();
	}
	return jz_mxu_available;
}

/* Cached software-surface opaque blit: collapses contiguous frames and uses
 * MXU 32-byte burst copy with pref 0 + pref 30 when 4-byte aligned. */
void SDL_JZ_BlitCopy(SDL_BlitInfo *info)
{
	Uint8 *src = info->s_pixels;
	Uint8 *dst = info->d_pixels;
	int w = info->d_width * info->dst->BytesPerPixel;
	int h = info->d_height;
	int srcskip = w + info->s_skip;
	int dstskip = w + info->d_skip;
	int use_mxu = SDL_JZ_HasMXU();

	if (info->s_skip == 0 && info->d_skip == 0) {
		unsigned int total = (unsigned int)w * (unsigned int)h;
		if (use_mxu && total >= 64 && (((uintptr_t)dst | (uintptr_t)src) & 3) == 0) {
			sdl_mxu_memcpy32_cached(dst, src, total);
		} else {
			SDL_memcpy(dst, src, total);
		}
		return;
	}

	if (use_mxu && w >= 64 &&
	    (((uintptr_t)dst | (uintptr_t)src | (uintptr_t)dstskip | (uintptr_t)srcskip) & 3) == 0) {
		while (h--) {
			sdl_mxu_memcpy32_cached(dst, src, (unsigned int)w);
			src += srcskip;
			dst += dstskip;
		}
		return;
	}

	while (h--) {
		SDL_memcpy(dst, src, w);
		src += srcskip;
		dst += dstskip;
	}
}

/* 2-pixel interleaved RGB565 surface alpha blend (bit-exact with stock SDL 1.2) */
void SDL_JZ_Blit565to565SurfaceAlpha(SDL_BlitInfo *info)
{
	unsigned alpha = info->src->alpha >> 3;
	int width = info->d_width;
	int height = info->d_height;
	const Uint16 *srcp = (const Uint16 *)info->s_pixels;
	int srcskip = info->s_skip >> 1;
	Uint16 *dstp = (Uint16 *)info->d_pixels;
	int dstskip = info->d_skip >> 1;

	while (height--) {
		int w = width;
		const Uint16 *s = srcp;
		Uint16 *d = dstp;

		/* Align dst to 4 bytes so we can load/store pairs cleanly */
		if (w > 0 && ((uintptr_t)d & 2)) {
			Uint32 sp = *s++;
			Uint32 dp = *d;
			sp = (sp | (sp << 16)) & 0x07e0f81fU;
			dp = (dp | (dp << 16)) & 0x07e0f81fU;
			dp += ((sp - dp) * alpha) >> 5;
			dp &= 0x07e0f81fU;
			*d++ = (Uint16)(dp | (dp >> 16));
			w--;
		}

		if ((((uintptr_t)s) & 2) == 0) {
			/* Both src and dst are 4-byte aligned: process 2 pixels per iteration
			 * with interleaved multiplies to hide JZ4760B MUL latency. */
			const Uint32 *s32 = (const Uint32 *)s;
			Uint32 *d32 = (Uint32 *)d;
			int pairs = w >> 1;
			while (pairs--) {
				Uint32 spair = *s32++;
				Uint32 dpair = *d32;
				Uint32 s0 = spair & 0xFFFFU;
				Uint32 s1 = spair >> 16;
				Uint32 d0 = dpair & 0xFFFFU;
				Uint32 d1 = dpair >> 16;

				s0 = (s0 | (s0 << 16)) & 0x07e0f81fU;
				s1 = (s1 | (s1 << 16)) & 0x07e0f81fU;
				d0 = (d0 | (d0 << 16)) & 0x07e0f81fU;
				d1 = (d1 | (d1 << 16)) & 0x07e0f81fU;

				Uint32 m0 = (s0 - d0) * alpha;
				Uint32 m1 = (s1 - d1) * alpha;

				d0 = (d0 + (m0 >> 5)) & 0x07e0f81fU;
				d1 = (d1 + (m1 >> 5)) & 0x07e0f81fU;

				d0 = (d0 | (d0 >> 16)) & 0xFFFFU;
				d1 = (d1 | (d1 >> 16)) & 0xFFFFU;
				*d32++ = d0 | (d1 << 16);
			}
			s = (const Uint16 *)s32;
			d = (Uint16 *)d32;
			w &= 1;
		} else {
			/* Unaligned src, aligned dst: 2x unrolled halfword reads, 32-bit writes */
			Uint32 *d32 = (Uint32 *)d;
			int pairs = w >> 1;
			while (pairs--) {
				Uint32 s0 = s[0];
				Uint32 s1 = s[1];
				Uint32 dpair = *d32;
				Uint32 d0 = dpair & 0xFFFFU;
				Uint32 d1 = dpair >> 16;
				s += 2;

				s0 = (s0 | (s0 << 16)) & 0x07e0f81fU;
				s1 = (s1 | (s1 << 16)) & 0x07e0f81fU;
				d0 = (d0 | (d0 << 16)) & 0x07e0f81fU;
				d1 = (d1 | (d1 << 16)) & 0x07e0f81fU;

				Uint32 m0 = (s0 - d0) * alpha;
				Uint32 m1 = (s1 - d1) * alpha;

				d0 = (d0 + (m0 >> 5)) & 0x07e0f81fU;
				d1 = (d1 + (m1 >> 5)) & 0x07e0f81fU;

				d0 = (d0 | (d0 >> 16)) & 0xFFFFU;
				d1 = (d1 | (d1 >> 16)) & 0xFFFFU;
				*d32++ = d0 | (d1 << 16);
			}
			d = (Uint16 *)d32;
			w &= 1;
		}

		if (w) {
			Uint32 sp = *s++;
			Uint32 dp = *d;
			sp = (sp | (sp << 16)) & 0x07e0f81fU;
			dp = (dp | (dp << 16)) & 0x07e0f81fU;
			dp += ((sp - dp) * alpha) >> 5;
			dp &= 0x07e0f81fU;
			*d++ = (Uint16)(dp | (dp >> 16));
		}

		srcp += width + srcskip;
		dstp += width + dstskip;
	}
}

/* 2-pixel fast-skip / fast-opaque ARGB8888 -> RGB565 per-pixel alpha blitter
 * (hot path for SDL_ttf blended text & alpha PNG icons; bit-exact with stock SDL 1.2) */
static inline Uint16 argb_to_565_opaque(Uint32 s)
{
	return (Uint16)(((s >> 8) & 0xf800U) | ((s >> 5) & 0x07e0U) | ((s >> 3) & 0x001fU));
}

static inline Uint16 argb_to_565_blend(Uint32 s, Uint32 d, unsigned alpha)
{
	Uint32 sp = ((s & 0xfc00U) << 11) | ((s >> 8) & 0xf800U) | ((s >> 3) & 0x001fU);
	d = (d | (d << 16)) & 0x07e0f81fU;
	d += ((sp - d) * alpha) >> 5;
	d &= 0x07e0f81fU;
	return (Uint16)(d | (d >> 16));
}

void SDL_JZ_BlitARGBto565PixelAlpha(SDL_BlitInfo *info)
{
	int width = info->d_width;
	int height = info->d_height;
	const Uint32 *srcp = (const Uint32 *)info->s_pixels;
	int srcskip = info->s_skip >> 2;
	Uint16 *dstp = (Uint16 *)info->d_pixels;
	int dstskip = info->d_skip >> 1;

	while (height--) {
		int w = width;
		const Uint32 *s = srcp;
		Uint16 *d = dstp;

		if (w > 0 && ((uintptr_t)d & 2)) {
			Uint32 p = *s++;
			unsigned a = p >> 27;
			if (a) {
				if (a == 31) *d = argb_to_565_opaque(p);
				else *d = argb_to_565_blend(p, *d, a);
			}
			d++;
			w--;
		}

		Uint32 *d32 = (Uint32 *)d;
		int pairs = w >> 1;
		while (pairs--) {
			Uint32 s0 = s[0];
			Uint32 s1 = s[1];
			s += 2;

			/* Fast-skip fully transparent pixel pairs (common in fonts & icons) */
			if (((s0 | s1) & 0xf8000000U) == 0) {
				d32++;
				continue;
			}
			/* Fast-pack fully opaque pixel pairs with a single 32-bit store */
			if (((s0 & s1) & 0xf8000000U) == 0xf8000000U) {
				Uint32 p0 = argb_to_565_opaque(s0);
				Uint32 p1 = argb_to_565_opaque(s1);
				*d32++ = p0 | (p1 << 16);
				continue;
			}

			/* Mixed / translucent pair */
			Uint32 dpair = *d32;
			unsigned a0 = s0 >> 27;
			unsigned a1 = s1 >> 27;
			Uint32 p0 = dpair & 0xFFFFU;
			Uint32 p1 = dpair >> 16;

			if (a0) {
				p0 = (a0 == 31) ? argb_to_565_opaque(s0) : argb_to_565_blend(s0, p0, a0);
			}
			if (a1) {
				p1 = (a1 == 31) ? argb_to_565_opaque(s1) : argb_to_565_blend(s1, p1, a1);
			}
			*d32++ = p0 | (p1 << 16);
		}

		d = (Uint16 *)d32;
		if (w & 1) {
			Uint32 p = *s++;
			unsigned a = p >> 27;
			if (a) {
				if (a == 31) *d = argb_to_565_opaque(p);
				else *d = argb_to_565_blend(p, *d, a);
			}
		}

		srcp += width + srcskip;
		dstp += width + dstskip;
	}
}

/* 2-pixel 16-bit colorkey sprite blitter (bit-exact with Blit2to2Key) */
void SDL_JZ_Blit2to2Key(SDL_BlitInfo *info)
{
	int width = info->d_width;
	int height = info->d_height;
	const Uint16 *srcp = (const Uint16 *)info->s_pixels;
	int srcskip = info->s_skip >> 1;
	Uint16 *dstp = (Uint16 *)info->d_pixels;
	int dstskip = info->d_skip >> 1;
	Uint32 rgbmask = ~info->src->Amask & 0xFFFFU;
	Uint32 ckey = info->src->colorkey & rgbmask;

	if (rgbmask != 0xFFFFU) {
		while (height--) {
			int w = width;
			const Uint16 *s = srcp;
			Uint16 *d = dstp;
			while (w--) {
				Uint16 p = *s++;
				if ((p & rgbmask) != ckey) *d = p;
				d++;
			}
			srcp += width + srcskip;
			dstp += width + dstskip;
		}
		return;
	}

	Uint32 ckey32 = ckey | (ckey << 16);
	while (height--) {
		int w = width;
		const Uint16 *s = srcp;
		Uint16 *d = dstp;

		if (w > 0 && ((uintptr_t)d & 2)) {
			Uint16 p = *s++;
			if (p != ckey) *d = p;
			d++;
			w--;
		}

		if ((((uintptr_t)s) & 2) == 0) {
			const Uint32 *s32 = (const Uint32 *)s;
			Uint32 *d32 = (Uint32 *)d;
			int pairs = w >> 1;
			while (pairs--) {
				Uint32 sp = *s32++;
				if (sp == ckey32) {
					d32++;
					continue;
				}
				Uint32 p0 = sp & 0xFFFFU;
				Uint32 p1 = sp >> 16;
				if (p0 != ckey && p1 != ckey) {
					*d32++ = sp;
				} else {
					Uint16 *dh = (Uint16 *)d32;
					if (p0 != ckey) dh[0] = (Uint16)p0;
					else dh[1] = (Uint16)p1;
					d32++;
				}
			}
			s = (const Uint16 *)s32;
			d = (Uint16 *)d32;
			w &= 1;
		} else {
			Uint32 *d32 = (Uint32 *)d;
			int pairs = w >> 1;
			while (pairs--) {
				Uint32 p0 = s[0];
				Uint32 p1 = s[1];
				s += 2;
				if (p0 != ckey && p1 != ckey) {
					*d32++ = p0 | (p1 << 16);
				} else {
					Uint16 *dh = (Uint16 *)d32;
					if (p0 != ckey) dh[0] = (Uint16)p0;
					if (p1 != ckey) dh[1] = (Uint16)p1;
					d32++;
				}
			}
			d = (Uint16 *)d32;
			w &= 1;
		}

		if (w) {
			Uint16 p = *s++;
			if (p != ckey) *d = p;
		}

		srcp += width + srcskip;
		dstp += width + dstskip;
	}
}
