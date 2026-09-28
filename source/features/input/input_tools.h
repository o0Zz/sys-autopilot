// Generated from input_tools.json by scripts/generate_resource.py -- do not edit by hand.
#pragma once

#include "features/mcp/mcp_server.h"

static const McpToolDef kToolTapButtons = {
    "{\"name\":\"tap_buttons\",\"description\":\"Press and release controller buttons (synchronous). A virtual P"
    "ro Controller is attached automatically. Button names: A, B, X, Y, L, R, ZL, ZR, PLUS, MINUS, UP, DO"
    "WN, LEFT, RIGHT, LSTICK, RSTICK, HOME, CAPTURE.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"butto"
    "ns\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Buttons to act on. Button names: A, B, "
    "X, Y, L, R, ZL, ZR, PLUS, MINUS, UP, DOWN, LEFT, RIGHT, LSTICK, RSTICK, HOME, CAPTURE.\"},\"durationMs"
    "\":{\"type\":\"integer\",\"description\":\"How long to hold before releasing, in ms. Default 100, max 10000."
    "\"},\"screenshot\":{\"type\":\"boolean\",\"description\":\"If true, the result also includes a screenshot take"
    "n after the input, saving a separate screenshot call.\"},\"screenshotDelayMs\":{\"type\":\"integer\",\"descr"
    "iption\":\"Delay before that screenshot in ms (lets the UI settle). Default 250.\"}},\"required\":[\"butto"
    "ns\"]}}"
};

static const McpToolDef kToolTapSequence = {
    "{\"name\":\"tap_sequence\",\"description\":\"Perform a sequence of button taps in one call, e.g. to navigat"
    "e menus. Each step presses its buttons, holds, releases, then waits before the next step. Button nam"
    "es: A, B, X, Y, L, R, ZL, ZR, PLUS, MINUS, UP, DOWN, LEFT, RIGHT, LSTICK, RSTICK, HOME, CAPTURE.\",\"i"
    "nputSchema\":{\"type\":\"object\",\"properties\":{\"taps\":{\"type\":\"array\",\"maxItems\":32,\"description\":\"Seque"
    "nce of taps (max 32).\",\"items\":{\"type\":\"object\",\"properties\":{\"buttons\":{\"type\":\"array\",\"items\":{\"ty"
    "pe\":\"string\"},\"description\":\"Buttons to act on. Button names: A, B, X, Y, L, R, ZL, ZR, PLUS, MINUS,"
    " UP, DOWN, LEFT, RIGHT, LSTICK, RSTICK, HOME, CAPTURE.\"},\"durationMs\":{\"type\":\"integer\",\"description"
    "\":\"Hold time in ms. Default 100.\"},\"delayAfterMs\":{\"type\":\"integer\",\"description\":\"Pause after relea"
    "se in ms. Default 150.\"}},\"required\":[\"buttons\"]}},\"screenshot\":{\"type\":\"boolean\",\"description\":\"If "
    "true, the result also includes a screenshot taken after the input, saving a separate screenshot call"
    ".\"},\"screenshotDelayMs\":{\"type\":\"integer\",\"description\":\"Delay before that screenshot in ms (lets th"
    "e UI settle). Default 250.\"}},\"required\":[\"taps\"]}}"
};

static const McpToolDef kToolHoldButtons = {
    "{\"name\":\"hold_buttons\",\"description\":\"Press buttons and keep them held until release_buttons or clea"
    "r_input. Button names: A, B, X, Y, L, R, ZL, ZR, PLUS, MINUS, UP, DOWN, LEFT, RIGHT, LSTICK, RSTICK,"
    " HOME, CAPTURE.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"buttons\":{\"type\":\"array\",\"items\":{\"ty"
    "pe\":\"string\"},\"description\":\"Buttons to act on. Button names: A, B, X, Y, L, R, ZL, ZR, PLUS, MINUS,"
    " UP, DOWN, LEFT, RIGHT, LSTICK, RSTICK, HOME, CAPTURE.\"},\"screenshot\":{\"type\":\"boolean\",\"description"
    "\":\"If true, the result also includes a screenshot taken after the input, saving a separate screensho"
    "t call.\"},\"screenshotDelayMs\":{\"type\":\"integer\",\"description\":\"Delay before that screenshot in ms (l"
    "ets the UI settle). Default 250.\"}},\"required\":[\"buttons\"]}}"
};

static const McpToolDef kToolReleaseButtons = {
    "{\"name\":\"release_buttons\",\"description\":\"Release previously held buttons.\",\"inputSchema\":{\"type\":\"ob"
    "ject\",\"properties\":{\"buttons\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Buttons to ac"
    "t on. Button names: A, B, X, Y, L, R, ZL, ZR, PLUS, MINUS, UP, DOWN, LEFT, RIGHT, LSTICK, RSTICK, HO"
    "ME, CAPTURE.\"},\"screenshot\":{\"type\":\"boolean\",\"description\":\"If true, the result also includes a scr"
    "eenshot taken after the input, saving a separate screenshot call.\"},\"screenshotDelayMs\":{\"type\":\"int"
    "eger\",\"description\":\"Delay before that screenshot in ms (lets the UI settle). Default 250.\"}},\"requi"
    "red\":[\"buttons\"]}}"
};

static const McpToolDef kToolSetStick = {
    "{\"name\":\"set_stick\",\"description\":\"Set an analog stick position. x/y range -1.0..1.0 (y=1.0 is up). "
    "With durationMs the stick recenters afterwards; without it the position persists until changed or cl"
    "ear_input.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"side\":{\"type\":\"string\",\"enum\":[\"left\",\"rig"
    "ht\"]},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"durationMs\":{\"type\":\"integer\",\"description\":\"Hold"
    " time before recentering. Max 10000.\"},\"screenshot\":{\"type\":\"boolean\",\"description\":\"If true, the re"
    "sult also includes a screenshot taken after the input, saving a separate screenshot call.\"},\"screens"
    "hotDelayMs\":{\"type\":\"integer\",\"description\":\"Delay before that screenshot in ms (lets the UI settle)"
    ". Default 250.\"}},\"required\":[\"side\"]}}"
};

static const McpToolDef kToolTapScreen = {
    "{\"name\":\"tap_screen\",\"description\":\"Tap the touch screen at a pixel coordinate. Coordinates are in t"
    "he same 1280x720 space as the screenshot tool, so you can read them straight off the image: x 0-1279"
    " left to right, y 0-719 top to bottom. Only reaches the console in handheld mode (docked, the panel "
    "is off).\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"integer\",\"description\":\"0-1279, "
    "from the left edge.\"},\"y\":{\"type\":\"integer\",\"description\":\"0-719, from the top edge.\"},\"durationMs\":"
    "{\"type\":\"integer\",\"description\":\"How long the finger stays down. Default 100, min 32, max 10000. Rai"
    "se it for a long press.\"},\"screenshot\":{\"type\":\"boolean\",\"description\":\"If true, the result also inc"
    "ludes a screenshot taken after the input, saving a separate screenshot call.\"},\"screenshotDelayMs\":{"
    "\"type\":\"integer\",\"description\":\"Delay before that screenshot in ms (lets the UI settle). Default 250"
    ".\"}},\"required\":[\"x\",\"y\"]}}"
};

static const McpToolDef kToolSwipeScreen = {
    "{\"name\":\"swipe_screen\",\"description\":\"Drag a finger across the touch screen, e.g. to scroll a list o"
    "r flick a page. Same 1280x720 coordinate space as the screenshot tool. The gesture is interpolated o"
    "ver durationMs, which is what makes scrolling register - a fast swipe flicks, a slow one drags.\",\"in"
    "putSchema\":{\"type\":\"object\",\"properties\":{\"fromX\":{\"type\":\"integer\",\"description\":\"Start x, 0-1279.\""
    "},\"fromY\":{\"type\":\"integer\",\"description\":\"Start y, 0-719.\"},\"toX\":{\"type\":\"integer\",\"description\":\""
    "End x, 0-1279.\"},\"toY\":{\"type\":\"integer\",\"description\":\"End y, 0-719.\"},\"durationMs\":{\"type\":\"intege"
    "r\",\"description\":\"Time from start to end. Default 300, max 10000.\"},\"screenshot\":{\"type\":\"boolean\",\""
    "description\":\"If true, the result also includes a screenshot taken after the input, saving a separat"
    "e screenshot call.\"},\"screenshotDelayMs\":{\"type\":\"integer\",\"description\":\"Delay before that screensh"
    "ot in ms (lets the UI settle). Default 250.\"}},\"required\":[\"fromX\",\"fromY\",\"toX\",\"toY\"]}}"
};

static const McpToolDef kToolClearInput = {
    "{\"name\":\"clear_input\",\"description\":\"Release all held buttons and recenter both sticks.\",\"inputSchem"
    "a\":{\"type\":\"object\",\"properties\":{\"screenshot\":{\"type\":\"boolean\",\"description\":\"If true, the result "
    "also includes a screenshot taken after the input, saving a separate screenshot call.\"},\"screenshotDe"
    "layMs\":{\"type\":\"integer\",\"description\":\"Delay before that screenshot in ms (lets the UI settle). Def"
    "ault 250.\"}}}}"
};
