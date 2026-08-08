#
# ThumbyOne — unified multi-slot firmware build.
#
# Pulls in the ThumbyOne lobby + enabled subproject firmwares
# (ThumbyNES, ThumbyP8, ThumbyDOOM, mp-thumby) and produces one
# flash-once UF2 that boots into the lobby and can launch each
# system from its partition.
#
# Build:
#   cmake -B build_device -DCMAKE_BUILD_TYPE=Release \
#         [-DTHUMBYONE_WITH_NES=ON|OFF]   (default ON)
#         [-DTHUMBYONE_WITH_P8=ON|OFF]    (default ON)
#         [-DTHUMBYONE_WITH_DOOM=ON|OFF]  (default ON)
#         [-DTHUMBYONE_WITH_MPY=ON|OFF]   (default ON)
#   cmake --build build_device -j8
#
# Each flag toggles whether that slot's subproject is built and
# included in the combined UF2. The lobby's grid also greys out
# any disabled slot so the selector can't launch a partition
# that isn't present.
#
# Output: build_device/thumbyone.uf2
#

cmake_minimum_required(VERSION 3.16)

# ----- Pico SDK setup --------------------------------------------------
set(PICO_BOARD    tinycircuits_thumby_color_rp2350)
set(PICO_PLATFORM rp2350)
if (NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "/home/maustin/mp-thumby/lib/pico-sdk")
endif()
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(thumbyone C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# Signals to subprojects that this is the unified ThumbyOne
# build, not a standalone build of any individual project.
set(THUMBYONE_UNIFIED_BUILD ON)

# ----- ThumbyOne firmware version --------------------------------------
# Single source of truth — bumping this here updates every menu in
# every slot (lobby About row, shared content-picker fw row, NES
# in-game About line). Subprojects that build standalone (NES on
# its own, MPY on its own) keep an inline #define fallback in their
# .c source so they still compile sensibly outside the unified
# build, but inside the ThumbyOne build this CMake variable wins.
#
# DOOM is deliberately excluded — it tracks the ThumbyDOOM port's
# own versioning ("ThumbyDOOM v1.2"), not ThumbyOne's release
# cadence.
set(THUMBYONE_FW_VERSION "1.36.1")
add_compile_definitions(THUMBYONE_FW_VERSION="${THUMBYONE_FW_VERSION}")

# ----- Slot selection --------------------------------------------------
# (Only NES is wired in step C1. P8 / DOOM / MPY follow once their
# subproject CMake hooks are in place.)
option(THUMBYONE_WITH_NES   "Include ThumbyNES firmware"     ON)
option(THUMBYONE_WITH_P8    "Include ThumbyP8 firmware"      ON)
option(THUMBYONE_WITH_DOOM  "Include ThumbyDOOM firmware"    ON)
option(THUMBYONE_WITH_MPY   "Include MicroPython firmware"   ON)
option(THUMBYONE_WITH_SCUMM "Include ThumbyScummby firmware" ON)
# The Mote OS slot hosts a whole library off the shared FAT — Cue and
# Indemnity Run are now Mote GAMES in /mote/, NOT firmware slots. Since 1.29
# ThumbyCraft is back as its own standalone slot. The STANDARD build =
# these defaults + THUMBYONE_WITH_CRAFT=ON; ELITE and ROGUE stay OFF.
# Do NOT enable ELITE/ROGUE for a normal build — that ships Cue/Indemnity
# as slots again (the mistake this comment exists to prevent).
option(THUMBYONE_WITH_MOTE  "Include the Mote OS slot"       ON)
option(THUMBYONE_WITH_CRAFT "Include ThumbyCraft slot (standard build: ON)" OFF)
option(THUMBYONE_WITH_ROGUE "Build ThumbyCue as a slot (former rogue partition) — non-standard" OFF)
option(THUMBYONE_WITH_ELITE "Build Indemnity Run/Elite as a slot — non-standard" OFF)
# Optional 9th slot: ThumbyRogue appended after Elite (custom variant only).
option(THUMBYONE_WITH_ROGUE9 "Add ThumbyRogue as a 9th slot" OFF)

# Propagate the WITH_X flags as compile-time defines so slot_layout.h's
# `#if THUMBYONE_WITH_<X>` conditionals see the *actual* enable state
# instead of the header's hardcoded `#ifndef ... #define 1` fallback.
# Without these definitions, every non-default preset (no-doom,
# scumm-only, …) would build with mismatched FAT offsets between
# pt.json (correct) and slot_layout.h (always all-on), causing the
# lobby to read/format the FAT at the wrong flash address and slot
# launches to chain into wrong partitions.
foreach(_flag NES P8 DOOM MPY SCUMM CRAFT ROGUE ELITE ROGUE9 MOTE)
    if (THUMBYONE_WITH_${_flag})
        add_compile_definitions(THUMBYONE_WITH_${_flag}=1)
    else()
        add_compile_definitions(THUMBYONE_WITH_${_flag}=0)
    endif()
endforeach()
# THUMBYONE_WITH_MD: include the Mega Drive / Genesis core (PicoDrive)
# in ThumbyNES. MD's precomputed flash tables (FAME jumptable + YM2612
# + cz80) add ~850 KB, pushing ThumbyNES past the default 1 MB NES
# partition. WITH_MD=ON grows the NES partition to 2 MB and shifts
# every downstream partition + the FAT up by 1 MB (FAT goes from 9.6
# MB to 8.6 MB of shared ROM storage). WITH_MD=OFF keeps the original
# partition layout for backward-compatible smaller firmware builds.
option(THUMBYONE_WITH_MD   "Include Mega Drive (PicoDrive) in ThumbyNES" ON)
if (THUMBYONE_WITH_MD AND NOT THUMBYONE_WITH_NES)
    message(FATAL_ERROR "THUMBYONE_WITH_MD requires THUMBYONE_WITH_NES — MD lives inside the NES slot.")
endif()
# Propagate as a compile define into the lobby + every slot so
# slot_layout.h picks the matching partition offsets.
if (THUMBYONE_WITH_MD)
    add_compile_definitions(THUMBYONE_WITH_MD=1)
endif()

# THUMBYNES_YM2612_GENPLUS: pick which YM2612 implementation MD/Genesis
# uses. OFF = PicoDrive's stock YM2612 (small, fast, MAME-derived).
# ON = Genesis Plus GX YM2612 (larger; matches RetroArch's GenPlus core
# audio quality). Forwarded to the NES slot's CMakeLists below where
# the actual filter / table-generator wiring lives.
option(THUMBYNES_YM2612_GENPLUS
    "Use Genesis Plus GX YM2612 (vendored separately) instead of PicoDrive's"
    OFF)

# THUMBYONE_WITH_PCE: include the PC Engine / TurboGrafx-16 core
# (HuExpress) in the ThumbyNES slot. Adds ~143 KB of flash on top of
# MD; with MD+PCE the NES partition lands at ~1.95 MB / 2 MB capacity.
# No partition table change required — fits inside the existing
# WITH_MD layout. Default ON since the cost is acceptable.
option(THUMBYONE_WITH_PCE  "Include PC Engine (HuExpress) in ThumbyNES" ON)
if (THUMBYONE_WITH_PCE AND NOT THUMBYONE_WITH_NES)
    message(FATAL_ERROR "THUMBYONE_WITH_PCE requires THUMBYONE_WITH_NES — PCE lives inside the NES slot.")
endif()
# ThumbyNES's CMakeLists checks DEFINED THUMBYONE_WITH_PCE, so always
# set the variable explicitly (true/false) regardless of value here.
# A compile define is also useful if any C code wants to know.
if (THUMBYONE_WITH_PCE)
    add_compile_definitions(THUMBYONE_WITH_PCE=1)
endif()

# ----- Shared library --------------------------------------------------
# Brings in the handoff API + slot_layout.h + THUMBYONE_PT_JSON.
add_subdirectory(common)

# Pull in the generated offset/size constants emitted by
# tools/gen_pt.py.  The same flags drive both the C header
# (common/slot_layout.h via #if cascade) and these CMake variables
# (via the generator's --cmake-out emission).  Slot rebase math
# below uses them instead of hardcoded hex literals.
include(${THUMBYONE_PT_CMAKE})


# ----- Lobby ----------------------------------------------------------
# Flash at 0x10000000, absolute family UF2, partition table
# embedded.
# Pack the lobby's system-selector icons (48x48, 4-bit indexed) at
# build time from the PNGs under lobby/icons/. Regenerates when any
# PNG changes; the generated C is compiled into the lobby executable
# alongside lobby_icons.c's blit function.
set(LOBBY_ICONS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lobby/icons)
set(LOBBY_ICONS_GEN ${CMAKE_CURRENT_BINARY_DIR}/lobby_icons_data.c)
file(GLOB LOBBY_ICON_PNGS CONFIGURE_DEPENDS ${LOBBY_ICONS_DIR}/*.png)
add_custom_command(
    OUTPUT  ${LOBBY_ICONS_GEN}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_icons.py
            ${LOBBY_ICONS_DIR} ${LOBBY_ICONS_GEN}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_icons.py
            ${LOBBY_ICON_PNGS}
    COMMENT "Packing lobby system icons"
    VERBATIM
)

add_executable(lobby
    lobby/lobby_main.c
    lobby/lobby_usb.c
    lobby/lobby_icons.c
    lobby/lobby_defrag.c
    ${LOBBY_ICONS_GEN}
    ${THUMBYONE_PICKER_UI_SRC}
    ${THUMBYONE_COMMON_SRC}
    ${THUMBYONE_COMMON_FS_SRC}
    ${THUMBYONE_FATFS_SRC}
    ${THUMBYONE_COMMON_BATTERY_SRC}
    ${THUMBYONE_COMMON_FS_STATS_SRC}
    ${THUMBYONE_COMMON_BACKLIGHT_SRC}
    ${THUMBYONE_COMMON_LED_SRC}
    ${THUMBYONE_COMMON_SETTINGS_SRC}
    ${THUMBYONE_COMMON_RTC_SRC}
)
target_include_directories(lobby PRIVATE
    lobby
    ${THUMBYONE_PICKER_INCLUDE}
    ${THUMBYONE_COMMON_INCLUDE}
    ${THUMBYONE_FATFS_INCLUDE}
    ${THUMBYONE_COMMON_BATTERY_INCLUDE}
    ${THUMBYONE_COMMON_FS_STATS_INCLUDE}
    ${THUMBYONE_COMMON_BACKLIGHT_INCLUDE}
    ${THUMBYONE_COMMON_LED_INCLUDE}
    ${THUMBYONE_COMMON_SETTINGS_INCLUDE}
    ${THUMBYONE_COMMON_RTC_INCLUDE}
)
# tusb_config.h lives in lobby/ — put the lobby dir BEFORE the
# tinyUSB include search so our config is picked up rather than
# tinyUSB's default.
target_include_directories(lobby BEFORE PRIVATE lobby)
target_link_libraries(lobby PRIVATE
    pico_stdlib
    pico_bootrom
    pico_unique_id
    hardware_watchdog
    hardware_gpio
    hardware_pwm
    hardware_pio    # shared thumbyone_backlight (PIO PWM on GP7)
    hardware_adc
    hardware_i2c    # BM8563 RTC chip on i2c0 (GPIO 8/9)
    hardware_spi
    hardware_dma
    hardware_flash
    tinyusb_device
    tinyusb_board
)

# Which slots are present in this build? The lobby grid greys out
# any disabled tile and skips it during D-pad navigation. 1 / 0 so
# the lobby can just drop the define into a bool array initialiser.
target_compile_definitions(lobby PRIVATE
    THUMBYONE_LOBBY_HAS_NES=$<IF:$<BOOL:${THUMBYONE_WITH_NES}>,1,0>
    THUMBYONE_LOBBY_HAS_P8=$<IF:$<BOOL:${THUMBYONE_WITH_P8}>,1,0>
    THUMBYONE_LOBBY_HAS_DOOM=$<IF:$<BOOL:${THUMBYONE_WITH_DOOM}>,1,0>
    THUMBYONE_LOBBY_HAS_MPY=$<IF:$<BOOL:${THUMBYONE_WITH_MPY}>,1,0>
    THUMBYONE_LOBBY_HAS_SCUMM=$<IF:$<BOOL:${THUMBYONE_WITH_SCUMM}>,1,0>
    THUMBYONE_LOBBY_HAS_CRAFT=$<IF:$<BOOL:${THUMBYONE_WITH_CRAFT}>,1,0>
    THUMBYONE_LOBBY_HAS_ROGUE=$<IF:$<BOOL:${THUMBYONE_WITH_ROGUE}>,1,0>
    THUMBYONE_LOBBY_HAS_ELITE=$<IF:$<BOOL:${THUMBYONE_WITH_ELITE}>,1,0>
    THUMBYONE_LOBBY_HAS_ROGUE9=$<IF:$<BOOL:${THUMBYONE_WITH_ROGUE9}>,1,0>
    THUMBYONE_LOBBY_HAS_MOTE=$<IF:$<BOOL:${THUMBYONE_WITH_MOTE}>,1,0>
)
pico_embed_pt_in_binary(lobby ${THUMBYONE_PT_JSON})
add_dependencies(lobby thumbyone_pt_json)
pico_set_uf2_family(lobby "absolute")
pico_add_extra_outputs(lobby)


# ----- Collected per-slot ELFs → UF2 combine spec ---------------------
# Each enabled slot appends its UF2 path (and the flash offset to
# rebase it to) to THUMBYONE_SLOT_UF2_SPECS. The combine step at
# the bottom reads this list and produces one flash-once UF2.
set(THUMBYONE_SLOT_UF2_INPUTS ${CMAKE_CURRENT_BINARY_DIR}/lobby.uf2)
set(THUMBYONE_SLOT_UF2_REBASES "")   # no rebase for lobby
set(THUMBYONE_SLOT_DEPS lobby)


# ----- NES slot -------------------------------------------------------
if (THUMBYONE_WITH_NES)
    set(THUMBYONE_SLOT_MODE "nes")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ThumbyNES/device
                     ${CMAKE_BINARY_DIR}/nes_slot
                     EXCLUDE_FROM_ALL)
    unset(THUMBYONE_SLOT_MODE)

    set(NES_TARGET nesrun_device)
    list(APPEND THUMBYONE_SLOT_UF2_INPUTS
        ${CMAKE_BINARY_DIR}/nes_slot/${NES_TARGET}.uf2)
    math(EXPR NES_REBASE_DST "0x10000000 + ${THUMBYONE_NES_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "${NES_TARGET}.uf2=0x10000000:${NES_REBASE_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS ${NES_TARGET})
endif()


# ----- P8 slot --------------------------------------------------------
# Linked at default 0x10000000 logically (bootrom's ATRANS remaps).
# Combiner rebases blocks to 0x10120000 (partition 1, "P8", per
# common/pt.json: 512 KB at flash offset 1152K = 0x120000).
if (THUMBYONE_WITH_P8)
    set(THUMBYONE_SLOT_MODE "p8")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ThumbyP8/device
                     ${CMAKE_BINARY_DIR}/p8_slot
                     EXCLUDE_FROM_ALL)
    unset(THUMBYONE_SLOT_MODE)

    set(P8_TARGET p8run_device)
    list(APPEND THUMBYONE_SLOT_UF2_INPUTS
        ${CMAKE_BINARY_DIR}/p8_slot/${P8_TARGET}.uf2)
    math(EXPR P8_REBASE_DST "0x10000000 + ${THUMBYONE_P8_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "${P8_TARGET}.uf2=0x10000000:${P8_REBASE_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS ${P8_TARGET})
endif()


# ----- DOOM slot ------------------------------------------------------
# Rebases to flash offset 0x1A0000 (partition 2, "DOOM", 2.5 MB).
# Shareware WHD is embedded in the firmware via .incbin, so the
# full image is ~2.37 MB. Fits with breathing room.
if (THUMBYONE_WITH_DOOM)
    set(THUMBYONE_SLOT_MODE "doom")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ThumbyDOOM/device
                     ${CMAKE_BINARY_DIR}/doom_slot
                     EXCLUDE_FROM_ALL)
    unset(THUMBYONE_SLOT_MODE)

    set(DOOM_TARGET thumbydoom)
    list(APPEND THUMBYONE_SLOT_UF2_INPUTS
        ${CMAKE_BINARY_DIR}/doom_slot/${DOOM_TARGET}.uf2)
    math(EXPR DOOM_REBASE_DST "0x10000000 + ${THUMBYONE_DOOM_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "${DOOM_TARGET}.uf2=0x10000000:${DOOM_REBASE_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS ${DOOM_TARGET})
endif()


# ----- MicroPython slot -----------------------------------------------
# Rebases to flash offset 0x420000 (partition 3, "MPY", 2 MB).
#
# mp-thumby has its own complex CMake project with its own
# pico_sdk_init() + project() calls and ~600 lines of module
# configuration. Rather than shoehorn that into add_subdirectory
# (which is messy with nested projects), we use ExternalProject_Add
# to invoke a fully isolated CMake build. Outputs firmware.uf2
# which the combiner then rebases.
#
# THUMBYONE_SLOT_MODE / THUMBYONE_COMMON_SRC / THUMBYONE_COMMON_INCLUDE
# are passed through so mp-thumby's CMakeLists' ThumbyOne hook
# (at the bottom of ports/rp2/CMakeLists.txt) compiles in the
# handoff source and marks UF2 family "absolute".
if (THUMBYONE_WITH_MPY)
    include(ExternalProject)

    # mp-thumby isn't always a direct sibling of ThumbyOne — on
    # the primary development box it lives at $HOME/mp-thumby
    # because the Pico SDK is vendored under it and the SDK path
    # is referenced widely. Probe both locations; allow override.
    if (NOT DEFINED THUMBYONE_MPTHUMBY_DIR)
        if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../mp-thumby/ports/rp2/CMakeLists.txt)
            set(THUMBYONE_MPTHUMBY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../mp-thumby)
        elseif (EXISTS $ENV{HOME}/mp-thumby/ports/rp2/CMakeLists.txt)
            set(THUMBYONE_MPTHUMBY_DIR $ENV{HOME}/mp-thumby)
        else()
            message(FATAL_ERROR
                "mp-thumby not found. Set THUMBYONE_MPTHUMBY_DIR "
                "or clone mp-thumby alongside ThumbyOne "
                "(or under $HOME/mp-thumby).")
        endif()
    endif()
    message(STATUS "mp-thumby: ${THUMBYONE_MPTHUMBY_DIR}")

    set(MPY_SRC_DIR   ${THUMBYONE_MPTHUMBY_DIR}/ports/rp2)
    set(MPY_BUILD_DIR ${CMAKE_BINARY_DIR}/mpy_slot)
    set(MPY_UF2 ${MPY_BUILD_DIR}/firmware.uf2)

    # User C modules: Tiny Game Engine ONLY — the top-level
    # TinyCircuits-Tiny-Game-Engine/micropython.cmake also pulls
    # in gbemu, chess, and doom user modules, but ThumbyOne ships
    # those systems as dedicated slots so including them again
    # inside the MPY slot is pure duplication. Point straight at
    # the engine's own micropython.cmake to skip the extras.
    set(MPY_USER_C_MODULES
        ${THUMBYONE_MPTHUMBY_DIR}/TinyCircuits-Tiny-Game-Engine/src/micropython.cmake)

    # Build mp-thumby via its own cmake, isolated from ours.
    # THUMBYONE_ROOT lets mp-thumby's port CMakeLists assemble source
    # paths (picker, common/fs, lib/fatfs) itself — passing multi-
    # element CMake lists across ExternalProject_Add is awkward
    # because of semicolon mangling, so we hand over the tree root
    # and let the consumer figure out the rest.
    # Propagate ALL layout-affecting WITH_* flags so slot_layout.h (force-
    # included via mpconfigboard.h) computes the SAME THUMBYONE_FAT_OFFSET
    # as the lobby. The MPY slot is an isolated ExternalProject, so it does
    # NOT inherit the top-level add_compile_definitions() above — without
    # this it falls back to slot_layout.h's all-defaults (DOOM=1, CRAFT=0,
    # ...) and the picker/mpconfigboard mount the shared FAT at the wrong
    # flash offset ("FS ERR mount failed, go to lobby, wipe"). Only WITH_MD
    # used to be forwarded, so every preset whose DOOM/CRAFT/... differ from
    # those defaults was mismatched — including the default build once the
    # CRAFT slot shipped in 1.29 (off by the 480 KB CRAFT partition) and the
    # _nodoom preset (off by ~1.9 MB). Same 0/1 conversion as the loop above.
    set(MPY_LAYOUT_FLAGS "")
    foreach(_mf NES P8 DOOM MPY SCUMM CRAFT ROGUE ELITE ROGUE9 MOTE MD PCE)
        if (THUMBYONE_WITH_${_mf})
            list(APPEND MPY_LAYOUT_FLAGS -DTHUMBYONE_WITH_${_mf}=1)
        else()
            list(APPEND MPY_LAYOUT_FLAGS -DTHUMBYONE_WITH_${_mf}=0)
        endif()
    endforeach()
    ExternalProject_Add(mpy_slot
        SOURCE_DIR     ${MPY_SRC_DIR}
        BINARY_DIR     ${MPY_BUILD_DIR}
        CMAKE_ARGS
            -DMICROPY_BOARD=THUMBY_COLOR
            -DPICO_SDK_PATH_OVERRIDE=${PICO_SDK_PATH}
            -DUSER_C_MODULES=${MPY_USER_C_MODULES}
            -DTHUMBYONE_SLOT_MODE=mpy
            -DTHUMBYONE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DTHUMBYONE_FW_VERSION=${THUMBYONE_FW_VERSION}
            ${MPY_LAYOUT_FLAGS}
        BUILD_BYPRODUCTS ${MPY_UF2}
        INSTALL_COMMAND ""
        BUILD_ALWAYS 1
    )

    list(APPEND THUMBYONE_SLOT_UF2_INPUTS ${MPY_UF2})
    math(EXPR MPY_REBASE_DST "0x10000000 + ${THUMBYONE_MPY_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "firmware.uf2=0x10000000:${MPY_REBASE_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS mpy_slot)
endif()


# ----- SCUMM slot -----------------------------------------------------
# Rebases to flash offset 0x620000 (partition 4, "SCUMM", 768 KB).
# Default layout; shifts to 0x720000 when WITH_MD=ON (+1 MB for the
# enlarged NES slot).  Engine code only — game data lives in the
# shared FAT under /scumm/<game>/.
if (THUMBYONE_WITH_SCUMM)
    set(THUMBYONE_SLOT_MODE "scumm")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ThumbyScummby/device_pico
                     ${CMAKE_BINARY_DIR}/scumm_slot
                     EXCLUDE_FROM_ALL)
    unset(THUMBYONE_SLOT_MODE)

    # Slot mode builds one universal UF2 (no per-game suffix) — the
    # engine scans /scumm/* on the shared FAT at boot and picks a
    # supported game from engine/include/game_table.h.
    set(SCUMM_TARGET thumbyscummby)
    set(SCUMM_UF2_NAME firmware_thumbyscummby.uf2)
    list(APPEND THUMBYONE_SLOT_UF2_INPUTS
        ${CMAKE_BINARY_DIR}/scumm_slot/${SCUMM_UF2_NAME})
    math(EXPR SCUMM_REBASE_DST "0x10000000 + ${THUMBYONE_SCUMM_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "${SCUMM_UF2_NAME}=0x10000000:${SCUMM_REBASE_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS ${SCUMM_TARGET})
endif()


# ----- Mote OS slot (TWO partitions: runner + lobby) -------------------
# Replaces Craft + Cue + Indemnity. The LOBBY (the selectable tile) lists .mote
# games from /.mote/ on the shared FAT and chains the picked game's flash offset
# to the RUNNER, which is the engine with NO USB so the game gets maximum SRAM.
# Both are standalone-shaped images the combiner rebases to their offsets.
if (THUMBYONE_WITH_MOTE)
    # RUNNER — the engine, no USB.
    set(THUMBYONE_SLOT_MODE "mote")
    set(MOTE_SLOT_ROLE "runner")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../mote/os/device
                     ${CMAKE_BINARY_DIR}/mote_runner_slot EXCLUDE_FROM_ALL)
    unset(MOTE_SLOT_ROLE)
    list(APPEND THUMBYONE_SLOT_UF2_INPUTS
        ${CMAKE_BINARY_DIR}/mote_runner_slot/firmware_mote_runner.uf2)
    math(EXPR MOTE_RUN_DST "0x10000000 + ${THUMBYONE_MOTE_RUNNER_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "firmware_mote_runner.uf2=0x10000000:${MOTE_RUN_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS mote_runner)

    # LOBBY — small launcher + FatFs (USB is Phase 2).
    set(MOTE_SLOT_ROLE "lobby")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../mote/os/device
                     ${CMAKE_BINARY_DIR}/mote_lobby_slot EXCLUDE_FROM_ALL)
    unset(MOTE_SLOT_ROLE)
    unset(THUMBYONE_SLOT_MODE)
    list(APPEND THUMBYONE_SLOT_UF2_INPUTS
        ${CMAKE_BINARY_DIR}/mote_lobby_slot/firmware_mote_lobby.uf2)
    math(EXPR MOTE_LOB_DST "0x10000000 + ${THUMBYONE_MOTE_LOBBY_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "firmware_mote_lobby.uf2=0x10000000:${MOTE_LOB_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS mote_lobby)
endif()


# ----- ThumbyCraft slot ----------------------------------------------
# Per-world chunk + save data lives in the shared FAT under
# /thumbycraft/. Slot binary is just the engine + title screen.
if (THUMBYONE_WITH_CRAFT)
    set(THUMBYONE_SLOT_MODE "craft")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ThumbyCraft/device
                     ${CMAKE_BINARY_DIR}/craft_slot
                     EXCLUDE_FROM_ALL)
    unset(THUMBYONE_SLOT_MODE)

    set(CRAFT_TARGET thumbycraft)
    set(CRAFT_UF2_NAME firmware_thumbycraft.uf2)
    list(APPEND THUMBYONE_SLOT_UF2_INPUTS
        ${CMAKE_BINARY_DIR}/craft_slot/${CRAFT_UF2_NAME})
    math(EXPR CRAFT_REBASE_DST "0x10000000 + ${THUMBYONE_CRAFT_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "${CRAFT_UF2_NAME}=0x10000000:${CRAFT_REBASE_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS ${CRAFT_TARGET})
endif()


# ----- ThumbyCue slot (NON-STANDARD — occupies the former ThumbyRogue partition) ----
# Accurate 3D snooker & pool on the vendored ThumbyElite renderer. This is a
# CUSTOM build only: in the standard build Cue ships as a Mote game in /mote/,
# not as a slot. Enabling THUMBYONE_WITH_ROGUE brings it back as a slot.
# Kept at the ROGUE offset/size so the FAT position is unchanged (installs
# over an existing flash without disturbing the user's files).
if (THUMBYONE_WITH_ROGUE)
    set(THUMBYONE_SLOT_MODE "cue")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ThumbyCue/device
                     ${CMAKE_BINARY_DIR}/rogue_slot
                     EXCLUDE_FROM_ALL)
    unset(THUMBYONE_SLOT_MODE)

    set(ROGUE_TARGET thumbycue)
    set(ROGUE_UF2_NAME firmware_thumbycue.uf2)
    list(APPEND THUMBYONE_SLOT_UF2_INPUTS
        ${CMAKE_BINARY_DIR}/rogue_slot/${ROGUE_UF2_NAME})
    math(EXPR ROGUE_REBASE_DST "0x10000000 + ${THUMBYONE_ROGUE_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "${ROGUE_UF2_NAME}=0x10000000:${ROGUE_REBASE_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS ${ROGUE_TARGET})
endif()


# ----- ThumbyElite slot ------------------------------------------------
# Bare-metal Elite/MechWarrior space sim. Save data (the dock
# checkpoint) lives in the shared FAT under /thumbyelite/.
if (THUMBYONE_WITH_ELITE)
    set(THUMBYONE_SLOT_MODE "elite")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ThumbyElite/device
                     ${CMAKE_BINARY_DIR}/elite_slot
                     EXCLUDE_FROM_ALL)
    unset(THUMBYONE_SLOT_MODE)

    set(ELITE_TARGET thumbyelite)
    set(ELITE_UF2_NAME firmware_thumbyelite.uf2)
    list(APPEND THUMBYONE_SLOT_UF2_INPUTS
        ${CMAKE_BINARY_DIR}/elite_slot/${ELITE_UF2_NAME})
    math(EXPR ELITE_REBASE_DST "0x10000000 + ${THUMBYONE_ELITE_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "${ELITE_UF2_NAME}=0x10000000:${ELITE_REBASE_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS ${ELITE_TARGET})
endif()


# ----- ThumbyRogue 9th slot (optional, custom build only) ------------
# Only relevant to a custom Cue-slot + Elite-slot variant: when both
# THUMBYONE_WITH_ROGUE and _WITH_ELITE are on, enabling THUMBYONE_WITH_ROGUE9
# appends ThumbyRogue after Elite so all ship. Off by default and irrelevant
# to the standard build — shifts the P8 scratch + FAT for that variant.
if (THUMBYONE_WITH_ROGUE9)
    set(THUMBYONE_SLOT_MODE "rogue")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ThumbyRogue/device
                     ${CMAKE_BINARY_DIR}/rogue9_slot
                     EXCLUDE_FROM_ALL)
    unset(THUMBYONE_SLOT_MODE)

    set(ROGUE9_TARGET thumbyrogue)
    set(ROGUE9_UF2_NAME firmware_thumbyrogue.uf2)
    list(APPEND THUMBYONE_SLOT_UF2_INPUTS
        ${CMAKE_BINARY_DIR}/rogue9_slot/${ROGUE9_UF2_NAME})
    math(EXPR ROGUE9_REBASE_DST "0x10000000 + ${THUMBYONE_ROGUE9_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "${ROGUE9_UF2_NAME}=0x10000000:${ROGUE9_REBASE_DST}")
    list(APPEND THUMBYONE_SLOT_DEPS ${ROGUE9_TARGET})
endif()


# ----- Combine everything into one flash-once UF2 ---------------------
# Build a list of --rebase args once so the custom command is
# readable even as we add P8/DOOM/MPY slots later.
set(THUMBYONE_REBASE_ARGS "")
foreach(spec IN LISTS THUMBYONE_SLOT_UF2_REBASES)
    list(APPEND THUMBYONE_REBASE_ARGS --rebase ${spec})
endforeach()

# Combine into one flash-once UF2. Runs on every build so
# ExternalProject byproducts (mp-thumby's firmware.uf2) don't
# get missed — CMake doesn't reliably track file-level timestamp
# changes across ExternalProject_Add boundaries, so a naive
# OUTPUT-based custom_command would leave thumbyone.uf2 stale
# when only mpy_slot rebuilt. An ALL custom_target with no
# output file always runs, and the Python combiner is cheap.
# GBA-dedicated builds output a distinct UF2 so the standard combined
# firmware (thumbyone.uf2 / firmware_thumbyone.uf2) is never silently
# replaced. The user copies whichever they want to flash.
if (THUMBYONE_WITH_GBA)
    set(THUMBYONE_COMBINED_UF2 ${CMAKE_CURRENT_BINARY_DIR}/thumbyone_gba.uf2)
else()
    set(THUMBYONE_COMBINED_UF2 ${CMAKE_CURRENT_BINARY_DIR}/thumbyone.uf2)
endif()

add_custom_target(thumbyone ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Combining ThumbyOne UF2s..."
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/combine_uf2.py
                ${THUMBYONE_REBASE_ARGS}
                -o ${THUMBYONE_COMBINED_UF2}
                ${THUMBYONE_SLOT_UF2_INPUTS}
    DEPENDS ${THUMBYONE_SLOT_DEPS}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/combine_uf2.py
    BYPRODUCTS ${THUMBYONE_COMBINED_UF2}
    VERBATIM
)
