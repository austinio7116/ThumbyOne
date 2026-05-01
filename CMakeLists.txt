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
set(THUMBYONE_FW_VERSION "1.12")
add_compile_definitions(THUMBYONE_FW_VERSION="${THUMBYONE_FW_VERSION}")

# ----- Slot selection --------------------------------------------------
# (Only NES is wired in step C1. P8 / DOOM / MPY follow once their
# subproject CMake hooks are in place.)
option(THUMBYONE_WITH_NES  "Include ThumbyNES firmware"  ON)
option(THUMBYONE_WITH_P8   "Include ThumbyP8 firmware"   ON)
option(THUMBYONE_WITH_DOOM "Include ThumbyDOOM firmware" ON)
option(THUMBYONE_WITH_MPY  "Include MicroPython firmware" ON)
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
)
pico_embed_pt_in_binary(lobby ${THUMBYONE_PT_JSON})
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
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "${NES_TARGET}.uf2=0x10000000:0x10020000")
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
    # P8 base addr: 0x10120000 default (after 1 MB NES partition),
    # 0x10220000 when WITH_MD=ON (after 2 MB NES partition).
    if (THUMBYONE_WITH_MD)
        list(APPEND THUMBYONE_SLOT_UF2_REBASES
            "${P8_TARGET}.uf2=0x10000000:0x10220000")
    else()
        list(APPEND THUMBYONE_SLOT_UF2_REBASES
            "${P8_TARGET}.uf2=0x10000000:0x10120000")
    endif()
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
    # DOOM base addr shifts +0x100000 when WITH_MD=ON.
    if (THUMBYONE_WITH_MD)
        list(APPEND THUMBYONE_SLOT_UF2_REBASES
            "${DOOM_TARGET}.uf2=0x10000000:0x102A0000")
    else()
        list(APPEND THUMBYONE_SLOT_UF2_REBASES
            "${DOOM_TARGET}.uf2=0x10000000:0x101A0000")
    endif()
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
    # Propagate THUMBYONE_WITH_MD so mpconfigboard.h picks the
     # matching FAT base (shifted +1 MB when MD grows the NES slot).
    if (THUMBYONE_WITH_MD)
        set(MPY_WITH_MD_FLAG -DTHUMBYONE_WITH_MD=1)
    else()
        set(MPY_WITH_MD_FLAG -DTHUMBYONE_WITH_MD=0)
    endif()
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
            ${MPY_WITH_MD_FLAG}
        BUILD_BYPRODUCTS ${MPY_UF2}
        INSTALL_COMMAND ""
        BUILD_ALWAYS 1
    )

    list(APPEND THUMBYONE_SLOT_UF2_INPUTS ${MPY_UF2})
    # MPY base addr shifts +0x100000 when WITH_MD=ON.
    if (THUMBYONE_WITH_MD)
        list(APPEND THUMBYONE_SLOT_UF2_REBASES
            "firmware.uf2=0x10000000:0x10520000")
    else()
        list(APPEND THUMBYONE_SLOT_UF2_REBASES
            "firmware.uf2=0x10000000:0x10420000")
    endif()
    list(APPEND THUMBYONE_SLOT_DEPS mpy_slot)
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
add_custom_target(thumbyone ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Combining ThumbyOne UF2s..."
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/combine_uf2.py
                ${THUMBYONE_REBASE_ARGS}
                -o ${CMAKE_CURRENT_BINARY_DIR}/thumbyone.uf2
                ${THUMBYONE_SLOT_UF2_INPUTS}
    DEPENDS ${THUMBYONE_SLOT_DEPS}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/combine_uf2.py
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/thumbyone.uf2
    VERBATIM
)
