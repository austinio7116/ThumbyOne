# Flash-layout spike

Validates that two independent firmware images at different flash
offsets can coexist on a single RP2350 and cross-boot via
`rom_chain_image()`. If this works, ThumbyOne's multi-slot boot
architecture is viable; if not, Plan B (custom second-stage
bootloader in slot 0) kicks in.

## What it does

Builds two tiny "blinky" images from the same source file:

| Slot | Flash origin | Size window | Blink rate |
|------|--------------|-------------|------------|
| 0    | `0x10000000` | 128 KB      | ~1.25 Hz (slow) |
| 1    | `0x10020000` | 1 MB        | ~6 Hz (fast)    |

Each image:

- Blinks the LCD backlight (GPIO 7) at its slot's rate.
- On **A** press: calls `rom_chain_image()` to chain into the
  other slot.
- On **MENU** press: `watchdog_reboot()` (lands back in slot 0 by
  default since it's the boot ROM's default entry).

If `rom_chain_image` returns (meaning the target slot failed
validation — bad IMAGE_DEF, wrong size, etc.), the backlight does
a distinctive rapid triple-blink forever.

## Build

From the repo root:

```
cmake -S spike -B build_spike
cmake --build build_spike -j8
```

Outputs (in `build_spike/`):

- `blinky_slot0.uf2` — slot 0 image only
- `blinky_slot1.uf2` — slot 1 image only
- `spike_combined.uf2` — both slots in one UF2 (flash once)

## Flash + run

1. Power off the Thumby Color.
2. Hold **DOWN** on the d-pad and turn on → boots into BOOTSEL; a
   drive named `RP2350` appears on the host.
3. Copy `spike_combined.uf2` to the drive. Device reboots.
4. Observe: backlight blinks **slowly** (~1.25 Hz) → you're in
   slot 0.
5. Press **A** → backlight switches to **fast blink** (~6 Hz).
   You're in slot 1, chained to successfully.
6. Press **A** again → back to slow blink (slot 0).
7. Press **MENU** from any slot → watchdog reboots → lands in
   slot 0.

## What each outcome means

| Observed | Means |
|----------|-------|
| Slow blink on power-on | Slot 0 is linked + booting correctly |
| A → fast blink | `rom_chain_image` works; multi-slot architecture is viable |
| A → rapid triple-blink forever | Slot 1 image didn't validate — linker issue, missing IMAGE_DEF, bad size, etc. |
| Boot to flashing white / crash | Image metadata invalid; chain fell through |
| Nothing on screen | Slot 0 didn't boot — more fundamental linker problem |

## Plan B trigger

If `rom_chain_image` refuses to chain to a valid slot-1 image even
after diagnosing (e.g. because RP2350's BootROM demands a
partition-table entry for each image), switch to Plan B: slot 0
becomes a hand-rolled second-stage bootloader that manually jumps
to slot 1's entry vector after flipping the XIP/VTOR setup.
