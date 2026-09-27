#pragma once

// Built-in file explorer: a single self-contained HTML page (explorer.html,
// baked in as a C string by scripts/generate_resource.py) that drives the
// /files endpoints from the browser. Being served by this same server is the
// point -- the page is same-origin with the API, so its fetches need no CORS
// grant, and HTTP Basic credentials the browser collected for the page are
// reused for every call it makes.

// Registers GET /explorer (the page) and GET / (redirects to /explorer, so
// the bare console address is usable).
void explorer_http_register(void);
