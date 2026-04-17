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

set(THUMBYONE_PT_JSON
    ${CMAKE_CURRENT_SOURCE_DIR}/pt.json
    PARENT_SCOPE)
