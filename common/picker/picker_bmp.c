/*
 * ThumbyOne picker — 16-bit RGB565 BMP loader. See header for
 * the interface contract and which BMP variants are accepted.
 */
#include "picker_bmp.h"

#include <string.h>
#include "ff.h"

static uint16_t rd16(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

int thumbyone_picker_bmp_load(const char *path,
                              uint16_t *out, size_t out_cap,
                              int *w, int *h) {
    if (!path || !out || !w || !h) return -1;

    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return -1;

    /* Read file + info headers (14 + 40 minimum; with V4/V5 headers
     * the bits-offset field points past the 40-byte V1 block so we
     * still only need the first 40 bytes of info to decode). */
    unsigned char hdr[54];
    UINT br = 0;
    if (f_read(&f, hdr, sizeof(hdr), &br) != FR_OK || br < sizeof(hdr)) {
        f_close(&f);
        return -2;
    }
    if (hdr[0] != 'B' || hdr[1] != 'M') { f_close(&f); return -3; }

    uint32_t pix_off    = rd32(hdr + 10);
    uint32_t info_size  = rd32(hdr + 14);
    int32_t  width      = (int32_t)rd32(hdr + 18);
    int32_t  height     = (int32_t)rd32(hdr + 22);
    uint16_t bpp        = rd16(hdr + 28);
    uint32_t compression = rd32(hdr + 30);

    if (info_size < 40)             { f_close(&f); return -4; }
    if (width <= 0 || width > 128)  { f_close(&f); return -5; }
    if (height <= 0 || height > 128){ f_close(&f); return -5; }
    if (bpp != 16)                  { f_close(&f); return -6; }
    if (compression != 3)           { f_close(&f); return -7; }  /* BI_BITFIELDS / RGB565 */
    if ((size_t)(width * height) > out_cap) { f_close(&f); return -8; }

    *w = (int)width;
    *h = (int)height;

    /* Jump to the pixel data. The row stride is width*2 bytes
     * padded up to a multiple of 4 — at 16 bpp, any even width
     * is already aligned, but we compute it formally for safety. */
    if (f_lseek(&f, pix_off) != FR_OK) { f_close(&f); return -9; }

    int row_bytes = width * 2;
    int pad = (4 - (row_bytes & 3)) & 3;  /* 0 for even widths */

    /* BMP rows are bottom-up. Read into the correct row from the
     * end so the output buffer is top-down. A small stack buffer
     * holds one row at a time. */
    unsigned char row_buf[128 * 2];
    for (int y = (int)height - 1; y >= 0; --y) {
        if (f_read(&f, row_buf, row_bytes, &br) != FR_OK || (int)br != row_bytes) {
            f_close(&f);
            return -9;
        }
        if (pad) {
            unsigned char pad_buf[4];
            if (f_read(&f, pad_buf, pad, &br) != FR_OK || (int)br != pad) {
                f_close(&f);
                return -9;
            }
        }
        /* Pack little-endian RGB565 bytes into uint16_t in the row. */
        uint16_t *dst = out + y * width;
        for (int x = 0; x < width; ++x) {
            dst[x] = rd16(row_buf + x * 2);
        }
    }

    f_close(&f);
    return 0;
}
