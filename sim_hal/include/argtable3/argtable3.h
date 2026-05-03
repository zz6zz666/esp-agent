/*
 * argtable3/argtable3.h — Minimal but functional stub for desktop simulator
 *
 * Provides enough of the argtable3 API for esp-claw CLI commands (lua, qq,
 * feishu, tg, wechat, event_router, skill, auto, cap) to parse correctly.
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ARG_TYPE_LIT = 1,
    ARG_TYPE_STR,
    ARG_TYPE_INT,
    ARG_TYPE_END,
} arg_type_t;

/* ---- common header (must be first field in every arg struct) ---- */
struct arg_hdr {
    arg_type_t  type;
    const char *shortopts;  /* e.g. "l" or NULL */
    const char *longopts;   /* e.g. "list" or NULL */
};

#define ARG_MAX_SVAL  16

struct arg_lit {
    struct arg_hdr hdr;
    int count;
};

struct arg_str {
    struct arg_hdr hdr;
    int   count;
    char *sval[ARG_MAX_SVAL];
};

struct arg_int {
    struct arg_hdr hdr;
    int   count;
    int   ival[ARG_MAX_SVAL];
};

struct arg_end {
    struct arg_hdr hdr;
    int max_errors;
    int error_count;
};

/* ---- helpers ---- */

static int match_short(const char *shortopts, const char *arg)
{
    if (!shortopts || !arg) return 0;
    if (arg[0] != '-' || arg[1] == '-' || arg[1] == '\0') return 0;
    const char *s = shortopts;
    while (*s) {
        if (arg[1] == *s && arg[2] == '\0') return 1;
        s++;
    }
    return 0;
}

static int match_long(const char *longopts, const char *arg)
{
    if (!longopts || !arg) return 0;
    if (arg[0] != '-' || arg[1] != '-') return 0;
    return strcmp(arg + 2, longopts) == 0;
}

/* ---- constructors ---- */

static inline struct arg_lit *arg_lit0(const char *shortopts,
                                        const char *longopts,
                                        const char *glossary)
{
    (void)glossary;
    struct arg_lit *a = calloc(1, sizeof(*a));
    if (a) {
        a->hdr.type      = ARG_TYPE_LIT;
        a->hdr.shortopts = shortopts;
        a->hdr.longopts  = longopts;
    }
    return a;
}

static inline struct arg_str *arg_str0(const char *shortopts,
                                        const char *longopts,
                                        const char *glossary,
                                        const char *datatype)
{
    (void)glossary; (void)datatype;
    struct arg_str *a = calloc(1, sizeof(*a));
    if (a) {
        a->hdr.type      = ARG_TYPE_STR;
        a->hdr.shortopts = shortopts;
        a->hdr.longopts  = longopts;
    }
    return a;
}

static inline struct arg_int *arg_int0(const char *shortopts,
                                        const char *longopts,
                                        const char *glossary,
                                        const char *datatype)
{
    (void)glossary; (void)datatype;
    struct arg_int *a = calloc(1, sizeof(*a));
    if (a) {
        a->hdr.type      = ARG_TYPE_INT;
        a->hdr.shortopts = shortopts;
        a->hdr.longopts  = longopts;
    }
    return a;
}

static inline struct arg_end *arg_end(int max_errors)
{
    struct arg_end *a = calloc(1, sizeof(*a));
    if (a) {
        a->hdr.type      = ARG_TYPE_END;
        a->hdr.shortopts = NULL;
        a->hdr.longopts  = NULL;
        a->max_errors    = max_errors;
    }
    return a;
}

/* ---- argument parsing ---- */

static inline int arg_parse(int argc, char **argv, void **argtable)
{
    if (!argtable) return 0;

    /* Reset all counts to 0 before parsing */
    for (int j = 0; argtable[j] != NULL; j++) {
        struct arg_hdr *hdr = (struct arg_hdr *)argtable[j];
        if (hdr->type == ARG_TYPE_END) continue;
        switch (hdr->type) {
        case ARG_TYPE_LIT: ((struct arg_lit *)argtable[j])->count = 0; break;
        case ARG_TYPE_STR: ((struct arg_str *)argtable[j])->count = 0; break;
        case ARG_TYPE_INT: ((struct arg_int *)argtable[j])->count = 0; break;
        default: break;
        }
    }

    int nerrors = 0;

    /* Walk argv[1..argc-1] */
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        int matched = 0;

        for (int j = 0; argtable[j] != NULL; j++) {
            struct arg_hdr *hdr = (struct arg_hdr *)argtable[j];
            if (hdr->type == ARG_TYPE_END) continue;

            if (!match_short(hdr->shortopts, arg) &&
                !match_long(hdr->longopts, arg))
                continue;

            matched = 1;

            switch (hdr->type) {
            case ARG_TYPE_LIT: {
                struct arg_lit *lit = (struct arg_lit *)argtable[j];
                lit->count++;
                break;
            }
            case ARG_TYPE_STR: {
                struct arg_str *s = (struct arg_str *)argtable[j];
                if (s->count < ARG_MAX_SVAL && i + 1 < argc) {
                    const char *next = argv[i + 1];
                    /* Don't consume the next arg if it looks like a flag */
                    if (next[0] == '-' && (next[1] == '-' || strlen(next) == 2))
                        break;
                    s->sval[s->count++] = (char *)next;
                    i++;  /* consume value */
                }
                break;
            }
            case ARG_TYPE_INT: {
                struct arg_int *iv = (struct arg_int *)argtable[j];
                if (iv->count < ARG_MAX_SVAL && i + 1 < argc) {
                    const char *next = argv[i + 1];
                    if (next[0] == '-' && (next[1] == '-' || strlen(next) == 2))
                        break;
                    iv->ival[iv->count++] = atoi(next);
                    i++;
                }
                break;
            }
            default:
                break;
            }
            break;  /* matched, move to next argv */
        }

        if (!matched) {
            /* Treat as unknown — but it might be a positional arg or
               value of a previously-consumed flag.  Silently skip. */
        }
    }

    /* Count errors in end entry */
    for (int j = 0; argtable[j] != NULL; j++) {
        struct arg_hdr *hdr = (struct arg_hdr *)argtable[j];
        if (hdr->type == ARG_TYPE_END) {
            struct arg_end *end = (struct arg_end *)argtable[j];
            nerrors = end->error_count;
            break;
        }
    }

    return nerrors;
}

static inline void arg_print_errors(FILE *fp, struct arg_end *end, const char *progname)
{
    if (end && end->error_count > 0)
        fprintf(fp, "%s: %d argument error(s)\n",
                progname ? progname : "", end->error_count);
}

static inline void arg_freetable(void **argtable, size_t n)
{
    (void)argtable; (void)n;
}

#ifdef __cplusplus
}
#endif
