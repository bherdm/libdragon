/**
 * @file debug_write.c
 * @brief Debug output to the debug channels, one writer at a time
 * @ingroup debug
 *
 * An interrupt handler mustn't write to the debug channels while a thread is
 * in the middle of it: a USB cart sends a buffer in two steps (copy it to the
 * cart, then tell the cart to send it), and an interrupt's message between
 * them takes the thread's place. So a thread's write marks itself, and an
 * interrupt handler's output during it waits here, to go out right after.
 *
 * This file is reached only through debug_init_*() (see #__debug_write_fn):
 * a program that never opens a debug channel has nothing to protect, and
 * links none of it.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "exception.h"
#include "interrupt.h"
#include "debug_internal.h"

/** @brief A thread is writing to the debug channels. */
static volatile bool writing = false;
/** @brief Output from interrupt handlers that came while a thread was writing. */
static char pending[256];
/** @brief How many bytes #pending holds. */
static volatile int pending_len = 0;
/** @brief Bytes of interrupt output that didn't fit in #pending. */
static volatile int lost = 0;

/** @brief Send the interrupt handlers' output that waited for a thread's write, then end the write. */
static void send_pending(void)
{
    char out[sizeof(pending) + 64];
    while (1) {
        disable_interrupts();
        int n = pending_len, l = lost;
        if (n == 0 && l == 0) {
            writing = false;
            enable_interrupts();
            return;
        }
        memcpy(out, pending, n);
        pending_len = 0;
        lost = 0;
        enable_interrupts();

        if (l)
            n += snprintf(out + n, sizeof(out) - n, "[debug: %d bytes from interrupt handlers lost]\n", l);
        __debug_write_channels(out, n);
    }
}

void __debug_write(const char *buf, int len)
{
    if (exception_is_running()) {
        if (!writing) {
            __debug_write_channels(buf, len);
        } else if (len <= (int)sizeof(pending) - pending_len) {
            memcpy(pending + pending_len, buf, len);
            pending_len += len;
        } else {
            lost += len;
        }
        return;
    }

    writing = true;
    __debug_write_channels(buf, len);
    send_pending();
}
