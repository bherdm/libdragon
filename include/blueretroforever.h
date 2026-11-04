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
 * @brief Pairing, status, keyboard settings and firmware for BlueRetroForever.
 *
 * BlueRetroForever is a Bluetooth adapter for the controller ports. Its
 * command 0x7B lets a program open and close pairing, see what's connected to
 * each port, disconnect a device, set how bright its LEDs get, read and
 * change each port's keyboard settings, put the adapter to sleep and wake
 * it, read its firmware's version, its Bluetooth address and its log, and
 * flash new firmware to it: a settings page in a menu, say, or a tool.
 *
 * Each call sends one command on one controller port and waits for the reply,
 * about a millisecond. That's fine for menus and settings screens, not for
 * every frame. The version and the log come back a request late, so reading
 * them takes 15 ms or so per four bytes. The adapter answers on any of its
 * ports, whatever is connected there. It takes a command's action after
 * replying, so a reply shows the state from before it: read the status again
 * to see pairing open, say, or a device gone. For a second or two after it
 * starts, it answers before its state is ready: the LED level 0, the board's
 * address all zeros, and the version and log #BRF_NOT_READY.
 *
 * The module holds no state, and a program that doesn't call it links none
 * of it. Only flashing changes the firmware; no command does a factory reset
 * or changes button mappings.
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
    /** @brief Not ready: the adapter still hadn't staged the version or the log after a few
     *         tries, as for a second or two after it starts. Ask again. */
    BRF_NOT_READY = -2,
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
    /** @brief A single-port adapter: the devices paired to it share its port. The adapter
     *         tells from the console's traffic, so it reads false for a moment after it starts. */
    bool single_port;
    /** @brief Asleep (#brf_sleep): no Bluetooth, so nothing can connect to it or find it. */
    bool asleep;
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

/** @brief A flash's state (#brf_flash_status). */
typedef enum
{
    /** @brief No flash begun. */
    BRF_FLASH_IDLE = 0,
    /** @brief Taking chunks. */
    BRF_FLASH_RECEIVING = 1,
    /** @brief Checked and set to boot: #brf_restart runs it. */
    BRF_FLASH_DONE = 2,
    /** @brief Failed: see #brf_flash_status_t.error. */
    BRF_FLASH_FAILED = 3,
} brf_flash_state_t;

/** @brief Why a flash failed. */
typedef enum
{
    /** @brief It didn't. */
    BRF_FLASH_ERROR_NONE = 0,
    /** @brief The adapter couldn't begin. */
    BRF_FLASH_ERROR_BEGIN = 1,
    /** @brief A write failed. */
    BRF_FLASH_ERROR_WRITE = 2,
    /** @brief The image isn't firmware. */
    BRF_FLASH_ERROR_IMAGE = 3,
    /** @brief The image is for other hardware than the adapter's. */
    BRF_FLASH_ERROR_HARDWARE = 4,
    /** @brief The image is incomplete or corrupt. */
    BRF_FLASH_ERROR_CHECK = 5,
    /** @brief The image couldn't be set to boot. */
    BRF_FLASH_ERROR_BOOT = 6,
} brf_flash_error_t;

/** @brief A flash's progress. */
typedef struct brf_flash_status_s
{
    /** @brief Its state. */
    brf_flash_state_t state;
    /** @brief Why it failed, if it did. */
    brf_flash_error_t error;
    /** @brief The bytes written so far. */
    uint32_t written;
} brf_flash_status_t;

/** @brief A flash chunk's result (#brf_flash_chunk). */
typedef enum
{
    /** @brief No reply: the adapter may be busy writing; send the chunk again. */
    BRF_CHUNK_NO_REPLY = -1,
    /** @brief Taken. */
    BRF_CHUNK_OK = 0,
    /** @brief Not the chunk expected: send the one the reply asks for. */
    BRF_CHUNK_OUT_OF_ORDER = 1,
    /** @brief The adapter's buffer is full: send it again. */
    BRF_CHUNK_BUSY = 2,
    /** @brief Garbled on the way: send it again. */
    BRF_CHUNK_BAD_CRC = 3,
    /** @brief No flash begun, or it ended. */
    BRF_CHUNK_NO_FLASH = 4,
} brf_chunk_result_t;

/** @brief The data a flash chunk carries, in bytes. */
#define BRF_FLASH_CHUNK_SIZE 32

/** @brief The longest firmware version string, without its terminator. */
#define BRF_VERSION_MAX 32

/** @brief A log position for #brf_read_log: start from the oldest byte the adapter keeps. */
#define BRF_LOG_OLDEST 0xFFFF

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
 * @param level The level, from 1, the faintest: awake, the LEDs can't be
 *              turned off. Asleep (#brf_sleep), they go dark whatever the level.
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

/**
 * @brief Put the adapter to sleep: no Bluetooth until it's woken.
 * @preview
 *
 * It disconnects every device and stops searching and being found, so
 * nothing can connect to it or find it until #brf_wake, a search asked for
 * (#brf_set_pairing), or power off. Sleep isn't saved, so a power cycle always
 * wakes it. A single-port adapter keeps its group, so its devices join again
 * after a wake. Asleep, its LEDs go dark, or glow faintly on some adapters.
 * The status shows it (#brf_status_t.asleep).
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @return The command's result: #BRF_UNKNOWN_VERB for firmware without it.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_sleep(joypad_port_t port);

/**
 * @brief Wake the adapter from sleep.
 * @preview
 *
 * Bluetooth comes back as at power on, and the LEDs light again.
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @return The command's result.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_wake(joypad_port_t port);

/**
 * @brief Restart the adapter, as at power on.
 * @preview
 *
 * Measured on a console: it goes on answering for about a second, goes
 * silent for half a second, so its ports disconnect and connect again, then
 * answers before it's ready for about another second (#BRF_NOT_READY). After
 * a flash (#BRF_FLASH_DONE), it starts the new firmware.
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @return The command's result.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_restart(joypad_port_t port);

/**
 * @brief Read the board's Bluetooth address.
 * @preview
 *
 * A board wired to several ports answers the same address on each, so a
 * program can tell one board from several.
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param[out] address Where to store the six bytes, most significant first.
 * @return The command's result.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_get_board(joypad_port_t port, uint8_t address[6]);

/**
 * @brief Read the firmware's version string.
 * @preview
 *
 * It comes four bytes a request, each a request late, so it takes up to
 * about 135 ms.
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param[out] version Where to store it, with its terminator.
 * @param size The buffer's size: #BRF_VERSION_MAX + 1 holds any.
 * @return The command's result: #BRF_UNKNOWN_VERB for firmware without it,
 *         #BRF_NOT_READY for an adapter just started.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_get_firmware_version(joypad_port_t port, char *version, int size);

/**
 * @brief Read the adapter's log, from a position up to what it has written.
 * @preview
 *
 * The adapter keeps the last part of what it logged since it booted. Pass
 * #BRF_LOG_OLDEST in @p position to start from the oldest byte kept; the
 * call leaves it after the last byte read, so calling again with it reads
 * what was logged since. A position the adapter no longer keeps starts from
 * the oldest it does. It comes four bytes a request, each a request late:
 * about 15 ms per four bytes, so read a little at a time. A call's bytes are
 * always one run of the log: where the adapter overwrites the next position
 * while it's read, the call stops there, and the next starts from the oldest
 * kept. A busy adapter can also answer where its log stood a moment before,
 * so a call can read less than there is, or nothing: the next call reads the
 * rest.
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param[in,out] position Where to read from, then where to read from next.
 * @param[out] buf Where to store the bytes.
 * @param size How many bytes to read at most.
 * @param[out] read How many were read.
 * @return The command's result: #BRF_UNKNOWN_VERB for firmware without it,
 *         #BRF_NOT_READY for an adapter just started.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_read_log(joypad_port_t port, uint16_t *position, void *buf, int size, int *read);

/**
 * @brief Begin a flash: new firmware follows in chunks (#brf_flash_chunk).
 * @preview
 *
 * The adapter takes a moment to begin: wait for #brf_flash_status to say
 * #BRF_FLASH_RECEIVING before the first chunk. Then send the image in order,
 * 32 bytes a chunk from sequence 0, the last padded with 0xFF; finish with
 * #brf_flash_end, wait for #BRF_FLASH_DONE, and #brf_restart runs it. The
 * firmware being replaced keeps running until then, and #brf_flash_abort
 * leaves it in place.
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @return The command's result: #BRF_UNKNOWN_VERB for firmware that can't be flashed this way.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_flash_begin(joypad_port_t port);

/**
 * @brief Send one chunk of a flash's image.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param sequence The chunk's number: its data is the image's 32 bytes at sequence * 32.
 * @param data The chunk's #BRF_FLASH_CHUNK_SIZE bytes.
 * @param[out] next The sequence the adapter expects next, or NULL: the one to
 *             send after #BRF_CHUNK_OUT_OF_ORDER.
 * @return The chunk's result: send the next chunk after #BRF_CHUNK_OK, and this
 *         one again after #BRF_CHUNK_BUSY, #BRF_CHUNK_BAD_CRC or #BRF_CHUNK_NO_REPLY.
 */
LIBDRAGON_PREVIEW_API
brf_chunk_result_t brf_flash_chunk(joypad_port_t port, uint16_t sequence, const void *data, uint16_t *next);

/**
 * @brief Read a flash's progress.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @param[out] status Where to store it.
 * @return The command's result.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_flash_status(joypad_port_t port, brf_flash_status_t *status);

/**
 * @brief End a flash: the image is whole. The adapter checks it, and sets it
 *        to boot from the next restart (#BRF_FLASH_DONE) or fails.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @return The command's result.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_flash_end(joypad_port_t port);

/**
 * @brief Abandon a flash, leaving the firmware as it was.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t) to send the command on.
 * @return The command's result.
 */
LIBDRAGON_PREVIEW_API
brf_result_t brf_flash_abort(joypad_port_t port);

#ifdef __cplusplus
}
#endif

/** @} */ /* blueretroforever */

#endif
