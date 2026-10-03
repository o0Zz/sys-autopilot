# CLAUDE.md

Nintendo Switch sysmodule (C, libnx) that runs a single-threaded HTTP server
with a REST API and a native MCP endpoint. Title id `4200000000004150`,
port 4150. See README.md for the user-facing API.

## Commands

```sh
make                              # build sys-autopilot.nsp (needs devkitPro, python3)
make dist                         # SD card tree in dist/
make clean && make MCP=0          # build without MCP/OAuth
make clean && make FEATURES="…"   # choose optional features
./tests/run.sh                    # host test suite (plain cc + python3, no devkitPro)
```

Both `make` and `tests/run.sh` run `scripts/generate_resource.py`, which
turns every `*_tools.json` and `explorer.html` into a header under
`build/gen/` (or `tests/build/gen/`). The headers are not committed.

No devkitPro on Windows? Build and test in the CI image:

```sh
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src \
  devkitpro/devkita64:20260219 sh -c 'make clean && make'
```

The image has no host `cc`; install `gcc libc6-dev` with apt before running
`tests/run.sh` in it.

## Layout

- `source/main.c`: sysmodule entry. Fixed 1 MB inner heap, `__appInit`
  opens services by hand, socket pool config.
- `source/core/`: HTTP parser and responses, router, server loop, request
  memory, config, mDNS, logging.
- `source/platform/`: power (PSC sleep/wake, keep-awake), network interface,
  device info.
- `source/util/`: base64, JSON (jsmn wrappers), SHA-256, a streaming JPEG
  transcoder (scaled and cropped screenshots).
- `source/features/<name>/`: one folder per feature.
  - `<name>.c`: the service, no HTTP.
  - `<name>_http.c`: registers REST routes with `http_server_register_route`.
  - `<name>_mcp.c`: registers MCP tools.
  - `<name>_tools.json`: tool schemas. The build generates
    `build/gen/features/<name>/<name>_tools.h` from it.
- `source/features/feature_list.c`: initializes and registers every feature
  that is compiled in.
- `sys-autopilot.json`: NPDM. Every service the code opens must be listed here.
- `tests/`: host tests. `run.sh` lists the sources linked into each test binary.
- `lib/jsmn/`: vendored JSON tokenizer.

## Adding a feature

1. Create `source/features/<name>/` with the service, `_http.c` and `_mcp.c`.
2. Add it to `FEATURES` in the Makefile, or to `BASE_FEATURES` if it must
   always be built. The Makefile defines `FEATURE_<NAME>` for it.
3. Add its init and register calls to `feature_list.c`, behind
   `#ifdef FEATURE_<NAME>` (and `FEATURE_MCP` for the tools).
4. Add the services it opens to `sys-autopilot.json`.
5. Write tool schemas in `<name>_tools.json`. The build generates
   `<name>_tools.h`; include it as `features/<name>/<name>_tools.h`.
6. Add a test to `tests/run.sh`.

Route and tool tables are fixed size: `HTTP_MAX_ROUTES` (80) in
`core/http_server.h` and `MCP_MAX_TOOLS` (64) in `features/mcp/mcp_server.h`.

## Rules

- **Memory is tight.** Do not add large static buffers. Take per-request
  buffers from `request_alloc()` (`core/request.h`); they are freed when the
  response is sent. `scripts/check_static_buffers.sh` runs after every `make`
  and fails the build on any `.bss`/`.data` symbol over 4 KB that is not in
  its allowlist.
- **Keep code host-testable.** Put Switch-only code under `#ifdef __SWITCH__`
  and give the `#else` branch a small host model, as `process.c` and
  `power.c` do, so the route and tool layers run in the host tests.
- **Stock libnx only.** CI builds with the released libnx in the devkitPro
  image, not libnx master. If a wrapper exists only in master (for example
  `idlesys`), call the service directly with `smGetService` and
  `serviceDispatch`, as `power.c` does.
- **Sleep safety.** No socket or SD card I/O between the PSC sleep
  acknowledgement and wake. The server loop closes sockets and suspends file
  logging for that window. Do not add I/O that can run there.
- **Single-threaded server.** A handler blocks every other client until it
  returns. Keep handlers bounded. A handler that waits (the `wait_for_*`
  tools) caps itself at `HTTP_MAX_WAIT_MS` and sleeps through
  `http_server_wait_ms()`, which keeps the console awake meanwhile; one that
  loops over request memory rewinds it with `request_mark()` /
  `request_rewind()`.
- **u64 ids as strings in JSON** (pids, title ids). JSON numbers lose
  precision in most clients.
- `.sh`, `.html` and `.json` files must stay LF (`.gitattributes`). With
  CRLF, `sh` fails with `set: Illegal option -` and `generate_resource.py`
  rejects the input.

## Testing on a console

The console runs the server itself, so you can test through its REST API or
MCP tools. Screenshots come back as 1280x720 JPEG; touch coordinates use the
same space.

To try a new build of sys-autopilot itself, copy the new `exefs.nsp` to
`/atmosphere/contents/4200000000004150/` and reboot. Stopping this
sysmodule through its own `/process/stop` kills the server that is handling
the request.

## Releases

Push a tag (`git tag 1.6.0 && git push origin 1.6.0`). `release.yml` builds
with `APP_VERSION=<tag>` and publishes the SD card zip. Local builds use
`git describe` as the version.
