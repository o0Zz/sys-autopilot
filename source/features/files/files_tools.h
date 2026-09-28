// Generated from files_tools.json by scripts/generate_resource.py -- do not edit by hand.
#pragma once

#include "features/mcp/mcp_server.h"

static const McpToolDef kToolListDirectory = {
    "{\"name\":\"list_directory\",\"description\":\"List a directory on the SD card. Returns JSON with name/type"
    "/size/mtime per entry.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"descri"
    "ption\":\"Absolute path on the SD card, e.g. /switch/myapp/log.txt\"}},\"required\":[\"path\"]}}"
};

static const McpToolDef kToolReadFile = {
    "{\"name\":\"read_file\",\"description\":\"Read a text file from the SD card (logs, configs). Returns up to "
    "32 KB per call; use offset/length to page through larger files. A negative offset reads from the end"
    " of the file (tail).\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"descript"
    "ion\":\"Absolute path on the SD card, e.g. /switch/myapp/log.txt\"},\"offset\":{\"type\":\"integer\",\"descrip"
    "tion\":\"Byte offset; negative = from end of file.\"},\"length\":{\"type\":\"integer\",\"description\":\"Max byt"
    "es to read (capped at 32768).\"}},\"required\":[\"path\"]}}"
};

static const McpToolDef kToolUploadFile = {
    "{\"name\":\"upload_file\",\"description\":\"Write a file to the SD card. content is base64-encoded and is s"
    "treamed to disk, so size is limited only by SD space - but large binaries are cheaper to deploy via "
    "the raw HTTP API (curl -T file 'http://<ip>:<port>/files?path=...').\",\"inputSchema\":{\"type\":\"object\""
    ",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path on the SD card, e.g. /switch/mya"
    "pp/log.txt\"},\"content\":{\"type\":\"string\",\"description\":\"Base64-encoded file content.\"}},\"required\":[\""
    "path\",\"content\"]}}"
};

static const McpToolDef kToolMoveFile = {
    "{\"name\":\"move_file\",\"description\":\"Rename or move a file or directory on the SD card. Parent directo"
    "ries of the destination are created; an existing destination is never overwritten.\",\"inputSchema\":{\""
    "type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path on the SD card, e."
    "g. /switch/myapp/log.txt\"},\"to\":{\"type\":\"string\",\"description\":\"Absolute destination path on the SD "
    "card, e.g. /switch/myapp/log.old.txt\"}},\"required\":[\"path\",\"to\"]}}"
};

static const McpToolDef kToolDeleteFile = {
    "{\"name\":\"delete_file\",\"description\":\"Delete a file or empty directory on the SD card.\",\"inputSchema\""
    ":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path on the SD card,"
    " e.g. /switch/myapp/log.txt\"}},\"required\":[\"path\"]}}"
};

static const McpToolDef kToolHashFile = {
    "{\"name\":\"hash_file\",\"description\":\"Compute the SHA-256 of a file on the SD card. The file is hashed "
    "in a streaming fashion (constant memory, any size), and only the 64-char hex digest is returned - no"
    "t the file contents. Use this to verify an upload landed intact: pass the SHA-256 you expected as 'e"
    "xpected' and the result reports matched:true/false. Returns JSON: {algorithm, hash, size[, matched]}"
    ".\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path"
    " on the SD card, e.g. /switch/myapp/log.txt\"},\"expected\":{\"type\":\"string\",\"description\":\"Optional ex"
    "pected SHA-256 hex digest. When provided, the result includes a 'matched' boolean (case-insensitive "
    "comparison).\"}},\"required\":[\"path\"]}}"
};
