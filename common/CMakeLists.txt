#
# ThumbyOne common code.
#
# Exports:
#   THUMBYONE_COMMON_SRC      — list of .c sources to add to any
#                                slot/lobby target that needs the
#                                handoff API.
#   THUMBYONE_COMMON_INCLUDE  — include path for the public headers.
#   THUMBYONE_PT_JSON         — canonical partition-table JSON path.
#
# Consumers add the sources to their own add_executable(), add the
# include dir, and that's it. We deliberately DON'T wrap this as a
# STATIC/OBJECT/INTERFACE library: every target (NES / P8 / DOOM /
# MPY / lobby) already links pico_stdlib directly, and any CMake
# library wrapper on top of pico_stdlib ends up either absorbing
# the SDK's transitive sources (STATIC / OBJECT with target_link
# of pico_stdlib) or needing extra include-propagation plumbing.
# Having every consumer compile the handoff source itself is the
# straightforward, dedup-free approach.
#

set(THUMBYONE_COMMON_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/thumbyone_handoff.c
    PARENT_SCOPE)

set(THUMBYONE_COMMON_INCLUDE
    ${CMAKE_CURRENT_SOURCE_DIR}
    PARENT_SCOPE)

set(THUMBYONE_PT_JSON
    ${CMAKE_CURRENT_SOURCE_DIR}/pt.json
    PARENT_SCOPE)
