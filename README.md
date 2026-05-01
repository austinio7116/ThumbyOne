# ThumbyOne

## The One Firmware

> *One firmware to rule them all, one lobby to find them.*
> *One file to bring them all, and in the Thumby bind them.*

ThumbyOne is a unified multi-boot firmware for the [TinyCircuits Thumby Color](https://thumby.us/) — the tiny colour handheld with a 128×128 screen, dual-core Arm Cortex-M33, 520 KB SRAM, and 16 MB of on-board flash. One flash gives you **NES**, **Master System**, **Game Gear**, **Game Boy**, **Mega Drive (Genesis)**, **PC Engine / TurboGrafx-16**, **PICO-8**, **DOOM**, and the full **MicroPython + Tiny Game Engine** experience, each optimized to run perfectly on the device.

<p align="center">
  <img src="docs/screenshots/nes-game.jpg" width="240" alt="NES on Thumby Color">
  <img src="docs/screenshots/p8-celeste.jpg" width="240" alt="Celeste Classic on PICO-8">
  <img src="docs/screenshots/doom-gameplay.jpg" width="240" alt="Doom on Thumby Color">
</p>

---

## Contents

- [What you get](#what-you-get)
- [Quickstart](#quickstart)
- [The lobby](#the-lobby)
- [Transferring files](#transferring-files)
- [Wiping / recovery](#wiping--recovery)
- [The systems](#the-systems)
  - [ThumbyNES](#thumbynes--nes--master-system--game-gear--game-boy)
  - [ThumbyP8](#thumbyp8--pico-8)
  - [ThumbyDOOM](#thumbydoom--shareware-doom)
  - [MicroPython + Tiny Game Engine](#micropython--tiny-game-engine)
- [Changelog](#changelog)
- [Tips and troubleshooting](#tips-and-troubleshooting)
- [Technical specifications](#technical-specifications)

---

## What you get

| System | What it plays | Content goes in |
|---|---|---|
| **ThumbyNES** | `.nes` (NES), `.sms` (Master System), `.gg` (Game Gear), `.gb` / `.gbc` (Game Boy + Color), `.md` / `.gen` / `.bin` (Mega Drive / Genesis), `.pce` (PC Engine / TurboGrafx-16 HuCard) | `/roms/` |
| **ThumbyP8** | `.p8.png` PICO-8 carts | `/carts/` |
| **ThumbyDOOM** | Shareware DOOM I — WAD baked into the firmware | *(none — embedded)* |
| **MicroPython + Engine** | Python games written against the [Tiny Game Engine](https://github.com/austinio7116/TinyCircuits-Tiny-Game-Engine) | `/games/<name>/` |

All four systems share one FAT drive, visible over USB when you're in the lobby. Size depends on the build: **8.6 MB in the default (MD-enabled) layout**, 9.6 MB in the backward-compat `THUMBYONE_WITH_MD=OFF` build.

---

## Quickstart

> ### ⚠️ Before you flash — please read
>
> Flashing ThumbyOne **replaces the stock TinyCircuits firmware** with a completely different system. This is a full takeover, not an overlay:
>
> - **The TinyCircuits launcher, stock games, and system files will be gone.** The shared FAT (8.6 MB in the default MD-enabled build, 9.6 MB with `THUMBYONE_WITH_MD=OFF`) is formatted on first boot of ThumbyOne — anything you had on the device (saves, scores, installed games) is wiped.
> - The device is easy to flash back to stock afterwards — see instructions below. But stock firmware **doesn't expose a USB drive** — to back up anything from stock first (e.g. save files under `/Saves/`), connect via [Thonny](https://color.thumby.us/pages/getting-started-with-thonny/getting-started-with-thonny/) or `mpremote` and pull files over the REPL. Do that **before** flashing ThumbyOne.
> - ThumbyOne uses its own filesystem layout (`/roms/`, `/carts/`, `/games/`) — stock `/Games/` Python games won't be visible until you move them into `/games/`.
> - There is **no going back to stock with your data intact unless you back it up first** once ThumbyOne has first-booted.
>
> If that all sounds fine, carry on.

---

### 1. Flash the firmware

**Download** [`firmware_thumbyone.uf2`](https://github.com/austinio7116/ThumbyOne/blob/main/firmware_thumbyone.uf2) from the root of this repo (or the latest [release](https://github.com/austinio7116/ThumbyOne/releases)) — or [build from source](#build-matrix).

> **Already running ThumbyOne 1.04 or earlier?** The 1.05 default build **moves the shared FAT and resizes it from 9.6 MB to 8.6 MB** to make room for Mega Drive / Genesis emulation. The old FAT is invisible to 1.05 — it gets auto-formatted on first boot and everything on it is wiped. **Back up `/roms/`, `/carts/`, `/games/`, `/Saves/` etc. over USB from 1.04 first** (instructions above in the [1.05 changelog callout](#whats-new-in-105)). After flashing, copy back as much as fits — the new ceiling is 8.6 MB.
>
> If you don't want Mega Drive / Genesis support, flash a `THUMBYONE_WITH_MD=OFF` build instead — keeps the original 9.6 MB FAT layout so your existing volume mounts untouched. The `firmware_thumbyone_nomd.uf2` file at the repo root is a prebuilt WITH_MD=OFF image.

1. Power off the Thumby Color.
2. Hold **DOWN** on the d-pad and plug in USB.
3. The device appears as an `RPI-RP2350` drive on your computer.
4. Drag `firmware_thumbyone.uf2` onto it.
5. The device reboots into ThumbyOne. On first boot you'll see a brief "formatting shared FAT..." splash while the new layout is prepared, then the lobby appears.

### 2. Upload ROMs / carts / games

With ThumbyOne running, plug the device in (while sitting in the **lobby**). It enumerates as a USB drive called **ThumbyOne Storage**. Copy files into the right folder:

| Content | Folder | File types |
|---|---|---|
| NES / SMS / GG / GB / MD ROMs | `/roms/` | `.nes`, `.sms`, `.gg`, `.gb`, `.gbc`, `.md`, `.gen`, `.bin` |
| PICO-8 carts | `/carts/` | `.p8.png` |
| MicroPython games | `/games/<Name>/` | Folder per game with `main.py`, `icon.bmp`, `arcade_description.txt`, assets |

**Example:**
```
/roms/
    Super Mario Bros.nes
    Sonic.gg
    Tetris.gb
/carts/
    celeste.p8.png
    delunky.p8.png
/games/
    DeepThumb/
        main.py
        icon.bmp
        arcade_description.txt
        assets/
```

When you're done copying, **eject the drive** (Windows: right-click → Eject; macOS: drag to Trash; Linux: `sync && umount`). The on-screen USB dot turns from blue back to dim-grey; the physical LED goes back to green. Now pick a system with the d-pad and press **A**.

To transfer more files later: from inside any running system, **hold MENU** → back to lobby → plug USB → repeat. See [Transferring files](#transferring-files) for more on the USB state machine and LED indicators.

### 3. Returning to stock

Want to go back to TinyCircuits stock firmware later? You can — see [Returning to stock](#returning-to-stock) under Tips and troubleshooting. It's a single drag-and-drop with the bundled `firmware_thumbyone_revert.uf2`, after which the device is back to factory state and behaves like any other stock Thumby Color.

---

## The lobby

The lobby is the home screen. It's a 2×2 grid of system icons: NES, PICO-8, DOOM, and MicroPython. Move with the **d-pad**, press **A** to launch.

<p align="center">
  <img src="docs/screenshots/lobby.jpg"      width="380" alt="ThumbyOne lobby — 2x2 system grid">
  <img src="docs/screenshots/lobby-menu.jpg" width="380" alt="Lobby MENU overlay — battery, disk, USB, firmware">
</p>

**Controls:**

| Button | Action |
|---|---|
| D-pad | Move selection between the four tiles |
| **A** | Launch the selected system |
| **MENU** | Open the lobby overlay (volume + brightness sliders, battery, disk, USB, firmware) |
| **MENU** (held at boot) | Force lobby (bypass any pending slot chain) |
| **LB + RB** (held at boot) | Wipe and reformat the shared FAT |

Inside the MENU overlay, **LEFT / RIGHT** adjusts the highlighted slider (brightness or volume). Changes apply live — the backlight dims as you scrub — and persist to the shared FAT so every slot picks them up on the next launch.

**Getting back to the lobby** depends on which slot you're in — each system has a native pause / picker menu with a **Back to lobby** item:

| Slot | Return gesture |
|---|---|
| ThumbyNES (NES / SMS / GG / GB) | **MENU** (hold ~0.5 s in-game) → pause menu → **Back to lobby** |
| ThumbyP8 (PICO-8) | **MENU** (hold ~0.5 s in-game) → PICO-8 pause menu → **Back to lobby** |
| ThumbyDOOM | In-game Main Menu → **Quit Game** (no confirm dialog in slot mode) |
| MicroPython + Engine | **MENU** held ~5 s in-game — direct reboot to the lobby (no on-screen prompt; game state is lost, so the hold is deliberately long to prevent accidents) |

A small **USB** label + LED dot in the top-right corner of the lobby — and the device's physical RGB LED — both show the USB state:

| On-screen dot | Physical LED | Meaning |
|---|---|---|
| green | green | USB cable not connected (idle) |
| blue  | blue  | Host has mounted the drive — safe to drop files |
| red   | red   | Transfer in flight — **do not unplug** |

The physical LED mirrors the on-screen dot so you can see at a glance whether a transfer is still happening even without looking at the screen. When a copy finishes the LED settles back to blue; when you eject or unplug, it goes back to green.

Slot-launch is held off while USB is active: if you're mid-copy and press A, ThumbyOne waits for the FAT to go quiet before handing off, so a half-written file never turns into a corrupt one on the slot.

---

## Transferring files

ThumbyOne exposes a **single** USB drive, and only while you're **in the lobby**. Sub-systems don't have their own USB drives — this is deliberate, and it's what "One firmware, one lobby" means:

- There is never a moment where both a host and a running slot are writing to the FAT.
- Windows / macOS / Linux see one device, with one drive letter, one identity.
- "Did I land in NES's drive or P8's drive?" is a question that no longer exists.

**Workflow:**

1. Boot into the lobby.
2. Plug in USB. A drive appears named **ThumbyOne Storage**.
3. Drop files into the right folder:
   - ROMs into `/roms/` (any of `.nes`, `.sms`, `.gg`, `.gb`)
   - PICO-8 carts into `/carts/` (`.p8.png`)
   - MicroPython games into `/games/<GameName>/` (a folder per game with `main.py` + assets)
4. Eject the drive (Windows: right-click → Eject; macOS: drag to Trash; Linux: `sync && umount`).
5. Pick a system with the d-pad, press A.

To transfer more later: pick MENU inside any system → **Back to lobby** → plug in → repeat.

---

## Wiping / recovery

Two escape hatches for when something goes wrong:

**Hold MENU at boot** → forces the lobby even if a pending slot-chain would otherwise try to start a broken slot. Useful after a bad flash or a hang.

**Hold LB + RB at boot** → the lobby prompts you to keep both held for a one-second countdown, then wipes and reformats the whole shared FAT (8.6 MB in the default MD-enabled build, 9.6 MB in `THUMBYONE_WITH_MD=OFF` builds). Erases all ROMs, carts, games, and saves. Only needed if the FAT itself is corrupt (no slot can read it, or the PC says "unformatted disk").

No driver weirdness, no Windows Format dialog, no `mpremote` incantations. LB + RB at boot is the canonical wipe.

---

## The systems

### ThumbyNES — NES / Master System / Game Gear / Game Boy / Mega Drive / Genesis / PC Engine

*Based on [ThumbyNES](https://github.com/austinio7116/ThumbyNES) — see that repo for the standalone firmware, the full feature list, and detailed docs.*

<p align="center">
  <img src="docs/screenshots/nes-picker.jpg" width="240" alt="ThumbyNES hero picker">
  <img src="docs/screenshots/nes-game.jpg" width="240" alt="Super Mario Bros. 3">
  <img src="docs/screenshots/nes-menu.jpg" width="240" alt="In-game menu overlay">
</p>

A six-in-one retro emulator running Nofrendo for NES, smsplus for Master System + Game Gear, Peanut-GB (fhoedemakers' CGB fork + minigb_apu) for Game Boy DMG and Color, PicoDrive for Sega Mega Drive / Genesis, and HuExpress (ODROID-GO fork of Hu-Go!) for PC Engine / TurboGrafx-16 HuCards. Drop `.nes`, `.sms`, `.gg`, `.gb`, `.gbc`, `.md`, `.gen`, `.bin`, or `.pce` into `/roms/`; the tabbed picker groups them by system, shows thumbnails and metadata, and lets you favourite.

**MD note:** PicoDrive adds ~850 KB of precomputed flash tables (FAME jumptable + YM2612 + cz80 SZHVC), pushing the NES slot past the default 1 MB partition. ThumbyOne's default build (`THUMBYONE_WITH_MD=ON`) grows the NES partition to 2 MB to hold it, shifting every downstream partition and the shared FAT up by 1 MB. Build with `-DTHUMBYONE_WITH_MD=OFF` for the backward-compatible 1 MB layout (NES / SMS / GB only, original 9.6 MB FAT).

**PCE note:** The PC Engine core is the smallest of the six in flash terms — about **70 KB code + 5 KB BSS + 97 KB working heap** after a HuCard-only trim (CD support, the 74 KB CD-track table, Arcade Card, netplay, and the SDL / Haiku / iniconfig frontends are all stripped at vendor time). It fits comfortably inside the existing slot in **both** the `WITH_MD=ON` and `WITH_MD=OFF` builds, so adding PCE in 1.08 doesn't change the partition layout — the 8.6 MB / 9.6 MB FAT stays exactly where it was in 1.07, with no migration on upgrade. PCE rendering is done with a custom per-scanline compositor that writes straight into the LCD framebuffer (the upstream `XBUF` / `SPM` / `VRAM2` / `VRAMS` scratch buffers were 568 KB combined and didn't fit on Thumby Color); the h6280 dispatcher runs from static IRAM for performance; PSG audio mixes mono through the same PWM mixer as the other cores with a single-pole low-pass matching the analog DAC rolloff. Six-emulator coexistence in the slot also drove a heap-leak audit in HuExpress and (incidentally) smsplus — see the [ThumbyNES v1.08 changelog](https://github.com/austinio7116/ThumbyNES#v108--pc-engine--turbograf16-tab-strip-facelift-lcd-reliability) for the full technical write-up.

<p align="center">
  <img src="docs/screenshots/nes-sms.jpg" width="240" alt="Sonic on Master System">
  <img src="docs/screenshots/nes-gb.jpg" width="240" alt="Super Mario Land on Game Boy">
</p>

<p align="center">
  <img src="docs/screenshots/md-picker.jpg" width="240" alt="Mega Drive ROMs in the picker">
  <img src="docs/screenshots/md-title-sonic.jpg" width="240" alt="Sonic 1 on Mega Drive">
  <img src="docs/screenshots/md-cannon-fodder.jpg" width="240" alt="Cannon Fodder on Mega Drive (newly playable in 1.06)">
</p>

<!-- TODO: PCE shot row — capture once 1.08 is flashed -->

**Features:**

- Per-ROM save states, per-ROM settings, favorites
- In-game pause menu (MENU button)
- Fast-forward, palette switching, idle sleep
- Live-pan read mode for Game Boy / GG (the 128×128 screen is narrower than the native output; pan to see the edges)
- Play-while-cropped pan chord (LB + d-pad) for MD and PCE — pan the source viewport without losing game control
- Game Boy Color carts run with their native CGB palette (DMG carts use the six built-in shade palettes)
- **Cart-RTC support for Pokemon Crystal / Gold / Silver** (new in 1.11) — the GB cart's MBC3 real-time clock is driven from the lobby-set BM8563, so day-night cycle, time-of-day-only encounters, and berry growth all work correctly. Other GBC RTC carts (Harvest Moon GB 2, etc.) get the same fix for free
- Chained-XIP fallback — fragmented ROMs still map from flash and run full-speed without a defrag
- Cluster-level defragmenter with before/after preview and live move visualisation (picker menu → **Defragment now**)
- Configurable CPU clock per-ROM
- Hand-painted 12×8 tab icons in the picker (new in 1.08) — same bitmaps drive the no-screenshot placeholder thumbnails

**ThumbyOne differences:**

- ROMs live in **`/roms/`** on the shared FAT (stock ThumbyNES puts them at the root).
- USB transfers happen in the ThumbyOne lobby, not here — returning to the lobby is the "drop a new ROM" workflow.
- The standalone ThumbyNES logo splash and file-scan diagnostic are skipped — you go straight from the lobby's handoff into the picker.
- The in-game menu's **Back to lobby** item cleanly unmounts the FAT and hands off.

**Controls:**

| Button | Action |
|---|---|
| D-pad | Navigate picker / drive in-game |
| LB / RB | Switch tabs (picker) / shoulder buttons (in-game) |
| A / B | Launch / in-game A & B |
| MENU (tap) | Open picker menu |
| MENU (held ~0.5 s, in-game) | Open the in-game pause menu (contains **Back to lobby**, save-state, palette, fast-forward, etc.) |
| Hold B (on picker) | Toggle favourite for the highlighted ROM |

#### Defragmenter

ThumbyNES has a full-volume FAT defragmenter with a preview-and-confirm UX and live cluster-map visualisation, reachable from the picker menu's **Defragment now** action. It's only needed occasionally — chained-XIP means fragmented carts still load and play — so the full write-up lives under [Tips and troubleshooting → FAT defragmenter](#fat-defragmenter) rather than taking up space here.

### ThumbyP8 — PICO-8

*Based on [ThumbyP8](https://github.com/austinio7116/P8Thumb) — a clean-room PICO-8 runtime. PICO-8 is a trademark of [Lexaloffle](https://www.lexaloffle.com/pico-8.php); if you enjoy playing carts, **please buy PICO-8** to support the creators and the community.*

<p align="center">
  <img src="docs/screenshots/p8-celeste.jpg" width="240" alt="Celeste Classic">
  <img src="docs/screenshots/p8-delunky.jpg" width="240" alt="Delunky">
  <img src="docs/screenshots/p8-picker.jpg" width="240" alt="PICO-8 picker">
</p>

A full PICO-8 fantasy console with 4-channel audio, the native 128×128 display, and cart conversion that runs on-device. Drop `.p8.png` cart files into `/carts/`; the next boot converts them into playable bytecode (one cart per reboot cycle, a few seconds each), and you're off.

<p align="center">
  <img src="docs/screenshots/p8-menu.jpg" width="320" alt="P8 picker menu">
</p>

**Features:**

- Favorites + most-played sort modes
- In-game pause menu with brightness, volume, save-state
- Multi-cart chain (PICO-8's `load()` call works across reboots)
- On-device `.p8.png` → bytecode conversion — no host tools

**ThumbyOne differences:**

- Carts live in **`/carts/`** on the shared FAT (same as standalone P8).
- The standalone "welcome, drop carts" lobby screen is skipped — direct to picker.
- Menu has a **Back to lobby** entry.

**Controls:**

| Button | Action |
|---|---|
| D-pad | D-pad |
| A | X / confirm |
| B | O / cancel |
| MENU (tap, in picker) | Open the picker menu (sort, favourites, back to lobby) |
| MENU (held ~0.5 s, in-game) | Open PICO-8 pause menu (save state, back to lobby, cart `menuitem()` entries) |

### ThumbyDOOM — shareware DOOM

*Based on [ThumbyDOOM](https://github.com/austinio7116/ThumbyDOOM) — based on Graham Sanderson's rp2040-doom.*

<p align="center">
  <img src="docs/screenshots/doom-title.jpg" width="240" alt="DOOM title screen">
  <img src="docs/screenshots/doom-gameplay.jpg" width="240" alt="DOOM gameplay">
  <img src="docs/screenshots/doom-menu.jpg" width="240" alt="DOOM in-game menu">
</p>

The real deal. Music, sound effects, save games, screen melts, all on a 128×128 LCD. The shareware WAD is baked into the firmware; no files to transfer.

**Features:**

- Full shareware DOOM I (E1M1 – E1M9)
- 12-bit PWM DAC audio with dithering — OPL2 music (via [emu8950](https://github.com/digital-sound-antiques/emu8950)) + 8-channel ADPCM SFX mixed on core1
- Save / load to flash (6 save slots)
- Overlay menu (**MENU long-press**) with brightness, gamma, volume, controls scheme (CLASSIC / SOUTHPAW / BA STRAFE), and cheats (god / all-weapons / no-clip / level warp)
- **Front-LED health indicator** — smooth green → yellow → red blend as HP drops from 100 → 0. Hidden on title / demo; only active during actual gameplay. Gamma-balanced against the Thumby's LED dies (green die is dimmer than red, so "yellow" caps R at ~96/255 for a visually-balanced midpoint).
- Persistent settings (slot 7) survive power cycles

**ThumbyOne differences:**

- WAD is in the firmware itself (1.8 MB shareware), so DOOM never touches the shared FAT — it plays just fine even on a freshly-wiped device.
- The in-game **Quit Game** menu item returns to the lobby directly — the vanilla "Are you sure? (Y/N)" confirm dialog is short-circuited under `THUMBYONE_SLOT_MODE`.
- System-wide volume + brightness from `/.volume` and `/.brightness` are honoured; changing them in the overlay menu writes back to the shared settings sector.

**Controls:**

Three switchable schemes (overlay menu → **Controls**: CLASSIC,
SOUTHPAW, BA STRAFE). Default is CLASSIC. Common bindings:

| Button | Action |
|---|---|
| D-pad U/D | Move forward / back |
| MENU (tap) | Main Menu (Save / Load / Options / Quit Game) |
| MENU (long-press) | Overlay menu (cheats, gamma, volume, warp) |

Per-scheme bindings:

| | CLASSIC | SOUTHPAW | BA STRAFE |
|---|---|---|---|
| D-pad L/R | Turn | Strafe | Turn |
| LB / RB | Strafe L / R | Turn L / R | **Use** / **Fire** |
| A | Fire / confirm | Fire / confirm | Strafe right / confirm |
| B | Use (hold = automap) / cancel | Use (hold = automap) / cancel | Strafe left / cancel |
| Use **+** trigger | B + LB / B + RB = prev / next weapon | B + LB / B + RB = prev / next weapon | LB + B / LB + A = prev / next weapon |
| Long-press use | B held → automap | B held → automap | LB held → automap |

### MicroPython + Tiny Game Engine

*The stock Thumby Color experience — [TinyCircuits-Tiny-Game-Engine](https://github.com/austinio7116/TinyCircuits-Tiny-Game-Engine) plus MicroPython.*

<p align="center">
  <img src="docs/screenshots/mpy-picker.jpg" width="380" alt="MPY hero picker — DeepThumb">
  <img src="docs/screenshots/mpy-menu.jpg"   width="380" alt="MPY picker menu overlay">
  <img src="docs/screenshots/mpy-game.jpg"   width="380" alt="DeepThumb chess — running MPY game">
</p>

MicroPython with the Tiny Game Engine C module baked in, running a custom C picker that replaces the stock launcher entirely. Drop a game folder into `/games/<GameName>/` with a `main.py`, an `icon.bmp`, and an `arcade_description.txt`, and it appears on the hero picker with full artwork, title, and blurb.

**Features:**

- One-game-per-screen hero picker with 64×64 icons + description blurb
- Favourites (press **B** on any game to star it — no menu needed)
- Three sort modes: Name, Favourites first, Author
- Menu overlay with live battery + free-disk + sort selector + back-to-lobby, matching the NES menu style
- Engine's `/system/` assets served from firmware ROM — no FAT space wasted on fonts and splash graphics

**ThumbyOne differences vs. the stock Thumby Color launcher:**

- **Custom C picker** replaces the launcher: loads instantly (no Python startup wait), shows a proper hero view.
- **ROM-backed `/system/`** — the engine's `filesystem/system/` tree (fonts, splashes, launcher assets, ~376 KB) is packed into the firmware image and mounted as a read-only MicroPython VFS. Saves FAT space and means `/system/` is always available without a first-boot copy.
- **No USB REPL** — the MPY slot doesn't enumerate as a serial port because the lobby owns USB. Games just run; no Thonny connection possible while in a game. Lobby-based transfers only.
- **Flash scratch partition** — `TextureResource(in_ram=False)` stores into the upper 768 KB of the MPY partition rather than the chip-wide default, so loading textures doesn't clobber sibling slots.
- **Per-game saves** — each game gets its own `/Saves/games/<name>/` namespace.

**Controls:**

| Button | Action |
|---|---|
| D-pad | Step through games |
| A | Launch the selected game |
| B | Toggle favourite (★) for the highlighted game |
| MENU (in picker) | Open info overlay (battery, disk, sort, back to lobby) |
| MENU (tap, in legacy Thumby game) | Cycle scale preset (1.0× → 1.5× → 1.75× → 2.0× → 2.5× → 1.0×) |
| LB + RB (held ~0.5 s, in legacy Thumby game) | Capture a screenshot — saves the current 128×128 frame, downsampled to 64×64, as `icon.bmp` in the game's folder. The picker shows it as the game's thumbnail next time round. |
| MENU (held ~5 s in-game) | Reboot to the lobby (no splash; game state is lost) |

**Game structure** in `/games/<name>/`:

```
/games/DeepThumb/
    main.py                  # entry point
    icon.bmp                 # 16 bpp RGB565, up to 64x64
    arcade_description.txt   # line 1 = title; rest = description blurb; optional "Author: ..." line
    assets/
        sprites.bmp
        music.wav
```

The icon + description are optional (picker falls back to the directory name and a placeholder tile), but having them makes your game look at home next to everything else on the picker.

**Original (monochrome) Thumby games — supported as of 1.10.** Drop a classic 72×40 mono Thumby game folder into `/games/<name>/` and it appears on the same picker, scaled to the colour screen with the original gameplay intact. Buttons, audio, save format, and Timendus's `thumbyGrayscale` library are all shimmed transparently — no game code modification needed.

- **Greyscale games work too**, both flavours: games that `import thumbyGrayscale` (LyteBykes, Foxgine, RocketCup), and older games that bundled an inline copy of the library inside their own `display.py` (Umby & Glow's display.py is the canonical example). Both paths route through the same Color-aware renderer; the inline-copy case is detected by sniffing `display.py` for the Timendus library's `class Grayscale` + `_thread` signatures.
- **Tap MENU during a legacy game to cycle through scale presets** — `1.0× / 1.5× / 1.75× / 2.0× / 2.5×`. The default is set per-game from the picker's *Legacy scale* row before launch; the in-game tap is a quick override that doesn't persist (so the next launch uses the picker default again). The 5-second MENU hold still returns to the lobby.

See the [1.10 changelog](#110) for the supported feature set and known caveats; the technical write-up of how the shim works (and what we had to revert in MicroPython to make Umby & Glow's viper-tag trick work) is in [MicroPython + engine slot](#micropython--engine-slot).

---

## Changelog

### 1.12

Megadrive / Genesis FM audio sounds cleaner, and every emulator now
holds its target frame rate steadily instead of drifting below it
under load.

- **Cleaner Megadrive FM.** ThumbyNES now uses the Genesis Plus GX
  YM2612 implementation in place of PicoDrive's stock one — the
  high-frequency hash that sat on top of every Genesis chord is
  reduced.
- **All emulators hold the target frame rate.** Fixed a long-standing
  pacing bug where a frame that ran a little over budget would
  permanently lose those few milliseconds of schedule, dragging the
  wall-clock loop rate below the target refresh and starving the
  audio buffer in lockstep — audible as music playing slightly slow
  and occasional audio thinning. Affected NES / SMS / PCE / GB / MD;
  all now lock to their refresh target with no drift.
- **New DOOM control scheme: BA STRAFE.** A third option in the
  overlay menu's Controls row, alongside CLASSIC and SOUTHPAW. Moves
  Fire and Use onto the shoulder buttons (RB / LB) and turns A and
  B into strafe-right / strafe-left — closer to a modern-FPS layout
  for players who prefer face buttons for strafing. Selection
  persists across reboots.

### 1.11

Legacy Thumby polysynth games (PSdemo, TinyFreddy) play correctly
on Color, the lobby gains a real-time clock that drives the home
screen and Pokemon Crystal / Gold / Silver, and Game Boy / Game
Boy Color audio is significantly cleaner.

- **Polysynth games run.** The original Thumby's `polysynth.py`
  library used to brick Color when bundled because the bare-GPIO
  PIO synth conflicted with the LCD backlight, RGB LED, and three
  of the face buttons. ThumbyOne now replaces it with a software
  shim before the game imports, so PSdemo and TinyFreddy play with
  full 7-voice chiptune synthesis (square waves, noise drums,
  phase-locked chords, arpeggios — same as the original Thumby).
- **Set the time in the lobby.** Lobby menu → **SET TIME** opens a
  date/time picker (year/month/day/hour/minute). The home screen
  shows a live `HH:MM` clock; it reads `--:--` if the chip's
  battery has lost power, prompting a re-set.
- **Pokemon Crystal / Gold / Silver work properly on the GB
  emulator.** The cart's MBC3 real-time clock is driven by the
  lobby clock and persisted across power-off via a `.rtc` sidecar
  alongside each cart's `.sav`, so day-night cycle, time-of-day
  events, and berry growth all track real elapsed wall time. Other
  GBC RTC carts (Harvest Moon GB 2 etc.) get the fix for free.
- **GB / GBC audio significantly cleaner.** Chord-heavy tracks no
  longer have crunchy aliasing on top of the music; envelope
  changes don't pop; music doesn't speed up or crackle under heavy
  on-screen load.
- **Screenshot chord for legacy Thumby games** — hold **LB + RB**
  together for ~0.5 s while a legacy game is running to capture the
  current frame as the game's picker thumbnail.
- **MD / PCE FPS overlay simplified** to fps + skipped-frame count
  only (the per-frame microsecond timings and audio-mode tag are
  removed).

Technical detail — the polysynth shim mechanics, the RTC integration
with Peanut-GB, the GB audio anti-alias filter + frame-pacing clamp,
and the upstream MicroPython parser fix that PSdemo needed — is in
the [MicroPython + engine slot](#micropython--engine-slot),
[ThumbyNES](#thumbynes--nes--master-system--game-gear--game-boy),
and [Real-time clock](#real-time-clock-111) sections.

### 1.10

Original (monochrome) Thumby games run inside the MicroPython slot,
the lobby volume slider applies to legacy game audio for the first
time, and a new bare-metal render path means classic games run at
full speed instead of the laggy rate they hit going through the
standard engine pipeline.

- **Original Thumby games run.** Drop a classic 72×40 monochrome
  Thumby game folder into `/games/<name>/` and it appears on the
  same hero picker as Color games. Buttons, audio, saves, and
  multiplayer-link constructors all work transparently — no game
  source modifications.
- **Big performance jump for legacy games.** A new bare-metal render
  path blits the legacy 72×40 framebuffer straight onto Color's
  128×128 panel via viper kernels, bypassing the engine's general-
  purpose sprite / draw pipeline. Combined with a fast button-poll
  hook that keeps legacy games' tight read-button loops responsive
  without paying for a full engine tick on every read, classic
  games now run at full speed where they previously felt sluggish.
- **Greyscale games supported** — both common variants. Games that
  `import thumbyGrayscale` (Timendus's library) and games that
  bundle an inline copy of the same library inside their own
  `display.py` both end up at the same Color-aware renderer. Umby
  & Glow uses the inline-display variant; LyteBykes, Foxgine, and
  RocketCup use the import variant.
- **Press MENU during a legacy game to cycle 5 scale presets** —
  `1.0× / 1.5× / 1.75× / 2.0× / 2.5×`. The default scale per game
  is set from the picker's *Legacy scale* row before launch. The
  5-second MENU hold still returns to the lobby.
- **Lobby volume slider applies to legacy game audio** — both
  tone-style games (Umby & Glow, Gravity, TinyGolf, …) and the
  PCM-streaming kind (BadApple) now respect the slider end-to-end.
  Native Color games are unchanged.
- **Per-game *Legacy scale* and *Legacy FPS* rows** in the MPY
  picker menu — set the default scale preset for each legacy game
  and toggle an on-screen FPS counter, persisted on the FAT.

Note: Umby & Glow plays correctly **only on ThumbyOne**. The game
relies on a MicroPython viper detail that was changed upstream
between MP 1.19 (the original Thumby firmware) and MP 1.20+ (which
stock Thumby Color uses); we revert that one upstream commit in our
mp-thumby fork specifically so this game works. On stock TinyCircuits
firmware it's been broken since the Color launch.

Technical detail — the launcher's pin / PWM / UART shims, the audio
path's tone-vs-PCM mode detection, the grayscale dual-path import
sniff, the `zlib` compat shim for MP 1.21+, and the deliberate
viper revert that makes Umby & Glow's small-int tag idiom work — is
in [MicroPython + engine slot](#micropython--engine-slot).

### 1.09

Small follow-up to 1.08 that lines the FILL/CROP behaviour up across
every emulator core and tidies a couple of PCE labelling misses.

- **NES / SMS / GG / GB CROP harmonised to LB + d-pad.** Previously
  NES and SMS paused the cart and panned with bare d-pad; GG and GB
  panned via MENU + d-pad. All four now keep the cart running with
  the **LB + d-pad** chord — same shape as MD and PCE. Tap LB still
  maps to SELECT on NES / GB (strip-and-pulse pattern); SMS / GG
  don't bind LB cart-side, so it's just the pan modifier. The
  "pause and read NES dialogue" feature is gone — the in-game
  menu's *Resume* anchor still pauses if you need to step away.
- **SMS FILL is now horizontally pannable** with LB + L/R (range
  0..64 source pixels). The 192-col centre slice was hard-coded
  in 1.08; you can now pick which third of the SMS image gets
  cropped off.
- **PCE FILL pan defaults to centred per-cart**, computed from the
  running game's actual viewport instead of a hard-coded 16. Lands
  centred on 256×224 (Bonk, Soldier Blade), 256×240 (R-Type), and
  336× modes. The hard-coded value pinned wider/taller modes
  against the right edge.
- **Lobby NES tile label now lists PCE** — bottom-of-screen tagline
  reads `NES / SMS / GG / GB / MD / PCE` (or without `/ MD` in the
  no-MD build).
- **Picker hero view shows "PC ENGINE"** as the tab label — was a
  silent miss in 1.08 (the tab-bar icon was already there, the
  big system label below the thumbnail wasn't).

### 1.08

- **PC Engine / TurboGrafx-16 (HuCard) support** — sixth emulator
  in the ThumbyNES slot. Drop `.pce` ROMs into `/roms/`; the picker
  gets a new **PCE** tab. Plays HuCards at native 60 fps on the
  250 MHz overclock with PSG audio, save state, region detect,
  master volume. Available in **both** `WITH_MD=ON` and `WITH_MD=OFF`
  builds — the HuCard-only core fits inside the existing slot
  partition, so **upgrading from 1.07 doesn't trigger a FAT
  migration**: the volume you have today mounts untouched.
  See the [ThumbyNES v1.08 changelog](https://github.com/austinio7116/ThumbyNES#v108--pc-engine--turbograf16-tab-strip-facelift-lcd-reliability)
  for the technical write-up — HuExpress trim (~70 KB flash, no CD
  / Arcade Card / netplay), per-scanline renderer that drops 568 KB
  of upstream scratch buffers to fit Thumby Color's 520 KB SRAM,
  static-IRAM `exe_go` for performance, PSG audio with mono-mix
  workaround for an upstream stereo memset bug, JP-default region
  detect (Hudson cart pre-decoding makes US carts boot from the JP
  code path), 0xFC + VDC-reg-select strict-abort fixes for
  HuCard compatibility, six-core heap-leak audit (HuExpress + smsplus).

- **Tab strip facelift in the picker.** The procedurally-drawn
  console icons in the ThumbyNES picker tab strip are replaced by
  hand-painted 12×8 bitmaps.

- **LCD cold-boot reliability.** The lobby's GC9107 init now issues
  an explicit `SWRESET` + `INVOFF` before configuring orientation
  and pixel format. Fixes a rare cold-boot artefact where the panel
  came up rotated 180° or with colours inverted until the next
  power cycle. Same fix applied in the ThumbyNES slot's display
  init.

- **Stock-firmware recovery path.** A new `revert_uf2.py` tool
  (`tools/`) repacks the local stock TinyCircuits firmware image
  into a one-shot recovery UF2 for users who want to drop back to
  factory firmware. Walkthrough lives in
  [Tips and troubleshooting → Returning to stock](#returning-to-stock).

- **Screenshot persistence on power-off.** MENU+A snapshot in any
  emulator (NES / SMS / GG / GB / MD / PCE) now flushes the FAT
  write-cache to flash immediately. Previously a power-off (or USB
  pull) before the next battery_save / cfg_save would lose the
  shot. Quitting through the in-game menu always saved correctly
  because the menu's flush picked up the pending writes; only
  hard-power-off lost them.

### 1.07

- **MD pad B stuck on second cart load — fixed.** Launching a
  second MD cart inside the same slot session left pad B reading
  as held from the first frame — Cannon Fodder firing constantly,
  Sonic ignoring the jump downpress so only jumping on release, any cart that polls B
  affected. First launch was fine; only subsequent launches broke.
  See the [ThumbyNES v1.07 changelog](https://github.com/austinio7116/ThumbyNES#v107--defrag-overhaul-md-b-stuck-fix-300-mhz-removed)
  for the technical write-up.

- **300 MHz overclock removed.** The 300 MHz option in the picker's
  Overclock row and the per-cart Overclock in each in-game menu has
  been dropped — it could cause hard-to-recover crashes, because a
  crash on next launch with 300 MHz saved meant needing USB-MSC to
  delete the config file before the device would boot a cart again.
  Saved configs still set to 300 MHz now fall back to 250 MHz
  silently on load; no user action needed. Max overclock is 250 MHz.

- **Defragmenter overhaul.** The NES-slot defragmenter is more
  reliable on near-full volumes:
    - New `<PACK>` mode past K=500 on the preview slider —
      guaranteed layout, always reaches `frag: 0` if the volume
      physically has room, at the cost of more writes.
    - Automatic reclaim of leaked FAT entries left behind by dirty
      USB disconnects — built-in `chkdsk`-style pass that recovers
      the lost space, shown as `rclm:N` in green on the preview.
    - Honest "after" numbers — the free-space-after figure no
      longer claims contiguous space that wouldn't actually exist
      post-defrag.
    - Bigger, red-free cluster-map palette so the pinned-cluster
      indicator (red) never blends into a file colour.

### 1.06

- **Mega Drive / Genesis compatibility jump** — every cart in our
  41-ROM test set now boots and renders, up from 27/41 in 1.05.
  Roughly one in three previously-broken carts now play. Headline
  beneficiaries: Xenon 2, Castle of Illusion, FIFA / FIFA 98,
  Cannon Fodder, Gunstar Heroes, Brian Lara Cricket 96, Theme Park
  (was a hard crash), Rock n' Roll Racing, Sonic & Knuckles,
  Street Fighter II SCE, Thunder Force IV. See the
  [ThumbyNES v1.06 changelog](https://github.com/austinio7116/ThumbyNES#v106--mega-drive-compat--pan-chord)
  for the technical write-up.

- **MD save / load state — fixed.** Save state in the in-game menu
  used to silently hang on most carts; now works reliably across
  the library. Note that MD save states are larger than the other
  systems — see [Storage budgets](#storage-budgets--leave-headroom)
  below.

- **Play-while-cropped pan chord (CROP + FILL).** Hold **LB** + a
  d-pad direction to pan the source viewport without losing game
  control. CROP gets full XY pan; FILL gains left/right pan within
  the centre crop. LB → MD START now fires on release (a tap still
  acts as START) so the chord never accidentally pauses the game.
  Replaces the always-on CROP pan from 1.05, which permanently
  stole the d-pad from cart input.

### 1.05

> ### ⚠️ Upgrading from 1.04 or earlier — back up first
>
> The 1.05 default build shifts every partition up by 1 MB to make room for the enlarged Mega Drive / Genesis ROM / tables area in the NES slot. The shared FAT therefore moves from `0x660000` (9.6 MB) to `0x760000` (**8.6 MB**) — a different on-disk location AND 1 MB smaller. When you flash 1.05 the lobby sees no valid FAT header at the new offset and **auto-formats** a fresh volume on first boot. **Everything on your ThumbyOne USB drive is wiped.**
>
> Back up first, every time:
> 1. Boot into the 1.04 lobby (or any slot, then back to lobby).
> 2. Plug in USB — the device enumerates as `ThumbyOne Storage`.
> 3. Copy everything off — `/roms/`, `/carts/`, `/games/`, `/Saves/`, `/.favs`, `/.active_game`, and the hidden `/.volume` / `/.brightness` settings if you care about those.
> 4. Eject, unplug, flash 1.05, plug back in.
> 5. Copy as much back as fits in **8.6 MB**. If you were near-full on 1.04 you'll need to trim something — typically the largest ROMs or a couple of MicroPython games with big assets. The per-slot picker's `Disk` row in the settings menu tells you exactly how full the new FAT is after each write.
>
> If you don't need Mega Drive / Genesis support at all, you can skip the migration hassle by building / flashing with `-DTHUMBYONE_WITH_MD=OFF` instead (see the [Build matrix](#build-matrix)). That keeps the original layout (9.6 MB FAT at `0x660000`) and the FAT you already had on 1.04 will mount untouched — no format, no data loss. You just don't get MD.

- **Sega Mega Drive / Genesis support** via vendored
  [PicoDrive](https://github.com/notaz/picodrive) (LGPLv2), integrated
  into the ThumbyNES slot. Drop `.md` / `.gen` / `.bin` into `/roms/`;
  the picker gets a new **MD** tab. Boots and plays most 1990-era
  3-button carts (Sonic 2, Streets of Rage 2, many more) locked at
  50 FPS PAL with full audio at the 250 MHz overclock. See the
  ThumbyNES repo's [Changelog v1.05](https://github.com/austinio7116/ThumbyNES#v105--mega-drive--genesis)
  for the emulator-level details (IRAM tricks, adaptive VDP skip,
  runtime audio modes, etc.) and [`vendor/VENDORING.md`](https://github.com/austinio7116/ThumbyNES/blob/main/vendor/VENDORING.md)
  for the 18 individual PicoDrive patches that got it running on
  Cortex-M33 + XIP flash.

- **New `THUMBYONE_WITH_MD` build option** (default `ON`). PicoDrive's
  ~850 KB of precomputed flash tables (FAME 68K jumptable 256 KB,
  YM2612 log-sine + LFO 208 + 128 KB, cz80 SZHVC 256 KB) don't fit
  in the original 1 MB NES partition, so the default build grows
  that partition to 2 MB and shifts every downstream partition +
  the shared FAT up by 1 MB (FAT goes from 9.6 MB to 8.6 MB of
  ROM storage). Users who want the old layout + no MD can rebuild
  with `-DTHUMBYONE_WITH_MD=OFF`; ThumbyNES drops to 643 KB, the
  NES partition stays 1 MB, and the FAT keeps its 9.6 MB.

- **DOOM overlay menu trigger changed** from the LB + RB chord to a
  MENU long-press, matching the in-game menu gesture used everywhere
  else. MENU-tap still opens the vanilla DOOM Main Menu (Save /
  Load / Options / Quit). The cross-slot handoff from DOOM's
  overlay menu to the lobby is now wired up — no need to route
  through Main Menu → Quit Game any more.

- **DOOM front-LED health indicator.** The device's front RGB LED
  now smoothly tracks player HP during gameplay — pure green at
  100, yellow around 50, pure red at 0. Handy for peripheral-vision
  awareness while your eyes are locked on the tiny screen. Hidden
  on the title screen / demo playback (LED stays at boot idle
  green). The blend is gamma-balanced against the Thumby's LED dies
  — raw `(R=255, G=255)` reads almost-red because the green die is
  dimmer than red at equal duty, so the "yellow" midpoint caps red
  at ~96 to get a visually-balanced yellow. Zero perceptible cost —
  new PWM writes only fire when HP actually changes.

- **Consistent front-LED behaviour across every slot.** The LED now
  reads `/.brightness` on every slot entry (DOOM too — previously it
  flashed white at full brightness on load), stays dark during the
  chain_image handoff between slots (no white flash between tiles),
  and tracks the brightness slider live as you drag it in every
  picker and in-game menu. New shared `thumbyone_slot_init_brightness_and_led()`
  helper + live `on_change` callback plumbed through the menu slider
  rows — every slot now drives the LED through the same code path
  with the same PWM policy.

- **Battery module raw-ADC diagnostics** exposed via a new accessor
  so the picker / in-game overlays can show counts for debugging,
  and the half-voltage logic got refreshed for more accurate
  reporting near the low-battery cutoff.

- **NES defragmenter planner refresh** — the cluster-level defragmenter
  introduced in v1.03 got a planner rewrite for faster compaction and
  a cleaner preview on near-full volumes. See the NES slot's picker
  menu → **Defragment now**.

### 1.04

- **Game Boy Color and Game Gear look better.** The asymmetric
  5:4 × 9:8 scaling the 160×144 screens need to fill the Thumby's
  128×128 display used to drop every 5th source column and every
  9th row with a pure nearest-neighbour blit — thin text strokes
  disappeared in menus, HUDs and dialogue boxes. FIT now runs a
  coverage-weighted blend that preserves every source pixel
  proportional to its footprint, making text clean and legible.
  A new **BLEND toggle** in each cart's in-game menu (default on
  for GB / GBC / GG) lets you flip back to pure nearest on carts
  where you specifically want the old sharp look.
- **Palette-aware DMG blend.** On original Game Boy carts the
  blend runs in palette-index space and interpolates between
  neighbouring palette entries, so the classic Nintendo green (and
  every other DMG palette) stays on its own gradient instead of
  picking up the teal hue shift a naive RGB blend would introduce
  between the lighter and darker greens.
- **Packed-RGB565 lerp** for the fast blend path. One 32-bit
  multiply per pixel lerp handles all three channels in parallel;
  the full 128×128 blend costs well under a millisecond per frame
  at 250 MHz.
- **New 300 MHz overclock option** in the NES-slot menus — both
  the global Overclock row in the picker menu and the per-cart
  Overclock in each in-game menu. Default stays at 250 MHz; 300
  MHz is there for dense carts that want extra headroom — but may
  not work on all hardware. Use at your own risk; if it fails you
  may have to delete the config files to revert it.
  _(Removed in 1.07 — see changelog.)_

### 1.03

- **MicroPython games exit cleanly back to the picker.** If a game calls `sys.exit()`, `engine.end()`, raises an uncaught exception, or simply returns from `main.py`, the slot now reboots straight back into the MicroPython game picker instead of hanging on a dead REPL. Previously the device appeared frozen until you power-cycled it.
- **Game Boy Color support** on the ThumbyNES slot. Drop `.gbc` carts alongside your `.gb` files — the emulator now runs them with their full CGB palette (converted to RGB565 per line). Powered by a swap to fhoedemakers' CGB-capable peanut_gb fork; DMG carts still work unchanged with the six built-in shade palettes.
- **Chained-XIP for fragmented ROMs.** Fragmented cartridges used to refuse to load (`load err -35` on the red splash). They now fall back to a per-cluster XIP pointer table and still run at full 60 fps — no RAM copy, no defrag required. Drop a ROM, play it, defragment whenever (or never).
- **Brand-new cluster-level defragmenter** for the shared FAT. The old file-level rewrite couldn't handle near-full volumes; the new one cycle-sorts clusters directly and works down to tiny free margins. Features a preview-and-confirm screen (A = apply, B = cancel) with before/after cluster maps so you can see what it'll do before it touches anything, live per-file cluster-map animation during the move, and a red "DO NOT POWER OFF" banner with matching front-LED indicator while the FAT is mid-write. Triggered from the ThumbyNES picker menu's "Defragment now"; running it is optional because chained-XIP handles fragmentation transparently, but compacting the free space lets you drop large new ROMs that otherwise wouldn't fit in one contiguous run.

### 1.02

System-wide controls, consistent across every menu:

- **Global volume and brightness.** The lobby MENU overlay gets two new sliders — `VOLUME` and `BRIGHTNESS`. Set them once, they apply to every slot on launch: NES, SMS, GG, Game Boy, PICO-8, DOOM and every MicroPython game pick up the same values. Change volume inside any slot's menu and the lobby shows the new value next time you back out.
- **Brightness sliders in every slot menu.** ThumbyNES picker + in-game pause, ThumbyP8 picker + in-game pause, and the MicroPython picker all have a live brightness slider now — the backlight PWM tracks while you slide.
- **DOOM honours the system volume and brightness** too. On launch DOOM picks up whatever the lobby was set to; its own pause menu still works as a session-level override.
- **Consistent widgets**: thick right-aligned outlined slider across the lobby + every slot menu, press-and-hold autorepeat with identical timing everywhere (300 ms warm-up, 60 ms cadence), and every slider takes ~20 clicks end-to-end regardless of its internal range. Full-row highlighted cursor band in the lobby (used to be a hairline).
- **"Back to lobby" and "Quit to picker" are always the LAST menu item.** Press UP once from the top of any menu to wrap straight to back-out — the muscle-memory shortcut.

Cleanups:

- **No more "checking files" / "DEFRAGMENTING" flashes** on every NES launch. The auto-defrag was unreliable and fired unnecessarily; it's gone.
- **Lobby MENU tidied**. The old "Reboot lobby" action is gone (it was occasionally landing in a random slot) — close the menu via B / MENU / A-on-Close.
- **NES picker menu tidied**. Dead "Defragment now" action removed.
- **Minimum brightness lowered** (FLOOR 25 → 5, about 2 % duty). Room to dim the screen further for dark-room play without letting a slider set 0 = invisible.
- **Consistent disk display**. Every menu with a disk row reads "`X.XM / Y.YM`" used / total with the bar filling with used; previously the direction and formatting varied slot-to-slot.
- **Consistent battery readout**. One shared sampler (16-sample trimmed mean + EMA + ±2 % percent hysteresis) — the number stays put, stops flickering, and reads the same on every screen that shows it.

Under-the-hood fixes that you might notice:

- **DOOM no longer slows down** after saving a game or opening the LB+RB overlay menu — a flash-write bug that left the chip in slow-XIP mode has been fixed.
- **NES + P8 brightness actually applies.** Those slots share a PWM slice with their audio output, so the old hardware-PWM backlight was overwritten on every audio sample. The shared backlight driver now uses PIO PWM on a dedicated state machine; audio can't touch it.
- **MicroPython: no bright-frame flash** between menu redraws in the picker.

Storage format: system settings (volume + brightness) live in a single 4 KB flash sector — readable by every slot including DOOM, written only when you move a slider and close the menu.

See the [MENU overlay](#the-lobby) and per-slot menus for the slider rows; the [technical notes](#per-slot-architecture) explain the flash layout for curious readers.

---

## Tips and troubleshooting

### Returning to stock

> ⚠️ **The official TinyCircuits stock firmware UF2 will NOT recover a ThumbyOne-installed device** if you drag it directly onto the BOOTSEL drive.
>
> Stock firmware ships as UF2 family `rp2350-arm-s`, which the RP2350 bootrom treats as a *movable* application image — it gets routed into one of ThumbyOne's slot partitions (which have `arm_boot 1` set), not over the lobby. The drive copy may even appear to succeed, but the device keeps booting ThumbyOne and the lobby never gets replaced. This is RP2350 bootrom behaviour, not a ThumbyOne bug — no partition-table tweak or firmware update fixes it. **Use one of the procedures below instead.**

#### Option A — drag-and-drop the recovery UF2 (recommended)

The repo ships a prebuilt `firmware_thumbyone_revert.uf2` — exactly the stock TinyCircuits MicroPython firmware (build `b8a2e6a`), with every UF2 block re-flagged as family `absolute` so the bootrom writes it at its declared addresses (0x10000000+) and overwrites the lobby region.

1. Power off the Thumby Color.
2. Hold **DOWN** on the d-pad and plug in USB.
3. The device appears as `RPI-RP2350` on your computer.
4. Drag `firmware_thumbyone_revert.uf2` onto it.
5. The device reboots into stock firmware. Your ThumbyOne data is gone; the device is back to factory state with a fresh empty FAT.

**After the revert, the device is fully stock — no partition table left in flash, no ThumbyOne traces.** Future stock firmware updates from TinyCircuits drag-and-drop normally (the standard update path on a stock Thumby Color). The recovery UF2 is only needed for the one-shot ThumbyOne → stock transition; from then on the device behaves exactly like a unit that's never had ThumbyOne installed.

If you want to revert to a *different* custom firmware build than the bundled one - either flash the revert firmware first - then flash the custom one or you can repack it as absolute family yourself with the converter script in this repo:
```
python3 ThumbyOne/tools/uf2_repack_absolute.py firmware_<hash>.uf2 firmware_<hash>_recovery.uf2
```
Then drag the produced `_recovery.uf2` onto BOOTSEL the same way. picotool will report the rewritten file as `family ID 'absolute'`.

#### Option B — picotool

If you'd rather use a host-side tool than the recovery UF2, [picotool](https://github.com/raspberrypi/pico-sdk-tools/releases) (Raspberry Pi's official Pico command-line tool) can erase ThumbyOne's partition table and let the device accept any stock firmware UF2 directly.

**Install picotool** (one-time, prebuilt binaries from Raspberry Pi's release page):

*Linux x86_64:*
```bash
curl -LO https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.2.0-3/picotool-2.2.0-a4-x86_64-lin.tar.gz
tar xzf picotool-2.2.0-a4-x86_64-lin.tar.gz
sudo cp picotool/picotool /usr/local/bin/
sudo cp picotool/udev/99-picotool.rules /etc/udev/rules.d/ 2>/dev/null || true
sudo udevadm control --reload-rules && sudo udevadm trigger
picotool version
```

*Linux aarch64* (Raspberry Pi OS, ARM laptops): same commands, swap `x86_64-lin` for `aarch64-lin` in the URL.

*macOS:*
```bash
brew install picotool
# or download picotool-2.2.0-a4-mac.zip from the release page above and unzip
```

*Windows:* download `picotool-2.2.0-a4-x64-win.zip` from the [release page](https://github.com/raspberrypi/pico-sdk-tools/releases/tag/v2.2.0-3), unzip it somewhere (e.g. `C:\picotool\`), and either add that folder to your `PATH` or invoke the binary directly. Windows also needs the libusb driver bound to the device — easiest path is [Zadig](https://zadig.akeo.ie/): with the Thumby Color in BOOTSEL, run Zadig, pick `RP2 Boot` from the device dropdown, choose `WinUSB` as the driver, and click `Replace Driver`. Picotool will then see it.

**Run the recovery:**

```
# 1. Power off the Thumby Color, hold DOWN, plug in USB to enter BOOTSEL.
# 2. Erase the chip (wipes ThumbyOne and its embedded partition table):
picotool erase --all
# 3. Load the stock firmware UF2 with an explicit target offset, and execute:
picotool load <path-to-stock>.uf2 -o 0x10000000 -x
```

The `-o 0x10000000` flag tells picotool exactly where to put the bytes — without it, picotool refuses an `rp2350-arm-s` UF2 with `Family ID 'rp2350-arm-s' cannot be downloaded anywhere` (its own conservative check on movable UF2s when the chip has no partition table to map them onto). With the explicit offset picotool stops trying to interpret the family and just writes the blocks. The `-x` flag executes (boots) the firmware afterwards. Result: device boots stock firmware, no PT in flash, fully back to factory state.

### Other tips

**The picker takes a few seconds to appear after I pick a system.**
That's the bootrom chaining into the slot partition — NES in particular has to re-initialise USB clocks and LCD DMA. P8 is snappy; DOOM is basically instant.

**I dropped a game into `/games/` but the picker doesn't see it.**
The picker scans at boot. Return to the lobby (MENU → Back to lobby), plug in, check the folder has a `main.py`, eject, re-enter MPY.

**The MPY game just shows a blank screen.**
The launcher writes any game crash to `/.last_error.txt` on the shared FAT. Return to the lobby, plug in USB, open that file from the drive for a Python traceback.

**My PC is still showing an old "ThumbyNES Carts" drive from a pre-ThumbyOne flash.**
ThumbyOne enumerates as a different USB product ID to avoid driver-letter collision, but your host's `Devices and Printers` may remember old entries. Harmless; delete from there if it's cluttering things up.

**Everything's broken after a bad transfer.**
Hold **LB + RB** at boot, hold them through the countdown — fresh FAT, pristine device.

### Storage budgets — leave headroom

The shared FAT is **8.6 MB in the default MD-enabled build** (9.6 MB with `THUMBYONE_WITH_MD=OFF`). ROMs are the bulk of it; save states and battery saves are small individually but add up across a library, and MD's are by far the biggest:

| File | Typical size |
|---|---|
| **MD save state** (`.sta`) | **130–200 KB per slot** (full 68K + Z80 + VRAM + CRAM + YM2612 + WRAM snapshot) |
| MD battery save (`.srm`) | 8–32 KB depending on cart |
| NES / SMS save state | 15–25 KB |
| GB / GBC save state | 17–25 KB |
| GB / GBC battery save | 8–32 KB |

**Aim to keep at least ~500 KB free** at any time — enough headroom for an MD state save plus the FAT defragmenter's working room. Save state writes fail silently if the disk is full (the OSD reports "save fail" with no further detail). The lobby MENU overlay's `Disk` row shows live used / total — check it after big USB transfers.

If you fill up, the easiest fixes are: drop a couple of the largest ROMs over USB, or delete old save states (they live alongside their ROM in `/roms/`, with the same basename but `.sta` extension).

### "load err -35" on the red splash

Most fragmented carts fall back to the chained-XIP path ([added in 1.03](#whats-new-in-103)) and load cleanly, but extreme fragmentation combined with low free space can still produce a `load err -35` on the cart-launch splash — the cluster chain runs longer than the loader's pointer table can hold. Run **Defragment now** from the ThumbyNES picker menu (see [FAT defragmenter](#fat-defragmenter) below); a clean volume guarantees the cart loads via the contiguous mmap path.

### FAT defragmenter

ThumbyNES runs cartridges straight out of flash via **XIP** rather than copying them to RAM — that's how a 1 MB Game Boy Color cart fits on a device with a ~330 KB heap. There are two XIP paths:

1. **Contiguous mmap** — when the file's FAT chain is a single cluster run, the whole ROM maps to one flat address range in flash. Every ROM byte access is a single flash read. Optimal path.
2. **Chained XIP** *(new in 1.03)* — when the file is fragmented, the loader builds a per-cluster pointer table and every ROM read does a shift + mask + table lookup to find the right cluster. Still zero-copy, but the indirection adds CPU cost on every byte the core touches.

**Most games are fine either way.** Lighter carts (early NES, Game Boy, most SMS) stay locked to their native refresh rate on chained XIP. Heavier ones — dense NES mapper carts, some Game Boy Color games with large tilemaps, a few SMS titles with aggressive sample playback — can drop frames on chained XIP when they wouldn't on contiguous mmap. If a fragmented cart feels sluggish, defragging is the fix.

**Why run the defragmenter:**
- **Moves currently-fragmented carts onto the fast path.** Every cart on the volume ends up as a contiguous chain, so every cart runs via direct mmap with no per-byte indirection. If a cart was dropping frames because it was fragmented, a defrag puts it right.
- **New uploads stay on the fast path too.** Defragging consolidates all free space into one run at the end of the volume. The next ROM you drop over USB lands in that contiguous run, which means it starts life as a contiguous file — no fragmentation, no chained XIP, no redefrag needed.

**Why you can also not bother:** chained XIP means a fragmented volume isn't broken — every cart still loads and plays. Lighter games won't notice the difference. Defrag when a specific cart feels sluggish, or periodically just to keep future uploads on the fast path.

Trigger it from the ThumbyNES picker menu → **Defragment now**. You get a preview first and nothing is written until you confirm.

**Preview screen** — before/after cluster maps, frag count, largest free contiguous block, total files and bytes. **A** applies, **B** cancels.

<p align="center">
  <img src="docs/screenshots/nes-defrag-preview.png" width="240" alt="Defrag preview — before/after cluster map, A=apply B=cancel">
</p>

**During execution** — clusters move live; the map redraws per file (one hue per file, cycled through a 15-colour palette). A red "DO NOT POWER OFF" banner at the top is mirrored by the front LED going solid red while the FAT is mid-write.

<p align="center">
  <img src="docs/screenshots/nes-defrag-moving.png" width="240" alt="Defrag running — live cluster map with DO NOT POWER OFF banner and progress counter">
</p>

The pass does an **in-place cluster-level cycle sort** (same family as Norton SpeedDisk and ext4 `e4defrag`): it plans the target layout first, then cycles each cluster into place using only two cluster-sized RAM buffers — so it works on near-full volumes where a file-level rewrite would need 2× the largest file free. Phases: cluster move, FAT rebuild, directory-entry patch, FatFs remount. A post-pass re-check counts fragmented files against the original to confirm the pass worked.

Running it on an already-clean volume is a no-op (preview shows `0 mv`, zero writes).

**A note on flash wear:** a full defrag rewrites every moved cluster to flash, and the RP2350's internal QSPI flash is a consumer-grade chip with a finite erase/program endurance (spec is ~100k cycles per sector). One defrag a week after a big upload session won't meaningfully dent that budget, but don't run it for fun — it's an occasional tidy-up, not something to trigger every time you boot. Preview-and-confirm is there partly to make accidental runs hard; the "nothing to do" no-op also means repeat runs on a clean volume cost zero writes.

---

---

# Technical specifications

*Below the line: internals, architecture, build system. Skip unless you're curious about how the pieces fit.*

## Architecture at a glance

```
                               ┌─────────────────────────────────────┐
                               │            16 MB flash              │
                               │                                     │
                              ┌┤  0x000000  ──── Lobby (128 KB)      │
                              ││             (selector, USB MSC)     │
               bootrom        ││  0x020000  ──── NES slot  (1 MB)    │
              rom_chain      ─┤│  0x120000  ──── P8  slot  (512 KB)  │
              image →         ││  0x1A0000  ──── DOOM slot (2.5 MB)  │
              chosen slot     ││  0x420000  ──── MPY slot  (2 MB)    │
                              ││  0x620000  ──── P8 active-cart (252 KB) │
                              ││  0x65F000  ──── Settings sector (4 KB)  │
                               │  0x660000  ──── Shared FAT (9.6 MB) │
                              ─┤                                     │
                               │  (NES/P8/DOOM/MPY all mount the     │
                               │   shared FAT via common FatFs.)     │
                               └─────────────────────────────────────┘
                                           ▲
                                           │  USB MSC (drive letter)
                                           │
                                      ┌─────────┐
                                      │   PC    │  only when in lobby
                                      └─────────┘
```

Each slot is a **completely independent** firmware image, laid out at flash offset `0x10000000` as it would be if it owned the chip. The bootrom's `rom_chain_image()` remaps physical flash at boot via QMI ATRANS so the chosen slot sees itself at the base of XIP and runs unmodified. When a slot wants to return to the lobby it writes a handoff magic into watchdog scratch and triggers a reset; the lobby's `main()` consumes the magic and chains to the target.

No two slots are in memory at the same time. Each slot has the whole 520 KB SRAM to itself.

## Flash layout

Two layouts depending on `THUMBYONE_WITH_MD`:

### Default build (`THUMBYONE_WITH_MD=ON`)

NES slot is 2 MB to hold PicoDrive's precomputed tables; every downstream partition shifts +1 MB and the shared FAT loses 1 MB of ROM storage.

| Partition  | Offset     | Size    | XIP address     | Purpose |
|-----------:|-----------:|--------:|-----------------|---------|
| Lobby      | `0x000000` | 128 KB  | `0x10000000`    | Selector, USB MSC, mkfs, handoff consumption |
| Handoff sector | `0x010000` | 4 KB | `0x10010000`    | Cross-slot payload (bigger than watchdog scratch can hold) |
| NES        | `0x020000` | **2 MB** | `0x10020000`   | ThumbyNES + **MD (PicoDrive)** |
| P8         | `0x220000` | 512 KB  | `0x10220000`    | ThumbyP8 firmware |
| DOOM       | `0x2A0000` | 2.5 MB  | `0x102A0000`    | ThumbyDOOM + shareware WAD |
| MPY        | `0x520000` | 2 MB    | `0x10520000`    | MicroPython + engine + 768 KB resource scratch |
| P8 scratch | `0x720000` | 252 KB  | `0x10720000`    | P8 active-cart working area |
| Settings sector | `0x75F000` | 4 KB | `0x1075F000`    | System-wide volume + brightness |
| Shared FAT | `0x760000` | **8.6 MB** | `0x10760000` | `/roms`, `/carts`, `/games`, `/Saves`, `/.favs`, `/.active_game` |

### Backward-compat build (`THUMBYONE_WITH_MD=OFF`)

Original layout with 1 MB NES and full 9.6 MB FAT. MD emulation is excluded entirely (no `.md/.gen/.bin` support in the picker).

| Partition  | Offset     | Size    | XIP address     | Purpose |
|-----------:|-----------:|--------:|-----------------|---------|
| NES        | `0x020000` | 1 MB    | `0x10020000`    | ThumbyNES (NES/SMS/GG/GB only) |
| P8         | `0x120000` | 512 KB  | `0x10120000`    | ThumbyP8 firmware |
| DOOM       | `0x1A0000` | 2.5 MB  | `0x101A0000`    | ThumbyDOOM + shareware WAD |
| MPY        | `0x420000` | 2 MB    | `0x10420000`    | MicroPython + engine |
| P8 scratch | `0x620000` | 252 KB  | `0x10620000`    | — |
| Settings sector | `0x65F000` | 4 KB | `0x1065F000`    | — |
| Shared FAT | `0x660000` | 9.6 MB  | `0x10660000`    | — |

Canonical source: [`common/slot_layout.h`](common/slot_layout.h) (preprocessor-selects the layout based on `THUMBYONE_WITH_MD`). Must stay in lock-step with [`common/pt.json`](common/pt.json) (no-MD) and [`common/pt_with_md.json`](common/pt_with_md.json) (with-MD), which are the partition tables consumed by the RP2350 bootrom.

## Boot and handoff

**Lobby startup** (`lobby/lobby_main.c`):

1. Init MENU button first — holding it at boot escapes to lobby even if a stale handoff magic would otherwise chain into a broken slot.
2. If no override held, consume any pending handoff via `thumbyone_handoff_consume_if_present()` — if there's one, the bootrom chains into the target and never returns.
3. Otherwise init LCD, buttons, USB, mount the shared FAT, render the grid.

**Slot entry** (applies to NES, P8, DOOM, MPY):

- Each slot links at `0x10000000` as though it owned the chip. The bootrom's ATRANS remap makes this a lie.
- On entry, each slot runs `thumbyone_xip_fast_setup()` from RAM. This resets the flash chip (Winbond 66h / 99h reset-enable / reset pair) and reconfigures QMI for fast continuous-read XIP. Without this step, flash left in continuous-read mode from the lobby's boot_stage2 is mis-interpreted when the slot first reconfigures QMI — we tracked an NES blank-screen hang to exactly this for a full day. See [`common/thumbyone_handoff.c`](common/thumbyone_handoff.c) and the memory note at [feedback_rp2350_xip_reset_first.md](https://github.com/austinio7116/ThumbyOne).

**Return to lobby:**

- Slot writes a "return to lobby" sentinel into watchdog scratch registers, calls `watchdog_reboot()`.
- Bootrom restarts, lobby consumes the sentinel, `rom_chain_image()` loads lobby firmware into place (no-op — it's already at `0x10000000`), lobby runs normally.

## Shared filesystem

The shared FAT is a plain FAT16 volume with 1 KB clusters, label `THUMBYONE`. Location and size depend on `THUMBYONE_WITH_MD`: **8.6 MB at `0x10760000`** in the default MD-enabled build, **9.6 MB at `0x10660000`** in the backward-compat build. All five participants (lobby + four slots) use the **same** FatFs R0.15 code, compiled from [`common/lib/fatfs/`](common/lib/fatfs/) with the same `ffconf.h`, linked against the same block device [`common/fs/thumbyone_disk.c`](common/fs/thumbyone_disk.c).

**Only the lobby ever calls `f_mkfs()`.** Slots strictly mount-or-fail. This guarantees on-disk layout identity across slots — a FAT written by the NES slot is byte-compatible with a FAT read by the MPY slot.

`thumbyone_disk.c` is a 512-byte-sector block device over 4 KB flash erase blocks, with read-modify-erase-program for sub-erase writes. Writes disable interrupts for the ~50 ms erase+program window per sector; this is why the lobby holds off slot launches for 500 ms after the last USB MSC op.

**MicroPython compatibility:** the MPY slot uses stock upstream FatFs R0.15 rather than the ooFatFs fork MicroPython historically carried. Port details in [`extmod/vfs_fat_diskio.c`](https://github.com/austinio7116/micropython/blob/thumbyone-slot/extmod/vfs_fat_diskio.c) — we rewrote the diskio shim for plain FatFs API and added a pre-mount fallback through `thumbyone_disk` for the pre-Python picker window.

## USB MSC centralisation

Single entry point for host transfers: `lobby/lobby_usb.c`.

- **Composite-less device** — MSC only, no CDC. Descriptor set is minimal (one interface, two endpoints).
- **Distinct VID/PID** (`0xCAFE:0x4020`) and serial prefix (`ONE-<board uid>`) so Windows doesn't inherit drive-letter assignments from earlier slot-era firmwares that used `0xCAFE:0x4011`.
- **tud_msc callbacks route directly to `thumbyone_disk_*`** — no deferred-write cache, because the RMW is already synchronous at the disk layer. Simpler state, no SYNCHRONIZE_CACHE work.
- **Slot-launch debounce** — lobby tracks `lobby_usb_last_op_us()`; A-press is accepted but handoff is held back until MSC has been quiet for 500 ms, so an in-flight `WRITE(10)` finishes before the FAT gets handed to a slot.

Slots carry no tinyUSB stack at all in ThumbyOne-slot-mode builds. We strip tinyUSB device + class drivers + descriptors, gate every `tud_task()` / `tusb_init()` / `tud_mounted()` call site behind `#ifndef THUMBYONE_SLOT_MODE`, and rely on `--gc-sections` to drop the rest. Per-slot savings: ~15 KB flash on NES/P8, ~27 KB on MPY, and several KB of SRAM each.

## Per-slot architecture

Every slot has a 520 KB SRAM budget and a ~250 MHz default clock (higher on overclock). Each has been individually tuned to maximise performance and compatibility while keeping RAM usage inside those limits. The lists below are the highlights — see the slot repos for the full story.

### NES / SMS / GG / GB / MD slot

**Emulator cores** (all vendored under [`ThumbyNES/vendor/`](https://github.com/austinio7116/ThumbyNES/tree/main/vendor)):

- **Nofrendo** (GPLv2) — NES 6502 + PPU + APU.
- **smsplus** (GPLv2, from the retro-go fork) — Master System / Game Gear Z80 + VDP + PSG.
- **Peanut-GB** (MIT) — Game Boy DMG + CGB core. Vendored from [fhoedemakers' fork](https://github.com/fhoedemakers/pico-peanutGB), which adds Game Boy Color support on top of Mahyar Koshkouei's original [Peanut-GB](https://github.com/deltabeard/Peanut-GB). Enabled via `PEANUT_FULL_GBC_SUPPORT`.
- **minigb_apu** (MIT) — Game Boy 4-channel APU by Alex Baines, paired with Peanut-GB.
- **PicoDrive** (LGPLv2, notaz master @ `dd762b8`) — Sega Mega Drive / Genesis. FAME 68K core + cz80 Z80 core + YM2612 FM + SN76489 PSG + VDP. Heavily patched for Cortex-M33 Thumb-2 (CZ80 function-pointer Thumb bit fix), XIP flash ROMs (`FAME_BIG_ENDIAN` + bswap-on-read in 17+ sites), heap-allocated statics (~4.4 MB of upstream BSS moved to heap), and build-time precomputed tables (FAME jumptable, YM2612 log-sine, cz80 SZHVC — ~730 KB of `const` flash data). Full catalogue: [`ThumbyNES/vendor/VENDORING.md`](https://github.com/austinio7116/ThumbyNES/blob/main/vendor/VENDORING.md#picodrive).

**Performance & compatibility:**

- **Multi-core dispatcher** (`nes_device_main.c`) switches between Nofrendo / smsplus / Peanut-GB / PicoDrive based on file extension; only one core is linked into the hot path per cart.
- **Per-cart clock override** — global + per-ROM selection of 125 / 150 / 200 / 250 MHz; dispatcher re-runs `nes_lcd_init` + `nes_audio_pwm_init` on every launch so SPI dividers and audio IRQ rate follow the new clock correctly.
- **Hot loops in SRAM** — `IRAM_ATTR` / `.time_critical.*` placement on the CPU+PPU inner loops so XIP cache misses never hit the frame budget. v1.01 also moved the smsplus Z80 core out of flash into RAM for a large SMS/GG speedup.
- **MD dynamic IRAM** — PicoDrive's `Cz80_Exec` (17 KB hot Z80 dispatch loop) lives in a custom flash section (`.md_iram_pool`) and gets `memcpy`'d into a heap-allocated SRAM buffer at `mdc_init`, with a `--wrap=Cz80_Exec` linker thunk that indirects callers through a function pointer. Zero BSS cost across sibling cores — the 17 KB only occupies heap while MD is the active emulator. +2-3 ms/frame reclaimed vs flash execution.
- **MD adaptive VDP skip-render** — when the last frame's emulation time overran the refresh budget, the next frame sets `PicoIn.skipFrame=1`, letting 68K + Z80 + audio emulate normally while the VDP line composite + `FinalizeLine` bail early. Caps at 2 consecutive skips. Saves ~6-8 ms on skipped frames; locks 50 PAL on heavy action scenes that would otherwise miss frames.
- **MD runtime audio modes** (FULL / HALF / OFF) — per-cart menu item. FULL = 22050 Hz YM2612 + PSG + Z80. HALF = 11025 Hz YM2612 with ZOH upsample (halves FM cost ~2.5 ms, audible HF roll-off). OFF = `POPT_EN_Z80|FM|PSG` stripped (completely silent, maximum refresh).
- **Zero-copy XIP ROM execution** — the picker walks FAT cluster chains for any ROM ≥256 KB, checks contiguity, and hands the core a direct pointer into the XIP address space. Fragmented ROMs trigger the in-firmware defragmenter (`f_expand` reserves a contiguous chain, then streams the file through a 4 KB buffer). MD's XIP path suppresses `PicoCartInsert`'s safety-op write (which would otherwise try to write 4 bytes to read-only flash).
- **Per-system scalers** — NES 2:1 nearest or 2×2 box-blend to 128×120 letterbox; SMS FIT / BLEND / FILL (new 1.5× fill in v1.02); GG & GB asymmetric 5:4 × 9:8 nearest; MD per-line streamed downsample (640 B scratch, no full-frame buffer) with packed-RGB565 2×2 blend and sx-column LUT; FILL in MD preserves aspect (scale-by-height, crop X sides) to match SMS's FILL behaviour.
- **Region detection** — iNES 2.0 byte-12, iNES 1.0 byte-9, with a filename fallback (`(E)`, `(PAL)`, `(Europe)`) — overridable per-ROM. MD uses PicoDrive's native header-based region detect (auto EU → US → JP preference).
- **Per-ROM save states** (NES / SMS: SNSS-tagged; GB: `'GBCS'`-tagged `gb_s` + APU memcpy; MD: PicoDrive's native state chunks) via a thin `thumby_state_bridge.[ch]` patch that macros `STATE_OPEN` / `STATE_WRITE` in nofrendo & smsplus over a single shared `FIL` for atomic save/load. PicoDrive uses its own `PicoState` API routed through the same bridge.
- **Battery SRAM** for all four families — NES battery (FCEUmm), SMS SRAM (smsplus), GB cart RAM, MD battery RAM (Sonic 3, Streets of Rage 2, etc.). 30 s autosave on change, `.sav` sidecar per cart.
- **Palette cycling** — six selectable palettes for NES and GB; SMS / GG drive their VDP palettes natively; MD reads the CRAM directly (512-colour palette, no user override needed).
- **Fast-forward 4× with audio preservation** — four cart frames per loop, only the newest rendered; audio ring untouched so FX don't stutter.
- **30 s battery-SRAM autosave** + 90 s idle → LCD blank + tight sleep.
- **Tabbed picker** with per-tab selection memory, favourites (hold B), 5–10 s B-hold to delete, live USB-activity rescan (polls MSC activity, rescans FAT after 400 ms of host quiet).

**SRAM discipline:**

- **One shared 32 KB RGB565 framebuffer** (`static uint16_t fb[128*128]`) across all three runners — no double-buffer.
- **Per-core source framebuffers** (NES ~137 KB, SMS ~98 KB, GB ~92 KB) are **malloc'd by each core's `init()` and freed by `shutdown()`** — only the active core's buffer occupies heap.
- **Cores coexist in BSS, not heap** — Nofrendo (~30 KB) + smsplus (~80 KB) + Peanut-GB (~30 KB) all present in the image, but only one is active at a time.
- Menu backdrop is a reused `static uint16_t fb_dim[128*128]` — allocated once, only in use while the menu is open.
- FAT scans use a single `static uint8_t fat_sec[512]`; defragger uses a single `static uint8_t buf[4096]` — no per-call mallocs.
- Screenshots stream one row at a time (no 8 KB scratch buffer).
- Typical free heap in-game: ~330 KB.

**Slot-mode adjustments** (`#ifdef THUMBYONE_SLOT_MODE`):

- ROMs + sidecars (saves, screenshots, state files, favourites) live under `/roms/` rather than the filesystem root.
- Boot splash + file-check diagnostic silent unless the defragger is actually running.
- MSC + tinyUSB excluded — ~15 KB flash + several KB RAM reclaimed.
- Picker's settings menu grows a "Back to lobby" action; in-game menu gains the same.
- `boot_filesystem()`'s auto-format on mount-failure is gated out — slots never wipe the shared FAT.

### PICO-8 slot

**Runtime**: clean-room PICO-8 fantasy console implementation. Uses **Lua 5.2.4** (`LUA_VERSION_NUM == 502` in [`ThumbyP8/lua/lua.h`](https://github.com/austinio7116/P8Thumb/blob/main/lua/lua.h)) — 5.2 and not 5.3 because 5.2's integer/bitwise story maps directly onto PICO-8's 16.16 fixed-point numeric model.

**Performance & compatibility:**

- **On-device cart conversion pipeline** (`.p8.png` → playable bytecode), running entirely on the Thumby:
  - stb_image streaming PNG decode via file-I/O callbacks (no full PNG in heap).
  - PXA decompressor for PICO-8's compressed code section.
  - **shrinko8 streaming tokenizer + parser + emitter** — a C port of shrinko8's unminify pass; handles minified PICO-8 source and converts PICO-8's fixed-point bitwise operators (`band`, `bor`, `shl`, `rotl`, `flr`, `\`, `@`, `$`, `%`, `!=`, `+=` etc.) to their Lua 5.2 equivalents during emission.
  - PICO-8 dialect character-level transforms: `\` → `p8idiv()`, `@` / `$` / `%` / `!=` → runtime calls, button glyphs → indices, `0b1010` binary → decimal, P8SCII high bytes → numeric escapes.
  - `luaL_loadbuffer` + `lua_dump` → Lua bytecode, programmed into the 256 KB active-cart scratch partition at `0x620000`.
  - One cart per boot cycle — prevents the ~260 KB PNG peak from fragmenting the Lua heap across multiple conversions.
- **int32 16.16 fixed-point numerics** — `lua_Number = int32_t` interpreted bit-for-bit as PICO-8's fixed-point format. Bitwise ops work on 32-bit patterns with no float round-trip, so `0xbe74` round-trips through `band` / `bor` / `shl` as an address without precision loss.
- **`_ENV` metatable fallback** — every source-level binding site gets `{__index = _G}` patched in so bare-global references in cart code still resolve under Lua 5.2's lexical env model.
- **String byte-indexing via metatable** — `str[i]` returns the byte value (PICO-8 convention) via a custom `__index` on the string metatable.
- **Multi-cart `load()` chain** — implemented as `watchdog_reboot` with `/.pending_load` marker + transition param plumbed through `stat(6)`; sub-carts hidden in `/.hidden` to declutter the picker.
- **`menuitem(index, label, cb)`** — up to 5 cart-defined custom pause-menu items, fully interoperating with our Back-to-lobby entry.
- **Full 4-channel audio synth** — 8 waveforms, pattern-advance with loop / stop flags, fade-in / fade-out, `fillp` patterns, `tline` tilemap blits, full P8SCII font rendering.

**SRAM discipline:**

- **XIP bytecode execution** — `lundump.c` patched so when the undump buffer address is in the XIP range (`0x10000000..0x11000000`, `IS_XIP_ADDR`), `Proto.code[]` and `Proto.lineinfo[]` become **direct flash pointers** instead of heap-copied arrays. `lfunc.c` patched so the GC never tries to free those XIP-resident arrays. Saves 40–80 KB of Lua heap per cart.
- **Debug info stripped** at `lua_dump` — reclaims another 5–20 KB per cart.
- **Capped allocator** — Lua VM hard-capped at 280 KB (`P8_LUA_HEAP_CAP`) so large carts OOM cleanly inside Lua rather than starving libc.
- **PICO-8 machine memory is `static uint8_t mem[64 KB]`** — drawing writes 4-bit colour indices straight into that (not RGB565). The present step expands indices → RGB565 **one scanline at a time** through a reused `static uint16_t scanline[128]`. Halves the RAM cost of the screen.
- **Cart ROM is `const uint8_t *` into XIP on device** (zero SRAM). Host build is the only path that malloc's it.
- **Streaming shrinko8 uses ~90 KB peak** — no AST, no token array, the C call stack *is* the parse tree.
- **Fixed-position Lua VM in BSS** — avoids heap fragmentation on successive cart loads.
- **Reboot-on-exit** — quit-to-picker triggers a watchdog reboot, guaranteeing the Lua heap is reclaimed cleanly with zero fragmentation carryover.
- **16 KB stack** (`PICO_STACK_SIZE=0x4000`) — default 2 KB is too small for PICO-8 C → Lua → C reentry.
- BSS ~148 KB, free heap in-game ~356 KB (280 KB Lua cap + ~76 KB libc headroom).

**Slot-mode adjustments**:

- Welcome / USB-mount-wait screen skipped — direct to picker.
- MSC + tinyUSB excluded.
- Picker menu grows a "Back to lobby" action.
- `boot_filesystem()` auto-format gated out.

### DOOM slot

Based on Graham Sanderson's [rp2040-doom](https://github.com/kilograham/rp2040-doom) port of Chocolate Doom, retargeted to RP2350 + Thumby Color. Shareware IWAD embedded via `.incbin` — ~2.3 MB, fits in the 2.5 MB partition with room. Pure XIP — the code runs direct from flash; no ROM load, no FAT access, no USB.

**Performance & compatibility** (`#if THUMBY_NATIVE` / `#if THUMBYONE_SLOT_MODE`):

- **Full 32-bit pointer model** — `shortptr_t` defanged to `void *` on RP2350; Doom's original 256 KB window / base-offset pointer compression is gone. Simpler, faster, fits the RP2350's memory map.
- **Single-core display pipeline** — the original rp2040-doom used PIO scanvideo + core1 beam-racing. On Thumby we render into a 128×128 8-bit indexed framebuffer on core0, palette-convert to RGB565 on the fly, and DMA to the GC9107. All the scanline / PIO machinery deleted.
- **Custom RGB565 melt wipe** in `i_video_thumby.c` — classic random-walk acceleration, operates directly on `g_fb`. The vanilla state machine is killed so the game tick keeps advancing during the wipe (audio / level state both continue).
- **320×200 overlay buffer** (`v_overlay_buf`) for HUD / menu / intermission / automap — composited at vanilla coordinates then 2×2 box-blended down to 128×128, with a split Y-map so the 32-row status bar lands on exactly 16 native rows.
- **Weapon sprite scaling fix** — `pspritescale = FRACUNIT * viewwidth / 320` (vanilla) not `/ 128`; weapon X centering uses the vanilla half-width (160); `BASEYCENTER = 57` so the weapon is vertically placed correctly.
- **OPL2 + SFX mixer on core1** — emu8950 OPL2 native 49716 Hz output, downsampled to 22050 Hz then mixed with 8-channel ADPCM SFX via int32 accumulators with clamping. Core1 runs `multicore_lockout_victim_init()` so flash erase/program on core0 can NMI-pause core1 during saves.
- **12-bit PWM DAC with triangular dither** (v1.2+) — up from 10-bit; eliminates quantisation noise. Loosened low-pass filter (1.5:1 ratio, ~85% new sample) for crisper SFX edges.
- **Settings persistence in save slot 7** — FPS cap, controls scheme, volume, music, gamma, overlay preferences all survive power cycles.
- **DOS-style boot log** — hooks the SDK's `stdio_driver_t` to capture `printf()` output during init and render it with the mini font (red header + grey scrolling text).
- **Crash diagnostics** — HardFault handler renders a red screen with PC / LR; DWT watchpoint infrastructure + a blue diagnostic screen available for debugging hangs.
- **Overlay menu** — hold LB + RB for 3 s: cheats (god, all-weapons, no-clip), level warp, gamma, volume / music sliders, control-scheme toggle, battery gauge.
- **Flash-safe save system** — `M_SaveSelect` auto-fills slot names (no on-screen text entry), uses `flash_safe_execute()` for the NMI-based multicore lockout during erase + program.
- **Auto-advance through the original's intro slides** — less tapping to start a game.

**SRAM discipline:**

- **160 KB fixed BSS zone** — `static byte zone[160 * 1024]` in `port/i_system_thumby.c` holds Doom's entire dynamic heap. Zero malloc in flight during play.
- **8-bit indexed double-buffer** — `frame_buffer[2][SCREENWIDTH * SCREENHEIGHT]` = 2 × 128×128 bytes (= 32 KB), **not** RGB565. Palette expansion happens once per present into `g_fb`. Halves the front-buffer cost vs storing RGB565 per buffer.
- **`list_buffer` aliased to both `render_cols` AND `flat_runs`** via `#define` — ~90 KB of BSS serves column data during the wall-render phase then repurposes as the flat cache during span rendering. The buffer is physically one allocation.
- **Single 32 KB `g_fb` LCD buffer** — no double-buffer for RGB565; DMA completes before the next write.
- **64 KB `v_overlay_buf`** reused every frame (cleared at frame top by `V_ClearOverlay()` — no separate HUD buffer).
- **8 KB audio ring buffer** total.
- **Stacks**: `PICO_STACK_SIZE=0x2000` on core0, **`PICO_CORE1_STACK_SIZE=0x800`** on core1 (audio loop is tight, 2 KB is enough).
- **`PICO_HEAP_SIZE=0`** — libc heap disabled entirely. Doom allocates only via its zone.
- **2 MB shareware WAD via `.incbin`** in `.rodata.doom1_whd` — executes / reads directly from XIP, never copied to RAM.
- **`USE_THINKER_POOL=0`** — the original's thinker pool disabled after a 1-byte corruption bug in it.
- **`DOOM_TINY=1 DOOM_SMALL=1`** — vendor's compressed-structures modes retained.

**Slot-mode adjustments:**

- **`M_QuitDOOM` short-circuits** — in-game Quit Game skips the vanilla "Are you sure?" dialog and calls `thumbyone_handoff_request_lobby()` inline under `#ifdef THUMBYONE_SLOT_MODE` (GCC inlines the function at `-O2` so linker `--wrap` alone isn't enough).
- **`I_Quit` wrapped** — `doom_quit_handoff.c` wraps `I_Quit` to drain display DMA (50 ms) and hand off to the lobby, covering any fatal-exit paths (out-of-memory, `Z_Malloc` failure, etc.) that bypass the main Quit menu.
- MSC + tinyUSB excluded.

### MicroPython + engine slot

The most involved slot, because we're bolting a pre-Python C picker onto MicroPython's boot sequence:

**Boot order:**

```
main()
  ├── thumbyone_xip_fast_setup()       // QMI reset + fast XIP
  ├── thumbyone_picker_run()           // C picker — see below
  │     └── writes /.active_game
  ├── mp_init()                        // MicroPython runtime
  ├── _boot_fat.py (frozen)            // vfs.mount shared FAT + ROM /system
  ├── thumbyone_launcher.py (frozen)   // read /.active_game, exec main.py
  └── pyexec REPL (fallback)
```

**The C picker** ([`common/picker/picker.c`](common/picker/picker.c)) runs **before** `mp_init()`. It mounts the shared FAT directly via FatFs (bypassing MicroPython's VFS which isn't up yet), scans `/games/<name>/main.py`, renders a hero view with icon + description, handles d-pad navigation + favourites + sort + menu overlay. On A-press it writes the chosen path to `/.active_game`, unmounts, tears down the LCD + SPI + DMA, and returns to `main()`. Zero Python runtime cost for selection — the first Python thing you see is the game itself.

**ROM-backed `/system/` VFS**: the engine's `filesystem/system/` tree (fonts, splashes, launcher assets, ~376 KB of 51 files) is packed into the firmware image at build time by [`tools/pack_system_rom.py`](tools/pack_system_rom.py) as a single 242 KB byte blob + 51-entry directory table. The C module in [`mp-thumby/ports/rp2/thumbyone_rom_vfs.c`](https://github.com/austinio7116/micropython/blob/thumbyone-slot/ports/rp2/thumbyone_rom_vfs.c) implements the MicroPython VFS protocol against that blob — `open()`, `stat()`, `ilistdir()`, stream read / seek / tell / close. `_boot_fat.py` mounts it at `/system` after the shared-FAT root mount, so `open('/system/assets/foo.bmp')` resolves transparently without consuming any FAT space. Added in 1.10: the same blob is **also mounted at `/lib`** with a `/lib`-prefixed path lookup, so legacy games that import from the original Thumby's library path (e.g. `from /lib/thumbyGrayscale import Grayscale` patterns, or any open(`/lib/...`) lookup) resolve into the same firmware-baked content with no FAT space cost. The `ThumbyOneRomVFS` C class accepts an optional path-prefix argument that gets prepended to entry lookups, so we mount the same underlying blob twice with different visible roots.

**Flash resource scratch override**: the Tiny Game Engine stores non-in-RAM textures into "flash scratch" via `hardware_flash`. The engine's default scratch region is at 1 MB from chip base — which in ThumbyOne is the **NES partition**. Left as-is, `TextureResource("foo.bmp")` would erase NES firmware, leading to truly glorious sprite corruption. The CMake passes `-DFLASH_RESOURCE_SPACE_BASE=0x560000u -DFLASH_RESOURCE_SPACE_SIZE=0xC0000u`, which points scratch at the upper 768 KB of the MPY partition; the engine source is `#ifndef`-guarded so the override takes effect.

**USBDEV disabled**: the MPY slot builds with `MICROPY_HW_ENABLE_USBDEV=0` + `MICROPY_HW_USB_MSC=0` + `MICROPY_PY_OS_DUPTERM=0`. No CDC serial, no MSC, no `stdin_ringbuf` dependency. Lobby owns USB; slot is USB-silent. The engine's multiplayer-link module (`engine_link_rp3.c`) is gated behind the same flag and compiles into no-op stubs.

**Launcher**: [`thumbyone_launcher.py`](https://github.com/austinio7116/micropython/blob/thumbyone-slot/ports/rp2/modules/thumbyone_launcher.py) is a frozen module. Reads `/.active_game`, adds the game dir to `sys.path`, `os.chdir`s into it (so `TextureResource("sprite.bmp")` resolves relative to the game folder), initialises `engine_save` with a per-game namespace, `exec`s `main.py`. On exception, captures the traceback to `/.last_error.txt` before falling through.

**`engine.reset()` → back-to-picker** — the MPY slot wraps `watchdog_reboot` via `-Wl,--wrap=watchdog_reboot` in [`thumbyone_reset_hook.c`](https://github.com/austinio7116/micropython/blob/thumbyone-slot/ports/rp2/thumbyone_reset_hook.c). MicroPython's `engine.reset()` and `machine.reset()` ultimately both go through `watchdog_reboot`; our wrap sets the handoff scratch to `THUMBYONE_SLOT_MPY` before calling the real function, so the chip reboots straight back into the MPY picker rather than the lobby. The **lobby-side** handoff path (`thumbyone_handoff_request_lobby`) uses `rom_reboot` instead of `watchdog_reboot`, bypassing the wrap — otherwise "Back to lobby" would be caught by the same wrap and loop back into MPY.

**FatFs port** — the MPY slot runs stock upstream FatFs R0.15 (vendored in [`common/lib/fatfs/`](common/lib/fatfs/)), not MicroPython's historical ooFatFs fork. The `extmod/vfs_fat_diskio.c` shim was rewritten against the R0.15 API; the block device is [`common/fs/thumbyone_disk.c`](common/fs/thumbyone_disk.c), shared byte-for-byte with the lobby and other slots — a FAT written by MPY is guaranteed readable by NES / P8 / DOOM and vice versa.

**Legacy (original-Thumby) game support** — added in 1.10. The launcher detects a legacy game heuristically (presence of a top-level `<game_dir>.py` file matching the original Thumby naming convention; native Color games use `main.py` only). When detected, before `exec`ing the game it installs a transparent compatibility layer:

- **`sys.modules['machine']` hijack.** A `_MachineShim` wrapper takes over the `machine` module so `from machine import Pin, PWM, UART` resolves to our shimmed classes. Untouched names (`Timer`, `freq`, `reset`, `disable_irq`, etc.) forward to the real module via `__getattr__`. Patching `machine.Pin` directly doesn't work — `machine` is a built-in module with a read-only attr dict.
- **Pin shim.** The class' `__new__(pin_id, ...)` dispatches:
  - GPIO 3, 4, 5, 6, 24, 27 → `_LegacyButtonPin` wrappers around `engine_io.UP/DOWN/LEFT/RIGHT/B/A`. `.value()` calls `engine_io.update_buttons()` then returns `0 if pressed else 1`, preserving the original Thumby's active-low semantics.
  - GPIO 28 → `_BUZZER_PIN` sentinel singleton.
  - GPIO 0, 1, 2 → `_NoopPin` (these are the Color d-pad GPIOs but the original Thumby uses them for the UART link cable; games that try to reconfigure them as outputs would brick three of the four d-pad buttons).
  - Anything else → real `machine.Pin` constructor.
- **PWM shim.** `_PwmShim.__new__(pin_obj, ...)` returns the singleton `_LegacyBuzzerPwm` instance when given the buzzer-pin sentinel, otherwise the real `machine.PWM`. `_LegacyBuzzerPwm` forwards to `thumbyAudio.audio.pwm` (which is the real `PWM(Pin(23))`) and applies frequency-detected volume scaling — *tone mode* (carrier < 20 kHz) uses cube scaling for perceptual linearity across the slider, *PCM mode* (carrier ≥ 20 kHz) re-centres the waveform around 50 % duty with linearly-attenuated swing so the class-D amp doesn't rail-clip. The mode flips automatically based on the underlying PWM's current frequency, queried per-call.
- **UART shim.** No-op `_LegacyUart` class (the Color has no link-cable pins) so `UART(0, ...)` constructors swallow without raising.
- **`thumbyHardware` button augmentation.** Frozen `thumbyHardware` only exports `swBuzzer` (the audio PWM); it doesn't expose `swA` / `swB` / `swU` / `swD` / `swL` / `swR` button objects. Some games (TinyGolf, Bowling Days) read those module-level attributes directly. The shim attaches `_LegacyButtonPin` wrappers to `thumbyHardware` post-import.
- **`display` shim for inline grayscale games.** A few games inline a partial copy of the Timendus grayscale library inside their own `display.py` rather than importing `thumbyGrayscale`. We sniff the file (`class Grayscale` + `_thread`) and replace `sys.modules['display']` with our `thumbyGrayscale` module so the inline copy never gets imported (its SSD1306 SPI / mem32 register pokes would crash on Color hardware).

**`thumbyGrayscale` library — two entry paths, one renderer.** Color-aware port of [Timendus's library](https://github.com/Timendus/thumby-grayscale), frozen into firmware under [`mp-thumby/ports/rp2/modules/thumbyGrayscale.py`](https://github.com/austinio7116/micropython/blob/thumbyone-slot/ports/rp2/modules/thumbyGrayscale.py). Two distinct legacy-game patterns reach it; both transparent:

  1. **Import path.** Games that do `import thumbyGrayscale` (newer indie titles, anything published after Timendus shipped the library as a standalone module — LyteBykes, Foxgine, RocketCup, etc.) pick up our frozen port directly. No special handling needed; standard frozen-module resolution.
  2. **Inline-display path.** Older games that predate the standalone library bundled their own copy of `class Grayscale` (often a slightly modified or pre-release version) inside the game folder's `display.py`. The canonical example is Umby & Glow — its `display.py` defines `class Grayscale` plus the `_thread`-based GPU pacing loop, then the rest of the game does `from display import *`. Two problems on Color: (a) those bundled copies poke SSD1306-specific SPI registers and `mem32[0xD0000000+0x01C]` PIO addresses that crash the device, and (b) we want them to share our colour renderer anyway. The launcher solves both with a `sys.modules` interception: before the game's first import resolves, we read `<game_dir>/display.py`, scan for the byte sequences `b"class Grayscale"` and `b"_thread"`, and if both are present we install `sys.modules['display'] = thumbyGrayscale`. Subsequent `import display` / `from display import Grayscale` then resolve to the frozen Color port, the bundled `display.py` is never parsed, and the game ends up talking to the same `Grayscale()` API surface as the import-path games. False-positive risk is low — the two-marker check is specific to the Timendus library + its variants.

  Both paths construct a `Grayscale()` object exposing the same 72×40 buffer + 72×40 shading-plane API the original Thumby firmware shipped. The actual blit goes via our `thumby_render` module onto Color's 128×128 RGB565 panel. Each output pixel is composed as `(buffer_bit | shading_bit << 1)` and looked up in a fixed 4-entry RGB565 palette baked into [`thumby_render.py`](https://github.com/austinio7116/micropython/blob/thumbyone-slot/ports/rp2/modules/thumby_render.py): `BLACK 0x0000 / WHITE 0xFFFF / DARKGRAY 0x4208 (~33 % luma) / LIGHTGRAY 0x8410 (~66 % luma)`. The original's GPU-thread sub-frame pacing (rapid SPI re-clocking on the SSD1306 to fake the four levels) is replaced by a single full-frame blit at the configured FPS (default 30 Hz, matching the original's effective rate). Games that supply only a buffer (no shading plane) hit a 2-entry mono palette via the same code path. The 4-entry palette is currently hardcoded — a per-game palette override would be a small addition (one bytearray per game folder + lookup at `Grayscale()` construct time) but no game has needed it yet.

**`thumby_render` module** — frozen helper at [`mp-thumby/ports/rp2/modules/thumby_render.py`](https://github.com/austinio7116/micropython/blob/thumbyone-slot/ports/rp2/modules/thumby_render.py). Owns a 128×128 RGB565 shadow texture, registers it as `engine_draw.set_background()`, and exposes viper-compiled `present_mono()` / `present_gray()` kernels that scale the legacy 72×40 mono / grey framebuffers up to fill the colour screen. Two render paths per kernel:

  - **1× pixel-perfect** when the active scale is 1.0 — direct byte-by-byte blit, centred 72×40 in 128×128 with a 28-px / 44-px black border.
  - **Nearest-neighbour scaled** for 1.5× / 1.75× / 2.0× / 2.5× — two precomputed bytearray LUTs (one per axis, ASCII source-coordinate per output pixel) feed the viper kernel so the per-pixel hot path pays zero divide cost. LUTs are rebuilt only when the active scale changes.

  Active scale comes from `/.legacy_scale` (ASCII "0".."4" → preset index into `(1.0, 1.5, 1.75, 2.0, 2.5)`) on first import. After that the per-frame `present_*()` entry points poll `engine_io.MENU.is_pressed` for a quick tap (rising-edge), and each tap advances `_scale_idx = (_scale_idx + 1) % 5` and rebuilds the LUTs in place. The change does **not** write back to `/.legacy_scale` — the picker's *Legacy scale* row remains the persisted source of truth, so re-launching the game starts again at the user's picker-set default. The 5-second `menu_watchdog` hold-to-lobby (`common/picker/menu_watchdog.c`) takes precedence over taps; we deliberately use `is_pressed` (level) rather than `is_just_pressed` (edge) for the tap detector because legacy games may absorb the edge into their own button tick before our render path sees it.

  An on-screen FPS overlay is gated by `/.legacy_fps == "1"` (set from the picker's *Legacy FPS* row) and renders into the 128×128 shadow with a tiny embedded 4×6 digit font, top-right corner.

**`engine_io.update_buttons()`** — small new C function in the engine for refreshing the button state machine without paying for a full engine `tick()`. Lets the legacy button shim poll fresh state from inside the game's own loop, which is the difference between legacy games being responsive and not — classic Thumby games drive their own ticking and never call `engine.tick()`, so without this entry point the shim couldn't refresh button state at all without dragging the entire engine pipeline through every `Pin.value()` call. Submitted upstream as a standalone PR.

**Polysynth shim — full 7-voice chiptune emulation (added in 1.11).** The original Thumby's [`polysynth.py`](https://github.com/TinyCircuits/TinyCircuits-Thumby-Games/blob/master/PSdemo/polysynth.py) library (used by PSdemo and TinyFreddy) is a 7-voice PIO-based synthesiser that drives bare GPIOs directly: 7 wave-generator output pins (7, 8, 9, 10, 11, 21, 22), an inhibit pin (25), and a mixed audio output (28). On Color those GPIOs collide with the LCD backlight (7), the RGB LED PWMs (10, 11), and the A / B / RB buttons (21, 22, 25); letting the upstream library run would brick the device while a song plays.

The launcher detects bundled `polysynth.py` (any non-empty file at `<game_dir>/polysynth.py`) before the game's first import resolves and replaces `sys.modules['polysynth']` with our frozen [`polysynth.py`](https://github.com/austinio7116/micropython/blob/thumbyone-slot/ports/rp2/modules/polysynth.py) shim. The bundled source therefore never gets parsed and none of its GPIO claims happen. The game's `polysynth.setpitch / setnote / play / playstream / playnote / instrument` calls land in the shim instead.

The shim routes notes through `engine_audio`'s 7-channel mixer (CHANNEL_COUNT bumped from 4 to 7 in `engine_audio_channel.h` specifically for this — RAM cost ~150 bytes, ISR cost a few extra branch-and-skip iterations per fire). Each logical polysynth voice owns one `ToneSoundResource` whose properties are configured up front:

  - **`shape`** (1.11 addition to `ToneSoundResource`): `0=SINE` (default — engine games unchanged), `1=SQUARE`, `2=NOISE`. Set per voice from the game's `configure(types)` call. The sampler in [`engine_tone_sound_resource.c`](https://github.com/austinio7116/Tiny-Game-Engine/blob/thumbyone/src/resources/engine_tone_sound_resource.c) dispatches on shape: SINE is the original `sinf(omega·time)`, SQUARE is `sign(sinf(omega·time))` (perfect 50% duty rectangular wave with all the harmonic content of a chiptune square), NOISE advances a 22-bit LFSR with feedback polynomial `bit_low XOR bit_high` matching the upstream library's `pio_lfsr` algorithm bit-for-bit. The LFSR step rate is gated by `frequency` so the noise's perceived pitch / brightness scales with the same control surface the original PIO uses.
  - **`instant_freq`** (1.11 addition): bool, default false. When true, frequency writes skip the engine's FADE_DOWN → FADE_UP smoothing (which exists to prevent clicks when one resource transitions between two unrelated pitches in normal engine usage). Polysynth wants snappy pitch changes for arpeggios, vibrato, and pitch rises — the fade smear sounds noticeably worse than the absence of a transition click for these patterns. Set true on every shim-managed voice.
  - **`phase`** (1.11 addition): float, settable in `[0, 1)` representing fraction-of-cycle. Writing `tone.phase = 0.0` resets `self->time = 0` (or `phase / frequency` for non-zero values), which is the lock-step needed for chord coherence. The same property lets the original library's per-instrument `phase` parameter (in halfcycles, where 0.5 = 90°, 1.0 = 180°, 1.5 = 270°) work correctly — the shim divides by 2 to get a fraction-of-cycle and writes the result.

Phase locking across multiple voices uses the engine's 22050 Hz mixer rate as an alignment quantum: the shim collects all phase-locked note starts in the current `audiotick` event-flush loop, then applies them as a tight sequence of Python statements (`tone.phase = 0.0; tone.frequency = hz` per voice). Each Python store takes single-digit microseconds; even a 7-voice chord lock fits inside one 45 µs audio sample, so all voices reset their `time` accumulator within the same ISR fire and produce the chord with audibly perfect phase alignment. This matches what the upstream library achieves via its `synthcore.put(0)` mixer-disable + parallel wavegen FIFO writes — different mechanism, same audible result.

What's not emulated: PSdemo's oscilloscope feature reads raw RP2040 GPIO state via direct register access at `0x40014000`, which is the RP2040 SIO GPIO_IN base. RP2350 has a different memory map; the read returns garbage, which the visualiser displays as garbage. The synth output also doesn't appear on a physical GPIO on Color (the shim writes via `engine_audio.play(tone, channel)` straight into the mixer ISR), so even if the addresses were correct there'd be nothing to read. The music still plays correctly — only the diagnostic visualiser is affected.

**`machine.freq` hijack — ties into the polysynth fix.** PSdemo runs `machine.freq(125_000_000)` at startup to "ensure full speed". Color boots at a higher clock; the standard rp2-port `machine.freq` calls `set_sys_clock_khz` and returns. That's fine for the CPU but drops a hidden audio bug: the engine's audio-mixer PWM-IRQ wrap was computed for the BOOT clock at engine init time, not the new clock. With the wrap stuck at the old value, the IRQ fires at a proportionally lower rate, samples take longer than `1/22050` s to play, and audio comes out at exactly half-rate — one octave low — when the user-side clock halves. Engine 1.11 exposes `engine.freq()` which calls `set_sys_clock_khz` AND `engine_audio_freq_adjust()` (recomputes the PWM-IRQ wrap for the new clock). The launcher's `_MachineShim.freq` routes `machine.freq(hz)` through `engine.freq(hz)` so any legacy game's clock change keeps the audio rate correct without source modification.

**Per-voice mixer-fair gain.** `engine_audio` mixes channels via simple summation then clamps to [-1, +1]. Summing 7 polysynth voices each at full ±1.0 amplitude clips brutally; the harmonic distortion the ear interprets as a wrong fundamental made earlier 1.11 test builds sound "octave low" in chord-heavy passages. Upstream's PIO mixer dodges this naturally because it's a 1-bit time-multiplexer — each voice's instantaneous amplitude contributes 1/N of the output duty, sum bounded to ±1.0 by construction. Our shim replicates the upstream semantics by setting each voice's `channel.gain = 1/corecount` in `_set_voice` and `_apply_per_voice_gain()` (called from `configure()` and `enabled()` whenever the active count changes). Per-voice peak amplitude scales with N exactly as the upstream library; chord summing fits cleanly without clipping.

**Defensive frequency clamp.** `tone_sound_resource_set_frequency` clamps incoming values to `[0, 100 kHz]`. The NOISE-shape sampler advances `lfsr_phase` by `frequency * dt` per outer ISR fire, then a `while(lfsr_phase >= 1.0f)` inner loop drains it one LFSR step at a time. With `frequency = Inf` the addition produces Inf, the loop's subtract keeps it Inf, and the audio ISR never returns — locking up the device hard. PSdemo songs with `instrument(rise=...)` accumulators could push the value to Inf over time, which hung early 1.11 builds. The clamp catches NaN and negatives via `!(frequency >= 0.0f)` (NaN compares false to all finite numbers) and Inf via the upper bound; sanitises everything to a sane range.

**`zlib` compatibility** — MicroPython 1.21+ renamed `zlib` to `deflate`. We freeze a small `zlib.py` wrapper into firmware ([`mp-thumby/ports/rp2/modules/zlib.py`](https://github.com/austinio7116/micropython/blob/thumbyone-slot/ports/rp2/modules/zlib.py)) — re-exports `decompress` / `compress` over `deflate.DeflateIO`. Required for BadApple's per-frame zlib-compressed audio block decoder.

**MicroPython viper `STORE_ATTR` reverted to MP 1.19.1 behaviour** — load-bearing for Umby & Glow. Upstream commit [`9714a0e`](https://github.com/micropython/micropython/commit/9714a0e) (Jim Mussared, Jul 2022, *"py/emitnative: Fix STORE_ATTR viper code-gen when value is not a pyobj"*) inserted an `MP_F_CONVERT_NATIVE_TO_OBJ` call into `emit_native_store_attr` so viper-typed `int` values stored into Python attributes get boxed via `mp_obj_new_int(val)` before reaching `mp_store_attr`. That's a legitimate type-safety fix from upstream's perspective — the old code passed the raw 32-bit word straight through to `mp_store_attr` as `mp_obj_t`, which is type confusion if the caller doesn't construct a valid tagged-pointer bit pattern. **But Umby & Glow's viper-compiled physics tick code deliberately exploits exactly that:** the upstream game writes attribute stores like

  ```python
  self._y = (yf + (yv >> 8)) << 1 | 1
  ```

  The trailing `<< 1 | 1` is a hand-rolled `MP_OBJ_NEW_SMALL_INT` — under `MICROPY_OBJ_REPR_A`, a small int with value `n` is stored as the pointer `(n << 1) | 1` (low bit = type tag). On MP 1.19.1 (the original Thumby firmware) that bit pattern goes straight through `mp_store_attr` as `mp_obj_t`, the slot holds a valid tagged small int, and the next read recovers `n`. **No allocation per tick** — the trick was the author's per-tick alloc-free attribute store. On MP 1.20+ the boxing call wraps the raw `(n << 1) | 1` literal as a fresh Python int with value `(n << 1) | 1` — double-encoded — so the next read returns `(n << 1) | 1` instead of `n`, and within ~6 game ticks the player's `_y` blows up exponentially and they die "fell into the abyss".

  We revert the Mussared commit in our `mp-thumby` fork (`py/emitnative.c::emit_native_store_attr` is back to the 1.19.1 form: pop, assert, store). The function gains a multi-line comment block calling out the constraint that drops out: **viper code that stores into attributes must use Python objects or pre-tagged small ints, never raw native ints.** Storing an even native `int` would now drive `10` (or whatever) through `mp_store_attr` as `mp_obj_t` — low bit 0, interpreted as a heap pointer to address `0xa`, GC corruption follows. Our launcher Python modules (`thumbyGraphics.py`, `thumby_render.py`, `thumbySaves.py`, `thumbyGrayscale.py`) and the engine's viper code paths are audited to avoid that pattern; new viper code added to the slot needs the same audit.

  This is a deliberate one-commit divergence from upstream. It applies *only* to the MPY slot in ThumbyOne — the engine's `engine`, `engine-1.23.0`, `engine-1.24.0` branches that stock TinyCircuits firmware tracks all carry the upstream fix and break Umby & Glow accordingly. The reasoning, the original commit hash, and the "if we ever rebase" note are kept inline in the source comment so a future maintainer doesn't accidentally paper over the divergence.

**SRAM discipline:**

Bolting a pre-Python C picker onto MicroPython has a cost: the picker's framebuffer, icon cache, game-metadata table, etc. can't just live in plain `.bss` — that would push `__bss_end__` up and permanently steal RAM from the MicroPython GC heap for data that's only alive for a second at boot. Several measures keep the slot honest:

- **`.picker_scratch` linker section** ([`memmap_mp_rp2350.ld`](https://github.com/austinio7116/micropython/blob/thumbyone-slot/ports/rp2/memmap_mp_rp2350.ld)) — a NOLOAD section pinned to `__StackLimit` (which equals `__GcHeapStart`). The picker's 32 KB framebuffer (`g_fb`), 8 KB icon cache (`g_icon_px`), and 4 KB game-metadata table (`g_games`) all live in this section via `__attribute__((section(".picker_scratch")))`. Because the section's address range sits *inside* the GC heap range, `gc_init()` claims those same bytes as heap once MicroPython is up. The picker uses them before `mp_init`, then they're transparently reclaimed — no code change, no `free` call, just linker alignment. Net: ~44 KB that would otherwise be stuck in BSS is returned to the GC heap.
- **No in-game overlay** — MENU-long-hold used to pop a 128×128 "returning to picker..." splash before rebooting, which required carrying a permanent 32 KB BSS framebuffer (`g_ovl_fb`) plus the LCD-acquire / SPI-release plumbing to steal the panel mid-frame from the running engine. The overlay has been dropped entirely; 5 s MENU hold reboots directly. The 5 s threshold is deliberate enough to not need visual confirmation. Saving: 32 KB BSS + several KB of code.
- **Menu-backdrop regenerated, not cached** — opening the picker's in-picker menu used to snapshot the hero frame into a dedicated `g_menu_backdrop[128×128]` so it could be restored + darkened on every cursor move. That 32 KB cache is gone; the menu renderer now re-renders the hero into `g_fb` and darkens in place. The redraw cost is trivial.
- **Frozen manifest pipeline** — launcher, `_boot_fat.py`, and assorted helpers are compiled to bytecode and frozen into the firmware image. No `.py` / `.mpy` files to pay FAT space for, no parse-time RAM on boot.
- **Shared flash-write RMW buffer** — `rp2_flash.c` uses a single `static uint8_t s_rmw_buf[FLASH_ERASE_BLOCK]` (+ a `static uint32_t s_saved_atrans[4]` for ATRANS save/restore around flash ops); every `flash.writeblocks` call reuses them, no per-call mallocs.
- **Shared MSC RMW buffer** — the lobby's `msc_disk.c` has one `static uint8_t s_msc_rmw_buf[FLASH_ERASE_SIZE]` that serves every host write.
- **Fixed-size PIO state tables** — `rp2_state_machine_initial_pc[NUM_PIOS*4]`, `rp2_pio_instruction_memory_usage_mask[NUM_PIOS]` — no dynamic PIO allocation overhead.
- **One-slot-at-a-time residency** — MPY gets the full 520 KB SRAM when running; no co-residency with NES / P8 / DOOM, so the MicroPython heap doesn't have to be pre-partitioned around sibling VMs.
- **`/system/` assets stay in XIP** — fonts, splash graphics, launcher art all served from `.rodata` by `thumbyone_rom_vfs.c`, never copied into RAM. Cumulative saving: 376 KB of files that would otherwise live on the FAT and, if `in_ram=True`, in SRAM too.

**Measured impact (v1.01+):** the ThumbyOne MPY slot now ships with **more MicroPython heap than stock firmware** — not just parity. Straight from the linker map (`firmware.elf.map`, `__GcHeapStart` to `__GcHeapEnd`):

| Build | BSS end | MicroPython GC heap |
|---|---|---:|
| Stock Thumby Color firmware | `0x2002e8b8` (~186 KB BSS) | **~197 KB** (`0x2004dcb8..0x2007f000`) |
| ThumbyOne MPY slot | `0x2000cbe4` (~51 KB BSS) | **~332 KB** (`0x2002bfe4..0x2007f000`) |
| **Delta** | −135 KB BSS | **+135 KB (~1.68× stock)** |

Both builds reserve the same 128 KB MicroPython C heap (libc `malloc`) and the same 4 KB extra stack. The difference is pure BSS → GC-heap reclaim. The lobby owning USB (no tinyUSB in the slot), the `.picker_scratch` section overlapping `__GcHeapStart`, ROM-backed `/system/`, and no co-tenant pre-partitioning each pay into that ~135 KB.

That extra heap is what unblocked import-heavy startup cases like Thumbalaga (MemoryError on its 30th `import` under stock; fine under ThumbyOne).

## Lobby architecture

- **Icon pipeline**: [`tools/pack_icons.py`](tools/pack_icons.py) runs at build time, reads the four PNGs in `lobby/icons/`, quantises each to a 16-colour adaptive palette, packs two 4-bit indices per byte → ~1.1 KB per icon + 32 bytes palette. Total icon data: ~4.8 KB, vs ~18 KB for raw RGB565. The blitter in [`lobby/lobby_icons.c`](lobby/lobby_icons.c) decodes on the fly — one shift + one palette lookup per pixel.
- **Grid**: 2×2 of 48×48 tiles at positions `(12,12)`, `(68,12)`, `(12,68)`, `(68,68)`. D-pad navigation via XOR on the cursor (UP/DOWN flip bit 1, LEFT/RIGHT flip bit 0).
- **Greyed tiles**: disabled slots (via `THUMBYONE_WITH_*` build flags) are drawn normally then per-channel right-shifted by 2 in place — a 1/4-brightness overlay that reads as "present but unavailable" rather than "missing".
- **USB state row**: top strip re-renders every 100 ms to reflect mount / activity state. Idle → green dot + "USB"; mounted → blue dot; transferring → red dot. The physical RGB LED (PWM on GP10/11/12) mirrors the same state at full brightness.

## Real-time clock (1.11)

ThumbyOne 1.11 added a shared real-time clock infrastructure: a vendored hardware-agnostic BM8563 driver, a thin i2c0 wrapper, lobby UI to set the time, a live home-screen clock, and a Game Boy cart-RTC integration in ThumbyNES.

**Hardware:** Color carries a [BM8563](https://www.lcsc.com/datasheet/lcsc_datasheet_2308181040_GATEMODE-BM8563EMA_C269878.pdf) I²C real-time clock at address `0x51` on `i2c0` (SDA = GP8, SCL = GP9). The chip exposes a low-voltage (`VL`) flag in its seconds register that latches whenever supply drops below threshold — read back later as "time was lost since this flag was last cleared". A successful set clears the flag; the lobby UI uses this to drive the visible difference between `--:--` (compromised) and `HH:MM` (trustworthy).

**Driver layout:**

- [`common/lib/bm8563/bm8563.{c,h}`](common/lib/bm8563/) — vendored straight from the engine's `lib/bm8563/`. Mika Tuupola's hardware-agnostic library (MIT). 281 lines C, 116 lines header.
- [`common/lib/thumbyone_rtc.{c,h}`](common/lib/) — thin wrapper. Sets up i2c0 at 100 kHz (the engine's vetted speed; 400 kHz was unreliable on this board's pull-up resistor values), enables the SDA/SCL pulls on GP8/9, registers C i2c read/write callbacks with the bm8563 driver, and exposes a small public API: `init / get / set / is_compromised`. Idempotent — safe to call from any slot's startup.
- CMake: `THUMBYONE_COMMON_RTC_SRC` + `THUMBYONE_COMMON_RTC_INCLUDE` are exported from `common/CMakeLists.txt` for any consumer that wants RTC access. Lobby and ThumbyNES both pick them up.

**Lobby UI:**

- **SET TIME submenu** ([`lobby/lobby_main.c::lobby_set_time_open()`](lobby/lobby_main.c)) — five-field date/time picker (year, month, day, hour, minute). UP/DOWN moves between fields, LEFT/RIGHT adjusts with autorepeat, A commits to the chip and exits, B cancels. The working state is a `struct tm` on the stack — only flushed to the chip on commit, so the user can dial through values without each LEFT/RIGHT triggering an I²C write (which would also fight the chip's ticking second register mid-edit).
- **Home-screen clock** — `HH:MM` rendered in the header bar between "ThumbyOne" and the USB indicator. Right-aligned just left of the USB label; falls back to dim `--:--` when `is_compromised()` returns true.
- **Live tick** — a `lobby_clock_tick()` helper runs once per second from inside the home-loop's `USB_PUMP` macro. It does a single 7-byte I²C read, compares the minute against `g_last_clock_minute`, and only re-renders the home screen when the minute actually changes (so the clock advances exactly once a minute, no per-second flicker on the SPI present).

**Pokemon GBC RTC:** Peanut-GB implements MBC3 cart RTC (`gb_set_rtc()`, `gb_tick_rtc()`) but the naive integration — read BM8563 at game load, seed `cart_rtc` to wall clock, tick once per second — produced a multi-hour shift on every reload. The bug: Pokemon's bootcode reads `cart_rtc`, then ZEROS it as part of its own RTC initialisation. With our seed clobbered, `cart_rtc` ticked from 0 during play, Pokemon wrote `last_seen = small_tick_count` to cart RAM, and on the next reload our seed put wall clock back into `cart_rtc` — Pokemon's elapsed math then returned `wall_clock − small`, advancing the in-game clock by ≈17 hours immediately (the diff between user-entered start time and "midnight day 0").
- 
- The fix is a `.rtc` sidecar that persists `cart_rtc[5] + wall_clock_unix_secs` alongside the `.sav`. At every successful `battery_save`, we write the sidecar. At cart load, if the sidecar exists we restore `cart_rtc = saved_cart_rtc + (now − saved_wall)`. Pokemon's first read at boot then reads exactly what `last_seen + real_wall_delta` would be on a real persistent-RTC cart; Pokemon's elapsed math gives the actual real-world delta, day/night and berry growth track wall clock across power-off, and there's no shift on reload because Pokemon's view of cart_rtc is consistent with its `last_seen` snapshot. On brand-new games (no sidecar yet), we don't seed — Pokemon zeros cart_rtc anyway, and the first save creates the sidecar that subsequent loads use.
- 
- Save state files (`.sta`) bumped from V1 to V2 — V2 embeds the wall-clock unix seconds at save moment. On state load, we advance `cart_rtc` by `(now − saved_wall)` so day/night / berry growth track real elapsed wall clock across the time the state file sat on disk. V1 files still load (V1 fallback path skips the wall-delta advance — same behaviour as a fresh state).
- 
- `gbc_tick_rtc()` is called once per real-world second from the runner loop (paced via `time_us_64()` accumulator, so fast-forward / scaler stalls don't desync the cart's clock from reality). Internal helpers `gbc_peek_cart_rtc(out[5])` / `gbc_poke_cart_rtc(in[5])` expose the raw bytes for sidecar persistence; `advance_cart_rtc(rtc[5], delta_secs)` does the carry math (sec → min → hour → 9-bit day with overflow flag, halt bit preserved).

Standalone ThumbyNES builds (without ThumbyOne) skip the RTC integration entirely — the `#ifdef THUMBYONE_SLOT_MODE` guards keep the standalone build dependency-free.

## Build system

ThumbyOne is a top-level CMake project that composes four subproject firmwares plus the lobby. Each subproject keeps its own repo and standalone build intact; ThumbyOne flips on `THUMBYONE_SLOT_MODE` during the unified build, which gates slot-specific behaviour (slot-mode picker, stripped USB, lobby-return menu items, overridden flash-scratch, etc.) without breaking standalone output.

**NES / P8 / DOOM**: pulled in via `add_subdirectory(../ThumbyNES/device)` etc. Shares CMake toolchain, pico-sdk init, project scope. Each emits a `<slot>_device.uf2` that the combiner then rebases.

**mp-thumby (MPY slot)**: uses `ExternalProject_Add` — its internal CMake is a full 600-line nested project with its own `pico_sdk_init()` and `project()` calls, which doesn't compose cleanly with `add_subdirectory`. The outer ThumbyOne passes `THUMBYONE_ROOT` so the inner CMake can find the picker, handoff, and common/fs sources.

**Combiner** ([`tools/combine_uf2.py`](tools/combine_uf2.py)): rebases each slot's UF2 to its target flash offset (per-slot `0x10000000 → 0x10<offset>`) and concatenates them into one flash-once UF2.

**Asset pipelines**:

- `tools/pack_system_rom.py` — engine `/system/` tree → C blob for the MPY slot.
- `tools/pack_icons.py` — lobby system icons PNG → 4-bit indexed C for the lobby.
- `ThumbyP8/tools/p8png_extract.py` — PICO-8 cart preprocessor (standalone to P8 but worth mentioning).

## Build matrix

The slot flags default to `ON`. Flipping any to `OFF` excludes its subproject from the build **and** greys out its tile in the lobby. `THUMBYONE_WITH_MD` is a separate flag that controls whether the NES slot includes PicoDrive (Mega Drive / Genesis emulation) — it's nested inside `THUMBYONE_WITH_NES`.

```
cmake -B build_device -DCMAKE_BUILD_TYPE=Release \
      [-DTHUMBYONE_WITH_NES=ON|OFF] \
      [-DTHUMBYONE_WITH_MD=ON|OFF]   \   # default ON, requires WITH_NES
      [-DTHUMBYONE_WITH_P8=ON|OFF] \
      [-DTHUMBYONE_WITH_DOOM=ON|OFF] \
      [-DTHUMBYONE_WITH_MPY=ON|OFF]
cmake --build build_device -j8
# -> build_device/thumbyone.uf2
```

Example sizes (release builds):

| Configuration | UF2 size | FAT size |
|---|---:|---:|
| Full with MD (default) | 10.9 MB | 8.6 MB |
| Full without MD | 8.9 MB | 9.6 MB |
| NES + P8 only (with MD) | 4.2 MB | 8.6 MB |
| NES + P8 only (no MD) | 2.2 MB | 9.6 MB |
| MPY only | 3.2 MB | 9.6 MB |
| DOOM only | 2.5 MB | 9.6 MB |

The MD build adds ~2 MB to the UF2 (the picodrive library + its precomputed YM2612 / FAME / cz80 flash tables) and loses 1 MB of shared FAT to the enlarged NES partition. PCE (added in 1.08) is HuCard-only and adds ~70 KB to the slot; it fits inside the existing partition and doesn't change the FAT layout.

## Build from source

**Prerequisites:**

- `arm-none-eabi-gcc` (10.3+)
- `cmake` (3.16+)
- `python3` with `Pillow` (for icon / ROM packers)
- Sibling checkouts of the subproject repos:

```
/your-work-dir/
    ThumbyNES/         https://github.com/austinio7116/ThumbyNES
    ThumbyP8/          https://github.com/austinio7116/P8Thumb
    ThumbyDOOM/        https://github.com/austinio7116/ThumbyDOOM
    mp-thumby/         https://github.com/austinio7116/micropython  (branch: thumbyone-slot)
    ThumbyOne/         https://github.com/austinio7116/ThumbyOne    (this repo)
```

`mp-thumby` vendors both the Pico SDK and the Tiny Game Engine as submodules — after cloning, `git submodule update --init --recursive` inside it.

Each sibling is independently buildable from its own `device/` subtree (or equivalent); ThumbyOne composes on top.

## Replacing lobby icons

Drop replacement PNGs into `lobby/icons/` (same filenames: `nes.png`, `p8.png`, `doom.png`, `mpy.png`). Any PNG size works — the packer resizes to 48×48. Rebuild; the lobby UF2 picks up the new art automatically because the CMake custom-command's `DEPENDS` list includes every PNG in the directory.

## Known limits / future work

- **Cross-slot chord for DOOM.** DOOM's in-game Quit menu returns to the lobby, and MENU-long-hold works for MPY/NES/P8. DOOM doesn't honour a long-hold chord yet — user has to go through the menu.
- **PCE wild write — workaround in place, root cause open.** PCE init produces a wild write somewhere we couldn't pinpoint with leading + trailing 256-byte canaries on every PCE allocation. The workaround pads every `my_special_alloc` allocation with 256 bytes on each side (~15 KB heap overhead per PCE session) so the wild write lands harmlessly. Symptoms before the workaround: any 32 KB malloc/free cycle in the same firmware-image session (notably `nes_menu_run`'s fb_dim) corrupted newlib's free-list and hung the next 64 KB malloc — surfaced most easily as PCE → SMS hanging at SMS render_init's malloc(64 KB). See `ThumbyNES/PCE_HEAP_BUG.md` for the full investigation, what's been ruled out, and where to start if you ever pick this back up.

## Repo layout

```
ThumbyOne/
├── CMakeLists.txt              # top-level composer
├── README.md                   # this file
├── PLAN.md                     # design history
├── common/
│   ├── slot_layout.h           # partition offsets (authoritative)
│   ├── pt.json                 # partition table for the bootrom
│   ├── thumbyone_handoff.[ch]  # cross-slot reset-chain API
│   ├── fs/
│   │   ├── thumbyone_disk.[ch] # shared FAT block device
│   │   ├── thumbyone_diskio.c  # FatFs diskio glue
│   │   └── thumbyone_fs.[ch]   # mount + mkfs helpers
│   ├── lib/fatfs/              # FatFs R0.15 (vendored)
│   └── picker/                 # C picker + BMP + LCD + font
│       ├── picker.c            # the MPY slot's hero picker + menu
│       ├── picker_bmp.[ch]     # 16 bpp BMP loader
│       ├── lcd_gc9107.[ch]     # shared LCD driver
│       └── font.[ch]           # bitmap font
├── lobby/
│   ├── lobby_main.c            # grid selector + USB state UI
│   ├── lobby_usb.[ch]          # tinyUSB MSC stack
│   ├── lobby_icons.[ch]        # 4-bit-indexed blitter
│   ├── tusb_config.h           # tinyUSB config
│   └── icons/                  # 48x48 system PNG sources
├── tools/
│   ├── combine_uf2.py          # UF2 rebaser + combiner
│   ├── pack_system_rom.py      # engine /system/ → C blob
│   └── pack_icons.py           # lobby icons → 4-bit C
└── docs/screenshots/           # for this README
```

## Acknowledgements

ThumbyOne stitches together a lot of work by a lot of people.

### The slot firmwares

Each system in ThumbyOne is a complete standalone firmware in its own repo; ThumbyOne just composes them. Full docs + standalone builds live at:

- **[ThumbyNES](https://github.com/austinio7116/ThumbyNES)** — NES / SMS / GG / Game Boy / Mega Drive / PC Engine emulator
- **[ThumbyP8](https://github.com/austinio7116/P8Thumb)** — PICO-8 fantasy console
- **[ThumbyDOOM](https://github.com/austinio7116/ThumbyDOOM)** — shareware DOOM
- **[TinyCircuits-Tiny-Game-Engine](https://github.com/austinio7116/TinyCircuits-Tiny-Game-Engine)** (austinio7116 fork) — MicroPython + engine slot
- **[mp-thumby](https://github.com/austinio7116/micropython)** (`thumbyone-slot` branch) — MicroPython port with the ThumbyOne hooks

### The upstream projects the slots stand on

- **[TinyCircuits](https://tinycircuits.com/)** — made the Thumby Color.
- **[Tiny Game Engine](https://github.com/TinyCircuits/TinyCircuits-Tiny-Game-Engine)** — original C engine + MicroPython port.
- **Emulator cores in ThumbyNES:**
  - **[Nofrendo](https://github.com/TheDuckEmulates/nofrendo)** — NES 6502 + PPU + APU.
  - **[smsplus](https://github.com/ducalex/retro-go)** (from the retro-go fork of Charles MacDonald's original) — Master System / Game Gear Z80 + VDP + PSG.
  - **[Peanut-GB](https://github.com/deltabeard/Peanut-GB)** by Mahyar Koshkouei (deltabeard) — Game Boy DMG core; CGB support added by Frans Hoedemakers in the **[pico-peanutGB fork](https://github.com/fhoedemakers/pico-peanutGB)** which ThumbyNES vendors.
  - **[minigb_apu / MiniGBS](https://github.com/baines/MiniGBS)** by Alex Baines — Game Boy APU, paired with Peanut-GB.
  - **[PicoDrive](https://github.com/notaz/picodrive)** by notaz (Grazvydas Ignotas) — Mega Drive / Genesis 68K + Z80 + VDP + YM2612 / SN76489. ThumbyNES vendors a HuCard-only-style trim with the FAME 68K and CZ80 cores.
  - **[HuExpress](https://github.com/ducalex/retro-go)** (from the retro-go ODROID-GO fork of David Michel and Cédric Stiehl's [Hu-Go!](http://www.zeograd.com/parent.php?section=hugo)) — PC Engine / TurboGrafx-16 HuC6280 + VDC + VCE + PSG. ThumbyNES strips it down to HuCards-only and replaces the upstream full-frame renderer with a per-scanline composite.
- **[rp2040-doom](https://github.com/kilograham/rp2040-doom)** — Graham Sanderson's tour-de-force DOOM port (Chocolate Doom → RP2040/RP2350).
- **[Lexaloffle](https://www.lexaloffle.com/)** — creators of PICO-8. ThumbyP8 is a clean-room implementation of the documented API; if you play carts you like, [buy PICO-8](https://www.lexaloffle.com/pico-8.php) to support the creators and the community.
- **[MicroPython](https://micropython.org/)** — Damien George and contributors.
- **[Pico SDK](https://github.com/raspberrypi/pico-sdk)** and **[tinyUSB](https://github.com/hathach/tinyusb)** — the backbone of every RP2xxx project.
- **[FatFs](http://elm-chan.org/fsw/ff/00index_e.html)** (ChaN) — the shared filesystem across every slot.

---

*One firmware to rule them all.*
