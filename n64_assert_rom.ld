/*
 * Linked before n64.ld, keeps libdragon's assert texts out of RAM: their
 * section, .assert_texts, isn't loaded, at addresses no RAM has, and
 * __assert_text_rom reads a text from the ROM's .assert file when an assert
 * fires. The ROM must carry the section as that file:
 *
 *     objcopy --dump-section .assert_texts=<rom name>.assert <elf>
 *
 * then given to n64tool with the other files. n64.mk does both when a
 * program sets N64_ROM_ASSERT_TEXTS=1.
 */
SECTIONS {
    .assert_texts 0xF0000000 (INFO) : { *(.assert_texts .assert_texts.*) }
}
EXTERN(__assert_text_rom)
