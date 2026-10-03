#include "features/crash/crash_mcp.h"
#include "core/request.h"
#include "features/crash/crash.h"
#include "features/crash/crash_tools.h"
#include "features/mcp/mcp_server.h"

#include <stdio.h>

static void tool_list_crash_reports(McpCall *call) {
    int limit = mcp_arg_int(call, "limit", CRASH_DEFAULT_LIMIT);
    if (limit < 1 || limit > CRASH_MAX_REPORTS) {
        mcp_reply_error(call, "invalid 'limit' (1-32)");
        return;
    }
    enum { LINE_SIZE = 256, TEXT_SIZE = CRASH_MAX_REPORTS * LINE_SIZE + 128 };
    CrashReport *reports = request_alloc(call->req, sizeof(*reports) * CRASH_MAX_REPORTS);
    char *text = request_alloc(call->req, TEXT_SIZE);
    if (!reports || !text) {
        mcp_reply_rpc_error(call, -32603, "out of request memory");
        return;
    }
    int n = crash_list(reports, limit);
    if (n == 0) {
        mcp_reply_text(call, "no crash or fatal reports in /atmosphere");
        return;
    }

    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        const CrashReport *r = &reports[i];
        char when[40];
        crash_format_time(r->timestamp, when, sizeof(when));
        pos += (size_t)snprintf(text + pos, TEXT_SIZE - pos,
                                "%s  %s  %016llx%s%s%s  %s  %s%s\n", when,
                                r->fatal ? "fatal" : "crash", (unsigned long long)r->program_id,
                                r->process[0] ? " (" : "", r->process, r->process[0] ? ")" : "",
                                r->result[0] ? r->result : "result unknown", r->path,
                                r->has_screenshot ? "  [+ .jpg screenshot]" : "");
    }
    snprintf(text + pos, TEXT_SIZE - pos,
             "Newest first. read_file a report for the registers and stack trace.");
    mcp_reply_text(call, text);
}

void crash_mcp_register(void) {
    mcp_server_register_tool(&kToolListCrashReports, tool_list_crash_reports);
}
