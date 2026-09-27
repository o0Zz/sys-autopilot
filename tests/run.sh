#!/bin/sh
# Builds and runs the host-side test suite (no devkitPro required).
set -e
cd "$(dirname "$0")"

SRC=../source
CORE=$SRC/core
UTIL=$SRC/util
PLATFORM=$SRC/platform
FEAT=$SRC/features
JSMN=../lib/jsmn
OUT=build
FAKE_SD=/tmp/sys-autopilot-fakesd

mkdir -p "$OUT"

CFLAGS="-g -Wall -Wextra -Wno-unused-parameter -I$SRC -I$JSMN"

# The HTTP router and request memory, used by every test that drives routes.
ROUTER="$CORE/http_router.c $CORE/request.c $CORE/http.c $CORE/config.c \
    $UTIL/base64.c $UTIL/json.c"

echo "== test_http =="
cc $CFLAGS -o "$OUT/test_http" test_http.c "$CORE/http.c" "$UTIL/base64.c"
"$OUT/test_http"

echo "== test_explorer =="
cc $CFLAGS -o "$OUT/test_explorer" test_explorer.c \
    "$FEAT/explorer/explorer_http.c" $ROUTER
"$OUT/test_explorer"

echo "== test_core =="
cc $CFLAGS -o "$OUT/test_core" test_core.c \
    "$FEAT/input/input_args.c" "$UTIL/base64.c" "$FEAT/input/buttons.c" "$UTIL/json.c" \
    "$FEAT/mcp/jstream.c" "$UTIL/sha256.c"
"$OUT/test_core"

echo "== test_mdns =="
cc $CFLAGS -o "$OUT/test_mdns" test_mdns.c \
    "$CORE/mdns.c" "$PLATFORM/device_info.c" "$CORE/config.c" "$PLATFORM/netif.c"
"$OUT/test_mdns"

echo "== test_install =="
cc $CFLAGS -o "$OUT/test_install" test_install.c "$FEAT/install/install.c"
"$OUT/test_install"

echo "== test_oauth =="
cc $CFLAGS \
    -DOAUTH_TOKENS_PATH="\"$FAKE_SD-tokens.txt\"" \
    -o "$OUT/test_oauth" test_oauth.c \
    "$FEAT/oauth/oauth.c" "$UTIL/sha256.c" "$PLATFORM/device_info.c" \
    "$CORE/mdns.c" "$PLATFORM/netif.c" $ROUTER
"$OUT/test_oauth"

echo "== test_mcp =="
cc $CFLAGS -Ifake -DFEATURE_MCP \
    -DFILES_ROOT="\"$FAKE_SD\"" \
    -DMCP_UPLOAD_TMP="\"$FAKE_SD/.upload.tmp\"" \
    -DFAKE_SD="\"$FAKE_SD\"" \
    -DOAUTH_TOKENS_PATH="\"$FAKE_SD-mcp-tokens.txt\"" \
    -o "$OUT/test_mcp" test_mcp.c stubs.c \
    "$FEAT/mcp/mcp_server.c" "$FEAT/mcp/jstream.c" \
    "$FEAT/screen/screen_mcp.c" \
    "$FEAT/input/input_mcp.c" "$FEAT/input/input_args.c" "$FEAT/input/buttons.c" \
    "$FEAT/status/status_mcp.c" \
    "$FEAT/files/files.c" "$FEAT/files/files_http.c" "$FEAT/files/files_mcp.c" \
    "$FEAT/oauth/oauth.c" "$FEAT/oauth/oauth_mcp.c" \
    "$FEAT/power/power_mcp.c" "$PLATFORM/power.c" \
    "$FEAT/process/process.c" "$FEAT/process/process_mcp.c" \
    "$FEAT/settings/settings.c" "$FEAT/settings/settings_mcp.c" \
    "$FEAT/titles/titles.c" "$FEAT/titles/titles_mcp.c" \
    "$FEAT/network/network.c" "$FEAT/network/network_mcp.c" \
    "$CORE/mdns.c" "$PLATFORM/netif.c" "$PLATFORM/device_info.c" "$UTIL/sha256.c" \
    $ROUTER
"$OUT/test_mcp"

# Release tooling: our resilient changelog generator (replaces the flaky
# @changesets/changelog-github). Runs only when Node is available; fetch is
# stubbed so no network/token is needed.
if command -v node >/dev/null 2>&1; then
    echo "== changelog-github =="
    node --test changelog-github.test.mjs
fi

echo "ALL HOST TESTS PASSED"
