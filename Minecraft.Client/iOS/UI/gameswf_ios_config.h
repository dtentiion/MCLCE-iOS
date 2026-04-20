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

// Drop jpeg support for the first probe pass. jpeglib/ sources can be folded
// in later; without them the bundled jpeg.cpp can't find jpeglib.h.
#ifndef TU_CONFIG_LINK_TO_JPEGLIB
#  define TU_CONFIG_LINK_TO_JPEGLIB 0
#endif

// zlib ships with iOS as libz. Will link against it in the final library
// target when we start handling compressed SWFs.
#ifndef TU_CONFIG_LINK_TO_ZLIB
#  define TU_CONFIG_LINK_TO_ZLIB 1
#endif

// Enable GameSWF's IF_VERBOSE_PARSE / IF_VERBOSE_DEBUG macros so we can see
// what the parser actually does with a SWF. Routed to NSLog through a
// trace callback installed at init.
#ifndef TU_CONFIG_VERBOSE
#  define TU_CONFIG_VERBOSE 1
#endif

// libpng is a bigger lift on iOS. For the first probe, skip PNG support so
// we can see the rest of the library compile. Bring it back in a later pass.
#ifndef TU_CONFIG_LINK_TO_LIBPNG
#  define TU_CONFIG_LINK_TO_LIBPNG 0
#endif
