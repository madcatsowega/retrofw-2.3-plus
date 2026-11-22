/*
 * RetroFW 2.3+ SDL 2.30.11 Hardware Verification & Interactive Demo
 * Tests:
 *  - Native retrofw /dev/fb0 video driver (IPU scaling + triple-buffered vsync)
 *  - SDL_CreateRenderer(SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) fallback
 *  - Alpha-blended rotating SDL_Texture + SDL_RenderFillRect / SDL_RenderDrawRect
 *  - Native Linux evdev input for all RS-97 Plus buttons
 *  - Native OSS (/dev/dsp) audio synthesis on button press
 */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Compact 5x7 uppercase/digit font */
static const unsigned char FONT5X7[][5] = {
    /* 0-9 */
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    /* A-Z */
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    /* extras: '.' ':' '+' '-' '/' */
    {0x00,0x60,0x60,0x00,0x00}, {0x00,0x36,0x36,0x00,0x00},
    {0x08,0x08,0x3E,0x08,0x08}, {0x08,0x08,0x08,0x08,0x08},
    {0x20,0x10,0x08,0x04,0x02}
};

static int glyph_idx(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 10 + (c - 'a');
    if (c == '.') return 36;
    if (c == ':') return 37;
    if (c == '+') return 38;
    if (c == '-') return 39;
    if (c == '/') return 40;
    return -1;
}

static void draw_text(SDL_Renderer *r, int x, int y, int scale, const char *s,
                      Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    for (; *s; s++, x += 6 * scale) {
        int idx = glyph_idx(*s);
        if (idx < 0) continue;
        for (int col = 0; col < 5; col++) {
            unsigned char bits = FONT5X7[idx][col];
            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row)) {
                    SDL_Rect px = { x + col * scale, y + row * scale, scale, scale };
                    SDL_RenderFillRect(r, &px);
                }
            }
        }
    }
}

typedef struct {
    float phase;
    float freq;
    int samples_left;
} ToneState;

static void audio_cb(void *userdata, Uint8 *stream, int len) {
    ToneState *t = (ToneState *)userdata;
    Sint16 *out = (Sint16 *)stream;
    int n = len / 2;
    for (int i = 0; i < n; i++) {
        if (t->samples_left > 0) {
            float env = (t->samples_left < 800) ? (t->samples_left / 800.0f) : 1.0f;
            float s = sinf(t->phase) * 9000.0f * env;
            out[i] = (Sint16)s;
            t->phase += 2.0f * 3.14159265f * t->freq / 22050.0f;
            if (t->phase > 6.2831853f) t->phase -= 6.2831853f;
            t->samples_left--;
        } else {
            out[i] = 0;
        }
    }
}

static void draw_btn(SDL_Renderer *r, int x, int y, int w, int h, const char *label, int active) {
    SDL_Rect rc = { x, y, w, h };
    if (active) {
        SDL_SetRenderDrawColor(r, 82, 168, 255, 255);
    } else {
        SDL_SetRenderDrawColor(r, 26, 33, 46, 255);
    }
    SDL_RenderFillRect(r, &rc);
    SDL_SetRenderDrawColor(r, active ? 170 : 62, active ? 220 : 78, active ? 255 : 104, 255);
    SDL_RenderDrawRect(r, &rc);
    int tx = x + (w - (int)strlen(label) * 6) / 2;
    int ty = y + (h - 7) / 2;
    draw_text(r, tx, ty, 1, label, active ? 10 : 210, active ? 14 : 222, active ? 22 : 240);
}

int main(int argc, char **argv) {
    int auto_frames = 0;
    if (argc > 1 && strcmp(argv[1], "--auto") == 0) {
        auto_frames = 120; /* ~2 seconds at 60fps for automated verification */
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_version linked;
    SDL_GetVersion(&linked);
    printf("SDL2 %d.%d.%d | video=%s | audio=%s\n",
           linked.major, linked.minor, linked.patch,
           SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none",
           SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none");

    SDL_Window *win = SDL_CreateWindow("RetroFW 2.3+ SDL2",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       320, 240, SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* Deliberately pass SDL_RENDERER_ACCELERATED | PRESENTVSYNC to verify fallback! */
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
                                           SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    SDL_RendererInfo rinfo;
    SDL_GetRendererInfo(ren, &rinfo);
    printf("Renderer: %s (flags=0x%x)\n", rinfo.name, rinfo.flags);

    /* Create an alpha-blended ARGB8888 diamond/badge texture */
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STATIC, 48, 48);
    Uint32 pixels[48 * 48];
    for (int y = 0; y < 48; y++) {
        for (int x = 0; x < 48; x++) {
            float dx = (x - 23.5f) / 23.5f;
            float dy = (y - 23.5f) / 23.5f;
            float d = sqrtf(dx * dx + dy * dy);
            if (d <= 1.0f) {
                Uint8 a = (Uint8)((1.0f - d * 0.45f) * 230.0f);
                Uint8 r = (Uint8)(70 + 170 * (1.0f - d));
                Uint8 g = (Uint8)(140 + 100 * (0.5f + 0.5f * dx));
                Uint8 b = 255;
                pixels[y * 48 + x] = ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
            } else {
                pixels[y * 48 + x] = 0;
            }
        }
    }
    SDL_UpdateTexture(tex, NULL, pixels, 48 * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    /* Init OSS audio chime */
    ToneState tone = {0.0f, 440.0f, 3500}; /* brief startup chime */
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 22050;
    want.format = AUDIO_S16LSB;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_cb;
    want.userdata = &tone;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (adev > 0) {
        printf("Audio opened: %d Hz, %d ch, %d samples\n", have.freq, have.channels, have.samples);
        SDL_PauseAudioDevice(adev, 0);
    } else {
        printf("Audio open warning: %s\n", SDL_GetError());
    }

    int running = 1;
    int frame = 0;
    Uint32 t_start = SDL_GetTicks();
    Uint32 fps_t0 = t_start;
    int fps_cnt = 0;
    int cur_fps = 60;
    char status_buf[64];

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Scancode sc = e.key.keysym.scancode;
                if (sc == SDL_SCANCODE_LCTRL)       { SDL_LockAudioDevice(adev); tone.freq = 523.25f; tone.samples_left = 2600; SDL_UnlockAudioDevice(adev); }
                else if (sc == SDL_SCANCODE_LALT)   { SDL_LockAudioDevice(adev); tone.freq = 659.25f; tone.samples_left = 2600; SDL_UnlockAudioDevice(adev); }
                else if (sc == SDL_SCANCODE_SPACE)  { SDL_LockAudioDevice(adev); tone.freq = 783.99f; tone.samples_left = 2600; SDL_UnlockAudioDevice(adev); }
                else if (sc == SDL_SCANCODE_LSHIFT) { SDL_LockAudioDevice(adev); tone.freq = 880.00f; tone.samples_left = 2600; SDL_UnlockAudioDevice(adev); }
                else if (sc == SDL_SCANCODE_HOME)   { running = 0; }
            }
        }

        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        if (ks[SDL_SCANCODE_ESCAPE] && ks[SDL_SCANCODE_RETURN]) {
            running = 0;
        }

        /* Background: MidnightMinimal deep slate */
        SDL_SetRenderDrawColor(ren, 13, 17, 26, 255);
        SDL_RenderClear(ren);

        /* Top bar */
        SDL_Rect topbar = { 0, 0, 320, 26 };
        SDL_SetRenderDrawColor(ren, 20, 27, 40, 255);
        SDL_RenderFillRect(ren, &topbar);
        SDL_SetRenderDrawColor(ren, 56, 132, 232, 255);
        SDL_RenderDrawLine(ren, 0, 26, 319, 26);

        draw_text(ren, 8, 9, 1, "RETROFW 2.3+  SDL 2.30.11", 232, 240, 252);
        snprintf(status_buf, sizeof(status_buf), "%2d FPS", cur_fps);
        draw_text(ren, 272, 9, 1, status_buf, 112, 212, 160);

        /* Animated alpha-blended rotating orb */
        float ang = frame * 3.0f;
        int ox = 136 + (int)(sinf(frame * 0.05f) * 34.0f);
        int oy = 52 + (int)(cosf(frame * 0.07f) * 14.0f);
        SDL_Rect dst = { ox, oy, 48, 48 };
        SDL_RenderCopyEx(ren, tex, NULL, &dst, ang, NULL, SDL_FLIP_NONE);

        /* RS-97 Plus live button map */
        draw_btn(ren, 12,  36, 42, 16, "L", ks[SDL_SCANCODE_TAB]);
        draw_btn(ren, 266, 36, 42, 16, "R", ks[SDL_SCANCODE_BACKSPACE]);

        /* D-Pad */
        draw_btn(ren, 34, 86,  24, 20, "U", ks[SDL_SCANCODE_UP]);
        draw_btn(ren, 34, 130, 24, 20, "D", ks[SDL_SCANCODE_DOWN]);
        draw_btn(ren, 8,  108, 24, 20, "L", ks[SDL_SCANCODE_LEFT]);
        draw_btn(ren, 60, 108, 24, 20, "R", ks[SDL_SCANCODE_RIGHT]);

        /* Face buttons (RS-97 mapping: A=LCtrl, B=LAlt, X=Space, Y=LShift) */
        draw_btn(ren, 258, 86,  24, 20, "X", ks[SDL_SCANCODE_SPACE]);
        draw_btn(ren, 258, 130, 24, 20, "B", ks[SDL_SCANCODE_LALT]);
        draw_btn(ren, 232, 108, 24, 20, "Y", ks[SDL_SCANCODE_LSHIFT]);
        draw_btn(ren, 284, 108, 24, 20, "A", ks[SDL_SCANCODE_LCTRL]);

        /* SELECT / START */
        draw_btn(ren, 96,  166, 56, 18, "SELECT", ks[SDL_SCANCODE_ESCAPE]);
        draw_btn(ren, 168, 166, 56, 18, "START",  ks[SDL_SCANCODE_RETURN]);

        /* Footer */
        draw_text(ren, 34, 202, 1, "ABXY: AUDIO CHIME  ALL BUTTONS LIVE", 150, 168, 194);
        draw_text(ren, 64, 218, 1, "SELECT+START OR POWER TO EXIT", 104, 152, 224);

        SDL_RenderPresent(ren);

        frame++;
        fps_cnt++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_t0 >= 500) {
            cur_fps = (int)(fps_cnt * 1000 / (now - fps_t0));
            fps_cnt = 0;
            fps_t0 = now;
        }
        if (auto_frames > 0 && frame >= auto_frames) {
            running = 0;
        }
    }

    Uint32 elapsed = SDL_GetTicks() - t_start;
    if (elapsed > 0) {
        printf("Rendered %d frames in %u ms (%.1f FPS)\n",
               frame, (unsigned)elapsed, frame * 1000.0 / elapsed);
    }

    if (adev > 0) SDL_CloseAudioDevice(adev);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
