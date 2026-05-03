/*
 * soc_memory_layout.h — stub for desktop simulator
 *
 * On a desktop, all memory is the same.  These constants are used by
 * emote_load.c to check if data resides in flash or DRAM — always false.
 */
#pragma once

#define SOC_MMU_FLASH_VADDR_BASE  0x00000000UL
#define SOC_MMU_DBUS_VADDR_BASE   0x00000000UL
