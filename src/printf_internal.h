/**
 * @file printf_internal.h
 * @brief libdragon's own printing, through vsnprintf's engine
 * @ingroup lowlevel
 *
 * newlib has a printf engine for streams (vfprintf) and another for strings
 * (svfprintf, behind sprintf, snprintf and vsnprintf), each over 13 KB. A
 * program that formats only into strings would link both if libdragon printed
 * to stdout or stderr with printf. Included after <stdio.h> in libdragon's
 * sources, this header sends their printf, fprintf, vprintf and vfprintf
 * through #__ld_vfprintf, which formats with the string engine and writes the
 * text to the same stream.
 */

#ifndef __LIBDRAGON_PRINTF_INTERNAL_H
#define __LIBDRAGON_PRINTF_INTERNAL_H

#include <stdio.h>
#include <stdarg.h>

/** @brief vfprintf through the string engine: the text, formatted, written to the stream */
int __ld_vfprintf(FILE *f, const char *fmt, va_list va);
/** @brief fprintf through the string engine */
__attribute__((format(printf, 2, 3)))
int __ld_fprintf(FILE *f, const char *fmt, ...);
/** @brief printf through the string engine (stdout found here, not at each call) */
__attribute__((format(printf, 1, 2)))
int __ld_printf(const char *fmt, ...);
/** @brief vprintf through the string engine */
int __ld_vprintf(const char *fmt, va_list va);

/// @cond
#define printf(...)             __ld_printf(__VA_ARGS__)
#define fprintf(...)            __ld_fprintf(__VA_ARGS__)
#define vprintf(fmt, va)        __ld_vprintf(fmt, va)
#define vfprintf(f, fmt, va)    __ld_vfprintf(f, fmt, va)
/// @endcond

#endif
