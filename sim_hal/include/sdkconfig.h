/*
 * sdkconfig.h stub — replaces ESP-IDF Kconfig output on desktop.
 * Most CONFIG_* macros are injected via CMake compile definitions.
 */
#pragma once

#define CONFIG_IDF_TARGET "linux-x86_64"
#define CONFIG_IDF_TARGET_LINUX_X86_64 1
#define CONFIG_EMOTE_ASSETS_HASH_TABLE_SIZE  64
#define CONFIG_EMOTE_DEF_SCROLL_SPEED        2
#define CONFIG_EMOTE_DEF_LABEL_HEIGHT        24
#define CONFIG_EMOTE_DEF_LABEL_WIDTH         200
#define CONFIG_EMOTE_DEF_LABEL_Y_OFFSET      180
#define CONFIG_EMOTE_DEF_ANIMATION_FPS       10
#define CONFIG_EMOTE_DEF_FONT_COLOR          0xFFFF
#define CONFIG_EMOTE_DEF_BG_COLOR            0x0000
#define CONFIG_SPIRAM_XIP_FROM_PSRAM         0
#define CONFIG_MMAP_FILE_NAME_LENGTH         64
