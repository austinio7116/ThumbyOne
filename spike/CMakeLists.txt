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


# ----- Slot 0: lobby at 0x10000000 with embedded partition table ------
add_executable(lobby
    blinky_slot.c
    lcd_gc9107.c
    font.c
)
target_compile_definitions(lobby PRIVATE SPIKE_SLOT_ID=0)
target_link_libraries(lobby PRIVATE
    pico_stdlib
    pico_bootrom
    hardware_watchdog
    hardware_gpio
    hardware_spi
    hardware_dma
    hardware_flash
)

# Default linker script → flash at 0x10000000 with full 16 MB region.
# (The lobby lives in unpartitioned space, which the PT defines as
# "absolute" family compatible. It's free to use anywhere not
# covered by declared partitions.)

# Embed the partition table into the lobby binary. picotool runs
# after link to inject the PT block(s).
pico_embed_pt_in_binary(lobby ${CMAKE_CURRENT_SOURCE_DIR}/pt.json)

# Mark as "absolute" family — bootrom treats this as a
# direct-address boot image, not something to place into a partition.
pico_set_uf2_family(lobby "absolute")

pico_add_extra_outputs(lobby)


# ----- Slot 1: app ---------------------------------------------------
# Linked at default 0x10000000 flash origin. When chained via
# rom_chain_image, bootrom sets up QMI ATRANS to remap the XIP
# window so the image's absolute addresses work — so the image
# MUST be linked at 0x10000000 logically, not at its physical
# partition offset.
# The combiner below rebases the app UF2's target addresses from
# 0x10000000 to 0x10020000 so the bytes end up in partition 0
# physically at flash offset 0x20000.
add_executable(app
    blinky_slot.c
)
target_compile_definitions(app PRIVATE SPIKE_SLOT_ID=1)
target_link_libraries(app PRIVATE
    pico_stdlib
    pico_bootrom
    hardware_watchdog
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
