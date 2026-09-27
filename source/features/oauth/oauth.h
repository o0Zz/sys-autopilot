#pragma once

#include "core/config.h"

// Minimal OAuth 2.1 authorization server + resource server glue for the MCP
// browser auth flow:
//   - RFC 9728 protected resource metadata (/.well-known/oauth-protected-resource)
//   - RFC 8414 AS metadata (/.well-known/oauth-authorization-server)
//   - RFC 7591 dynamic client registration (POST /oauth/register)
//   - Authorization endpoint with an HTML login form (GET/POST /oauth/authorize)
//   - Token endpoint with PKCE S256 (POST /oauth/token)
//
// Issued bearer tokens are non-expiring and persisted one-per-line to
// OAUTH_TOKENS_PATH; revoke by deleting a line. The browser flow requires
// username+password to be configured.

#ifndef OAUTH_TOKENS_PATH
#define OAUTH_TOKENS_PATH CONFIG_DIR "/tokens.txt"
#endif

// Stores the config reference and loads the persisted token list.
void oauth_init(const Config *cfg);

// True if `token` matches an issued token (constant-time per entry). Reloads
// the tokens file when its mtime changes, so edits apply without a reboot.
bool oauth_token_valid(const char *token);

// Mints a fresh bearer token and persists it to OAUTH_TOKENS_PATH with the
// given note in the comment. out must hold at least 65 chars. Used by the
// OAuth token endpoint and by the create_token MCP tool.
bool oauth_mint_token(char *out, size_t outsz, const char *note);

// Removes a token: deletes its line from OAUTH_TOKENS_PATH and reloads the
// store. Returns false when the token is not in the store (e.g. the static
// config token, which lives in config.ini instead).
bool oauth_revoke_token(const char *token);

// Registers the OAuth routes, marks them public, installs oauth_token_valid
// as the server's extra bearer validator and advertises "auth=oauth" over
// mDNS when username + password are configured. Call after oauth_init().
void oauth_http_register(void);
