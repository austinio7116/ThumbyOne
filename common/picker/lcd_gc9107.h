/*
 * ThumbyNES — minimal GC9107 LCD driver for Thumby Color (RP2350).
 *
 * 128×128 RGB565 panel, 4-wire SPI on spi0, 80 MHz.
 * Pin map (from board reference):
 *   GP18 = SCK
 *   GP19 = SDA (MOSI)
 *   GP17 = CS  (manual GPIO)
 *   GP16 = DC
 *   GP4  = RST
 *   GP7  = BL  (driven high = full brightness)
 *
 * The init sequence is the documented startup register write
 * order for the GC9107 panel — these are device-mandated values,
 * not creative work, and they match the published datasheet flow.
 */
#ifndef THUMBYNES_LCD_GC9107_H
#define THUMBYNES_LCD_GC9107_H

#include <stdint.h>

void nes_lcd_init(void);

/* Push a 128×128 RGB565 framebuffer to the panel. Blocks until the
 * previous DMA finishes, then kicks a new DMA transfer. */
void nes_lcd_present(const uint16_t *fb_rgb565);

/* Wait for any in-flight DMA push to complete. Call before reusing
 * the framebuffer for the next frame. */
void nes_lcd_wait_idle(void);

/* Backlight control. The Thumby Color drives BL via a single GPIO,
 * so we can only do on/off — no PWM dimming. Used by the sleep timer. */
void nes_lcd_backlight(int on);

/* Release SPI0 and the claimed DMA channel, then reset both
 * peripheral blocks. Call before rom_chain_image — on RP2350 the
 * bootrom uses hardware resources during image validation and
 * the LCD's active state can cause the chain to hang silently.
 * After teardown, no more nes_lcd_* calls may be made. */
void nes_lcd_teardown(void);

/* Softer release used when handing the LCD back to an already-
 * running driver (e.g. the MPY engine's display) rather than to
 * the bootrom. Waits for our DMA to drain, releases our DMA
 * channel, and deasserts CS — but does NOT reset the SPI0 or DMA
 * peripheral blocks. Resetting those would kill the engine's own
 * DMA channels (audio + display) and wedge it. */
void nes_lcd_release(void);

#endif
