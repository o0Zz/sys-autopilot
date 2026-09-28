#include "features/titles/titles_mcp.h"
#include "core/request.h"
#include "features/mcp/mcp_server.h"
#include "features/titles/titles.h"
#include "features/titles/titles_tools.h"

#include <stdio.h>

static void tool_list_installed_titles(McpCall *call) {
    enum { TEXT_SIZE = TITLES_MAX * 128 + 64 };
    TitleInfo *titles = request_alloc(call->req, sizeof(*titles) * TITLES_MAX);
    void *work = request_alloc(call->req, titles_work_size());
    char *text = request_alloc(call->req, TEXT_SIZE);
    if (!titles || !work || !text) {
        mcp_reply_rpc_error(call, -32603, "out of request memory");
        return;
    }
    int count = 0;
    titles_list(titles, TITLES_MAX, &count, work);
    size_t pos = 0;
    text[0] = '\0';
    if (count == 0)
        pos += (size_t)snprintf(text, TEXT_SIZE, "(no titles installed)");
    for (int i = 0; i < count && pos < TEXT_SIZE - 128; i++) {
        const char *storage =
            titles[i].storage_id == 5 ? "sd" :
            titles[i].storage_id == 4 ? "nand" :
            titles[i].storage_id == 2 ? "gamecard" : "other";
        pos += (size_t)snprintf(text + pos, TEXT_SIZE - pos,
            "%s%016llx  v%-6u  %-8s  %s", i ? "\n" : "",
            (unsigned long long)titles[i].title_id, titles[i].version, storage,
            titles[i].name[0] ? titles[i].name : "(name unavailable)");
    }
    mcp_reply_text(call, text);
}

void titles_mcp_register(void) {
    mcp_server_register_tool(&kToolListInstalledTitles, tool_list_installed_titles);
}
