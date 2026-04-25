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
