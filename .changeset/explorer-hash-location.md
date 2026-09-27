---
"sys-autopilot": patch
---

Explorer: keep the current directory (and open file) in the location hash, so
F5 / Ctrl+R comes back where you were instead of the root, back/forward walk
the browsing history, and the address bar can be bookmarked or shared. A hash
pointing at something that is gone falls back to `/`.
