/*
 * display_hal_input.h — Simulator-only input HAL extensions
 *
 * Declares input types and functions implemented by display_sdl2.c.
 * These are NOT part of the upstream esp-claw display_hal.h — they are
 * desktop-simulator extensions gated behind SIMULATOR_BUILD.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Input event types (queued for discrete events) ---- */

typedef enum {
    INPUT_EVENT_KEY_DOWN = 0,
    INPUT_EVENT_KEY_UP,
    INPUT_EVENT_TEXT,          /* UTF-8 text input */
    INPUT_EVENT_MOUSE_DOWN,
    INPUT_EVENT_MOUSE_UP,
    INPUT_EVENT_MOUSE_WHEEL,
    INPUT_EVENT_WINDOW_RESIZED,
} input_event_type_t;

#define INPUT_EVENT_QUEUE_SIZE 64
#define INPUT_TEXT_MAX 8

typedef struct {
    input_event_type_t type;
    int16_t x, y;              /* mouse position at event (logical coords) */
    uint8_t button;            /* SDL_BUTTON_LEFT/MIDDLE/RIGHT */
    int32_t key;               /* SDL scancode */
    uint16_t mod;              /* modifier mask */
    char text[INPUT_TEXT_MAX]; /* UTF-8 text (for INPUT_EVENT_TEXT) */
} input_event_t;

/* ---- API ---- */

/* Pump SDL events without rendering.  Call from main loop when display is
 * idle but input events still need processing. */
esp_err_t display_hal_poll_input(void);

/* Get latest mouse state (non-blocking, always returns current position). */
esp_err_t display_hal_get_mouse_state(int16_t *out_x, int16_t *out_y,
                                       bool *out_left, bool *out_middle,
                                       bool *out_right, int *out_wheel);

/* Check if a key is currently held down (scancode = SDL_SCANCODE_*). */
bool display_hal_is_key_down(int32_t scancode);

/* Active modifier mask (SDL_Keymod bits). */
uint16_t display_hal_get_modifiers(void);

/* Pop one input event from the queue (non-blocking).
 * Returns true if an event was dequeued. */
bool display_hal_pop_input_event(input_event_t *out_event);

#ifdef __cplusplus
}
#endif
