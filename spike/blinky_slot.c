/*
 * ThumbyOne flash-layout spike — tiny blinky image.
 *
 * Two instances of this TU are built with different SPIKE_SLOT_ID
 * values and linked at different flash offsets. Each blinks the
 * LCD backlight at a slot-specific rate so the running slot is
 * visually obvious. Pressing the A button calls rom_chain_image()
 * to chain into the other slot.
 *
 * Success criterion: flash the combined UF2, power on, observe
 * slot 0's blink rate. Press A — observe slot 1's rate. Press A
 * again — back to slot 0. Proves:
 *   (a) two independent images at different flash offsets both
 *       build, link, and boot correctly;
 *   (b) rom_chain_image is the right RP2350 primitive for
 *       cross-slot dispatch;
 *   (c) no custom second-stage bootloader is required.
 */

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"

#ifndef SPIKE_SLOT_ID
#error "SPIKE_SLOT_ID must be defined (0 or 1)"
#endif

/* Slot 0 lives at 0x10000000. Slot 1 lives at 0x10020000.
 * Matches ThumbyOne/PLAN.md's flash layout for slots 0 and 1
 * offsets, minus any partition-table / handoff sector overhead. */
#define SLOT0_FLASH_BASE  0x10000000u
#define SLOT0_FLASH_SIZE  (128u * 1024u)

#define SLOT1_FLASH_BASE  0x10020000u
#define SLOT1_FLASH_SIZE  (1u * 1024u * 1024u)

/* GPIO map (from ThumbyNES). */
#define PIN_BL       7   /* LCD backlight */
#define PIN_BTN_A   21
#define PIN_BTN_MENU 26

#if SPIKE_SLOT_ID == 0
#define BLINK_ON_MS   400
#define BLINK_OFF_MS  400
#elif SPIKE_SLOT_ID == 1
#define BLINK_ON_MS   80
#define BLINK_OFF_MS  80
#else
#error "SPIKE_SLOT_ID must be 0 or 1"
#endif

/* Work area for rom_chain_image. Must be >= 3064 bytes, word-aligned. */
static uint8_t g_chain_workarea[3u * 1024u] __attribute__((aligned(4)));

static void init_gpio(void) {
    gpio_init(PIN_BL);
    gpio_set_dir(PIN_BL, GPIO_OUT);
    gpio_put(PIN_BL, 0);

    gpio_init(PIN_BTN_A);
    gpio_set_dir(PIN_BTN_A, GPIO_IN);
    gpio_pull_up(PIN_BTN_A);

    gpio_init(PIN_BTN_MENU);
    gpio_set_dir(PIN_BTN_MENU, GPIO_IN);
    gpio_pull_up(PIN_BTN_MENU);
}

static bool btn_a_pressed(void) {
    /* Active-low with internal pull-up. */
    return !gpio_get(PIN_BTN_A);
}

static bool btn_menu_pressed(void) {
    return !gpio_get(PIN_BTN_MENU);
}

static void chain_to_other_slot(void) {
#if SPIKE_SLOT_ID == 0
    rom_chain_image(g_chain_workarea, sizeof(g_chain_workarea),
                    SLOT1_FLASH_BASE, SLOT1_FLASH_SIZE);
#else
    rom_chain_image(g_chain_workarea, sizeof(g_chain_workarea),
                    SLOT0_FLASH_BASE, SLOT0_FLASH_SIZE);
#endif
    /* If rom_chain_image returns, it's an error — bad image,
     * missing IMAGE_DEF, etc. Fall through to a rapid triple-blink
     * so the failure is visible and distinct from the normal
     * slot rates. */
    while (1) {
        for (int i = 0; i < 3; ++i) {
            gpio_put(PIN_BL, 1); sleep_ms(50);
            gpio_put(PIN_BL, 0); sleep_ms(50);
        }
        sleep_ms(400);
    }
}

int main(void) {
    init_gpio();

    /* Brief settle pulse: on for 50 ms so there's an
     * unambiguous "I just booted" signal before the slow blink
     * pattern begins. */
    gpio_put(PIN_BL, 1); sleep_ms(50); gpio_put(PIN_BL, 0);
    sleep_ms(200);

    while (1) {
        if (btn_a_pressed()) {
            chain_to_other_slot();
        }
        if (btn_menu_pressed()) {
            /* Reboot to the default entry (slot 0). Validates
             * that a watchdog reboot lands cleanly. */
            watchdog_reboot(0, 0, 0);
        }
        gpio_put(PIN_BL, 1);
        sleep_ms(BLINK_ON_MS);
        gpio_put(PIN_BL, 0);
        sleep_ms(BLINK_OFF_MS);
    }
}
