#
# ThumbyOne flash-layout spike v2 — RP2350 partition-table edition.
#
# Two slots linked at different flash offsets, with a RP2350
# partition table embedded in slot 0 so rom_chain_image has the
# metadata it needs to validate and transfer control.
#
# Slot 0 (lobby)  — flash at 0x10000000, "absolute" UF2 family,
#                    PT embedded via pico_embed_pt_in_binary.
# Slot 1 (app)    — flash at 0x10020000 (inside partition 0),
#                    "absolute" UF2 family for direct-address flash.
#
# Output:
#   build_spike/lobby.uf2   — slot 0 only (includes PT)
#   build_spike/app.uf2     — slot 1 only
#   build_spike/spike.uf2   — both, combined (flash-once)
#

cmake_minimum_required(VERSION 3.16)

set(PICO_BOARD    tinycircuits_thumby_color_rp2350)
set(PICO_PLATFORM rp2350)
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "/home/maustin/mp-thumby/lib/pico-sdk")
endif()
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(thumbyone_spike C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

# Common library (handoff API + slot layout constants).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common
                 ${CMAKE_BINARY_DIR}/common)


# ----- Slot 0: lobby at 0x10000000 with embedded partition table ------
add_executable(lobby
    blinky_slot.c
    lcd_gc9107.c
    font.c
)
target_compile_definitions(lobby PRIVATE SPIKE_SLOT_ID=0)
target_link_libraries(lobby PRIVATE
    thumbyone_common
    hardware_gpio
    hardware_spi
    hardware_dma
    hardware_flash
)

# Embed the real ThumbyOne partition table (from common/pt.json).
# This is the canonical PT used by the whole project; any slot
# linked against thumbyone_common will reference the same layout.
pico_embed_pt_in_binary(lobby ${THUMBYONE_PT_JSON})

# Mark as "absolute" family — bootrom treats this as a
# direct-address boot image, not something to place into a partition.
pico_set_uf2_family(lobby "absolute")

pico_add_extra_outputs(lobby)


# ----- Slot 1: app (test binary for the NES partition) ---------------
# Linked at default 0x10000000 flash origin; rom_chain_image uses
# QMI ATRANS to remap the XIP window to the partition's physical
# offset at launch, so the image must be linked at the default
# origin. The combiner rebases UF2 block addresses from
# 0x10000000 to 0x10020000 so the bytes land in partition 0 (NES).
#
# This target is a placeholder — once the real ThumbyNES firmware
# is wired up in step C, it replaces this app.
add_executable(app
    blinky_slot.c
)
target_compile_definitions(app PRIVATE SPIKE_SLOT_ID=1)
target_link_libraries(app PRIVATE
    thumbyone_common
    hardware_gpio
)
pico_set_uf2_family(app "absolute")
pico_add_extra_outputs(app)


# ----- Combined UF2 ---------------------------------------------------
add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/spike.uf2
    COMMAND ${CMAKE_COMMAND} -E echo "Combining lobby + app UF2s..."
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/combine_uf2.py
                --rebase "app.uf2=0x10000000:0x10020000"
                -o ${CMAKE_CURRENT_BINARY_DIR}/spike.uf2
                ${CMAKE_CURRENT_BINARY_DIR}/lobby.uf2
                ${CMAKE_CURRENT_BINARY_DIR}/app.uf2
    DEPENDS lobby app
            ${CMAKE_CURRENT_SOURCE_DIR}/../tools/combine_uf2.py
    VERBATIM
)
add_custom_target(spike ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/spike.uf2
)
