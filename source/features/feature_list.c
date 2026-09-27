// Every feature this build contains, and how each one plugs into the servers.
//
// A feature is a folder under features/: a service (no HTTP), a *_http.c that
// registers its REST routes, and a *_mcp.c that registers its MCP tools.
// Adding one means adding its folder and a block here. Optional features are
// compiled in by the Makefile (FEATURES := ..., MCP := 0|1), which defines
// FEATURE_<NAME> for each one it builds.
#include "features/feature_list.h"
#include "core/http_server.h"
#include "core/log.h"

#include "features/files/files_http.h"
#include "features/input/input.h"
#include "features/input/input_http.h"
#include "features/screen/screen_http.h"
#include "features/settings/settings.h"
#include "features/settings/settings_http.h"
#include "features/status/status_http.h"

#ifdef FEATURE_EXPLORER
#include "features/explorer/explorer_http.h"
#endif
#ifdef FEATURE_INSTALL
#include "features/install/install.h"
#include "features/install/install_http.h"
#endif
#ifdef FEATURE_NETWORK
#include "features/network/network_http.h"
#endif
#ifdef FEATURE_POWER
#include "features/power/power_http.h"
#endif
#ifdef FEATURE_PROCESS
#include "features/process/process.h"
#include "features/process/process_http.h"
#endif
#ifdef FEATURE_TITLES
#include "features/titles/titles_http.h"
#endif

#ifdef FEATURE_MCP
#include "features/files/files_mcp.h"
#include "features/input/input_mcp.h"
#include "features/mcp/mcp_server.h"
#include "features/oauth/oauth.h"
#include "features/oauth/oauth_mcp.h"
#include "features/screen/screen_mcp.h"
#include "features/settings/settings_mcp.h"
#include "features/status/status_mcp.h"
#ifdef FEATURE_NETWORK
#include "features/network/network_mcp.h"
#endif
#ifdef FEATURE_POWER
#include "features/power/power_mcp.h"
#endif
#ifdef FEATURE_PROCESS
#include "features/process/process_mcp.h"
#endif
#ifdef FEATURE_TITLES
#include "features/titles/titles_mcp.h"
#endif
#endif

void features_init(void) {
    // lbl/audctl/psm for the settings endpoints (and the battery in /status).
    settings_init();

#ifdef FEATURE_INSTALL
    // ncm/ns/es for the title installer.
    if (!install_init())
        LOGF("install: services unavailable; /install disabled\n");
#endif

#ifdef FEATURE_PROCESS
    // pm:shell/pm:dmnt for starting, stopping and querying other programs.
    if (!process_init())
        LOGF("process: pm unavailable; /process disabled\n");
#endif
}

void features_register(const Config *cfg) {
    // --- always built ---
    status_http_register(cfg);
    screen_http_register();
    input_http_register();
    files_http_register();
    settings_http_register();

    // The HDLS work buffer holds hid transfer memory, which must not survive
    // the sleep transition.
    http_server_on_sleep(input_suspend);

    // --- optional ---
#ifdef FEATURE_EXPLORER
    explorer_http_register();
#endif
#ifdef FEATURE_INSTALL
    install_http_register();
#endif
#ifdef FEATURE_NETWORK
    network_http_register();
#endif
#ifdef FEATURE_POWER
    power_http_register();
#endif
#ifdef FEATURE_PROCESS
    process_http_register();
#endif
#ifdef FEATURE_TITLES
    titles_http_register();
#endif

    // --- MCP: the endpoint, its OAuth login, and each feature's tools ---
#ifdef FEATURE_MCP
    mcp_server_http_register();
    oauth_init(cfg);
    oauth_http_register();

    screen_mcp_register();
    input_mcp_register();
    status_mcp_register();
    files_mcp_register();
    oauth_mcp_register();
#ifdef FEATURE_POWER
    power_mcp_register();
#endif
#ifdef FEATURE_PROCESS
    process_mcp_register();
#endif
    settings_mcp_register();
#ifdef FEATURE_TITLES
    titles_mcp_register();
#endif
#ifdef FEATURE_NETWORK
    network_mcp_register();
#endif
#endif
}

void features_exit(void) {
#ifdef FEATURE_PROCESS
    process_exit();
#endif
#ifdef FEATURE_INSTALL
    install_exit();
#endif
    settings_exit();
    input_exit();
}
