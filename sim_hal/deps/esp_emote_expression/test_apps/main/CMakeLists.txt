idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    WHOLE_ARCHIVE
)

if(${IDF_TARGET} STREQUAL "esp32s3")
    set(RESOLUTION "320_240")
elseif(${IDF_TARGET} STREQUAL "esp32p4")
    set(RESOLUTION "1024_600")
else()
    message(STATUS "Unknown target, using default")
    set(RESOLUTION "320_240")
endif()

message(STATUS "Resolution: ${RESOLUTION}")
message(STATUS "IDF_TARGET: ${IDF_TARGET}")

set(ASSETS_NAME "${IDF_TARGET}_assets.bin")
set(ASSETS_FILE "${CMAKE_BINARY_DIR}/${ASSETS_NAME}.bin")
build_speaker_assets_bin("anim_icon" ${RESOLUTION} ${ASSETS_FILE} ${CONFIG_MMAP_FILE_NAME_LENGTH})
message(STATUS "Generated emote assets: ${ASSETS_FILE} -> anim_icon partition")
esptool_py_flash_to_partition(flash "anim_icon" "${ASSETS_FILE}")

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
