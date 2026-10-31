/**
 * @file blueretroforever.h
 * @brief BlueRetroForever commands
 * @ingroup blueretroforever
 */

#ifndef __LIBDRAGON_BLUERETROFOREVER_H
#define __LIBDRAGON_BLUERETROFOREVER_H

#include <stdbool.h>
#include <stdint.h>

#include "preview.h"
#include "joypad.h"
#include "keyboard.h"

/**
 * @defgroup blueretroforever BlueRetroForever commands
 * @ingroup peripherals
 * @brief Pairing, status and keyboard settings for BlueRetroForever.
 *
 * BlueRetroForever is a Bluetooth adapter for the controller ports. Its
 * command 0x7B lets a program open and close pairing, see what's connected to
 * each port, disconnect a device, set how bright its LEDs get, and read and
 * change each port's keyboard settings: a settings page in a menu, say.
 *
 * Each call sends one command on one controller port and waits for the reply,
 * about a millisecond. That's fine for menus and settings screens, not for
 * every frame. The adapter answers on any of its ports, whatever is connected
 * there. It takes a command's action after replying, so a reply shows the
 * state from before it: read the status again to see pairing open, say, or a
 * device gone.
 *
 * The module holds no state, and a program that doesn't call it links none
 * of it. Commands that change things can't wipe a setup: factory reset and
 * button mappings aren't among them.
 *
 * Its names start with `brf_` and `BRF_`, for BlueRetroForever.
 *
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A command's result. */
typedef enum
{
    /** @brief No BlueRetroForever answered on the port, or one too old for commands. */
    BRF_NOT_PRESENT = -1,
    /** @brief Accepted. */
    BRF_OK = 0,
    /** @brief The adapter's firmware hasn't got this command. */
    BRF_UNKNOWN_VERB = 1,
    /** @brief Refused: an argument is out of range. */
    BRF_BAD_ARGUMENT = 2,
} brf_result_t;

/** @brief What's connected to one of the adapter's ports. */
typedef enum
{
    /** @brief Nothing. */
    BRF_DEVICE_NONE = 0,
    /** @brief A gamepad. */
    BRF_DEVICE_GAMEPAD = 1,
    /** @brief A keyboard. */
    BRF_DEVICE_KEYBOARD = 2,
    /** @brief A mouse. */
    BRF_DEVICE_MOUSE = 3,
} brf_device_t;

/** @brief Every port, for #brf_disconnect and #brf_set_keyboard_settings. */
#define BRF_ALL_PORTS (-1)

/** @brief The adapter's state. */
typedef struct brf_status_s
{
    /** @brief The command protocol's version. */
    uint8_t version;
    /** @brief Pairing is open. */
    bool pairing;
    /** @brief The LEDs' brightness level, 1 or more. */
    uint8_t led_level;
    /** @brief Each port's device. */
    brf_device_t device[JOYPAD_PORT_COUNT];
} brf_status_t;

/** @brief A port's keyboard settings. */
typedef struct brf_keyboard_settings_s
{
    /** @brief The layout the keyboard is labelled with. */
    keyboard_layout_t layout;
    /** @brief A Mac's arrangement or a PC's. */
    keyboard_arrangement_t arrangement;
    /**
     * @brief Classic mode: the port presents a Randnet keyboard to the
     *        Randnet's own software, and the Keyboard subsystem doesn't read
     *        it. Full layout mode otherwise.
     */
    bool classic;
    /**
     * @brief Read, the port's setting isn't the saved one. Set, keep it
     *        until power off, leaving the saved one.
     */
    bool temporary;
} brf_keyboard_settings_t;

/**
 * @brief Is a BlueRetroForever on a port, with its commands?
 * @preview
 *
 * @param port Joypad port (#joypad_port_t).
 * @return true if one answered.
 */
LIBDRAGON_PREVIEW_API
bool brf_is_present(joypad_port_t port);

/**
 * @brief Read the adapter's state.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param[out] status Where to store the state.
 * @return #BRF_OK, or #BRF_NOT_PRESENT.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_get_status(joypad_port_t port, brf_status_t *status);

/**
 * @brief Open or close pairing, as the adapter's button does.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param open true to open pairing, false to close it.
 * @return The command's result.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_set_pairing(joypad_port_t port, bool open);

/**
 * @brief Disconnect a port's device, or every port's.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param target The port whose device goes (#joypad_port_t), or #BRF_ALL_PORTS.
 * @return The command's result.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_disconnect(joypad_port_t port, int target);

/**
 * @brief Set how bright the adapter's LEDs get. The adapter saves it.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param level The level, from 1, the faintest: the LEDs can't be turned off.
 * @return The command's result: #BRF_BAD_ARGUMENT for a level the adapter hasn't got.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_set_led_level(joypad_port_t port, uint8_t level);

/**
 * @brief Read each port's keyboard settings.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param[out] settings Where to store each port's settings.
 * @return The command's result.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_get_keyboard_settings(joypad_port_t port, brf_keyboard_settings_t settings[JOYPAD_PORT_COUNT]);

/**
 * @brief Set a port's keyboard settings, or every port's.
 * @preview
 *
 * The adapter saves them, unless they're set until power off
 * (#brf_keyboard_settings_t.temporary). Changing a port's mode leaves it
 * silent for a moment, so a keyboard there disconnects and connects again,
 * in the new mode.
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param target The port to set (#joypad_port_t), or #BRF_ALL_PORTS.
 * @param settings The settings.
 * @return The command's result: #BRF_BAD_ARGUMENT for a layout the adapter hasn't got.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_set_keyboard_settings(joypad_port_t port, int target, const brf_keyboard_settings_t *settings);

#ifdef __cplusplus
}
#endif

/** @} */ /* blueretroforever */

#endif
