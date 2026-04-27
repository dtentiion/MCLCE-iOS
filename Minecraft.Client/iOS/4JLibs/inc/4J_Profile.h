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

// Opaque player identity stub. Other platforms back this with platform-
// specific identity (Sony NP, Xbox Live, etc). For iOS we have no online
// identity yet, so PlayerUID is a fixed-size byte buffer that satisfies
// the type usage in DataInput / DataOutput / Connection / save formats.
// Layout chosen to be wide enough to hold any platform's identity at
// the size LCE expects (16 bytes covers Xbox XUID, PS NP id digest, etc).
#ifdef __cplusplus
struct PlayerUID {
    uint8_t bytes[16];

    PlayerUID() { for (int i = 0; i < 16; ++i) bytes[i] = 0; }
    PlayerUID(uint64_t v) {
        // Pack the 64-bit value into the leading 8 bytes; remaining 8 are
        // zeroed. INVALID_XUID = 0xFFFFFFFFFFFFFFFFu maps to all-FF in
        // the leading bytes which doubles as a sentinel.
        for (int i = 0; i < 16; ++i) bytes[i] = 0;
        for (int i = 0; i < 8; ++i) bytes[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
    PlayerUID& operator=(uint64_t v) { *this = PlayerUID(v); return *this; }
    bool operator==(const PlayerUID& o) const {
        for (int i = 0; i < 16; ++i) if (bytes[i] != o.bytes[i]) return false;
        return true;
    }
    bool operator!=(const PlayerUID& o) const { return !(*this == o); }
    // 4J Stu: Sony platforms expose toString for debug-print paths
    // (DirectoryLevelStorage prints player UIDs while clearing old data).
    // Render as a 32-char hex string so the log line is human-readable.
    std::wstring toString() const {
        wchar_t buf[33];
        const wchar_t *hex = L"0123456789abcdef";
        for (int i = 0; i < 16; ++i) {
            buf[2*i]     = hex[(bytes[i] >> 4) & 0xF];
            buf[2*i + 1] = hex[bytes[i] & 0xF];
        }
        buf[32] = 0;
        return std::wstring(buf);
    }
};
typedef PlayerUID* PPlayerUID;

// Stream-extract for the StringHelpers.h _fromString<PlayerUID> path. Reads
// the same 32-char hex form that toString() emits; tolerates the empty
// string case by leaving the UID zeroed.
inline std::wistream& operator>>(std::wistream& in, PlayerUID& out) {
    std::wstring s;
    in >> s;
    for (int i = 0; i < 16; ++i) out.bytes[i] = 0;
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
        if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
        return -1;
    };
    for (size_t i = 0; i + 1 < s.size() && i / 2 < 16; i += 2) {
        int hi = hex(s[i]), lo = hex(s[i + 1]);
        if (hi < 0 || lo < 0) break;
        out.bytes[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    return in;
}

// Hash specialization so unordered_map<PlayerUID, ...> works on iOS.
// (Other consoles use PlayerUID::Hash; the iOS branch falls through to
// std::hash<PlayerUID> in DirectoryLevelStorage's m_playerMappings.)
namespace std {
    template<> struct hash<PlayerUID> {
        size_t operator()(const PlayerUID &k) const noexcept {
            size_t h = 0;
            for (int i = 0; i < 16; ++i) h = (h * 131) + k.bytes[i];
            return h;
        }
    };
}

// 4J profile / pad-routing manager. Other platforms expose a real
// C_4JProfile that owns per-pad user identity (Xbox Live signin, NP
// authoritative user, etc). On iOS we have one local user; the stub
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
    template<class... A> const wchar_t* GetGamertag(A...) { return L"iOSPlayer"; }
    template<class... A> int  GetUserIndexForPad(A...)    { return 0; }
    template<class... A> int  GetPadForUserIndex(A...)    { return 0; }
    template<class... A> bool IsFullVersion(A...)         { return true; }
    template<class... A> int  AreXUIDSEqual(A...)         { return 0; }
    template<class... A> bool HasGoldMembership(A...)     { return false; }
};

// Real 4J_Profile.h (Durango/Orbis) defines `extern C_4JProfile
// ProfileManager;` here. Mirror that so upstream code referencing the
// global resolves. The actual instance is provided by a single TU
// (Minecraft.Client/iOS/Profile/ProfileManager.cpp or similar).
extern C_4JProfile ProfileManager;
#endif
