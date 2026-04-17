# ThumbyOne — unified multi-boot firmware plan

**Goal.** One firmware image the user flashes once that bundles any
combination of:

- **NES/SMS/GG/GB** emulators (ThumbyNES)
- **PICO-8** (ThumbyP8, including its multi-cart reboot pathway)
- **DOOM** shareware (ThumbyDOOM, optional because of WAD size)
- **MicroPython + Tiny Game Engine** (stock Thumby Color experience)

A tiny boot lobby starts first, shows a system-selector
(NES / P8 / DOOM / MicroPython), and hands off to the chosen
system via watchdog reboot. Each system then runs *its own*
content picker (the same NES/P8-style C picker the user already
likes) on the shared FAT volume. Each system runs with the full
520 KB SRAM to itself — no co-residency. A single shared FAT
volume exposed over USB MSC carries all ROMs, carts, WADs, Python
games, and sidecar assets in per-system folders.

Critically, every slot uses the **same C picker** for its own
content. In the MicroPython slot that means: the picker is C,
MicroPython is only spun up when the user actually picks a Python
game — no Python picker code.

Build-time flags (`THUMBYONE_WITH_NES`, `_WITH_P8`, `_WITH_DOOM`,
`_WITH_MPY`) select which system slots are packaged into the final
UF2. All four enabled is the "deluxe" build; any subset produces a
smaller image with more room for the shared FAT.

The ThumbyOne build pulls from the existing sibling project
directories (`../ThumbyNES`, `../ThumbyP8`, `../ThumbyDOOM`,
`../mp-thumby`) as live CMake subdirectories — not by forking or
copying code. Improvements in those repos show up in the next
ThumbyOne rebuild automatically; each subproject remains
independently buildable on its own.

Not a goal for v1: same-millisecond system switching. A ~1-second
reboot between systems is fine. Also not a goal: commercial WAD
support, custom Doom mods, Arcade download flow, Thonny REPL inside
the unified build (Thonny remains available by flashing stock
mp-thumby separately).

This document supersedes `ThumbyNES/dualboot.md` and generalises its
two-slot scheme to N slots + a dedicated lobby slot.


## Why separate slots, not co-residency

From the existing footprint audit:

| System | Raw .bin on flash | Resident SRAM (.bss+.data+IRAM) |
|---|---|---|
| ThumbyNES (NES+SMS+GG+GB) | 630 KB | ~250 KB |
| ThumbyP8 | 390 KB | ~180 KB |
| ThumbyDOOM + shareware WHD | 2.37 MB (370 KB code + 2 MB WHD) | ~300 KB (Z_Zone heap) |
| MicroPython + TGE | ~1.8 MB (est.) | ~250–350 KB |

The 520 KB RP2350 SRAM can hold one of these at a time, never two.
BSS is statically placed by the linker and cannot be reclaimed at
runtime. So the only sane architecture is:

- One system is running with full SRAM ownership.
- Crossing systems = reboot via `watchdog_reboot()` into a different
  flash slot.
- A tiny lobby slot (128 KB) handles the top-level picker and
  dispatches the first boot after power-on.


## Flash layout

16 MB flash total. Worst-case layout with all four systems +
shareware DOOM enabled:

```
0x10000000  Lobby bootloader + system selector    64 KB     slot 0 (always present)
0x10010000  Handoff sector                         4 KB     (1 flash sector, CRC-protected)
0x10011000  (unused / reserved)                   60 KB
0x10020000  ThumbyNES slot                       1 MB       slot 1   [-DWITH_NES]
0x10120000  ThumbyP8 slot                       512 KB      slot 2   [-DWITH_P8]
0x101A0000  ThumbyDOOM slot (code + WHD blob)   2.5 MB      slot 3   [-DWITH_DOOM]
0x10420000  MicroPython + TGE slot                2 MB      slot 4   [-DWITH_MPY]
0x10620000  P8 active-cart scratch              256 KB      (preserved across reboots)
0x10660000  Shared FAT volume                    ~9.6 MB
0x11000000  end of flash
```

**Headroom per slot**: each slot is sized ~40–60% above the current
.bin size. NES and DOOM have the tightest margins — keep an eye on
them as features grow.

**Build-flag-aware layout**: the lobby reads a *build manifest* at a
fixed offset inside its own slot that lists which slot indices are
present. Disabled systems simply have their slot region absent from
the UF2 and removed from the manifest; the FAT volume grows to fill
the gap. A disabled-minimum build (lobby + MPY only) gives ~12.8 MB
FAT.

**Why a lobby slot instead of making NES the default boot**: decouples
the system selector from any single emulator, lets us add/remove
systems without touching others. Lobby only owns a tab-bar of
systems; each system slot owns its own content picker. Cost is
~64 KB of flash and one extra reboot hop on cold start.


## Handoff struct

4-KB flash sector (`slot 0 + 0x20000`). Written by whichever slot is
about to reboot, read by the target slot on first boot.

```c
#define DBHO_MAGIC   0x4F48_4244u    /* 'DBHO' */
#define DBHO_VERSION 2

struct thumbyone_handoff {
    uint32_t magic;
    uint32_t version;

    /* Target identification */
    uint8_t  target_slot;     /* 0=lobby, 1=NES, 2=P8, 3=DOOM, 4=MPY */
    uint8_t  action;          /* see actions below */
    uint16_t reserved0;

    /* Payload — meaning depends on action */
    char     path[96];        /* game/cart/ROM path on the FAT volume */
    uint32_t flags;           /* per-system flags (e.g. P8_RESUME_ACTIVE_CART) */

    /* Shared config read by every slot on boot */
    uint32_t overclock_hz;    /* 0 = default, else CPU freq target */
    uint8_t  volume_q15;      /* 0..15 (see settings) */
    uint8_t  brightness_q15;  /* 0..15 */
    uint16_t reserved1;

    uint32_t crc32;           /* over all preceding fields */
};
```

**Actions**:
- `0 ACTION_LOBBY` — boot into the lobby system selector
- `1 ACTION_OPEN_PICKER` — target slot boots into its own content
  picker (the normal case — lobby writes this when the user picks
  a system tab)
- `2 ACTION_P8_RESUME` — P8 slot: resume the cart whose bytecode is
  already programmed at `P8_ACTIVE_CART_FLASH_OFFSET` (see below).
  `path` holds the cart stem for display + save-file name.
- `3 ACTION_LAUNCH_PATH` — (optional, for future deep-links) target
  slot skips its picker and launches `path` directly

**CRC validation**: any slot that reads a corrupt handoff treats it
as `{target_slot=0, action=0}` and clears the sector. Prevents
reboot-loop states from half-written sectors.

**Who writes it**: any slot that wants to leave writes the sector
with its target-slot + action, then calls `watchdog_reboot()`.

**Who clears it**: the consuming slot clears the magic as soon as
it's read (one extra flash sector erase on boot). Prevents stale
handoff replay if a slot crashes and the device is power-cycled.


## Per-slot boot flow

On cold boot (RP2350 ROM → slot 0 by default):

```
Lobby slot 0 entry
    ├── Read handoff sector
    │     ├── magic mismatch / CRC fail → clear, draw system selector
    │     ├── target_slot == 0         → clear, draw system selector
    │     └── target_slot != 0         → clear, jump to slot N
    │                                     (alternate XIP start, see below)
    ├── System selector loop (tab bar only — no content lists)
    │     ├── D-pad / A picks a system icon (NES, P8, DOOM, MPY)
    │     ├── → write handoff {slot=N, action=OPEN_PICKER},
    │     │   watchdog_reboot()
    │     └── MENU → tiny settings page (volume, brightness,
    │                 overclock, flash wipe, build info)
```

Each system slot's entry:

```
Slot N entry  (N ∈ {1..4})
    ├── Read handoff
    │     ├── target_slot != N  → rewrite cleared, reboot to lobby
    │     │                       (got here by mistake / cold power-on
    │     │                       into a non-default slot)
    │     └── action dispatch below
    ├── ACTION_OPEN_PICKER → init LCD/buttons/FS/audio/MSC,
    │                        run slot's own C picker on its content
    │                        folder. Launch callback starts the
    │                        emulator / Lua VM / MicroPython.
    ├── ACTION_P8_RESUME   → (P8 only) skip cart load, run
    │                        bytecode already in scratch
    ├── ACTION_LAUNCH_PATH → bypass picker, launch `path` directly
    │                        (future: for deep-links, favourites
    │                        shortcut, Recent-item replay)
    └── In-game/in-emu menu → has a "Return to lobby" item
                              = write handoff {slot=0, action=LOBBY},
                              watchdog_reboot()
```

**Return-to-lobby is a menu item, not a chord.** MENU long-hold is
already spoken-for in the current firmwares (NES uses it for pause,
P8 uses it for the cart menu, DOOM uses it for its own menu). Each
system's existing in-game menu gains one new row — "Return to
lobby" — that writes the handoff and reboots. Power-cycle is also a
safe return-to-lobby path: no handoff → lobby draws its selector
on cold boot.

Games / emulators that lack an in-game menu (e.g. a raw
MicroPython game with no overlay) are expected to handle their own
exit condition and return control to their slot's picker, which
then has its own "Return to lobby" option on its menu. Worst case:
physical power-cycle is always a safe out.

**"Alternate XIP start"** on RP2350: the Pico SDK exposes
`pico_set_binary_type` + partition-table entries that mark each slot
with its own flash origin. The boot ROM honours these when
`watchdog_reboot()` is used with a slot-select argument. We
pre-populate a partition table at image build time that enumerates
all slots present in the build. Slot 0 is *always* the default boot
target; all other slots are only reached via explicit reboot.


## The P8 multi-cart reboot pathway — preservation plan

ThumbyP8's single biggest invariant: carts launch by writing their
`.luac` bytecode + ROM data into a dedicated **active-cart flash
region** (256 KB at offset 13 MB in the current single-firmware
layout), then calling `watchdog_reboot(0,0,0)`. On boot, the P8 main
reads the stem, maps the bytecode via XIP (`Proto.code[]` points
directly at flash — no heap copy), and runs.

Preserved in ThumbyOne:

1. **Reserve a dedicated P8 scratch region** in the new flash map
   (256 KB at `0x10630000` in the worst-case layout above). Size and
   alignment unchanged from current P8 build so no runtime changes
   inside the P8 VM.
2. **Two distinct P8 reboot flows** — both live entirely inside the
   P8 slot (lobby is not involved):
   - *Cart launch from the P8 slot's own picker*: user picks cart,
     picker calls `p8_cart_flash_program()` (same code as today)
     to write `.luac` + ROM to the scratch region, then
     `watchdog_reboot()`. On boot the P8 slot reads the cart stem
     and runs what's in the scratch region. Unchanged from today's
     flow — we just didn't rip it out.
   - *Sub-cart load from inside a running P8 game* (pico-8's
     `load()` function, used by multi-cart carts like Pinball
     Sandwich Flow): same mechanism, different trigger. Handoff
     carries `ACTION_P8_RESUME` + the stem; P8 slot skips
     re-programming the scratch region and runs what's there.
3. **.sav files live in FAT** (`/carts/<stem>.sav`) — unchanged.
4. **Scratch region survives reboots into any slot**. Lobby / NES /
   DOOM / MPY never touch offsets 13 MB–13.25 MB. Only the P8 slot
   erases or programs that region.

Gotcha: if a P8 cart uses `load()` to chain to another cart and the
user then returns to lobby mid-chain, the scratch holds a cart the
user didn't explicitly pick. Safe behaviour: the P8 slot's picker
never auto-resumes from scratch; picking a cart always triggers a
full program cycle. The `ACTION_P8_RESUME` path is used *only* for
P8 → P8 intra-session reboots.


## Shared FAT volume

Single FAT16 volume for all systems, accessed via USB MSC when the
device is plugged in in lobby or any picker screen.

Folder convention:

```
/NES/           *.nes
/SMS/           *.sms
/GG/            *.gg
/GB/            *.gb *.gbc
/carts/         *.p8 *.p8.png *.luac  (+ <stem>.sav, <stem>.claims)
/DOOM/          doom1.wad   (optional — if user drops a WAD, DOOM
                 slot prefers it over the baked-in shareware blob)
/Games/         <GameName>/main.py + assets/   (MicroPython games,
                 same layout as stock Thumby Color)
/.thumbyone/    configuration + caches
    build.json        manifest (slots present, firmware build id)
    .global           overclock + brightness + volume (shared)
    thumbnails/       64×64 RGB565 thumbnails generated by lobby
    last_played.txt   most recent N items for the "Recent" tab
```

**FS implementation**:

- Reuse the `nes_flash_disk` / `p8_flash_disk` FatFs diskio layer —
  it's already battle-tested in both. Move it into
  `device/common/ff_diskio.c` and link the same TU into every slot.
- All slots mount the same volume at boot with the same mkfs params
  (4 KB clusters, 512-byte sectors, FAT16).
- Windows File Explorer is happy with FAT16; MSC host-side is
  identical to current ThumbyNES/P8 behaviour.

**MicroPython FS swap**: stock mp-thumby uses LittleFS. The MPY slot
needs to be rebuilt with FatFs as the primary VFS, mounted at `/`
pointing at the same volume. MicroPython already supports FatFs —
it's a board-config change in mp-thumby's `mpconfigboard.h` +
`fatfs_port.c`, not a rewrite. Tests: Thonny upload flow continues
to work (writes to FAT instead of LittleFS), REPL `os.listdir('/')`
shows the expected folders.

**Existing LittleFS volumes on already-deployed Thumby Colors**:
first-boot of ThumbyOne detects LittleFS signature and offers a
"wipe to FAT" confirmation screen. The user's existing Python games
are unreachable from the unified build until migrated — acceptable
cost for getting everything in one volume.


## Lobby — system selector only

The lobby is deliberately small. It is **not** a content picker.
It shows one icon per enabled system, the user picks a system,
lobby reboots into that slot where the real content picker lives.

```
      ┌───────────────────────────────────────┐
      │              ThumbyOne                │
      │                                       │
      │     [NES]   [P8]   [DOOM]   [MPY]     │
      │      ^--                              │
      │                                       │
      │   press A to enter  •  MENU: settings │
      └───────────────────────────────────────┘
```

D-pad moves focus, A boots into the selected slot (handoff
`{slot=N, action=OPEN_PICKER}`), MENU opens a small settings
screen. Only tabs for slots compiled into the firmware appear —
build flags drive this.

Slot icons are baked into the lobby image as 32×32 RGB565 BMPs.
Content enumeration, thumbnails, favourites, Recent — none of
that lives in the lobby. Each system slot handles its own picker
on its own content in its own style (today's NES/P8 pickers,
unchanged).

**Rendering cost**: ~16 KB code + 32 KB framebuffer + font ≈
50–60 KB BSS. Fits in the 64 KB lobby slot. If it doesn't, slot
size bumps to 128 KB and the FAT volume shrinks by 64 KB — not
a serious budget worry.

**Settings propagation**: settings are saved to
`/.thumbyone/.global` on the FAT volume. Every slot reads this on
boot. Overclock field is honoured consistently — either all slots
at 250 MHz or all at 300 MHz, no mid-session surprises. (Already
the recommendation in `dualboot.md`.) Only the lobby's settings
page writes it.


## MicroPython slot — structure

The MPY slot is the new thing. Stock mp-thumby boots into Python
immediately (a `main.py` auto-run + the TinyCircuits arcade menu).
ThumbyOne's MPY slot boots into a **C picker** first. Only when
the user picks a Python game does the MicroPython interpreter get
initialised. No Python picker code exists.

Slot contents:

```
MPY slot image
    ├── C entry point (same pattern as NES/P8 slots)
    ├── Shared C picker (scans /Games/*/main.py)
    ├── MicroPython interpreter (mp-thumby, uninitialised at boot)
    ├── Tiny Game Engine C module (statically linked)
    └── FatFs VFS (mp-thumby mounts it at / once interpreter starts)
```

Boot flow:

```
MPY slot start
    ├── Read handoff
    ├── Init LCD, buttons, FatFs, audio PWM, USB MSC
    ├── ACTION_OPEN_PICKER (the common case):
    │     └── Run C picker on /Games/*/main.py
    │           ├── list entry per folder, icon from
    │           │   /Games/<Name>/icon.bmp (fallback: generic badge)
    │           ├── A on a game → call mpy_launch("/Games/Foo")
    │           │   │  → mp_init(), mp_import_stat(),
    │           │   │    pyexec_file("/Games/Foo/main.py")
    │           │   │  → game runs
    │           │   └── game returns / raises / SystemExit
    │           │       → mp_deinit() (tear down interpreter,
    │           │          free all heap)
    │           │       → return to picker loop
    │           └── Picker's menu has "Return to lobby" item
    └── ACTION_LAUNCH_PATH → (future deep-link) same launcher,
                              skipping the picker
```

Key deltas from stock mp-thumby firmware:

1. **Alternate XIP start** at `0x10420000` — linker script change;
   Pico SDK `pico_set_binary_type` with a slot offset. No code
   changes inside MicroPython itself.
2. **FatFs VFS** replaces LittleFS as the primary FS at `/`.
   `fatfs_port.c` already exists in the mp-thumby port; wire it to
   the same flash region (`0x10660000` + size) every other slot
   uses.
3. **Entry path is C, not `main.py`.** The mp-thumby image's main
   becomes a thin C wrapper that runs the shared picker *before*
   any Python is touched. The Python `main.py` auto-run is
   disabled (compile-time flag or a null `frozentext/main.py`).
4. **Game launch = `pyexec_file(path + "/main.py")`**. No
   `_thumbyone` native module, no handoff-reading in Python, no
   return-to-lobby chord — all of that is handled by the C picker
   before/after the game runs.
5. **Interpreter lifecycle is per-game**: `mp_init` on launch,
   `mp_deinit` on exit. Full heap reset between games, no stale
   globals / imports across runs. Same safety net on exception.
6. **Screenshots** (future): a small C helper callable from
   Python saves a 128×128 RGB565 sidecar next to the game folder.
   Not v1.

Behaviour the user sees:

- Boot → lobby → pick MPY tab → reboot.
- MPY slot picker lists Python games in the same NES/P8-style
  list / icon layout.
- Pick a game → it runs.
- Game exits → back at the MPY picker (no reboot, no lobby
  bounce — pick another Python game immediately).
- Picker menu → "Return to lobby" → reboot back to lobby.

**Thonny / REPL access**: not a boot target in ThumbyOne. Users
who want Thonny reflash stock mp-thumby temporarily — a deliberate
simplification. Could add an `ACTION_LAUNCH_PATH` with a
well-known `/system/repl` sentinel later if there's real demand.


## DOOM slot — WAD handling

The shareware doom1.whd is ~2 MB on flash. Two options:

**Option A (recommended for v1): bake WHD into the DOOM slot.**
DOOM slot is sized 2.5 MB; the WHD is `.incbin`-ed as today. Zero
runtime loading cost, zero FS dependency. Disable-DOOM build just
drops the whole 2.5 MB slot — that 2.5 MB becomes FAT headroom.

**Option B (future): WAD on the FAT volume.**
- `/DOOM/doom1.wad` is loaded at boot.
- Doom's `W_InitFile` path is modified to walk the FatFs cluster
  chain and pass a single XIP pointer range to the WAD reader (same
  trick ThumbyNES uses for zero-copy ROM loads). Requires the file
  to be contiguously allocated; FatFs doesn't guarantee this, so
  either (a) defrag on first boot or (b) copy into the P8-style
  scratch region on first launch.
- Enables user-supplied shareware WAD updates, and commercial WAD
  drops (`/DOOM/doom2.wad`, `/DOOM/ultdoom.wad`).

**Build-flag matrix**:

| Flag | Slot size cost | Effect |
|---|---|---|
| `THUMBYONE_WITH_DOOM=OFF` | 0 | DOOM tab hidden, slot absent, +2.5 MB FAT |
| `THUMBYONE_WITH_DOOM=SHAREWARE` | 2.5 MB | Bakes shareware WHD |
| `THUMBYONE_WITH_DOOM=FAT` | 0.5 MB | Reads WAD from `/DOOM/doom1.wad` at boot; requires user-provided WAD |

Default: `SHAREWARE`. Users who want commercial WADs flip to `FAT`.


## Shared ThumbyOne-only code

The subprojects already each contain their own (near-identical)
LCD / button / audio / FatFs / MSC / font drivers. ThumbyOne does
**not** try to dedupe those for v1 — leave them in place per slot.
Each subproject continues to be independently buildable; the cost
is ~20–40 KB of duplicated driver code per slot, which the flash
budget easily absorbs.

What ThumbyOne *does* own lives in `ThumbyOne/common/`:

```
ThumbyOne/common/
    handoff.c/h          (read/write/validate the handoff sector)
    settings.c/h         (read /.thumbyone/.global shared config)
    slot_jump.c/h        (watchdog_reboot + RP2350 slot-select wrapper)
    picker/
        picker_core.c/h  (the NES/P8-style picker — shared between
                          lobby and every slot, used for both the
                          system selector and content picking)
        picker_icons.c   (cart-style icons, generic badges)
    linker/
        slot0_lobby.ld
        slot1_nes.ld     (offset 0x10020000, size 1 MB)
        slot2_p8.ld      (offset 0x10120000, size 512 KB)
        slot3_doom.ld    (offset 0x101A0000, size 2.5 MB)
        slot4_mpy.ld     (offset 0x10420000, size 2 MB)
```

`picker_core.c` is the one piece of meaningful new code — a
parameterised version of the NES/P8 picker accepting:

- content folder + extension list
- icon-render callback (cart / thumbnail / bitmap / none)
- launch callback (per-slot: `nes_launch`, `p8_launch_cart`,
  `mpy_launch`, `doom_launch`)
- optional "Return to lobby" menu row

The lobby uses it to draw the top-level system selector (4-item
list, system icons). The MPY slot uses it to draw the `/Games/*`
list. NES and P8 slots *optionally* migrate to it later; v1 they
keep their existing pickers so no subproject churn is required.

**Deliberate non-goal for v1**: deduping the 3000-odd lines of
duplicated driver code across subprojects. Each subproject
continues to ship its own drivers and standalone firmware — we
just stitch their builds together into one UF2.

**Risk**: Pico SDK version pin diverging between slots. Enforce by
pointing every subproject's build at the same `PICO_SDK_PATH`
exported from ThumbyOne's top-level CMakeLists.


## Build system — pulls from live subproject folders

ThumbyOne's build does *not* copy or fork subproject code. It
references the sibling checkouts as-is:

```
/home/maustin/thumby-color/
    ThumbyNES/            (own CMakeLists, own build, still standalone)
    ThumbyP8/             ( "  )
    ThumbyDOOM/           ( "  )
    ThumbyOne/            (new — this folder)
    mp-thumby/            (alternate start + FatFs VFS patch applied
                           here, stays a standalone mp-thumby checkout)
```

`ThumbyOne/CMakeLists.txt` uses `add_subdirectory(... EXCLUDE_FROM_ALL)`
to pull each subproject's build in, passing per-slot variables that
each subproject's CMakeLists reads to decide:

- flash origin (from `slotN_*.ld` linker script)
- picker mode (`STANDALONE` = build own picker for own UF2,
  `THUMBYONE` = skip picker and expose emulator-core entry as a
  library target for ThumbyOne to link into its slot)
- whether to produce a standalone `.uf2` (off — we want the ELF
  only)
- shared PICO_SDK_PATH

```cmake
# ThumbyOne/CMakeLists.txt (sketch)
set(THUMBYONE_ROOT      ${CMAKE_CURRENT_SOURCE_DIR})
set(PICO_SDK_PATH       ${THUMBYONE_ROOT}/../mp-thumby/lib/pico-sdk)

# Common lib first (handoff, picker_core, settings, linker scripts)
add_subdirectory(common)

# Per-slot subbuilds — each emits a slot-local ELF
if (THUMBYONE_WITH_NES)
    set(THUMBYONE_SLOT_LD   ${THUMBYONE_ROOT}/common/linker/slot1_nes.ld)
    set(THUMBYONE_SLOT_MODE thumbyone)
    add_subdirectory(${THUMBYONE_ROOT}/../ThumbyNES
                     ${CMAKE_BINARY_DIR}/nes_slot  EXCLUDE_FROM_ALL)
endif()
# ... same shape for P8, DOOM, MPY
```

**Subproject-side changes needed** (small, additive, conditional —
each subproject still builds standalone when `THUMBYONE_SLOT_MODE`
is unset):

- Accept the `THUMBYONE_SLOT_LD` variable, use it in place of the
  default linker script if set.
- Accept `THUMBYONE_SLOT_MODE=thumbyone` to (a) skip emitting a
  standalone UF2, (b) keep building the emulator core as a normal
  executable target so ThumbyOne can consume its ELF.
- Swap to the shared handoff/settings libs via a small glue TU.

Those are ~30-line CMake diffs per subproject — non-invasive.

**Rebuild flow**:

```
# First time
cd ThumbyOne
cmake -B build_device -DCMAKE_BUILD_TYPE=Release \
      -DTHUMBYONE_WITH_NES=ON \
      -DTHUMBYONE_WITH_P8=ON \
      -DTHUMBYONE_WITH_DOOM=SHAREWARE \
      -DTHUMBYONE_WITH_MPY=ON
cmake --build build_device -j8
# -> build_device/thumbyone.uf2

# Later: improve ThumbyNES on its own, in its own folder
cd ../ThumbyNES
# ... hack, test standalone build as usual ...

# Back to ThumbyOne — picks up the changes automatically
cd ../ThumbyOne
cmake --build build_device -j8
# CMake sees the NES source timestamps changed, rebuilds just that
# slot's ELF, re-stitches the UF2.
```

No forking, no copying, no manual syncing. Subproject improvements
flow into ThumbyOne on the next `cmake --build`.

Output: `build_device/thumbyone.uf2` — one UF2 containing:

- Slot 0 image (lobby, from `ThumbyOne/lobby/`)
- Slot N images (one ELF per enabled subproject, each linked at
  its slot's flash origin)
- A partition-table header at offset 0 that the RP2350 boot ROM
  honours
- Build manifest at a fixed offset inside slot 0 (slots present,
  git SHA of each subproject, build date)

**Under-the-hood steps**:

1. Build `common/` as a CMake library (handoff, settings,
   picker_core, slot_jump, linker scripts).
2. For each enabled slot, invoke the subproject's CMake (via
   `add_subdirectory`) with slot-specific linker + mode flags.
   Output: one ELF per slot.
3. Build the lobby slot 0 ELF from `ThumbyOne/lobby/`.
4. `tools/combine_uf2.py` reads every ELF's flash segments,
   concatenates them into a single UF2 with correct page
   addresses + writes the partition table + build manifest.

**Build flags**:

- `THUMBYONE_WITH_NES={ON,OFF}` — default ON
- `THUMBYONE_WITH_P8={ON,OFF}` — default ON
- `THUMBYONE_WITH_DOOM={OFF,SHAREWARE,FAT}` — default SHAREWARE
- `THUMBYONE_WITH_MPY={ON,OFF}` — default ON
- `THUMBYONE_FAT_MIN_MB=<int>` — fail the build if the calculated
  FAT size drops below this; default 4.

**Directory layout**:

```
ThumbyOne/
    CMakeLists.txt
    PLAN.md
    README.md
    common/
        handoff.c/h
        settings.c/h
        slot_jump.c/h
        picker/picker_core.c/h
        linker/slot{0..4}_*.ld
    lobby/
        lobby_main.c
        lobby_icons.c
        CMakeLists.txt
    tools/
        combine_uf2.py
        build_manifest.py
    build_device/         (generated)
```

No `slots/` wrapper folder in ThumbyOne — slots are just the live
subproject directories next to ThumbyOne. The subprojects remain
the source of truth.


## Work order

Each step is independently testable. Don't skip ahead.

1. **Flash layout spike.** Write the slot linker scripts, build a
   no-op "blinky" image for each slot, flash manually, confirm
   each can be reached via `watchdog_reboot()` with the RP2350
   slot-select bits. Proves the alternate-XIP story works before
   any integration work starts.

2. **Subproject build hooks.** Small additive CMake diffs in
   ThumbyNES / ThumbyP8 / ThumbyDOOM: accept
   `THUMBYONE_SLOT_LD`, `THUMBYONE_SLOT_MODE`, don't emit
   standalone UF2 when set, point at shared `PICO_SDK_PATH`.
   Confirm each subproject still builds standalone when no flags
   are set.

3. **Common lib + handoff protocol.**
   `common/handoff.c`, `common/settings.c`, `common/slot_jump.c`
   with unit-ish tests. Dummy target slots read + print + clear.
   CRC edge cases covered.

4. **Lobby skeleton.** Minimal lobby slot: init LCD, buttons,
   FatFs (read-only for settings), draw the 4-icon system
   selector, handle handoff + reboot. Shows on power-on, picks a
   system, dispatches (slots don't exist yet — stub responses).

5. **NES slot integration.** Pull ThumbyNES in via
   `add_subdirectory`. Its own picker + menu continues to run
   inside the slot unchanged — ThumbyOne just relocates the link
   and adds the return-to-lobby menu row. Lobby picks NES tab,
   reboots into NES slot, user sees their NES picker, picks a
   ROM, plays, uses NES menu → "Return to lobby" item works.

6. **P8 slot integration + multi-cart preservation.** Same shape
   as (5). Verify the P8 active-cart scratch region at
   `0x10620000` survives lobby reboots intact. Write a test cart
   that `load()`s another cart and confirm the chain works
   across the reboot boundary (handoff carries `ACTION_P8_RESUME`).

7. **DOOM slot integration (shareware).** Simplest of the four —
   no picker, WHD baked in. DOOM slot boots straight into the
   game when selected from lobby. Add a DOOM in-game menu row
   "Return to lobby" (or repurpose an existing menu item).

8. **MPY slot: FatFs VFS swap.** Highest-risk step.
   First de-risk on stock mp-thumby outside ThumbyOne: rebuild
   with FatFs as the primary VFS, confirm Thonny / `os.listdir`
   still work, verify WAV/BMP loaders read from FAT cleanly.
   No ThumbyOne integration yet.

9. **MPY slot: alternate start + C picker entry.** With FatFs
   proven, apply the slot-start linker change and the C-wrapper
   main that runs the shared picker before initialising
   MicroPython. Launch callback calls `mp_init` +
   `pyexec_file`; on exit `mp_deinit`, back to picker.

10. **Lobby polish.** System-selector icons, settings page,
    `/.thumbyone/.global` round-tripping, overclock / brightness /
    volume sliders.

11. **USB MSC + FAT sharing polish.** Confirm Windows, macOS, and
    Linux all see the FAT volume cleanly. Confirm writes from
    host are picked up on next power-cycle. Hot-unplug safety
    (cache flush). MSC only active while in a picker (never
    during gameplay) — same invariant as current NES/P8.

12. **Build-flag matrix + UF2 combiner.** Every combo (2×2×3×2 =
    24, but prune to sensible subset) builds, runs the right FAT
    math, produces a working UF2. Smoke-test each on device.

13. **DOOM in FAT (optional).** Option B above.

14. **Polish + docs (open-ended).** Error screens for corrupt
    handoff, missing game, slot crash → auto-return to lobby,
    first-boot wipe-to-FAT prompt, README refresh.

Realistic total for steps 1-12 is bounded less by novel
engineering than by plumbing — most of the subproject code is
already working; we're wiring it together. The risky parts are
(1) the RP2350 alternate-slot boot and (8) the MicroPython FatFs
swap. Everything else is plumbing.


## Open questions

- **Partition table vs manual jump.** RP2350 boot ROM supports
  partition tables that describe slot layouts. Easier path. But if
  any slot needs a non-standard start that the partition API
  doesn't model, we'd fall back to a second-stage bootloader
  implemented inside slot 0. Needs a spike (step 1) to confirm.
- **mp-thumby checkout pin.** mp-thumby is referenced as a sibling
  path (`../mp-thumby`), same as the other subprojects — improvements
  there flow in on rebuild. No submodule needed; the user maintains
  the mp-thumby checkout themselves. If ThumbyOne moves to its own
  repo later, submodule-ise then.
- **Returning modified settings from a slot.** If a slot changes
  overclock mid-session, should the lobby pick that up on next
  boot? Easiest: slots can only *read* `/.thumbyone/.global`; only
  settings-page in lobby writes. Avoids race conditions with
  mid-session changes.
- **Handling a slot crash mid-session.** Watchdog will reboot →
  RP2350 ROM lands in slot 0 → lobby sees no handoff (or stale one
  from before) → shows lobby. Good default. A "last slot crashed"
  indicator on lobby would be nice (set a flag in handoff sector
  before calling the game, clear on clean return).
- **Flash writes while MSC is active.** If user saves a game while
  their PC has the FAT volume mounted, Windows may corrupt its
  cached FAT. Current NES/P8 behaviour: MSC is only active during
  the lobby picker, not during gameplay. Keep that invariant.
- **Screenshot sharing across slots.** Lobby should display
  thumbnails written by games. Agree on a single sidecar format
  (`<game>.scr32` RGB565 or `<game>.thumb.bmp` 64×64 indexed).
  Not blocking for v1, but decide before the first non-trivial
  thumbnail renderer lands.
- **Top-level repo layout.** For the initial build, ThumbyOne
  lives as a sibling folder of the subprojects and references
  them by relative path (`../ThumbyNES` etc.). If/when this goes
  upstream, promote ThumbyOne to its own repo with submodules
  pointing at the subproject repos. Not blocking for the initial
  build — the relative-path approach is fine for a local
  development setup.


## What NOT to do (scope management)

- **Don't fork the subprojects.** `add_subdirectory` from their
  live checkouts. Small additive CMake hooks only; each
  subproject still builds standalone.
- **Don't write a Python picker.** The MPY slot uses the same C
  picker every other slot uses. MicroPython only wakes up when
  the user launches a Python game.
- **Don't try to hot-swap between systems without reboot.** Cost
  is massive (relocatable BSS, dynamic linker), benefit is ~800 ms.
- **Don't reuse MENU long-hold for return-to-lobby.** Each system
  already uses MENU long-hold for its own purpose. Return-to-lobby
  is an in-game menu item, not a chord. Power-cycle is the
  always-safe fallback.
- **Don't bundle commercial WADs, mods, ROM packs.** FAT is user
  territory; firmware ships with shareware or nothing.
- **Don't dedupe the driver layer for v1.** Each subproject keeps
  its own LCD/button/audio/FS drivers. The flash budget absorbs
  the 20–40 KB per slot easily, and leaving the subprojects alone
  preserves the "subproject still works standalone" invariant.
