---
"sys-autopilot": minor
---

Add touch screen input: `POST /input/touch` taps a pixel coordinate and
`POST /input/swipe` drags between two, exposed as the `tap_screen` and
`swipe_screen` MCP tools. Coordinates are in the panel's 1280x720 space —
the same space as the JPEG `/screenshot` returns — so an agent can read a
target off the screenshot it is already looking at and touch it, with no
coordinate mapping in between.

This does not go through HDLS: `hiddbg` replays the state into hid's own
touch sampler, so it needs neither the work buffer nor the virtual
controller, and a physical controller can stay connected. The state is
latched (hid re-reports the last one every sampling frame), so a gesture is
driven frame by frame at ~60Hz and always ends by reporting "no finger" and
unsetting, which hands the panel back to the player — including on the sleep
path, where a latched touch would otherwise survive the transition.

Swipes are interpolated over `durationMs` rather than teleported, which is
what makes scroll and flick handlers react at all.

Touch only reaches applications in handheld mode: docked, the panel is off
and the console ignores injected touch exactly as it ignores a finger.
