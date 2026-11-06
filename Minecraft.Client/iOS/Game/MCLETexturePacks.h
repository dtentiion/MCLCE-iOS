#pragma once

#include <string>
#include <vector>

class Minecraft;

// Minecraft::init's texture pack repository, made at launch with the media
// in Documents as the default pack. Game/MCLETexturePacks.cpp.
void mcle_texture_packs_init(Minecraft *minecraft);

// The repository's packs for IUIScene_StartGame's texture pack list
int mcle_texture_pack_count();
unsigned int mcle_texture_pack_id(int index);
// getPackIcon / getPackComparison, png data or nullptr
const unsigned char *mcle_texture_pack_icon(int index, unsigned int *bytes);
const unsigned char *mcle_texture_pack_comparison(int index, unsigned int *bytes);
// getName / getDesc1 as UTF-8
std::string mcle_texture_pack_name(int index);
std::string mcle_texture_pack_desc(int index);
// getTexturePackIndex, and whether getTexturePackById finds the pack
int mcle_texture_pack_index(unsigned int id);
bool mcle_texture_pack_installed(unsigned int id);

// CMinecraftApp::HandleXuiActions, from the main loop
void mcle_handle_xui_actions();

// UIController::NavigateToHomeMenu's part for the texture packs: the menus
// go back to the default pack
void mcle_texture_packs_navigate_to_home_menu();

// The client's wait in _RunNetworkGame for the selected pack's data and
// its reload, from the game thread
void mcle_texture_packs_wait_for_reload();

// Whether a UIController::ReloadSkin came since the last call, for the
// main loop to reload the skin libraries
bool mcle_ui_take_skin_reload();
// A file from the selected texture pack's media archive, as
// CMinecraftApp::getArchiveFile finds it before the game's own media
bool mcle_texture_pack_archive_file(const std::wstring &filename, std::vector<unsigned char> &out);
