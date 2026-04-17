#
# ThumbyOne flash-layout spike.
#
# Builds two instances of the same "blinky" image linked at
# different flash offsets. Each instance calls rom_chain_image()
# on button A to chain into the other slot.
#
# Output:
#   build_spike/blinky_slot0.uf2   (linked at 0x10000000, 128 KB)
#   build_spike/blinky_slot1.uf2   (linked at 0x10020000, 1 MB)
#   build_spike/spike_combined.uf2 (concatenated, flash once)
#

cmake_minimum_required(VERSION 3.16)

# ----- Pico SDK setup --------------------------------------------------
set(PICO_BOARD    tinycircuits_thumby_color_rp2350)
set(PICO_PLATFORM rp2350)
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "/home/maustin/mp-thumby/lib/pico-sdk")
endif()
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(thumbyone_spike C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

# ----- Helper: build one slot target -----------------------------------
# Generates a slot-specific memmap.ld from the template, declares the
# executable, and sets PICO_TARGET_LINKER_SCRIPT to the generated
# script so the SDK links at the slot's flash origin.
function(thumbyone_add_slot TARGET SLOT_ID FLASH_ORIGIN FLASH_LENGTH)
    # Configure the memmap
    set(SLOT_FLASH_ORIGIN ${FLASH_ORIGIN})
    set(SLOT_FLASH_LENGTH ${FLASH_LENGTH})
    set(SLOT_LD_OUT ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_memmap.ld)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/linker/slot_memmap.ld.in
        ${SLOT_LD_OUT}
        @ONLY
    )

    add_executable(${TARGET} blinky_slot.c)
    target_compile_definitions(${TARGET} PRIVATE SPIKE_SLOT_ID=${SLOT_ID})
    target_link_libraries(${TARGET} PRIVATE
        pico_stdlib
        pico_bootrom
        hardware_watchdog
        hardware_gpio
    )
    pico_set_linker_script(${TARGET} ${SLOT_LD_OUT})

    pico_add_extra_outputs(${TARGET})
endfunction()

# ----- Slot 0: 128 KB at 0x10000000 ------------------------------------
thumbyone_add_slot(blinky_slot0 0 "0x10000000" "128 * 1024")

# ----- Slot 1: 1 MB at 0x10020000 --------------------------------------
thumbyone_add_slot(blinky_slot1 1 "0x10020000" "1 * 1024 * 1024")

# ----- Combined UF2 (flash once) ---------------------------------------
add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/spike_combined.uf2
    COMMAND ${CMAKE_COMMAND} -E echo "Combining slot 0 + slot 1 UF2s..."
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/combine_uf2.py
                -o ${CMAKE_CURRENT_BINARY_DIR}/spike_combined.uf2
                ${CMAKE_CURRENT_BINARY_DIR}/blinky_slot0.uf2
                ${CMAKE_CURRENT_BINARY_DIR}/blinky_slot1.uf2
    DEPENDS blinky_slot0 blinky_slot1
            ${CMAKE_CURRENT_SOURCE_DIR}/../tools/combine_uf2.py
    VERBATIM
)
add_custom_target(spike_combined ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/spike_combined.uf2
)
