/**
 * @file blueretroforever.c
 * @brief BlueRetroForever commands
 * @ingroup blueretroforever
 */

#include <string.h>

#include "joybus.h"
#include "joybus_commands.h"
#include "blueretroforever.h"

/**
 * @addtogroup blueretroforever
 * @{
 */

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

/** @} */ /* blueretroforever */
