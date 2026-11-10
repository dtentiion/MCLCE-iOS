#pragma once

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

// UTF-8 text for an IDS_* id from Windows64Media/strings.h, looked up in
// Documents/languages.loc. Returns "" if the table or id is missing.
// The pointer stays valid for the life of the app.
const char *mcle_string(int id);

// The player's name: the first line of Documents/username.txt, "Player"
// without one
const wchar_t *mcle_username(void);

#ifdef __cplusplus
}
#endif
