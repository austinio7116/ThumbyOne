/*
 * ThumbyOne lobby — 4-bit indexed icon blitter.
 *
 * Icon data + the `lobby_icons[]` table are emitted by
 * tools/pack_icons.py into a separate compilation unit
 * (`lobby_icons_data.c`). This file only provides the runtime
 * decode function.
 */
#include "lobby_icons.h"

#define FB_W 128
#define FB_H 128

void lobby_icon_draw(uint16_t *fb, const lobby_icon_t *icon,
                     int dst_x, int dst_y) {
    const uint8_t  *src = icon->pixels;
    const uint16_t *pal = icon->palette;

    for (int y = 0; y < LOBBY_ICON_H; ++y) {
        int dy = dst_y + y;
        if (dy < 0 || dy >= FB_H) {
            /* Still advance `src` so we keep in sync with the row
             * we skipped — icon bytes are contiguous. */
            src += LOBBY_ICON_W / 2;
            continue;
        }
        for (int x = 0; x < LOBBY_ICON_W; x += 2) {
            uint8_t pair = *src++;
            int hi = (pair >> 4) & 0x0F;
            int lo =  pair       & 0x0F;
            int dx0 = dst_x + x;
            int dx1 = dst_x + x + 1;
            if ((unsigned)dx0 < FB_W) fb[dy * FB_W + dx0] = pal[hi];
            if ((unsigned)dx1 < FB_W) fb[dy * FB_W + dx1] = pal[lo];
        }
    }
}
