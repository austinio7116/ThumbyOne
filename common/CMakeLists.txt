#
# ThumbyOne common code.
#
# Exports (as PARENT_SCOPE variables — consumers add to their own
# add_executable/target_sources rather than linking a library here;
# see the big comment block below for why):
#
#   THUMBYONE_COMMON_SRC      — handoff API + QMI/ATRANS setup.
#                                Every slot and the lobby needs this.
#   THUMBYONE_COMMON_INCLUDE  — include path for the handoff header
#                                and slot_layout.h.
#   THUMBYONE_COMMON_FS_SRC   — shared flash-disk + FatFs glue +
#                                lobby-owned mount/format API.
#                                For targets that mount the shared
#                                FAT themselves (lobby, NES, P8).
#                                NOT for DOOM (doesn't touch FAT)
#                                or MPY (has its own diskio through
#                                the MicroPython bdev layer).
#   THUMBYONE_FATFS_SRC       — upstream FatFs R0.15 sources
#                                (ff.c + ffunicode.c + ffsystem.c).
#                                Paired with THUMBYONE_COMMON_FS_SRC.
#   THUMBYONE_FATFS_INCLUDE   — include path for ff.h / diskio.h /
#                                ffconf.h.
#   THUMBYONE_PICKER_SRC      — LCD + font + picker UI, shared between
#                                the lobby and the MPY-slot C picker.
#                                LCD is the GC9107 via SPI+DMA; font
#                                is a bitmapped 6x8 with 2x variant.
#   THUMBYONE_PICKER_INCLUDE  — include path for lcd_gc9107.h + font.h.
#   THUMBYONE_PT_JSON         — canonical partition-table JSON path.
#
# No CMake library wrapping: every target already links pico_stdlib
# directly, and wrapping this code in a STATIC/OBJECT library either
# absorbs pico_stdlib's transitive sources or needs extra propagation
# plumbing. Having every consumer compile the handoff source itself
# is the simple, dedup-free answer.
#

# Common handoff — QMI/ATRANS restore, scratch-based slot handoff.
set(THUMBYONE_COMMON_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/thumbyone_handoff.c
    PARENT_SCOPE)

set(THUMBYONE_COMMON_INCLUDE
    ${CMAKE_CURRENT_SOURCE_DIR}
    PARENT_SCOPE)

# Shared-FAT flash-disk + FatFs glue + lobby-owned mount/format.
# Any target that adds this MUST also pick up THUMBYONE_FATFS_SRC
# (ff.c etc.) and add THUMBYONE_FATFS_INCLUDE to its include path.
set(THUMBYONE_COMMON_FS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/fs/thumbyone_disk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fs/thumbyone_diskio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fs/thumbyone_fs.c
    PARENT_SCOPE)

# Upstream FatFs R0.15 — symlinked from mp-thumby/lib/fatfs so
# there's one authoritative copy that the MPY port also compiles.
set(THUMBYONE_FATFS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/fatfs/ff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/fatfs/ffunicode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/fatfs/ffsystem.c
    PARENT_SCOPE)

set(THUMBYONE_FATFS_INCLUDE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/fatfs
    ${CMAKE_CURRENT_SOURCE_DIR}/fs
    PARENT_SCOPE)

# LCD (GC9107) + bitmap font. Shared between the ThumbyOne lobby
# and the MPY slot's C picker (both need a selector UI before any
# Python runtime exists). Consumers also need hardware_spi +
# hardware_dma + hardware_gpio linked in, same as the lobby has.
set(THUMBYONE_PICKER_UI_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/picker/lcd_gc9107.c
    ${CMAKE_CURRENT_SOURCE_DIR}/picker/font.c
    PARENT_SCOPE)

# The MPY-slot picker itself — adds picker.c on top of the UI bits.
# MPY slot wants THUMBYONE_PICKER_SRC, which is PICKER_UI_SRC + picker.c.
# The lobby uses PICKER_UI_SRC only (draws its own home screen).
set(THUMBYONE_PICKER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/picker/lcd_gc9107.c
    ${CMAKE_CURRENT_SOURCE_DIR}/picker/font.c
    ${CMAKE_CURRENT_SOURCE_DIR}/picker/picker_bmp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/picker/menu_watchdog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/picker/picker.c
    PARENT_SCOPE)

set(THUMBYONE_PICKER_INCLUDE
    ${CMAKE_CURRENT_SOURCE_DIR}/picker
    PARENT_SCOPE)

set(THUMBYONE_PT_JSON
    ${CMAKE_CURRENT_SOURCE_DIR}/pt.json
    PARENT_SCOPE)
