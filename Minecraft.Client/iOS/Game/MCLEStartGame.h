// What CreateWorldMenu and LoadMenu (IUIScene_StartGame) keep for More
// Options and start a game with. Upstream's LaunchMoreOptionsMenuInitData
// from UIStructs.h, with its constructor's defaults.
#pragma once

#include <string>

struct MCLELaunchMoreOptions {
    bool bOnlineGame = true;
    bool bInviteOnly = false;
    bool bAllowFriendsOfFriends = true;

    bool bGenerateOptions = false;
    bool bStructures = false;
    bool bFlatWorld = false;
    bool bBonusChest = false;

    bool bPVP = true;
    bool bTrust = false;
    bool bFireSpreads = true;
    bool bTNT = true;

    bool bHostPrivileges = false;
    bool bResetNether = false;

    bool bMobGriefing = true;
    bool bKeepInventory = false;
    bool bDoMobSpawning = true;
    bool bDoMobLoot = true;
    bool bDoTileDrops = true;
    bool bNaturalRegeneration = true;
    bool bDoDaylightCycle = true;

    bool bOnlineSettingChangedBySystem = false;

    int iPad = -1;

    unsigned int dwTexturePack = 0;

    std::wstring seed;
    int worldSize = 3;
    bool bDisableSaving = false;

    // EGameHostOptionWorldSize, 0 is e_worldSize_Unknown
    int currentWorldSize = 0;
    int newWorldSize = 0;
    bool newWorldSizeOverwriteEdges = false;
};

// UIScene_LoadMenu's constructor: the params for a save, from its host
// options when its thumbnail had them
void mcle_load_menu_options(MCLELaunchMoreOptions &params, bool hostOptionsRead,
                            unsigned int uiHostOptions, bool &isSaveOwner);

// UIScene_LoadMenu::StartGameFromSave's host options
void mcle_game_set_load_options(const MCLELaunchMoreOptions &params, bool isSaveOwner,
                                int difficulty, int gameModeId);

// UIScene_LoadMenu::StartGameFromSave for a level generator's world, the
// index one of mcle_level_generators_refresh's
int mcle_game_load_level_gen(int i, const MCLELaunchMoreOptions &params, bool isSaveOwner,
                             int difficulty, int gameModeId);

// UIScene_CreateWorldMenu::CreateGame, then the game thread starts on a new level
int mcle_game_create_world(const MCLELaunchMoreOptions &params, const std::wstring &worldName,
                           int gameModeId, int difficulty);
