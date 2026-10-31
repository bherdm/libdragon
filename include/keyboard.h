/**
 * @file keyboard.h
 * @brief Keyboard Subsystem
 * @ingroup keyboard
 */

#ifndef __LIBDRAGON_KEYBOARD_H
#define __LIBDRAGON_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "preview.h"
#include "joypad.h"

/**
 * @defgroup keyboard Keyboard Subsystem
 * @ingroup peripherals
 * @brief Full-size keyboards, through BlueRetroForever.
 *
 * The Keyboard subsystem reads the full-size Bluetooth keyboards that
 * BlueRetroForever, a controller adapter, connects to the controller ports:
 * every key of them, ten at once, on any port.
 *
 * To use it, call #keyboard_init. Keyboards are found by the Joybus
 * subsystem's background detection, so plugging one in or pulling it out is
 * noticed within a second. While one is connected, the subsystem reads every
 * keyboard in one Joybus operation of its own on each vertical blank, beside
 * the @ref joypad "Joypad subsystem's" read, and with none connected it reads
 * nothing. It is independent of the Joypad subsystem: a program that doesn't
 * call #keyboard_init links none of it, and its joypad reads are unchanged.
 *
 * Every read is compared with the one before it, under interrupt, and each
 * key that went down or up becomes an event (#keyboard_event_t) in a queue.
 * Take them with #keyboard_get_event. Since the events are made as the reads
 * arrive, a game running below the display's frame rate still sees every key.
 * #keyboard_get_keys gives the keys held as of the last read instead.
 *
 * Keys (#keyboard_key_t) are the keyboard's own HID usages: where a key is,
 * not what it types. Their names are their places on a US keyboard, as SDL's
 * scancodes are, so #KEYBOARD_KEY_Q is the key right of Tab, which types A on
 * a French keyboard. What a key types is the program's to decide, from the
 * layout and arrangement the adapter is set to for that keyboard
 * (#keyboard_get_layout, #keyboard_get_arrangement), Shift and Caps Lock, and
 * key repeat.
 *
 * A keyboard reports ten keys at once, the modifiers among them: the held
 * modifiers first, then the other keys in the order they went down, then
 * media and system keys. With more held, it reports the first ten and says so
 * (#keyboard_keys_t.overflow).
 *
 * #keyboard_set_leds lights the keyboard's lock LEDs. None are lit by default,
 * and Caps Lock's LED doesn't follow the key by itself: the program decides
 * what Caps Lock does, and lights it.
 *
 * BlueRetroForever presents a keyboard with the Randnet keyboard's identity
 * and answers its own read, command 0x2A, which the subsystem sends when a
 * keyboard connects. A Randnet keyboard (RND-001), or a port the adapter has
 * set to classic mode for the Randnet's own software, doesn't answer it, and
 * isn't a keyboard to this subsystem. When a port's mode changes, the adapter
 * leaves it silent for longer than detection takes to notice, so the keyboard
 * goes and comes back as the new mode's.
 *
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A key: its HID usage.
 *
 * A Keyboard/Keypad page key is its usage, 0x0004 to 0x00E7; a key on
 * another page is its usage in the page's range (@ref KEYBOARD_KEY_PAGES).
 */
typedef uint16_t keyboard_key_t;

/**
 * @anchor KEYBOARD_KEY_PAGES
 * @name Keys beyond the Keyboard/Keypad page
 *
 * Media keys and system keys aren't on HID's Keyboard/Keypad page. Each page
 * has a range of its own, and each macro builds a key's code from its usage.
 * @{
 */
/** @brief A Consumer page key (HID page 0x0C): media keys, usages 0x001 to 0xFFF. */
#define KEYBOARD_KEY_CONSUMER(usage) ((keyboard_key_t)(0x1000 | (usage)))
/** @brief A Generic Desktop page key (HID page 0x01): system controls, such as Sleep. */
#define KEYBOARD_KEY_DESKTOP(usage)  ((keyboard_key_t)(0x2000 | (usage)))
/** @brief A key on Apple's vendor page (HID page 0xFF): the fn key. */
#define KEYBOARD_KEY_APPLE(usage)    ((keyboard_key_t)(0x3000 | (usage)))
/** @brief A key's page: 0 Keyboard/Keypad, 1 Consumer, 2 Generic Desktop, 3 Apple's. */
#define KEYBOARD_KEY_PAGE(key)       ((key) >> 12)
/** @brief Is a key a modifier (Ctrl, Shift, Alt and the Windows or Command key, either side)? */
#define KEYBOARD_KEY_IS_MODIFIER(key) ((key) >= KEYBOARD_KEY_LCTRL && (key) <= KEYBOARD_KEY_RGUI)
/** @} */

/**
 * @anchor KEYBOARD_KEY
 * @name Keys
 *
 * The Keyboard/Keypad page's keys by their US places, as SDL's scancodes name
 * them, then the media and system keys keyboards have. Any other usage on
 * those pages arrives too, as its page's code.
 * @{
 */
#define KEYBOARD_KEY_NONE                   0x00 ///< No key
#define KEYBOARD_KEY_A                      0x04 ///< A
#define KEYBOARD_KEY_B                      0x05 ///< B
#define KEYBOARD_KEY_C                      0x06 ///< C
#define KEYBOARD_KEY_D                      0x07 ///< D
#define KEYBOARD_KEY_E                      0x08 ///< E
#define KEYBOARD_KEY_F                      0x09 ///< F
#define KEYBOARD_KEY_G                      0x0A ///< G
#define KEYBOARD_KEY_H                      0x0B ///< H
#define KEYBOARD_KEY_I                      0x0C ///< I
#define KEYBOARD_KEY_J                      0x0D ///< J
#define KEYBOARD_KEY_K                      0x0E ///< K
#define KEYBOARD_KEY_L                      0x0F ///< L
#define KEYBOARD_KEY_M                      0x10 ///< M
#define KEYBOARD_KEY_N                      0x11 ///< N
#define KEYBOARD_KEY_O                      0x12 ///< O
#define KEYBOARD_KEY_P                      0x13 ///< P
#define KEYBOARD_KEY_Q                      0x14 ///< Q
#define KEYBOARD_KEY_R                      0x15 ///< R
#define KEYBOARD_KEY_S                      0x16 ///< S
#define KEYBOARD_KEY_T                      0x17 ///< T
#define KEYBOARD_KEY_U                      0x18 ///< U
#define KEYBOARD_KEY_V                      0x19 ///< V
#define KEYBOARD_KEY_W                      0x1A ///< W
#define KEYBOARD_KEY_X                      0x1B ///< X
#define KEYBOARD_KEY_Y                      0x1C ///< Y
#define KEYBOARD_KEY_Z                      0x1D ///< Z
#define KEYBOARD_KEY_1                      0x1E ///< 1 (the top row)
#define KEYBOARD_KEY_2                      0x1F ///< 2 (the top row)
#define KEYBOARD_KEY_3                      0x20 ///< 3 (the top row)
#define KEYBOARD_KEY_4                      0x21 ///< 4 (the top row)
#define KEYBOARD_KEY_5                      0x22 ///< 5 (the top row)
#define KEYBOARD_KEY_6                      0x23 ///< 6 (the top row)
#define KEYBOARD_KEY_7                      0x24 ///< 7 (the top row)
#define KEYBOARD_KEY_8                      0x25 ///< 8 (the top row)
#define KEYBOARD_KEY_9                      0x26 ///< 9 (the top row)
#define KEYBOARD_KEY_0                      0x27 ///< 0 (the top row)
#define KEYBOARD_KEY_RETURN                 0x28 ///< Return (Enter)
#define KEYBOARD_KEY_ESCAPE                 0x29 ///< Escape
#define KEYBOARD_KEY_BACKSPACE              0x2A ///< Backspace (Delete on a Mac)
#define KEYBOARD_KEY_TAB                    0x2B ///< Tab
#define KEYBOARD_KEY_SPACE                  0x2C ///< Space
#define KEYBOARD_KEY_MINUS                  0x2D ///< - _ on US
#define KEYBOARD_KEY_EQUALS                 0x2E ///< = + on US
#define KEYBOARD_KEY_LEFTBRACKET            0x2F ///< [ { on US, @ ` on JIS
#define KEYBOARD_KEY_RIGHTBRACKET           0x30 ///< ] } on US, [ { on JIS
#define KEYBOARD_KEY_BACKSLASH              0x31 ///< \ | above Enter on ANSI
#define KEYBOARD_KEY_NONUSHASH              0x32 ///< # ~ left of Enter on ISO, ] } on JIS
#define KEYBOARD_KEY_SEMICOLON              0x33 ///< ; : on US
#define KEYBOARD_KEY_APOSTROPHE             0x34 ///< ' " on US, : * on JIS
#define KEYBOARD_KEY_GRAVE                  0x35 ///< ` ~ on US, Hankaku/Zenkaku on JIS, ^ ° on German
#define KEYBOARD_KEY_COMMA                  0x36 ///< , < on US
#define KEYBOARD_KEY_PERIOD                 0x37 ///< . > on US
#define KEYBOARD_KEY_SLASH                  0x38 ///< / ? on US
#define KEYBOARD_KEY_CAPSLOCK               0x39 ///< Caps Lock (Eisu on JIS)
#define KEYBOARD_KEY_F1                     0x3A ///< F1
#define KEYBOARD_KEY_F2                     0x3B ///< F2
#define KEYBOARD_KEY_F3                     0x3C ///< F3
#define KEYBOARD_KEY_F4                     0x3D ///< F4
#define KEYBOARD_KEY_F5                     0x3E ///< F5
#define KEYBOARD_KEY_F6                     0x3F ///< F6
#define KEYBOARD_KEY_F7                     0x40 ///< F7
#define KEYBOARD_KEY_F8                     0x41 ///< F8
#define KEYBOARD_KEY_F9                     0x42 ///< F9
#define KEYBOARD_KEY_F10                    0x43 ///< F10
#define KEYBOARD_KEY_F11                    0x44 ///< F11
#define KEYBOARD_KEY_F12                    0x45 ///< F12
#define KEYBOARD_KEY_PRINTSCREEN            0x46 ///< Print Screen
#define KEYBOARD_KEY_SCROLLLOCK             0x47 ///< Scroll Lock
#define KEYBOARD_KEY_PAUSE                  0x48 ///< Pause (Break)
#define KEYBOARD_KEY_INSERT                 0x49 ///< Insert (Help on old Macs)
#define KEYBOARD_KEY_HOME                   0x4A ///< Home
#define KEYBOARD_KEY_PAGEUP                 0x4B ///< Page Up
#define KEYBOARD_KEY_DELETE                 0x4C ///< Delete (forward)
#define KEYBOARD_KEY_END                    0x4D ///< End
#define KEYBOARD_KEY_PAGEDOWN               0x4E ///< Page Down
#define KEYBOARD_KEY_RIGHT                  0x4F ///< Right arrow
#define KEYBOARD_KEY_LEFT                   0x50 ///< Left arrow
#define KEYBOARD_KEY_DOWN                   0x51 ///< Down arrow
#define KEYBOARD_KEY_UP                     0x52 ///< Up arrow
#define KEYBOARD_KEY_NUMLOCKCLEAR           0x53 ///< Num Lock (Clear on a Mac)
#define KEYBOARD_KEY_KP_DIVIDE              0x54 ///< Keypad /
#define KEYBOARD_KEY_KP_MULTIPLY            0x55 ///< Keypad *
#define KEYBOARD_KEY_KP_MINUS               0x56 ///< Keypad -
#define KEYBOARD_KEY_KP_PLUS                0x57 ///< Keypad +
#define KEYBOARD_KEY_KP_ENTER               0x58 ///< Keypad Enter
#define KEYBOARD_KEY_KP_1                   0x59 ///< Keypad 1
#define KEYBOARD_KEY_KP_2                   0x5A ///< Keypad 2
#define KEYBOARD_KEY_KP_3                   0x5B ///< Keypad 3
#define KEYBOARD_KEY_KP_4                   0x5C ///< Keypad 4
#define KEYBOARD_KEY_KP_5                   0x5D ///< Keypad 5
#define KEYBOARD_KEY_KP_6                   0x5E ///< Keypad 6
#define KEYBOARD_KEY_KP_7                   0x5F ///< Keypad 7
#define KEYBOARD_KEY_KP_8                   0x60 ///< Keypad 8
#define KEYBOARD_KEY_KP_9                   0x61 ///< Keypad 9
#define KEYBOARD_KEY_KP_0                   0x62 ///< Keypad 0
#define KEYBOARD_KEY_KP_PERIOD              0x63 ///< Keypad .
#define KEYBOARD_KEY_NONUSBACKSLASH         0x64 ///< \ | left of Z on ISO, < > on German and French
#define KEYBOARD_KEY_APPLICATION            0x65 ///< Menu (Application)
#define KEYBOARD_KEY_POWER                  0x66 ///< Power (keyboard page)
#define KEYBOARD_KEY_KP_EQUALS              0x67 ///< Keypad = (Mac)
#define KEYBOARD_KEY_F13                    0x68 ///< F13
#define KEYBOARD_KEY_F14                    0x69 ///< F14
#define KEYBOARD_KEY_F15                    0x6A ///< F15
#define KEYBOARD_KEY_F16                    0x6B ///< F16
#define KEYBOARD_KEY_F17                    0x6C ///< F17
#define KEYBOARD_KEY_F18                    0x6D ///< F18
#define KEYBOARD_KEY_F19                    0x6E ///< F19
#define KEYBOARD_KEY_F20                    0x6F ///< F20
#define KEYBOARD_KEY_F21                    0x70 ///< F21
#define KEYBOARD_KEY_F22                    0x71 ///< F22
#define KEYBOARD_KEY_F23                    0x72 ///< F23
#define KEYBOARD_KEY_F24                    0x73 ///< F24
#define KEYBOARD_KEY_EXECUTE                0x74 ///< Execute
#define KEYBOARD_KEY_HELP                   0x75 ///< Help
#define KEYBOARD_KEY_MENU                   0x76 ///< Menu
#define KEYBOARD_KEY_SELECT                 0x77 ///< Select
#define KEYBOARD_KEY_STOP                   0x78 ///< Stop
#define KEYBOARD_KEY_AGAIN                  0x79 ///< Again
#define KEYBOARD_KEY_UNDO                   0x7A ///< Undo
#define KEYBOARD_KEY_CUT                    0x7B ///< Cut
#define KEYBOARD_KEY_COPY                   0x7C ///< Copy
#define KEYBOARD_KEY_PASTE                  0x7D ///< Paste
#define KEYBOARD_KEY_FIND                   0x7E ///< Find
#define KEYBOARD_KEY_MUTE                   0x7F ///< Mute (keyboard page)
#define KEYBOARD_KEY_VOLUMEUP               0x80 ///< Volume Up (keyboard page)
#define KEYBOARD_KEY_VOLUMEDOWN             0x81 ///< Volume Down (keyboard page)
#define KEYBOARD_KEY_KP_COMMA               0x85 ///< Keypad , (Brazilian, JIS Mac)
#define KEYBOARD_KEY_KP_EQUALSAS400         0x86 ///< Keypad = (AS/400)
#define KEYBOARD_KEY_INTERNATIONAL1         0x87 ///< \ _ (Ro) left of right Shift on JIS
#define KEYBOARD_KEY_INTERNATIONAL2         0x88 ///< Katakana/Hiragana on JIS
#define KEYBOARD_KEY_INTERNATIONAL3         0x89 ///< Yen \ | left of Backspace on JIS
#define KEYBOARD_KEY_INTERNATIONAL4         0x8A ///< Henkan on JIS
#define KEYBOARD_KEY_INTERNATIONAL5         0x8B ///< Muhenkan on JIS
#define KEYBOARD_KEY_INTERNATIONAL6         0x8C ///< International 6
#define KEYBOARD_KEY_INTERNATIONAL7         0x8D ///< International 7
#define KEYBOARD_KEY_INTERNATIONAL8         0x8E ///< International 8
#define KEYBOARD_KEY_INTERNATIONAL9         0x8F ///< International 9
#define KEYBOARD_KEY_LANG1                  0x90 ///< Kana on a JIS Mac, Han/Yeong on Korean
#define KEYBOARD_KEY_LANG2                  0x91 ///< Eisu on a JIS Mac, Hanja on Korean
#define KEYBOARD_KEY_LANG3                  0x92 ///< Katakana
#define KEYBOARD_KEY_LANG4                  0x93 ///< Hiragana
#define KEYBOARD_KEY_LANG5                  0x94 ///< Zenkaku/Hankaku
#define KEYBOARD_KEY_LANG6                  0x95 ///< Language 6
#define KEYBOARD_KEY_LANG7                  0x96 ///< Language 7
#define KEYBOARD_KEY_LANG8                  0x97 ///< Language 8
#define KEYBOARD_KEY_LANG9                  0x98 ///< Language 9
#define KEYBOARD_KEY_ALTERASE               0x99 ///< Alternate Erase
#define KEYBOARD_KEY_SYSREQ                 0x9A ///< SysReq
#define KEYBOARD_KEY_CANCEL                 0x9B ///< Cancel
#define KEYBOARD_KEY_CLEAR                  0x9C ///< Clear
#define KEYBOARD_KEY_PRIOR                  0x9D ///< Prior
#define KEYBOARD_KEY_RETURN2                0x9E ///< Return (the second)
#define KEYBOARD_KEY_SEPARATOR              0x9F ///< Separator
#define KEYBOARD_KEY_OUT                    0xA0 ///< Out
#define KEYBOARD_KEY_OPER                   0xA1 ///< Oper
#define KEYBOARD_KEY_CLEARAGAIN             0xA2 ///< Clear/Again
#define KEYBOARD_KEY_CRSEL                  0xA3 ///< CrSel
#define KEYBOARD_KEY_EXSEL                  0xA4 ///< ExSel
#define KEYBOARD_KEY_KP_00                  0xB0 ///< Keypad 00
#define KEYBOARD_KEY_KP_000                 0xB1 ///< Keypad 000
#define KEYBOARD_KEY_THOUSANDSSEPARATOR     0xB2 ///< Thousands separator
#define KEYBOARD_KEY_DECIMALSEPARATOR       0xB3 ///< Decimal separator
#define KEYBOARD_KEY_CURRENCYUNIT           0xB4 ///< Currency unit
#define KEYBOARD_KEY_CURRENCYSUBUNIT        0xB5 ///< Currency sub-unit
#define KEYBOARD_KEY_KP_LEFTPAREN           0xB6 ///< Keypad (
#define KEYBOARD_KEY_KP_RIGHTPAREN          0xB7 ///< Keypad )
#define KEYBOARD_KEY_KP_LEFTBRACE           0xB8 ///< Keypad {
#define KEYBOARD_KEY_KP_RIGHTBRACE          0xB9 ///< Keypad }
#define KEYBOARD_KEY_KP_TAB                 0xBA ///< Keypad Tab
#define KEYBOARD_KEY_KP_BACKSPACE           0xBB ///< Keypad Backspace
#define KEYBOARD_KEY_KP_A                   0xBC ///< Keypad A
#define KEYBOARD_KEY_KP_B                   0xBD ///< Keypad B
#define KEYBOARD_KEY_KP_C                   0xBE ///< Keypad C
#define KEYBOARD_KEY_KP_D                   0xBF ///< Keypad D
#define KEYBOARD_KEY_KP_E                   0xC0 ///< Keypad E
#define KEYBOARD_KEY_KP_F                   0xC1 ///< Keypad F
#define KEYBOARD_KEY_KP_XOR                 0xC2 ///< Keypad XOR
#define KEYBOARD_KEY_KP_POWER               0xC3 ///< Keypad ^
#define KEYBOARD_KEY_KP_PERCENT             0xC4 ///< Keypad %
#define KEYBOARD_KEY_KP_LESS                0xC5 ///< Keypad <
#define KEYBOARD_KEY_KP_GREATER             0xC6 ///< Keypad >
#define KEYBOARD_KEY_KP_AMPERSAND           0xC7 ///< Keypad &
#define KEYBOARD_KEY_KP_DBLAMPERSAND        0xC8 ///< Keypad &&
#define KEYBOARD_KEY_KP_VERTICALBAR         0xC9 ///< Keypad |
#define KEYBOARD_KEY_KP_DBLVERTICALBAR      0xCA ///< Keypad ||
#define KEYBOARD_KEY_KP_COLON               0xCB ///< Keypad :
#define KEYBOARD_KEY_KP_HASH                0xCC ///< Keypad #
#define KEYBOARD_KEY_KP_SPACE               0xCD ///< Keypad Space
#define KEYBOARD_KEY_KP_AT                  0xCE ///< Keypad @
#define KEYBOARD_KEY_KP_EXCLAM              0xCF ///< Keypad !
#define KEYBOARD_KEY_KP_MEMSTORE            0xD0 ///< Keypad Memory Store
#define KEYBOARD_KEY_KP_MEMRECALL           0xD1 ///< Keypad Memory Recall
#define KEYBOARD_KEY_KP_MEMCLEAR            0xD2 ///< Keypad Memory Clear
#define KEYBOARD_KEY_KP_MEMADD              0xD3 ///< Keypad Memory Add
#define KEYBOARD_KEY_KP_MEMSUBTRACT         0xD4 ///< Keypad Memory Subtract
#define KEYBOARD_KEY_KP_MEMMULTIPLY         0xD5 ///< Keypad Memory Multiply
#define KEYBOARD_KEY_KP_MEMDIVIDE           0xD6 ///< Keypad Memory Divide
#define KEYBOARD_KEY_KP_PLUSMINUS           0xD7 ///< Keypad +/-
#define KEYBOARD_KEY_KP_CLEAR               0xD8 ///< Keypad Clear
#define KEYBOARD_KEY_KP_CLEARENTRY          0xD9 ///< Keypad Clear Entry
#define KEYBOARD_KEY_KP_BINARY              0xDA ///< Keypad Binary
#define KEYBOARD_KEY_KP_OCTAL               0xDB ///< Keypad Octal
#define KEYBOARD_KEY_KP_DECIMAL             0xDC ///< Keypad Decimal
#define KEYBOARD_KEY_KP_HEXADECIMAL         0xDD ///< Keypad Hexadecimal
#define KEYBOARD_KEY_LCTRL                  0xE0 ///< Left Ctrl (Control on a Mac)
#define KEYBOARD_KEY_LSHIFT                 0xE1 ///< Left Shift
#define KEYBOARD_KEY_LALT                   0xE2 ///< Left Alt (Option on a Mac)
#define KEYBOARD_KEY_LGUI                   0xE3 ///< Left Windows (Command on a Mac)
#define KEYBOARD_KEY_RCTRL                  0xE4 ///< Right Ctrl
#define KEYBOARD_KEY_RSHIFT                 0xE5 ///< Right Shift
#define KEYBOARD_KEY_RALT                   0xE6 ///< Right Alt (AltGr; Option on a Mac)
#define KEYBOARD_KEY_RGUI                   0xE7 ///< Right Windows (Command on a Mac)
#define KEYBOARD_KEY_SNAPSHOT               KEYBOARD_KEY_CONSUMER(0x65) ///< Screenshot (Consumer Snapshot)
#define KEYBOARD_KEY_BRIGHTNESS_UP          KEYBOARD_KEY_CONSUMER(0x6F) ///< Display Brightness Up
#define KEYBOARD_KEY_BRIGHTNESS_DOWN        KEYBOARD_KEY_CONSUMER(0x70) ///< Display Brightness Down
#define KEYBOARD_KEY_BACKLIGHT_UP           KEYBOARD_KEY_CONSUMER(0x79) ///< Keyboard Backlight Up
#define KEYBOARD_KEY_BACKLIGHT_DOWN         KEYBOARD_KEY_CONSUMER(0x7A) ///< Keyboard Backlight Down
#define KEYBOARD_KEY_BACKLIGHT_TOGGLE       KEYBOARD_KEY_CONSUMER(0x7C) ///< Keyboard Backlight Toggle
#define KEYBOARD_KEY_MEDIA_PLAY             KEYBOARD_KEY_CONSUMER(0xB0) ///< Play
#define KEYBOARD_KEY_MEDIA_PAUSE            KEYBOARD_KEY_CONSUMER(0xB1) ///< Pause
#define KEYBOARD_KEY_MEDIA_RECORD           KEYBOARD_KEY_CONSUMER(0xB2) ///< Record
#define KEYBOARD_KEY_MEDIA_FAST_FORWARD     KEYBOARD_KEY_CONSUMER(0xB3) ///< Fast Forward
#define KEYBOARD_KEY_MEDIA_REWIND           KEYBOARD_KEY_CONSUMER(0xB4) ///< Rewind
#define KEYBOARD_KEY_MEDIA_NEXT_TRACK       KEYBOARD_KEY_CONSUMER(0xB5) ///< Next Track
#define KEYBOARD_KEY_MEDIA_PREVIOUS_TRACK   KEYBOARD_KEY_CONSUMER(0xB6) ///< Previous Track
#define KEYBOARD_KEY_MEDIA_STOP             KEYBOARD_KEY_CONSUMER(0xB7) ///< Stop
#define KEYBOARD_KEY_MEDIA_EJECT            KEYBOARD_KEY_CONSUMER(0xB8) ///< Eject
#define KEYBOARD_KEY_MEDIA_PLAY_PAUSE       KEYBOARD_KEY_CONSUMER(0xCD) ///< Play/Pause
#define KEYBOARD_KEY_VOICE_COMMAND          KEYBOARD_KEY_CONSUMER(0xCF) ///< Dictation, Siri (Voice Command)
#define KEYBOARD_KEY_VOLUME_MUTE            KEYBOARD_KEY_CONSUMER(0xE2) ///< Mute
#define KEYBOARD_KEY_VOLUME_INCREMENT       KEYBOARD_KEY_CONSUMER(0xE9) ///< Volume Up
#define KEYBOARD_KEY_VOLUME_DECREMENT       KEYBOARD_KEY_CONSUMER(0xEA) ///< Volume Down
#define KEYBOARD_KEY_MEDIA_SELECT           KEYBOARD_KEY_CONSUMER(0x183) ///< Media Player
#define KEYBOARD_KEY_AL_EMAIL               KEYBOARD_KEY_CONSUMER(0x18A) ///< Mail
#define KEYBOARD_KEY_AL_CALCULATOR          KEYBOARD_KEY_CONSUMER(0x192) ///< Calculator
#define KEYBOARD_KEY_AL_LOCAL_BROWSER       KEYBOARD_KEY_CONSUMER(0x194) ///< Files (My Computer)
#define KEYBOARD_KEY_AL_TERMINAL_LOCK       KEYBOARD_KEY_CONSUMER(0x19E) ///< Lock the screen
#define KEYBOARD_KEY_AC_NEW                 KEYBOARD_KEY_CONSUMER(0x201) ///< New
#define KEYBOARD_KEY_AC_OPEN                KEYBOARD_KEY_CONSUMER(0x202) ///< Open
#define KEYBOARD_KEY_AC_CLOSE               KEYBOARD_KEY_CONSUMER(0x203) ///< Close
#define KEYBOARD_KEY_AC_EXIT                KEYBOARD_KEY_CONSUMER(0x204) ///< Exit
#define KEYBOARD_KEY_AC_SAVE                KEYBOARD_KEY_CONSUMER(0x207) ///< Save
#define KEYBOARD_KEY_AC_PRINT               KEYBOARD_KEY_CONSUMER(0x208) ///< Print
#define KEYBOARD_KEY_AC_PROPERTIES          KEYBOARD_KEY_CONSUMER(0x209) ///< Properties
#define KEYBOARD_KEY_AC_SEARCH              KEYBOARD_KEY_CONSUMER(0x221) ///< Search (Spotlight)
#define KEYBOARD_KEY_AC_HOME                KEYBOARD_KEY_CONSUMER(0x223) ///< Browser Home
#define KEYBOARD_KEY_AC_BACK                KEYBOARD_KEY_CONSUMER(0x224) ///< Browser Back
#define KEYBOARD_KEY_AC_FORWARD             KEYBOARD_KEY_CONSUMER(0x225) ///< Browser Forward
#define KEYBOARD_KEY_AC_STOP                KEYBOARD_KEY_CONSUMER(0x226) ///< Browser Stop
#define KEYBOARD_KEY_AC_REFRESH             KEYBOARD_KEY_CONSUMER(0x227) ///< Browser Refresh
#define KEYBOARD_KEY_AC_BOOKMARKS           KEYBOARD_KEY_CONSUMER(0x22A) ///< Bookmarks
#define KEYBOARD_KEY_SHOW_ALL_WINDOWS       KEYBOARD_KEY_CONSUMER(0x29F) ///< Mission Control (Show All Windows)
#define KEYBOARD_KEY_SHOW_ALL_APPLICATIONS  KEYBOARD_KEY_CONSUMER(0x2A2) ///< Launchpad (Show All Applications)
#define KEYBOARD_KEY_SYSTEM_POWER           KEYBOARD_KEY_DESKTOP(0x81) ///< System Power Down
#define KEYBOARD_KEY_SLEEP                  KEYBOARD_KEY_DESKTOP(0x82) ///< System Sleep
#define KEYBOARD_KEY_WAKE                   KEYBOARD_KEY_DESKTOP(0x83) ///< System Wake Up
#define KEYBOARD_KEY_DO_NOT_DISTURB         KEYBOARD_KEY_DESKTOP(0x9B) ///< Do Not Disturb (System Do Not Disturb)
#define KEYBOARD_KEY_MICROPHONE_MUTE        KEYBOARD_KEY_DESKTOP(0xA9) ///< Microphone Mute (System Microphone Mute)
#define KEYBOARD_KEY_FN                     KEYBOARD_KEY_APPLE(0x03) ///< fn, the Globe key on a Mac (Apple's vendor page)
/** @} */

/**
 * @anchor KEYBOARD_USAGE
 * @name HID usages
 *
 * A usage as #keyboard_key_usage returns it: its HID usage page in the high
 * 16 bits and its usage ID in the low 16, as HID's extended usages are written.
 * @{
 */
#define KEYBOARD_USAGE(page, id)        (((uint32_t)(page) << 16) | (uint32_t)(id)) ///< A page and usage ID
#define KEYBOARD_USAGE_PAGE_DESKTOP     0x01 ///< HID Generic Desktop page
#define KEYBOARD_USAGE_PAGE_KEYBOARD    0x07 ///< HID Keyboard/Keypad page
#define KEYBOARD_USAGE_PAGE_CONSUMER    0x0C ///< HID Consumer page
#define KEYBOARD_USAGE_PAGE_APPLE       0xFF ///< Apple's vendor page (the fn key)
/** @} */

/**
 * @anchor KEYBOARD_LED
 * @name Keyboard LEDs
 *
 * HID's LED bits, which the adapter passes to the keyboard as they are.
 * @{
 */
#define KEYBOARD_LED_NUM_LOCK     (1<<0) ///< Num Lock LED
#define KEYBOARD_LED_CAPS_LOCK    (1<<1) ///< Caps Lock LED
#define KEYBOARD_LED_SCROLL_LOCK  (1<<2) ///< Scroll Lock LED
#define KEYBOARD_LED_COMPOSE      (1<<3) ///< Compose LED
#define KEYBOARD_LED_KANA         (1<<4) ///< Kana LED
/** @} */

/** @brief The most keys a keyboard reports held at once, modifiers among them. */
#define KEYBOARD_KEYS_MAX 10

/** @brief The layout BlueRetroForever is set to for a keyboard: what its keys are labelled. */
typedef enum
{
    /** @brief No keyboard. */
    KEYBOARD_LAYOUT_NONE = -1,
    /** @brief US (ANSI). */
    KEYBOARD_LAYOUT_US = 0,
    /** @brief UK (ISO). */
    KEYBOARD_LAYOUT_UK = 1,
    /** @brief German (ISO, QWERTZ). */
    KEYBOARD_LAYOUT_DE = 2,
    /** @brief French (ISO, AZERTY). */
    KEYBOARD_LAYOUT_FR = 3,
    /** @brief Japanese (JIS). */
    KEYBOARD_LAYOUT_JP = 4,
} keyboard_layout_t;

/**
 * @brief The arrangement BlueRetroForever is set to for a keyboard: a Mac's or a PC's.
 *
 * A layout types some characters differently on each: Apple's UK layout has
 * \@ on Shift+2, and its German and French layouts type with Option what
 * Windows' type with AltGr.
 */
typedef enum
{
    /** @brief No keyboard. */
    KEYBOARD_ARRANGEMENT_NONE = -1,
    /** @brief A Mac keyboard: Control, Option and Command. */
    KEYBOARD_ARRANGEMENT_MAC = 0,
    /** @brief A PC keyboard: Ctrl, Alt (AltGr on the right) and Windows. */
    KEYBOARD_ARRANGEMENT_PC = 1,
} keyboard_arrangement_t;

/** @brief A key going down or up on a keyboard. */
typedef struct keyboard_event_s
{
    /** @brief The key. */
    keyboard_key_t key;
    /** @brief The port the keyboard is on (#joypad_port_t). */
    uint8_t port;
    /** @brief True if the key went down, false if it went up. */
    bool pressed;
} keyboard_event_t;

/** @brief The keys held on a keyboard, as of its last read. */
typedef struct keyboard_keys_s
{
    /**
     * @brief The held keys: the modifiers first, then the other keys in the
     *        order they went down, then media and system keys. The entries
     *        after the last are #KEYBOARD_KEY_NONE.
     */
    keyboard_key_t key[KEYBOARD_KEYS_MAX];
    /** @brief More keys are held than the keyboard reports; these are the first ten. */
    bool overflow;
} keyboard_keys_t;

/**
 * @brief Initialize the Keyboard subsystem.
 * @preview
 *
 * Starts the Joybus subsystem's background detection (#joybus_init) and a
 * read on each vertical blank, which reads only while a keyboard is
 * connected. Calls are counted, as #joypad_init's are.
 */
LIBDRAGON_PREVIEW_API
void keyboard_init(void);

/**
 * @brief Close the Keyboard subsystem.
 * @preview
 *
 * Stops reading keyboards. The next #keyboard_init starts with an empty queue.
 */
LIBDRAGON_PREVIEW_API
void keyboard_close(void);

/**
 * @brief Is a keyboard connected to a port?
 * @preview
 *
 * A keyboard counts from its first read, a frame or two after detection
 * finds it.
 *
 * @param port Joypad port (#joypad_port_t).
 * @return true if a keyboard is connected there.
 */
LIBDRAGON_PREVIEW_API
bool keyboard_is_connected(joypad_port_t port);

/**
 * @brief The layout BlueRetroForever is set to for a port's keyboard.
 * @preview
 *
 * The adapter sends it with every read, so a change made in its settings
 * shows from the next frame.
 *
 * @param port Joypad port (#joypad_port_t).
 * @return The layout, #KEYBOARD_LAYOUT_NONE with no keyboard, or a value
 *         beyond #KEYBOARD_LAYOUT_JP for a layout added after this one.
 */
LIBDRAGON_PREVIEW_API
keyboard_layout_t keyboard_get_layout(joypad_port_t port);

/**
 * @brief The arrangement BlueRetroForever is set to for a port's keyboard.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t).
 * @return The arrangement, or #KEYBOARD_ARRANGEMENT_NONE with no keyboard.
 */
LIBDRAGON_PREVIEW_API
keyboard_arrangement_t keyboard_get_arrangement(joypad_port_t port);

/**
 * @brief Take the next key event from the queue.
 * @preview
 *
 * Events come in the order their reads arrived, and each read's releases
 * before its presses. The queue holds 31 events; while it's full, newer
 * events are dropped, so take them every frame.
 *
 * @param[out] event Where to store the event.
 * @return true if an event was taken, false if the queue was empty.
 */
LIBDRAGON_PREVIEW_API
bool keyboard_get_event(keyboard_event_t *event);

/**
 * @brief The keys held on a port's keyboard, as of its last read.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t).
 * @return The held keys; none if no keyboard is connected.
 */
LIBDRAGON_PREVIEW_API
keyboard_keys_t keyboard_get_keys(joypad_port_t port);

/**
 * @brief Light a port's keyboard LEDs.
 * @preview
 *
 * The LEDs are sent with every read, so they take effect from the next frame,
 * and they stay as set when a keyboard is unplugged and plugged in again.
 *
 * @param port Joypad port (#joypad_port_t).
 * @param leds The LEDs to light (@ref KEYBOARD_LED); the rest go out.
 */
LIBDRAGON_PREVIEW_API
void keyboard_set_leds(joypad_port_t port, uint8_t leds);

/**
 * @brief The LEDs set for a port's keyboard.
 * @preview
 *
 * @param port Joypad port (#joypad_port_t).
 * @return The lit LEDs (@ref KEYBOARD_LED).
 */
LIBDRAGON_PREVIEW_API
uint8_t keyboard_get_leds(joypad_port_t port);

/**
 * @brief A key's name: its constant's, without `KEYBOARD_KEY_`.
 * @preview
 *
 * For diagnostics and test programs. The names are linked only into
 * programs that call this function.
 *
 * @param key The key.
 * @return Its name, such as "LSHIFT" or "MEDIA_PLAY_PAUSE", or NULL for a
 *         code with no constant.
 */
LIBDRAGON_PREVIEW_API
const char *keyboard_key_name(keyboard_key_t key);

/**
 * @brief A key's HID usage: its page and usage ID.
 * @preview
 *
 * @param key The key.
 * @return Its usage (@ref KEYBOARD_USAGE), or 0 for #KEYBOARD_KEY_NONE or a
 *         code beyond the pages.
 */
static inline uint32_t keyboard_key_usage(keyboard_key_t key)
{
    unsigned page = KEYBOARD_KEY_PAGE(key);
    if (!key || page > 3) return 0;
    // The HID pages of the four ranges, a byte each
    return KEYBOARD_USAGE((0xFF010C07u >> (page * 8)) & 0xFF, key & 0x0FFF);
}

#ifdef __cplusplus
}
#endif

/** @} */ /* keyboard */

#endif
