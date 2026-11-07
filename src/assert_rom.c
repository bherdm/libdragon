/**
 * @file assert_rom.c
 * @brief An assert's texts read from ROM, for programs that leave them there
 * @ingroup lowlevel
 */

#include <stdint.h>
#include "dma.h"
#include "rompak_internal.h"

/** @brief Where n64_assert_rom.ld puts .assert_texts: addresses no RAM has, read from the ROM's .assert file */
#define ASSERT_TEXTS_BASE   0xF0000000

/** @brief The texts read, one buffer for each of the file, the expression and the message */
static char assert_texts[3][256];

/**
 * @brief An assert's text, read from ROM if that's where it is.
 *
 * Linked only into programs that leave libdragon's assert texts in ROM
 * (n64_assert_rom.ld names it), and called by debug_assert_func_f with
 * interrupts disabled.
 *
 * @param text  The text's address: in RAM, or at #ASSERT_TEXTS_BASE onwards
 * @param slot  Which buffer to read it into (0 to 2)
 * @return The text in RAM
 */
const char *__assert_text_rom(const char *text, int slot)
{
    uint32_t addr = (uint32_t)text;
    if (addr < ASSERT_TEXTS_BASE) return text;

    static pi_addr_t rom;
    if (!rom) rom = rompak_search_ext(".assert", NULL);
    if (!rom) return "(text not in the ROM: no .assert file)";

    char *buf = assert_texts[slot];
    pi_addr_t pos = rom + (addr - ASSERT_TEXTS_BASE);
    for (int i = 0; i < (int)sizeof(assert_texts[0]) - 1; i++, pos++) {
        buf[i] = io_read(pos & ~3) >> (24 - 8 * (pos & 3));
        if (!buf[i]) return buf;
    }
    buf[sizeof(assert_texts[0]) - 1] = 0;
    return buf;
}
