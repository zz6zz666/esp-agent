/*
 * platform_utils.c — Cross-platform utility implementations
 *
 * Non-inline functions from platform abstraction layer.
 * Separated from the inline-only headers to avoid duplication.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <shellapi.h>
# include <shlobj.h>
# include <direct.h>
#else
# include <sys/stat.h>
# include <sys/types.h>
# include <unistd.h>
# include <errno.h>
#endif

/* ---- platform_get_data_dir ---- */

void platform_get_data_dir(char *buf, size_t size)
{
    const char *env = getenv("CRUSH_CLAW_DATA_DIR");
    if (env && env[0]) {
        strncpy(buf, env, size - 1);
        buf[size - 1] = '\0';
        return;
    }
#if defined(PLATFORM_WINDOWS)
    const char *home = getenv("USERPROFILE");
    if (!home) {
        const char *drive = getenv("HOMEDRIVE");
        const char *path  = getenv("HOMEPATH");
        static char fallback[MAX_PATH];
        if (drive && path) {
            snprintf(fallback, MAX_PATH, "%s%s", drive, path);
            home = fallback;
        } else {
            home = "C:\\";
        }
    }
    snprintf(buf, size, "%s\\.crush-claw", home);
#else
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(buf, size, "%s/.crush-claw", home);
#endif
}

/* ---- platform_mkdir_p ---- */

int platform_mkdir_p(const char *path)
{
    char tmp[512];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;

    memcpy(tmp, path, len + 1);
    int rc = 0;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
#if defined(PLATFORM_WINDOWS)
            rc = _mkdir(tmp);
#else
            rc = mkdir(tmp, 0755);
#endif
            if (rc != 0 && errno != EEXIST) return rc;
            *p = saved;
        }
    }
    /* Create the final (leaf) directory */
#if defined(PLATFORM_WINDOWS)
    rc = _mkdir(tmp);
#else
    rc = mkdir(tmp, 0755);
#endif
    if (rc != 0 && errno != EEXIST) return rc;
    return 0;
}

/* ---- platform_realpath ---- */

int platform_realpath(const char *path, char *resolved, size_t size)
{
#if defined(PLATFORM_WINDOWS)
    DWORD len = GetFullPathNameA(path, (DWORD)size, resolved, NULL);
    return (len > 0 && len < (DWORD)size) ? 0 : -1;
#else
    return (realpath(path, resolved) != NULL) ? 0 : -1;
#endif
}

/* ---- platform_copy_tree ---- */

int platform_copy_tree(const char *src, const char *dst)
{
#if defined(PLATFORM_WINDOWS)
    /* Use SHFileOperationA for recursive copy */
    char src_buf[MAX_PATH * 2];
    char dst_buf[MAX_PATH * 2];
    snprintf(src_buf, sizeof(src_buf), "%s\\*", src);
    snprintf(dst_buf, sizeof(dst_buf), "%s\\", dst);

    /* SHFileOperation requires double-null-terminated strings */
    size_t src_len = strlen(src_buf);
    size_t dst_len = strlen(dst_buf);
    src_buf[src_len + 1] = '\0';
    dst_buf[dst_len + 1] = '\0';

    SHFILEOPSTRUCTA op = {0};
    op.wFunc  = FO_COPY;
    op.pFrom  = src_buf;
    op.pTo    = dst_buf;
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;

    int ret = SHFileOperationA(&op);
    return (ret == 0 && !op.fAnyOperationsAborted) ? 0 : -1;
#else
    pid_t pid = fork();
    if (pid == 0) {
        execlp("cp", "cp", "-rn", "--", src, dst, (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return 0;
    }
    return -1;
#endif
}

/* ---- PID helpers ---- */

int platform_get_pid(void)
{
#if defined(PLATFORM_WINDOWS)
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

int platform_write_pid_file(const char *path)
{
    if (!path || !path[0]) return -1;
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "%d\n", platform_get_pid());
    fclose(fp);
    return 0;
}

/* ---- strftime with ACP→UTF-8 conversion (Windows) ---- */
#if defined(PLATFORM_WINDOWS)
#include <wchar.h>

size_t _mingw_strftime_utf8(char *buf, size_t maxsize, const char *format, const struct tm *tm)
{
    char tmp[256];
    size_t n;
#ifdef strftime
# pragma push_macro("strftime")
# undef strftime
    n = strftime(tmp, sizeof(tmp), format, tm);
# pragma pop_macro("strftime")
#else
    n = strftime(tmp, sizeof(tmp), format, tm);
#endif
    if (n == 0 || n >= maxsize) {
        if (n > 0 && n < maxsize) { memcpy(buf, tmp, n); buf[n] = '\0'; }
        return n;
    }

    /* On Windows, strftime with %Z outputs locale-encoded timezone name
     * (GBK on Chinese systems). Convert ACP → UTF-8 for LLM compatibility. */
    wchar_t wbuf[256];
    int wlen = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, tmp, (int)n, wbuf, 256);
    if (wlen <= 0) {
        /* Pure ASCII (or conversion failed) — no conversion needed */
        memcpy(buf, tmp, n); buf[n] = '\0';
        return n;
    }
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, buf, (int)maxsize - 1, NULL, NULL);
    if (ulen <= 0) {
        memcpy(buf, tmp, n); buf[n] = '\0';
        return n;
    }
    buf[ulen] = '\0';
    return (size_t)ulen;
}
#endif  /* PLATFORM_WINDOWS */
