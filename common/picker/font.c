/*
 * ThumbyNES — minimal text renderer.
 *
 * 3×5 glyphs in a 4×6 cell. Glyph bitmap data was transcribed from
 * Pemsa (https://github.com/egordorichev/pemsa, MIT license) into
 * the same 16-bit row-major format used elsewhere in the project.
 * Pemsa license terms (MIT) preserved by attribution above.
 *
 * Encoding (per glyph, 16-bit little-endian):
 *   bit  0..2  = pixels at row 0 (LSB = leftmost)
 *   bit  3..5  = pixels at row 1
 *   bit  6..8  = pixels at row 2
 *   bit  9..11 = pixels at row 3
 *   bit 12..14 = pixels at row 4
 */
#include "font.h"
#include <string.h>

#define FB_W 128
#define FB_H 128

static const uint16_t glyph_unknown = 0x7fff;  /* solid 3×5 block */

static const uint16_t font[256] = {
    [' '] = 0x0000,
    [ 33] = 0x2092, [ 34] = 0x002d, [ 35] = 0x5f7d, [ 36] = 0x2f9f,
    [ 37] = 0x52a5, [ 38] = 0x7adb, [ 39] = 0x0012, [ 40] = 0x224a,
    [ 41] = 0x2922, [ 42] = 0x55d5, [ 43] = 0x05d0, [ 44] = 0x1400,
    [ 45] = 0x01c0, [ 46] = 0x2000, [ 47] = 0x1494,
    [ 48] = 0x7b6f, [ 49] = 0x7493, [ 50] = 0x73e7, [ 51] = 0x79a7,
    [ 52] = 0x49ed, [ 53] = 0x79cf, [ 54] = 0x7bc9, [ 55] = 0x4927,
    [ 56] = 0x7bef, [ 57] = 0x49ef,
    [ 58] = 0x0410, [ 59] = 0x1410, [ 60] = 0x4454, [ 61] = 0x0e38,
    [ 62] = 0x1511, [ 63] = 0x21a7, [ 64] = 0x636a,
    [ 65] = 0x5bef, [ 66] = 0x7aef, [ 67] = 0x624e, [ 68] = 0x7b6b,
    [ 69] = 0x72cf, [ 70] = 0x12cf, [ 71] = 0x7a4e, [ 72] = 0x5bed,
    [ 73] = 0x7497, [ 74] = 0x3497, [ 75] = 0x5aed, [ 76] = 0x7249,
    [ 77] = 0x5b7f, [ 78] = 0x5b6b, [ 79] = 0x3b6e, [ 80] = 0x13ef,
    [ 81] = 0x676a, [ 82] = 0x5aef, [ 83] = 0x39ce, [ 84] = 0x2497,
    [ 85] = 0x6b6d, [ 86] = 0x2b6d, [ 87] = 0x7f6d, [ 88] = 0x5aad,
    [ 89] = 0x79ed, [ 90] = 0x72a7,
    [ 91] = 0x324b, [ 92] = 0x4491, [ 93] = 0x6926, [ 94] = 0x002a,
    [ 95] = 0x7000, [ 96] = 0x0022,
    [ 97] = 0x5f78, [ 98] = 0x7ad8, [ 99] = 0x7278, [100] = 0x3b58,
    [101] = 0x72f8, [102] = 0x12f8, [103] = 0x7a78, [104] = 0x5f68,
    [105] = 0x74b8, [106] = 0x34b8, [107] = 0x5ae8, [108] = 0x7248,
    [109] = 0x5bf8, [110] = 0x5b58, [111] = 0x3b70, [112] = 0x1f78,
    [113] = 0x6750, [114] = 0x5778, [115] = 0x3870, [116] = 0x24b8,
    [117] = 0x6b68, [118] = 0x2f68, [119] = 0x7f68, [120] = 0x5aa8,
    [121] = 0x79e8, [122] = 0x7338,
    [123] = 0x64d6, [124] = 0x2492, [125] = 0x3593, [126] = 0x03e0,
    [127] = 0x0550,
};

static inline void put(uint16_t *fb, int x, int y, uint16_t c) {
    if ((unsigned)x < FB_W && (unsigned)y < FB_H) fb[y * FB_W + x] = c;
}

int nes_font_draw(uint16_t *fb, const char *text, int x, int y, uint16_t color) {
    if (!text || !fb) return x;
    int cur_x = x, cur_y = y;
    for (; *text; text++) {
        unsigned char ch = (unsigned char)*text;
        if (ch == '\n') { cur_x = x; cur_y += NES_FONT_CELL_H; continue; }
        uint16_t g = (ch < 128) ? font[ch] : glyph_unknown;
        for (int row = 0; row < 5; row++) {
            int bits = (g >> (row * 3)) & 0x7;
            if (bits & 0x1) put(fb, cur_x + 0, cur_y + row, color);
            if (bits & 0x2) put(fb, cur_x + 1, cur_y + row, color);
            if (bits & 0x4) put(fb, cur_x + 2, cur_y + row, color);
        }
        cur_x += NES_FONT_CELL_W;
    }
    return cur_x;
}

int nes_font_width(const char *text) {
    if (!text) return 0;
    int n = 0;
    for (; *text; text++) if (*text != '\n') n++;
    return n * NES_FONT_CELL_W;
}

/* 2× scaled draw — every glyph pixel becomes a 2×2 block. The glyph
 * grid is the same 3×5 source as nes_font_draw, so the rendered cell
 * is 6×10 inside an 8×12 cell. */
int nes_font_draw_2x(uint16_t *fb, const char *text, int x, int y, uint16_t color) {
    if (!text || !fb) return x;
    int cur_x = x, cur_y = y;
    for (; *text; text++) {
        unsigned char ch = (unsigned char)*text;
        if (ch == '\n') { cur_x = x; cur_y += NES_FONT_CELL_H2; continue; }
        uint16_t g = (ch < 128) ? font[ch] : glyph_unknown;
        for (int row = 0; row < 5; row++) {
            int bits = (g >> (row * 3)) & 0x7;
            int dy = cur_y + row * 2;
            for (int col = 0; col < 3; col++) {
                if (!(bits & (1 << col))) continue;
                int dx = cur_x + col * 2;
                put(fb, dx,     dy,     color);
                put(fb, dx + 1, dy,     color);
                put(fb, dx,     dy + 1, color);
                put(fb, dx + 1, dy + 1, color);
            }
        }
        cur_x += NES_FONT_CELL_W2;
    }
    return cur_x;
}

int nes_font_width_2x(const char *text) {
    if (!text) return 0;
    int n = 0;
    for (; *text; text++) if (*text != '\n') n++;
    return n * NES_FONT_CELL_W2;
}
