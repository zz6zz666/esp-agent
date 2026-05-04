/*
 * tray_icon.h — Windows system tray icon for esp-agent
 */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize system tray icon (creates hidden window, adds tray icon).
 *  Returns true on success.  Must be called on the main thread. */
bool tray_icon_init(void);

/** Show the display window (restore from tray). */
void tray_icon_show_window(void);

/** Hide the display window to the system tray. */
void tray_icon_hide_window(void);

/** Check whether the display window is currently visible. */
bool tray_icon_is_window_visible(void);

/** Poll tray icon messages.  Call from the main event loop
 *  (needed because the tray window runs on the same thread). */
void tray_icon_pump(void);

/** Remove the tray icon and destroy the helper window. */
void tray_icon_cleanup(void);

/** Returns true if the user requested a full quit via the tray menu. */
bool tray_icon_quit_requested(void);

/** Set the SDL2 window HWND for show/hide control. */
void tray_icon_set_sdl_window(void *hwnd);

#ifdef __cplusplus
}
#endif
