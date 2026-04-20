# iOS storage layer. Replaces 4J_Storage for file I/O, save games, DLC paths.
# Uses NSFileManager under the hood.

add_library(mcle_ios_storage STATIC
  STO_iOS_Paths.mm
  STO_iOS_SaveGame.mm
)

target_include_directories(mcle_ios_storage PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UPSTREAM_DIR}/Minecraft.Client/Windows64/4JLibs/inc
)

target_link_libraries(mcle_ios_storage PUBLIC
  "-framework Foundation"
)

set_target_properties(mcle_ios_storage PROPERTIES
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
)
