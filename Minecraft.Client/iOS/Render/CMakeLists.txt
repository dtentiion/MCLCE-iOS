# iOS renderer. Will be replaced with a GL ES 3.0 backend translated to Metal
# through MetalANGLE, or a straight Metal backend if we run out of patience
# with GL ES. For now this target is a stub so the rest of the build links.

add_library(mcle_ios_render STATIC
  RND_iOS_Metal.mm
)

target_include_directories(mcle_ios_render PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UPSTREAM_DIR}/Minecraft.Client/Windows64/4JLibs/inc
)

# Linking Metal even though we aren't using it yet, so the renderer can be
# swapped in without rebuilding the CMake graph.
target_link_libraries(mcle_ios_render PUBLIC
  "-framework Metal"
  "-framework MetalKit"
  "-framework QuartzCore"
  "-framework OpenGLES"
)

set_target_properties(mcle_ios_render PROPERTIES
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
)
