#pragma once

// Registers the /power/* routes. The PSC/spsm plumbing they drive lives in
// platform/power (the server loop needs it for sleep handling regardless).
void power_http_register(void);
