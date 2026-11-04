// The BlueRetroForever module's reply decoding, run on made-up replies with no
// Joybus traffic, so whatever is plugged into the console can't change it.
#include "../src/joybus/blueretroforever.c"

// A command's reply.
static joybus_cmd_brf_command_port_t brf_test_reply(uint8_t marker, uint8_t version, uint8_t result,
    uint8_t flags, uint8_t led_level, uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3)
{
    joybus_cmd_brf_command_port_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.recv.marker = marker;
    cmd.recv.version = version;
    cmd.recv.result = result;
    cmd.recv.flags = flags;
    cmd.recv.led_level = led_level;
    cmd.recv.ports[0] = p0;
    cmd.recv.ports[1] = p1;
    cmd.recv.ports[2] = p2;
    cmd.recv.ports[3] = p3;
    return cmd;
}

void test_brf(TestContext *ctx)
{
    joybus_cmd_brf_command_port_t cmd;

    // The wire: 4 bytes sent, 10 received
    ASSERT_EQUAL_UNSIGNED(sizeof(cmd.send), 4, "send size");
    ASSERT_EQUAL_UNSIGNED(sizeof(cmd.recv), 10, "receive size");

    // Results, and no adapter: no reply (zeros), firmware without commands
    // (its identity), or another protocol version
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQUAL_SIGNED(brf_reply_result(&cmd), BRF_OK, "accepted");
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 1, 0, 0, 0, 0, 0, 0);
    ASSERT_EQUAL_SIGNED(brf_reply_result(&cmd), BRF_UNKNOWN_VERB, "unknown verb");
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 2, 0, 0, 0, 0, 0, 0);
    ASSERT_EQUAL_SIGNED(brf_reply_result(&cmd), BRF_BAD_ARGUMENT, "bad argument");
    cmd = brf_test_reply(0, 0, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQUAL_SIGNED(brf_reply_result(&cmd), BRF_NOT_PRESENT, "no reply");
    cmd = brf_test_reply(0x05, 0x00, 0x02, 0, 0, 0, 0, 0, 0);
    ASSERT_EQUAL_SIGNED(brf_reply_result(&cmd), BRF_NOT_PRESENT, "an identity reply");
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 2, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQUAL_SIGNED(brf_reply_result(&cmd), BRF_NOT_PRESENT, "another version");

    // Status: pairing open, LED level 3, a gamepad, a keyboard, nothing and a mouse
    brf_status_t status;
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, JOYBUS_BRF_FLAG_PAIRING, 3, 1, 2, 0, 3);
    brf_reply_status(&cmd, &status);
    ASSERT_EQUAL_UNSIGNED(status.version, 1, "version");
    ASSERT(status.pairing, "pairing not open");
    ASSERT_EQUAL_UNSIGNED(status.led_level, 3, "LED level");
    ASSERT(status.device[0] == BRF_DEVICE_GAMEPAD && status.device[1] == BRF_DEVICE_KEYBOARD &&
           status.device[2] == BRF_DEVICE_NONE && status.device[3] == BRF_DEVICE_MOUSE, "devices");
    ASSERT(!status.single_port && !status.asleep, "single port or asleep");
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, 0, 1, 0, 0, 0, 0);
    brf_reply_status(&cmd, &status);
    ASSERT(!status.pairing, "pairing open");
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, JOYBUS_BRF_FLAG_SINGLE_PORT | JOYBUS_BRF_FLAG_ASLEEP, 0, 0, 0, 0, 0);
    brf_reply_status(&cmd, &status);
    ASSERT(!status.pairing && status.single_port && status.asleep, "single port and asleep");

    // A verb's own data, from byte 4: a board's address in order
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, 0x24, 0x6F, 0x28, 0x01, 0x02, 0x03);
    const uint8_t address[6] = { 0x24, 0x6F, 0x28, 0x01, 0x02, 0x03 };
    ASSERT(memcmp(brf_reply_data(&cmd), address, 6) == 0, "the board's address");

    // Flash status: receiving, no error, 0x012345 bytes written; failed writing
    brf_flash_status_t flash;
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, 1, 0, 0x01, 0x23, 0x45, 0);
    brf_reply_flash_status(&cmd, &flash);
    ASSERT(flash.state == BRF_FLASH_RECEIVING && flash.error == BRF_FLASH_ERROR_NONE, "receiving");
    ASSERT_EQUAL_HEX(flash.written, 0x012345, "bytes written");
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, 3, 2, 0, 0, 0, 0);
    brf_reply_flash_status(&cmd, &flash);
    ASSERT(flash.state == BRF_FLASH_FAILED && flash.error == BRF_FLASH_ERROR_WRITE, "failed writing");

    // Log: where the log is, and four bytes from a position, whose top bit is clear
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, 0x80 | 0x12, 0x34, 0x02, 0x00, 0, 0);
    ASSERT(brf_reply_log_where(&cmd), "where the log is");
    ASSERT_EQUAL_HEX(brf_reply_log_position(&cmd), 0x1234, "the head");
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, 0x7F, 0xFC, 'a', 'b', 'c', 'd');
    ASSERT(!brf_reply_log_where(&cmd), "a log position");
    ASSERT_EQUAL_HEX(brf_reply_log_position(&cmd), 0x7FFC, "the position");
    ASSERT_EQUAL_UNSIGNED(brf_log_behind(5, 0x7FFE), 7, "behind across the wrap");
    ASSERT_EQUAL_UNSIGNED(brf_log_behind(0x100, 0x100), 0, "at the head");

    // A flash chunk: 36 bytes sent, 3 received, and a Controller Pak write's CRC
    joybus_cmd_brf_flash_chunk_port_t chunk;
    ASSERT_EQUAL_UNSIGNED(sizeof(chunk.send), 36, "chunk send size");
    ASSERT_EQUAL_UNSIGNED(sizeof(chunk.recv), 3, "chunk receive size");
    uint8_t data[BRF_FLASH_CHUNK_SIZE];
    for (int fill = 0; fill < 3; fill++)
    {
        for (int i = 0; i < BRF_FLASH_CHUNK_SIZE; i++)
            data[i] = fill == 0 ? 0x00 : fill == 1 ? i * 37 + 5 : 0xFF;
        // The bitwise CRC the adapter checks: x^8 + x^7 + x^2 + 1, over the data and a zero byte
        uint8_t crc = 0;
        for (int i = 0; i <= BRF_FLASH_CHUNK_SIZE; i++)
            for (int bit = 7; bit >= 0; bit--)
            {
                uint8_t xor = (crc & 0x80) ? 0x85 : 0x00;
                crc <<= 1;
                if (i < BRF_FLASH_CHUNK_SIZE && (data[i] & (1 << bit))) crc |= 1;
                crc ^= xor;
            }
        ASSERT_EQUAL_HEX(brf_chunk_crc(data), crc, "a chunk's CRC, fill %d", fill);
    }

    // Keyboard settings: US Mac, German PC, French Mac classic, Japanese PC until power off
    brf_keyboard_settings_t settings[JOYPAD_PORT_COUNT];
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, 0, 1, 0x00, 0x0A, 0x13, 0x8C);
    brf_reply_keyboard_settings(&cmd, settings);
    ASSERT(settings[0].layout == KEYBOARD_LAYOUT_US && settings[0].arrangement == KEYBOARD_ARRANGEMENT_MAC &&
           !settings[0].classic && !settings[0].temporary, "US Mac");
    ASSERT(settings[1].layout == KEYBOARD_LAYOUT_DE && settings[1].arrangement == KEYBOARD_ARRANGEMENT_PC &&
           !settings[1].classic && !settings[1].temporary, "German PC");
    ASSERT(settings[2].layout == KEYBOARD_LAYOUT_FR && settings[2].arrangement == KEYBOARD_ARRANGEMENT_MAC &&
           settings[2].classic && !settings[2].temporary, "French Mac classic");
    ASSERT(settings[3].layout == KEYBOARD_LAYOUT_JP && settings[3].arrangement == KEYBOARD_ARRANGEMENT_PC &&
           !settings[3].classic && settings[3].temporary, "Japanese PC until power off");

    // ...and back to the bytes
    const uint8_t bytes[JOYPAD_PORT_COUNT] = { 0x00, 0x0A, 0x13, 0x8C };
    for (int i = 0; i < JOYPAD_PORT_COUNT; i++)
        ASSERT_EQUAL_HEX(brf_keyboard_settings_byte(&settings[i]), bytes[i], "a settings byte");
    brf_keyboard_settings_t uk = { .layout = KEYBOARD_LAYOUT_UK, .arrangement = KEYBOARD_ARRANGEMENT_MAC };
    ASSERT_EQUAL_HEX(brf_keyboard_settings_byte(&uk), 0x01, "UK Mac");

    // Targets
    ASSERT_EQUAL_HEX(brf_target(BRF_ALL_PORTS), 0xFF, "every port");
    ASSERT_EQUAL_HEX(brf_target(JOYPAD_PORT_3), 2, "port 3");
}
