/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD

 * SPDX-License-Identifier: Apache-2.0

 *

 * open_memstream() implementation for platforms that lack it (Windows/MinGW).

 * Backed by a temporary file; __wrap_fclose intercepts stdin to read back.

 * Requires -Wl,--wrap,fclose in linker flags.

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <windows.h>

/* Provided by linker (--wrap,fclose) */
extern int __real_fclose(FILE *);

#define MAX_MEMSTREAMS 16

typedef struct {
    FILE *fp;
    char **bufp;
    size_t *sizep;
    char path[MAX_PATH];
    int active;
} memstream_entry_t;

static memstream_entry_t s_entries[MAX_MEMSTREAMS];

static memstream_entry_t *find_entry(FILE *fp)
{
    for (int i = 0; i < MAX_MEMSTREAMS; i++) {
        if (s_entries[i].active && s_entries[i].fp == fp)
            return &s_entries[i];
    }
    return NULL;
}

static memstream_entry_t *alloc_entry(void)
{
    for (int i = 0; i < MAX_MEMSTREAMS; i++) {
        if (!s_entries[i].active) {
            memset(&s_entries[i], 0, sizeof(memstream_entry_t));
            s_entries[i].active = 1;
            return &s_entries[i];
        }
    }
    return NULL;
}

FILE *open_memstream(char **bufp, size_t *sizep)
{
    char tmpdir[MAX_PATH];
    char fname[MAX_PATH];
    FILE *fp;
    memstream_entry_t *entry;

    if (!bufp || !sizep) return NULL;

    DWORD ret = GetTempPathA(MAX_PATH, tmpdir);
    if (ret == 0 || ret > MAX_PATH) return NULL;

    if (GetTempFileNameA(tmpdir, "crc", 0, fname) == 0) return NULL;

    fp = fopen(fname, "w+b");
    if (!fp) {
        DeleteFileA(fname);
        return NULL;
    }

    entry = alloc_entry();
    if (!entry) {
        fclose(fp);
        DeleteFileA(fname);
        return NULL;
    }

    entry->fp = fp;
    entry->bufp = bufp;
    entry->sizep = sizep;
    strncpy(entry->path, fname, MAX_PATH - 1);
    entry->path[MAX_PATH - 1] = '\0';

    *bufp = NULL;
    *sizep = 0;

    return fp;
}

int __wrap_fclose(FILE *fp)
{
    memstream_entry_t *entry;

    if (!fp) return EOF;

    entry = find_entry(fp);
    if (entry) {
        struct stat st;
        if (fstat(fileno(fp), &st) == 0 && st.st_size > 0) {
            char *buf = (char *)malloc((size_t)st.st_size + 1);
            if (buf) {
                fseek(fp, 0, SEEK_SET);
                fread(buf, 1, (size_t)st.st_size, fp);
                buf[st.st_size] = '\0';
                *entry->bufp = buf;
                *entry->sizep = (size_t)st.st_size;
            }
        }
        entry->active = 0;
        DeleteFileA(entry->path);
        return __real_fclose(fp);
    }

    return __real_fclose(fp);
}
