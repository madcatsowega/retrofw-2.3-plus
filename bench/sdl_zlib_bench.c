#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <SDL/SDL.h>
#include <zlib.h>

static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void dummy_audio_cb(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    memset(stream, 0, len);
}

static uint32_t crc_buf(const void *p, size_t n) {
    return (uint32_t)crc32(0L, (const Bytef *)p, (uInt)n);
}

int main(void) {
    if (SDL_Init(SDL_INIT_NOPARACHUTE) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const int W = 320, H = 240;
    SDL_Surface *dst = SDL_CreateRGBSurface(SDL_SWSURFACE, W, H, 16, 0xF800, 0x07E0, 0x001F, 0);
    SDL_Surface *src = SDL_CreateRGBSurface(SDL_SWSURFACE, W, H, 16, 0xF800, 0x07E0, 0x001F, 0);
    SDL_Surface *argb = SDL_CreateRGBSurface(SDL_SWSURFACE, W, H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!dst || !src || !argb) { fprintf(stderr, "Surface alloc failed\n"); return 1; }

    /* Populate deterministic test patterns */
    uint16_t *sp = (uint16_t *)src->pixels;
    uint16_t *dp = (uint16_t *)dst->pixels;
    uint32_t *ap = (uint32_t *)argb->pixels;
    uint32_t rng = 0x12345678U;
    for (int i = 0; i < W * H; i++) {
        rng = rng * 1664525U + 1013904223U;
        sp[i] = (uint16_t)(rng >> 16);
        dp[i] = (uint16_t)(rng & 0xFFFFU);
        int mod = i % 20;
        uint32_t a = (mod < 12) ? 0 : (mod < 17 ? 255 : ((rng >> 8) & 0xFF));
        ap[i] = (a << 24) | (rng & 0x00FFFFFFU);
    }

    double t0, dt;

    /* 1. SDL_FillRect 16-bit non-zero */
    t0 = now_ms();
    for (int i = 0; i < 400; i++) SDL_FillRect(dst, NULL, 0x39E7);
    dt = now_ms() - t0;
    printf("FillRect16 (400x 320x240):     %7.2f ms  crc=%08x\n", dt, crc_buf(dst->pixels, W * H * 2));

    /* 2. SDL_BlitCopy opaque 16-bit */
    SDL_SetAlpha(src, 0, 255);
    SDL_SetColorKey(src, 0, 0);
    t0 = now_ms();
    for (int i = 0; i < 400; i++) SDL_BlitSurface(src, NULL, dst, NULL);
    dt = now_ms() - t0;
    printf("BlitCopy16 (400x 320x240):     %7.2f ms  crc=%08x\n", dt, crc_buf(dst->pixels, W * H * 2));

    /* 3. Blit565to565SurfaceAlpha (alpha=180) */
    SDL_FillRect(dst, NULL, 0x18E3);
    SDL_SetAlpha(src, SDL_SRCALPHA, 180);
    t0 = now_ms();
    for (int i = 0; i < 150; i++) SDL_BlitSurface(src, NULL, dst, NULL);
    dt = now_ms() - t0;
    printf("Blit565Alpha (150x 320x240):   %7.2f ms  crc=%08x\n", dt, crc_buf(dst->pixels, W * H * 2));

    /* 4. BlitARGBto565PixelAlpha */
    SDL_FillRect(dst, NULL, 0x10A2);
    SDL_SetAlpha(argb, SDL_SRCALPHA, 255);
    t0 = now_ms();
    for (int i = 0; i < 150; i++) SDL_BlitSurface(argb, NULL, dst, NULL);
    dt = now_ms() - t0;
    printf("BlitARGB565 (150x 320x240):    %7.2f ms  crc=%08x\n", dt, crc_buf(dst->pixels, W * H * 2));

    /* 5. Blit2to2Key (50% transparent colorkey) */
    uint16_t ckey = 0xF81F;
    for (int i = 0; i < W * H; i++) {
        if ((i & 3) < 2) sp[i] = ckey;
    }
    SDL_FillRect(dst, NULL, 0x2104);
    SDL_SetAlpha(src, 0, 255);
    SDL_SetColorKey(src, SDL_SRCCOLORKEY, ckey);
    t0 = now_ms();
    for (int i = 0; i < 250; i++) SDL_BlitSurface(src, NULL, dst, NULL);
    dt = now_ms() - t0;
    printf("Blit2to2Key (250x 320x240):    %7.2f ms  crc=%08x\n", dt, crc_buf(dst->pixels, W * H * 2));

    /* 6. SDL_MixAudio AUDIO_S16LSB */
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want;
        memset(&want, 0, sizeof(want));
        want.freq = 32000;
        want.format = AUDIO_S16LSB;
        want.channels = 2;
        want.samples = 1024;
        want.callback = dummy_audio_cb;
        if (SDL_OpenAudio(&want, NULL) == 0) {
            const int NSAMP = 4096;
            int16_t *abuf = (int16_t *)malloc(NSAMP * 2);
            int16_t *asrc = (int16_t *)malloc(NSAMP * 2);
            uint32_t r2 = 0xCAFEBABEU;
            for (int i = 0; i < NSAMP; i++) {
                r2 = r2 * 1664525U + 1013904223U;
                asrc[i] = (int16_t)(r2 >> 16);
                abuf[i] = (int16_t)(r2 & 0xFFFFU);
            }
            t0 = now_ms();
            for (int i = 0; i < 600; i++) {
                SDL_MixAudio((Uint8 *)abuf, (const Uint8 *)asrc, NSAMP * 2, 96);
            }
            dt = now_ms() - t0;
            printf("MixAudioS16 (600x 4096s):      %7.2f ms  crc=%08x\n", dt, crc_buf(abuf, NSAMP * 2));
            free(abuf); free(asrc);
            SDL_CloseAudio();
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    /* 7. zlib compress2 + uncompress benchmark (256 KB synthetic ROM/PNG-like data) */
    const size_t ZLEN = 256 * 1024;
    uint8_t *zsrc = (uint8_t *)malloc(ZLEN);
    uint8_t *zcmp = (uint8_t *)malloc(ZLEN + 4096);
    uint8_t *zout = (uint8_t *)malloc(ZLEN);
    for (size_t i = 0; i < ZLEN; i++) {
        zsrc[i] = (uint8_t)((i * 3 + (i >> 5) + ((i & 63) < 40 ? 0xAA : (i ^ 0x55))) & 0xFF);
    }
    uLongf clen = ZLEN + 4096;
    t0 = now_ms();
    for (int i = 0; i < 10; i++) {
        clen = ZLEN + 4096;
        compress2(zcmp, &clen, zsrc, ZLEN, 6);
    }
    dt = now_ms() - t0;
    printf("zlib compress2 (10x 256KB):    %7.2f ms  out=%luB\n", dt, (unsigned long)clen);

    uLongf ulen = ZLEN;
    t0 = now_ms();
    for (int i = 0; i < 40; i++) {
        ulen = ZLEN;
        uncompress(zout, &ulen, zcmp, clen);
    }
    dt = now_ms() - t0;
    printf("zlib uncompress (40x 256KB):   %7.2f ms  ok=%d crc=%08x\n",
           dt, (ulen == ZLEN && memcmp(zsrc, zout, ZLEN) == 0), crc_buf(zout, ZLEN));

    free(zsrc); free(zcmp); free(zout);
    SDL_FreeSurface(argb); SDL_FreeSurface(src); SDL_FreeSurface(dst);
    SDL_Quit();
    return 0;
}
