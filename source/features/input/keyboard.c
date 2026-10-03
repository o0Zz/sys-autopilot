#include "features/input/keyboard.h"

#include <string.h>

// Unshifted and shifted characters for the US-layout keys 30..56 that are
// not letters (0 = no character on that key in this table).
static const char kPlain[] = "1234567890\n\x1b\b\t -=[]\\\0;'`,./";
static const char kShifted[] = "!@#$%^&*()\0\0\0\0\0_+{}|\0:\"~<>?";
#define FIRST_KEY 30 // HidKeyboardKey_D1

bool keyboard_map_char(char c, uint8_t *usage, bool *shift) {
    if (c >= 'a' && c <= 'z') {
        *usage = (uint8_t)(4 + (c - 'a'));
        *shift = false;
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        *usage = (uint8_t)(4 + (c - 'A'));
        *shift = true;
        return true;
    }
    if (c == '\0' || c == '\x1b') // no Escape: it would close the keyboard
        return false;
    for (size_t i = 0; i < sizeof(kPlain) - 1; i++) {
        if (kPlain[i] == c) {
            *usage = (uint8_t)(FIRST_KEY + i);
            *shift = false;
            return true;
        }
        if (kShifted[i] == c) {
            *usage = (uint8_t)(FIRST_KEY + i);
            *shift = true;
            return true;
        }
    }
    return false;
}

int keyboard_find_unsupported(const char *text) {
    uint8_t usage;
    bool shift;
    for (int i = 0; text[i]; i++)
        if (!keyboard_map_char(text[i], &usage, &shift))
            return i;
    return -1;
}
