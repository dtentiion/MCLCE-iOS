# iOS UI layer: GameSWF runtime + Metal render_handler.
#
# Compiles the full GameSWF source set for iOS ARM64. This is what the
# upstream "Iggy" calls eventually route through via Iggy_Shim.cpp (the
# .iggy assets are renamed SWFs). The main app links this library and gets
# a full SWF player.
#
# The build requires scripts/patch-gameswf.sh to have run against the
# vendored submodule first. CI runs it automatically; set up locally with
# `./scripts/patch-gameswf.sh` before `cmake --build`.

set(GAMESWF_ROOT "${THIRD_PARTY_DIR}/gameswf/GameSwfPort/GameSwf")

# base/ layer. Matches the set proven green by the Port probes workflow.
set(GAMESWF_BASE_SOURCES
  ${GAMESWF_ROOT}/base/configvars.cpp
  ${GAMESWF_ROOT}/base/container.cpp
  ${GAMESWF_ROOT}/base/dlmalloc.c
  ${GAMESWF_ROOT}/base/ear_clip_triangulate_float.cpp
  ${GAMESWF_ROOT}/base/ear_clip_triangulate_sint16.cpp
  ${GAMESWF_ROOT}/base/file_util.cpp
  ${GAMESWF_ROOT}/base/image.cpp
  ${GAMESWF_ROOT}/base/image_filters.cpp
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
  ${GAMESWF_ROOT}/base/zlib_adapter.cpp
)

# Player layer. Skip desktop-OpenGL / Xbox / SDL / OpenAL handlers and the
# FreeType font path.
file(GLOB GAMESWF_PLAYER_SOURCES CONFIGURE_DEPENDS
  "${GAMESWF_ROOT}/gameswf/*.cpp"
)
list(FILTER GAMESWF_PLAYER_SOURCES EXCLUDE REGEX
  "gameswf_(render_handler_[a-z0-9]+|sound_handler_[a-z]+|freetype|test_ogl)\\.cpp$"
)

add_library(mcle_ios_ui STATIC
  Iggy_Shim.cpp
  render_handler_metal.mm
  mcle_swf_bridge.mm
  ${GAMESWF_BASE_SOURCES}
  ${GAMESWF_PLAYER_SOURCES}
)

target_include_directories(mcle_ios_ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UPSTREAM_DIR}/Minecraft.Client/Windows64/Iggy/include
  ${GAMESWF_ROOT}
  ${GAMESWF_ROOT}/base
  ${GAMESWF_ROOT}/gameswf
)

target_compile_options(mcle_ios_ui PRIVATE
  -include "${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/UI/gameswf_ios_config.h"
  -w  # legacy codebase; focus on errors, not warnings
)

set_target_properties(mcle_ios_ui PROPERTIES
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
)
