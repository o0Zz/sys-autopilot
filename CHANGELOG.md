# sys-autopilot

## 1.6.0

### Minor Changes

- [`e4e6cc7`](https://github.com/TooTallNate/sys-autopilot/commit/e4e6cc7683b871ccf12ee77d9670e3457b614918) Thanks [@o0Zz](https://github.com/o0Zz)! - Serve a built-in file explorer: `GET /` redirects to `/explorer`, a single
  self-contained page that browses the SD card and edits text files in place
  (upload, download, delete, and a live tail for logs) by calling the existing
  `/files` endpoints same-origin. The 401 challenge now offers Basic before
  Bearer so browsers show their login prompt when `username`/`password` are set.

- [`d1c1458`](https://github.com/TooTallNate/sys-autopilot/commit/d1c145820c771edbfe31d480cdf069e86cee897c) Thanks [@o0Zz](https://github.com/o0Zz)! - Add `POST /files/move?path=<src>&to=<dst>` to rename or move a file or
  directory on the SD card (parent directories are created, an existing
  destination is never overwritten), and a **Rename** button in the file
  explorer that uses it.

- [`433b5ef`](https://github.com/TooTallNate/sys-autopilot/commit/433b5ef3ba482e6483897c550286b81cd1893b2f) Thanks [@o0Zz](https://github.com/o0Zz)! - Hold off auto-sleep while the sysmodule runs. Sleep powers down the WLAN
  module, so the console stopped answering a few minutes after boot and only a
  physical button press brought it back — which made unattended remote driving
  impossible.

  The server pings `idle:sys` (`ReportUserIsActive`) every 5 seconds — plus
  once at startup and again on every wake — which resets the console's idle
  counter. The interval has to beat the shortest idle policy the system applies,
  not the shortest sleep plan offered in System Settings (1 minute): the lock
  screen (“press A three times”, shown after boot and after each wake) idles out
  in about 10 seconds. Nothing is persisted: the System Settings sleep plan is
  left untouched, so stopping the sysmodule restores normal behaviour
  immediately. An explicit sleep — the power button, or the
  `sleep` tool — still works.

  Controlled by a new `[power] keep_awake` setting in `config.ini`, on by
  default, and reported as `keepAwake` by `GET /status` (false when the setting
  is off _or_ the `idle:sys` session could not be opened).

  Requires the `idle:sys` service, now added to the NPDM.

- [`196d511`](https://github.com/TooTallNate/sys-autopilot/commit/196d51168f941dd09914db42dbc08e57f813eadf) Thanks [@o0Zz](https://github.com/o0Zz)! - Expose file move/rename as the `move_file` MCP tool. `POST /files/move`
  already existed on the REST side, but no tool wrapped it, so an agent could
  list, read, upload, hash and delete files yet had to drop out of MCP to
  rename one. Same semantics as the endpoint: destination parents are created,
  an existing destination is never overwritten, and moving a path onto itself
  is a no-op.

- [`17db817`](https://github.com/TooTallNate/sys-autopilot/commit/17db817a84e6c2240287d7b9a3a1ab830fafa69b) Thanks [@o0Zz](https://github.com/o0Zz)! - Cut the sysmodule's resident memory by about 850 KB, leaving more for the rest
  of the system.

  The large buffers that each feature kept as its own static array (screenshot
  JPEG, title installer, title listing, file I/O) now share one request-scoped
  arena, so only the largest of them is resident instead of all of them at
  once. The fourteen per-handler JSON token buffers (16 KB each) are now one.
  The build also uses `-Os`, drops unreferenced data, and discards the
  libraries' unused `.eh_frame` unwind tables (~40 KB; nothing here unwinds).

  New `make MCP=0` build option leaves out the MCP endpoint and its OAuth
  browser login for REST-only setups, saving about another 100 KB of code and
  120 KB of RAM. With it, Bearer auth accepts only the `token` from
  `config.ini`.

  `GET /status` now reports `heapSizeBytes`, `heapArenaBytes` and
  `heapUsedBytes`, so the heap size can be tuned from real numbers.

- [#22](https://github.com/TooTallNate/sys-autopilot/pull/22) [`9d8a105`](https://github.com/TooTallNate/sys-autopilot/commit/9d8a105e8371a8ea52cb9153e8886340ae36cf8b) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Add DNS configuration for the active network connection. `GET /network/dns`
  reads the current config (automatic/manual + primary/secondary servers) and
  `POST /network/dns` sets manual DNS servers or reverts to DHCP — handy for
  pointing the console at a custom/black-hole DNS while keeping it on the LAN.
  Also exposed as the `get_dns` / `set_dns` MCP tools.

  Setting the profile requires the `nifm:a` admin service. Because libnx's
  `nifmInitialize` is refcounted and ignores the service type after the first
  call, the sysmodule now opens nifm as Admin at boot (a superset of the User
  session used for mDNS) so the profile write is authorized.

- [`9dd30fd`](https://github.com/TooTallNate/sys-autopilot/commit/9dd30fd66f7482eb016885083b7a34e17b55d830) Thanks [@o0Zz](https://github.com/o0Zz)! - Add process control: start, stop, restart and query any program by title id.
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

- [`b221dfc`](https://github.com/TooTallNate/sys-autopilot/commit/b221dfce83a5549a71b25d75561972eec6406f21) Thanks [@o0Zz](https://github.com/o0Zz)! - Add touch screen input: `POST /input/touch` taps a pixel coordinate and
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

### Patch Changes

- [`b221dfc`](https://github.com/TooTallNate/sys-autopilot/commit/b221dfce83a5549a71b25d75561972eec6406f21) Thanks [@o0Zz](https://github.com/o0Zz)! - Explorer: keep the current directory (and open file) in the location hash, so
  F5 / Ctrl+R comes back where you were instead of the root, back/forward walk
  the browsing history, and the address bar can be bookmarked or shared. A hash
  pointing at something that is gone falls back to `/`.

- [`0acecfd`](https://github.com/TooTallNate/sys-autopilot/commit/0acecfd8c6fcbb6ae4a2f1a24915b42feeda6f9c) Thanks [@o0Zz](https://github.com/o0Zz)! - Explorer: add a Reboot button (with confirmation) that calls `POST /power/restart`,
  and save the open file with Ctrl+S / Cmd+S.

- [#25](https://github.com/TooTallNate/sys-autopilot/pull/25) [`51a249c`](https://github.com/TooTallNate/sys-autopilot/commit/51a249ca27be371020aaec1d2d1b0590e7cb3de3) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Support HTTP/1.1 keep-alive so MCP clients can reuse one connection across
  initialize → notifications/initialized → tools/list. Responses previously
  forced `Connection: close`, which made the Streamable HTTP transport's reused
  socket get reset ("socket connection closed unexpectedly"). The server now
  keeps the connection alive when the client speaks HTTP/1.1, drains the request
  body between requests, and half-closes cleanly to avoid RSTs. `GET /mcp`
  returns 405 (no server-initiated SSE stream offered), the spec-sanctioned way
  to decline the optional channel.

- [`b221dfc`](https://github.com/TooTallNate/sys-autopilot/commit/b221dfce83a5549a71b25d75561972eec6406f21) Thanks [@o0Zz](https://github.com/o0Zz)! - Stop resetting reused keep-alive connections. Responses advertised
  `Keep-Alive: timeout=10` while the server dropped an idle connection after
  50ms, so a browser reloading the explorer (Ctrl+R) wrote its request into a
  socket we had already closed; `close()` with those bytes still queued sends an
  RST, which surfaced as a connection reset before the retry succeeded. The
  advertised timeout now matches what the server actually does, and connections
  are closed by half-closing and draining first, so the peer always sees a clean
  end of connection it can silently retry.

- [#24](https://github.com/TooTallNate/sys-autopilot/pull/24) [`b524891`](https://github.com/TooTallNate/sys-autopilot/commit/b52489188a4f013e61e910d12cb318f60466ee73) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Fix ~5s latency on every request when reaching the console by its `.local`
  hostname. The mDNS responder answered A queries but stayed silent for AAAA, so
  dual-stack resolvers (macOS `getaddrinfo`, used by curl/ping and MCP clients)
  blocked ~5s waiting on a nonexistent AAAA before falling back to IPv4. The A
  record and the announcement now carry an NSEC record asserting "A only, no
  AAAA" per RFC 6762 §6.1, so resolvers proceed immediately.

- [`fdf0188`](https://github.com/TooTallNate/sys-autopilot/commit/fdf018899f14bc39e460a94320b242ec1d060ab6) Thanks [@o0Zz](https://github.com/o0Zz)! - Fix the connection resets that made the server look unreachable at random.
  Clients saw "connection forcibly closed", the explorer reset on reload, and
  REST tools failed with WinError 10054 whenever anything else was talking to
  the console.

  The socket buffer pool bsd allocates is `sb_efficiency * (tcp_tx + tcp_rx +
udp_tx + udp_rx)`, and every socket draws from it, including ones merely
  waiting in the listen backlog and ones sitting in TIME_WAIT. At
  `sb_efficiency = 2` the pool held the listener plus exactly one connection, so
  the second concurrent client was accepted by the stack and then immediately
  reset - and browsers open several connections per page load. Smaller
  per-socket buffers now buy ten slots for roughly the same memory.

  The server also closed every connection itself, so each one left the console
  holding TIME_WAIT and a burst of requests exhausted the pool anyway.
  `close_client()` now drains first and lets the peer hang up, which makes the
  console the passive closer; responses all carry a Content-Length, so clients
  close on their own within a millisecond or two. A peer that connects and then
  sends nothing is dropped after a second instead of occupying the
  single-threaded server for the full 10s I/O timeout.

  Measured on hardware: concurrent connections 1 -> 8, and 60 rapid sequential
  requests, 4 rounds of 6 parallel requests, and 5 req/s sustained all complete
  with zero failures where the first burst previously died at request 14.

## 1.5.0

### Minor Changes

- [#21](https://github.com/TooTallNate/sys-autopilot/pull/21) [`a7c9a45`](https://github.com/TooTallNate/sys-autopilot/commit/a7c9a450c325da9667df6c4466ad2b4e211c65c3) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Add a tool to list installed titles. `GET /titles` (and the
  `list_installed_titles` MCP tool) enumerates the base applications installed on
  the console across SD, internal storage, and an inserted gamecard, returning
  each title's id, content-meta version, storage, and display name (resolved from
  its control data / NACP). Read-only — uses the `ncm`/`ns` access the installer
  already declares, no new NPDM services.

- [#16](https://github.com/TooTallNate/sys-autopilot/pull/16) [`c2a15c3`](https://github.com/TooTallNate/sys-autopilot/commit/c2a15c30fddbd33d402c9e149211a731ab64cb00) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Add system-settings tools (REST + MCP):

  - `get_theme` / `set_theme` — read/set the UI theme (light or dark).
  - `get_nickname` / `set_nickname` — read/set the console's device nickname.
  - `get_brightness` / `set_brightness` — read/set screen brightness (0.0–1.0).
  - `get_volume` / `set_volume` — read/set master volume (0.0–1.0).
  - `airplane_mode` — enable airplane mode (disable wireless). One-way: it cuts
    the server's own connectivity and cannot be undone remotely, so there is no
    remote re-enable.
  - `get_auto_time` / `set_auto_time` — read/set internet clock synchronization.
  - `get_datetime` / `set_datetime` — read the local date/time/timezone, or set
    the date/time (written via the network system clock, which the displayed time
    follows). The timezone can't be changed remotely. If internet time sync is
    on, the OS may re-sync later; disable it first to make a manual time stick.
  - `status` now also reports `batteryPercent` and `charging`.

  Exposed both as MCP tools and `/settings/*` REST endpoints. Adds the `lbl`,
  `audctl`, and `psm` services to the NPDM (theme/nickname/auto-time use the
  existing `set:sys`, date/time uses `time:s`, airplane mode uses `nifm`).

  Note: `set_theme` updates the stored setting immediately but the HOME menu
  only reflects it after it reloads (sleep/wake or reboot); brightness and volume
  take effect live.

- [#18](https://github.com/TooTallNate/sys-autopilot/pull/18) [`af94253`](https://github.com/TooTallNate/sys-autopilot/commit/af94253e704fba9bd515881d12f27c8b6051b9e6) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Add network title installation. Stream an NSP straight to the console with a
  single `curl -T` and it is installed into NCM content storage and registered
  so it appears on the HOME menu — no SD-card staging, nothing to clean up.

  ```sh
  curl -g -T "Game [0100...000][v0].nsp" http://<ip>:4150/install
  ```

  - New `POST`/`PUT /install[?storage=sd|nand]` endpoint. The NSP is streamed
    (sized from `Content-Length`), so multi-GB titles install without buffering
    to disk.
  - Parses the PFS0/NSP, writes each NCA via the NCM placeholder/register flow,
    verifies each NCA's SHA-256 against its content id, parses the CNMT, sets the
    content-meta database entry, imports the ticket (`es`), and pushes the
    application record (`ns`). Rolls back written content on failure.
  - No external keys required: NCAs are copied byte-for-byte, and the CNMT is read
    from the registered meta NCA via the firmware's own decryption.
  - Adds the `ncm`, `ns:am2`, and `es` services to the NPDM and vendors small
    ns/es IPC wrappers that libnx does not expose.

  NSP only for now; compressed NSZ is planned as a follow-up.

- [#20](https://github.com/TooTallNate/sys-autopilot/pull/20) [`b6c7144`](https://github.com/TooTallNate/sys-autopilot/commit/b6c7144504fc6cbe3d934faf8d81267e017239af) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Install **XCI** (gamecard image) titles via the existing `/install` endpoint.
  The container format is auto-detected from the stream — `PFS0`/NSP installs as
  before, and an XCI is recognized by its `HEAD` card-header magic (both trimmed
  and full layouts). For XCI, the installer skips to the root HFS0, locates the
  `secure` partition, and streams its NCAs into NCM using the same
  placeholder/register → CNMT → application-record flow as NSP. Fully streaming
  (sized from `Content-Length`); no extra sysmodule memory is reserved.

  Verified on hardware: a retail XCI installs and launches.

  Note: gamecard NCAs are not guaranteed to hash to their filename content id, so
  the per-NCA SHA-256 check (kept for NSP) is skipped for XCI; the meta NCA is
  still verified and the CNMT drives registration.

## 1.4.1

### Patch Changes

- [#13](https://github.com/TooTallNate/sys-autopilot/pull/13) [`3a2abd0`](https://github.com/TooTallNate/sys-autopilot/commit/3a2abd0e319953fddd192c9f99f77db9d22bcea3) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Fix the console hanging on wake-from-sleep when file logging is enabled
  (`log = true`). The log sink writes to the SD card, and a write triggered
  during the PSC sleep transition (e.g. the "power: sleeping" line) issued
  fsp-srv I/O inside the window between the sleep notification and
  acknowledgement, which hangs the wake sequence. File writes are now suspended
  for the duration of the sleep/wake transition.

- [#15](https://github.com/TooTallNate/sys-autopilot/pull/15) [`1aeb7da`](https://github.com/TooTallNate/sys-autopilot/commit/1aeb7da23d96b540eeaee876d77d4b4644036ae7) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Re-advertise mDNS when the network changes (wifi reconnect, airplane-mode
  toggle, DHCP renewal) so the console's `<hostname>.local` name keeps resolving
  without a reboot. Previously the advertisement only happened at startup, so it
  went stale after connectivity changes.

  - Detects connectivity changes by polling nifm for the current IP every ~2s
    and acting when it changes. (We tested nifm's connectivity-change event on
    hardware; on an unsubmitted request it never fires, so polling is the
    reliable trigger.)
  - On a detected IP change, the HTTP listener and mDNS socket are both rebuilt
    (a network teardown can otherwise leave the listener silently not accepting).
  - Unsolicited announcements now retry until a send actually succeeds, instead
    of giving up while routing is still coming up after the network returns.

  Sleep-safety: the IP poll runs only while awake, so no nifm IPC hits the PSC
  sleep window. (The one confirmed sleep/wake hazard is filesystem I/O during
  the transition, fixed separately by suspending the log sink.)

## 1.4.0

### Minor Changes

- [#11](https://github.com/TooTallNate/sys-autopilot/pull/11) [`9392d7e`](https://github.com/TooTallNate/sys-autopilot/commit/9392d7e8e8b66da6955ce2c00a234fc35ec69fbb) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Add optional file logging for the sysmodule. Since a sysmodule has no console
  output, diagnostics can now be captured by setting `log = true` in
  `config.ini`; output is appended to `sdmc:/config/sys-autopilot/log.txt`.
  Logging is off by default and gated at runtime, so it has no cost when unused.

- [#10](https://github.com/TooTallNate/sys-autopilot/pull/10) [`4851df6`](https://github.com/TooTallNate/sys-autopilot/commit/4851df624f862991392fd5f3fb8473da323b8bd9) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Add mDNS / DNS-SD network discovery. The console now advertises itself on the
  local network as `<hostname>.local` and publishes the `_sys-autopilot._tcp`
  service with a TXT record describing the version/path/auth/model/firmware/
  Atmosphère version, so MCP clients and `curl` can target a stable name instead
  of a hard-coded IP.

  - New `hostname` key in `config.ini`. When blank it auto-generates
    `switch-<last 4 of serial>`, which is unique per console so two devices on
    the same network don't collide.
  - The LAN address is obtained from `nifm` (a sysmodule's `gethostid()` only
    ever returns loopback), with the server retrying until the interface is up.
  - New `scripts/discover.sh` helper browses for consoles on the LAN.

## 1.3.0

### Minor Changes

- [#8](https://github.com/TooTallNate/sys-autopilot/pull/8) [`4a246b9`](https://github.com/TooTallNate/sys-autopilot/commit/4a246b9cb581cc64744d2a19e6f0d7ea2dda076d) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Add file hashing for upload verification. A new `hash_file` MCP tool computes a
  file's SHA-256 (hardware-accelerated and streamed in 32 KB chunks, so memory use
  is constant for any file size) and returns just the 64-char hex digest. Passing
  an `expected` digest also reports a `matched` boolean, so an agent can confirm an
  upload landed intact in a single round-trip. The same digest is available over
  the raw HTTP API via `GET /files/hash?path=...`.

## 1.2.0

### Minor Changes

- [#7](https://github.com/TooTallNate/sys-autopilot/pull/7) [`8552135`](https://github.com/TooTallNate/sys-autopilot/commit/8552135cb90a8a102b2857e0d73b64a9cd8916a2) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Add `create_token` and `revoke_token` MCP tools. `create_token` mints a
  bearer token over the already-authenticated MCP channel; `revoke_token`
  removes a previously issued token again (agents can clean up after
  themselves). This lets agents use the raw HTTP
  API (e.g. `curl -T` for large file uploads, where base64 tool-call
  content would be impractical) without being handed credentials out of
  band. Tokens share the `tokens.txt` store and revocation model with the
  OAuth flow.

- [#6](https://github.com/TooTallNate/sys-autopilot/pull/6) [`f5f4be8`](https://github.com/TooTallNate/sys-autopilot/commit/f5f4be8855d892c438540863a6fedb8fa9fd287c) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Input MCP tools (`tap_buttons`, `tap_sequence`, `hold_buttons`,
  `release_buttons`, `set_stick`, `clear_input`) accept an optional
  `"screenshot": true` argument that appends a screenshot (taken after an
  optional `screenshotDelayMs`, default 250ms) to the tool result as an
  image content block — saving the agent a separate screenshot round trip
  after every input.

- [#2](https://github.com/TooTallNate/sys-autopilot/pull/2) [`7930a11`](https://github.com/TooTallNate/sys-autopilot/commit/7930a11f81097124c3411eaaa56f8706344a28ea) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Add an OAuth 2.1 browser login flow for MCP clients. When username/password
  are configured, clients like Claude Code now authenticate with zero manual
  header configuration: the first 401 triggers metadata discovery (RFC 9728 /
  RFC 8414), dynamic client registration (RFC 7591), and a browser login page
  served by the Switch; after signing in, the client receives a non-expiring
  bearer token via the authorization-code + PKCE S256 grant. Tokens are
  persisted to `config/sys-autopilot/tokens.txt` (revoke by deleting a line).
  Also adds CORS support for browser-based MCP clients.

- [#5](https://github.com/TooTallNate/sys-autopilot/pull/5) [`0a3b148`](https://github.com/TooTallNate/sys-autopilot/commit/0a3b14837ec838816bcc1281f7489cf2d398faf8) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Add `sleep`, `restart`, and `power_off` MCP tools (and matching
  `POST /power/sleep|restart|off` REST endpoints) so agents can manage
  console power state. Actions execute only after the confirmation
  response has been delivered; tool descriptions carry explicit warnings
  about server availability (sleep and power-off require physical human
  interaction to recover; restart may too, depending on bootloader
  autoboot configuration).

### Patch Changes

- [#4](https://github.com/TooTallNate/sys-autopilot/pull/4) [`984167d`](https://github.com/TooTallNate/sys-autopilot/commit/984167df764f4ab3cb1f041e32070b5252faa56e) Thanks [@TooTallNate](https://github.com/TooTallNate)! - Fix console crash on sleep (hard restart on wake). Two changes were
  required: the HDLS work buffer (transfer memory mapped into the hid
  sysmodule) is now attached lazily and released when the console
  prepares to sleep, and the sysmodule registers as a PSC power
  management module so it can close its listener socket and quiesce all
  bsd IPC before acknowledging the sleep transition. Previously the held
  HDLS state and live socket activity made the sleep sequence fail (omm
  abort with psc error 2165-1001, occasional bsdsockets aborts), forcing
  a full reboot on wake. The virtual controller re-attaches automatically
  on the first input request after wake.

## 1.1.0

Initial public release.

- Persistent HTTP server sysmodule (auto-starts at boot via boot2)
- Native MCP endpoint at `POST /mcp` (stateless Streamable HTTP, JSON-RPC 2.0)
  with 12 tools: screenshots returned as image content, controller input
  (taps, sequences, holds, sticks), and SD card file access
- REST API with JSON request bodies for input endpoints
- JPEG screenshots via `caps:sc` (firmware 10.0.0+)
- Virtual Pro Controller input injection via `hiddbg` HDLS
- File upload/download/listing/deletion rooted at `sdmc:/`; MCP
  `upload_file` content is streamed to disk so size is bounded by the SD
  card, not RAM
- INI configuration (`/config/sys-autopilot/config.ini`): port, bearer
  token and/or HTTP Basic credentials
- Dev `.nro` flavor of the same server for fast iteration via nxlink
