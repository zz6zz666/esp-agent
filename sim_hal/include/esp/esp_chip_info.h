/*
 * ESP-IDF esp_chip_info.h stub for desktop simulator
 *
 * Reads real CPU info.
 * On Windows: GetSystemInfo + registry
 * On POSIX:   /proc/cpuinfo + sysconf()
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t model;
    uint8_t cores;
    uint8_t revision;
} esp_chip_info_t;

/* Global — filled on first call to esp_chip_info() */
static char _chip_model_name[64] = {0};
static long _chip_cores = 0;

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>

static inline void _chip_init_once(void)
{
    if (_chip_cores > 0) return;

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    _chip_cores = si.dwNumberOfProcessors;
    if (_chip_cores <= 0) _chip_cores = 8;

    /* Read processor name from registry */
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type, size = sizeof(_chip_model_name);
        if (RegQueryValueExA(hKey, "ProcessorNameString",
                NULL, &type, (BYTE *)_chip_model_name, &size) == ERROR_SUCCESS
                && type == REG_SZ) {
            /* Trim trailing spaces / newlines */
            size_t len = strlen(_chip_model_name);
            while (len > 0 && (_chip_model_name[len-1] == ' '
                            || _chip_model_name[len-1] == '\n'
                            || _chip_model_name[len-1] == '\r'))
                _chip_model_name[--len] = '\0';
        }
        RegCloseKey(hKey);
    }
    if (!_chip_model_name[0]) {
        /* Fallback based on architecture */
        const char *arch;
        switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: arch = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86";    break;
        case PROCESSOR_ARCHITECTURE_ARM64: arch = "ARM64";  break;
        case PROCESSOR_ARCHITECTURE_ARM:   arch = "ARM";    break;
        default:                           arch = "Unknown"; break;
        }
        snprintf(_chip_model_name, sizeof(_chip_model_name),
                 "%s (%lu cores)", arch, (unsigned long)_chip_cores);
    }
}
#else
# include <unistd.h>

static inline void _chip_init_once(void)
{
    if (_chip_cores > 0) return; /* already done */

    _chip_cores = sysconf(_SC_NPROCESSORS_CONF);
    if (_chip_cores <= 0) _chip_cores = 8;

    /* Read model name from first processor entry in /proc/cpuinfo */
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char *sep = strchr(line, ':');
            if (sep && strncmp(line, "model name", 10) == 0) {
                const char *val = sep + 1;
                while (*val == ' ' || *val == '\t') val++;
                size_t len = strlen(val);
                while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
                    len--;
                if (len >= sizeof(_chip_model_name))
                    len = sizeof(_chip_model_name) - 1;
                memcpy(_chip_model_name, val, len);
                _chip_model_name[len] = '\0';
                break;
            }
        }
        fclose(f);
    }
    if (!_chip_model_name[0])
        snprintf(_chip_model_name, sizeof(_chip_model_name), "Unknown x86_64");
}
#endif

static inline void esp_chip_info(esp_chip_info_t *info) {
    _chip_init_once();
    static esp_chip_info_t fake = {0};
    if (fake.cores == 0) {
        fake.model = 0;
        fake.cores = (uint8_t)(_chip_cores > 255 ? 255 : _chip_cores);
        fake.revision = 0;
    }
    if (info) *info = fake;
}

/* Expose the model name string — used by cap_system via CONFIG_IDF_TARGET */
static inline const char* _get_chip_model(void) {
    _chip_init_once();
    return _chip_model_name;
}

/* Override the CMake-set CONFIG_IDF_TARGET to use the real host CPU model name.
 * This only affects translation units that include this header (cap_system.c). */
#ifdef CONFIG_IDF_TARGET
#undef CONFIG_IDF_TARGET
#endif
#define CONFIG_IDF_TARGET _get_chip_model()

#ifdef __cplusplus
}
#endif
