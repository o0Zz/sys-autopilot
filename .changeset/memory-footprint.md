---
"sys-autopilot": minor
---

Cut the sysmodule's resident memory by about 850 KB, leaving more for the rest
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
