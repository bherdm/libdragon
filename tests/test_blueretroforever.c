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
    cmd = brf_test_reply(JOYBUS_BRF_MARKER, 1, 0, 0, 1, 0, 0, 0, 0);
    brf_reply_status(&cmd, &status);
    ASSERT(!status.pairing, "pairing open");

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
