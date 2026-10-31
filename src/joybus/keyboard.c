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
 * @brief How many reads a device with the Randnet keyboard's identity can
 *        leave unanswered before it isn't read again: a keyboard plugged in a
 *        moment ago can miss one, and a Randnet keyboard misses them all.
 */
#define KEYBOARD_PROBES 3

/** @brief A read command's size in a Joybus block. */
#define KEYBOARD_READ_COMMAND_SIZE \
    (JOYBUS_COMMAND_METADATA_SIZE + sizeof(joybus_cmd_brf_keyboard_read_port_t))

/**
 * @brief How many keyboards a Joybus block reads: each read takes the place
 *        of its port's skip byte, and the block ends with 0xFE before its
 *        status byte.
 */
#define KEYBOARD_READS_PER_BLOCK \
    ((JOYBUS_BLOCK_SIZE - 1 - JOYPAD_PORT_COUNT - 1) / (KEYBOARD_READ_COMMAND_SIZE - JOYBUS_COMMAND_SKIP_SIZE))

/** @brief A port's keyboard state, kept under interrupt. */
typedef struct keyboard_port_hot_s
{
    /** @brief The held keys as of the last read; unused entries are #KEYBOARD_KEY_NONE. */
    keyboard_key_t key[KEYBOARD_KEYS_MAX];
    /** @brief How many keys are held. */
    uint8_t count;
    /** @brief The last read said more keys were held than it reported. */
    bool overflow;
    /** @brief The LEDs sent with each read (@ref KEYBOARD_LED). */
    uint8_t leds;
    /** @brief The adapter's settings byte, as of the last read. */
    uint8_t settings;
    /** @brief Reads left to answer before the port isn't read, until the keyboard has answered one. */
    uint8_t probes;
} keyboard_port_hot_t;

/** @brief Refcount of #keyboard_init vs #keyboard_close calls. */
static int keyboard_init_refcount = 0;
/** @brief The ports read: a bit for each keyboard, and each device that may be one. */
static volatile uint8_t keyboard_read_ports = 0;
/** @brief The ports a keyboard is connected to: those whose device has answered a read. */
static volatile uint8_t keyboard_connected = 0;
/** @brief Each port's keyboard state. */
static volatile keyboard_port_hot_t keyboard_ports[JOYPAD_PORT_COUNT];
/** @brief The event queue (a ring buffer), written under interrupt. */
static keyboard_event_t keyboard_events[KEYBOARD_EVENT_QUEUE_SIZE];
/** @brief The event queue's read index. */
static volatile uint8_t keyboard_events_ridx = 0;
/** @brief The event queue's write index. */
static volatile uint8_t keyboard_events_widx = 0;
/** @brief Are we in the middle of reading the keyboards? */
static volatile bool keyboard_read_pending = false;
/** @brief Is the cached Joybus input block for reading the keyboards valid? */
static volatile bool keyboard_read_input_valid = false;
/** @brief The port a block starts from when the keyboards don't all fit in one. */
static volatile uint8_t keyboard_read_rotation = 0;
/** @brief Cached Joybus input block for reading the keyboards. */
static volatile uint8_t keyboard_read_input[JOYBUS_BLOCK_SIZE] = {0};

/** @brief Queue an event, under interrupt. While the queue is full, the event is dropped. */
static void keyboard_event_push(int port, keyboard_key_t key, bool pressed)
{
    uint8_t widx = keyboard_events_widx;
    uint8_t next = (widx + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    if (next == keyboard_events_ridx) return;
    keyboard_events[widx] = (keyboard_event_t){ .key = key, .port = port, .pressed = pressed };
    keyboard_events_widx = next;
}

/** @brief Is a key among the first @p count of a set of keys? */
static bool keyboard_keys_contain(const keyboard_key_t *keys, int count, keyboard_key_t key)
{
    for (int i = 0; i < count; i++)
        if (keys[i] == key) return true;
    return false;
}

/** @brief Make a port's held keys the @p count keys in @p now, queueing a release or press for each change. */
static void keyboard_port_update(int port, const keyboard_key_t *now, int count)
{
    volatile keyboard_port_hot_t *state = &keyboard_ports[port];
    keyboard_key_t held[KEYBOARD_KEYS_MAX];
    int held_count = state->count;
    memcpy(held, (const void *)state->key, held_count * sizeof(keyboard_key_t));

    // A read's releases come before its presses
    for (int i = 0; i < held_count; i++)
        if (!keyboard_keys_contain(now, count, held[i]))
            keyboard_event_push(port, held[i], false);
    for (int i = 0; i < count; i++)
        if (!keyboard_keys_contain(held, held_count, now[i]))
            keyboard_event_push(port, now[i], true);

    memcpy((void *)state->key, now, count * sizeof(keyboard_key_t));
    for (int i = count; i < held_count; i++) state->key[i] = KEYBOARD_KEY_NONE;
    state->count = count;
}

/**
 * @brief Process one port's read reply, under interrupt.
 *
 * Until a port's device has answered with the marker, a reply without it, or
 * no reply #KEYBOARD_PROBES times, means it isn't a keyboard, and the port
 * isn't read again until a device connects there. Once it has, a reply
 * without the marker leaves the keys as they were, until detection reports
 * the change: BlueRetroForever answers with its identity while changing it.
 */
static void keyboard_port_read(int port, const joybus_cmd_brf_keyboard_read_port_t *cmd, bool error)
{
    volatile keyboard_port_hot_t *state = &keyboard_ports[port];
    if (error || cmd->recv.marker != JOYBUS_BRF_MARKER ||
        cmd->recv.version != JOYBUS_BRF_KEYBOARD_READ_VERSION)
    {
        if (!(keyboard_connected & (1 << port)) && (!error || --state->probes == 0))
        {
            keyboard_read_ports &= ~(1 << port);
            keyboard_read_input_valid = false;
        }
        return;
    }
    keyboard_connected |= 1 << port;
    state->settings = cmd->recv.settings;
    state->overflow = cmd->recv.status & JOYBUS_BRF_KEYBOARD_STATUS_EXCESS_KEYS;

    // The keys fill the slots from the first
    keyboard_key_t now[KEYBOARD_KEYS_MAX];
    int count = 0;
    while (count < KEYBOARD_KEYS_MAX && (now[count] = cmd->recv.keys[count])) count++;
    keyboard_port_update(port, now, count);
}

/** @brief Release every key held on a port's keyboard, which has gone. */
static void keyboard_port_release(int port)
{
    static const keyboard_key_t none[1] = { KEYBOARD_KEY_NONE };
    keyboard_port_update(port, none, 0);
    keyboard_ports[port].overflow = false;
}

/**
 * @brief Write a port's keyboard read command into a Joybus block.
 *
 * @return The command's size in bytes.
 */
static size_t keyboard_write_read_command(joypad_port_t port, uint8_t *dst)
{
    // The command and the LEDs; the block is zeroed, so the reply's bytes need no writing
    dst[JOYBUS_COMMAND_OFFSET_SEND_LEN] = sizeof(((joybus_cmd_brf_keyboard_read_port_t *)0)->send);
    dst[JOYBUS_COMMAND_OFFSET_RECV_LEN] = sizeof(((joybus_cmd_brf_keyboard_read_port_t *)0)->recv);
    dst[JOYBUS_COMMAND_OFFSET_COMMAND_ID] = JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ;
    dst[JOYBUS_COMMAND_OFFSET_COMMAND_ID + 1] = keyboard_ports[port].leds;
    return KEYBOARD_READ_COMMAND_SIZE;
}

/**
 * @brief Build the Joybus block that reads the keyboards.
 *
 * A block reads two keyboards. With more, it reads two of them, starting
 * after the last port the block before read, so each is read in turn.
 *
 * @return true if the block reads every port, so it can be kept for the next read.
 */
static bool keyboard_build_read_block(uint8_t *input)
{
    uint8_t ports = keyboard_read_ports;
    uint8_t read = 0;
    int reads = 0, last = 0;
    for (int k = 0; k < JOYPAD_PORT_COUNT && reads < KEYBOARD_READS_PER_BLOCK; k++)
    {
        int port = (keyboard_read_rotation + k) % JOYPAD_PORT_COUNT;
        if (ports & (1 << port))
        {
            read |= 1 << port;
            reads++;
            last = port;
        }
    }

    memset(input, 0, JOYBUS_BLOCK_SIZE);
    size_t i = 0;
    JOYPAD_PORT_FOREACH (port)
        i += (read & (1 << port)) ? keyboard_write_read_command(port, &input[i]) : JOYBUS_COMMAND_SKIP_SIZE;

    // Close out the Joybus operation block
    input[i] = 0xFE;
    input[JOYBUS_BLOCK_SIZE - 1] = 0x01;

    if (read == ports) return true;
    keyboard_read_rotation = (last + 1) % JOYPAD_PORT_COUNT;
    return false;
}

/** @brief Process the keyboard replies in a completed read block, under interrupt. */
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
        if ((keyboard_read_ports & (1 << port)) &&
            out[i + JOYBUS_COMMAND_OFFSET_COMMAND_ID] == JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ)
            keyboard_port_read(port, (const void *)&out[i + JOYBUS_COMMAND_METADATA_SIZE], recv_len & 0x80);
        i += JOYBUS_COMMAND_METADATA_SIZE + send_len + (recv_len & 0x3F);
    }
}

/** @brief Callback for reading the keyboards, under interrupt. */
static void keyboard_read_callback(uint64_t *out_dwords, void *ctx)
{
    keyboard_read_pending = false;
    keyboard_read_completed((const uint8_t *)out_dwords);
}

/** @brief Read the keyboards asynchronously, under the VI interrupt. */
static void keyboard_vi_interrupt_callback(void *ctx)
{
    // Nothing to read, or async operations are disabled during reset
    if (!keyboard_read_ports || exception_reset_time() > 0) return;

    // Bail if this operation is already in-progress
    if (keyboard_read_pending) return;
    keyboard_read_pending = true;

    uint8_t * const input = (void *)keyboard_read_input;
    if (!keyboard_read_input_valid)
        keyboard_read_input_valid = keyboard_build_read_block(input);

    joybus_exec_async(input, keyboard_read_callback, NULL);
}

/** @brief Joybus detection callback: read each device with the Randnet keyboard's identity. */
static void keyboard_detect_callback(
    joybus_identifier_t identifier, joybus_detection_event_t event,
    int port, uint8_t device_status, void *ctx
) {
    // Port 4 is the cartridge's
    if (port >= JOYPAD_PORT_COUNT || identifier != JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD) return;

    switch (event) {
    case JOYBUS_DETECT_CONNECTED:
        keyboard_ports[port].probes = KEYBOARD_PROBES;
        keyboard_read_ports |= 1 << port;
        keyboard_read_input_valid = false;
        break;

    case JOYBUS_DETECT_DISCONNECTED:
        keyboard_read_ports &= ~(1 << port);
        keyboard_connected &= ~(1 << port);
        keyboard_port_release(port);
        keyboard_read_input_valid = false;
        break;

    case JOYBUS_DETECT_POLLED:
        break;
    }
}

void keyboard_init(void)
{
    // Just increment the refcount if already initialized
    if (keyboard_init_refcount++ > 0) { return; }

    // Keyboards are found by the Joybus subsystem's background detection
    joybus_init();
    // Initialize the VI subsystem so that we can poll in sync with VI interrupts
    vi_init();

    disable_interrupts();
    keyboard_read_ports = 0;
    keyboard_connected = 0;
    keyboard_events_ridx = 0;
    keyboard_events_widx = 0;
    keyboard_read_input_valid = false;
    keyboard_read_rotation = 0;
    memset((void *)keyboard_ports, 0, sizeof(keyboard_ports));
    enable_interrupts();

    // The iQue Player has no controller ports for a keyboard. Elsewhere, this
    // reports the keyboards already connected at once.
    if (!sys_bbplayer())
        joybus_register_detection_callback(keyboard_detect_callback, NULL);

    // Read the keyboards on vblank interrupt
    vi_install_vblank_handler(keyboard_vi_interrupt_callback, NULL);
}

void keyboard_close(void)
{
    // Do nothing if there are still dangling references.
    if (--keyboard_init_refcount > 0) { return; }

    // Stop reading the keyboards on vblank interrupt. A read still in flight
    // finds no port to read when it completes.
    vi_uninstall_vblank_handler(keyboard_vi_interrupt_callback, NULL);
    if (!sys_bbplayer())
        joybus_unregister_detection_callback(keyboard_detect_callback, NULL);
    keyboard_read_ports = 0;
    keyboard_connected = 0;

    vi_close();
    joybus_close();
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
    disable_interrupts();
    uint8_t ridx = keyboard_events_ridx;
    bool any = ridx != keyboard_events_widx;
    if (any)
    {
        *event = keyboard_events[ridx];
        keyboard_events_ridx = (ridx + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
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
    // The LEDs are part of the cached read command
    keyboard_read_input_valid = false;
    enable_interrupts();
}

uint8_t keyboard_get_leds(joypad_port_t port)
{
    ASSERT_KEYBOARD_INITIALIZED();
    ASSERT_KEYBOARD_PORT_VALID(port);
    return keyboard_ports[port].leds;
}

/** @} */ /* keyboard */
