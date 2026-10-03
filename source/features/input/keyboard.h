#pragma once

#include <stdbool.h>
#include <stdint.h>

// Text to USB HID key presses on a US layout, for the virtual keyboard
// (type_text). Host-compilable so the mapping is unit-tested off device.

#define KEYBOARD_TEXT_MAX 256 // characters per type_text call

#define KEY_USAGE_LEFT_SHIFT 225

// Maps one character to its HID usage and whether Shift is held. Covers
// printable ASCII plus '\n' (Enter), '\t' (Tab) and '\b' (Backspace).
// Returns false for anything else.
bool keyboard_map_char(char c, uint8_t *usage, bool *shift);

// Index of the first character keyboard_map_char() rejects, or -1.
int keyboard_find_unsupported(const char *text);
