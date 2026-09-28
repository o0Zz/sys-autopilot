#include "features/process/process_mcp.h"
#include "features/mcp/mcp_server.h"
#include "features/process/process.h"
#include "features/process/process_tools.h"

#include <stdio.h>

// Shared by the four tools. Replies with its own error and returns false.
static bool arg_title_id(McpCall *call, uint64_t *out) {
    if (!process_available()) {
        mcp_reply_error(call, "process control unavailable");
        return false;
    }
    char raw[32] = {0};
    if (!mcp_arg_string(call, "titleId", raw, sizeof(raw)) || raw[0] == '\0') {
        mcp_reply_error(call, "missing string 'titleId'");
        return false;
    }
    if (!process_parse_title_id(raw, out)) {
        mcp_reply_error(call, "malformed 'titleId' (expected 16 hex digits)");
        return false;
    }
    return true;
}

static void tool_process_status(McpCall *call) {
    uint64_t tid;
    if (!arg_title_id(call, &tid))
        return;

    ProcessStatus st;
    char msg[160];
    process_status(tid, &st);
    if (st.running)
        snprintf(msg, sizeof(msg), "%016llx is running (pid %llu)",
                 (unsigned long long)tid, (unsigned long long)st.pid);
    else
        snprintf(msg, sizeof(msg), "%016llx is not running", (unsigned long long)tid);
    mcp_reply_text(call, msg);
}

static void tool_process_start(McpCall *call) {
    uint64_t tid;
    if (!arg_title_id(call, &tid))
        return;

    uint64_t pid = 0;
    uint32_t rc = 0;
    char msg[160];
    if (!process_start(tid, &pid, &rc)) {
        snprintf(msg, sizeof(msg), "failed to launch %016llx (rc 0x%08x)",
                 (unsigned long long)tid, rc);
        mcp_reply_error(call, msg);
        return;
    }
    snprintf(msg, sizeof(msg), "launched %016llx (pid %llu)",
             (unsigned long long)tid, (unsigned long long)pid);
    mcp_reply_text(call, msg);
}

static void tool_process_stop(McpCall *call) {
    uint64_t tid;
    if (!arg_title_id(call, &tid))
        return;

    uint32_t rc = 0;
    char msg[160];
    if (!process_stop(tid, &rc)) {
        snprintf(msg, sizeof(msg), "failed to terminate %016llx (rc 0x%08x)",
                 (unsigned long long)tid, rc);
        mcp_reply_error(call, msg);
        return;
    }
    snprintf(msg, sizeof(msg), "terminated %016llx", (unsigned long long)tid);
    mcp_reply_text(call, msg);
}

static void tool_process_restart(McpCall *call) {
    uint64_t tid;
    if (!arg_title_id(call, &tid))
        return;

    uint64_t pid = 0;
    uint32_t rc = 0;
    char msg[160];
    if (!process_restart(tid, &pid, &rc)) {
        snprintf(msg, sizeof(msg), "failed to restart %016llx (rc 0x%08x)",
                 (unsigned long long)tid, rc);
        mcp_reply_error(call, msg);
        return;
    }
    snprintf(msg, sizeof(msg), "restarted %016llx (pid %llu)",
             (unsigned long long)tid, (unsigned long long)pid);
    mcp_reply_text(call, msg);
}

void process_mcp_register(void) {
    mcp_server_register_tool(&kToolProcessStatus,  tool_process_status);
    mcp_server_register_tool(&kToolProcessStart,   tool_process_start);
    mcp_server_register_tool(&kToolProcessStop,    tool_process_stop);
    mcp_server_register_tool(&kToolProcessRestart, tool_process_restart);
}
