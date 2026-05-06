/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD

 * SPDX-License-Identifier: Apache-2.0

 *

 * Modifiable stdout pointer — wraps the CRT's non-lvalue stdout macro

 * so POSIX-style code (stdout = capture) compiles on Windows/MinGW.

 */

#include <stdio.h>

/* mingw_compat.h replaces stdout with crush_stdout.
 * Undo the override so we can access the real CRT function. */
#undef stdout

FILE *crush_stdout = NULL;

__attribute__((constructor))
static void init_crush_stdout(void)
{
    crush_stdout = __acrt_iob_func(1);
}
