#include "gfx.h"
#include "font_data.h"
#include <string.h>

static fb_t *T;
void gfx_set_target(fb_t *fb) { T = fb; }

static inline uint32_t mix(uint32_t a, uint32_t b, int alpha) {
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    int r = (ar * (255 - alpha) + br * alpha) / 255;
    int g = (ag * (255 - alpha) + bg * alpha) / 255;
    int bl = (ab * (255 - alpha) + bb * alpha) / 255;
    return RGB(r, g, bl);
}

void gfx_clear_gradient(uint32_t top, uint32_t bottom) {
    for (int y = 0; y < T->h; y++) {
        uint32_t c = mix(top, bottom, y * 255 / (T->h - 1));
        uint32_t *row = T->px + (size_t)y * T->w;
        for (int x = 0; x < T->w; x++) row[x] = c;
    }
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > T->w) w = T->w - x;
    if (y + h > T->h) h = T->h - y;
    for (int j = 0; j < h; j++) {
        uint32_t *row = T->px + (size_t)(y + j) * T->w + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

void gfx_blend_rect(int x, int y, int w, int h, uint32_t color, int alpha) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > T->w) w = T->w - x;
    if (y + h > T->h) h = T->h - y;
    for (int j = 0; j < h; j++) {
        uint32_t *row = T->px + (size_t)(y + j) * T->w + x;
        for (int i = 0; i < w; i++) row[i] = mix(row[i], color, alpha);
    }
}

void gfx_rect(int x, int y, int w, int h, uint32_t c, int t) {
    gfx_fill_rect(x, y, w, t, c);
    gfx_fill_rect(x, y + h - t, w, t, c);
    gfx_fill_rect(x, y, t, h, c);
    gfx_fill_rect(x + w - t, y, t, h, c);
}

void gfx_glow_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = 6; i >= 1; i--) {               /* soft halo outside */
        int a = 70 / i;
        gfx_blend_rect(x - i * 2, y - i * 2, w + i * 4, 2, color, a);
        gfx_blend_rect(x - i * 2, y + h + i * 2 - 2, w + i * 4, 2, color, a);
        gfx_blend_rect(x - i * 2, y - i * 2, 2, h + i * 4, color, a);
        gfx_blend_rect(x + w + i * 2 - 2, y - i * 2, 2, h + i * 4, color, a);
    }
    gfx_rect(x, y, w, h, color, 2);
}

int gfx_text_width(const char *s) { return (int)strlen(s) * FONT_W; }

int gfx_text(int x, int y, const char *s, uint32_t color) {
    for (; *s; s++, x += FONT_W) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        const unsigned char *g = font_data[c - 32];
        for (int j = 0; j < FONT_H; j++) {
            int py = y + j;
            if (py < 0 || py >= T->h) continue;
            for (int i = 0; i < FONT_W; i++) {
                int a = g[j * FONT_W + i];
                int px = x + i;
                if (!a || px < 0 || px >= T->w) continue;
                uint32_t *p = T->px + (size_t)py * T->w + px;
                *p = mix(*p, color, a);
            }
        }
    }
    return x;
}

void gfx_text_clip(int x, int y, const char *s, uint32_t color, int max_chars) {
    char buf[256];
    int n = (int)strlen(s);
    if (max_chars < 4) max_chars = 4;
    if (max_chars > 250) max_chars = 250;
    if (n <= max_chars) { gfx_text(x, y, s, color); return; }
    memcpy(buf, s, (size_t)max_chars - 3);
    memcpy(buf + max_chars - 3, "...", 4);
    gfx_text(x, y, buf, color);
}

void gfx_progress(int x, int y, int w, int h, double frac) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    gfx_fill_rect(x, y, w, h, RGB(4, 10, 28));
    int fw = (int)((w - 4) * frac);
    if (fw > 0) {
        gfx_fill_rect(x + 2, y + 2, fw, h - 4, C_NEON_DIM);
        gfx_blend_rect(x + 2, y + 2, fw, (h - 4) / 2, C_NEON, 160);
    }
    gfx_glow_rect(x, y, w, h, C_NEON);
}
