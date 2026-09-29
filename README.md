# sys-autopilot

A Nintendo Switch (Atmosphère) sysmodule that runs an HTTP server on the
console, with a **native MCP endpoint** so an AI agent (Claude Code, Cursor, …)
can drive the Switch: take screenshots, press buttons, touch the screen, read
and write the SD card, and start or stop programs while you test homebrew.

Typical agent loop:

1. `curl -T myapp.nro` (or `upload_file`) to deploy a build
2. `screenshot` + `tap_buttons` to launch it from hbmenu
3. `screenshot` / `read_file` to watch the app and its logs, then iterate

## What this fork adds

A fork of [TooTallNate/sys-autopilot](https://github.com/TooTallNate/sys-autopilot) with:

- **Process control**: start, stop, restart and query any program by title id.
  Reload a sysmodule without rebooting.
- **File explorer** in the browser at `http://<console>:4150/`.
- **File move / rename**, **touch gestures** (tap, swipe), **keep awake**.
- **Smaller footprint**: 1 MB heap instead of 4 MB, one shared per-request
  buffer instead of per-feature statics, about 65 KB less code.
- **Reliable networking**: ten sockets instead of two for the same memory, so
  browsers and concurrent clients no longer get random resets.
- **Modular build**: leave out features with `FEATURES=` or MCP with `MCP=0`.

## Install

Requires Atmosphère and firmware 10.0.0+.

Extract the zip from [Releases](https://github.com/o0Zz/sys-autopilot/releases)
to the root of the SD card and reboot. The server starts at boot on port 4150:

```
atmosphere/contents/4200000000004150/exefs.nsp
atmosphere/contents/4200000000004150/flags/boot2.flag
config/sys-autopilot/config.ini
```

The console finds itself on the LAN as `switch-<last 4 of serial>.local`
(mDNS). `scripts/discover.sh` lists every console on the subnet.

## Configuration

`sdmc:/config/sys-autopilot/config.ini`, created on first boot. Changes apply
after a reboot.

```ini
[server]
port = 4150
; Blank = switch-<last 4 of serial>.
hostname =
; Bearer token.
token =
; Basic auth. Also enables the OAuth login for MCP clients.
username =
password =
; Write sdmc:/config/sys-autopilot/log.txt: false, error, warn, info, debug.
log = false

[power]
; Hold off auto-sleep (sleep turns the WLAN off).
keep_awake = true
```

Auth is on when `token` is set, or both `username` and `password`. It is plain
HTTP: treat the API as LAN-trusted.

Log lines are columns: level, local time, module, message. At 256 KB the file
moves to `log.old.txt` and a new one starts.

```
|I|2026-09-29 14:03:12.345|server  | listening on port 4150
|I|2026-09-29 14:03:15.020|http    | GET /status
|E|2026-09-29 14:03:16.781|process | launch 0100000000010000 failed rc=0x410
```

## MCP

The endpoint is `POST /mcp` (stateless Streamable HTTP, JSON-RPC 2.0).

With `username`/`password` set, clients that support MCP OAuth need nothing
else. The first request opens a login page served by the console:

```sh
claude mcp add --transport http switch http://switch-5322.local:4150/mcp
```

With a static token instead:

```json
{
  "mcpServers": {
    "switch": {
      "type": "http",
      "url": "http://switch-5322.local:4150/mcp",
      "headers": { "Authorization": "Bearer <token>" }
    }
  }
}
```

OAuth tokens never expire. They are stored one per line in
`config/sys-autopilot/tokens.txt`; delete a line to revoke it.

| Area | Tools |
|---|---|
| Screen | `screenshot` (returned as an image the agent sees) |
| Buttons | `tap_buttons`, `tap_sequence`, `hold_buttons`, `release_buttons`, `set_stick`, `clear_input` |
| Touch | `tap_screen`, `swipe_screen` (1280x720, same space as the screenshot; handheld only) |
| Files | `list_directory`, `read_file` (negative `offset` = tail), `upload_file`, `move_file`, `delete_file`, `hash_file` |
| Processes | `process_list`, `process_status`, `process_start`, `process_stop`, `process_restart` |
| Settings | `get_*` / `set_*` for `theme`, `nickname`, `brightness`, `volume`, `auto_time`, `datetime`; `airplane_mode` |
| System | `status`, `list_installed_titles`, `get_dns`, `set_dns`, `sleep`, `restart`, `power_off` |
| Auth | `create_token`, `revoke_token` |

Buttons: `A B X Y L R ZL ZR PLUS MINUS UP DOWN LEFT RIGHT LSTICK RSTICK HOME
CAPTURE`.

Upload large files (`.nro`, `.nsp`) with `curl -T`, not `upload_file`: tool
arguments are generated token by token, so big uploads cost a lot of context.

`airplane_mode` is one-way: it cuts the server off, and wireless must be
turned back on at the console.

## Reloading a sysmodule

```sh
curl -X POST http://<ip>:4150/process/stop -d '{"titleId":"690000000000000d"}'
curl -T exefs.nsp "http://<ip>:4150/files?path=/atmosphere/contents/690000000000000d/exefs.nsp"
curl -X POST http://<ip>:4150/process/start -d '{"titleId":"690000000000000d"}'
```

A program does not need `flags/boot2.flag` to be started this way. Leave the
flag off while developing: a build that crashes at boot then cannot take the
console down, and you can still upload the fix. `stop` is a hard kill.

## REST API

All responses are JSON unless noted. Send `Authorization: Bearer <token>` (or
Basic) when auth is on. Paths are rooted at the SD card.

```
GET  /screenshot                                  image/jpeg, 1280x720
POST /input/tap      {"buttons":["A"],"durationMs":100}
POST /input/hold | /input/release  {"buttons":["ZL"]}
POST /input/stick    {"side":"left","x":1.0,"y":0.0,"durationMs":500}
POST /input/clear | /controller/attach | /controller/detach
POST /input/touch    {"x":640,"y":360}
POST /input/swipe    {"fromX":640,"fromY":600,"toX":640,"toY":150,"durationMs":250}

GET    /files?path=/switch/app/log.txt[&offset=-4096]   read (or list a directory)
PUT    /files?path=/switch/app.nro                      upload the request body
DELETE /files?path=/switch/app.nro                      delete a file or empty directory
POST   /files/move?path=/a.nro&to=/b.nro                rename / move, never overwrites
GET    /files/hash?path=/switch/app.nro                 SHA-256

GET  /process?titleId=<id>                        running + pid
GET  /process/list                               every running process: pid + titleId
POST /process/start | /process/stop | /process/restart  {"titleId":"<id>"}

POST|PUT /install[?storage=sd|nand]               stream an NSP or XCI (not NSZ)
GET  /titles                                      installed applications
GET|POST /network/dns                             {"primary":"…","secondary":"…"} or {"automatic":true}
GET|POST /settings/{theme,nickname,brightness,volume,auto-time,datetime}
POST /settings/airplane                           one-way, see above
POST /power/sleep | /power/restart | /power/off
GET  /status                                      version, firmware, heap, battery, uptime
```

## File explorer

Open `http://<console>:4150/` in a browser: browse, edit and save (Ctrl+S),
upload by drag and drop, download, rename, delete, tail a log live, reboot. It
signs in with Basic auth, so it needs `username`/`password`.

## Building

Needs [devkitPro](https://devkitpro.org/) with `switch-dev`.

```sh
make                              # sys-autopilot.nsp
make dist                         # SD card tree in dist/
make MCP=0                        # no MCP or OAuth: about 100 KB code and 120 KB RAM less
make FEATURES="explorer power"    # optional features to include (run make clean first)
./tests/run.sh                    # host tests, needs only a C compiler
```

The optional features are `explorer install network power process titles`
(all on by default). `status screen input files settings` are always built.

Push a tag (`git tag 1.6.0 && git push origin 1.6.0`) to publish a release.

## Limitations

- Single-threaded: a tap with a duration blocks until it is released, which
  keeps agent actions in order.
- Touch input only works in handheld mode.
- Screenshots fail where the OS blocks capture.
- No TLS.

## License

MIT, see [LICENSE](LICENSE). The vendored `lib/jsmn` keeps its own MIT license.
