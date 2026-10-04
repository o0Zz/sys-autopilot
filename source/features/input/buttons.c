#include "features/input/buttons.h"

#include <strings.h>

// Every BTN_* is a single bit, so the table keeps its index.
typedef struct {
    char name[8];
    uint8_t bit;
} ButtonMap;

#define B(name, mask) { name, __builtin_ctzll(mask) }
static const ButtonMap kButtons[] = {
    B("A",       BTN_A),
    B("B",       BTN_B),
    B("X",       BTN_X),
    B("Y",       BTN_Y),
    B("L",       BTN_L),
    B("R",       BTN_R),
    B("ZL",      BTN_ZL),
    B("ZR",      BTN_ZR),
    B("PLUS",    BTN_PLUS),
    B("START",   BTN_PLUS),
    B("MINUS",   BTN_MINUS),
    B("SELECT",  BTN_MINUS),
    B("UP",      BTN_UP),
    B("DOWN",    BTN_DOWN),
    B("LEFT",    BTN_LEFT),
    B("RIGHT",   BTN_RIGHT),
    B("DUP",     BTN_UP),
    B("DDOWN",   BTN_DOWN),
    B("DLEFT",   BTN_LEFT),
    B("DRIGHT",  BTN_RIGHT),
    B("LSTICK",  BTN_LSTICK),
    B("RSTICK",  BTN_RSTICK),
    B("HOME",    BTN_HOME),
    B("CAPTURE", BTN_CAPTURE),
};
#undef B

bool button_from_name(const char *name, uint64_t *out_mask) {
    for (size_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); i++) {
        if (strcasecmp(name, kButtons[i].name) == 0) {
            *out_mask = 1ULL << kButtons[i].bit;
            return true;
        }
    }
    return false;
}
