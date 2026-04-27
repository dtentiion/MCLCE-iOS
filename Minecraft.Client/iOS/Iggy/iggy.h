// iOS shim for the Iggy SWF runtime API surface.
//
// On Windows64 / Xbox / PS3 / Orbis / Vita, upstream gameplay headers
// reach into the RAD Iggy headers (Windows64/Iggy/include/iggy.h etc)
// for SWF playback. The iOS port replaces Iggy with the Ruffle-based
// SWF runtime in third_party/ruffle_ios/, so the upstream Iggy types
// only need to satisfy declarations - no runtime calls land here.
//
// Every name below mirrors the upstream Iggy header by shape, not by
// implementation. Callers that try to invoke an Iggy entry point on
// iOS will hit a no-op (or never reach this code at all, since the
// SWF playback path is owned by Ruffle).
//
// Keep this file pointer-only where possible: forward-declare opaque
// structs whose size never has to be known at compile time. The few
// concrete types here (IggyName, IggyBitmapFontProvider, font / glyph
// metrics) are members in upstream UI classes and have to be
// concrete for those classes to have a size.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// RAD scalar typedefs that show up in upstream Iggy callback signatures.
typedef int8_t   S8;
typedef uint8_t  U8;
typedef int16_t  S16;
typedef uint16_t U16;
typedef int32_t  S32;
typedef uint32_t U32;
typedef int64_t  S64;
typedef uint64_t U64;
typedef float    F32;
typedef double   F64;
typedef int      rrbool;

// RAD calling-convention markers. Iggy headers attach these to public
// entry points; on iOS we don't care about calling convention so they
// expand to nothing.
#ifndef RADLINK
#  define RADLINK
#endif
#ifndef RADEXPLINK
#  define RADEXPLINK
#endif
#ifndef RADEXPFUNC
#  define RADEXPFUNC extern
#endif
#ifndef RADDEFFUNC
#  define RADDEFFUNC
#endif

// Iggy uses UTF-16 strings on every platform. iOS wchar_t is 32-bit so
// we can't typedef IggyUTF16 to wchar_t - use unsigned short to match
// the upstream Iggy ABI.
typedef unsigned short IggyUTF16;

// IggyName is a value-type aggregate. Upstream defines it as
// `struct { U32 idx; }` but for the probe target we only need an
// integer-shaped type that hashes for free in std::unordered_map.
// This avoids needing a `std::hash<IggyName>` specialization for
// the `unordered_map<wstring, IggyName>` member in UIScene.h.
typedef S32 IggyName;

// Forward-declared opaque types. Upstream UI headers reference these
// only as pointers, so a forward decl is enough to compile.
struct Iggy;
struct IggyMemoryUseInfo;
struct IggyCustomDrawCallbackRegion;
struct IggyExternalFunctionCallUTF16;
struct IggyTextfieldInfo;
struct IggyVectorShape;
struct GDrawTexture;
struct IggyStringUTF16;
struct IggyStringUTF8;
struct IggyKey;
struct IggyEvent;
struct IggyMouseEvent;
struct IggyPlatformExternalFunctionCallback;

// IggyValuePath is held BY VALUE as a member (`IggyValuePath m_iggyPath;`)
// in upstream UIControl.h, so it needs a complete type with a non-zero
// size. Real upstream IggyValuePath is a struct holding a pointer chain
// and an Iggy*. The probe never traverses the path so dummy body is
// fine; the size just has to be reasonable so member layout works.
typedef struct IggyValuePath {
    void *iggy;
    void *root;
    int   path_token;
} IggyValuePath;

// Iggy macro upstream UIScene.h uses at file scope. Real version
// returns an IggyValuePath* pointing at the SWF root. The probe
// never invokes the path so the null cast is fine.
#ifndef IggyPlayerRootPath
#  define IggyPlayerRootPath(...) ((struct IggyValuePath*)0)
#endif

// Concrete font / glyph metrics types referenced by value in upstream
// UIBitmapFont.h's IggyBitmapFontProvider callbacks. The probe target
// never invokes the callbacks, so opaque dummy bodies suffice; their
// size just has to be non-zero so member-pointer arithmetic compiles.
typedef struct IggyFontMetrics       { int dummy; } IggyFontMetrics;
typedef struct IggyGlyphMetrics      { int dummy; } IggyGlyphMetrics;
typedef struct IggyBitmapCharacter   { int dummy; } IggyBitmapCharacter;

// Function-pointer typedefs for the IggyBitmapFontProvider struct.
// Layout matches Windows64/Iggy/include/iggy.h:500-515 by shape.
typedef IggyFontMetrics*    (RADLINK *IggyFontGetFontMetrics)(void *ud, IggyFontMetrics *out);
typedef S32                 (RADLINK *IggyFontGetCodepointGlyph)(void *ud, U32 codepoint);
typedef IggyGlyphMetrics*   (RADLINK *IggyFontGetGlyphMetrics)(void *ud, S32 glyph, IggyGlyphMetrics *out);
typedef rrbool              (RADLINK *IggyFontIsGlyphEmpty)(void *ud, S32 glyph);
typedef F32                 (RADLINK *IggyFontGetKerningForGlyphPair)(void *ud, S32 a, S32 b);
typedef rrbool              (RADLINK *IggyBitmapFontCanProvideBitmap)(void *ud, S32 glyph, F32 pixel_scale);
typedef rrbool              (RADLINK *IggyBitmapFontGetGlyphBitmap)(void *ud, S32 glyph, F32 pixel_scale, IggyBitmapCharacter *out);
typedef void                (RADLINK *IggyBitmapFontFreeGlyphBitmap)(void *ud, S32 glyph, F32 pixel_scale, IggyBitmapCharacter *out);

// IggyBitmapFontProvider is held by value as a member of UIAbstractBitmapFont
// in upstream UIBitmapFont.h. Layout mirrors the upstream Iggy header.
typedef struct IggyBitmapFontProvider {
    IggyFontGetFontMetrics          get_font_metrics;
    IggyFontGetCodepointGlyph       get_glyph_for_codepoint;
    IggyFontGetGlyphMetrics         get_glyph_metrics;
    IggyFontIsGlyphEmpty            is_empty;
    IggyFontGetKerningForGlyphPair  get_kerning;
    IggyBitmapFontCanProvideBitmap  can_bitmap;
    IggyBitmapFontGetGlyphBitmap    get_bitmap;
    IggyBitmapFontFreeGlyphBitmap   free_bitmap;
    S32                             num_glyphs;
    void                           *userdata;
} IggyBitmapFontProvider;

#ifdef __cplusplus
} // extern "C"
#endif
