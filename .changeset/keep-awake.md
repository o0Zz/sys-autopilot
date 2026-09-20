---
"sys-autopilot": minor
---

Hold off auto-sleep while the sysmodule runs. Sleep powers down the WLAN
module, so the console stopped answering a few minutes after boot and only a
physical button press brought it back — which made unattended remote driving
impossible.

The server now pings `idle:sys` (`ReportUserIsActive`) every 30 seconds, which
resets the console's auto-sleep counter. Nothing is persisted: the System
Settings sleep plan is left untouched, so stopping the sysmodule restores
normal behaviour immediately. An explicit sleep — the power button, or the
`sleep` tool — still works.

Controlled by a new `[power] keep_awake` setting in `config.ini`, on by
default, and reported as `keepAwake` by `GET /status` (false when the setting
is off *or* the `idle:sys` session could not be opened).

Requires the `idle:sys` service, now added to the NPDM.
