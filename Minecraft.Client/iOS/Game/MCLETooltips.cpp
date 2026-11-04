// The in-game tooltips. scripts/gen-ingame-tooltips.py copies the block
// that works them out from upstream Minecraft::tick into
// iOS_IngameTooltips.inc, and it's compiled here against the names it uses:
// player, level, hitResult, gameMode, ui and InputManager.

#include "MCLETooltips.h"

#include <mutex>

#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/ServerPlayerGameMode.h"
#include "../../../upstream/Minecraft.Client/Common/Potion_Macros.h"
#include "../../../upstream/Minecraft.World/Abilities.h"
#include "../../../upstream/Minecraft.World/Animal.h"
#include "../../../upstream/Minecraft.World/ColoredTile.h"
#include "../../../upstream/Minecraft.World/Dimension.h"
#include "../../../upstream/Minecraft.World/DyePowderItem.h"
#include "../../../upstream/Minecraft.World/EntityHorse.h"
#include "../../../upstream/Minecraft.World/FoodData.h"
#include "../../../upstream/Minecraft.World/FoodItem.h"
#include "../../../upstream/Minecraft.World/GoldenAppleItem.h"
#include "../../../upstream/Minecraft.World/HitResult.h"
#include "../../../upstream/Minecraft.World/Inventory.h"
#include "../../../upstream/Minecraft.World/Item.h"
#include "../../../upstream/Minecraft.World/ItemFrame.h"
#include "../../../upstream/Minecraft.World/ItemInstance.h"
#include "../../../upstream/Minecraft.World/LeashItem.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.World/LevelData.h"
#include "../../../upstream/Minecraft.World/Material.h"
#include "../../../upstream/Minecraft.World/Mob.h"
#include "../../../upstream/Minecraft.World/MobEffect.h"
#include "../../../upstream/Minecraft.World/Ocelot.h"
#include "../../../upstream/Minecraft.World/Pig.h"
#include "../../../upstream/Minecraft.World/Sheep.h"
#include "../../../upstream/Minecraft.World/TallGrass.h"
#include "../../../upstream/Minecraft.World/Tile.h"
#include "../../../upstream/Minecraft.World/TileItem.h"
#include "../../../upstream/Minecraft.World/Vec3.h"
#include "../../../upstream/Minecraft.World/Villager.h"
#include "../../../upstream/Minecraft.World/Wolf.h"
#include "../../../upstream/Minecraft.World/Zombie.h"

// The Win64 4J_Input.h values; the iOS one has no button defines
#ifndef _360_JOY_BUTTON_A
#define _360_JOY_BUTTON_A            0x00000001
#define _360_JOY_BUTTON_RB           0x00000040
#define _360_JOY_BUTTON_LB           0x00000080
#define _360_JOY_BUTTON_RTHUMB       0x00000100
#define _360_JOY_BUTTON_LSTICK_RIGHT 0x00004000
#define _360_JOY_BUTTON_RT           0x00400000
#define _360_JOY_BUTTON_LT           0x00800000
#endif

namespace {

std::mutex g_mutex;
int g_tooltips[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

// MultiPlayerGameMode as the block uses it: useItem and useItemOn with
// bTestUseOnly set, which only ask whether the use would do anything, and
// no tutorial
struct TooltipGameMode {
    struct NoTutorial {
        void onLookAt(int, int) {}
        void onLookAtEntity(std::shared_ptr<Entity>) {}
    };

    bool creative = false;

    NoTutorial *getTutorial() { return nullptr; }

    bool useItem(std::shared_ptr<Player> player, Level *level, std::shared_ptr<ItemInstance> item, bool) {
        if (!player->isAllowedToUse(item)) return false;
        return item->TestUse(item, level, player);
    }

    bool useItemOn(std::shared_ptr<Player> player, Level *level, std::shared_ptr<ItemInstance> item, int x, int y,
                   int z, int face, Vec3 *hit, bool bTestUseOnly, bool *pbUsedItem = nullptr) {
        if (pbUsedItem) *pbUsedItem = false;
        float clickX = static_cast<float>(hit->x) - x;
        float clickY = static_cast<float>(hit->y) - y;
        float clickZ = static_cast<float>(hit->z) - z;
        bool didSomething = false;

        if (!player->isSneaking() || player->getCarriedItem() == nullptr) {
            int t = level->getTile(x, y, z);
            if (t > 0 && player->isAllowedToUse(Tile::tiles[t])) {
                switch (t) {
                case Tile::jukebox_Id:
                case Tile::bed_Id: // special case for a bed
                    if (Tile::tiles[t]->TestUse(level, x, y, z, player)) {
                        return true;
                    } else if (t == Tile::bed_Id) {
                        // bed is too far away, or something
                        return false;
                    }
                    break;
                default:
                    if (Tile::tiles[t]->TestUse()) return true;
                    break;
                }
            }
        }

        if (!didSomething && item != nullptr && dynamic_cast<TileItem *>(item->getItem())) {
            TileItem *tile = dynamic_cast<TileItem *>(item->getItem());
            if (!tile->mayPlace(level, x, y, z, face, player, item)) return false;
        }

        if (item != nullptr) {
            if (!didSomething && player->isAllowedToUse(item)) {
                if (creative) {
                    int aux = item->getAuxValue();
                    int count = item->count;
                    didSomething = item->useOn(player, level, x, y, z, face, clickX, clickY, clickZ, bTestUseOnly);
                    item->setAuxValue(aux);
                    item->count = count;
                } else {
                    didSomething = item->useOn(player, level, x, y, z, face, clickX, clickY, clickZ, bTestUseOnly);
                }
                if (didSomething && pbUsedItem) *pbUsedItem = true;
            }
        }
        return didSomething;
    }
};

// Windows64_Minecraft.cpp's MAP_STYLE_0, the only layout iOS has
struct TooltipInput {
    int GetJoypadMapVal(int) { return MAP_STYLE_0; }
    unsigned int GetGameJoypadMaps(int, int action) {
        switch (action) {
        case MINECRAFT_ACTION_JUMP: return _360_JOY_BUTTON_A;
        case MINECRAFT_ACTION_USE: return _360_JOY_BUTTON_LT;
        case MINECRAFT_ACTION_ACTION: return _360_JOY_BUTTON_RT;
        case MINECRAFT_ACTION_SNEAK_TOGGLE: return _360_JOY_BUTTON_RTHUMB;
        }
        return 0;
    }
};

// Where ui.SetTooltips goes
struct TooltipUI {
    int values[10];
    bool IsReloadingSkin() { return false; }
    void SetTooltips(int, int iA, int iB, int iX, int iY, int iLT, int iRT, int iLB, int iRB, int iLS, int iRS) {
        const int v[10] = { iA, iB, iX, iY, iLT, iRT, iLB, iRB, iLS, iRS };
        for (int i = 0; i < 10; i++) values[i] = v[i];
    }
};

} // namespace

void mcle_tooltips_update(const std::shared_ptr<ServerPlayer> &serverPlayer, HitResult *hitResult) {
    if (!serverPlayer || !serverPlayer->level || !serverPlayer->gameMode) return;
    std::shared_ptr<ServerPlayer> player = serverPlayer;
    Level *level = player->level;
    // The block was written for the client's level
    const bool levelIsClientSide = true;
    const int iPad = 0;

    TooltipGameMode gameModeImpl;
    gameModeImpl.creative = player->gameMode->isCreative();
    TooltipGameMode *gameMode = &gameModeImpl;
    TooltipInput tooltipInput;
    TooltipUI tooltipUI;
    for (int i = 0; i < 10; i++) tooltipUI.values[i] = -1;

    {
#define InputManager tooltipInput
#define ui tooltipUI
#include "../../../upstream/Minecraft.Client/iOS_IngameTooltips.inc"
#undef ui
#undef InputManager
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (int i = 0; i < 10; i++) g_tooltips[i] = tooltipUI.values[i];
}

extern "C" void mcle_tooltips_get(int out[10]) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (int i = 0; i < 10; i++) out[i] = g_tooltips[i];
}
