/*
 * platform_utils.c — Cross-platform utility implementations
 *
 * Non-inline functions from platform abstraction layer.
 * Separated from the inline-only headers to avoid duplication.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    const char *env = getenv("ESP_AGENT_DATA_DIR");
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
    snprintf(buf, size, "%s\\.esp-agent", home);
#else
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(buf, size, "%s/.esp-agent", home);
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
