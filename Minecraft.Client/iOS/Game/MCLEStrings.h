#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// UTF-8 text for an IDS_* id from Windows64Media/strings.h, looked up in
// Documents/languages.loc. Returns "" if the table or id is missing.
// The pointer stays valid for the life of the app.
const char *mcle_string(int id);

#ifdef __cplusplus
}
#endif
