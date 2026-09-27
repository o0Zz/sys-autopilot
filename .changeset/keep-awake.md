---
"sys-autopilot": minor
---

Hold off auto-sleep while the sysmodule runs. Sleep powers down the WLAN
module, so the console stopped answering a few minutes after boot and only a
physical button press brought it back — which made unattended remote driving
impossible.

The server pings `idle:sys` (`ReportUserIsActive`) every 5 seconds — plus
once at startup and again on every wake — which resets the console's idle
counter. The interval has to beat the shortest idle policy the system applies,
not the shortest sleep plan offered in System Settings (1 minute): the lock
screen (“press A three times”, shown after boot and after each wake) idles out
in about 10 seconds. Nothing is persisted: the System Settings sleep plan is
left untouched, so stopping the sysmodule restores normal behaviour
immediately. An explicit sleep — the power button, or the
`sleep` tool — still works.

Controlled by a new `[power] keep_awake` setting in `config.ini`, on by
default, and reported as `keepAwake` by `GET /status` (false when the setting
is off *or* the `idle:sys` session could not be opened).

Requires the `idle:sys` service, now added to the NPDM.
