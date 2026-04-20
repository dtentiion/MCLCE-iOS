# Probe target for compiling GameSWF core for iOS ARM64.
#
# Flip on with -DENABLE_GAMESWF_PROBE=ON. Expected to fail on the first few
# passes, just like the world probe. The errors tell us which Marmalade /
# Windows assumptions remain in GameSWF and guide the iOS port of the
# library.

set(GAMESWF_ROOT "${THIRD_PARTY_DIR}/gameswf/GameSwfPort/GameSwf")

# Base library. Skipped: ogl.cpp + test_ogl.cpp (OpenGL on desktop, we use
# Metal), Stackwalker.cpp (Windows debug tool), tu_file_SDL.cpp (SDL dep),
# png_helper.cpp (libpng disabled for now in gameswf_ios_config.h), demo.cpp
# and test_*.cpp (examples).
set(GAMESWF_PROBE_SOURCES
  ${GAMESWF_ROOT}/base/configvars.cpp
  ${GAMESWF_ROOT}/base/container.cpp
  ${GAMESWF_ROOT}/base/dlmalloc.c
  ${GAMESWF_ROOT}/base/ear_clip_triangulate_float.cpp
  ${GAMESWF_ROOT}/base/ear_clip_triangulate_sint16.cpp
  ${GAMESWF_ROOT}/base/file_util.cpp
  ${GAMESWF_ROOT}/base/image.cpp
  ${GAMESWF_ROOT}/base/image_filters.cpp
  # jpeg.cpp needs bundled libjpeg wired in; disabled in this pass.
  # TODO: enable once jpeglib/ sources are added to the target.
  ${GAMESWF_ROOT}/base/logger.cpp
  ${GAMESWF_ROOT}/base/membuf.cpp
  ${GAMESWF_ROOT}/base/postscript.cpp
  ${GAMESWF_ROOT}/base/triangulate_float.cpp
  ${GAMESWF_ROOT}/base/triangulate_sint32.cpp
  ${GAMESWF_ROOT}/base/tu_file.cpp
  ${GAMESWF_ROOT}/base/tu_gc_singlethreaded_marksweep.cpp
  ${GAMESWF_ROOT}/base/tu_loadlib.cpp
  ${GAMESWF_ROOT}/base/tu_random.cpp
  ${GAMESWF_ROOT}/base/tu_timer.cpp
  ${GAMESWF_ROOT}/base/tu_types.cpp
  ${GAMESWF_ROOT}/base/utf8.cpp
  ${GAMESWF_ROOT}/base/utility.cpp
)

# Player library. Picked up via glob after excluding platform-specific ones.
# Skipped: render_handler_{d3d,ogl,ogles,iwgl,xbox}.cpp (backend-specific, we
# bring our own Metal render_handler), sound_handler_{sdl,openal}.cpp (we
# wire an AVFoundation handler later), freetype.cpp (needs FreeType; fonts
# come in a follow-up pass).
file(GLOB GAMESWF_PLAYER_SOURCES_ALL CONFIGURE_DEPENDS
  "${GAMESWF_ROOT}/gameswf/*.cpp"
)
list(FILTER GAMESWF_PLAYER_SOURCES_ALL EXCLUDE REGEX
  "gameswf_(render_handler_[a-z0-9]+|sound_handler_[a-z]+|freetype|test_ogl)\\.cpp$"
)

add_library(mcle_gameswf_probe STATIC
  ${GAMESWF_PROBE_SOURCES}
  ${GAMESWF_PLAYER_SOURCES_ALL}
  probe_stub.cpp
)

target_include_directories(mcle_gameswf_probe PRIVATE
  ${GAMESWF_ROOT}
  ${GAMESWF_ROOT}/base
  ${GAMESWF_ROOT}/gameswf
  ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS
  ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/UI
  ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/compat_headers
)

target_compile_options(mcle_gameswf_probe PRIVATE
  # Replace the Marmalade compatibility_include.h with our iOS config.
  -include "${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/UI/gameswf_ios_config.h"
  # Quiet the flood of warnings from the legacy codebase so errors stand out.
  -w
)
