#include "features/process/process_mcp.h"
#include "core/request.h"
#include "features/mcp/mcp_server.h"
#include "features/process/process.h"
#include "features/process/process_tools.h"

#include <stdio.h>
#include <string.h>

// Shared by the tools that take a title id. Replies with its own error and returns false.
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

static void tool_process_list(McpCall *call) {
    if (!process_available()) {
        mcp_reply_error(call, "process control unavailable");
        return;
    }
    // One "<pid>  <titleId>" line per process, under 48 bytes each.
    enum { LINE_SIZE = 48, TEXT_SIZE = PROCESS_LIST_MAX * LINE_SIZE + 32 };
    ProcessEntry *procs = request_alloc(call->req, sizeof(*procs) * PROCESS_LIST_MAX);
    char *text = request_alloc(call->req, TEXT_SIZE);
    if (!procs || !text) {
        mcp_reply_rpc_error(call, -32603, "out of request memory");
        return;
    }

    uint32_t rc = 0;
    int count = process_list(procs, PROCESS_LIST_MAX, &rc);
    if (count < 0) {
        char msg[80];
        snprintf(msg, sizeof(msg), "failed to list processes (rc 0x%08x)", rc);
        mcp_reply_error(call, msg);
        return;
    }

    size_t pos = 0;
    text[0] = '\0';
    for (int i = 0; i < count && pos < TEXT_SIZE - LINE_SIZE; i++) {
        const char *sep = i ? "\n" : "";
        if (procs[i].has_program_id)
            pos += (size_t)snprintf(text + pos, TEXT_SIZE - pos, "%spid %-6llu %016llx", sep,
                                    (unsigned long long)procs[i].pid,
                                    (unsigned long long)procs[i].program_id);
        else
            pos += (size_t)snprintf(text + pos, TEXT_SIZE - pos, "%spid %-6llu (no title id)",
                                    sep, (unsigned long long)procs[i].pid);
    }
    mcp_reply_text(call, text);
}

static void tool_process_start(McpCall *call) {
    uint64_t tid;
    if (!arg_title_id(call, &tid))
        return;

    uint64_t pid = 0;
    uint32_t rc = 0;
    char msg[160];
    if (!process_start(tid, &pid, &rc)) {
        if (rc == PROCESS_RC_ALREADY_RUNNING)
            snprintf(msg, sizeof(msg), "%016llx is already running (pid %llu)",
                     (unsigned long long)tid, (unsigned long long)pid);
        else
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
        if (rc == PROCESS_RC_NOT_RUNNING)
            snprintf(msg, sizeof(msg), "%016llx is not running", (unsigned long long)tid);
        else
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

static void tool_wait_for_process(McpCall *call) {
    uint64_t tid;
    if (!arg_title_id(call, &tid))
        return;
    char state[16] = "running";
    mcp_arg_string(call, "state", state, sizeof(state));
    bool want_running = strcmp(state, "running") == 0;
    if (!want_running && strcmp(state, "stopped") != 0) {
        mcp_reply_error(call, "invalid 'state' (running or stopped)");
        return;
    }
    int timeout = mcp_arg_int(call, "timeoutMs", PROCESS_WAIT_DEFAULT_TIMEOUT_MS);

    ProcessStatus st;
    int elapsed = 0;
    char msg[160];
    if (process_wait(tid, want_running, timeout, &st, &elapsed)) {
        if (want_running)
            snprintf(msg, sizeof(msg), "%016llx is running (pid %llu) after %d ms",
                     (unsigned long long)tid, (unsigned long long)st.pid, elapsed);
        else
            snprintf(msg, sizeof(msg), "%016llx is not running after %d ms",
                     (unsigned long long)tid, elapsed);
    } else {
        snprintf(msg, sizeof(msg), "timed out after %d ms: %016llx is %s", elapsed,
                 (unsigned long long)tid, st.running ? "still running" : "still not running");
    }
    mcp_reply_text(call, msg);
}

void process_mcp_register(void) {
    mcp_server_register_tool(&kToolProcessStatus,  tool_process_status);
    mcp_server_register_tool(&kToolProcessList,    tool_process_list);
    mcp_server_register_tool(&kToolProcessStart,   tool_process_start);
    mcp_server_register_tool(&kToolProcessStop,    tool_process_stop);
    mcp_server_register_tool(&kToolProcessRestart, tool_process_restart);
    mcp_server_register_tool(&kToolWaitForProcess, tool_wait_for_process);
}
