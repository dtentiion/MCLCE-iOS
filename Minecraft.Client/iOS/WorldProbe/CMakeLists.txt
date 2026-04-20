# Probe target: try to compile upstream Minecraft.World sources for iOS.
#
# Flip on with -DENABLE_WORLD_PROBE=ON. Expected to fail loudly. Each failure
# here is a signal about another Windows-ism or missing platform shim that
# has to be addressed as part of Phase 1.
#
# Start with a tiny set (just AABB for now) and grow as things compile.
# Do not link this into the app yet; it exists to drive a TU-compile pass
# only.

set(WORLD_PROBE_SOURCES
  ${UPSTREAM_DIR}/Minecraft.World/AABB.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Vec3.cpp
)

add_library(mcle_world_probe STATIC
  ${WORLD_PROBE_SOURCES}
  probe_stub.cpp
)

target_include_directories(mcle_world_probe PRIVATE
  ${UPSTREAM_DIR}/Minecraft.World
  ${UPSTREAM_DIR}/Minecraft.World/x64headers
  ${UPSTREAM_DIR}/include
  ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS
  ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/compat_headers
)

# Drop the Win32 compat shim into every translation unit via force-include so
# we do not have to patch upstream .cpp files.
target_compile_options(mcle_world_probe PRIVATE
  -include "${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/iOS_WinCompat.h"
)

target_compile_definitions(mcle_world_probe PRIVATE
  _LIB
  _LARGE_WORLDS
)
