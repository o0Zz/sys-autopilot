#include "core/config.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#define CONFIG_PATH CONFIG_DIR "/config.ini"

// Prefix for the auto-generated default hostname ("switch-<serial suffix>").
#define CONFIG_DEFAULT_PREFIX "switch"
// How many trailing serial characters to append to the default hostname.
#define CONFIG_SERIAL_SUFFIX_LEN 4

static const char *kDefaultConfig =
    "; sys-autopilot configuration\n"
    "; Changes take effect after a reboot (or sysmodule restart).\n"
    "\n"
    "[server]\n"
    "; TCP port the HTTP server listens on.\n"
    "port = 4150\n"
    "\n"
    "; Name advertised on the local network via mDNS / DNS-SD (Bonjour).\n"
    "; The console becomes reachable at '<hostname>.local' and advertises the\n"
    "; '_sys-autopilot._tcp' service so clients can discover it without an IP.\n"
    "; Leave blank to auto-generate 'switch-<last 4 of serial>' (unique per console).\n"
    "hostname =\n"
    "\n"
    "; Optional authentication. Auth is enforced when EITHER a bearer token\n"
    "; is set, or both username and password are set (HTTP Basic).\n"
    "; Clients may then use 'Authorization: Bearer <token>' or Basic auth.\n"
    "; Username+password also enables the OAuth browser login for MCP clients;\n"
    "; tokens issued that way are stored in tokens.txt next to this file.\n"
    "token =\n"
    "username =\n"
    "password =\n"
    "\n"
    "; Write diagnostics to log.txt next to this file (the sysmodule has no\n"
    "; console output). Minimum level written: off, error, warn, info or debug.\n"
    "; Set to debug when troubleshooting, or off to write nothing.\n"
    "log = info\n"
    "\n"
    "[power]\n"
    "; Hold off auto-sleep while this sysmodule runs. Sleep powers down the\n"
    "; WLAN module, so the console stops answering until someone presses a\n"
    "; button on it. Nothing is persisted: set this to false and the console\n"
    "; sleeps again according to System Settings.\n"
    "keep_awake = true\n";

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

static bool is_true(const char *val) {
    return strcasecmp(val, "true") == 0 ||
           strcasecmp(val, "1") == 0 ||
           strcasecmp(val, "yes") == 0 ||
           strcasecmp(val, "on") == 0;
}

static const char *const kLogLevelNames[] = {"debug", "info", "warn", "error", "off"};

// `log = ...`: a level name, or a boolean (true = info). Anything else
// leaves logging off.
static LogLevel parse_log_level(const char *val) {
    if (is_true(val))
        return LOG_LEVEL_INFO;
    if (strcasecmp(val, "warning") == 0)
        return LOG_LEVEL_WARN;
    for (int i = 0; i < LOG_LEVEL_OFF; i++)
        if (strcasecmp(val, kLogLevelNames[i]) == 0)
            return (LogLevel)i;
    return LOG_LEVEL_OFF;
}

static void write_default_config(void) {
    mkdir("sdmc:/config", 0777);
    mkdir(CONFIG_DIR, 0777);
    FILE *f = fopen(CONFIG_PATH, "wb");
    if (f) {
        fwrite(kDefaultConfig, 1, strlen(kDefaultConfig), f);
        fclose(f);
    }
}

void config_load(Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->port = 4150;
    cfg->log_level = LOG_LEVEL_OFF;
    cfg->keep_awake = true;

    FILE *f = fopen(CONFIG_PATH, "rb");
    if (!f) {
        write_default_config();
        LOGI("config", "wrote default config to %s", CONFIG_PATH);
        return;
    }

    char line[256];
    char section[32] = "";
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (*s == '\0' || *s == ';' || *s == '#')
            continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = '\0';
                snprintf(section, sizeof(section), "%s", s + 1);
            }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (strcasecmp(section, "power") == 0) {
            if (strcasecmp(key, "keep_awake") == 0)
                cfg->keep_awake = is_true(val);
            continue;
        }

        if (strcasecmp(section, "server") != 0)
            continue;

        if (strcasecmp(key, "port") == 0) {
            int port = atoi(val);
            if (port > 0 && port <= 65535)
                cfg->port = port;
        } else if (strcasecmp(key, "username") == 0) {
            snprintf(cfg->username, sizeof(cfg->username), "%s", val);
        } else if (strcasecmp(key, "password") == 0) {
            snprintf(cfg->password, sizeof(cfg->password), "%s", val);
        } else if (strcasecmp(key, "token") == 0) {
            snprintf(cfg->token, sizeof(cfg->token), "%s", val);
        } else if (strcasecmp(key, "hostname") == 0) {
            snprintf(cfg->hostname, sizeof(cfg->hostname), "%s", val);
        } else if (strcasecmp(key, "log") == 0) {
            cfg->log_level = parse_log_level(val);
        }
    }
    fclose(f);

    // Activate the file log sink now that we know the user's preference, so
    // subsequent LOG*() calls (here and across the server) are captured.
    log_set_level(cfg->log_level);

    LOGI("config", "port=%d auth=%s hostname=%s log=%s keep_awake=%s", cfg->port,
         config_auth_enabled(cfg) ? "enabled" : "disabled",
         cfg->hostname[0] != '\0' ? cfg->hostname : "(auto)",
         kLogLevelNames[cfg->log_level],
         cfg->keep_awake ? "on" : "off");
}

bool config_basic_enabled(const Config *cfg) {
    return cfg->username[0] != '\0' && cfg->password[0] != '\0';
}

bool config_auth_enabled(const Config *cfg) {
    return cfg->token[0] != '\0' || config_basic_enabled(cfg);
}

// Keeps only DNS-label-safe chars (alnum and '-'), lowercasing letters.
static size_t append_sanitized(char *out, size_t cap, size_t len, const char *s) {
    for (; *s && len + 1 < cap; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
            out[len++] = c;
    }
    out[len] = '\0';
    return len;
}

const char *config_hostname(const Config *cfg, const char *serial,
                            char *out, size_t cap) {
    if (cfg->hostname[0] != '\0') {
        snprintf(out, cap, "%s", cfg->hostname);
        return out;
    }

    // Auto: "switch-<last N serial chars>" (or just "switch" without a serial).
    size_t len = append_sanitized(out, cap, 0, CONFIG_DEFAULT_PREFIX);
    size_t slen = serial ? strlen(serial) : 0;
    if (slen > 0) {
        const char *suffix = serial;
        if (slen > CONFIG_SERIAL_SUFFIX_LEN)
            suffix = serial + (slen - CONFIG_SERIAL_SUFFIX_LEN);
        if (len + 1 < cap)
            out[len++] = '-';
        len = append_sanitized(out, cap, len, suffix);
    }
    return out;
}
