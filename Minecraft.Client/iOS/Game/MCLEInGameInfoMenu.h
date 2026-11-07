#pragma once

// UIScene_InGameInfoMenu, and the UIScene_InGameHostOptionsMenu and
// UIScene_InGamePlayerOptionsMenu the host opens from it
// (MCLEInGameInfoMenu.cpp), for the one player here, the host of an offline
// game. The scenes run in the container menus' frame loop
// (MCLEContainerMenu.h), the options going over the info menu on its stack.

// Minecraft::tick's game info action: the buttons it's on in the pad's
// scheme, and whether the game mode lets it through
unsigned int mcle_ingame_info_buttons();
bool mcle_ingame_info_input_allowed();

// ui.NavigateToScene(eUIScene_InGameInfoMenu) once InGameInfoMenu1080 is up,
// and the press Minecraft::tick plays after it. False when it couldn't open.
bool mcle_ingame_info_open();
