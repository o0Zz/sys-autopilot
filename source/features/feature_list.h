#pragma once

#include "core/config.h"

// The list of features compiled into this build, in one place (feature_list.c).
// Which optional features are in is chosen by the Makefile (FEATURES, MCP).

// Opens the system services the features need. Call from __appInit while the
// sm session is open. Best-effort: a missing service only makes that
// feature's endpoints report unavailability.
void features_init(void);

// Registers every feature's HTTP routes and MCP tools. Call once, before the
// server runs.
void features_register(const Config *cfg);

// Closes what features_init opened.
void features_exit(void);
