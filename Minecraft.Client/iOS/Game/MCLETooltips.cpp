// The in-game tooltips. scripts/gen-ingame-tooltips.py copies the block
// that works them out from upstream Minecraft::tick into
// iOS_IngameTooltips.inc, and it's compiled here against the names it uses:
// player, level, hitResult, gameMode and ui.

#include "MCLETooltips.h"
#include "MCLETutorial.h"

#include <mutex>

#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/ServerPlayerGameMode.h"
#include "../../../upstream/Minecraft.Client/Common/Potion_Macros.h"
#include "../../../upstream/Minecraft.World/Abilities.h"
#include "../../../upstream/Minecraft.World/Animal.h"
#include "../../../upstream/Minecraft.World/BedTile.h"
#include "../../../upstream/Minecraft.World/ChestTile.h"
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

namespace {

std::mutex g_mutex;
int g_tooltips[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

// The block asks the client's level, where these items' useOn returns true
// before anything else. On the server level the spawn egg's test spawns the
// mob, so they're answered the client's way.
bool useOnIsTrueOnClient(const std::shared_ptr<ItemInstance> &item) {
    return item->id == Item::spawnEgg_Id || item->id == Item::fireball_Id || item->id == Item::bed_Id;
}

// The TutorialMode as the block uses it: the tutorial it has, useItem, and
// useItemOn with bTestUseOnly set, which only asks whether the use would do
// anything
struct TooltipGameMode {
    bool creative = false;

    Tutorial *getTutorial() { return mcle_tutorial(); }

    bool useItem(std::shared_ptr<Player> player, Level *level, std::shared_ptr<ItemInstance> item, bool) {
        if (!player->isAllowedToUse(item)) return false;
        return item->TestUse(item, level, player);
    }

    // TutorialMode::useItemOn
    bool useItemOn(std::shared_ptr<Player> player, Level *level, std::shared_ptr<ItemInstance> item, int x, int y,
                   int z, int face, Vec3 *hit, bool bTestUseOnly, bool *pbUsedItem = nullptr) {
        MCLETutorialUse use = mcle_tutorial_use_item_on(level, item, x, y, z, bTestUseOnly);
        bool result = multiPlayerUseItemOn(player, level, item, x, y, z, face, hit, bTestUseOnly, pbUsedItem);
        mcle_tutorial_used_item_on(use, result, item, bTestUseOnly);
        return result;
    }

    // BedTile::TestUse and the test half of Player::startSleepInBed, as the
    // client's player gets them. Its y is at the eyes, so the two blocks of
    // height it allows are measured from there.
    static bool bedTestUse(Level *level, int x, int y, int z, const std::shared_ptr<Player> &player) {
        int data = level->getData(x, y, z);
        if (!BedTile::isHeadPiece(data)) {
            // fetch head piece instead
            int direction = DirectionalTile::getDirection(data);
            x += BedTile::HEAD_DIRECTION_OFFSETS[direction][0];
            z += BedTile::HEAD_DIRECTION_OFFSETS[direction][1];
            if (level->getTile(x, y, z) != Tile::bed_Id) return true;
            data = level->getData(x, y, z);
        }
        if (!level->dimension->mayRespawn()) return false;
        if (BedTile::isOccupied(data)) return false;
        if (player->isSleeping() || !player->isAlive()) return false;
        if (!level->dimension->isNaturalDimension()) return false;
        const double eyeY = player->y + 1.62;
        if (abs(player->x - x) > 3 || abs(eyeY - y) > 2 || abs(player->z - z) > 3) return false;
        return true;
    }

    // MultiPlayerGameMode::useItemOn
    bool multiPlayerUseItemOn(std::shared_ptr<Player> player, Level *level, std::shared_ptr<ItemInstance> item,
                              int x, int y, int z, int face, Vec3 *hit, bool bTestUseOnly, bool *pbUsedItem) {
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
                    if (t == Tile::bed_Id ? bedTestUse(level, x, y, z, player)
                                          : Tile::tiles[t]->TestUse(level, x, y, z, player)) {
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
                if (useOnIsTrueOnClient(item)) {
                    didSomething = true;
                } else if (creative) {
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
    TooltipUI tooltipUI;
    for (int i = 0; i < 10; i++) tooltipUI.values[i] = -1;

    {
#define ui tooltipUI
#include "../../../upstream/Minecraft.Client/iOS_IngameTooltips.inc"
#undef ui
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (int i = 0; i < 10; i++) g_tooltips[i] = tooltipUI.values[i];
}

extern "C" void mcle_tooltips_get(int out[10]) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (int i = 0; i < 10; i++) out[i] = g_tooltips[i];
}
