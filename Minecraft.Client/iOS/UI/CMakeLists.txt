# iOS UI layer. The upstream game drives its menus / HUD through Iggy
# (RAD Game Tools' SWF renderer). Iggy ships as closed-source .lib files on
# Windows, which cannot link into an iOS binary.
#
# The plan is to swap Iggy for GameSWF (open-source C++ SWF runtime) since the
# .iggy asset files are effectively SWFs. Until GameSWF is wired up, this
# target provides stub implementations that satisfy the linker and log a
# runtime warning for any Iggy call. The game will "run" without a UI.
#
# See ROADMAP.md section "UI: GameSWF" for the wiring plan.

add_library(mcle_ios_ui STATIC
  Iggy_Shim.cpp
)

target_include_directories(mcle_ios_ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UPSTREAM_DIR}/Minecraft.Client/Windows64/Iggy/include
)

# When third_party/gameswf exists we'll link it here. Left commented for now.
# target_link_libraries(mcle_ios_ui PUBLIC gameswf)
