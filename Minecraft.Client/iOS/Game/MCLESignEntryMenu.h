#pragma once

// UIScene_SignEntryMenu over a sign the player has just placed, and the
// UIScene_Keyboard its lines are typed on (MCLESignEntryMenu.cpp). The
// scenes run in the container menus' frame loop (MCLEContainerMenu.h).

// From the sim thread: the TileEditorOpenPacket ServerPlayer::openTextEdit
// sends the local player's client
void mcle_sign_entry_tile_editor_open(int editorType, int x, int y, int z);

// ClientConnection::handleTileEditorOpen for the last packet, on the main
// thread while no menu is up. True when LocalPlayer::openTextEdit has a
// sign to load the sign entry menu for.
bool mcle_sign_entry_take_request();

// app.LoadSignEntryMenu once SignEntryMenu1080 is up, false when it
// couldn't open
bool mcle_sign_entry_open();
