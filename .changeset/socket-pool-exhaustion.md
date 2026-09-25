---
"sys-autopilot": patch
---

Fix the connection resets that made the server look unreachable at random.
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
