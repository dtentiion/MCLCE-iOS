# The iOS app bundle target. Everything else in the build graph feeds into
# this. Produces Minecraft.LCE.app which CI then packages into an .ipa.

set(APP_SOURCES
  main.m
  AppDelegate.h
  AppDelegate.mm
  MinecraftViewController.h
  MinecraftViewController.mm
  MetalView.h
  MetalView.mm
  GameBootstrap.cpp
)

set(APP_RESOURCES
  ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/Resources/LaunchScreen.storyboard
  # GameSWF's own test movie, checked into its public-domain repo. Used as a
  # smoke test for the Metal render_handler until we bundle LCE assets.
  ${CMAKE_SOURCE_DIR}/third_party/gameswf/GameSwfPort/data/start.swf
)

add_executable(Minecraft.LCE MACOSX_BUNDLE
  ${APP_SOURCES}
  ${APP_RESOURCES}
)

target_include_directories(Minecraft.LCE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Minecraft.LCE PRIVATE
  mcle_ios_storage
  mcle_ios_input
  mcle_ios_render
  mcle_ios_ui
  "-framework UIKit"
  "-framework Foundation"
  "-framework CoreGraphics"
  "-framework QuartzCore"
  # GameSWF reads compressed SWF headers via zlib. iOS ships libz.
  "-lz"
)

set_source_files_properties(${APP_RESOURCES} PROPERTIES
  MACOSX_PACKAGE_LOCATION Resources
)

set_target_properties(Minecraft.LCE PROPERTIES
  BUNDLE TRUE
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/Resources/Info.plist.in
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.legacy.mclce"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${DEPLOYMENT_TARGET}"
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
  XCODE_ATTRIBUTE_ENABLE_BITCODE NO
  # Unsigned build. Install via AltStore / Sideloadly / xtool.
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
  XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
)
