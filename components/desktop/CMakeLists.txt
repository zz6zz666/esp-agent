# Desktop components — Linux simulator extensions
#
# These are compiled ONLY in SIMULATOR_BUILD mode (never on ESP32).
# On ESP32, this entire directory is excluded from the build.
#
# To add a new desktop component:
#   file(GLOB_RECURSE desktop_srcs src/*.c)
#   target_sources(esp-claw-desktop PRIVATE ${desktop_srcs})

if(NOT SIMULATOR_BUILD)
    message(FATAL_ERROR "components/desktop/ must only be built with SIMULATOR_BUILD")
endif()

# Include paths for desktop component headers
target_include_directories(esp-claw-desktop PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Future desktop component sources go here:
# file(GLOB_RECURSE desktop_srcs src/*.c)
# target_sources(esp-claw-desktop PRIVATE ${desktop_srcs})
