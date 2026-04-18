# ThumbyOne

## The One Firmware

*One firmware to rule them all, one lobby to find them. One UF2 to bring them all, and in the Thumby bind them.*

Unified multi-boot firmware for the TinyCircuits Thumby Color.

One UF2 image that bundles any combination of:

- **ThumbyNES** — NES / SMS / GG / GB emulator
- **ThumbyP8** — PICO-8 player
- **ThumbyDOOM** — shareware DOOM
- **MicroPython + Tiny Game Engine** — stock Thumby Color experience

A tiny boot lobby starts first and shows a system selector; picking a
system reboots into that slot. Each system has the full 520 KB SRAM
to itself and its own content picker on a shared FAT volume exposed
over USB MSC.

See [`PLAN.md`](PLAN.md) for the full design.

## Status

Early scaffold — flash-layout spike in progress. Not yet functional.

## Build

```
cmake -B build_device -DCMAKE_BUILD_TYPE=Release \
      -DTHUMBYONE_WITH_NES=ON \
      -DTHUMBYONE_WITH_P8=ON \
      -DTHUMBYONE_WITH_DOOM=SHAREWARE \
      -DTHUMBYONE_WITH_MPY=ON
cmake --build build_device -j8
# -> build_device/thumbyone.uf2
```

ThumbyOne references sibling checkouts as live CMake subdirectories:

```
/your-work-dir/
    ThumbyNES/
    ThumbyP8/
    ThumbyDOOM/
    mp-thumby/
    ThumbyOne/     <-- this repo
```

Improvements in any subproject flow into the next ThumbyOne rebuild
automatically. Each subproject remains independently buildable.
