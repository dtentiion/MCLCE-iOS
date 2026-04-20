# Probe target for compiling GameSWF core for iOS ARM64.
#
# Flip on with -DENABLE_GAMESWF_PROBE=ON. Expected to fail on the first few
# passes, just like the world probe. The errors tell us which Marmalade /
# Windows assumptions remain in GameSWF and guide the iOS port of the
# library.

set(GAMESWF_ROOT "${THIRD_PARTY_DIR}/gameswf/GameSwfPort/GameSwf")

# Start with the smallest possible slice. Grow as these compile.
set(GAMESWF_PROBE_SOURCES
  ${GAMESWF_ROOT}/base/container.cpp
  ${GAMESWF_ROOT}/base/logger.cpp
  ${GAMESWF_ROOT}/base/membuf.cpp
  ${GAMESWF_ROOT}/base/tu_file.cpp
)

add_library(mcle_gameswf_probe STATIC
  ${GAMESWF_PROBE_SOURCES}
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
