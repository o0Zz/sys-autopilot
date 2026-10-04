#pragma once

#include <stdbool.h>
#include <stddef.h>

// SD card file operations shared by the REST and MCP front-ends. No HTTP here.

// Filesystem root prefix; overridable for host-side tests.
#ifndef FILES_ROOT
#define FILES_ROOT "sdmc:"
#endif

// Size of the read/write buffer the front-ends take from request memory.
#define FILES_IO_BUF_SIZE 0x8000

// PUT /files streams into "<path>" FILES_UPLOAD_SUFFIX and renames it over
// the target once complete.
#define FILES_UPLOAD_SUFFIX ".upload"

// Resolves a user path ("/switch/foo") into FILES_ROOT-prefixed fspath.
// Rejects relative paths and ".." traversal; *err receives a message.
bool files_resolve(const char *userpath, char *out, size_t outsz, const char **err);

// Creates all parent directories of fspath.
void files_mkdirs_for(const char *fspath);

// Strips a trailing '/' from fspath, except the root's own ("sdmc:/").
// Returns true when one was stripped.
bool files_trim_slash(char *fspath);

// Clamps a read window to a file of fsize bytes. A negative *offset counts
// from the end (tail); a negative *length means "to the end".
void files_clamp_range(long long fsize, long long *offset, long long *length);

// Writes the JSON directory listing of fspath into buf (NUL-terminated) and
// its length into *out_len. Returns false with *err set when the directory
// cannot be opened or the listing does not fit; *out_len is then 0.
bool files_build_listing(const char *fspath, const char *userpath, char *buf, size_t bufsz,
                         size_t *out_len, const char **err);

// Deletes a file or empty directory. Returns true on success, *err on failure.
bool files_delete_path(const char *fspath, const char **err);

// Renames/moves a file or directory (creating dst's parent directories).
// Refuses to overwrite an existing destination. Returns true on success,
// *err on failure.
bool files_move_path(const char *src, const char *dst, const char **err);

// --- waiting for a file ----------------------------------------------------------

#define FILES_WAIT_DEFAULT_TIMEOUT_MS 10000
#define FILES_WAIT_MAX_NEEDLE 256

typedef struct {
    bool met;       // false: timed out
    bool exists;    // the file existed at the last check
    int elapsed_ms;
    long long size; // its size then
} FilesWait;

// Waits (at most HTTP_MAX_WAIT_MS) until fspath exists and, when `contains`
// is not empty, holds that text. With new_only, only text added after the
// wait began counts: the way to wait for a fresh line in a log that already
// holds older runs. A file that shrinks is taken as rewritten and searched
// again from its start. buf is scratch for reading (FILES_IO_BUF_SIZE bytes is
// plenty).
void files_wait(const char *fspath, const char *contains, bool new_only, int timeout_ms,
                char *buf, size_t bufsz, FilesWait *res);

// Computes the SHA-256 of a regular file, streamed through the caller's
// buffer (so memory use is constant regardless of file size). On success
// writes a 64-char lowercase hex string (NUL-terminated) into out_hex and the
// byte count into *out_size, and returns true. On failure returns false with
// *err set.
bool files_hash_sha256(const char *fspath, void *buf, size_t buf_size,
                       char out_hex[65], long long *out_size, const char **err);
