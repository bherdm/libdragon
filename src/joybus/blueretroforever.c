/**
 * @file blueretroforever.c
 * @brief BlueRetroForever commands
 * @ingroup blueretroforever
 */

#include <string.h>

#include "n64sys.h"
#include "joybus.h"
#include "joybus_commands.h"
#include "joybus_accessory_internal.h"
#include "blueretroforever.h"

/**
 * @addtogroup blueretroforever
 * @{
 */

/** @brief How long the adapter takes to stage a verb's data for the next request: its task runs within about 10 ms. */
#define BRF_STAGE_MS 15

/** @brief How many times a staged read is asked again when the adapter hadn't staged it yet. */
#define BRF_STAGE_TRIES 3

/** @brief A reply's result: #BRF_NOT_PRESENT without the marker, as no device or firmware without commands answers. */
static brf_result_t brf_reply_result(const joybus_cmd_brf_command_port_t *cmd)
{
    if (cmd->recv.marker != JOYBUS_BRF_MARKER || cmd->recv.version != JOYBUS_BRF_COMMAND_VERSION)
        return BRF_NOT_PRESENT;
    return cmd->recv.result;
}

/** @brief The adapter's state in a reply. */
static void brf_reply_status(const joybus_cmd_brf_command_port_t *cmd, brf_status_t *status)
{
    status->version = cmd->recv.version;
    status->pairing = cmd->recv.flags & JOYBUS_BRF_FLAG_PAIRING;
    status->single_port = cmd->recv.flags & JOYBUS_BRF_FLAG_SINGLE_PORT;
    status->asleep = cmd->recv.flags & JOYBUS_BRF_FLAG_ASLEEP;
    status->led_level = cmd->recv.led_level;
    for (int i = 0; i < JOYPAD_PORT_COUNT; i++)
        status->device[i] = cmd->recv.ports[i];
}

/** @brief Each port's keyboard settings in a reply to #JOYBUS_BRF_VERB_KEYBOARD_SETTINGS. */
static void brf_reply_keyboard_settings(const joybus_cmd_brf_command_port_t *cmd,
    brf_keyboard_settings_t settings[JOYPAD_PORT_COUNT])
{
    for (int i = 0; i < JOYPAD_PORT_COUNT; i++)
    {
        uint8_t byte = cmd->recv.ports[i];
        settings[i] = (brf_keyboard_settings_t){
            .layout = byte & JOYBUS_BRF_KEYBOARD_SETTINGS_LAYOUT,
            .arrangement = (byte >> JOYBUS_BRF_KEYBOARD_SETTINGS_ARRANGEMENT_SHIFT) & 1,
            .classic = byte & JOYBUS_BRF_KEYBOARD_SETTINGS_CLASSIC,
            .temporary = byte & JOYBUS_BRF_KEYBOARD_SETTINGS_TEMPORARY,
        };
    }
}

/** @brief A reply's bytes from 4, where a verb's own data replaces the flags, LED level and ports. */
static const uint8_t *brf_reply_data(const joybus_cmd_brf_command_port_t *cmd)
{
    return &cmd->recv.flags;
}

/** @brief A flash's progress in a reply to #JOYBUS_BRF_VERB_FLASH_STATUS. */
static void brf_reply_flash_status(const joybus_cmd_brf_command_port_t *cmd, brf_flash_status_t *status)
{
    const uint8_t *data = brf_reply_data(cmd);
    status->state = data[0];
    status->error = data[1];
    status->written = (data[2] << 16) | (data[3] << 8) | data[4];
}

/** @brief A log reply's position: the 15 bits of its first two bytes. */
static uint16_t brf_reply_log_position(const joybus_cmd_brf_command_port_t *cmd)
{
    const uint8_t *data = brf_reply_data(cmd);
    return ((data[0] << 8) | data[1]) & JOYBUS_BRF_LOG_POSITION;
}

/** @brief Is a log reply where the log is (asked with #JOYBUS_BRF_LOG_WHERE), not its bytes? */
static bool brf_reply_log_where(const joybus_cmd_brf_command_port_t *cmd)
{
    return brf_reply_data(cmd)[0] & (JOYBUS_BRF_LOG_WHERE >> 8);
}

/** @brief How far a log position is behind the head, modulo the positions. */
static uint16_t brf_log_behind(uint16_t head, uint16_t position)
{
    return (head - position) & JOYBUS_BRF_LOG_POSITION;
}

/** @brief A flash chunk's CRC, a Controller Pak write's. */
static uint8_t brf_chunk_crc(const uint8_t *data)
{
    return joybus_accessory_calculate_data_crc(data);
}

/** @brief A port's keyboard settings as the settings byte. */
static uint8_t brf_keyboard_settings_byte(const brf_keyboard_settings_t *settings)
{
    return (settings->layout & JOYBUS_BRF_KEYBOARD_SETTINGS_LAYOUT) |
        ((settings->arrangement == KEYBOARD_ARRANGEMENT_PC) << JOYBUS_BRF_KEYBOARD_SETTINGS_ARRANGEMENT_SHIFT) |
        (settings->classic ? JOYBUS_BRF_KEYBOARD_SETTINGS_CLASSIC : 0) |
        (settings->temporary ? JOYBUS_BRF_KEYBOARD_SETTINGS_TEMPORARY : 0);
}

/** @brief A target port as a verb's argument: 0xFF for every port. */
static uint8_t brf_target(int target)
{
    return target == BRF_ALL_PORTS ? 0xFF : target;
}

/** @brief Send a verb on a port and wait for the reply: one copy, not one per call (joybus_exec_cmd is inline). */
__attribute__((noinline))
static brf_result_t brf_command(joypad_port_t port, uint8_t verb, uint8_t arg0, uint8_t arg1,
    joybus_cmd_brf_command_port_t *cmd)
{
    memset(cmd, 0, sizeof(*cmd));
    cmd->send.command = JOYBUS_COMMAND_ID_BRF_COMMAND;
    cmd->send.verb = verb;
    cmd->send.arg[0] = arg0;
    cmd->send.arg[1] = arg1;
    joybus_exec_cmd_struct(port, (*cmd));
    return brf_reply_result(cmd);
}

/**
 * @brief Send a verb whose reply carries what the adapter staged for the
 *        request before: send it, give the adapter time to stage it, and send
 *        it again, whose reply has it.
 */
static brf_result_t brf_command_staged(joypad_port_t port, uint8_t verb, uint8_t arg0, uint8_t arg1,
    joybus_cmd_brf_command_port_t *cmd)
{
    brf_result_t result = brf_command(port, verb, arg0, arg1, cmd);
    if (result != BRF_OK) return result;
    wait_ms(BRF_STAGE_MS);
    return brf_command(port, verb, arg0, arg1, cmd);
}

bool brf_is_present(joypad_port_t port)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, JOYBUS_BRF_VERB_STATUS, 0, 0, &cmd) != BRF_NOT_PRESENT;
}

brf_result_t brf_get_status(joypad_port_t port, brf_status_t *status)
{
    joybus_cmd_brf_command_port_t cmd;
    brf_result_t result = brf_command(port, JOYBUS_BRF_VERB_STATUS, 0, 0, &cmd);
    if (result == BRF_OK) brf_reply_status(&cmd, status);
    return result;
}

brf_result_t brf_set_pairing(joypad_port_t port, bool open)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, open ? JOYBUS_BRF_VERB_OPEN_PAIRING : JOYBUS_BRF_VERB_CLOSE_PAIRING, 0, 0, &cmd);
}

brf_result_t brf_disconnect(joypad_port_t port, int target)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, JOYBUS_BRF_VERB_DISCONNECT, brf_target(target), 0, &cmd);
}

brf_result_t brf_set_led_level(joypad_port_t port, uint8_t level)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, JOYBUS_BRF_VERB_LED_LEVEL, level, 0, &cmd);
}

brf_result_t brf_get_keyboard_settings(joypad_port_t port, brf_keyboard_settings_t settings[JOYPAD_PORT_COUNT])
{
    joybus_cmd_brf_command_port_t cmd;
    brf_result_t result = brf_command(port, JOYBUS_BRF_VERB_KEYBOARD_SETTINGS, 0, 0, &cmd);
    if (result == BRF_OK) brf_reply_keyboard_settings(&cmd, settings);
    return result;
}

brf_result_t brf_set_keyboard_settings(joypad_port_t port, int target, const brf_keyboard_settings_t *settings)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, JOYBUS_BRF_VERB_SET_KEYBOARD_SETTINGS,
        brf_target(target), brf_keyboard_settings_byte(settings), &cmd);
}

brf_result_t brf_sleep(joypad_port_t port)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, JOYBUS_BRF_VERB_SLEEP, 0, 0, &cmd);
}

brf_result_t brf_wake(joypad_port_t port)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, JOYBUS_BRF_VERB_WAKE, 0, 0, &cmd);
}

brf_result_t brf_restart(joypad_port_t port)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, JOYBUS_BRF_VERB_RESTART, 0, 0, &cmd);
}

brf_result_t brf_get_board(joypad_port_t port, uint8_t address[6])
{
    joybus_cmd_brf_command_port_t cmd;
    brf_result_t result = brf_command(port, JOYBUS_BRF_VERB_BOARD, 0, 0, &cmd);
    if (result == BRF_OK) memcpy(address, brf_reply_data(&cmd), 6);
    return result;
}

brf_result_t brf_get_firmware_version(joypad_port_t port, char *version, int size)
{
    joybus_cmd_brf_command_port_t cmd;
    char text[JOYBUS_BRF_VERSION_CHUNKS * 4 + 1];
    int len = BRF_VERSION_MAX;
    brf_result_t result = BRF_OK;

    // Each chunk carries its index, the string's length and four bytes: no
    // length is a block not staged yet, as an adapter just started answers
    for (int chunk = 0; result == BRF_OK && chunk * 4 < len; chunk++)
    {
        const uint8_t *data;
        int tries = 0;
        do {
            result = brf_command_staged(port, JOYBUS_BRF_VERB_FIRMWARE_VERSION, chunk, 0, &cmd);
            data = brf_reply_data(&cmd);
        } while (result == BRF_OK && (data[0] != chunk || !data[1]) && ++tries < BRF_STAGE_TRIES);
        if (result == BRF_OK && (data[0] != chunk || !data[1])) result = BRF_NOT_READY;
        if (result != BRF_OK) break;
        len = data[1] < BRF_VERSION_MAX ? data[1] : BRF_VERSION_MAX;
        memcpy(&text[chunk * 4], &data[2], 4);
    }
    if (result != BRF_OK) len = 0;
    if (size > 0)
    {
        int n = len < size - 1 ? len : size - 1;
        memcpy(version, text, n);
        version[n] = 0;
    }
    return result;
}

brf_result_t brf_read_log(joypad_port_t port, uint16_t *position, void *buf, int size, int *read)
{
    joybus_cmd_brf_command_port_t cmd;
    uint8_t *out = buf;
    int tries = 0;

    *read = 0;
    // Where the log is: its head, then the oldest position kept
    brf_result_t result;
    do result = brf_command_staged(port, JOYBUS_BRF_VERB_LOG, JOYBUS_BRF_LOG_WHERE >> 8, 0, &cmd);
    while (result == BRF_OK && !brf_reply_log_where(&cmd) && ++tries < BRF_STAGE_TRIES);
    if (result != BRF_OK) return result;
    if (!brf_reply_log_where(&cmd)) return BRF_NOT_READY;
    const uint8_t *data = brf_reply_data(&cmd);
    uint16_t head = brf_reply_log_position(&cmd);
    uint16_t oldest = ((data[2] << 8) | data[3]) & JOYBUS_BRF_LOG_POSITION;

    // A position the adapter no longer keeps starts from the oldest it does
    uint16_t pos = *position;
    if (pos == BRF_LOG_OLDEST || brf_log_behind(head, pos) > brf_log_behind(head, oldest)) pos = oldest;

    while (*read < size && pos != head)
    {
        // A reply that isn't for this position is the request before's, not staged yet, unless
        // the position was overwritten meanwhile and the reply comes from the oldest kept
        tries = 0;
        uint16_t got;
        do {
            result = brf_command_staged(port, JOYBUS_BRF_VERB_LOG, pos >> 8, pos & 0xFF, &cmd);
            got = brf_reply_log_position(&cmd);
        } while (result == BRF_OK && (brf_reply_log_where(&cmd) ||
                 (got != pos && brf_log_behind(head, got) >= brf_log_behind(head, pos))) &&
                 ++tries < BRF_STAGE_TRIES);
        if (result != BRF_OK || brf_reply_log_where(&cmd) ||
            (got != pos && brf_log_behind(head, got) >= brf_log_behind(head, pos)))
            break;
        // Overwritten as it was read: the bytes from the oldest kept start the next call's run
        if (got != pos && *read) break;
        pos = got;
        int n = brf_log_behind(head, pos);
        if (n > 4) n = 4;
        if (n > size - *read) n = size - *read;
        memcpy(&out[*read], &data[2], n);
        *read += n;
        pos = (pos + n) & JOYBUS_BRF_LOG_POSITION;
    }
    *position = pos;
    return result;
}

brf_result_t brf_flash_begin(joypad_port_t port)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, JOYBUS_BRF_VERB_FLASH_BEGIN, 0, 0, &cmd);
}

brf_chunk_result_t brf_flash_chunk(joypad_port_t port, uint16_t sequence, const void *data, uint16_t *next)
{
    joybus_cmd_brf_flash_chunk_port_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.send.command = JOYBUS_COMMAND_ID_BRF_FLASH_CHUNK;
    cmd.send.sequence = sequence;
    memcpy(cmd.send.data, data, JOYBUS_BRF_FLASH_CHUNK_SIZE);
    cmd.send.crc = brf_chunk_crc(cmd.send.data);
    joybus_exec_cmd_struct(port, cmd);
    if (next) *next = cmd.recv.next;
    // The results are 0xA0 to 0xA4: anything else is no reply
    uint8_t result = cmd.recv.result - 0xA0;
    return result <= BRF_CHUNK_NO_FLASH ? (brf_chunk_result_t)result : BRF_CHUNK_NO_REPLY;
}

brf_result_t brf_flash_status(joypad_port_t port, brf_flash_status_t *status)
{
    joybus_cmd_brf_command_port_t cmd;
    brf_result_t result = brf_command(port, JOYBUS_BRF_VERB_FLASH_STATUS, 0, 0, &cmd);
    if (result == BRF_OK) brf_reply_flash_status(&cmd, status);
    return result;
}

brf_result_t brf_flash_end(joypad_port_t port)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, JOYBUS_BRF_VERB_FLASH_END, 0, 0, &cmd);
}

brf_result_t brf_flash_abort(joypad_port_t port)
{
    joybus_cmd_brf_command_port_t cmd;
    return brf_command(port, JOYBUS_BRF_VERB_FLASH_ABORT, 0, 0, &cmd);
}

/** @} */ /* blueretroforever */
