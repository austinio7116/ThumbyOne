#
# ThumbyOne common library.
#
# Add this subdirectory from a parent CMakeLists that has already
# set up the Pico SDK (pico_sdk_init called). Link any target
# against `thumbyone_common` to get:
#
#   - The ThumbyOne slot layout constants (slot_layout.h).
#   - The cross-reboot handoff API (thumbyone_handoff.[ch]).
#
# This library does not embed the partition table — that's the
# lobby's job via pico_embed_pt_in_binary(lobby ${THUMBYONE_PT_JSON}).
# Slot binaries don't embed the PT; they just need to know slot
# offsets (from slot_layout.h) and use the handoff API.
#

add_library(thumbyone_common STATIC
    thumbyone_handoff.c
)

target_include_directories(thumbyone_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(thumbyone_common PUBLIC
    pico_stdlib
    pico_bootrom
    hardware_watchdog
)

# Path to the canonical partition-table JSON. Parent projects
# pass this to pico_embed_pt_in_binary on their lobby target.
set(THUMBYONE_PT_JSON ${CMAKE_CURRENT_SOURCE_DIR}/pt.json
    PARENT_SCOPE)
