#ifndef GFX_H
#define GFX_H
#include "platform.h"
#include <stdint.h>

#define FONT_W 14
#define FONT_H 28
#define RGB(r,g,b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* Neon blue theme */
#define C_BG_TOP   RGB(3, 6, 20)
#define C_BG_BOT   RGB(8, 20, 56)
#define C_PANEL    RGB(8, 16, 40)
#define C_NEON     RGB(0, 200, 255)
#define C_NEON_DIM RGB(0, 110, 160)
#define C_TEXT     RGB(210, 240, 255)
#define C_TEXT_DIM RGB(110, 150, 180)
#define C_OK       RGB(60, 255, 160)
#define C_WARN     RGB(255, 200, 60)
#define C_ERR      RGB(255, 80, 110)

void gfx_set_target(fb_t *fb);
void gfx_clear_gradient(uint32_t top, uint32_t bottom);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_blend_rect(int x, int y, int w, int h, uint32_t color, int alpha /*0-255*/);
void gfx_rect(int x, int y, int w, int h, uint32_t color, int thickness);
void gfx_glow_rect(int x, int y, int w, int h, uint32_t color);  /* neon outline with halo */
int  gfx_text(int x, int y, const char *s, uint32_t color);      /* returns end x */
int  gfx_text_width(const char *s);
void gfx_text_clip(int x, int y, const char *s, uint32_t color, int max_chars);
void gfx_progress(int x, int y, int w, int h, double frac);
#endif
