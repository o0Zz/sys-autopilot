#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Text to USB HID key presses, for the virtual keyboard (type_text).
// Host-compilable so the mappings are unit-tested off device.
//
// The console turns key codes into characters through its own keyboard
// layout setting (set:sys KeyboardLayout, which follows the system
// language), so the same text needs different keys on a French console than
// on an American one.

#define KEYBOARD_TEXT_MAX 256 // bytes of UTF-8 per type_text call

// set:sys KeyboardLayout values (SetKeyboardLayout in libnx; input.c checks
// they match).
typedef enum {
    KEYBOARD_LAYOUT_JAPANESE = 0,
    KEYBOARD_LAYOUT_ENGLISH_US = 1,
    KEYBOARD_LAYOUT_ENGLISH_US_INTERNATIONAL = 2,
    KEYBOARD_LAYOUT_ENGLISH_UK = 3,
    KEYBOARD_LAYOUT_FRENCH = 4,
    KEYBOARD_LAYOUT_FRENCH_CA = 5,
    KEYBOARD_LAYOUT_SPANISH = 6,
    KEYBOARD_LAYOUT_SPANISH_LATIN = 7,
    KEYBOARD_LAYOUT_GERMAN = 8,
    KEYBOARD_LAYOUT_ITALIAN = 9,
    KEYBOARD_LAYOUT_PORTUGUESE = 10,
    KEYBOARD_LAYOUT_RUSSIAN = 11,
    KEYBOARD_LAYOUT_KOREAN = 12,
    KEYBOARD_LAYOUT_CHINESE_SIMPLIFIED = 13,
    KEYBOARD_LAYOUT_CHINESE_TRADITIONAL = 14,
} KeyboardLayout;

#define KEY_MOD_SHIFT 1
#define KEY_MOD_ALTGR 2

#define KEY_USAGE_LEFT_SHIFT 225
#define KEY_USAGE_RIGHT_ALT 230
// A key with no effect anywhere, pressed between two presses of the same
// key: the console ignores a key going down again until another key has.
#define KEY_USAGE_NOOP 115 // F24

typedef struct {
    uint8_t usage;
    uint8_t mods; // KEY_MOD_*
} KeyStroke;

// At most this many strokes per character (a dead key, then the letter).
#define KEYBOARD_MAX_STROKES 2

// Decodes the next UTF-8 code point from *s and advances it. Returns false
// at the end of the string or on invalid UTF-8.
bool keyboard_next_codepoint(const char **s, uint32_t *cp);

// The strokes that type `cp` on `layout`: fills out[] and returns their
// count, or 0 when that layout cannot type it.
int keyboard_strokes(int layout, uint32_t cp, KeyStroke out[KEYBOARD_MAX_STROKES]);

// Checks that all of `text` can be typed on `layout`. When not, writes a
// message for the user into msg (naming the layout or the character).
bool keyboard_check_text(int layout, const char *text, char *msg, size_t msgsz);

// Byte offset of the first character `layout` cannot type (or the first
// invalid UTF-8 sequence), or -1 when all of `text` can be typed.
int keyboard_find_unsupported(int layout, const char *text);
