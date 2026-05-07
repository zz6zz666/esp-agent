/*
 * tray_icon.h — Windows system tray icon for Crush Claw
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

/** Check if "Always Hide Windows" is enabled in tray menu. */
bool tray_always_hide_is_enabled(void);

/** Toggle the "Always Hide Windows" tray setting. */
void tray_always_hide_toggle(void);

/** Check if "Check for Updates on Startup" is enabled. */
bool tray_auto_update_is_enabled(void);

/** Set the "Check for Updates on Startup" registry setting. */
void tray_auto_update_set_enabled(bool enable);

/** Perform an update check against GitHub releases API.
 *  Compares latest tag with current_version.
 *  If a newer version is found, shows a popup dialog.
 *  Safe to call from main thread after tray_icon_init(). */
void tray_icon_perform_update_check(const char *current_version);

#ifdef __cplusplus
}
#endif
