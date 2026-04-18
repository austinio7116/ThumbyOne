/*
 * ThumbyOne picker — 16-bit RGB565 BMP loader.
 *
 * Reads a .bmp file from the mounted FAT and decodes it into a
 * caller-provided 16-bit framebuffer. Thumby-world BMPs are
 * always 16 bpp BI_BITFIELDS with the standard RGB565 masks
 * (R=0xF800, G=0x07E0, B=0x001F), positive-height (bottom-up),
 * any width/height up to 128 px. Anything else is rejected with
 * a negative error code — the picker falls back to a placeholder.
 */
#ifndef THUMBYONE_PICKER_BMP_H
#define THUMBYONE_PICKER_BMP_H

#include <stdint.h>
#include <stddef.h>

/* Load `path` as a 16 bpp RGB565 BMP. Decodes into `out` (row-
 * major, top-down, packed uint16 RGB565). Writes the image's
 * width and height to *w / *h. `out_cap` is the number of pixels
 * `out` can hold.
 *
 * Returns 0 on success, negative on error:
 *   -1  open failed
 *   -2  read too short
 *   -3  not a BMP (missing "BM")
 *   -4  bad info header size
 *   -5  width/height out of range (<= 0 or > 128)
 *   -6  unsupported bit depth (need 16)
 *   -7  unsupported compression (need BI_BITFIELDS)
 *   -8  output buffer too small
 *   -9  pixel read short / truncated file
 */
int thumbyone_picker_bmp_load(const char *path,
                              uint16_t *out, size_t out_cap,
                              int *w, int *h);

#endif
