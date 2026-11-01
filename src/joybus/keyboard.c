/**
 * @file keyboard.c
 * @brief Keyboard Subsystem
 * @ingroup keyboard
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "debug.h"
#include "interrupt.h"
#include "n64sys.h"
#include "vi.h"
#include "keyboard.h"
#include "joybus_commands.h"
#include "joypad_internal.h"
#include "../entropy_internal.h"

/**
 * @addtogroup keyboard
 * @{
 */

/** @brief Convenience macro to ensure Keyboard subsystem is initialized. */
#define ASSERT_KEYBOARD_INITIALIZED() \
    assertf(keyboard_init_refcount > 0, "keyboard_init() was not called")

/** @brief Convenience macro to validate a port number */
#define ASSERT_KEYBOARD_PORT_VALID(port) \
    assert((port) >= 0 && (port) < JOYPAD_PORT_COUNT)

/** @brief How many events the queue holds, plus one. */
#define KEYBOARD_EVENT_QUEUE_SIZE 32

/**
 * @brief How many full reads a device that may have a keyboard can leave
 *        unanswered before it isn't sent one again: a keyboard plugged in a
 *        moment ago can miss one, and a Randnet keyboard or a controller
 *        misses them all.
 */
#define KEYBOARD_PROBES 3

/** @brief A command's size in a Joybus block: its lengths, then what it sends and receives. */
#define KEYBOARD_COMMAND_SIZE(type) (JOYBUS_COMMAND_METADATA_SIZE + sizeof(type))

/** @brief The bytes a Joybus block's commands can fill: it ends with 0xFE before its status byte. */
#define KEYBOARD_BLOCK_COMMANDS (JOYBUS_BLOCK_SIZE - 2)

// A keyboard and mouse's compact read is the longest command a port has in the joypads' read
_Static_assert(KEYBOARD_COMMAND_SIZE(joybus_cmd_gcn_controller_read_port_t) <=
               KEYBOARD_COMMAND_SIZE(joybus_cmd_brf_keyboard_mouse_read_port_t), "a joypad's read is longer");
_Static_assert(JOYPAD_PORT_COUNT * KEYBOARD_COMMAND_SIZE(joybus_cmd_brf_keyboard_mouse_read_port_t) <=
               KEYBOARD_BLOCK_COMMANDS, "four compact reads don't fit in a block");

/** @brief A port's keyboard and mouse state, kept under interrupt. */
typedef struct keyboard_port_hot_s
{
    /** @brief The held keys besides the modifiers, as of the last read; unused entries are #KEYBOARD_KEY_NONE. */
    keyboard_key_t key[KEYBOARD_KEYS_MAX];
    /** @brief How many keys are held. */
    uint8_t count;
    /** @brief The held modifiers (@ref KEYBOARD_MODIFIER). */
    uint8_t modifiers;
    /** @brief The last read said more keys were held than it reported. */
    bool overflow;
    /** @brief The LEDs sent with each full read (@ref KEYBOARD_LED). */
    uint8_t leds;
    /** @brief The adapter's settings byte, as of the last full read. */
    uint8_t settings;
    /** @brief Full reads left to answer before the port isn't sent one, until its device has answered one. */
    uint8_t probes;
    /** @brief The mouse's buttons, as of the last read. */
    uint8_t buttons;
    /** @brief The mouse's buttons that went down since the program last took them. */
    uint8_t pressed;
    /** @brief The wheel's clicks since the program last took them. */
    int16_t wheel;
    /** @brief The horizontal wheel's clicks since the program last took them. */
    int16_t hwheel;
    /** @brief The mouse's motion right since the program last took it. */
    int32_t x;
    /** @brief The mouse's motion down since the program last took it. */
    int32_t y;
} keyboard_port_hot_t;

/** @brief Refcount of #keyboard_init vs #keyboard_close calls. */
static int keyboard_init_refcount = 0;
/** @brief Ports sent a full read until they answer: each device that may have a keyboard or mouse, since it connected. */
static volatile uint8_t keyboard_probing = 0;
/** @brief Ports read with a compact read on each frame: their full read answered, with a keyboard or mouse. */
static volatile uint8_t keyboard_compact = 0;
/** @brief Ports due a full read: the LEDs changed, or a compact read asked for one. */
static volatile uint8_t keyboard_full = 0;
/** @brief Ports whose device has a controller's identity: the joypads' read has a command there. */
static volatile uint8_t keyboard_pads = 0;
/** @brief The ports a keyboard is connected to. */
static volatile uint8_t keyboard_connected = 0;
/** @brief The ports a mouse is connected to, beside a keyboard or a gamepad. */
static volatile uint8_t keyboard_mice = 0;
/** @brief Each port's keyboard and mouse state. */
static volatile keyboard_port_hot_t keyboard_ports[JOYPAD_PORT_COUNT];
/** @brief The event queue (a ring buffer), written under interrupt. */
static keyboard_event_t keyboard_events[KEYBOARD_EVENT_QUEUE_SIZE];
/** @brief The event queue's read index (read by #keyboard_has_events). */
volatile uint8_t __keyboard_events_ridx = 0;
/** @brief The event queue's write index (read by #keyboard_has_events). */
volatile uint8_t __keyboard_events_widx = 0;
/** @brief Ports whose keys are paused (#keyboard_pause, #keyboard_pause_port). */
static volatile uint8_t keyboard_paused_keys = 0;
/** @brief Ports whose mouse is paused. */
static volatile uint8_t keyboard_paused_mice = 0;

/** @brief Is the read with the joypads in flight? */
static volatile bool keyboard_main_pending = false;
/** @brief Is #keyboard_main_input current? */
static volatile bool keyboard_main_valid = false;
/** @brief Does #keyboard_main_input read the joypads too? */
static bool keyboard_main_joypads = false;
/** @brief The ports #keyboard_main_input reads a keyboard or mouse on. */
static uint8_t keyboard_main_reads = 0;
/** @brief The ports #keyboard_main_input reads a GameCube controller on. */
static uint8_t keyboard_main_gcn = 0;
/** @brief Where each GameCube controller's rumble byte is in #keyboard_main_input. */
static uint8_t keyboard_main_rumble[JOYPAD_PORT_COUNT];
/** @brief The Joybus block that reads the keyboards inside the joypads' read. */
static uint8_t keyboard_main_input[JOYBUS_BLOCK_SIZE];
/** @brief Is the read beside the joypads' in flight? */
static volatile bool keyboard_side_pending = false;
/** @brief Is #keyboard_side_input current? */
static volatile bool keyboard_side_valid = false;
/** @brief The ports #keyboard_side_input reads. */
static uint8_t keyboard_side_reads = 0;
/** @brief The port the full reads start from when they don't all fit in a block. */
static uint8_t keyboard_side_rotation = 0;
/** @brief The Joybus block of the reads that can't join the joypads': full reads, and compact reads beside a gamepad. */
static uint8_t keyboard_side_input[JOYBUS_BLOCK_SIZE];

/** @brief The ports read with a compact read now: those with a keyboard or mouse that isn't paused. */
static uint8_t keyboard_compact_now(void)
{
    return keyboard_compact & ((keyboard_connected & ~keyboard_paused_keys) | (keyboard_mice & ~keyboard_paused_mice));
}

/** @brief The ports sent a full read now: those due one whose keys aren't paused, and the devices not yet known. */
static uint8_t keyboard_full_now(void)
{
    return keyboard_probing | (keyboard_full & ~keyboard_paused_keys);
}

/** @brief Rebuild both blocks on the next vertical blank. */
static void keyboard_invalidate(void)
{
    keyboard_main_valid = false;
    keyboard_side_valid = false;
}

/** @brief Queue an event, under interrupt. While the queue is full, the event is dropped. */
static void keyboard_event_push(int port, keyboard_key_t key, bool pressed)
{
    uint8_t widx = __keyboard_events_widx;
    uint8_t next = (widx + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    if (next == __keyboard_events_ridx) return;
    keyboard_events[widx] = (keyboard_event_t){ .key = key, .port = port, .pressed = pressed };
    __keyboard_events_widx = next;
}

/** @brief Is a key among the first @p count of a set of keys? */
static bool keyboard_keys_contain(const keyboard_key_t *keys, int count, keyboard_key_t key)
{
    for (int i = 0; i < count; i++)
        if (keys[i] == key) return true;
    return false;
}

/** @brief Make a port's held keys the @p count keys in @p now and @p modifiers, queueing a release or press for each change. */
static void keyboard_port_update(int port, const keyboard_key_t *now, int count, uint8_t modifiers)
{
    volatile keyboard_port_hot_t *state = &keyboard_ports[port];
    keyboard_key_t held[KEYBOARD_KEYS_MAX];
    int held_count = state->count;
    memcpy(held, (const void *)state->key, held_count * sizeof(keyboard_key_t));
    uint8_t up = state->modifiers & ~modifiers, down = modifiers & ~state->modifiers;

    // A read's releases come before its presses, and its modifiers go down
    // before its keys and up after them
    for (int i = 0; i < held_count; i++)
        if (!keyboard_keys_contain(now, count, held[i]))
            keyboard_event_push(port, held[i], false);
    for (int m = 0; up; m++, up >>= 1)
        if (up & 1) keyboard_event_push(port, KEYBOARD_KEY_LCTRL + m, false);
    for (int m = 0; down; m++, down >>= 1)
        if (down & 1) keyboard_event_push(port, KEYBOARD_KEY_LCTRL + m, true);
    for (int i = 0; i < count; i++)
        if (!keyboard_keys_contain(held, held_count, now[i]))
            keyboard_event_push(port, now[i], true);

    memcpy((void *)state->key, now, count * sizeof(keyboard_key_t));
    for (int i = count; i < held_count; i++) state->key[i] = KEYBOARD_KEY_NONE;
    state->count = count;
    state->modifiers = modifiers;
}

/** @brief Take a read's keys as a port's held keys. */
static void keyboard_port_set(int port, const keyboard_key_t *now, int count, uint8_t modifiers, bool overflow)
{
    volatile keyboard_port_hot_t *state = &keyboard_ports[port];
    state->overflow = overflow;

    // Most reads hold the keys the last one did: nothing to queue
    if (modifiers == state->modifiers && count == state->count) {
        int same = 0;
        while (same < count && now[same] == state->key[same]) same++;
        if (same == count) return;
    }
    keyboard_port_update(port, now, count, modifiers);
}

/** @brief Release every key held on a port's keyboard. */
static void keyboard_port_release_keys(int port)
{
    static const keyboard_key_t none[1] = { KEYBOARD_KEY_NONE };
    keyboard_port_update(port, none, 0, 0);
    keyboard_ports[port].overflow = false;
}

/** @brief Release every key held on a port's keyboard, and its mouse's buttons. */
static void keyboard_port_release(int port)
{
    keyboard_port_release_keys(port);
    keyboard_ports[port].buttons = 0;
}

/**
 * @brief Note the devices a port's read says it has, letting go of what one that left held.
 *
 * A port with the keyboard's identity has a keyboard, whatever its status
 * says: BlueRetroForever sets the bit at the keyboard's first report, a few
 * milliseconds after the port takes the identity.
 *
 * @return The status, with the keyboard's bit for a port with its identity.
 */
static uint8_t keyboard_port_devices(int port, uint8_t status)
{
    uint8_t bit = 1 << port;
    if (!(keyboard_pads & bit)) status |= JOYBUS_BRF_KEYBOARD_STATUS_KEYBOARD;
    if (status & JOYBUS_BRF_KEYBOARD_STATUS_KEYBOARD)
        keyboard_connected |= bit;
    else if (keyboard_connected & bit) {
        keyboard_connected &= ~bit;
        keyboard_port_release(port);
    }
    // A mouse's port is read with 0x2C
    if (!(status & JOYBUS_BRF_KEYBOARD_STATUS_MOUSE) != !(keyboard_mice & bit)) {
        keyboard_mice ^= bit;
        keyboard_ports[port].buttons = 0;
        keyboard_invalidate();
    }
    // A port that has neither is read again only once detection finds a change
    if (!(status & (JOYBUS_BRF_KEYBOARD_STATUS_KEYBOARD | JOYBUS_BRF_KEYBOARD_STATUS_MOUSE)) &&
        (keyboard_compact & bit)) {
        keyboard_compact &= ~bit;
        keyboard_invalidate();
    }
    return status;
}

/**
 * @brief Process a port's full read reply, under interrupt.
 *
 * Until a port's device has answered with the marker, a reply without it, or
 * no reply #KEYBOARD_PROBES times, means it has nothing for this subsystem,
 * and the port isn't read again until a device connects there. Once it has,
 * a reply without the marker leaves the keys as they were, until detection
 * reports the change: BlueRetroForever goes silent while changing a port.
 */
static void keyboard_port_full_read(int port, const joybus_cmd_brf_keyboard_read_port_t *cmd, bool error)
{
    volatile keyboard_port_hot_t *state = &keyboard_ports[port];
    uint8_t bit = 1 << port;
    // A port paused or gone since the block was sent
    if (!(keyboard_full_now() & bit)) return;
    if (error || cmd->recv.marker != JOYBUS_BRF_MARKER ||
        cmd->recv.version != JOYBUS_BRF_KEYBOARD_READ_VERSION)
    {
        if ((keyboard_probing & bit) && (!error || --state->probes == 0))
        {
            keyboard_probing &= ~bit;
            keyboard_invalidate();
        }
        return;
    }

    uint8_t probing = keyboard_probing, compact = keyboard_compact, full = keyboard_full;
    keyboard_probing &= ~bit;
    keyboard_compact |= bit;
    // LEDs changed since the block was built go with the next one
    if (cmd->send.leds == state->leds) keyboard_full &= ~bit;
    state->settings = cmd->recv.settings;
    uint8_t status = keyboard_port_devices(port, cmd->recv.status);
    if (probing != keyboard_probing || compact != keyboard_compact || full != keyboard_full)
        keyboard_invalidate();
    // A keyboard found while its port's keys are paused gives none until they resume
    if (!(status & JOYBUS_BRF_KEYBOARD_STATUS_KEYBOARD) || (keyboard_paused_keys & bit)) return;

    // The modifiers come first as keys: the reply's order is kept for the rest
    keyboard_key_t now[KEYBOARD_KEYS_MAX];
    int count = 0;
    uint8_t modifiers = 0;
    bool overflow = status & JOYBUS_BRF_KEYBOARD_STATUS_EXCESS_KEYS;
    for (int i = 0; i < JOYBUS_BRF_KEYBOARD_READ_KEYS && cmd->recv.keys[i]; i++)
    {
        keyboard_key_t key = cmd->recv.keys[i];
        if (KEYBOARD_KEY_IS_MODIFIER(key)) modifiers |= KEYBOARD_MODIFIER(key);
        else if (count < KEYBOARD_KEYS_MAX) now[count++] = key;
        else overflow = true;
    }
    keyboard_port_set(port, now, count, modifiers, overflow);
}

/** @brief Process a port's compact read reply (0x2B or 0x2C), under interrupt. */
static void keyboard_port_compact_read(int port, const uint8_t *cmd, bool error)
{
    // No answer leaves the keys as they were, until detection reports the
    // change; the port may also have been paused or gone since
    if (error || !(keyboard_compact_now() & (1 << port))) return;

    const joybus_cmd_brf_keyboard_compact_read_port_t *kb = (const void *)cmd;
    const joybus_cmd_brf_keyboard_mouse_read_port_t *mouse = (const void *)cmd;
    const uint8_t *keys = kb->recv.keys;
    uint8_t status = keyboard_port_devices(port, kb->recv.status);
    if (cmd[0] == JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ)
        keys = mouse->recv.keys;
    // A reply in flight when the mouse was paused brings it nothing
    if (cmd[0] == JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ && !(keyboard_paused_mice & (1 << port)))
    {
        volatile keyboard_port_hot_t *state = &keyboard_ports[port];
        uint8_t buttons = mouse->recv.buttons & JOYBUS_BRF_MOUSE_BUTTONS;
        // Each axis's 12 bits to the top of a word, and back down with its sign
        const uint8_t *m = mouse->recv.motion;
        state->x += (int32_t)(((uint32_t)m[0] << 24) | ((uint32_t)(m[1] & 0xF0) << 16)) >> 20;
        state->y += (int32_t)(((uint32_t)(m[1] & 0x0F) << 28) | ((uint32_t)m[2] << 20)) >> 20;
        state->wheel += (int8_t)mouse->recv.buttons >> JOYBUS_BRF_MOUSE_WHEEL_SHIFT;
        state->hwheel += (int8_t)mouse->recv.hwheel >> JOYBUS_BRF_MOUSE_WHEEL_SHIFT;
        state->pressed |= buttons & ~state->buttons;
        state->buttons = buttons;
    }

    // While the compact read isn't the whole story, the keys come with a full read
    if (status & JOYBUS_BRF_KEYBOARD_STATUS_FULL_READ) {
        if (!(keyboard_full & (1 << port))) {
            keyboard_full |= 1 << port;
            keyboard_side_valid = false;
        }
        return;
    }
    if (!(status & JOYBUS_BRF_KEYBOARD_STATUS_KEYBOARD) || (keyboard_paused_keys & (1 << port))) return;

    // The keys fill the slots from the first
    keyboard_key_t now[KEYBOARD_KEYS_MAX];
    int count = 0;
    while (count < KEYBOARD_KEYS_MAX && (now[count] = keys[count])) count++;
    keyboard_port_set(port, now, count, kb->recv.modifiers, status & JOYBUS_BRF_KEYBOARD_STATUS_EXCESS_KEYS);
}

/** @brief Write a port's compact read into a Joybus block. @return Its size in bytes. */
static size_t keyboard_write_compact_read(int port, uint8_t *dst)
{
    // The block is zeroed, so the reply's bytes need no writing
    bool mouse = keyboard_mice & ~keyboard_paused_mice & (1 << port);
    dst[JOYBUS_COMMAND_OFFSET_SEND_LEN] = 1;
    dst[JOYBUS_COMMAND_OFFSET_RECV_LEN] = mouse
        ? sizeof(((joybus_cmd_brf_keyboard_mouse_read_port_t *)0)->recv)
        : sizeof(((joybus_cmd_brf_keyboard_compact_read_port_t *)0)->recv);
    dst[JOYBUS_COMMAND_OFFSET_COMMAND_ID] = mouse
        ? JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ
        : JOYBUS_COMMAND_ID_BRF_KEYBOARD_COMPACT_READ;
    return JOYBUS_COMMAND_METADATA_SIZE + 1 + dst[JOYBUS_COMMAND_OFFSET_RECV_LEN];
}

/** @brief Write a port's full read, with its LEDs, into a Joybus block. @return Its size in bytes. */
static size_t keyboard_write_full_read(int port, uint8_t *dst)
{
    dst[JOYBUS_COMMAND_OFFSET_SEND_LEN] = sizeof(((joybus_cmd_brf_keyboard_read_port_t *)0)->send);
    dst[JOYBUS_COMMAND_OFFSET_RECV_LEN] = sizeof(((joybus_cmd_brf_keyboard_read_port_t *)0)->recv);
    dst[JOYBUS_COMMAND_OFFSET_COMMAND_ID] = JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ;
    dst[JOYBUS_COMMAND_OFFSET_COMMAND_ID + 1] = keyboard_ports[port].leds;
    return KEYBOARD_COMMAND_SIZE(joybus_cmd_brf_keyboard_read_port_t);
}

/**
 * @brief Build the block read with the joypads: theirs as joypad.c builds it,
 *        with a compact read for each keyboard port in place of its skip.
 *
 * Out of line and cold, as is #keyboard_build_side: a block is rebuilt only
 * when a device or the reads change, and inlined into the vertical blank
 * handler, the two made every frame's path miss several more cache lines.
 *
 * @param joypads Read the joypads too.
 */
__attribute__((noinline, cold))
static void keyboard_build_main(bool joypads)
{
    uint8_t *input = keyboard_main_input;
    uint8_t compact = keyboard_compact_now() & ~keyboard_pads;
    uint8_t reads = 0, gcn = 0;
    size_t i = 0;

    memset(input, 0, JOYBUS_BLOCK_SIZE);
    JOYPAD_PORT_FOREACH (port)
    {
        joypad_style_t style = joypads ? joypad_devices_hot[port].style : JOYPAD_STYLE_NONE;
        if (style == JOYPAD_STYLE_GCN) {
            // Most-compatible analog mode; the rumble byte is set on each frame
            input[i++] = sizeof(((joybus_cmd_gcn_controller_read_port_t *)0)->send);
            input[i++] = sizeof(((joybus_cmd_gcn_controller_read_port_t *)0)->recv);
            input[i] = JOYBUS_COMMAND_ID_GCN_CONTROLLER_READ;
            input[i + offsetof(joybus_cmd_gcn_controller_read_port_t, send.mode)] = 3;
            keyboard_main_rumble[port] = i + offsetof(joybus_cmd_gcn_controller_read_port_t, send.rumble);
            gcn |= 1 << port;
            i += sizeof(joybus_cmd_gcn_controller_read_port_t);
        } else if (style == JOYPAD_STYLE_N64 || style == JOYPAD_STYLE_MOUSE) {
            input[i++] = sizeof(((joybus_cmd_n64_controller_read_port_t *)0)->send);
            input[i++] = sizeof(((joybus_cmd_n64_controller_read_port_t *)0)->recv);
            input[i] = JOYBUS_COMMAND_ID_N64_CONTROLLER_READ;
            i += sizeof(joybus_cmd_n64_controller_read_port_t);
        } else if (compact & (1 << port)) {
            i += keyboard_write_compact_read(port, &input[i]);
            reads |= 1 << port;
        } else {
            i += JOYBUS_COMMAND_SKIP_SIZE;
        }
    }

    // Close out the Joybus operation block
    input[i] = 0xFE;
    input[JOYBUS_BLOCK_SIZE - 1] = 0x01;

    keyboard_main_joypads = joypads;
    keyboard_main_reads = reads;
    keyboard_main_gcn = gcn;
    keyboard_main_valid = true;
}

/**
 * @brief Build the block of the reads that can't join the joypads': the full
 *        reads, and the compact reads on ports with a controller's identity.
 *
 * Two full reads fit in a block. With more due, the block has two of them,
 * starting after the last port the block before gave one to, so each comes in
 * turn, and the compact reads that still fit.
 *
 * @return true if the block has every read due, so it can be kept for the next frame.
 */
__attribute__((noinline, cold))
static bool keyboard_build_side(void)
{
    uint8_t *input = keyboard_side_input;
    uint8_t full = keyboard_full_now();
    uint8_t compact = keyboard_compact_now() & keyboard_pads & ~full;
    uint8_t fulls = 0, compacts = 0;
    int size = JOYPAD_PORT_COUNT * JOYBUS_COMMAND_SKIP_SIZE, last = 0;

    // The full reads first, then the compact reads, as they fit in place of their skips
    for (int k = 0; k < JOYPAD_PORT_COUNT; k++)
    {
        int port = (keyboard_side_rotation + k) % JOYPAD_PORT_COUNT;
        int grow = KEYBOARD_COMMAND_SIZE(joybus_cmd_brf_keyboard_read_port_t) - JOYBUS_COMMAND_SKIP_SIZE;
        if ((full & (1 << port)) && size + grow <= KEYBOARD_BLOCK_COMMANDS) {
            fulls |= 1 << port;
            size += grow;
            last = port;
        }
    }
    JOYPAD_PORT_FOREACH (port)
    {
        int grow = (keyboard_mice & (1 << port)
            ? KEYBOARD_COMMAND_SIZE(joybus_cmd_brf_keyboard_mouse_read_port_t)
            : KEYBOARD_COMMAND_SIZE(joybus_cmd_brf_keyboard_compact_read_port_t)) - JOYBUS_COMMAND_SKIP_SIZE;
        if ((compact & (1 << port)) && size + grow <= KEYBOARD_BLOCK_COMMANDS) {
            compacts |= 1 << port;
            size += grow;
        }
    }

    memset(input, 0, JOYBUS_BLOCK_SIZE);
    size_t i = 0;
    JOYPAD_PORT_FOREACH (port)
    {
        if (fulls & (1 << port)) i += keyboard_write_full_read(port, &input[i]);
        else if (compacts & (1 << port)) i += keyboard_write_compact_read(port, &input[i]);
        else i += JOYBUS_COMMAND_SKIP_SIZE;
    }

    // Close out the Joybus operation block
    input[i] = 0xFE;
    input[JOYBUS_BLOCK_SIZE - 1] = 0x01;

    keyboard_side_reads = fulls | compacts;
    if (fulls == full && compacts == compact) return true;
    if (fulls != full) keyboard_side_rotation = (last + 1) % JOYPAD_PORT_COUNT;
    return false;
}

/** @brief Process the keyboard replies in a completed block, under interrupt. */
static void keyboard_read_completed(const uint8_t *out)
{
    // Walk the block's commands as they were sent: it may predate a port's change
    size_t i = 0;
    JOYPAD_PORT_FOREACH (port)
    {
        uint8_t send_len = out[i + JOYBUS_COMMAND_OFFSET_SEND_LEN];
        if (send_len == 0)
        {
            i += JOYBUS_COMMAND_SKIP_SIZE;
            continue;
        }
        // The error flag (0x80) means no device answered
        uint8_t recv_len = out[i + JOYBUS_COMMAND_OFFSET_RECV_LEN];
        const uint8_t *cmd = &out[i + JOYBUS_COMMAND_METADATA_SIZE];
        switch (cmd[0]) {
        case JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ:
            keyboard_port_full_read(port, (const void *)cmd, recv_len & 0x80);
            break;
        case JOYBUS_COMMAND_ID_BRF_KEYBOARD_COMPACT_READ:
        case JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ:
            keyboard_port_compact_read(port, cmd, recv_len & 0x80);
            break;
        }
        i += JOYBUS_COMMAND_METADATA_SIZE + send_len + (recv_len & 0x3F);
    }
}

/**
 * @brief Callback for the read with the joypads, under interrupt.
 *
 * @param out_dwords Joybus output block.
 * @param ctx Non-NULL if the block read the joypads: their reply is theirs, as
 *            joypad.c's own callback would have it.
 */
static void keyboard_main_callback(uint64_t *out_dwords, void *ctx)
{
    if (ctx)
    {
        memcpy((void *)joypad_read_output, out_dwords, JOYBUS_BLOCK_SIZE);
        joypad_read_pending = false;
        // Use some bytes from the joypad inputs to feed the entropy pool
        __entropy_add(out_dwords[0]);
    }
    keyboard_main_pending = false;
    if (keyboard_main_reads) keyboard_read_completed((const uint8_t *)out_dwords);
}

/** @brief Callback for the read beside the joypads', under interrupt. */
static void keyboard_side_callback(uint64_t *out_dwords, void *ctx)
{
    keyboard_side_pending = false;
    keyboard_read_completed((const uint8_t *)out_dwords);
}

/**
 * @brief Read the keyboards asynchronously, under the VI interrupt.
 *
 * It takes the place of the Joypad subsystem's own read: the joypads'
 * commands go in the block with the keyboards'. Only while #joypad_init's
 * own synchronous read is in flight do the keyboards have an operation of
 * their own.
 */
static void keyboard_vi_interrupt_callback(void *ctx)
{
    // Async operations are disabled during reset
    if (exception_reset_time() > 0) return;

    if (!keyboard_main_pending)
    {
        bool joypads = __joypad_is_initialized() && !joypad_read_pending;
        if (!keyboard_main_valid || joypads != keyboard_main_joypads)
            keyboard_build_main(joypads);
        if (joypads || keyboard_main_reads)
        {
            if (joypads) joypad_read_pending = true;
            keyboard_main_pending = true;
            // The GameCube controllers' rumble, as the Joypad subsystem has it now
            for (uint8_t gcn = keyboard_main_gcn, port = 0; gcn; gcn >>= 1, port++)
                if (gcn & 1) keyboard_main_input[keyboard_main_rumble[port]] = joypad_devices_hot[port].rumble_active;
            joybus_exec_async(keyboard_main_input, keyboard_main_callback, (void *)(uintptr_t)joypads);
        }
    }

    if (!keyboard_side_pending)
    {
        if (!keyboard_side_valid) keyboard_side_valid = keyboard_build_side();
        if (keyboard_side_reads)
        {
            keyboard_side_pending = true;
            joybus_exec_async(keyboard_side_input, keyboard_side_callback, NULL);
        }
    }
}

/** @brief Joybus detection callback: probe each device that may have a keyboard or mouse. */
static void keyboard_detect_callback(
    joybus_identifier_t identifier, joybus_detection_event_t event,
    int port, uint8_t device_status, void *ctx
) {
    // Port 4 is the cartridge's
    if (port >= JOYPAD_PORT_COUNT) return;
    uint8_t bit = 1 << port;

    switch (event) {
    case JOYBUS_DETECT_CONNECTED:
        // A keyboard has the Randnet keyboard's identity, or a controller's
        // when a gamepad shares its port
        if (identifier == JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD || identifier == JOYBUS_IDENTIFIER_N64_CONTROLLER)
        {
            keyboard_ports[port].probes = KEYBOARD_PROBES;
            keyboard_probing |= bit;
            if (identifier == JOYBUS_IDENTIFIER_N64_CONTROLLER) keyboard_pads |= bit;
        }
        // The joypads' commands change with any device
        keyboard_invalidate();
        break;

    case JOYBUS_DETECT_DISCONNECTED:
        keyboard_probing &= ~bit;
        keyboard_compact &= ~bit;
        keyboard_full &= ~bit;
        keyboard_pads &= ~bit;
        keyboard_connected &= ~bit;
        keyboard_mice &= ~bit;
        keyboard_port_release(port);
        keyboard_ports[port].x = keyboard_ports[port].y = 0;
        keyboard_ports[port].wheel = keyboard_ports[port].hwheel = 0;
        keyboard_ports[port].pressed = 0;
        keyboard_invalidate();
        break;

    case JOYBUS_DETECT_POLLED:
        break;
    }
}

void keyboard_init(void)
{
    // Just increment the refcount if already initialized
    if (keyboard_init_refcount++ > 0) { return; }
    // The iQue Player has no controller ports for a keyboard
    if (sys_bbplayer()) { return; }

    // Keyboards are found by the Joybus subsystem's background detection
    joybus_init();
    // Initialize the VI subsystem so that we can poll in sync with VI interrupts
    vi_init();

    disable_interrupts();
    keyboard_probing = keyboard_compact = keyboard_full = keyboard_pads = 0;
    keyboard_connected = keyboard_mice = 0;
    __keyboard_events_ridx = 0;
    __keyboard_events_widx = 0;
    keyboard_main_valid = keyboard_side_valid = false;
    keyboard_side_rotation = 0;
    keyboard_paused_keys = keyboard_paused_mice = 0;
    memset((void *)keyboard_ports, 0, sizeof(keyboard_ports));
    enable_interrupts();

    // This reports the devices already connected at once
    joybus_register_detection_callback(keyboard_detect_callback, NULL);

    // Read on vblank interrupt in the Joypad subsystem's place: its own
    // handler goes, and joypad_init installs none while this one is in
    disable_interrupts();
    if (__joypad_is_initialized()) vi_uninstall_vblank_handler(joypad_vi_interrupt_callback, NULL);
    __joypad_read_taken = true;
    vi_install_vblank_handler(keyboard_vi_interrupt_callback, NULL);
    enable_interrupts();
}

void keyboard_close(void)
{
    // Do nothing if there are still dangling references.
    if (--keyboard_init_refcount > 0) { return; }
    if (sys_bbplayer()) { return; }

    // Stop reading on vblank interrupt: the joypads' own read takes over. A
    // read still in flight finds no port to read when it completes.
    disable_interrupts();
    vi_uninstall_vblank_handler(keyboard_vi_interrupt_callback, NULL);
    __joypad_read_taken = false;
    if (__joypad_is_initialized()) vi_install_vblank_handler(joypad_vi_interrupt_callback, NULL);
    enable_interrupts();
    joybus_unregister_detection_callback(keyboard_detect_callback, NULL);
    disable_interrupts();
    keyboard_probing = keyboard_compact = keyboard_full = keyboard_pads = 0;
    keyboard_connected = keyboard_mice = 0;
    enable_interrupts();

    vi_close();
    joybus_close();
}

/** @brief Pause or resume the keys and the mouse of some ports, releasing what newly paused ones held. */
static void keyboard_pause_ports(uint8_t ports, uint8_t pause)
{
    uint8_t keys = (pause & KEYBOARD_PAUSE_KEYS) ? ports : 0;
    uint8_t mice = (pause & KEYBOARD_PAUSE_MOUSE) ? ports : 0;
    disable_interrupts();
    JOYPAD_PORT_FOREACH (port)
    {
        if (keys & ~keyboard_paused_keys & (1 << port)) keyboard_port_release_keys(port);
        if (mice & ~keyboard_paused_mice & (1 << port)) keyboard_ports[port].buttons = 0;
    }
    keyboard_paused_keys = (keyboard_paused_keys & ~ports) | keys;
    keyboard_paused_mice = (keyboard_paused_mice & ~ports) | mice;
    keyboard_invalidate();
    enable_interrupts();
}

void keyboard_pause(uint8_t pause)
{
    ASSERT_KEYBOARD_INITIALIZED();
    keyboard_pause_ports((1 << JOYPAD_PORT_COUNT) - 1, pause);
}

void keyboard_pause_port(joypad_port_t port, uint8_t pause)
{
    ASSERT_KEYBOARD_INITIALIZED();
    ASSERT_KEYBOARD_PORT_VALID(port);
    keyboard_pause_ports(1 << port, pause);
}

bool keyboard_is_connected(joypad_port_t port)
{
    ASSERT_KEYBOARD_INITIALIZED();
    ASSERT_KEYBOARD_PORT_VALID(port);
    return keyboard_connected & (1 << port);
}

keyboard_layout_t keyboard_get_layout(joypad_port_t port)
{
    ASSERT_KEYBOARD_INITIALIZED();
    ASSERT_KEYBOARD_PORT_VALID(port);
    if (!(keyboard_connected & (1 << port))) return KEYBOARD_LAYOUT_NONE;
    return keyboard_ports[port].settings & JOYBUS_BRF_KEYBOARD_SETTINGS_LAYOUT;
}

keyboard_arrangement_t keyboard_get_arrangement(joypad_port_t port)
{
    ASSERT_KEYBOARD_INITIALIZED();
    ASSERT_KEYBOARD_PORT_VALID(port);
    if (!(keyboard_connected & (1 << port))) return KEYBOARD_ARRANGEMENT_NONE;
    return (keyboard_ports[port].settings >> JOYBUS_BRF_KEYBOARD_SETTINGS_ARRANGEMENT_SHIFT) & 1;
}

bool keyboard_get_event(keyboard_event_t *event)
{
    ASSERT_KEYBOARD_INITIALIZED();
    // Most calls find the queue empty: say so without turning interrupts off
    if (!keyboard_has_events()) return false;
    disable_interrupts();
    uint8_t ridx = __keyboard_events_ridx;
    bool any = ridx != __keyboard_events_widx;
    if (any)
    {
        *event = keyboard_events[ridx];
        __keyboard_events_ridx = (ridx + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    }
    enable_interrupts();
    return any;
}

keyboard_keys_t keyboard_get_keys(joypad_port_t port)
{
    ASSERT_KEYBOARD_INITIALIZED();
    ASSERT_KEYBOARD_PORT_VALID(port);
    keyboard_keys_t keys;
    disable_interrupts();
    memcpy(keys.key, (const void *)keyboard_ports[port].key, sizeof(keys.key));
    keys.modifiers = keyboard_ports[port].modifiers;
    keys.overflow = keyboard_ports[port].overflow;
    enable_interrupts();
    return keys;
}

void keyboard_set_leds(joypad_port_t port, uint8_t leds)
{
    ASSERT_KEYBOARD_INITIALIZED();
    ASSERT_KEYBOARD_PORT_VALID(port);
    disable_interrupts();
    keyboard_ports[port].leds = leds;
    // The LEDs go with a full read, and the cached full reads carry them
    if (keyboard_compact & (1 << port)) keyboard_full |= 1 << port;
    keyboard_side_valid = false;
    enable_interrupts();
}

uint8_t keyboard_get_leds(joypad_port_t port)
{
    ASSERT_KEYBOARD_INITIALIZED();
    ASSERT_KEYBOARD_PORT_VALID(port);
    return keyboard_ports[port].leds;
}

bool keyboard_mouse_is_connected(joypad_port_t port)
{
    ASSERT_KEYBOARD_INITIALIZED();
    ASSERT_KEYBOARD_PORT_VALID(port);
    return keyboard_mice & (1 << port);
}

keyboard_mouse_t keyboard_get_mouse(joypad_port_t port)
{
    ASSERT_KEYBOARD_INITIALIZED();
    ASSERT_KEYBOARD_PORT_VALID(port);
    volatile keyboard_port_hot_t *state = &keyboard_ports[port];
    disable_interrupts();
    keyboard_mouse_t mouse = {
        .x = state->x, .y = state->y, .wheel = state->wheel, .hwheel = state->hwheel,
        .buttons = state->buttons, .pressed = state->pressed,
    };
    state->x = state->y = 0;
    state->wheel = state->hwheel = 0;
    state->pressed = 0;
    enable_interrupts();
    return mouse;
}

/** @} */ /* keyboard */
