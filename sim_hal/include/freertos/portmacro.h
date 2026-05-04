/*
 * FreeRTOS portmacro.h stub for desktop simulator
 */
#pragma once

#define portTICK_PERIOD_MS 1
#include <unistd.h>
#define portNUM_PROCESSORS   ((UBaseType_t)sysconf(_SC_NPROCESSORS_CONF))
