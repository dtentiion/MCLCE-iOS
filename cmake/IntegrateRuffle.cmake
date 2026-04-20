# IntegrateRuffle.cmake
#
# Registers `ruffle_ios` as an IMPORTED static library that CMake can link
# against the iOS app. The actual .a is produced by `cargo build` in CI;
# this file is just the bridge between "a path on disk" and "a proper
# CMake target".
#
# The expected path is stable across CI runs:
#   third_party/ruffle_ios/target/aarch64-apple-ios/release/libruffle_ios.a
#
# Local developers need to run the same cargo command before their first
# CMake build (see the "Install Rust toolchain" step in .github/workflows/
# ios.yml for the exact invocation).

set(RUFFLE_IOS_CRATE_DIR "${CMAKE_SOURCE_DIR}/third_party/ruffle_ios")
set(RUFFLE_IOS_LIB_PATH
    "${RUFFLE_IOS_CRATE_DIR}/target/aarch64-apple-ios/release/libruffle_ios.a")

add_library(ruffle_ios STATIC IMPORTED GLOBAL)
set_target_properties(ruffle_ios PROPERTIES
    IMPORTED_LOCATION "${RUFFLE_IOS_LIB_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${RUFFLE_IOS_CRATE_DIR}/include"
)
