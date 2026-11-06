#pragma once

// What TexturePackRepository, AbstractTexturePack and DLCTexturePack get on
// iOS in place of the Iggy UI header (scripts/patch-texturepacks-ios.py).

#include "ArchiveFile.h"

// UIController::ReloadSkin, the menus loading their skin again from the
// selected pack. Game/MCLETexturePacks.cpp.
void mcle_ui_reload_skin();

// The UIController calls the texture packs make. Windows64 reloads the
// skin straight away rather than on its thread, so there's never a reload
// thread to start or wait for.
struct McleTexturePackUI {
    void ReloadSkin() { mcle_ui_reload_skin(); }
    void StartReloadSkinThread() {}
    bool IsReloadingSkin() { return false; }
};
static McleTexturePackUI ui;

// AbstractTexturePack::getXuiRootPath builds an Xbox resource locator from
// the module handle
static inline HMODULE GetModuleHandle(const char *) { return nullptr; }
