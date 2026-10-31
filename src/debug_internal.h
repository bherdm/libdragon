/**
 * @file debug_internal.h
 * @brief Debug library internal functions
 * @ingroup debug
 */
#ifndef __LIBDRAGON_DEBUG_INTERNAL_H
#define __LIBDRAGON_DEBUG_INTERNAL_H

/**
 * @brief Where debug output goes once a debug channel is open: #__debug_write.
 *
 * NULL until a debug_init_*() function opens a channel, so a program that
 * never opens one links none of the debug output machinery, and code that
 * only matters while debugging can test it first.
 */
extern void (*__debug_write_fn)(const char *buf, int len);

/** @brief Write to every open debug channel. */
void __debug_write_channels(const char *buf, int len);

/** @brief Write debug output to the channels, one writer at a time (debug_write.c). */
void __debug_write(const char *buf, int len);

#endif
