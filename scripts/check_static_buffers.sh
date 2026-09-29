#!/bin/sh
# Fails the build when a large static buffer appears outside the places that
# are meant to own memory.
#
# Features must not keep big buffers in .bss/.data: every one of them would be
# resident for the life of the sysmodule. Large per-request buffers come from
# request memory (source/core/request.h); only the entries below may exceed
# the limit.
#
# usage: check_static_buffers.sh <elf>   (NM overrides the nm binary)
set -e

ELF="$1"
NM="${NM:-aarch64-none-elf-nm}"
LIMIT=4096

# name (exact, or a prefix ending in '*') : why it may be large
ALLOWED='
inner_heap*          the newlib heap (main.c)
g_request_memory     request memory (core/request.c)
g_request            the request being parsed, headers included (core/http_server.c)
g_codes              pending OAuth codes, must outlive the request (features/oauth)
fsdev_fsdevices      libnx
handles              libnx
__argdata__          libnx linker marker at the end of .bss (nm reports a bogus size)
'

"$NM" -S --size-sort "$ELF" | awk -v limit="$LIMIT" -v allowed="$ALLOWED" '
function hex(s,    i, c, v) {
    v = 0
    s = tolower(s)
    for (i = 1; i <= length(s); i++) {
        c = index("0123456789abcdef", substr(s, i, 1)) - 1
        v = v * 16 + c
    }
    return v
}
function is_allowed(name,    n, lines, i, pat) {
    n = split(allowed, lines, "\n")
    for (i = 1; i <= n; i++) {
        split(lines[i], f, " ")
        pat = f[1]
        if (pat == "") continue
        if (substr(pat, length(pat)) == "*") {
            if (index(name, substr(pat, 1, length(pat) - 1)) == 1) return 1
        } else if (name == pat) {
            return 1
        }
    }
    return 0
}
NF == 4 && $3 ~ /^[bBdD]$/ {
    size = hex($2)
    if (size > limit && !is_allowed($4)) {
        printf "static buffer %s is %d bytes (limit %d): take it from request memory instead\n", $4, size, limit
        bad = 1
    }
}
END { exit bad }
'
