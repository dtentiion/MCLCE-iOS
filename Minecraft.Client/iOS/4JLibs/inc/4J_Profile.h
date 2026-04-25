// iOS stub for the platform 4J profile / NP layer. The other consoles use
// this header to surface platform-specific user / award / online identity
// types into upstream gameplay code. Until iOS social and profile work
// lands, every type in here is empty or reduced to the smallest possible
// representation that lets upstream compile.
//
// Expand as the world-probe surfaces missing names. Add real iOS-side
// implementations under Minecraft.Client/iOS/Profile/ when they show up.

#pragma once

#include "iOS_WinCompat.h"

// Opaque player identity stub. Other platforms back this with platform-
// specific identity (Sony NP, Xbox Live, etc). For iOS we have no online
// identity yet, so PlayerUID is a fixed-size byte buffer that satisfies
// the type usage in DataInput / DataOutput / Connection / save formats.
// Layout chosen to be wide enough to hold any platform's identity at
// the size LCE expects (16 bytes covers Xbox XUID, PS NP id digest, etc).
#ifdef __cplusplus
struct PlayerUID {
    uint8_t bytes[16];
};
typedef PlayerUID* PPlayerUID;
#endif
