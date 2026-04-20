# Minimal iOS toolchain file for MCLCE-iOS.
#
# Configure once from the command line with:
#   cmake -B build-ios -G Xcode \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/ios.toolchain.cmake \
#         -DPLATFORM=OS64
#
# PLATFORM values:
#   OS64        - iOS device (arm64), default
#   SIMULATOR64 - iOS simulator on Intel macOS (x86_64)
#   SIMULATORARM64 - iOS simulator on Apple Silicon macOS (arm64)
#
# Heavily inspired by community toolchains but trimmed for our needs.

if(DEFINED CMAKE_CROSSCOMPILING_IOS_INCLUDED)
  return()
endif()
set(CMAKE_CROSSCOMPILING_IOS_INCLUDED ON)

set(CMAKE_SYSTEM_NAME iOS)

if(NOT DEFINED PLATFORM)
  set(PLATFORM "OS64")
endif()

set(DEPLOYMENT_TARGET "14.0" CACHE STRING "iOS deployment target")

if(PLATFORM STREQUAL "OS64")
  set(SDK_NAME iphoneos)
  set(CMAKE_SYSTEM_PROCESSOR arm64)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
elseif(PLATFORM STREQUAL "SIMULATOR64")
  set(SDK_NAME iphonesimulator)
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
  set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
elseif(PLATFORM STREQUAL "SIMULATORARM64")
  set(SDK_NAME iphonesimulator)
  set(CMAKE_SYSTEM_PROCESSOR arm64)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
else()
  message(FATAL_ERROR "Unknown PLATFORM: ${PLATFORM}. Use OS64, SIMULATOR64, or SIMULATORARM64.")
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "${DEPLOYMENT_TARGET}" CACHE STRING "" FORCE)
set(CMAKE_OSX_SYSROOT "${SDK_NAME}" CACHE STRING "" FORCE)

# Resolve the full SDK path for diagnostics and linking.
execute_process(
  COMMAND xcrun --sdk ${SDK_NAME} --show-sdk-path
  OUTPUT_VARIABLE CMAKE_OSX_SYSROOT_PATH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _xcrun_result
)
if(NOT _xcrun_result EQUAL 0 OR NOT CMAKE_OSX_SYSROOT_PATH)
  message(FATAL_ERROR "xcrun could not find SDK '${SDK_NAME}'. Install Xcode and run: sudo xcode-select -s /Applications/Xcode.app")
endif()

# Resolve developer tools up front.
execute_process(
  COMMAND xcrun --sdk ${SDK_NAME} -f clang
  OUTPUT_VARIABLE CMAKE_C_COMPILER
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND xcrun --sdk ${SDK_NAME} -f clang++
  OUTPUT_VARIABLE CMAKE_CXX_COMPILER
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CMAKE_C_COMPILER   "${CMAKE_C_COMPILER}"   CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER "${CMAKE_CXX_COMPILER}" CACHE STRING "" FORCE)

# Skip the compiler ID smoke test. Xcode-based generators handle this themselves,
# and a bare clang invocation without -isysroot fails for cross builds.
set(CMAKE_C_COMPILER_WORKS   TRUE CACHE BOOL "" FORCE)
set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE BOOL "" FORCE)

# Search behavior for finding libraries / headers
set(CMAKE_FIND_ROOT_PATH "${CMAKE_OSX_SYSROOT_PATH}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# We target arm64 devices and x86_64/arm64 simulators. Skip bitcode (deprecated).
set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO)
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH YES)

# Code signing is disabled by default. CI produces an unsigned .ipa;
# users sign at install time with AltStore / Sideloadly / xtool.
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO")
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")

# Expose a define so CMake scripts can branch on iOS easily without retesting
# CMAKE_SYSTEM_NAME everywhere.
set(PLATFORM_IOS TRUE)
add_compile_definitions(PLATFORM_IOS=1 _IOS=1)

message(STATUS "iOS toolchain:")
message(STATUS "  PLATFORM            : ${PLATFORM}")
message(STATUS "  SDK                 : ${SDK_NAME}")
message(STATUS "  SDK path            : ${CMAKE_OSX_SYSROOT_PATH}")
message(STATUS "  Deployment target   : ${CMAKE_OSX_DEPLOYMENT_TARGET}")
message(STATUS "  Architectures       : ${CMAKE_OSX_ARCHITECTURES}")
