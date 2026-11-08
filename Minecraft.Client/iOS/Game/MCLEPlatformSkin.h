#pragma once
#include <vector>

// skinHDOrbis.swf (or skinOrbis.swf) with its title logo and other non-button
// bitmaps swapped for the Windows64 skin's. False when either won't parse.
bool mcle_merge_platform_skin(const std::vector<unsigned char>& orbis,
                              const std::vector<unsigned char>& win,
                              std::vector<unsigned char>& out);
