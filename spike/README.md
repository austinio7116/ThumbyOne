# Flash-layout spike

Validates RP2350 multi-slot boot on the Thumby Color. A lobby
firmware at `0x10000000` and an app firmware flashed into a
declared partition at `0x10020000` can hand control back and
forth via the BootROM's `rom_chain_image` primitive and a plain
`watchdog_reboot`.

## What it proves

| Direction | Mechanism |
|-----------|-----------|
| Lobby → App | Lobby writes a magic into `watchdog_hw->scratch[0]`, triggers `rom_reboot(NORMAL)`. On the subsequent cold boot the lobby's `main()` checks scratch[0] *before* any peripheral init and — if the magic is present — clears it and calls `rom_chain_image` from a pristine chip state. BootROM validates and launches the app. |
| App → Lobby | App calls `watchdog_reboot(0, 0, 0)`. Chip resets. No magic in scratch → lobby takes the normal init path. |

## Why the handoff dance is required

On RP2350, `rom_chain_image` only works from a chip that hasn't
touched SPI / DMA / LCD. Called from an initialized lobby the
chain silently hangs somewhere inside bootrom. Doing a full
`rom_reboot` first gets us back to pristine post-bootrom state
for the chain call.

Discovery cost: a few dozen device flashes before arriving at
the scratch-magic pattern. Lessons are in the commit history.

## Flash layout

```
0x10000000   lobby image + embedded partition table
             (family: absolute)
0x10020000   partition 0 "app" — 256 KB window
             (lobby's pt.json declares this)
```

The partition table is embedded in the lobby via
`pico_embed_pt_in_binary` + `pt.json`. The BootROM reads it at
startup and `rom_chain_image` uses it to validate that the app's
flash window is a declared partition.

## Image relocation trick

The app is **linked at `0x10000000`** logically (default flash
origin), not at `0x10020000`. When `rom_chain_image` launches
the app, bootrom sets up the QMI ATRANS registers to remap the
XIP window from `0x10000000` to the partition's physical flash
offset. So the app's absolute code references resolve
correctly — they all point to what the CPU sees as `0x10000xxx`,
which ATRANS maps to physical flash `0x20xxx` (where the app's
bytes actually live).

The combiner (`tools/combine_uf2.py`) rebases the app UF2's
target addresses from `0x10000000` to `0x10020000` before
writing the combined `spike.uf2` so the bytes physically land
in partition 0.

## Build

```
cmake -S spike -B build_spike -DCMAKE_BUILD_TYPE=Release
cmake --build build_spike -j8
```

Outputs:

- `build_spike/lobby.uf2` — lobby alone
- `build_spike/app.uf2` — app alone (linked at `0x10000000`,
  **not** what you flash directly)
- `build_spike/spike.uf2` — combined, flash this

## Flash + run

1. Power off the Thumby Color.
2. Hold **DOWN** on the d-pad, turn on → BOOTSEL (`RP2350` drive
   appears on your host).
3. Copy `build_spike/spike.uf2` to it. Device reboots.
4. Lobby boots: cyan "SLOT 0 (LOBBY)" banner + log + steady
   backlight.
5. Press **A** → "A pressed -> reboot / (launching app...)" →
   brief black → backlight starts fast-flashing (~6 Hz). The
   panel still shows the lobby's last text because the app
   doesn't touch the LCD — the flashing BL is the app running.
6. Press **A** → `watchdog_reboot(0, 0, 0)` → chip resets →
   lobby comes back with steady backlight.
7. Press **MENU** in the lobby → lobby self-reset (same path
   used when no chain is wanted).

## Files

```
spike/
    pt.json             partition table (embedded in lobby)
    blinky_slot.c       both slots (SPIKE_SLOT_ID selects)
    lcd_gc9107.c/h      LCD driver (lobby only)
    font.c/h            bitmap font (lobby only)
    CMakeLists.txt      builds lobby + app, combines UF2
    README.md           this file
```

## References

- [pico-examples/bootloaders/encrypted](https://github.com/raspberrypi/pico-examples/tree/master/bootloaders/encrypted)
  — the canonical RP2350 `rom_chain_image` caller. Different
  target (SRAM, not flash) but same preamble pattern.
- [pico-bootrom-rp2350](https://github.com/raspberrypi/pico-bootrom-rp2350)
  — the source of truth for `rom_chain_image` validation and
  the QMI ATRANS setup. See `src/main/arm/varm_launch_image.c`.
- RP2350 datasheet, section 5 (Bootrom) — partition tables,
  image definition blocks, reboot types.
