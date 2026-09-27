---
"sys-autopilot": minor
---

Serve a built-in file explorer: `GET /` redirects to `/explorer`, a single
self-contained page that browses the SD card and edits text files in place
(upload, download, delete, and a live tail for logs) by calling the existing
`/files` endpoints same-origin. The 401 challenge now offers Basic before
Bearer so browsers show their login prompt when `username`/`password` are set.
