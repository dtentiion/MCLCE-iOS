#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Rescans Documents/saves. Returns the number of saves, newest first.
int         mcle_saves_refresh(void);
int         mcle_saves_count(void);
const char *mcle_saves_folder(int i);   // folder under saves/, used as the level id
const char *mcle_saves_name(int i);     // display name
const char *mcle_saves_seed(int i);     // "" when the thumbnail has no seed
int         mcle_saves_host_options(int i, unsigned int *out);  // 0 if not stored

#ifdef __cplusplus
}
#endif
