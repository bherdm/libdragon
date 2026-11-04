/**
 * @file joybus_commands.h
 * @author Christopher Bonhage <me@christopherbonhage.com>
 * @brief Joybus command helpers
 * @ingroup joybus_commands
 */

#ifndef __LIBDRAGON_JOYBUS_COMMANDS_H
#define __LIBDRAGON_JOYBUS_COMMANDS_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "joybus.h"

/**
 * @defgroup joybus_commands Joybus Commands
 * @ingroup peripherals
 * @brief Joybus command helpers and data structures.
 * 
 * Joybus commands are used to communicate with peripherals via the
 * N64's serial interface (SI). There are 4 ports on the front of the
 * N64 for connecting joypads and other devices, and one additional 
 * port on the cartridge connector for EEPROM and real-time clock.
 * 
 * A Joybus command consists of metadata, send data, and receive data.
 * 
 * Metadata is used to specify the length of the send and receive data.
 * It is always 2 bytes long, and does not include the command identifier.
 * 
 * The send data is the request-side of the command. Its shape and size
 * depends on the specific command that is being issued, but the send data
 * will always include a 1-byte command identifier at the beginning.
 * 
 * The receive data is the response-side of the command. Its shape and size
 * depends on the specific command that is being issued.
 * 
 * Joybus commands are composed into an "operation block" that issues a
 * command to each Joybus port on the system in order. A port can be skipped
 * by providing a single-byte command with a value of 0x00 (zero send length).
 * 
 * @{
 */

/**
 * @anchor JOYBUS_COMMAND_METADATA
 * @name Joybus command metadata values
 * @{
 */
/**
 * @brief Size of a Joybus command to "skip this port" in bytes.
 */
#define JOYBUS_COMMAND_SKIP_SIZE          1
/**
 * @brief Size of the Joybus command metadata fields in bytes.
 */
#define JOYBUS_COMMAND_METADATA_SIZE      2
/**
 * @brief Offset of the Joybus command "send length" field in bytes.
 */
#define JOYBUS_COMMAND_OFFSET_SEND_LEN    0
/**
 * @brief Offset of the Joybus command "receive length" field in bytes.
 */
#define JOYBUS_COMMAND_OFFSET_RECV_LEN    1
/**
 * @brief Offset of the Joybus command "command ID" field in bytes.
 */
#define JOYBUS_COMMAND_OFFSET_COMMAND_ID  2
/** @} */ /* JOYBUS_COMMAND_METADATA */

/**
 * @anchor JOYBUS_COMMAND_ID
 * @name Joybus command identifiers
 * @{
 */
/** @brief "Reset" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_RESET                       0xFF
/** @brief "Identify" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_IDENTIFY                    0x00
/** @brief "N64 Controller Read" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_N64_CONTROLLER_READ         0x01
/** @brief "N64 Accessory Read" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_N64_ACCESSORY_READ          0x02
/** @brief "N64 Accessory Write" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_N64_ACCESSORY_WRITE         0x03
/** @brief "EEPROM Read Block" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_EEPROM_READ_BLOCK           0x04
/** @brief "EEPROM Write Block" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_EEPROM_WRITE_BLOCK          0x05
/** @brief "Real-Time Clock Identify" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_RTC_IDENTIFY                0x06
/** @brief "Real-Time Clock Read Block" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_RTC_READ_BLOCK              0x07
/** @brief "Real-Time Clock Write Block" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_RTC_WRITE_BLOCK             0x08
/** @brief "N64 Randnet Keyboard Read" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_N64_RANDNET_KEYBOARD_READ   0x13
/**
 * @brief "BlueRetroForever Keyboard Read" Joybus command identifier.
 *
 * BlueRetroForever's read of a full-size keyboard it presents with the Randnet
 * keyboard's identity: ten keys as HID usages, with the layout it's set to. A
 * Randnet keyboard doesn't answer it.
 */
#define JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ     0x2A
/**
 * @brief "BlueRetroForever Keyboard Compact Read" Joybus command identifier.
 *
 * The read of every frame for a keyboard whose full read (#JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ)
 * answered version 3: the modifiers and five keys, without the marker.
 */
#define JOYBUS_COMMAND_ID_BRF_KEYBOARD_COMPACT_READ  0x2B
/**
 * @brief "BlueRetroForever Keyboard and Mouse Compact Read" Joybus command identifier.
 *
 * #JOYBUS_COMMAND_ID_BRF_KEYBOARD_COMPACT_READ with the port's mouse: its
 * buttons, both wheels and 12-bit motion.
 */
#define JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ    0x2C
/**
 * @brief "BlueRetroForever Command" Joybus command identifier.
 *
 * BlueRetroForever's commands from the console, on any port in any mode:
 * its status, pairing, disconnecting a device, its LEDs' level and each
 * port's keyboard settings.
 */
#define JOYBUS_COMMAND_ID_BRF_COMMAND           0x7B
/**
 * @brief "BlueRetroForever Flash Chunk" Joybus command identifier.
 *
 * 32 bytes of a firmware image for BlueRetroForever, between the command's
 * flash begin and flash end verbs.
 */
#define JOYBUS_COMMAND_ID_BRF_FLASH_CHUNK       0x7C
/** @brief "64GB Link Cable Read" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_64GB_LINK_CABLE_READ        0x13
/** @brief "GBA Link Cable Read" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_GBA_LINK_CABLE_READ         0x14
/** @brief "64GB Link Cable Write" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_64GB_LINK_CABLE_WRITE       0x14
/** @brief "GBA Link Cable Write" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_GBA_LINK_CABLE_WRITE        0x15
/**
 * @brief "PixelFX N64 Game ID" Joybus command identifier.
 * 
 * Used by PixelFX's N64Digital and Retro GEM console mods for per-game settings.
 * @see https://gitlab.com/pixelfx-public/n64-game-id#how-to-integrate-on-n64
 */
#define JOYBUS_COMMAND_ID_PIXELFX_N64_GAME_ID         0x1D
/** @brief "GameCube Controller Read" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_GCN_CONTROLLER_READ         0x40
/** @brief "GameCube Controller Read Origins" Joybus command identifier. */
#define JOYBUS_COMMAND_ID_GCN_CONTROLLER_ORIGIN       0x41
/**
 * @brief "GameCube Controller Recalibrate" Joybus command identifier.
 * 
 * Invalidates the origins of the GameCube controller.
 */
#define JOYBUS_COMMAND_ID_GCN_CONTROLLER_RECALIBRATE  0x42
/**
 * @brief "GameCube Controller 'Long Read'" Joybus command identifier.
 * 
 * Used by GameCube mode 0, mode 1, mode 2, and mode 4 to read
 * extended controller data, including analog A and B button values.
 */
#define JOYBUS_COMMAND_ID_GCN_CONTROLLER_READ_LONG    0x43
/** @} */ /* JOYBUS_COMMAND_ID */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief "N64 Accessory Read" Joybus command structure.
 * 
 * @see #JOYBUS_COMMAND_ID_N64_ACCESSORY_READ
 */
typedef struct __attribute__((packed)) joybus_cmd_n64_accessory_read_port_s
{
    /** @brief "N64 Accessory Read" command send data */
    struct __attribute__((packed))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_N64_ACCESSORY_READ) */
        uint8_t command;
        /** @brief Accessory address with 5-bit checksum. */
        uint16_t addr_checksum;
    } send;
    /** @brief "N64 Accessory Read" command receive data */
    struct __attribute__((packed))
    {
        /** @brief 32-byte payload of data read from the accessory. */
        uint8_t data[JOYBUS_ACCESSORY_DATA_SIZE];
        /** @brief CRC8 checksum of the data for verification. */
        uint8_t data_crc;
    } recv;
} joybus_cmd_n64_accessory_read_port_t;

/**
 * @brief "64GB Link Cable Read" Joybus command structure.
 * 
 * Identical to the "N64 Accessory Read" command but with a different command ID.
 * 
 * @see #JOYBUS_COMMAND_ID_64GB_LINK_CABLE_READ
 */
typedef joybus_cmd_n64_accessory_read_port_t joybus_cmd_64gb_link_cable_read_port_t;

/**
 * @brief "GBA Link Cable Read" Joybus command structure.
 * 
 * Identical to the "N64 Accessory Read" command but with a different command ID.
 * 
 * @see #JOYBUS_COMMAND_ID_GBA_LINK_CABLE_READ
 */
typedef joybus_cmd_n64_accessory_read_port_t joybus_cmd_gba_link_cable_read_port_t;

/**
 * @brief "N64 Accessory Write" Joybus command structure.
 * 
 * @see #JOYBUS_COMMAND_ID_N64_ACCESSORY_WRITE
 */
typedef struct __attribute__((packed)) joybus_cmd_n64_accessory_write_port_s
{
    /** @brief "N64 Accessory Write" command send data */
    struct __attribute__((packed))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_N64_ACCESSORY_WRITE) */
        uint8_t command;
        /** @brief Accessory address with 5-bit checksum. */
        uint16_t addr_checksum;
        /** @brief 32-byte payload of data to write to the accessory. */
        uint8_t data[JOYBUS_ACCESSORY_DATA_SIZE];

    } send;
    /** @brief "N64 Accessory Write" command receive data */
    struct __attribute__((packed))
    {
        /** @brief CRC8 checksum of the written data for verification. */
        uint8_t data_crc;
    } recv;
} joybus_cmd_n64_accessory_write_port_t;

/**
 * @brief "64GB Link Cable Write" Joybus command structure.
 * 
 * Identical to the "N64 Accessory Write" command but with a different command ID.
 * 
 * @see #JOYBUS_COMMAND_ID_64GB_LINK_CABLE_WRITE
 */
typedef joybus_cmd_n64_accessory_write_port_t joybus_cmd_64gb_link_cable_write_port_t;

/**
 * @brief "GBA Link Cable Write" Joybus command structure.
 * 
 * Identical to the "N64 Accessory Write" command but with a different command ID.
 * 
 * @see #JOYBUS_COMMAND_ID_GBA_LINK_CABLE_WRITE
 */
typedef joybus_cmd_n64_accessory_write_port_t joybus_cmd_gba_link_cable_write_port_t;

/**
 * @brief "Identify" Joybus command structure.
 * 
 * @see #JOYBUS_COMMAND_ID_IDENTIFY
 */
typedef struct __attribute__((packed)) joybus_cmd_identify_port_s
{
    /** @brief "Identify" command send data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_IDENTIFY) */
        uint8_t command;
    } send;
    /** @brief "Identify" command receive data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus device identifier */
        uint16_t identifier;
        /** @brief Joybus device status byte */
        uint8_t status;
    } recv;
} joybus_cmd_identify_port_t;

/**
 * @brief "Reset" Joybus command structure.
 * 
 * Identical to the "Identify" command but with a different command ID.
 * 
 * @see #JOYBUS_COMMAND_ID_RESET
 */
typedef joybus_cmd_identify_port_t joybus_cmd_reset_port_t;

/**
 * @brief "EEPROM Read Block" Joybus command structure.
 * 
 * @see #JOYBUS_COMMAND_ID_EEPROM_READ_BLOCK
 */
typedef struct __attribute__((packed)) joybus_cmd_eeprom_read_block_s
{
    /** @brief "EEPROM Read Block" command send data */
    struct __attribute__((packed))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_EEPROM_READ_BLOCK) */
        uint8_t command;
        /** @brief EEPROM block to read */
        uint8_t block;
    } send;
    /** @brief "EEPROM Read Block" command receive data */
    struct __attribute__((packed))
    {
        union __attribute__((packed))
        {
            /** @brief 8-byte payload from the EEPROM block */
            uint8_t data[8];
            /** @brief 64-bit payload from the EEPROM block */
            uint64_t dword;
        };
        /** @brief Status byte */
        uint8_t status;
    } recv;
} joybus_cmd_eeprom_read_block_t;

/**
 * @brief "RTC Read Block" Joybus command structure.
 * 
 * Identical to the "EEPROM Read Block" command but with a different command ID.
 * 
 * @see #JOYBUS_COMMAND_ID_RTC_READ_BLOCK
 */
typedef joybus_cmd_eeprom_read_block_t joybus_cmd_rtc_read_block_t;

/**
 * @brief "EEPROM Write Block" Joybus command structure.
 * 
 * @see #JOYBUS_COMMAND_ID_EEPROM_WRITE_BLOCK
 */
typedef struct __attribute__((packed)) joybus_cmd_eeprom_write_block_s
{
    /** @brief "EEPROM Write Block" command send data */
    struct __attribute__((packed))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_EEPROM_WRITE_BLOCK) */
        uint8_t command;
        /** @brief EEPROM block to read */
        uint8_t block;
        union __attribute__((packed))
        {
            /** @brief 8-byte payload for the EEPROM block */
            uint8_t data[8];
            /** @brief 64-bit payload for the EEPROM block */
            uint64_t dword;
        };
    } send;
    /** @brief "EEPROM Write Block" command receive data */
    struct __attribute__((packed))
    {
        /** @brief Status byte */
        uint8_t status;
    } recv;
} joybus_cmd_eeprom_write_block_t;

/**
 * @brief "RTC Write Block" Joybus command structure.
 * 
 * Identical to the "EEPROM Write Block" command but with a different command ID.
 * 
 * @see #JOYBUS_COMMAND_ID_RTC_WRITE_BLOCK
 */
typedef joybus_cmd_eeprom_write_block_t joybus_cmd_rtc_write_block_t;

/**
 * @brief "PixelFX N64 Game ID" Joybus command structure.
 * 
 * @see #JOYBUS_COMMAND_ID_PIXELFX_N64_GAME_ID
 */
typedef struct __attribute__((packed)) joybus_cmd_pixelfx_n64_game_id_s
{
    /** @brief "PixelFX N64 Game ID" command send data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_PIXELFX_N64_GAME_ID) */
        uint8_t command;
        /** @brief ROM check code (ROM header bytes 0x10-0x17).
         * 
         * 64-bit check code calculated on 1 Mbyte of ROM contents starting from offset 0x1000.
         * 
         * Sometimes these 8 bytes are incorrectly referred to as "CRC HI/LO" or "CRC1/2".
         */
        uint64_t rom_check_code;
        /** @brief Media category code (ROM header byte 0x3B) */
        uint8_t media_format;
        /** @brief Region code (ROM header byte 0x3E) */
        uint8_t region_code;
    } send;
    /**
     * @brief (unused)
     * 
     * As per PixelFX's documentation, N64Digital does not respond to this command.
     * N64Digital only acts as a bus sniffer.
     */
    uint8_t recv;
} joybus_cmd_pixelfx_n64_game_id_t;

/**
 * @brief "N64 Controller Read" Joybus command structure.
 * 
 * @see #JOYBUS_COMMAND_ID_N64_CONTROLLER_READ
 */
typedef struct __attribute__((packed)) joybus_cmd_n64_controller_read_port_s
{
    /** @brief "N64 Controller Read" command send data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_N64_CONTROLLER_READ) */
        uint8_t command;
    } send;
    /** @brief "N64 Controller Read" command receive data */
    struct __attribute__((__packed__))
    {
        unsigned a       : 1; ///< A button.
        unsigned b       : 1; ///< B button.
        unsigned z       : 1; ///< Z button.
        unsigned start   : 1; ///< Start button.
        unsigned d_up    : 1; ///< D-Pad up.
        unsigned d_down  : 1; ///< D-Pad down.
        unsigned d_left  : 1; ///< D-Pad left.
        unsigned d_right : 1; ///< D-Pad right.
        unsigned reset   : 1; ///< Reset flag.
        unsigned         : 1; ///< Unused padding.
        unsigned l       : 1; ///< L shoulder button.
        unsigned r       : 1; ///< R shoulder button.
        unsigned c_up    : 1; ///< C-Pad up.
        unsigned c_down  : 1; ///< C-Pad down.
        unsigned c_left  : 1; ///< C-Pad left.
        unsigned c_right : 1; ///< C-Pad right.
        signed   stick_x : 8; ///< Analog stick X-axis.
        signed   stick_y : 8; ///< Analog stick Y-axis.
    } recv;
} joybus_cmd_n64_controller_read_port_t;

/**
 * @anchor JOYBUS_BRF_KEYBOARD_READ
 * @name "BlueRetroForever Keyboard Read" reply values
 * @{
 */
/** @brief The first byte of every BlueRetroForever reply, telling it from a device that answers anything with its identity. */
#define JOYBUS_BRF_MARKER                         0x42
/** @brief The reply's version: 3, with HID usages, the settings byte and the status's device bits; the port answers the compact reads. */
#define JOYBUS_BRF_KEYBOARD_READ_VERSION          3
/** @brief How many key codes a reply holds. */
#define JOYBUS_BRF_KEYBOARD_READ_KEYS             10
/** @brief How many key codes a compact read holds. */
#define JOYBUS_BRF_KEYBOARD_COMPACT_KEYS          5
/** @brief Status bit: a keyboard is on the port. */
#define JOYBUS_BRF_KEYBOARD_STATUS_KEYBOARD       (1<<0)
/** @brief Status bit: a mouse is on the port. */
#define JOYBUS_BRF_KEYBOARD_STATUS_MOUSE          (1<<1)
/** @brief Status bit: more keys are held than the reply holds. */
#define JOYBUS_BRF_KEYBOARD_STATUS_EXCESS_KEYS    (1<<4)
/** @brief Status bit (the compact reads): the settings, or keys off the Keyboard/Keypad page, changed since the last full read. */
#define JOYBUS_BRF_KEYBOARD_STATUS_FULL_READ      (1<<5)
/** @brief The mouse buttons in a compact read's buttons byte. */
#define JOYBUS_BRF_MOUSE_BUTTONS                  0x1F
/** @brief A wheel's first bit, in a compact read's buttons byte and its horizontal wheel byte. */
#define JOYBUS_BRF_MOUSE_WHEEL_SHIFT              5
/** @brief Settings byte: the layout (#keyboard_layout_t). */
#define JOYBUS_BRF_KEYBOARD_SETTINGS_LAYOUT       0x07
/** @brief Settings byte: the arrangement's bit (#keyboard_arrangement_t). */
#define JOYBUS_BRF_KEYBOARD_SETTINGS_ARRANGEMENT_SHIFT 3
/** @brief Settings byte: classic mode, a Randnet keyboard for the Randnet's own software. */
#define JOYBUS_BRF_KEYBOARD_SETTINGS_CLASSIC      (1<<4)
/** @brief Settings byte: set until power off, not saved; read, the port's setting isn't the saved one. */
#define JOYBUS_BRF_KEYBOARD_SETTINGS_TEMPORARY    (1<<7)
/** @} */

/**
 * @brief "BlueRetroForever Keyboard Read" Joybus command structure.
 *
 * The keys are HID usages as the Keyboard subsystem's codes are
 * (#keyboard_key_t), from the first slot: the held modifiers, then the other
 * keys in the order they went down, then media and system keys. The settings
 * byte is the port's keyboard settings as BlueRetroForever's command 0x7B
 * reads them.
 *
 * @see #JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ
 */
typedef struct __attribute__((packed)) joybus_cmd_brf_keyboard_read_port_s
{
    /** @brief "BlueRetroForever Keyboard Read" command send data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_BRF_KEYBOARD_READ) */
        uint8_t command;
        /** @brief LEDs to light (@ref KEYBOARD_LED) */
        uint8_t leds;
    } send;
    /** @brief "BlueRetroForever Keyboard Read" command receive data */
    struct __attribute__((__packed__))
    {
        /** @brief #JOYBUS_BRF_MARKER */
        uint8_t marker;
        /** @brief #JOYBUS_BRF_KEYBOARD_READ_VERSION */
        uint8_t version;
        /** @brief Status byte (#JOYBUS_BRF_KEYBOARD_STATUS_EXCESS_KEYS) */
        uint8_t status;
        /** @brief The port's keyboard settings (@ref JOYBUS_BRF_KEYBOARD_READ) */
        uint8_t settings;
        /** @brief Up to ten held keys, then 0 */
        uint16_t keys[JOYBUS_BRF_KEYBOARD_READ_KEYS];
    } recv;
} joybus_cmd_brf_keyboard_read_port_t;

/**
 * @brief "BlueRetroForever Keyboard Compact Read" Joybus command structure.
 *
 * The keys are Keyboard/Keypad usages other than the modifiers, the first
 * five held in the order they went down.
 *
 * @see #JOYBUS_COMMAND_ID_BRF_KEYBOARD_COMPACT_READ
 */
typedef struct __attribute__((packed)) joybus_cmd_brf_keyboard_compact_read_port_s
{
    /** @brief "BlueRetroForever Keyboard Compact Read" command send data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_BRF_KEYBOARD_COMPACT_READ) */
        uint8_t command;
    } send;
    /** @brief "BlueRetroForever Keyboard Compact Read" command receive data */
    struct __attribute__((__packed__))
    {
        /** @brief Status byte (@ref JOYBUS_BRF_KEYBOARD_READ) */
        uint8_t status;
        /** @brief The held modifiers, as HID's bitmask */
        uint8_t modifiers;
        /** @brief Up to five held keys, then 0 */
        uint8_t keys[JOYBUS_BRF_KEYBOARD_COMPACT_KEYS];
    } recv;
} joybus_cmd_brf_keyboard_compact_read_port_t;

/**
 * @brief "BlueRetroForever Keyboard and Mouse Compact Read" Joybus command structure.
 *
 * The motion is the mouse's counts since the last of these reads, up to
 * 2047 each way on each axis; what doesn't fit comes with the next.
 *
 * @see #JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ
 */
typedef struct __attribute__((packed)) joybus_cmd_brf_keyboard_mouse_read_port_s
{
    /** @brief "BlueRetroForever Keyboard and Mouse Compact Read" command send data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_BRF_KEYBOARD_MOUSE_READ) */
        uint8_t command;
    } send;
    /** @brief "BlueRetroForever Keyboard and Mouse Compact Read" command receive data */
    struct __attribute__((__packed__))
    {
        /** @brief Status byte (@ref JOYBUS_BRF_KEYBOARD_READ) */
        uint8_t status;
        /** @brief The held modifiers, as HID's bitmask */
        uint8_t modifiers;
        /** @brief Buttons 1 to 5 held in bits 0-4; wheel clicks in bits 5-7, signed, away from the user positive */
        uint8_t buttons;
        /**
         * @brief Motion right and down, each signed 12 bits: x's bits 11-4,
         *        then x's bits 3-0 and y's bits 11-8, then y's bits 7-0
         */
        uint8_t motion[3];
        /** @brief Horizontal wheel clicks in bits 5-7, signed, right positive; bits 0-4 are 0 */
        uint8_t hwheel;
        /** @brief Up to five held keys, then 0 */
        uint8_t keys[JOYBUS_BRF_KEYBOARD_COMPACT_KEYS];
    } recv;
} joybus_cmd_brf_keyboard_mouse_read_port_t;

/**
 * @anchor JOYBUS_BRF_COMMAND
 * @name "BlueRetroForever Command" values
 * @{
 */
/** @brief The reply's protocol version. */
#define JOYBUS_BRF_COMMAND_VERSION                1
/** @brief Verb: status. */
#define JOYBUS_BRF_VERB_STATUS                    0x00
/** @brief Verb: open pairing. */
#define JOYBUS_BRF_VERB_OPEN_PAIRING              0x01
/** @brief Verb: close pairing. */
#define JOYBUS_BRF_VERB_CLOSE_PAIRING             0x02
/** @brief Verb: disconnect a port's device (arg0: port 0-3, or 0xFF for all). */
#define JOYBUS_BRF_VERB_DISCONNECT                0x03
/** @brief Verb: the LEDs' brightness level (arg0: 1 or more). */
#define JOYBUS_BRF_VERB_LED_LEVEL                 0x04
/** @brief Verb: each port's keyboard settings, in the reply's port bytes. */
#define JOYBUS_BRF_VERB_KEYBOARD_SETTINGS         0x05
/** @brief Verb: set keyboard settings (arg0: port 0-3, or 0xFF for all; arg1: a settings byte). */
#define JOYBUS_BRF_VERB_SET_KEYBOARD_SETTINGS     0x06
/** @brief Verb: stop searching for devices, and don't start again until woken. */
#define JOYBUS_BRF_VERB_SLEEP                     0x07
/** @brief Verb: wake from sleep. */
#define JOYBUS_BRF_VERB_WAKE                      0x08
/** @brief Verb: the log (arg0-1: a position, big-endian); the reply's bytes 4-9 are staged for the next request. */
#define JOYBUS_BRF_VERB_LOG                       0x09
/** @brief Verb: the firmware's version string (arg0: a chunk, 0-7); the reply's bytes 4-9 are staged for the next request. */
#define JOYBUS_BRF_VERB_FIRMWARE_VERSION          0x0A
/** @brief Verb: begin a flash: a firmware image follows in #JOYBUS_COMMAND_ID_BRF_FLASH_CHUNK commands. */
#define JOYBUS_BRF_VERB_FLASH_BEGIN               0x0B
/** @brief Verb: the image is whole: check it, and boot it from the next restart. */
#define JOYBUS_BRF_VERB_FLASH_END                 0x0C
/** @brief Verb: the flash's state, in the reply's bytes 4-9. */
#define JOYBUS_BRF_VERB_FLASH_STATUS              0x0D
/** @brief Verb: abandon a flash. */
#define JOYBUS_BRF_VERB_FLASH_ABORT               0x0E
/** @brief Verb: restart the adapter. */
#define JOYBUS_BRF_VERB_RESTART                   0x0F
/** @brief Verb: the board's Bluetooth address, in the reply's bytes 4-9. */
#define JOYBUS_BRF_VERB_BOARD                     0x10
/** @brief Flags bit: pairing is open. */
#define JOYBUS_BRF_FLAG_PAIRING                   (1<<0)
/** @brief Flags bit: a single-port adapter, whose paired devices share its port. */
#define JOYBUS_BRF_FLAG_SINGLE_PORT               (1<<1)
/** @brief Flags bit: asleep: it doesn't search for devices on its own. */
#define JOYBUS_BRF_FLAG_ASLEEP                    (1<<2)
/** @brief The log verb's position asking where the log is: the reply holds 0x8000 | the head, then the oldest position kept. */
#define JOYBUS_BRF_LOG_WHERE                      0x8000
/** @brief A log position's bits: positions count bytes since the adapter booted, modulo 0x8000. */
#define JOYBUS_BRF_LOG_POSITION                   0x7FFF
/** @brief How many chunks of four bytes the version string has at most. */
#define JOYBUS_BRF_VERSION_CHUNKS                 8
/** @brief A flash chunk's data size. */
#define JOYBUS_BRF_FLASH_CHUNK_SIZE               32
/** @} */

/**
 * @brief "BlueRetroForever Command" Joybus command structure.
 *
 * The reply is the same for every verb, and shows the state from before the
 * verb's action, which the adapter takes after replying.
 *
 * @see #JOYBUS_COMMAND_ID_BRF_COMMAND
 */
typedef struct __attribute__((packed)) joybus_cmd_brf_command_port_s
{
    /** @brief "BlueRetroForever Command" command send data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_BRF_COMMAND) */
        uint8_t command;
        /** @brief The verb (@ref JOYBUS_BRF_COMMAND) */
        uint8_t verb;
        /** @brief The verb's arguments */
        uint8_t arg[2];
    } send;
    /** @brief "BlueRetroForever Command" command receive data */
    struct __attribute__((__packed__))
    {
        /** @brief #JOYBUS_BRF_MARKER */
        uint8_t marker;
        /** @brief #JOYBUS_BRF_COMMAND_VERSION */
        uint8_t version;
        /** @brief The verb, echoed */
        uint8_t verb;
        /** @brief 0 accepted, 1 unknown verb, 2 bad argument */
        uint8_t result;
        /** @brief Flags (#JOYBUS_BRF_FLAG_PAIRING) */
        uint8_t flags;
        /** @brief The LEDs' brightness level */
        uint8_t led_level;
        /** @brief Each port's device (0 none, 1 gamepad, 2 keyboard, 3 mouse), or its keyboard settings byte */
        uint8_t ports[4];
    } recv;
} joybus_cmd_brf_command_port_t;

/**
 * @brief "BlueRetroForever Flash Chunk" Joybus command structure.
 *
 * The CRC is a Controller Pak write's, over the data; the adapter checks it.
 * The reply is the chunk's result, 0xA0 for taken, and the sequence the
 * adapter expects next.
 *
 * @see #JOYBUS_COMMAND_ID_BRF_FLASH_CHUNK
 */
typedef struct __attribute__((packed)) joybus_cmd_brf_flash_chunk_port_s
{
    /** @brief "BlueRetroForever Flash Chunk" command send data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_BRF_FLASH_CHUNK) */
        uint8_t command;
        /** @brief The chunk's sequence number, from 0 */
        uint16_t sequence;
        /** @brief The data's CRC */
        uint8_t crc;
        /** @brief The image's 32 bytes at sequence * 32 */
        uint8_t data[JOYBUS_BRF_FLASH_CHUNK_SIZE];
    } send;
    /** @brief "BlueRetroForever Flash Chunk" command receive data */
    struct __attribute__((__packed__))
    {
        /** @brief The chunk's result */
        uint8_t result;
        /** @brief The sequence number expected next */
        uint16_t next;
    } recv;
} joybus_cmd_brf_flash_chunk_port_t;

/**
 * @brief "GameCube Controller Read" Joybus command structure.
 * 
 * @see #JOYBUS_COMMAND_ID_GCN_CONTROLLER_READ
 */
typedef struct __attribute__((packed)) joybus_cmd_gcn_controller_read_port_s
{
    /** @brief "GameCube Controller Read" command send data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_GCN_CONTROLLER_READ) */
        uint8_t command;
        /** @brief GameCube controller mode (0-3) */
        uint8_t mode;
        /** @brief Rumble motor control */
        uint8_t rumble;
    } send;
    /** @brief "GameCube Controller Read" command receive data */
    struct __attribute__((__packed__))
    {
        unsigned              : 2; ///< Unused padding.
        unsigned check_origin : 1; ///< Origin check flag.
        unsigned start        : 1; ///< Start button.
        unsigned y            : 1; ///< Y button.
        unsigned x            : 1; ///< X button.
        unsigned b            : 1; ///< B button.
        unsigned a            : 1; ///< A button.
        unsigned use_origin   : 1; ///< Origin use flag.
        unsigned l            : 1; ///< L button.
        unsigned r            : 1; ///< R button.
        unsigned z            : 1; ///< Z button.
        unsigned d_up         : 1; ///< D-Pad up.
        unsigned d_down       : 1; ///< D-Pad down.
        unsigned d_right      : 1; ///< D-Pad right.
        unsigned d_left       : 1; ///< D-Pad left.
        unsigned stick_x      : 8; ///< Analog stick X-axis.
        unsigned stick_y      : 8; ///< Analog stick Y-axis.
        unsigned cstick_x     : 8; ///< Analog C-Stick X-axis.
        unsigned cstick_y     : 8; ///< Analog C-Stick Y-axis.
        unsigned analog_l     : 8; ///< Analog L trigger.
        unsigned analog_r     : 8; ///< Analog R trigger.
    } recv;
} joybus_cmd_gcn_controller_read_port_t;

/**
 * @brief "GameCube Controller Long Read" Joybus command structure.
 * 
 * @see #JOYBUS_COMMAND_ID_GCN_CONTROLLER_READ_LONG
 */
typedef struct __attribute__((packed)) joybus_cmd_gcn_controller_read_long_port_s
{
    /** @brief "GameCube Controller Long Read" command send data */
    struct __attribute__((__packed__))
    {
        /** @brief Joybus command ID (#JOYBUS_COMMAND_ID_GCN_CONTROLLER_READ_LONG) */
        uint8_t command;
    } send;
    /** @brief "GameCube Controller Long Read" command receive data */
    struct __attribute__((__packed__))
    {
        unsigned              : 2; ///< Unused padding.
        unsigned check_origin : 1; ///< Check origin flag.
        unsigned start        : 1; ///< Start button.
        unsigned y            : 1; ///< Y button.
        unsigned x            : 1; ///< X button.
        unsigned b            : 1; ///< B button.
        unsigned a            : 1; ///< A button.
        unsigned use_origin   : 1; ///< Use origin flag.
        unsigned l            : 1; ///< L button.
        unsigned r            : 1; ///< R button.
        unsigned z            : 1; ///< Z button.
        unsigned d_up         : 1; ///< D-Pad up.
        unsigned d_down       : 1; ///< D-Pad down.
        unsigned d_right      : 1; ///< D-Pad right.
        unsigned d_left       : 1; ///< D-Pad left.
        unsigned stick_x      : 8; ///< Analog stick X-axis.
        unsigned stick_y      : 8; ///< Analog stick Y-axis.
        unsigned cstick_x     : 8; ///< Analog C-Stick X-axis.
        unsigned cstick_y     : 8; ///< Analog C-Stick Y-axis.
        unsigned analog_l     : 8; ///< Analog L trigger.
        unsigned analog_r     : 8; ///< Analog R trigger.
        unsigned analog_a     : 8; ///< Analog A button.
        unsigned analog_b     : 8; ///< Analog B button.
    } recv;
} joybus_cmd_gcn_controller_read_long_port_t;

/**
 * @brief "GameCube Controller Recalibrate" Joybus command structure.
 * 
 * Identical to the "GameCube Controller Long Read" command but with a different command ID.
 * 
 * @see #JOYBUS_COMMAND_ID_GCN_CONTROLLER_RECALIBRATE
 */
typedef joybus_cmd_gcn_controller_read_long_port_t joybus_cmd_gcn_controller_recalibrate_port_t;

/**
 * @brief "GameCube Controller Read Origins" Joybus command structure.
 * 
* Identical to the "GameCube Controller Long Read" command but with a different command ID.
 * 
 * @see #JOYBUS_COMMAND_ID_GCN_CONTROLLER_ORIGIN
 */
typedef joybus_cmd_gcn_controller_read_long_port_t joybus_cmd_gcn_controller_origin_port_t;

#ifdef __cplusplus
}
#endif

/** @} */ /* joybus_commands */

#endif
