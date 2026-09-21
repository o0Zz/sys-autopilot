---
"sys-autopilot": patch
---

Stop resetting reused keep-alive connections. Responses advertised
`Keep-Alive: timeout=10` while the server dropped an idle connection after
50ms, so a browser reloading the explorer (Ctrl+R) wrote its request into a
socket we had already closed; `close()` with those bytes still queued sends an
RST, which surfaced as a connection reset before the retry succeeded. The
advertised timeout now matches what the server actually does, and connections
are closed by half-closing and draining first, so the peer always sees a clean
end of connection it can silently retry.
