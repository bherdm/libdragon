/**
 * @file flashcart.h
 * @brief Flashcarts
 * @ingroup flashcart
 */

#ifndef __LIBDRAGON_FLASHCART_H
#define __LIBDRAGON_FLASHCART_H

#include <stdint.h>

/**
 * @defgroup flashcart Flashcarts
 * @ingroup peripherals
 * @brief Choosing the flashcarts whose code a program links.
 *
 * libdragon reaches a flashcart's SD card (#debug_init_sdfs) and its USB port
 * (#debug_init_usblog) with code for each flashcart it knows: the 64drive,
 * the EverDrive-64 X series, the earlier EverDrive-64s and the SummerCart64.
 * By default a program links every one, and uses the one it runs on.
 *
 * A program that supports fewer names them, once, at file scope in one of its
 * source files, and links only their code:
 *
 * @code
 * FLASHCARTS(FLASHCART_SC64);
 * @endcode
 *
 * They're tried in the order given. On any other flashcart, the SD card and
 * USB logging aren't found, as on a plain cartridge.
 *
 * @{
 */

/** @brief The 64drive */
#define FLASHCART_64DRIVE       ci
/** @brief The EverDrive-64 X series */
#define FLASHCART_EVERDRIVE_X   edx
/** @brief The EverDrive-64 V1, V2, V2.5, V3 and ED64+ */
#define FLASHCART_EVERDRIVE     ed
/** @brief The SummerCart64 */
#define FLASHCART_SC64          sc

/** @brief Link only these flashcarts' code (from one to four of the FLASHCART_ names). */
#define FLASHCARTS(...) \
    __FLASHCART_EACH(__FLASHCART_DECLARE, __VA_ARGS__) \
    __FLASHCART_LIST(const int, __cart_types, __FLASHCART_TYPE, __VA_ARGS__) -1 }; \
    __FLASHCART_LIST(__flashcart_call_t, __cart_init, __FLASHCART_INIT, __VA_ARGS__) }; \
    __FLASHCART_LIST(__flashcart_call_t, __cart_exit, __FLASHCART_EXIT, __VA_ARGS__) }; \
    __FLASHCART_LIST(__flashcart_call_t, __cart_card_init, __FLASHCART_CARD_INIT, __VA_ARGS__) }; \
    __FLASHCART_LIST(__flashcart_rd_dram_t, __cart_card_rd_dram, __FLASHCART_RD_DRAM, __VA_ARGS__) }; \
    __FLASHCART_LIST(__flashcart_cart_t, __cart_card_rd_cart, __FLASHCART_RD_CART, __VA_ARGS__) }; \
    __FLASHCART_LIST(__flashcart_wr_dram_t, __cart_card_wr_dram, __FLASHCART_WR_DRAM, __VA_ARGS__) }; \
    __FLASHCART_LIST(__flashcart_cart_t, __cart_card_wr_cart, __FLASHCART_WR_CART, __VA_ARGS__) }; \
    __FLASHCART_LIST(const struct usb_driver_s *const, __usb_drivers, __FLASHCART_USB, __VA_ARGS__) 0 }

/** @} */ /* flashcart */

/// @cond
#ifdef __cplusplus
#define __FLASHCART_C "C"
#else
#define __FLASHCART_C
#endif

// One list a call, each in a section of its own: a list the program never
// uses isn't linked, and nor are the functions it names
#define __FLASHCART_LIST(type, name, m, ...) \
    extern __FLASHCART_C type name[]; \
    type name[] __attribute__((section(".rodata." #name))) = { __FLASHCART_EACH(m, __VA_ARGS__)
typedef int (*const __flashcart_call_t)(void);
typedef int (*const __flashcart_rd_dram_t)(void *, uint32_t, uint32_t);
typedef int (*const __flashcart_wr_dram_t)(const void *, uint32_t, uint32_t);
typedef int (*const __flashcart_cart_t)(uint32_t, uint32_t, uint32_t);

// Each cart: its type, its functions for each of libcart's calls, and usb.c's record
#define __FLASHCART_TYPE(c)       __FLASHCART_TYPE_##c,
#define __FLASHCART_TYPE_ci       0
#define __FLASHCART_TYPE_edx      1
#define __FLASHCART_TYPE_ed       2
#define __FLASHCART_TYPE_sc       3
#define __FLASHCART_INIT(c)       c##_init,
#define __FLASHCART_EXIT(c)       c##_exit,
#define __FLASHCART_CARD_INIT(c)  c##_card_init,
#define __FLASHCART_RD_DRAM(c)    c##_card_rd_dram,
#define __FLASHCART_RD_CART(c)    c##_card_rd_cart,
#define __FLASHCART_WR_DRAM(c)    c##_card_wr_dram,
#define __FLASHCART_WR_CART(c)    c##_card_wr_cart,
#define __FLASHCART_USB(c)        &__usb_driver_##c,
#define __FLASHCART_DECLARE(c) \
    extern __FLASHCART_C int c##_init(void), c##_exit(void), c##_card_init(void), \
        c##_card_rd_dram(void *, uint32_t, uint32_t), c##_card_rd_cart(uint32_t, uint32_t, uint32_t), \
        c##_card_wr_dram(const void *, uint32_t, uint32_t), c##_card_wr_cart(uint32_t, uint32_t, uint32_t); \
    extern __FLASHCART_C const struct usb_driver_s __usb_driver_##c;

#define __FLASHCART_EACH(m, ...) __FLASHCART_PICK(__VA_ARGS__, __FLASHCART_4, __FLASHCART_3, __FLASHCART_2, __FLASHCART_1, _)(m, __VA_ARGS__)
#define __FLASHCART_PICK(_1, _2, _3, _4, n, ...) n
#define __FLASHCART_1(m, a) m(a)
#define __FLASHCART_2(m, a, b) m(a) m(b)
#define __FLASHCART_3(m, a, b, c) m(a) m(b) m(c)
#define __FLASHCART_4(m, a, b, c, d) m(a) m(b) m(c) m(d)

struct usb_driver_s;
/// @endcond

#endif
