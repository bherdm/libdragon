/**
 * @file printf_internal.c
 * @brief libdragon's own printing, through vsnprintf's engine
 * @ingroup lowlevel
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "printf_internal.h"

int __ld_vfprintf(FILE *f, const char *fmt, va_list va)
{
    // The usual line fits on the stack; a longer one takes newlib's allocation
    char buf[256] __attribute__((uninitialized));
    size_t len = sizeof(buf);
    char *out = vasnprintf(buf, &len, fmt, va);
    if (!out) return -1;
    fwrite(out, 1, len, f);
    if (out != buf) free(out);
    return len;
}

int __ld_fprintf(FILE *f, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int n = __ld_vfprintf(f, fmt, va);
    va_end(va);
    return n;
}

int __ld_vprintf(const char *fmt, va_list va)
{
    return __ld_vfprintf(stdout, fmt, va);
}

int __ld_printf(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int n = __ld_vfprintf(stdout, fmt, va);
    va_end(va);
    return n;
}
