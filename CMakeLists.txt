cmake_minimum_required(VERSION 3.24)

# Must be set before project() so the toolchain has a chance to configure things.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(PLATFORM_IOS TRUE)
endif()

project(MCLE_iOS
  VERSION 0.0.1
  DESCRIPTION "Native iOS port of MinecraftConsoles (TU19 LCE)"
  LANGUAGES C CXX OBJC OBJCXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Only iOS is supported here. Use upstream/ for desktop/console work.
if(NOT PLATFORM_IOS)
  message(FATAL_ERROR
    "This build targets iOS only. Configure with the iOS toolchain:\n"
    "  cmake -B build-ios -G Xcode -DCMAKE_TOOLCHAIN_FILE=cmake/ios.toolchain.cmake -DPLATFORM=OS64\n"
    "For desktop builds, work from upstream/ (MCLCE/MinecraftConsoles)."
  )
endif()

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
set(UPSTREAM_DIR "${CMAKE_SOURCE_DIR}/upstream")
set(IOS_PLATFORM_DIR "${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS")
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

if(NOT EXISTS "${UPSTREAM_DIR}/Minecraft.Client/Common")
  message(FATAL_ERROR
    "Upstream submodule missing. Run: git submodule update --init --recursive"
  )
endif()

# ---------------------------------------------------------------------------
# Shared compile flags
# ---------------------------------------------------------------------------
add_compile_definitions(
  _IOS=1
  PLATFORM_IOS=1
  _LARGE_WORLDS=1
  # Upstream code gates feature sets on these. We pretend to be a console build
  # because the "Windows" path drags in too much Win32 for now.
  __APPLE_IOS__=1
)

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
# Build our platform-layer libraries first. Each maps one piece of 4J / game
# infra onto an iOS equivalent.
add_subdirectory(Minecraft.Client/iOS/Storage)
add_subdirectory(Minecraft.Client/iOS/Input)
add_subdirectory(Minecraft.Client/iOS/Render)
add_subdirectory(Minecraft.Client/iOS/UI)

# App target (Objective-C++ shell + game bootstrap).
add_subdirectory(Minecraft.Client/iOS/App)
