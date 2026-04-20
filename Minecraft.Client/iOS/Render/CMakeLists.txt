# iOS renderer. Metal-backed. MetalContext hosts shared device / queue / frame
# state so other iOS libraries (the GameSWF render_handler in particular)
# can draw into the same pass without creating their own drawables.

add_library(mcle_ios_render STATIC
  MetalContext.mm
  RND_iOS_Metal.mm
)

target_include_directories(mcle_ios_render PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${UPSTREAM_DIR}/Minecraft.Client/Windows64/4JLibs/inc
)

target_link_libraries(mcle_ios_render PUBLIC
  "-framework Metal"
  "-framework MetalKit"
  "-framework QuartzCore"
)

set_target_properties(mcle_ios_render PROPERTIES
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
)
