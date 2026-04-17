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
#         [-DTHUMBYONE_WITH_NES=ON] \
#         [-DTHUMBYONE_WITH_P8=OFF]   (not yet wired)
#         [-DTHUMBYONE_WITH_DOOM=OFF] (not yet wired)
#         [-DTHUMBYONE_WITH_MPY=OFF]  (not yet wired)
#   cmake --build build_device -j8
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

# ----- Slot selection --------------------------------------------------
# (Only NES is wired in step C1. P8 / DOOM / MPY follow once their
# subproject CMake hooks are in place.)
option(THUMBYONE_WITH_NES  "Include ThumbyNES firmware"  ON)
option(THUMBYONE_WITH_P8   "Include ThumbyP8 firmware"   ON)
option(THUMBYONE_WITH_DOOM "Include ThumbyDOOM firmware" OFF)
option(THUMBYONE_WITH_MPY  "Include MicroPython firmware" OFF)

# ----- Shared library --------------------------------------------------
# Brings in the handoff API + slot_layout.h + THUMBYONE_PT_JSON.
add_subdirectory(common)


# ----- Lobby ----------------------------------------------------------
# Flash at 0x10000000, absolute family UF2, partition table
# embedded.
add_executable(lobby
    lobby/lobby_main.c
    lobby/lcd_gc9107.c
    lobby/font.c
)
target_include_directories(lobby PRIVATE lobby)
target_link_libraries(lobby PRIVATE
    thumbyone_common
    hardware_gpio
    hardware_spi
    hardware_dma
    hardware_flash
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
    list(APPEND THUMBYONE_SLOT_UF2_REBASES
        "${P8_TARGET}.uf2=0x10000000:0x10120000")
    list(APPEND THUMBYONE_SLOT_DEPS ${P8_TARGET})
endif()


# ----- Combine everything into one flash-once UF2 ---------------------
# Build a list of --rebase args once so the custom command is
# readable even as we add P8/DOOM/MPY slots later.
set(THUMBYONE_REBASE_ARGS "")
foreach(spec IN LISTS THUMBYONE_SLOT_UF2_REBASES)
    list(APPEND THUMBYONE_REBASE_ARGS --rebase ${spec})
endforeach()

add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/thumbyone.uf2
    COMMAND ${CMAKE_COMMAND} -E echo "Combining ThumbyOne UF2s..."
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/combine_uf2.py
                ${THUMBYONE_REBASE_ARGS}
                -o ${CMAKE_CURRENT_BINARY_DIR}/thumbyone.uf2
                ${THUMBYONE_SLOT_UF2_INPUTS}
    DEPENDS ${THUMBYONE_SLOT_DEPS}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/combine_uf2.py
    VERBATIM
)
add_custom_target(thumbyone ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/thumbyone.uf2
)
