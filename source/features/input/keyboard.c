#include "features/input/keyboard.h"

#include <stddef.h>
#include <stdio.h>

// One character on one layout: the key, its modifiers, and for a dead key
// the character typed after it (a space for the accent alone, or the letter
// it combines with).
typedef struct {
    uint16_t cp;
    uint8_t usage;
    uint8_t mods;
    uint16_t then;
} KeyEntry;

#define S KEY_MOD_SHIFT
#define G KEY_MOD_ALTGR

// US: the keys after the letters, 30..56 (0 = Enter..Space, handled for every
// layout below).
static const char kUsPlain[]   = "1234567890\0\0\0\0\0-=[]\\\0;'`,./";
static const char kUsShifted[] = "!@#$%^&*()\0\0\0\0\0_+{}|\0:\"~<>?";
#define US_FIRST_KEY 30

// UK differs from US on these (checked on a console set to UK). Everything else on the UK layout falls
// through to the US table.
static const KeyEntry kUk[] = {
    { '"', 31, S, 0 },  { '@', 52, S, 0 },  { 0x00A3 /* £ */, 32, S, 0 },
    { '#', 50, 0, 0 },  { '~', 50, S, 0 },  { '\\', 100, 0, 0 }, { '|', 100, S, 0 },
    { '`', 53, 0, 0 },  { 0x00AC /* ¬ */, 53, S, 0 }, { 0x20AC /* € */, 33, G, 0 },
};

// French AZERTY, checked key by key on a console set to French. Unlike on a
// PC, AltGr ~ and ` are not dead keys there.
static const KeyEntry kFr[] = {
    // Number row: symbols plain, digits shifted, more on AltGr.
    { '&', 30, 0, 0 },  { '1', 30, S, 0 },
    { 0x00E9 /* é */, 31, 0, 0 }, { '2', 31, S, 0 }, { '~', 31, G, 0 },
    { '"', 32, 0, 0 },  { '3', 32, S, 0 },  { '#', 32, G, 0 },
    { '\'', 33, 0, 0 }, { '4', 33, S, 0 },  { '{', 33, G, 0 },
    { '(', 34, 0, 0 },  { '5', 34, S, 0 },  { '[', 34, G, 0 },
    { '-', 35, 0, 0 },  { '6', 35, S, 0 },  { '|', 35, G, 0 },
    { 0x00E8 /* è */, 36, 0, 0 }, { '7', 36, S, 0 }, { '`', 36, G, 0 },
    { '_', 37, 0, 0 },  { '8', 37, S, 0 },  { '\\', 37, G, 0 },
    { 0x00E7 /* ç */, 38, 0, 0 }, { '9', 38, S, 0 }, { '^', 38, G, 0 },
    { 0x00E0 /* à */, 39, 0, 0 }, { '0', 39, S, 0 }, { '@', 39, G, 0 },
    { ')', 45, 0, 0 },  { 0x00B0 /* ° */, 45, S, 0 }, { ']', 45, G, 0 },
    { '=', 46, 0, 0 },  { '+', 46, S, 0 },  { '}', 46, G, 0 },
    // Rest of the keyboard.
    { '$', 48, 0, 0 },  { 0x00A3 /* £ */, 48, S, 0 }, { 0x00A4 /* ¤ */, 48, G, 0 },
    { '*', 49, 0, 0 },  { 0x00B5 /* µ */, 49, S, 0 },
    { 0x00F9 /* ù */, 52, 0, 0 }, { '%', 52, S, 0 },
    { 0x00B2 /* ² */, 53, 0, 0 },
    { ',', 16, 0, 0 },  { '?', 16, S, 0 },
    { ';', 54, 0, 0 },  { '.', 54, S, 0 },
    { ':', 55, 0, 0 },  { '/', 55, S, 0 },
    { '!', 56, 0, 0 },  { 0x00A7 /* § */, 56, S, 0 },
    { '<', 100, 0, 0 }, { '>', 100, S, 0 },
    { 0x20AC /* € */, 8, G, 0 },
    // Dead keys ^ and ¨ (key 47), then the letter they go on.
    { 0x00A8 /* ¨ */, 47, S, ' ' }, // ^ alone is AltGr+ç above
    { 0x00E2, 47, 0, 'a' }, { 0x00EA, 47, 0, 'e' }, { 0x00EE, 47, 0, 'i' },
    { 0x00F4, 47, 0, 'o' }, { 0x00FB, 47, 0, 'u' },
    { 0x00C2, 47, 0, 'A' }, { 0x00CA, 47, 0, 'E' }, { 0x00CE, 47, 0, 'I' },
    { 0x00D4, 47, 0, 'O' }, { 0x00DB, 47, 0, 'U' },
    { 0x00E4, 47, S, 'a' }, { 0x00EB, 47, S, 'e' }, { 0x00EF, 47, S, 'i' },
    { 0x00F6, 47, S, 'o' }, { 0x00FC, 47, S, 'u' }, { 0x00FF, 47, S, 'y' },
    { 0x00C4, 47, S, 'A' }, { 0x00CB, 47, S, 'E' }, { 0x00CF, 47, S, 'I' },
    { 0x00D6, 47, S, 'O' }, { 0x00DC, 47, S, 'U' },
};

#undef S
#undef G

static const char *keyboard_layout_name(int layout) {
    // NUL-separated, in layout order; an empty name ends the list.
    static const char kNames[] =
        "Japanese\0English (US)\0English (US, international)\0English (UK)\0"
        "French\0French (Canada)\0Spanish\0Spanish (Latin America)\0German\0"
        "Italian\0Portuguese\0Russian\0Korean\0Chinese (simplified)\0"
        "Chinese (traditional)\0";
    const char *p = kNames;
    for (int i = 0; i < layout && *p; i++)
        while (*p++) {}
    return layout < 0 || !*p ? "unknown" : p;
}

static bool keyboard_layout_supported(int layout) {
    return layout == KEYBOARD_LAYOUT_ENGLISH_US || layout == KEYBOARD_LAYOUT_ENGLISH_UK ||
           layout == KEYBOARD_LAYOUT_FRENCH;
}

bool keyboard_next_codepoint(const char **s, uint32_t *cp) {
    const unsigned char *p = (const unsigned char *)*s;
    if (p[0] == 0)
        return false;
    int len = p[0] < 0x80 ? 1 : (p[0] & 0xE0) == 0xC0 ? 2 : (p[0] & 0xF0) == 0xE0 ? 3
            : (p[0] & 0xF8) == 0xF0 ? 4 : 0;
    if (len == 0)
        return false;
    uint32_t v = len == 1 ? p[0] : p[0] & (0x7F >> len);
    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80)
            return false;
        v = (v << 6) | (p[i] & 0x3F);
    }
    *cp = v;
    *s += len;
    return true;
}

static const KeyEntry *find(const KeyEntry *table, size_t n, uint32_t cp) {
    for (size_t i = 0; i < n; i++)
        if (table[i].cp == cp)
            return &table[i];
    return NULL;
}

// The US layout's entry for an ASCII character, built from the strings above.
static bool us_entry(uint32_t cp, KeyEntry *out) {
    for (size_t i = 0; i < sizeof(kUsPlain) - 1; i++) {
        if (kUsPlain[i] && (uint32_t)(unsigned char)kUsPlain[i] == cp) {
            *out = (KeyEntry){ (uint16_t)cp, (uint8_t)(US_FIRST_KEY + i), 0, 0 };
            return true;
        }
        if (kUsShifted[i] && (uint32_t)(unsigned char)kUsShifted[i] == cp) {
            *out = (KeyEntry){ (uint16_t)cp, (uint8_t)(US_FIRST_KEY + i), KEY_MOD_SHIFT, 0 };
            return true;
        }
    }
    return false;
}

// Key of a lowercase letter; AZERTY swaps a/q, z/w and moves m.
static uint8_t letter_usage(int layout, char c) {
    if (layout == KEYBOARD_LAYOUT_FRENCH) {
        switch (c) {
            case 'a': return 20;
            case 'q': return 4;
            case 'z': return 26;
            case 'w': return 29;
            case 'm': return 51;
        }
    }
    return (uint8_t)(4 + (c - 'a'));
}

// Every key except the dead-key follow-up.
static bool lookup(int layout, uint32_t cp, KeyEntry *out) {
    switch (cp) {
        case '\n': *out = (KeyEntry){ (uint16_t)cp, 40, 0, 0 }; return true;
        case '\b': *out = (KeyEntry){ (uint16_t)cp, 42, 0, 0 }; return true;
        case '\t': *out = (KeyEntry){ (uint16_t)cp, 43, 0, 0 }; return true;
        case ' ':  *out = (KeyEntry){ (uint16_t)cp, 44, 0, 0 }; return true;
    }
    if (cp >= 'a' && cp <= 'z') {
        *out = (KeyEntry){ (uint16_t)cp, letter_usage(layout, (char)cp), 0, 0 };
        return true;
    }
    if (cp >= 'A' && cp <= 'Z') {
        *out = (KeyEntry){ (uint16_t)cp, letter_usage(layout, (char)(cp - 'A' + 'a')),
                           KEY_MOD_SHIFT, 0 };
        return true;
    }
    const KeyEntry *e = NULL;
    switch (layout) {
        case KEYBOARD_LAYOUT_ENGLISH_US:
            return us_entry(cp, out);
        case KEYBOARD_LAYOUT_ENGLISH_UK:
            // UK moves a few US characters; the rest are where US has them.
            e = find(kUk, sizeof(kUk) / sizeof(kUk[0]), cp);
            if (!e)
                return us_entry(cp, out);
            break;
        case KEYBOARD_LAYOUT_FRENCH:
            e = find(kFr, sizeof(kFr) / sizeof(kFr[0]), cp);
            break;
    }
    if (!e)
        return false;
    *out = *e;
    return true;
}

int keyboard_strokes(int layout, uint32_t cp, KeyStroke out[KEYBOARD_MAX_STROKES]) {
    KeyEntry e;
    if (!keyboard_layout_supported(layout) || cp > 0xFFFF || !lookup(layout, cp, &e))
        return 0;
    out[0] = (KeyStroke){ e.usage, e.mods };
    if (!e.then)
        return 1;
    KeyEntry next;
    if (!lookup(layout, e.then, &next) || next.then)
        return 0;
    out[1] = (KeyStroke){ next.usage, next.mods };
    return 2;
}

int keyboard_find_unsupported(int layout, const char *text) {
    const char *p = text;
    while (*p) {
        const char *at = p;
        uint32_t cp;
        KeyStroke strokes[KEYBOARD_MAX_STROKES];
        if (!keyboard_next_codepoint(&p, &cp) || keyboard_strokes(layout, cp, strokes) == 0)
            return (int)(at - text);
    }
    return -1;
}

bool keyboard_check_text(int layout, const char *text, char *msg, size_t msgsz) {
    if (layout < 0) {
        snprintf(msg, msgsz, "could not read the console's keyboard layout");
        return false;
    }
    if (!keyboard_layout_supported(layout)) {
        snprintf(msg, msgsz,
                 "the console's keyboard layout (%s) is not supported yet: English (US), "
                 "English (UK) and French are",
                 keyboard_layout_name(layout));
        return false;
    }
    int bad = keyboard_find_unsupported(layout, text);
    if (bad >= 0) {
        // The character, copied by hand: the console's newlib turns a "%.*s"
        // of a UTF-8 sequence into a single Latin-1 byte.
        char ch[5] = { text[bad], 0, 0, 0, 0 };
        for (int i = 1; i < 4 && (text[bad + i] & 0xC0) == 0x80; i++)
            ch[i] = text[bad + i];
        snprintf(msg, msgsz, "byte %d ('%s') cannot be typed on the %s keyboard layout", bad, ch,
                 keyboard_layout_name(layout));
        return false;
    }
    return true;
}
