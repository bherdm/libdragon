// The Keyboard subsystem's reply processing, run on made-up replies with no
// Joybus traffic, so whatever is plugged into the console can't add events.
#include "../src/joybus/keyboard.c"

// Another device's command, which a block walk must step over: an N64 controller read.
static size_t keyboard_test_controller(uint8_t *at)
{
    at[0] = 1;
    at[1] = 4;
    at[2] = JOYBUS_COMMAND_ID_N64_CONTROLLER_READ;
    return JOYBUS_COMMAND_METADATA_SIZE + 1 + 4;
}

// A keyboard read and its reply.
static size_t keyboard_test_reply(uint8_t *at, uint8_t marker, uint8_t version, uint8_t status,
    uint8_t settings, const keyboard_key_t *keys, int count, uint8_t recv_flags)
{
    joybus_cmd_brf_keyboard_read_port_t *cmd = (void *)&at[JOYBUS_COMMAND_METADATA_SIZE];
    memset(at, 0, KEYBOARD_READ_COMMAND_SIZE);
    at[0] = sizeof(cmd->send);
    at[1] = sizeof(cmd->recv) | recv_flags;
    cmd->send.command = JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ;
    cmd->recv.marker = marker;
    cmd->recv.version = version;
    cmd->recv.status = status;
    cmd->recv.settings = settings;
    for (int i = 0; i < count; i++) cmd->recv.keys[i] = keys[i];
    return KEYBOARD_READ_COMMAND_SIZE;
}

// Process a block as the PIF returns it: a controller on port 1, a keyboard
// read's reply on port 2, and ports 3 and 4 skipped.
static void keyboard_test_read_full(uint8_t marker, uint8_t version, uint8_t status,
    uint8_t settings, const keyboard_key_t *keys, int count, uint8_t recv_flags)
{
    uint8_t out[JOYBUS_BLOCK_SIZE];
    memset(out, 0, JOYBUS_BLOCK_SIZE);
    size_t i = keyboard_test_controller(out);
    i += keyboard_test_reply(&out[i], marker, version, status, settings, keys, count, recv_flags);
    out[i++] = 0;
    out[i++] = 0;
    out[i] = 0xFE;
    keyboard_read_completed(out);
}

// Process a keyboard's reply on port 2, US and Mac.
static void keyboard_test_read(uint8_t status, const keyboard_key_t *keys, int count)
{
    keyboard_test_read_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION,
        status, 0, keys, count, 0);
}

// Take every queued event.
static int keyboard_test_drain(keyboard_event_t *events, int max)
{
    int n = 0;
    keyboard_event_t ev;
    while (keyboard_get_event(&ev))
        if (n < max) events[n++] = ev;
    return n;
}

// Is there an event for this key, going this way?
static bool keyboard_test_has(const keyboard_event_t *events, int n, keyboard_key_t key, bool pressed)
{
    for (int i = 0; i < n; i++)
        if (events[i].key == key && events[i].pressed == pressed) return true;
    return false;
}

// Connect a device with the Randnet keyboard's identity on a port.
static void keyboard_test_connect(int port)
{
    keyboard_detect_callback(JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD, JOYBUS_DETECT_CONNECTED, port, 0, NULL);
}

// Disconnect it.
static void keyboard_test_disconnect(int port)
{
    keyboard_detect_callback(JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD, JOYBUS_DETECT_DISCONNECTED, port, 0, NULL);
}

void test_keyboard(TestContext *ctx)
{
    keyboard_init_refcount = 1;
    keyboard_read_ports = 0;
    keyboard_connected = 0;
    keyboard_events_ridx = 0;
    keyboard_events_widx = 0;
    keyboard_read_rotation = 0;
    memset((void *)keyboard_ports, 0, sizeof(keyboard_ports));
    DEFER(keyboard_init_refcount = 0; keyboard_read_ports = 0; keyboard_connected = 0);

    uint8_t block[JOYBUS_BLOCK_SIZE] __attribute__((aligned(8)));
    keyboard_event_t ev, evs[64];
    keyboard_keys_t keys;
    int n;

    ASSERT_EQUAL_UNSIGNED(KEYBOARD_READS_PER_BLOCK, 2, "keyboards per block");

    // A controller isn't read
    keyboard_detect_callback(JOYBUS_IDENTIFIER_N64_CONTROLLER, JOYBUS_DETECT_CONNECTED, JOYPAD_PORT_1, 0, NULL);
    ASSERT_EQUAL_HEX(keyboard_read_ports, 0, "a controller read");

    // A device with the Randnet keyboard's identity is read, and isn't a keyboard until it answers
    keyboard_test_connect(JOYPAD_PORT_2);
    ASSERT(!keyboard_is_connected(JOYPAD_PORT_2), "a keyboard before its first reply");
    ASSERT_EQUAL_SIGNED(keyboard_get_layout(JOYPAD_PORT_2), KEYBOARD_LAYOUT_NONE, "a layout before the first reply");
    ASSERT_EQUAL_SIGNED(keyboard_get_arrangement(JOYPAD_PORT_2), KEYBOARD_ARRANGEMENT_NONE, "an arrangement before the first reply");
    ASSERT(keyboard_build_read_block(block), "one keyboard's read didn't fit a block");
    ASSERT_EQUAL_HEX(block[0], 0, "port 1 not skipped");
    ASSERT_EQUAL_HEX(block[1], 2, "send length");
    ASSERT_EQUAL_HEX(block[2], 24, "receive length");
    ASSERT_EQUAL_HEX(block[3], JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ, "read command");
    ASSERT_EQUAL_HEX(block[4], 0, "LEDs lit by default");
    ASSERT_EQUAL_HEX(block[29], 0, "port 3 not skipped");
    ASSERT_EQUAL_HEX(block[30], 0, "port 4 not skipped");
    ASSERT_EQUAL_HEX(block[31], 0xFE, "block not closed");
    ASSERT_EQUAL_HEX(block[JOYBUS_BLOCK_SIZE - 1], 0x01, "block status byte");

    // LEDs in HID's bits
    keyboard_set_leds(JOYPAD_PORT_2, KEYBOARD_LED_CAPS_LOCK | KEYBOARD_LED_KANA);
    keyboard_build_read_block(block);
    ASSERT_EQUAL_HEX(block[4], KEYBOARD_LED_CAPS_LOCK | KEYBOARD_LED_KANA, "LEDs set");
    ASSERT_EQUAL_HEX(keyboard_get_leds(JOYPAD_PORT_2), KEYBOARD_LED_CAPS_LOCK | KEYBOARD_LED_KANA, "LEDs read back");

    // Its first reply: a keyboard, its settings (German, PC, not saved) and its keys count
    keyboard_test_read_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION, 0, 0x80 | 0x08 | 2,
        (keyboard_key_t[]){ KEYBOARD_KEY_LSHIFT, KEYBOARD_KEY_A }, 2, 0);
    ASSERT(keyboard_is_connected(JOYPAD_PORT_2), "not a keyboard after its first reply");
    ASSERT_EQUAL_SIGNED(keyboard_get_layout(JOYPAD_PORT_2), KEYBOARD_LAYOUT_DE, "layout");
    ASSERT_EQUAL_SIGNED(keyboard_get_arrangement(JOYPAD_PORT_2), KEYBOARD_ARRANGEMENT_PC, "arrangement");
    n = keyboard_test_drain(evs, 64);
    ASSERT_EQUAL_SIGNED(n, 2, "first reply's events");
    ASSERT_EQUAL_HEX(evs[0].key, KEYBOARD_KEY_LSHIFT, "first press");
    ASSERT_EQUAL_UNSIGNED(evs[0].port, JOYPAD_PORT_2, "port");
    ASSERT(evs[0].pressed && evs[1].pressed && evs[1].key == KEYBOARD_KEY_A, "Shift and A not pressed");

    // The settings come with every reply: changed to Japanese, Mac
    keyboard_test_read_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION, 0, 4,
        (keyboard_key_t[]){ KEYBOARD_KEY_LSHIFT, KEYBOARD_KEY_A }, 2, 0);
    ASSERT_EQUAL_SIGNED(keyboard_get_layout(JOYPAD_PORT_2), KEYBOARD_LAYOUT_JP, "changed layout");
    ASSERT_EQUAL_SIGNED(keyboard_get_arrangement(JOYPAD_PORT_2), KEYBOARD_ARRANGEMENT_MAC, "changed arrangement");
    ASSERT(!keyboard_get_event(&ev), "held keys gave events");

    // A goes up
    keyboard_test_read(0, (keyboard_key_t[]){ KEYBOARD_KEY_LSHIFT }, 1);
    ASSERT(keyboard_get_event(&ev) && !ev.pressed && ev.key == KEYBOARD_KEY_A, "no release for A");
    ASSERT(!keyboard_get_event(&ev), "an extra event");

    // Ten keys at once, four of them modifiers: Shift goes up, the ten go down, held in the reply's order
    const keyboard_key_t ten[10] = {
        KEYBOARD_KEY_LCTRL, KEYBOARD_KEY_LALT, KEYBOARD_KEY_RSHIFT, KEYBOARD_KEY_RGUI, KEYBOARD_KEY_W,
        KEYBOARD_KEY_A, KEYBOARD_KEY_S, KEYBOARD_KEY_D, KEYBOARD_KEY_SPACE, KEYBOARD_KEY_MEDIA_PLAY_PAUSE,
    };
    keyboard_test_read(0, ten, 10);
    n = keyboard_test_drain(evs, 64);
    ASSERT_EQUAL_SIGNED(n, 11, "events for a ten-key chord");
    ASSERT(!evs[0].pressed && evs[0].key == KEYBOARD_KEY_LSHIFT, "the release not first");
    for (int i = 0; i < 10; i++)
        ASSERT(keyboard_test_has(evs, n, ten[i], true), "a chord key not pressed");
    keys = keyboard_get_keys(JOYPAD_PORT_2);
    for (int i = 0; i < 10; i++)
        ASSERT_EQUAL_HEX(keys.key[i], ten[i], "held keys out of the reply's order");
    ASSERT(!keys.overflow, "overflow without excess keys");

    // More keys held than the reply holds: the ten it holds count
    const keyboard_key_t ten2[10] = {
        KEYBOARD_KEY_LCTRL, KEYBOARD_KEY_LALT, KEYBOARD_KEY_RSHIFT, KEYBOARD_KEY_RGUI, KEYBOARD_KEY_W,
        KEYBOARD_KEY_A, KEYBOARD_KEY_S, KEYBOARD_KEY_D, KEYBOARD_KEY_SPACE, KEYBOARD_KEY_E,
    };
    keyboard_test_read(JOYBUS_BRF_KEYBOARD_STATUS_EXCESS_KEYS, ten2, 10);
    n = keyboard_test_drain(evs, 64);
    ASSERT_EQUAL_SIGNED(n, 2, "events in an overflowing read");
    ASSERT(keyboard_test_has(evs, n, KEYBOARD_KEY_MEDIA_PLAY_PAUSE, false) && keyboard_test_has(evs, n, KEYBOARD_KEY_E, true),
           "an overflowing read's keys not used");
    keys = keyboard_get_keys(JOYPAD_PORT_2);
    ASSERT(keys.overflow, "no overflow");
    ASSERT_EQUAL_HEX(keys.key[9], KEYBOARD_KEY_E, "the tenth held key");

    // Every page's keys: everything else goes up
    keyboard_test_read(0, (keyboard_key_t[]){ KEYBOARD_KEY_INTERNATIONAL3, KEYBOARD_KEY_VOLUME_INCREMENT,
        KEYBOARD_KEY_SLEEP, KEYBOARD_KEY_FN }, 4);
    n = keyboard_test_drain(evs, 64);
    ASSERT_EQUAL_SIGNED(n, 14, "ten releases and four presses");
    ASSERT(keyboard_test_has(evs, n, KEYBOARD_KEY_INTERNATIONAL3, true) && keyboard_test_has(evs, n, KEYBOARD_KEY_VOLUME_INCREMENT, true) &&
           keyboard_test_has(evs, n, KEYBOARD_KEY_SLEEP, true) && keyboard_test_has(evs, n, KEYBOARD_KEY_FN, true), "page key presses");
    keys = keyboard_get_keys(JOYPAD_PORT_2);
    ASSERT_EQUAL_HEX(keys.key[4], KEYBOARD_KEY_NONE, "stale held keys");
    ASSERT(!keys.overflow, "overflow kept");

    // Once a keyboard, a reply with its identity (BlueRetroForever changing it), an old
    // version's, or none: ignored, the keys and the keyboard kept
    keyboard_test_read_full(0x05, 0x00, 0x02, 0, NULL, 0, 0);
    keyboard_test_read_full(JOYBUS_BRF_MARKER, 1, 0, 0, (keyboard_key_t[]){ KEYBOARD_KEY_Z }, 1, 0);
    for (int i = 0; i < KEYBOARD_PROBES + 1; i++)
        keyboard_test_read_full(0, 0, 0, 0, NULL, 0, 0x80);
    ASSERT(!keyboard_get_event(&ev), "an ignored reply gave an event");
    ASSERT(keyboard_is_connected(JOYPAD_PORT_2), "an ignored reply dropped the keyboard");
    keys = keyboard_get_keys(JOYPAD_PORT_2);
    ASSERT_EQUAL_HEX(keys.key[0], KEYBOARD_KEY_INTERNATIONAL3, "held keys lost");

    // Pulled out with keys held: they're released, and later replies are ignored
    keyboard_test_disconnect(JOYPAD_PORT_2);
    ASSERT(!keyboard_is_connected(JOYPAD_PORT_2), "still connected");
    ASSERT_EQUAL_SIGNED(keyboard_get_layout(JOYPAD_PORT_2), KEYBOARD_LAYOUT_NONE, "a layout after unplugging");
    n = keyboard_test_drain(evs, 64);
    ASSERT_EQUAL_SIGNED(n, 4, "releases on unplugging");
    ASSERT(!evs[0].pressed && !evs[3].pressed, "a press on unplugging");
    ASSERT_EQUAL_HEX(keyboard_get_keys(JOYPAD_PORT_2).key[0], KEYBOARD_KEY_NONE, "keys held after unplugging");
    ASSERT_EQUAL_HEX(keyboard_read_ports, 0, "an unplugged port read");
    keyboard_test_read(0, (keyboard_key_t[]){ KEYBOARD_KEY_Z }, 1);
    ASSERT(!keyboard_get_event(&ev), "a reply used after unplugging");

    // Not keyboards: a device answering with its identity (the adapter in
    // classic mode), and the version before HID usages, aren't read again
    keyboard_test_connect(JOYPAD_PORT_2);
    keyboard_read_input_valid = true;
    keyboard_test_read_full(0x05, 0x00, 0x02, 0, NULL, 0, 0);
    ASSERT_EQUAL_HEX(keyboard_read_ports, 0, "an identity reply still read");
    ASSERT(!keyboard_read_input_valid, "the block kept for a port not read");
    keyboard_test_connect(JOYPAD_PORT_2);
    keyboard_test_read_full(JOYBUS_BRF_MARKER, 1, 0, 0, (keyboard_key_t[]){ KEYBOARD_KEY_Z }, 1, 0);
    ASSERT_EQUAL_HEX(keyboard_read_ports, 0, "version 1 still read");
    ASSERT(!keyboard_is_connected(JOYPAD_PORT_2) && !keyboard_get_event(&ev), "version 1 taken for a keyboard");

    // A Randnet keyboard doesn't answer: after three tries it isn't read
    keyboard_test_connect(JOYPAD_PORT_2);
    for (int i = 0; i < KEYBOARD_PROBES - 1; i++)
        keyboard_test_read_full(0, 0, 0, 0, NULL, 0, 0x80);
    ASSERT_EQUAL_HEX(keyboard_read_ports, 1 << JOYPAD_PORT_2, "given up before three unanswered reads");
    keyboard_test_read_full(0, 0, 0, 0, NULL, 0, 0x80);
    ASSERT_EQUAL_HEX(keyboard_read_ports, 0, "still read after three unanswered reads");

    // A keyboard that misses its first read still counts
    keyboard_test_connect(JOYPAD_PORT_2);
    keyboard_test_read_full(0, 0, 0, 0, NULL, 0, 0x80);
    keyboard_test_read(0, NULL, 0);
    ASSERT(keyboard_is_connected(JOYPAD_PORT_2), "a keyboard that missed a read not taken");

    // Four keyboards: two per block, in turn
    JOYPAD_PORT_FOREACH (port)
        keyboard_test_connect(port);
    keyboard_read_rotation = 0;
    ASSERT(!keyboard_build_read_block(block), "four reads fit one block");
    ASSERT_EQUAL_HEX(block[2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ, "port 1 not read first");
    ASSERT_EQUAL_HEX(block[28 + 2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ, "port 2 not read first");
    ASSERT_EQUAL_HEX(block[56], 0, "port 3 not skipped first");
    ASSERT_EQUAL_HEX(block[57], 0, "port 4 not skipped first");
    ASSERT_EQUAL_HEX(block[58], 0xFE, "first block not closed");
    ASSERT(!keyboard_build_read_block(block), "four reads fit one block");
    ASSERT_EQUAL_HEX(block[0], 0, "port 1 not skipped next");
    ASSERT_EQUAL_HEX(block[1], 0, "port 2 not skipped next");
    ASSERT_EQUAL_HEX(block[2 + 2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ, "port 3 not read next");
    ASSERT_EQUAL_HEX(block[30 + 2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ, "port 4 not read next");
    ASSERT_EQUAL_HEX(block[58], 0xFE, "next block not closed");

    // Three: each read two blocks in three
    keyboard_test_disconnect(JOYPAD_PORT_4);
    keyboard_read_rotation = 0;
    int reads[JOYPAD_PORT_COUNT] = {0};
    for (int b = 0; b < 3; b++)
    {
        keyboard_build_read_block(block);
        size_t i = 0;
        JOYPAD_PORT_FOREACH (port)
        {
            if (block[i] == 0) { i += JOYBUS_COMMAND_SKIP_SIZE; continue; }
            reads[port]++;
            i += KEYBOARD_READ_COMMAND_SIZE;
        }
    }
    ASSERT(reads[0] == 2 && reads[1] == 2 && reads[2] == 2 && reads[3] == 0, "three keyboards not read in turn");
    JOYPAD_PORT_FOREACH (port)
        if (port != JOYPAD_PORT_2)
            keyboard_test_disconnect(port);

    // While the queue is full, newer events are dropped
    keyboard_test_drain(evs, 64);
    for (int i = 0; i < 70; i++)
        keyboard_test_read(0, (keyboard_key_t[]){ KEYBOARD_KEY_A }, (i & 1) ? 0 : 1);
    int count = 0;
    while (keyboard_get_event(&ev))
    {
        ASSERT_EQUAL_SIGNED(ev.pressed, !(count & 1), "events out of order");
        count++;
    }
    ASSERT_EQUAL_SIGNED(count, KEYBOARD_EVENT_QUEUE_SIZE - 1, "queue size");

    // Names
    ASSERT(strcmp(keyboard_key_name(KEYBOARD_KEY_A), "A") == 0, "A's name");
    ASSERT(strcmp(keyboard_key_name(KEYBOARD_KEY_0), "0") == 0, "0's name");
    ASSERT(strcmp(keyboard_key_name(KEYBOARD_KEY_RGUI), "RGUI") == 0, "the last usage's name");
    ASSERT(strcmp(keyboard_key_name(KEYBOARD_KEY_INTERNATIONAL3), "INTERNATIONAL3") == 0, "a JIS key's name");
    ASSERT(strcmp(keyboard_key_name(KEYBOARD_KEY_MEDIA_PLAY_PAUSE), "MEDIA_PLAY_PAUSE") == 0, "a media key's name");
    ASSERT(strcmp(keyboard_key_name(KEYBOARD_KEY_SHOW_ALL_APPLICATIONS), "SHOW_ALL_APPLICATIONS") == 0, "a high consumer usage's name");
    ASSERT(strcmp(keyboard_key_name(KEYBOARD_KEY_SLEEP), "SLEEP") == 0, "a system key's name");
    ASSERT(strcmp(keyboard_key_name(KEYBOARD_KEY_FN), "FN") == 0, "fn's name");
    ASSERT(keyboard_key_name(KEYBOARD_KEY_NONE) == NULL, "a name for no key");
    ASSERT(keyboard_key_name(0x0003) == NULL, "a name for an error usage");
    ASSERT(keyboard_key_name(0x00E8) == NULL, "a name past the last usage");
    ASSERT(keyboard_key_name(KEYBOARD_KEY_CONSUMER(0x999)) == NULL, "a name for an unnamed consumer usage");

    // HID usages
    ASSERT_EQUAL_HEX(keyboard_key_usage(KEYBOARD_KEY_A), KEYBOARD_USAGE(KEYBOARD_USAGE_PAGE_KEYBOARD, 0x04), "A");
    ASSERT_EQUAL_HEX(keyboard_key_usage(KEYBOARD_KEY_RGUI), KEYBOARD_USAGE(KEYBOARD_USAGE_PAGE_KEYBOARD, 0xE7), "Right GUI");
    ASSERT_EQUAL_HEX(keyboard_key_usage(KEYBOARD_KEY_VOLUME_INCREMENT), KEYBOARD_USAGE(KEYBOARD_USAGE_PAGE_CONSUMER, 0xE9), "Volume Up");
    ASSERT_EQUAL_HEX(keyboard_key_usage(KEYBOARD_KEY_SHOW_ALL_APPLICATIONS), KEYBOARD_USAGE(KEYBOARD_USAGE_PAGE_CONSUMER, 0x2A2), "Launchpad");
    ASSERT_EQUAL_HEX(keyboard_key_usage(KEYBOARD_KEY_SLEEP), KEYBOARD_USAGE(KEYBOARD_USAGE_PAGE_DESKTOP, 0x82), "Sleep");
    ASSERT_EQUAL_HEX(keyboard_key_usage(KEYBOARD_KEY_FN), KEYBOARD_USAGE(KEYBOARD_USAGE_PAGE_APPLE, 0x03), "fn");
    ASSERT_EQUAL_HEX(keyboard_key_usage(KEYBOARD_KEY_NONE), 0, "a usage for no key");
    ASSERT_EQUAL_HEX(keyboard_key_usage(0x4001), 0, "a usage beyond the pages");
    ASSERT(KEYBOARD_KEY_IS_MODIFIER(KEYBOARD_KEY_LCTRL) && KEYBOARD_KEY_IS_MODIFIER(KEYBOARD_KEY_RGUI) &&
           !KEYBOARD_KEY_IS_MODIFIER(KEYBOARD_KEY_KP_HEXADECIMAL) && !KEYBOARD_KEY_IS_MODIFIER(KEYBOARD_KEY_CONSUMER(0xE0)),
           "modifiers");
}
