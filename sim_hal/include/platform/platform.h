/*
 * platform.h — Cross-platform abstraction layer
 *
 * Detects the target platform and includes the appropriate implementation.
 * Provides uniform APIs for threads, filesystem paths, timing, etc.
 *
 * Platform detection hierarchy:
 *   PLATFORM_WINDOWS  → use platform_win32.h
 *   PLATFORM_LINUX    → use platform_posix.h
 */

#pragma once

#if defined(PLATFORM_WINDOWS)
# include "platform_win32.h"
#else
# include "platform_posix.h"
#endif

/* ---- Common cross-platform wrappers ---- */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolve the data directory for Crush Claw.
 * On POSIX:   $HOME/.crush-claw/  (or $CRUSH_CLAW_DATA_DIR)
 * On Windows: %USERPROFILE%\.crush-claw\  (or %CRUSH_CLAW_DATA_DIR%)
 *
 * Caller provides a buffer of at least PLATFORM_MAX_PATH bytes.
 */
void platform_get_data_dir(char *buf, size_t size);

/*
 * Create a directory and all parent directories (like "mkdir -p").
 * Does NOT fail if the directory already exists.
 * Returns 0 on success, -1 on error.
 */
int platform_mkdir_p(const char *path);

/*
 * Resolve a path to its absolute form.
 * Writes result into resolved (size bytes).
 * Returns 0 on success, -1 on error.
 */
int platform_realpath(const char *path, char *resolved, size_t size);

/*
 * Recursively copy a directory tree from src to dst.
 * Returns 0 on success, -1 on error.
 */
int platform_copy_tree(const char *src, const char *dst);

/*
 * Get the current process ID (portable across platforms).
 */
int platform_get_pid(void);

/*
 * Write a PID file.
 * Returns 0 on success.
 */
int platform_write_pid_file(const char *path);

#ifdef __cplusplus
}
#endif
