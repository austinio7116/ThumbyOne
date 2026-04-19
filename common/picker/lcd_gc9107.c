/*
 * ThumbyNES — GC9107 LCD driver implementation.
 *
 * Init sequence is the panel-mandated register write order. The
 * actual register values come from the GC9107 datasheet startup
 * flow as documented for the Thumby Color hardware variant.
 */
#include "lcd_gc9107.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/resets.h"

/* Backlight: shared PWM driver (ThumbyOne common). Lobby + MPY
 * picker both link it; DOOM doesn't (it uses a plain gpio_put
 * from its own LCD driver). Also read /.brightness so the lobby
 * and MPY picker come up at the user's preferred level rather
 * than full-on + corrected later. */
#include "thumbyone_backlight.h"
#include "thumbyone_settings.h"

#define LCD_SPI            spi0
#define LCD_SPI_HZ         (80 * 1000 * 1000)

#define PIN_SCK   18
#define PIN_TX    19
#define PIN_CS    17
#define PIN_DC    16
#define PIN_RST    4
#define PIN_BL     7

#define LCD_W   128
#define LCD_H   128
#define LCD_PIXELS (LCD_W * LCD_H)

static int dma_ch = -1;
static dma_channel_config dma_cfg;

/* Send a command byte (and optional payload bytes). SPI is in 8-bit
 * mode for command/data writes; the framebuffer push later switches
 * to 16-bit mode. */
static void lcd_cmd(uint8_t cmd, const uint8_t *data, size_t len) {
    spi_set_format(LCD_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 0);                 /* command */
    spi_write_blocking(LCD_SPI, &cmd, 1);
    if (len > 0) {
        gpio_put(PIN_DC, 1);             /* data */
        spi_write_blocking(LCD_SPI, data, len);
    }
    gpio_put(PIN_CS, 1);
    spi_set_format(LCD_SPI, 16, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
}

static void lcd_set_window_full(void) {
    /* MADCTL: 0x00 = default orientation */
    lcd_cmd(0x36, (uint8_t[]){0x00}, 1);
    /* CASET 0..127 */
    lcd_cmd(0x2a, (uint8_t[]){0x00, 0x00, 0x00, 0x7f}, 4);
    /* RASET 0..127 */
    lcd_cmd(0x2b, (uint8_t[]){0x00, 0x00, 0x00, 0x7f}, 4);
    /* RAMWR — caller starts streaming pixels */
    lcd_cmd(0x2c, NULL, 0);
}

void nes_lcd_init(void) {
    /* SPI peripheral */
    spi_init(LCD_SPI, LCD_SPI_HZ);
    gpio_set_function(PIN_TX,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);

    /* Manual control pins */
    gpio_init(PIN_CS);  gpio_set_dir(PIN_CS,  GPIO_OUT); gpio_put(PIN_CS,  1);
    gpio_init(PIN_DC);  gpio_set_dir(PIN_DC,  GPIO_OUT); gpio_put(PIN_DC,  1);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT); gpio_put(PIN_RST, 1);

    /* Backlight: hardware PWM via the shared driver. Starts at 0
     * (off) so the panel init sequence runs silently — we turn it
     * up to the default after the panel is ready. */
    thumbyone_backlight_init();

    /* Hardware reset pulse */
    busy_wait_ms(5);
    gpio_put(PIN_RST, 0); busy_wait_ms(50);
    gpio_put(PIN_RST, 1); busy_wait_ms(120);

    /* Inter-register enable */
    lcd_cmd(0xFE, NULL, 0);
    lcd_cmd(0xEF, NULL, 0);

    /* Panel-mandated init values */
    lcd_cmd(0xB0, (uint8_t[]){0xC0}, 1);
    lcd_cmd(0xB1, (uint8_t[]){0x80}, 1);
    lcd_cmd(0xB2, (uint8_t[]){0x2F}, 1);
    lcd_cmd(0xB3, (uint8_t[]){0x03}, 1);
    lcd_cmd(0xB7, (uint8_t[]){0x01}, 1);
    lcd_cmd(0xB6, (uint8_t[]){0x19}, 1);

    lcd_cmd(0xAC, (uint8_t[]){0xC8}, 1);   /* RGB 5-6-5 */
    lcd_cmd(0xAB, (uint8_t[]){0x0F}, 1);

    lcd_cmd(0x3A, (uint8_t[]){0x05}, 1);   /* COLMOD = 16bpp */

    lcd_cmd(0xB4, (uint8_t[]){0x04}, 1);
    lcd_cmd(0xA8, (uint8_t[]){0x07}, 1);
    lcd_cmd(0xB8, (uint8_t[]){0x08}, 1);

    lcd_cmd(0xE7, (uint8_t[]){0x5A}, 1);   /* VREG_CTL */
    lcd_cmd(0xE8, (uint8_t[]){0x23}, 1);   /* VGH_SET */
    lcd_cmd(0xE9, (uint8_t[]){0x47}, 1);   /* VGL_SET */
    lcd_cmd(0xEA, (uint8_t[]){0x99}, 1);   /* VGH_VGL_CLK */

    lcd_cmd(0xC6, (uint8_t[]){0x30}, 1);
    lcd_cmd(0xC7, (uint8_t[]){0x1F}, 1);

    /* Gamma curves */
    lcd_cmd(0xF0, (uint8_t[]){
        0x05, 0x1D, 0x51, 0x2F, 0x85, 0x2A, 0x11,
        0x62, 0x00, 0x07, 0x07, 0x0F, 0x08, 0x1F
    }, 14);
    lcd_cmd(0xF1, (uint8_t[]){
        0x2E, 0x41, 0x62, 0x56, 0xA5, 0x3A, 0x3F,
        0x60, 0x0F, 0x07, 0x0A, 0x18, 0x18, 0x1D
    }, 14);

    lcd_cmd(0x11, NULL, 0);   /* SLPOUT */
    busy_wait_ms(120);
    lcd_cmd(0x29, NULL, 0);   /* DISPON */
    busy_wait_ms(10);

    /* DMA channel for SPI TX */
    dma_ch = dma_claim_unused_channel(true);
    dma_cfg = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
    channel_config_set_dreq(&dma_cfg, DREQ_SPI0_TX);

    /* Backlight on — honour /.brightness if the FAT is mounted
     * (default = full if the file isn't readable yet). */
    thumbyone_backlight_set(thumbyone_settings_load_brightness());
}

void nes_lcd_wait_idle(void) {
    if (dma_ch < 0) return;
    while (dma_channel_is_busy(dma_ch)) tight_loop_contents();
    /* Wait for SPI FIFO to drain too. */
    while (spi_get_hw(LCD_SPI)->sr & SPI_SSPSR_BSY_BITS) tight_loop_contents();
}

void nes_lcd_present(const uint16_t *fb_rgb565) {
    nes_lcd_wait_idle();

    lcd_set_window_full();

    /* Hold CS low + DC high while DMA streams pixels. */
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 1);

    dma_channel_configure(dma_ch, &dma_cfg,
        &spi_get_hw(LCD_SPI)->dr,
        fb_rgb565,
        LCD_PIXELS,
        true);
}

void nes_lcd_backlight(int on) {
    /* Binary on/off API retained for compatibility. "On" now means
     * "restore user-saved brightness"; "off" clamps up to the
     * FLOOR (still readable — see thumbyone_backlight_set). */
    if (on) thumbyone_backlight_set(thumbyone_settings_load_brightness());
    else    thumbyone_backlight_set(0);
}

void nes_lcd_teardown(void) {
    /* Wait for any in-flight DMA to finish so we don't yank the
     * rug while the panel is mid-frame. */
    nes_lcd_wait_idle();

    /* Deassert CS so the panel stops listening on SPI. */
    gpio_put(PIN_CS, 1);

    /* Stop and release the DMA channel. */
    if (dma_ch >= 0) {
        dma_channel_abort(dma_ch);
        dma_channel_cleanup(dma_ch);
        dma_channel_unclaim(dma_ch);
        dma_ch = -1;
    }

    /* Deinit SPI0 — the SDK helper disables and clears FIFOs. */
    spi_deinit(LCD_SPI);

    /* Reset SPI0 and the entire DMA peripheral via the RESETS
     * block, then bring them back out of reset. This puts both
     * into a known-clean hardware state for any subsequent user
     * (rom_chain_image in our case). */
    reset_block_mask(RESETS_RESET_SPI0_BITS | RESETS_RESET_DMA_BITS);
    unreset_block_mask_wait_blocking(
        RESETS_RESET_SPI0_BITS | RESETS_RESET_DMA_BITS);

    /* Hand the backlight pin back to a plain-GPIO full-on state so
     * the post-teardown user (rom_chain_image into a slot, or the
     * MPY engine's PIO-PWM driver) can reconfigure it without
     * our slice still driving the same pin. */
    thumbyone_backlight_release();
}

void nes_lcd_acquire(void) {
    /* Engine has already spi_init'd SPI0, PIO-PWM'd the backlight,
     * RST-pulsed the panel, and sent the full init sequence. All we
     * need is a DMA channel to push pixels through — the rest of
     * the hardware is good to use as-is. */
    if (dma_ch < 0) {
        dma_ch = dma_claim_unused_channel(true);
        dma_cfg = dma_channel_get_default_config(dma_ch);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
        channel_config_set_dreq(&dma_cfg, DREQ_SPI0_TX);
    }
    /* Drain any engine-in-flight SPI traffic so our first command
     * write (the window/RAMWR setup inside nes_lcd_present) starts
     * on a quiet bus. */
    while (spi_get_hw(LCD_SPI)->sr & SPI_SSPSR_BSY_BITS)
        tight_loop_contents();
}

void nes_lcd_release(void) {
    /* Softer than teardown: hands the LCD back to an already-
     * initialised driver in the same address space (the MPY
     * engine's GC9107 driver). The engine left SPI0 enabled and
     * its own DMA channel claimed; we must NOT reset either.
     *
     * Also: find the engine's OWN display DMA channel (the one
     * with DREQ_SPI0_TX set in its ctrl register) and abort it.
     * The engine may have been mid-transfer when our IRQ fired;
     * leaving the channel stuck in "busy" means the engine's next
     * `dma_channel_wait_for_finish_blocking(dma_tx)` hangs forever.
     * Aborting doesn't hurt — the engine's next reset_window()
     * starts a fresh RAMWR so the panel doesn't care about the
     * half-delivered pixels. */
    nes_lcd_wait_idle();
    gpio_put(PIN_CS, 1);
    if (dma_ch >= 0) {
        dma_channel_abort(dma_ch);
        dma_channel_cleanup(dma_ch);
        dma_channel_unclaim(dma_ch);
        dma_ch = -1;
    }

    /* Abort any other DMA channel still busy on SPI0 TX — that's
     * the engine's display DMA. Reading al1_ctrl doesn't trigger
     * the channel (unlike ctrl_trig). */
    for (uint ch = 0; ch < NUM_DMA_CHANNELS; ++ch) {
        uint32_t ctrl = dma_hw->ch[ch].al1_ctrl;
        bool busy = (ctrl & DMA_CH0_CTRL_TRIG_BUSY_BITS) != 0;
        uint treq = (ctrl & DMA_CH0_CTRL_TRIG_TREQ_SEL_BITS) >>
                    DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB;
        if (busy && treq == DREQ_SPI0_TX) {
            dma_channel_abort(ch);
        }
    }

    /* Drain any bytes still in the SPI TX FIFO now that no DMA
     * is refilling it. */
    while (spi_get_hw(LCD_SPI)->sr & SPI_SSPSR_BSY_BITS)
        tight_loop_contents();

    spi_set_format(LCD_SPI, 16, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
}
