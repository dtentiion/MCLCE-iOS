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
#include <string>
#include <istream>
#include <cwchar>

extern "C" const wchar_t *mcle_username(void); // Game/MCLEStrings.cpp

// The player's id is what the Windows64 build has (extraX64.h): a 64 bit
// number, written as a long and naming the player's file in decimal.
#ifdef __cplusplus
typedef ULONGLONG PlayerUID;
typedef PlayerUID* PPlayerUID;

// 4J profile / pad-routing manager. Other platforms expose a real
// C_4JProfile that owns per-pad user identity (Xbox Live signin, NP
// authoritative user, etc). On iOS there's one local user; the stub
// provides whatever upstream calls touch via variadic methods.
class C_4JProfile {
public:
    template<class... A> int  GetPrimaryPad(A...)         { return 0; }
    template<class... A> int  GetSignedInUsers(A...)      { return 1; }
    template<class... A> bool IsSignedIn(A...)            { return true; }
    template<class... A> bool IsHostPlayer(A...)          { return true; }
    template<class... A> bool HasOnlineAccess(A...)       { return false; }
    template<class... A> bool HasSocialAccess(A...)       { return false; }
    template<class... A> uint64_t GetXuid(A...)           { return 0; }
    // Upstream Minecraft.cpp calls profile.GetXUID(...) (uppercase ID).
    template<class... A> uint64_t GetXUID(A...)           { return 0; }
    template<class... A> bool IsGuest(A...)               { return false; }
    template<class... A> const wchar_t* GetDisplayName(A...) { return mcle_username(); }
    template<class... A> bool IsSignedInLive(A...)        { return false; }
    template<class... A> bool IsSystemUIDisplayed(A...)   { return false; }
    template<class... A> const wchar_t* GetGamertag(A...) { return mcle_username(); }
    template<class... A> int  GetUserIndexForPad(A...)    { return 0; }
    template<class... A> int  GetPadForUserIndex(A...)    { return 0; }
    template<class... A> bool IsFullVersion(A...)         { return true; }
    template<class... A> int  AreXUIDSEqual(A...)         { return 0; }
    template<class... A> bool HasGoldMembership(A...)     { return false; }
    template<class... A> void SetCurrentGameActivity(A...) {}
};

// Real 4J_Profile.h (Durango/Orbis) defines `extern C_4JProfile
// ProfileManager;` here. Mirror that so upstream code referencing the
// global resolves. The actual instance is provided by a single TU
// (Minecraft.Client/iOS/Profile/ProfileManager.cpp or similar).
extern C_4JProfile ProfileManager;
#endif
