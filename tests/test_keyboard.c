// The Keyboard subsystem's reply processing, run on made-up replies with no
// Joybus traffic, so whatever is plugged into the console can't add events.
#include "../src/joybus/keyboard.c"

// A block as the PIF returns it, with a controller read on port 1, and the
// commands the test writes on the next ports.
static uint8_t kbt_out[JOYBUS_BLOCK_SIZE] __attribute__((aligned(8)));
static size_t kbt_at;
static int kbt_ports;

static void kbt_begin(void)
{
    memset(kbt_out, 0, JOYBUS_BLOCK_SIZE);
    kbt_out[0] = 1;
    kbt_out[1] = 4;
    kbt_out[2] = JOYBUS_COMMAND_ID_N64_CONTROLLER_READ;
    kbt_at = JOYBUS_COMMAND_METADATA_SIZE + 1 + 4;
    kbt_ports = 1;
}

static void kbt_skip(void)
{
    kbt_out[kbt_at++] = 0;
    kbt_ports++;
}

// A full read and its reply.
static void kbt_full(uint8_t marker, uint8_t version, uint8_t status, uint8_t settings, uint8_t leds,
    const keyboard_key_t *keys, int count, uint8_t recv_flags)
{
    uint8_t *at = &kbt_out[kbt_at];
    joybus_cmd_brf_keyboard_read_port_t *cmd = (void *)&at[JOYBUS_COMMAND_METADATA_SIZE];
    at[0] = sizeof(cmd->send);
    at[1] = sizeof(cmd->recv) | recv_flags;
    cmd->send.command = JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ;
    cmd->send.leds = leds;
    cmd->recv.marker = marker;
    cmd->recv.version = version;
    cmd->recv.status = status;
    cmd->recv.settings = settings;
    for (int i = 0; i < count; i++) cmd->recv.keys[i] = keys[i];
    kbt_at += KEYBOARD_COMMAND_SIZE(*cmd);
    kbt_ports++;
}

// A compact read (0x2B) and its reply.
static void kbt_compact(uint8_t status, uint8_t modifiers, const keyboard_key_t *keys, int count, uint8_t recv_flags)
{
    uint8_t *at = &kbt_out[kbt_at];
    joybus_cmd_brf_keyboard_compact_read_port_t *cmd = (void *)&at[JOYBUS_COMMAND_METADATA_SIZE];
    at[0] = sizeof(cmd->send);
    at[1] = sizeof(cmd->recv) | recv_flags;
    cmd->send.command = JOYBUS_COMMAND_ID_BRF_KEYBOARD_COMPACT_READ;
    cmd->recv.status = status;
    cmd->recv.modifiers = modifiers;
    for (int i = 0; i < count; i++) cmd->recv.keys[i] = keys[i];
    kbt_at += KEYBOARD_COMMAND_SIZE(*cmd);
    kbt_ports++;
}

// A keyboard and mouse's compact read (0x2C) and its reply.
static void kbt_mouse(uint8_t status, uint8_t modifiers, uint8_t buttons, int wheel, int hwheel, int x, int y,
    const keyboard_key_t *keys, int count)
{
    uint8_t *at = &kbt_out[kbt_at];
    joybus_cmd_brf_keyboard_mouse_read_port_t *cmd = (void *)&at[JOYBUS_COMMAND_METADATA_SIZE];
    at[0] = sizeof(cmd->send);
    at[1] = sizeof(cmd->recv);
    cmd->send.command = JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ;
    cmd->recv.status = status;
    cmd->recv.modifiers = modifiers;
    cmd->recv.buttons = buttons | (wheel << JOYBUS_BRF_MOUSE_WHEEL_SHIFT);
    // Each axis 12 bits, packed byte by byte, so the byte order is the wire's on any host
    cmd->recv.motion[0] = (x >> 4) & 0xFF;
    cmd->recv.motion[1] = ((x & 0xF) << 4) | ((y >> 8) & 0xF);
    cmd->recv.motion[2] = y & 0xFF;
    cmd->recv.hwheel = hwheel << JOYBUS_BRF_MOUSE_WHEEL_SHIFT;
    for (int i = 0; i < count; i++) cmd->recv.keys[i] = keys[i];
    kbt_at += KEYBOARD_COMMAND_SIZE(*cmd);
    kbt_ports++;
}

// Close the block and process it.
static void kbt_end(void)
{
    while (kbt_ports < JOYPAD_PORT_COUNT) kbt_skip();
    kbt_out[kbt_at] = 0xFE;
    keyboard_read_completed(kbt_out);
}

// Port 2's full read, version 3, US and Mac, with no LEDs: one it was due.
static void kbt_read_full(uint8_t status, const keyboard_key_t *keys, int count)
{
    keyboard_full |= 1 << JOYPAD_PORT_2;
    kbt_begin();
    kbt_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION, status, 0, 0, keys, count, 0);
    kbt_end();
}

// Port 2's compact read.
static void kbt_read(uint8_t status, uint8_t modifiers, const keyboard_key_t *keys, int count)
{
    kbt_begin();
    kbt_compact(status, modifiers, keys, count, 0);
    kbt_end();
}

// A keyboard's status in version 3 and the compact reads.
#define KBT_KB JOYBUS_BRF_KEYBOARD_STATUS_KEYBOARD

// Take every queued event.
static int kbt_drain(keyboard_event_t *events, int max)
{
    int n = 0;
    keyboard_event_t ev;
    while (keyboard_get_event(&ev))
        if (n < max) events[n++] = ev;
    return n;
}

// Is there an event for this key, going this way?
static bool kbt_has(const keyboard_event_t *events, int n, keyboard_key_t key, bool pressed)
{
    for (int i = 0; i < n; i++)
        if (events[i].key == key && events[i].pressed == pressed) return true;
    return false;
}

// Connect a device with the Randnet keyboard's identity, or a controller's, on a port.
static void kbt_connect(int port)
{
    keyboard_detect_callback(JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD, JOYBUS_DETECT_CONNECTED, port, 0, NULL);
}
static void kbt_connect_pad(int port)
{
    keyboard_detect_callback(JOYBUS_IDENTIFIER_N64_CONTROLLER, JOYBUS_DETECT_CONNECTED, port, 0, NULL);
}

// Disconnect it.
static void kbt_disconnect(int port)
{
    keyboard_detect_callback(JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD, JOYBUS_DETECT_DISCONNECTED, port, 0, NULL);
}

// Reset the subsystem's state, as keyboard_init does, without its handlers.
static void kbt_reset(void)
{
    keyboard_init_refcount = 1;
    keyboard_probing = keyboard_compact = keyboard_full = keyboard_pads = 0;
    keyboard_connected = keyboard_mice = 0;
    __keyboard_events_ridx = 0;
    __keyboard_events_widx = 0;
    keyboard_main_valid = keyboard_side_valid = false;
    keyboard_side_rotation = 0;
    keyboard_paused_keys = keyboard_paused_mice = 0;
    memset((void *)keyboard_ports, 0, sizeof(keyboard_ports));
}

void test_keyboard(TestContext *ctx)
{
    kbt_reset();
    DEFER(kbt_reset(); keyboard_init_refcount = 0);

    keyboard_event_t ev, evs[64];
    keyboard_keys_t keys;
    keyboard_mouse_t mouse;
    int n;
    const uint8_t *block = keyboard_side_input;

    // A controller and a device with the Randnet keyboard's identity are probed with a full read
    kbt_connect_pad(JOYPAD_PORT_1);
    kbt_connect(JOYPAD_PORT_2);
    ASSERT_EQUAL_HEX(keyboard_probing, 0x3, "ports probed");
    ASSERT_EQUAL_HEX(keyboard_pads, 0x1, "controller ports");
    ASSERT(!keyboard_is_connected(JOYPAD_PORT_2), "a keyboard before its first reply");
    ASSERT_EQUAL_SIGNED(keyboard_get_layout(JOYPAD_PORT_2), KEYBOARD_LAYOUT_NONE, "a layout before the first reply");
    ASSERT_EQUAL_SIGNED(keyboard_get_arrangement(JOYPAD_PORT_2), KEYBOARD_ARRANGEMENT_NONE, "an arrangement before the first reply");
    ASSERT(keyboard_build_side(), "two full reads didn't fit a block");
    ASSERT_EQUAL_HEX(keyboard_side_reads, 0x3, "side reads");
    ASSERT_EQUAL_HEX(block[0], 2, "send length");
    ASSERT_EQUAL_HEX(block[1], 24, "receive length");
    ASSERT_EQUAL_HEX(block[2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ, "full read command");
    ASSERT_EQUAL_HEX(block[3], 0, "LEDs lit by default");
    ASSERT_EQUAL_HEX(block[28 + 2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ, "port 2 not read");
    ASSERT_EQUAL_HEX(block[56], 0, "port 3 not skipped");
    ASSERT_EQUAL_HEX(block[57], 0, "port 4 not skipped");
    ASSERT_EQUAL_HEX(block[58], 0xFE, "block not closed");
    ASSERT_EQUAL_HEX(block[JOYBUS_BLOCK_SIZE - 1], 0x01, "block status byte");

    // Nothing to read with the joypads yet
    keyboard_build_main(false);
    ASSERT_EQUAL_HEX(keyboard_main_reads, 0, "main reads before a keyboard answered");
    ASSERT_EQUAL_HEX(keyboard_main_input[4], 0xFE, "an empty main block not four skips");

    // LEDs in HID's bits go with the probe's full read
    keyboard_set_leds(JOYPAD_PORT_2, KEYBOARD_LED_CAPS_LOCK | KEYBOARD_LED_KANA);
    ASSERT(!keyboard_side_valid, "the side block kept after an LED change");
    keyboard_build_side();
    ASSERT_EQUAL_HEX(block[28 + 3], KEYBOARD_LED_CAPS_LOCK | KEYBOARD_LED_KANA, "LEDs set");
    ASSERT_EQUAL_HEX(keyboard_get_leds(JOYPAD_PORT_2), KEYBOARD_LED_CAPS_LOCK | KEYBOARD_LED_KANA, "LEDs read back");
    ASSERT_EQUAL_HEX(keyboard_full, 0, "a full read due on a port not yet known");

    // The controller doesn't answer; the keyboard's first reply: version 3, its settings
    // (German, PC, not saved) and its keys count, the modifier as a bit pressed first
    for (int i = 0; i < KEYBOARD_PROBES; i++) {
        kbt_begin();
        kbt_out[1] |= 0x80;     // port 1's controller read went unanswered as a full read would
        kbt_out[2] = JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ;
        kbt_end();
    }
    ASSERT_EQUAL_HEX(keyboard_probing, 0x2, "a controller probed after three unanswered reads");
    kbt_begin();
    kbt_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION, KBT_KB, 0x80 | 0x08 | 2,
        KEYBOARD_LED_CAPS_LOCK | KEYBOARD_LED_KANA, (keyboard_key_t[]){ KEYBOARD_KEY_A, KEYBOARD_KEY_LSHIFT }, 2, 0);
    kbt_end();
    ASSERT(keyboard_is_connected(JOYPAD_PORT_2), "not a keyboard after its first reply");
    ASSERT_EQUAL_HEX(keyboard_probing, 0, "still probed after answering");
    ASSERT_EQUAL_HEX(keyboard_compact, 0x2, "version 3 not read compact");
    ASSERT_EQUAL_HEX(keyboard_full, 0, "a full read due after one");
    ASSERT_EQUAL_SIGNED(keyboard_get_layout(JOYPAD_PORT_2), KEYBOARD_LAYOUT_DE, "layout");
    ASSERT_EQUAL_SIGNED(keyboard_get_arrangement(JOYPAD_PORT_2), KEYBOARD_ARRANGEMENT_PC, "arrangement");
    ASSERT(keyboard_has_events(), "no events to take after the first reply");
    n = kbt_drain(evs, 64);
    ASSERT(!keyboard_has_events(), "events to take after taking them all");
    ASSERT_EQUAL_SIGNED(n, 2, "first reply's events");
    ASSERT(evs[0].pressed && evs[0].key == KEYBOARD_KEY_LSHIFT, "Shift not pressed first");
    ASSERT_EQUAL_UNSIGNED(evs[0].port, JOYPAD_PORT_2, "port");
    ASSERT(evs[1].pressed && evs[1].key == KEYBOARD_KEY_A, "A not pressed");
    keys = keyboard_get_keys(JOYPAD_PORT_2);
    ASSERT_EQUAL_HEX(keys.key[0], KEYBOARD_KEY_A, "held key");
    ASSERT_EQUAL_HEX(keys.key[1], KEYBOARD_KEY_NONE, "a modifier in the key slots");
    ASSERT_EQUAL_HEX(keys.modifiers, KEYBOARD_MODIFIER(KEYBOARD_KEY_LSHIFT), "modifier bits");
    ASSERT(keys.modifiers & KEYBOARD_MODIFIER_SHIFT, "either Shift");

    // Now read with the joypads: a compact read in place of port 2's skip
    keyboard_build_main(false);
    block = keyboard_main_input;
    ASSERT_EQUAL_HEX(keyboard_main_reads, 0x2, "main reads");
    ASSERT_EQUAL_HEX(block[0], 0, "port 1 not skipped");
    ASSERT_EQUAL_HEX(block[1], 1, "compact send length");
    ASSERT_EQUAL_HEX(block[2], 7, "compact receive length");
    ASSERT_EQUAL_HEX(block[3], JOYBUS_COMMAND_ID_BRF_KEYBOARD_COMPACT_READ, "compact command");
    ASSERT_EQUAL_HEX(block[11], 0, "port 3 not skipped");
    ASSERT_EQUAL_HEX(block[12], 0, "port 4 not skipped");
    ASSERT_EQUAL_HEX(block[13], 0xFE, "main block not closed");
    keyboard_build_side();
    ASSERT_EQUAL_HEX(keyboard_side_reads, 0, "a side read with nothing due");

    // The same keys again: nothing; A goes up
    kbt_read(KBT_KB, KEYBOARD_MODIFIER(KEYBOARD_KEY_LSHIFT), (keyboard_key_t[]){ KEYBOARD_KEY_A }, 1);
    ASSERT(!keyboard_get_event(&ev), "held keys gave events");
    kbt_read(KBT_KB, KEYBOARD_MODIFIER(KEYBOARD_KEY_LSHIFT), NULL, 0);
    ASSERT(keyboard_get_event(&ev) && !ev.pressed && ev.key == KEYBOARD_KEY_A, "no release for A");
    ASSERT(!keyboard_get_event(&ev), "an extra event");

    // Five keys and three modifiers: Shift goes up, Ctrl and Alt go down before
    // the keys, held in the reply's order
    const keyboard_key_t five[5] = { KEYBOARD_KEY_W, KEYBOARD_KEY_A, KEYBOARD_KEY_S, KEYBOARD_KEY_D, KEYBOARD_KEY_SPACE };
    uint8_t mods = KEYBOARD_MODIFIER(KEYBOARD_KEY_LCTRL) | KEYBOARD_MODIFIER(KEYBOARD_KEY_LALT) | KEYBOARD_MODIFIER(KEYBOARD_KEY_RGUI);
    kbt_read(KBT_KB, mods, five, 5);
    n = kbt_drain(evs, 64);
    ASSERT_EQUAL_SIGNED(n, 9, "events for a five-key chord");
    ASSERT(!evs[0].pressed && evs[0].key == KEYBOARD_KEY_LSHIFT, "the release not first");
    ASSERT(evs[1].pressed && evs[1].key == KEYBOARD_KEY_LCTRL && evs[2].key == KEYBOARD_KEY_LALT &&
           evs[3].key == KEYBOARD_KEY_RGUI, "modifiers not pressed before the keys");
    for (int i = 0; i < 5; i++)
        ASSERT(evs[4 + i].pressed && evs[4 + i].key == five[i], "a chord key not pressed in order");
    keys = keyboard_get_keys(JOYPAD_PORT_2);
    for (int i = 0; i < 5; i++)
        ASSERT_EQUAL_HEX(keys.key[i], five[i], "held keys out of the reply's order");
    ASSERT_EQUAL_HEX(keys.modifiers, mods, "held modifiers");
    ASSERT(!keys.overflow, "overflow without excess keys");

    // More keys held than the reply holds: the five it holds count; the modifiers go up after the keys
    const keyboard_key_t five2[5] = { KEYBOARD_KEY_W, KEYBOARD_KEY_A, KEYBOARD_KEY_S, KEYBOARD_KEY_D, KEYBOARD_KEY_E };
    kbt_read(KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_EXCESS_KEYS, 0, five2, 5);
    n = kbt_drain(evs, 64);
    ASSERT_EQUAL_SIGNED(n, 5, "events in an overflowing read");
    ASSERT(!evs[0].pressed && evs[0].key == KEYBOARD_KEY_SPACE, "the key's release not before the modifiers'");
    ASSERT(!evs[1].pressed && evs[1].key == KEYBOARD_KEY_LCTRL, "Ctrl not released");
    ASSERT(evs[4].pressed && evs[4].key == KEYBOARD_KEY_E, "an overflowing read's key not used");
    keys = keyboard_get_keys(JOYPAD_PORT_2);
    ASSERT(keys.overflow, "no overflow");
    ASSERT_EQUAL_HEX(keys.key[4], KEYBOARD_KEY_E, "the fifth held key");
    kbt_read(KBT_KB, 0, NULL, 0);
    kbt_drain(evs, 64);

    // A media key: the compact read asks for a full read and its keys wait for it
    kbt_read(KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_FULL_READ, 0, (keyboard_key_t[]){ KEYBOARD_KEY_W }, 1);
    ASSERT(!keyboard_get_event(&ev), "keys taken from a compact read that asked for a full read");
    ASSERT_EQUAL_HEX(keyboard_full, 0x2, "no full read due");
    ASSERT(!keyboard_side_valid, "the side block kept with a full read due");
    ASSERT(keyboard_build_side(), "one full read didn't fit");
    ASSERT_EQUAL_HEX(keyboard_side_input[1 + 2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ, "port 2's full read");
    kbt_begin();
    kbt_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION, KBT_KB, 2,
        KEYBOARD_LED_CAPS_LOCK | KEYBOARD_LED_KANA, (keyboard_key_t[]){ KEYBOARD_KEY_W, KEYBOARD_KEY_MEDIA_PLAY_PAUSE }, 2, 0);
    kbt_end();
    n = kbt_drain(evs, 64);
    ASSERT(n == 2 && kbt_has(evs, n, KEYBOARD_KEY_W, true) && kbt_has(evs, n, KEYBOARD_KEY_MEDIA_PLAY_PAUSE, true),
           "a full read's keys not pressed");
    ASSERT_EQUAL_HEX(keyboard_get_keys(JOYPAD_PORT_2).key[1], KEYBOARD_KEY_MEDIA_PLAY_PAUSE, "the media key not held");
    ASSERT_EQUAL_HEX(keyboard_full, 0, "a full read still due after one");

    // Every page's keys in a full read: the modifiers among them as bits
    kbt_read_full(KBT_KB, (keyboard_key_t[]){ KEYBOARD_KEY_RCTRL, KEYBOARD_KEY_INTERNATIONAL3,
        KEYBOARD_KEY_VOLUME_INCREMENT, KEYBOARD_KEY_SLEEP, KEYBOARD_KEY_FN }, 5);
    n = kbt_drain(evs, 64);
    ASSERT_EQUAL_SIGNED(n, 7, "two releases and five presses");
    ASSERT(kbt_has(evs, n, KEYBOARD_KEY_RCTRL, true) && kbt_has(evs, n, KEYBOARD_KEY_INTERNATIONAL3, true) &&
           kbt_has(evs, n, KEYBOARD_KEY_VOLUME_INCREMENT, true) && kbt_has(evs, n, KEYBOARD_KEY_SLEEP, true) &&
           kbt_has(evs, n, KEYBOARD_KEY_FN, true), "page key presses");
    keys = keyboard_get_keys(JOYPAD_PORT_2);
    ASSERT_EQUAL_HEX(keys.key[3], KEYBOARD_KEY_FN, "the fourth held key");
    ASSERT_EQUAL_HEX(keys.key[4], KEYBOARD_KEY_NONE, "stale held keys");
    ASSERT_EQUAL_HEX(keys.modifiers, KEYBOARD_MODIFIER(KEYBOARD_KEY_RCTRL), "a full read's modifier");

    // A full read with more than five keys besides the modifiers overflows
    kbt_read_full(KBT_KB, (keyboard_key_t[]){ KEYBOARD_KEY_LCTRL, KEYBOARD_KEY_Q, KEYBOARD_KEY_W, KEYBOARD_KEY_E,
        KEYBOARD_KEY_R, KEYBOARD_KEY_T, KEYBOARD_KEY_Y }, 7);
    keys = keyboard_get_keys(JOYPAD_PORT_2);
    ASSERT(keys.overflow && keys.key[4] == KEYBOARD_KEY_T, "a full read's sixth key");
    kbt_read(KBT_KB, 0, NULL, 0);
    kbt_drain(evs, 64);

    // LEDs set while a full read with the old ones is in flight wait for the next
    keyboard_set_leds(JOYPAD_PORT_2, KEYBOARD_LED_NUM_LOCK);
    ASSERT_EQUAL_HEX(keyboard_full, 0x2, "no full read due for the LEDs");
    kbt_begin();
    kbt_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION, KBT_KB, 2,
        KEYBOARD_LED_CAPS_LOCK | KEYBOARD_LED_KANA, NULL, 0, 0);
    kbt_end();
    ASSERT_EQUAL_HEX(keyboard_full, 0x2, "LEDs dropped by a full read that didn't carry them");
    kbt_begin();
    kbt_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION, KBT_KB, 2, KEYBOARD_LED_NUM_LOCK, NULL, 0, 0);
    kbt_end();
    ASSERT_EQUAL_HEX(keyboard_full, 0, "a full read still due after carrying the LEDs");

    // A mouse joins: read with 0x2C from then on
    kbt_read(KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_MOUSE, 0, NULL, 0);
    ASSERT(keyboard_mouse_is_connected(JOYPAD_PORT_2), "no mouse");
    ASSERT(!keyboard_main_valid, "the main block kept after a mouse joined");
    keyboard_build_main(false);
    ASSERT_EQUAL_HEX(keyboard_main_input[2], 12, "keyboard and mouse receive length");
    ASSERT_EQUAL_HEX(keyboard_main_input[3], JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ, "keyboard and mouse command");
    ASSERT_EQUAL_HEX(keyboard_main_input[18], 0xFE, "main block with a mouse not closed");

    // Its motion and wheel add up until taken; a click between two calls is seen
    kbt_begin();
    kbt_mouse(KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_MOUSE, 0, KEYBOARD_MOUSE_BUTTON_LEFT, -1, 1, 300, -5,
        (keyboard_key_t[]){ KEYBOARD_KEY_W }, 1);
    kbt_end();
    kbt_begin();
    kbt_mouse(KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_MOUSE, 0, 0, 3, -4, 2047, -2048, (keyboard_key_t[]){ KEYBOARD_KEY_W }, 1);
    kbt_end();
    ASSERT(keyboard_get_event(&ev) && ev.pressed && ev.key == KEYBOARD_KEY_W, "a key beside the mouse not pressed");
    mouse = keyboard_get_mouse(JOYPAD_PORT_2);
    ASSERT_EQUAL_SIGNED(mouse.x, 300 + 2047, "motion right");
    ASSERT_EQUAL_SIGNED(mouse.y, -5 - 2048, "motion down");
    ASSERT_EQUAL_SIGNED(mouse.wheel, 2, "wheel");
    ASSERT_EQUAL_SIGNED(mouse.hwheel, -3, "horizontal wheel");
    ASSERT_EQUAL_HEX(mouse.buttons, 0, "buttons held");
    ASSERT_EQUAL_HEX(mouse.pressed, KEYBOARD_MOUSE_BUTTON_LEFT, "a click missed");
    mouse = keyboard_get_mouse(JOYPAD_PORT_2);
    ASSERT(mouse.x == 0 && mouse.y == 0 && mouse.wheel == 0 && mouse.hwheel == 0 && mouse.pressed == 0,
           "motion not taken");
    // The other ends: x left and y down at their limits, each sign on its own
    kbt_begin();
    kbt_mouse(KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_MOUSE, 0, 0, 0, 3, -2048, 2047, (keyboard_key_t[]){ KEYBOARD_KEY_W }, 1);
    kbt_end();
    kbt_begin();
    kbt_mouse(KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_MOUSE, 0, 0, 0, 0, -1, 1, (keyboard_key_t[]){ KEYBOARD_KEY_W }, 1);
    kbt_end();
    mouse = keyboard_get_mouse(JOYPAD_PORT_2);
    ASSERT_EQUAL_SIGNED(mouse.x, -2048 - 1, "motion left");
    ASSERT_EQUAL_SIGNED(mouse.y, 2047 + 1, "motion down, large");
    ASSERT_EQUAL_SIGNED(mouse.hwheel, 3, "horizontal wheel right");

    // The mouse leaves: 0x2B again
    kbt_read(KBT_KB, 0, (keyboard_key_t[]){ KEYBOARD_KEY_W }, 1);
    ASSERT(!keyboard_mouse_is_connected(JOYPAD_PORT_2), "a mouse kept");
    keyboard_build_main(false);
    ASSERT_EQUAL_HEX(keyboard_main_input[3], JOYBUS_COMMAND_ID_BRF_KEYBOARD_COMPACT_READ, "0x2C without a mouse");

    // Once known, a reply with an identity, an old version's, or none: ignored, the keys and the keyboard kept
    keyboard_full |= 1 << JOYPAD_PORT_2;
    kbt_begin();
    kbt_full(0x05, 0x00, 0x02, 0, 0, NULL, 0, 0);
    kbt_end();
    kbt_begin();
    kbt_full(JOYBUS_BRF_MARKER, 1, 0, 0, 0, (keyboard_key_t[]){ KEYBOARD_KEY_Z }, 1, 0);
    kbt_end();
    for (int i = 0; i < KEYBOARD_PROBES + 1; i++) {
        kbt_begin();
        kbt_compact(0, 0, NULL, 0, 0x80);
        kbt_end();
    }
    ASSERT(!keyboard_get_event(&ev), "an ignored reply gave an event");
    ASSERT(keyboard_is_connected(JOYPAD_PORT_2), "an ignored reply dropped the keyboard");
    ASSERT_EQUAL_HEX(keyboard_get_keys(JOYPAD_PORT_2).key[0], KEYBOARD_KEY_W, "held keys lost");

    // Pulled out with keys held: they're released, and later replies are ignored
    kbt_disconnect(JOYPAD_PORT_2);
    ASSERT(!keyboard_is_connected(JOYPAD_PORT_2), "still connected");
    ASSERT_EQUAL_SIGNED(keyboard_get_layout(JOYPAD_PORT_2), KEYBOARD_LAYOUT_NONE, "a layout after unplugging");
    ASSERT(keyboard_get_event(&ev) && !ev.pressed && ev.key == KEYBOARD_KEY_W, "no release on unplugging");
    ASSERT_EQUAL_HEX(keyboard_get_keys(JOYPAD_PORT_2).key[0], KEYBOARD_KEY_NONE, "keys held after unplugging");
    ASSERT_EQUAL_HEX(keyboard_compact | keyboard_full | keyboard_probing, 0, "an unplugged port read");
    kbt_read(KBT_KB, 0, (keyboard_key_t[]){ KEYBOARD_KEY_Z }, 1);
    ASSERT(!keyboard_get_event(&ev), "a reply used after unplugging");

    // Not keyboards: a device answering with its identity (the adapter in classic
    // mode), and the version before HID usages, aren't read again
    kbt_connect(JOYPAD_PORT_2);
    keyboard_side_valid = true;
    kbt_begin();
    kbt_full(0x05, 0x00, 0x02, 0, 0, NULL, 0, 0);
    kbt_end();
    ASSERT_EQUAL_HEX(keyboard_probing, 0, "an identity reply still probed");
    ASSERT(!keyboard_side_valid, "the block kept for a port not read");
    kbt_connect(JOYPAD_PORT_2);
    kbt_begin();
    kbt_full(JOYBUS_BRF_MARKER, 1, 0, 0, 0, (keyboard_key_t[]){ KEYBOARD_KEY_Z }, 1, 0);
    kbt_end();
    ASSERT_EQUAL_HEX(keyboard_probing, 0, "version 1 still probed");
    ASSERT(!keyboard_is_connected(JOYPAD_PORT_2) && !keyboard_get_event(&ev), "version 1 taken for a keyboard");

    // A Randnet keyboard doesn't answer: after three tries it isn't read
    kbt_connect(JOYPAD_PORT_2);
    for (int i = 0; i < KEYBOARD_PROBES - 1; i++) {
        kbt_begin();
        kbt_full(0, 0, 0, 0, 0, NULL, 0, 0x80);
        kbt_end();
    }
    ASSERT_EQUAL_HEX(keyboard_probing, 1 << JOYPAD_PORT_2, "given up before three unanswered reads");
    kbt_begin();
    kbt_full(0, 0, 0, 0, 0, NULL, 0, 0x80);
    kbt_end();
    ASSERT_EQUAL_HEX(keyboard_probing, 0, "still probed after three unanswered reads");

    // A keyboard that misses its first read still counts
    kbt_connect(JOYPAD_PORT_2);
    kbt_begin();
    kbt_full(0, 0, 0, 0, 0, NULL, 0, 0x80);
    kbt_end();
    kbt_read_full(KBT_KB, NULL, 0);
    ASSERT(keyboard_is_connected(JOYPAD_PORT_2), "a keyboard that missed a read not taken");

    // Before its first report, a keyboard's status has no keyboard bit: its identity says it's one
    kbt_disconnect(JOYPAD_PORT_2);
    kbt_connect(JOYPAD_PORT_2);
    kbt_read_full(0, NULL, 0);
    ASSERT(keyboard_is_connected(JOYPAD_PORT_2) && (keyboard_compact & (1 << JOYPAD_PORT_2)), "a keyboard without its bit dropped");
    kbt_read(0, 0, (keyboard_key_t[]){ KEYBOARD_KEY_Q }, 1);
    ASSERT(keyboard_get_event(&ev) && ev.pressed && ev.key == KEYBOARD_KEY_Q, "a compact read without the keyboard bit ignored");

    // An adapter with an older version of the full read isn't a keyboard
    kbt_disconnect(JOYPAD_PORT_2);
    kbt_connect(JOYPAD_PORT_2);
    kbt_begin();
    kbt_full(JOYBUS_BRF_MARKER, 2, 0, 0, 0, (keyboard_key_t[]){ KEYBOARD_KEY_LSHIFT, KEYBOARD_KEY_A }, 2, 0);
    kbt_end();
    ASSERT(!keyboard_is_connected(JOYPAD_PORT_2), "version 2 taken for a keyboard");
    ASSERT_EQUAL_HEX(keyboard_probing | keyboard_compact | keyboard_full, 0, "version 2 still read");
    kbt_disconnect(JOYPAD_PORT_2);
    kbt_drain(evs, 64);

    // Four probes: two full reads per block, in turn
    JOYPAD_PORT_FOREACH (port)
        kbt_connect(port);
    keyboard_side_rotation = 0;
    ASSERT(!keyboard_build_side(), "four full reads fit one block");
    ASSERT_EQUAL_HEX(keyboard_side_reads, 0x3, "ports 1 and 2 not read first");
    ASSERT_EQUAL_HEX(keyboard_side_input[58], 0xFE, "first block not closed");
    ASSERT(!keyboard_build_side(), "four full reads fit one block");
    ASSERT_EQUAL_HEX(keyboard_side_reads, 0xC, "ports 3 and 4 not read next");
    ASSERT_EQUAL_HEX(keyboard_side_input[0], 0, "port 1 not skipped next");
    ASSERT_EQUAL_HEX(keyboard_side_input[2 + 2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ, "port 3 not read next");

    // Three: each read two blocks in three
    kbt_disconnect(JOYPAD_PORT_4);
    keyboard_side_rotation = 0;
    int reads[JOYPAD_PORT_COUNT] = {0};
    for (int b = 0; b < 3; b++)
    {
        keyboard_build_side();
        JOYPAD_PORT_FOREACH (port)
            if (keyboard_side_reads & (1 << port)) reads[port]++;
    }
    ASSERT(reads[0] == 2 && reads[1] == 2 && reads[2] == 2 && reads[3] == 0, "three probes not read in turn");
    JOYPAD_PORT_FOREACH (port)
        kbt_disconnect(port);

    // A keyboard and mouse beside a gamepad: probed on the controller's port, then read
    // compact beside the joypads' read, which has the controller's command there
    kbt_connect_pad(JOYPAD_PORT_2);
    kbt_begin();
    kbt_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION, KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_MOUSE,
        0, 0, NULL, 0, 0);
    kbt_end();
    ASSERT(keyboard_is_connected(JOYPAD_PORT_2) && keyboard_mouse_is_connected(JOYPAD_PORT_2), "devices beside a gamepad");
    keyboard_build_main(false);
    ASSERT_EQUAL_HEX(keyboard_main_reads, 0, "a controller port's compact read with the joypads");
    ASSERT(keyboard_build_side(), "a controller port's compact read didn't fit");
    ASSERT_EQUAL_HEX(keyboard_side_reads, 0x2, "a controller port's compact read not beside the joypads'");
    ASSERT_EQUAL_HEX(keyboard_side_input[1 + 2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ, "the side block's 0x2C");

    // Both leave: the port isn't read until detection finds a change
    kbt_read(0, 0, NULL, 0);
    ASSERT(!keyboard_is_connected(JOYPAD_PORT_2) && !keyboard_mouse_is_connected(JOYPAD_PORT_2), "devices kept");
    ASSERT_EQUAL_HEX(keyboard_compact, 0, "a port with neither still read");
    kbt_disconnect(JOYPAD_PORT_2);

    // A gamepad alone answering the full read isn't read
    kbt_connect_pad(JOYPAD_PORT_2);
    kbt_begin();
    kbt_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION, 0, 0, 0, NULL, 0, 0);
    kbt_end();
    ASSERT_EQUAL_HEX(keyboard_probing | keyboard_compact | keyboard_full, 0, "a gamepad alone read");
    kbt_disconnect(JOYPAD_PORT_2);

    // While the queue is full, newer events are dropped
    kbt_connect(JOYPAD_PORT_2);
    kbt_read_full(KBT_KB, NULL, 0);
    kbt_drain(evs, 64);
    for (int i = 0; i < 70; i++)
        kbt_read(KBT_KB, 0, (keyboard_key_t[]){ KEYBOARD_KEY_A }, (i & 1) ? 0 : 1);
    int count = 0;
    while (keyboard_get_event(&ev))
    {
        ASSERT_EQUAL_SIGNED(ev.pressed, !(count & 1), "events out of order");
        count++;
    }
    ASSERT_EQUAL_SIGNED(count, KEYBOARD_EVENT_QUEUE_SIZE - 1, "queue size");

    // Paused: held keys are released, a known keyboard isn't read, and a reply in flight is ignored
    kbt_drain(evs, 64);
    kbt_read(KBT_KB, KEYBOARD_MODIFIER(KEYBOARD_KEY_LALT), (keyboard_key_t[]){ KEYBOARD_KEY_A }, 1);
    kbt_drain(evs, 64);
    keyboard_pause(KEYBOARD_PAUSE_ALL);
    n = kbt_drain(evs, 64);
    ASSERT(n == 2 && kbt_has(evs, n, KEYBOARD_KEY_A, false) && kbt_has(evs, n, KEYBOARD_KEY_LALT, false),
           "pausing didn't release A and Alt");
    keyboard_build_main(false);
    ASSERT_EQUAL_HEX(keyboard_main_reads, 0, "a known keyboard read while paused");
    kbt_read(KBT_KB, 0, (keyboard_key_t[]){ KEYBOARD_KEY_B }, 1);
    ASSERT(!keyboard_get_event(&ev), "a reply used while paused");
    ASSERT(keyboard_is_connected(JOYPAD_PORT_2), "pausing disconnected the keyboard");
    keyboard_set_leds(JOYPAD_PORT_2, 0);
    keyboard_build_side();
    ASSERT_EQUAL_HEX(keyboard_side_reads, 0, "an LED change sent while paused");

    // A keyboard that connects while paused is probed until it answers, then not read
    kbt_connect(JOYPAD_PORT_3);
    keyboard_build_side();
    ASSERT_EQUAL_HEX(keyboard_side_reads, 1 << JOYPAD_PORT_3, "a new keyboard not probed while paused");
    kbt_begin();
    kbt_skip();
    kbt_full(JOYBUS_BRF_MARKER, JOYBUS_BRF_KEYBOARD_READ_VERSION, KBT_KB, KEYBOARD_LAYOUT_UK, 0, NULL, 0, 0);
    kbt_end();
    ASSERT(keyboard_is_connected(JOYPAD_PORT_3), "a new keyboard not known after answering while paused");
    ASSERT_EQUAL_SIGNED(keyboard_get_layout(JOYPAD_PORT_3), KEYBOARD_LAYOUT_UK, "a new keyboard's layout while paused");
    keyboard_build_main(false);
    keyboard_build_side();
    ASSERT_EQUAL_HEX(keyboard_main_reads | keyboard_side_reads, 0, "a new keyboard still read after answering while paused");

    // Resuming reads them both again, the LEDs go, and held keys arrive as presses
    keyboard_pause(0);
    keyboard_build_main(false);
    ASSERT_EQUAL_HEX(keyboard_main_reads, (1 << JOYPAD_PORT_2) | (1 << JOYPAD_PORT_3), "keyboards not read after resuming");
    keyboard_build_side();
    ASSERT_EQUAL_HEX(keyboard_side_reads, 1 << JOYPAD_PORT_2, "the LEDs not sent after resuming");
    kbt_read(KBT_KB, 0, (keyboard_key_t[]){ KEYBOARD_KEY_B }, 1);
    ASSERT(keyboard_get_event(&ev) && ev.pressed && ev.key == KEYBOARD_KEY_B, "no press for a key held on resuming");

    // Only the keys paused: port 2, with a mouse now, is still read with 0x2C, its keys skipped;
    // port 3, a keyboard alone, isn't read
    kbt_read(KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_MOUSE, 0, NULL, 0);
    kbt_drain(evs, 64);
    keyboard_pause(KEYBOARD_PAUSE_KEYS);
    keyboard_build_main(false);
    ASSERT_EQUAL_HEX(keyboard_main_reads, 1 << JOYPAD_PORT_2, "reads with only the keys paused");
    ASSERT_EQUAL_HEX(keyboard_main_input[1 + 2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ, "the mouse not read with the keys paused");
    kbt_begin();
    kbt_mouse(KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_MOUSE, 0, 0, 0, 0, 5, 0, (keyboard_key_t[]){ KEYBOARD_KEY_W }, 1);
    kbt_end();
    ASSERT(!keyboard_get_event(&ev), "keys taken while the keys were paused");
    ASSERT_EQUAL_SIGNED(keyboard_get_mouse(JOYPAD_PORT_2).x, 5, "the mouse's motion lost while the keys were paused");

    // Only the mouse paused: 0x2B, and a 0x2C reply in flight gives its keys and no motion
    keyboard_pause(KEYBOARD_PAUSE_MOUSE);
    keyboard_build_main(false);
    ASSERT_EQUAL_HEX(keyboard_main_reads, (1 << JOYPAD_PORT_2) | (1 << JOYPAD_PORT_3), "reads with only the mouse paused");
    ASSERT_EQUAL_HEX(keyboard_main_input[1 + 2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_COMPACT_READ, "0x2C sent with the mouse paused");
    kbt_begin();
    kbt_mouse(KBT_KB | JOYBUS_BRF_KEYBOARD_STATUS_MOUSE, 0, KEYBOARD_MOUSE_BUTTON_LEFT, 1, -1, 7, 0, (keyboard_key_t[]){ KEYBOARD_KEY_W }, 1);
    kbt_end();
    ASSERT(keyboard_get_event(&ev) && ev.pressed && ev.key == KEYBOARD_KEY_W, "keys not taken with only the mouse paused");
    mouse = keyboard_get_mouse(JOYPAD_PORT_2);
    ASSERT(mouse.x == 0 && mouse.wheel == 0 && mouse.hwheel == 0 && mouse.buttons == 0 && mouse.pressed == 0,
           "a paused mouse moved");

    // One port paused: the others read
    keyboard_pause(0);
    keyboard_pause_port(JOYPAD_PORT_3, KEYBOARD_PAUSE_ALL);
    keyboard_build_main(false);
    ASSERT_EQUAL_HEX(keyboard_main_reads, 1 << JOYPAD_PORT_2, "a port's pause not its own");
    ASSERT_EQUAL_HEX(keyboard_main_input[1 + 2], JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ, "port 2's mouse paused with port 3");
    keyboard_pause_port(JOYPAD_PORT_3, 0);
    keyboard_build_main(false);
    ASSERT_EQUAL_HEX(keyboard_main_reads, (1 << JOYPAD_PORT_2) | (1 << JOYPAD_PORT_3), "port 3 not read after resuming it");
    kbt_disconnect(JOYPAD_PORT_3);
    kbt_disconnect(JOYPAD_PORT_2);

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
    ASSERT_EQUAL_HEX(KEYBOARD_MODIFIER(KEYBOARD_KEY_RGUI), 0x80, "Right GUI's bit");
    ASSERT_EQUAL_HEX(KEYBOARD_MODIFIER_CTRL, 0x11, "either Ctrl");
}
