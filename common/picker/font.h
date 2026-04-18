/*
 * ThumbyNES — minimal text renderer.
 *
 * 3×5 bitmap glyphs in a 4×6 cell, drawn directly into a 128×128
 * RGB565 framebuffer. The glyph table is shared with ThumbyP8 and
 * ultimately derived from Pemsa (MIT license — see nes_font.c
 * header for attribution).
 *
 * This is a self-contained renderer: no machine, no clip rect, no
 * camera. Used by the picker, the lobby, and the boot status screen.
 */
#ifndef THUMBYNES_FONT_H
#define THUMBYNES_FONT_H

#include <stdint.h>

#define NES_FONT_CELL_W   4
#define NES_FONT_CELL_H   6
#define NES_FONT_CELL_W2  8
#define NES_FONT_CELL_H2  12

/* Draw a NUL-terminated string at (x,y) into a 128×128 RGB565
 * framebuffer. Returns the x position after the last glyph. */
int nes_font_draw(uint16_t *fb, const char *text, int x, int y, uint16_t color);

/* 2× nearest-neighbour scaled draw — 6×10 glyphs in an 8×12 cell.
 * Used by the picker hero view for the large title underneath the
 * thumbnail so longish ROM names stay readable. */
int nes_font_draw_2x(uint16_t *fb, const char *text, int x, int y, uint16_t color);

/* Pixel width of a string when rendered (no wrapping). */
int nes_font_width(const char *text);
int nes_font_width_2x(const char *text);

#endif
