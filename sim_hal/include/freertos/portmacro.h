/*
 * FreeRTOS portmacro.h stub for desktop simulator
 *
 * Provides portNUM_PROCESSORS from the real host CPU count.
 */
#pragma once

#define portTICK_PERIOD_MS 1

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
  static inline UBaseType_t _port_get_num_processors(void) {
      SYSTEM_INFO si;
      GetSystemInfo(&si);
      return (UBaseType_t)si.dwNumberOfProcessors;
  }
# define portNUM_PROCESSORS (_port_get_num_processors())
#else
# include <unistd.h>
# define portNUM_PROCESSORS ((UBaseType_t)sysconf(_SC_NPROCESSORS_CONF))
#endif
