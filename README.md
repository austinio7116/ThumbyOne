# ThumbyOne

## The One Firmware

*One firmware to rule them all, one lobby to find them. One UF2 to bring them all, and in the Thumby bind them.*

Unified multi-boot firmware for the TinyCircuits Thumby Color. One UF2 image bundles any combination of:

- **ThumbyNES** — NES / SMS / GG / GB emulator
- **ThumbyP8** — PICO-8 player
- **ThumbyDOOM** — shareware DOOM
- **MicroPython + Tiny Game Engine** — the stock Thumby Color experience, with a C picker that boots straight into a chosen `/games/<name>` MicroPython app

A small lobby at the start of flash chains into the chosen slot. Each slot keeps the full 520 KB SRAM to itself and shares a 9.6 MB FAT volume for its content.

## Status

Functional. All four slots boot, the lobby grid-selector works, and transfers route through a single lobby-owned USB drive. See [`PLAN.md`](PLAN.md) for the original design doc.

## Flash layout

| Partition  | Offset      | Size       | Contents |
|-----------:|------------:|-----------:|----------|
| Lobby      | `0x000000`  | 128 KB     | Selector + USB MSC |
| NES        | `0x020000`  | 1 MB       | ThumbyNES firmware |
| P8         | `0x120000`  | 512 KB     | ThumbyP8 firmware  |
| DOOM       | `0x1A0000`  | 2.5 MB     | ThumbyDOOM + WAD   |
| MPY        | `0x420000`  | 2 MB       | MicroPython + engine + scratch |
| P8 scratch | `0x620000`  | 256 KB     | P8 active-cart working area |
| Shared FAT | `0x660000`  | 9.6 MB     | `/roms`, `/carts`, `/games`, saves, prefs |

## Controls

**Lobby (system selector):**
- D-pad → navigate 2×2 grid
- A → launch selected slot
- MENU → reboot lobby
- Plug in USB → drive appears; eject when done

**MPY picker:**
- D-pad (any axis) → step through games
- A → launch
- B → toggle favourite (★)
- MENU → open info overlay (battery, disk, sort order, return to lobby)

**Recovery chord:** hold LB + RB at boot to wipe and reformat the shared FAT.

## USB transfers

USB MSC lives in the lobby only. NES, P8, DOOM, and MPY slots don't enumerate a USB drive — returning to the lobby (MENU → Back to lobby, or MENU at boot) is the only way to drop files. This keeps the shared FAT single-writer at all times.

## Build

```
cmake -B build_device -DCMAKE_BUILD_TYPE=Release \
      [-DTHUMBYONE_WITH_NES=ON|OFF] \
      [-DTHUMBYONE_WITH_P8=ON|OFF] \
      [-DTHUMBYONE_WITH_DOOM=ON|OFF] \
      [-DTHUMBYONE_WITH_MPY=ON|OFF]
cmake --build build_device -j8
# -> build_device/thumbyone.uf2
```

All flags default to `ON`. Disabled slots are greyed out in the lobby grid and can't be launched.

ThumbyOne references sibling checkouts as live CMake subdirectories:

```
/your-work-dir/
    ThumbyNES/
    ThumbyP8/
    ThumbyDOOM/
    mp-thumby/
    ThumbyOne/     <-- this repo
```

Improvements in any subproject flow into the next ThumbyOne rebuild automatically. Each subproject remains independently buildable against its own `device/CMakeLists.txt` — the `THUMBYONE_SLOT_MODE` gate inside each slot keeps lobby-specific changes out of the standalone build.

## Flashing

Hold the lower d-pad button while plugging in USB to enter BOOTSEL, then drag `thumbyone.uf2` onto the `RPI-RP2350` drive that appears.

## Replacing lobby icons

`lobby/icons/*.png` hold 48×48 placeholder system icons. Replace them with real art (same file names, any 48×48 RGB image) and rebuild — `tools/pack_icons.py` runs automatically at build time and re-emits the 4-bit indexed data.

## Docs

- [`PLAN.md`](PLAN.md) — original multi-slot design
- `common/slot_layout.h` — canonical flash offsets (authoritative; keep in sync with `common/pt.json`)
- `common/thumbyone_handoff.h` — cross-slot handoff API
- Memory: `common/fs/` — shared FatFs + block device for the 9.6 MB volume
