---
"sys-autopilot": minor
---

Expose file move/rename as the `move_file` MCP tool. `POST /files/move`
already existed on the REST side, but no tool wrapped it, so an agent could
list, read, upload, hash and delete files yet had to drop out of MCP to
rename one. Same semantics as the endpoint: destination parents are created,
an existing destination is never overwritten, and moving a path onto itself
is a no-op.
