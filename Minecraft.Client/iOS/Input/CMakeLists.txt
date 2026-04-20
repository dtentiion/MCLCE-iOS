# iOS input layer. Bridges GameController.framework to the 4J_Input bitmasks
# the upstream game code already understands (Xbox 360 layout).

add_library(mcle_ios_input STATIC
  INP_iOS_Controller.mm
  INP_iOS_Bridge.cpp
)

target_include_directories(mcle_ios_input PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${UPSTREAM_DIR}/Minecraft.Client/Windows64/4JLibs/inc
)

target_link_libraries(mcle_ios_input PUBLIC
  "-framework Foundation"
  "-framework GameController"
  "-framework UIKit"
)

set_target_properties(mcle_ios_input PROPERTIES
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
)
