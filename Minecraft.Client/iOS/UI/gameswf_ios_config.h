// GameSWF build-time config overrides for iOS.
//
// Force-included (via -include) before any GameSWF translation unit. Turns
// off the Marmalade-port assumptions (OpenGL ES + OpenAL) so the library
// compiles for iOS cleanly and leaves the actual rendering to our Metal
// backend. Audio is deliberately disabled for now; it comes back once a
// Phase-5 audio layer exists.

#pragma once

// Thread support. 0 = no threads; 1 = TU's own threading; 2 = Marmalade.
// We run GameSWF from the main thread, so disable threading entirely.
#undef TU_CONFIG_LINK_TO_THREAD
#define TU_CONFIG_LINK_TO_THREAD 0

// Kill the Marmalade OGL ES shortcut. Our render_handler is Metal-backed.
#ifdef TU_USE_OGLES
#  undef TU_USE_OGLES
#endif

// Kill OpenAL. No audio in the probe; wire via AVAudioEngine later.
#ifdef TU_USE_OPENAL
#  undef TU_USE_OPENAL
#endif

// Keep jpeg (bundled inside GameSWF already) and zlib (iOS ships libz).
#ifndef TU_CONFIG_LINK_TO_JPEGLIB
#  define TU_CONFIG_LINK_TO_JPEGLIB 1
#endif
#ifndef TU_CONFIG_LINK_TO_ZLIB
#  define TU_CONFIG_LINK_TO_ZLIB 1
#endif

// libpng is a bigger lift on iOS. For the first probe, skip PNG support so
// we can see the rest of the library compile. Bring it back in a later pass.
#ifndef TU_CONFIG_LINK_TO_LIBPNG
#  define TU_CONFIG_LINK_TO_LIBPNG 0
#endif
