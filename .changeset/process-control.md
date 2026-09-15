---
"sys-autopilot": minor
---

Add process control: start, stop, restart and query any program by title id.
`GET /process?titleId=...` reports whether it is running (and its pid), and
`POST /process/{start,stop,restart}` drive it. Also exposed as the
`process_status` / `process_start` / `process_stop` / `process_restart` MCP
tools.

This is aimed at iterating on a sysmodule without rebooting: stop it, upload
the rebuilt `exefs.nsp` through `/files`, start it again. Because a program
does not need a `flags/boot2.flag` to be launched this way, a sysmodule under
development can be kept out of the boot sequence entirely — a build that
crashes on startup then no longer takes the console down before anything is
reachable, which is otherwise the failure that forces pulling the SD card.

`restart` waits for the terminated process to actually disappear before
relaunching, so callers never race a half-dead process.

Requires the `pm:shell` and `pm:dmnt` services, now added to the NPDM.
