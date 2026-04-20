#pragma once

// Resolves sandboxed iOS paths for save games, DLC, media, and user data.
// All returned paths are UTF-8 encoded, null-terminated, and owned by the
// callee (do not free). Paths persist for the lifetime of the process.

#ifdef __cplusplus
extern "C" {
#endif

// Writable documents directory (shown to users in the Files app).
// Used for save games.
const char* ios_documents_dir(void);

// Writable application-support directory (hidden from Files app).
// Used for engine state, config, uid.dat, username.txt.
const char* ios_app_support_dir(void);

// Writable caches directory. May be purged by the OS under disk pressure.
// Used for things we can regenerate (texture caches, etc).
const char* ios_caches_dir(void);

// Read-only main bundle resources directory.
// Used for Common/Media/*.arc and other shipped assets.
const char* ios_bundle_resources_dir(void);

// Writable path inside app-support where the LCE 'GameHDD' tree lives.
// Created on first access if missing.
const char* ios_gamehdd_dir(void);

#ifdef __cplusplus
}
#endif
