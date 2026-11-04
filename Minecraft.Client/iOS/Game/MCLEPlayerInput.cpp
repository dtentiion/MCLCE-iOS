// Client side of breaking, placing, attacking, item use and hotbar
// scrolling for the local player. Ported from Minecraft::tick (the ACTION /
// USE / scroll handling), GameRenderer::pick, LocalPlayer::handleMouseClick,
// handleMouseDown and creativeModeHandleMouseClick, and MultiPlayerGameMode.
//
// There's no client level, the server level is the one on screen, so the
// client-side prediction MultiPlayerGameMode does on its own level is left
// out. Each packet it would send is handled here the way PlayerConnection
// handles it on the server, against the same ServerPlayerGameMode.

#include "MCLEPlayerInput.h"

#include <cmath>
#include <mutex>

#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/ServerPlayerGameMode.h"
#include "../../../upstream/Minecraft.Client/ServerLevel.h"
#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/ParticleEngine.h"
#include "../../../upstream/Minecraft.World/HitResult.h"
#include "../../../upstream/Minecraft.World/Vec3.h"
#include "../../../upstream/Minecraft.World/AABB.h"
#include "../../../upstream/Minecraft.World/Tile.h"
#include "../../../upstream/Minecraft.World/TileItem.h"
#include "../../../upstream/Minecraft.World/Item.h"
#include "../../../upstream/Minecraft.World/ItemInstance.h"
#include "../../../upstream/Minecraft.World/Inventory.h"
#include "../../../upstream/Minecraft.World/LevelSettings.h"
#include "../../../upstream/Minecraft.World/Facing.h"
#include "../../../upstream/Minecraft.World/Dimension.h"
#include "../../../upstream/Minecraft.World/ChunkSource.h"
#include "../../../upstream/Minecraft.World/FoodData.h"
#include "../../../upstream/Minecraft.World/FoodConstants.h"
#include "../../../upstream/Minecraft.World/MobEffect.h"

namespace {

// _360_JOY_BUTTON_* from the Win64 4J_Input.h, which the iOS pad reader uses.
// Windows64_Minecraft.cpp maps ACTION to RT, USE to LT, scroll to LB / RB.
// JUMP is A and SNEAK_TOGGLE is the right stick click.
const unsigned int kButtonA = 0x00000001;
const unsigned int kButtonRB = 0x00000040;
const unsigned int kButtonLB = 0x00000080;
const unsigned int kButtonRThumb = 0x00000100;
const unsigned int kButtonRT = 0x00400000;
const unsigned int kButtonLT = 0x00800000;
const unsigned int kButtonDpadLeft = 0x00001000;
const unsigned int kButtonDpadRight = 0x00002000;

// PlayerActionPacket actions the client sends while mining
enum Action { START_DESTROY_BLOCK, ABORT_DESTROY_BLOCK, STOP_DESTROY_BLOCK };

// LocalPlayer::lastClickState
enum ClickState { lastClick_invalid, lastClick_init, lastClick_moving, lastClick_stopped,
                  lastClick_oldRepeat, lastClick_disabled };

// Same eye point the camera uses (ServerPlayer's y is at the feet)
const double kEyeHeight = 1.62;

struct ClientState {
    int ticks = 0;
    unsigned int prevButtons = 0;

    // LocalPlayer
    int missTime = 0;
    int lastClickTick[2] = {0, 0};
    ClickState lastClickState = lastClick_invalid;
    float lastClickX = 0, lastClickY = 0, lastClickZ = 0;
    float lastClickdX = 0, lastClickdY = 0, lastClickdZ = 0;
    float lastClickTolerance = 0;

    // MultiPlayerGameMode
    bool isDestroying = false;
    int xDestroyBlock = -1, yDestroyBlock = -1, zDestroyBlock = -1;
    float destroyProgress = 0;
    int destroyTicks = 0;
    int destroyDelay = 0;
    std::shared_ptr<ItemInstance> destroyingItem;

    HitResult *hitResult = nullptr;
};

ClientState s;

std::shared_ptr<ServerPlayer> P;
ServerLevel *L = nullptr;

bool isCreative() { return P->gameMode->isCreative(); }
bool isAdventureRestricted() { return P->gameMode->getGameModeForPlayer()->isAdventureRestricted(); }

// MultiPlayerGameMode::hasMissTime
bool hasMissTime() { return !isCreative(); }

Vec3 *eyePos(ServerPlayer *p, double x, double y, double z, float a) {
    double ex = p->xo + (x - p->xo) * a;
    double ey = p->yo + (y - p->yo) * a + kEyeHeight - p->heightOffset;
    double ez = p->zo + (z - p->zo) * a;
    return Vec3::newTemp(ex, ey, ez);
}

// GameRenderer::pick, from an explicit position so the creative repeat
// placement can pick from where the click should have come from
HitResult *pickFrom(const std::shared_ptr<ServerPlayer> &player, ServerLevel *level,
                    double px, double py, double pz, float a) {
    ServerPlayer *p = player.get();
    // MultiPlayerGameMode::getPickRange / hasFarPickRange
    const bool creative = p->gameMode->isCreative();
    double range = creative ? 5.0f : 4.5f;
    Vec3 *from = eyePos(p, px, py, pz, a);
    Vec3 *b = p->getViewVector(a);
    Vec3 *to = from->add(b->x * range, b->y * range, b->z * range);
    HitResult *hitResult = level->clip(from, to);

    // 4J - stop blocks right at the edge of the world from being pickable
    if (hitResult) {
        int maxxz = ((level->chunkSource->m_XZSize / 2) * 16) - 2;
        int minxz = (-(level->chunkSource->m_XZSize / 2) * 16) + 1;
        int hitx = hitResult->x;
        int hitz = hitResult->z;
        int face = hitResult->f;
        if (face == Facing::WEST && hitx < 0) hitx -= 1;
        if (face == Facing::EAST && hitx > 0) hitx += 1;
        if (face == Facing::NORTH && hitz < 0) hitz -= 1;
        if (face == Facing::SOUTH && hitz > 0) hitz += 1;
        if ((hitx < minxz) || (hitx > maxxz) || (hitz < minxz) || (hitz > maxxz)) {
            delete hitResult;
            hitResult = nullptr;
        }
    }

    double dist = range;
    from = eyePos(p, px, py, pz, a);

    if (creative) {
        dist = range = 6;
    } else {
        if (dist > 3) dist = 3;
        range = dist;
    }

    if (hitResult != nullptr) dist = hitResult->pos->distanceTo(from);

    to = from->add(b->x * range, b->y * range, b->z * range);
    std::shared_ptr<Entity> hovered;
    float overlap = 1;
    std::vector<std::shared_ptr<Entity> > *objects =
        level->getEntities(player, p->bb->expand(b->x * range, b->y * range, b->z * range)->grow(overlap, overlap, overlap));
    double nearest = dist;

    if (objects) {
        for (auto &e : *objects) {
            if (e == nullptr || !e->isPickable()) continue;

            float rr = e->getPickRadius();
            AABB *bb = e->bb->grow(rr, rr, rr);
            HitResult *p = bb->clip(from, to);
            if (bb->contains(from)) {
                if (0 < nearest || nearest == 0) {
                    hovered = e;
                    nearest = 0;
                }
            } else if (p != nullptr) {
                double dd = from->distanceTo(p->pos);
                auto const riding = player->riding;
                if (riding != nullptr && e == riding) {
                    if (nearest == 0) hovered = e;
                } else {
                    hovered = e;
                    nearest = dd;
                }
            }
            delete p;
        }
    }

    if (hovered != nullptr) {
        if (nearest < dist || (hitResult == nullptr)) {
            delete hitResult;
            hitResult = new HitResult(hovered);
        }
    }
    return hitResult;
}

// Last tick's targeted block, for the render thread's selection box
std::mutex g_hitMutex;
bool g_hitTile = false;
int g_hitX = 0, g_hitY = 0, g_hitZ = 0, g_hitF = 0;

void pick(float a) {
    delete s.hitResult;
    s.hitResult = pickFrom(P, L, P->x, P->y, P->z, a);

    std::lock_guard<std::mutex> lock(g_hitMutex);
    g_hitTile = s.hitResult != nullptr && s.hitResult->type == HitResult::TILE;
    if (g_hitTile) {
        g_hitX = s.hitResult->x;
        g_hitY = s.hitResult->y;
        g_hitZ = s.hitResult->z;
        g_hitF = s.hitResult->f;
    }
}

// PlayerConnection::handlePlayerAction for the three mining actions
void sendAction(Action action, int x, int y, int z, int face) {
    double xDist = P->x - (x + 0.5);
    // there is a mismatch between the player's camera and the player's
    // position, so add 1.5 blocks
    double yDist = P->y - (y + 0.5) + 1.5;
    double zDist = P->z - (z + 0.5);
    double dist = xDist * xDist + yDist * yDist + zDist * zDist;
    if (dist > 6 * 6) return;
    if (y >= L->getMaxBuildHeight()) return;

    if (action == START_DESTROY_BLOCK) P->gameMode->startDestroyBlock(x, y, z, face);
    else if (action == STOP_DESTROY_BLOCK) P->gameMode->stopDestroyBlock(x, y, z);
    else P->gameMode->abortDestroyBlock(x, y, z);
}

bool sameDestroyTarget(int x, int y, int z) {
    std::shared_ptr<ItemInstance> selected = P->getCarriedItem();
    bool sameItems = s.destroyingItem == nullptr && selected == nullptr;
    if (s.destroyingItem != nullptr && selected != nullptr) {
        sameItems = selected->id == s.destroyingItem->id &&
                    ItemInstance::tagMatches(selected, s.destroyingItem) &&
                    (selected->isDamageableItem() || selected->getAuxValue() == s.destroyingItem->getAuxValue());
    }
    return x == s.xDestroyBlock && y == s.yDestroyBlock && z == s.zDestroyBlock && sameItems;
}

// MultiPlayerGameMode::startDestroyBlock. The client's own destroyBlock and
// destroyTileProgress calls are prediction on its level; the server's
// startDestroyBlock does both on this one.
void startDestroyBlock(int x, int y, int z, int face) {
    if (!P->isAllowedToMine()) return;

    if (isAdventureRestricted()) {
        if (!P->mayDestroyBlockAt(x, y, z)) return;
    }

    if (isCreative()) {
        // Skip if we just broke a block, prevents double-break on single clicks
        if (s.destroyDelay > 0) return;
        sendAction(START_DESTROY_BLOCK, x, y, z, face);
        s.destroyDelay = 5;
    } else if (!s.isDestroying || !sameDestroyTarget(x, y, z)) {
        if (s.isDestroying) {
            sendAction(ABORT_DESTROY_BLOCK, s.xDestroyBlock, s.yDestroyBlock, s.zDestroyBlock, face);
        }
        sendAction(START_DESTROY_BLOCK, x, y, z, face);
        int t = L->getTile(x, y, z);
        if (!(t > 0 && Tile::tiles[t]->getDestroyProgress(P, P->level, x, y, z) >= 1)) {
            s.isDestroying = true;
            s.xDestroyBlock = x;
            s.yDestroyBlock = y;
            s.zDestroyBlock = z;
            s.destroyingItem = P->getCarriedItem();
            s.destroyProgress = 0;
            s.destroyTicks = 0;
        }
    }
}

void stopDestroyBlock() {
    if (s.isDestroying) {
        sendAction(ABORT_DESTROY_BLOCK, s.xDestroyBlock, s.yDestroyBlock, s.zDestroyBlock, -1);
    }
    s.isDestroying = false;
    s.destroyProgress = 0;
    s.destroyDelay = 0;
}

void continueDestroyBlock(int x, int y, int z, int face) {
    if (!P->isAllowedToMine()) return;

    if (s.destroyDelay > 0) {
        s.destroyDelay--;
        return;
    }

    if (isCreative()) {
        s.destroyDelay = 5;
        sendAction(START_DESTROY_BLOCK, x, y, z, face);
        return;
    }

    if (sameDestroyTarget(x, y, z)) {
        int t = L->getTile(x, y, z);
        if (t == 0) {
            s.isDestroying = false;
            return;
        }
        Tile *tile = Tile::tiles[t];

        s.destroyProgress += tile->getDestroyProgress(P, P->level, x, y, z);

        // Upstream plays the tile's step sound here every 4 ticks through
        // minecraft->soundEngine, which doesn't exist on iOS yet.

        s.destroyTicks++;

        if (s.destroyProgress >= 1) {
            s.isDestroying = false;
            sendAction(STOP_DESTROY_BLOCK, x, y, z, face);
            s.destroyProgress = 0;
            s.destroyTicks = 0;
            s.destroyDelay = 5;
        }
    } else {
        startDestroyBlock(x, y, z, face);
    }
}

// MultiPlayerGameMode::useItemOn plus PlayerConnection::handleUseItem. The
// tile/item use runs once, for real, in the same order as the client so
// pbUsedItem means the same thing.
bool useItemOn(std::shared_ptr<ItemInstance> item, int x, int y, int z, int face, Vec3 *hit, bool *pbUsedItem) {
    if (pbUsedItem) *pbUsedItem = false;

    float clickX = static_cast<float>(hit->x) - x;
    float clickY = static_cast<float>(hit->y) - y;
    float clickZ = static_cast<float>(hit->z) - z;
    bool didSomething = false;

    // handleUseItem only acts below the build limit and within 8 blocks
    int maxBuildHeight = L->getMaxBuildHeight();
    if (!((y < maxBuildHeight - 1) || (face != Facing::UP && y < maxBuildHeight))) return false;
    if (P->distanceToSqr(x + 0.5, y + 0.5, z + 0.5) >= 8 * 8) return false;

    if (!P->isSneaking() || P->getCarriedItem() == nullptr) {
        int t = L->getTile(x, y, z);
        if (t > 0 && P->isAllowedToUse(Tile::tiles[t])) {
            if (Tile::tiles[t]->use(L, x, y, z, P, face, clickX, clickY, clickZ)) didSomething = true;
        }
    }

    if (!didSomething && item != nullptr && dynamic_cast<TileItem *>(item->getItem())) {
        TileItem *tile = dynamic_cast<TileItem *>(item->getItem());
        if (!tile->mayPlace(L, x, y, z, face, P, item)) return false;
    }

    if (item != nullptr) {
        if (!didSomething && P->isAllowedToUse(item)) {
            if (isCreative()) {
                int aux = item->getAuxValue();
                int count = item->count;
                didSomething = item->useOn(P, L, x, y, z, face, clickX, clickY, clickZ);
                item->setAuxValue(aux);
                item->count = count;
            } else {
                didSomething = item->useOn(P, L, x, y, z, face, clickX, clickY, clickZ);
            }
            if (didSomething && pbUsedItem) *pbUsedItem = true;
        }
    }

    std::shared_ptr<ItemInstance> selected = P->inventory->getSelected();
    if (selected != nullptr && selected->count == 0) {
        P->inventory->items[P->inventory->selected] = nullptr;
    }
    return didSomething;
}

// MultiPlayerGameMode::useItem, handled as handleUseItem's face 255 case
bool useItem(std::shared_ptr<ItemInstance> item) {
    if (!P->isAllowedToUse(item)) return false;
    if (item == nullptr) return false;
    return P->gameMode->useItem(P, L, item);
}

// MultiPlayerGameMode::attack / interact through handleInteract
const double kInteractReachSq = 6.0 * 6.0;
const double kInteractBlockedReachSq = 3.0 * 3.0;

bool inReach(const std::shared_ptr<Entity> &target) {
    double maxDistSq = P->canSee(target) ? kInteractReachSq : kInteractBlockedReachSq;
    return P->distanceToSqr(target) <= maxDistSq;
}

void attack(const std::shared_ptr<Entity> &entity) {
    if (entity == nullptr || !inReach(entity)) return;
    if ((entity->GetType() == eTYPE_ITEMENTITY) || (entity->GetType() == eTYPE_EXPERIENCEORB) ||
        (entity->GetType() == eTYPE_ARROW) || entity == P) {
        return;
    }
    P->attack(entity);
}

bool interact(const std::shared_ptr<Entity> &entity) {
    if (entity == nullptr || !inReach(entity)) return false;
    return P->interact(entity);
}

// LocalPlayer::handleMouseDown
void handleMouseDown(int button, bool down) {
    if (P->isSleeping()) return;
    if (!down) s.missTime = 0;
    if (button == 0 && s.missTime > 0) return;

    if (down && s.hitResult != nullptr && s.hitResult->type == HitResult::TILE && button == 0) {
        int x = s.hitResult->x;
        int y = s.hitResult->y;
        int z = s.hitResult->z;

        // 4J - stop layer mining out of the top or bottom of the world, allowed in The End
        if (((y == 0) || ((y == 127) && P->level->dimension->hasCeiling)) && P->level->dimension->id != 1) return;

        continueDestroyBlock(x, y, z, s.hitResult->f);

        if (P->mayDestroyBlockAt(x, y, z)) {
            Minecraft *mc = Minecraft::GetInstance();
            if (mc && mc->particleEngine) mc->particleEngine->crack(x, y, z, s.hitResult->f);
            P->swing();
        }
    } else {
        stopDestroyBlock();
    }
}

// LocalPlayer::handleMouseClick
bool handleMouseClick(int button) {
    bool returnItemPlaced = false;

    if (button == 0 && s.missTime > 0) return false;
    if (button == 0) P->swing();

    bool mayUse = true;

    // Waking up is the only input taken while asleep
    if (button == 1 && P->isSleeping()) {
        if (s.lastClickState == lastClick_oldRepeat) return false;
        P->stopSleepInBed(false, true, true);
    }
    if (P->isSleeping()) return false;

    std::shared_ptr<ItemInstance> oldItem = P->inventory->getSelected();

    if (s.hitResult == nullptr) {
        if (button == 0 && hasMissTime()) s.missTime = 10;
    } else if (s.hitResult->type == HitResult::ENTITY) {
        if (button == 0) attack(s.hitResult->entity);
        if (button == 1) {
            // Milking a cow fills the bucket, so don't also use it on a tile
            if (s.hitResult->entity->GetType() == eTYPE_COW) {
                std::shared_ptr<ItemInstance> item = P->inventory->getSelected();
                if (item && (item->id == Item::bucket_empty_Id)) mayUse = false;
            }
            if (interact(s.hitResult->entity)) mayUse = false;
        }
    } else if (s.hitResult->type == HitResult::TILE) {
        int x = s.hitResult->x;
        int y = s.hitResult->y;
        int z = s.hitResult->z;
        int face = s.hitResult->f;

        if (button == 0) {
            if (!((y == 0) || ((y == 127) && P->level->dimension->hasCeiling)) || P->level->dimension->id == 1) {
                startDestroyBlock(x, y, z, s.hitResult->f);
            }
        } else {
            std::shared_ptr<ItemInstance> item = oldItem;
            bool usedItem = false;
            if (useItemOn(item, x, y, z, face, s.hitResult->pos, &usedItem)) {
                if (usedItem) returnItemPlaced = true;
                mayUse = false;
                P->swing();
            }
            if (item == nullptr) return false;
            if (item->count == 0) P->inventory->items[P->inventory->selected] = nullptr;
        }
    }

    if (mayUse && button == 1) {
        std::shared_ptr<ItemInstance> item = P->inventory->getSelected();
        if (item != nullptr) useItem(item);
    }
    return returnItemPlaced;
}

// LocalPlayer::creativeModeHandleMouseClick
bool creativeModeHandleMouseClick(int button, bool buttonPressed) {
    if (buttonPressed) {
        if (s.lastClickState == lastClick_oldRepeat) return false;

        // Auto-repeat: only click again after moving a unit from the last click
        if (s.lastClickState != lastClick_invalid) {
            if (s.lastClickState == lastClick_disabled) return false;
            if (P->isSprinting()) {
                s.lastClickState = lastClick_disabled;
                return false;
            }

            float dX = static_cast<float>(P->x) - s.lastClickX;
            float dY = static_cast<float>(P->y) - s.lastClickY;
            float dZ = static_cast<float>(P->z) - s.lastClickZ;
            bool newClick = false;

            float ddx = dX - s.lastClickdX;
            float ddy = dY - s.lastClickdY;
            float ddz = dZ - s.lastClickdZ;

            if (s.lastClickState == lastClick_moving) {
                float deltaChange = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
                if (deltaChange < 0.01f) {
                    s.lastClickState = lastClick_stopped;
                    s.lastClickTolerance = 0.0f;
                }
            } else if (s.lastClickState == lastClick_stopped) {
                float deltaChange = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
                if (deltaChange >= 0.01f) {
                    s.lastClickState = lastClick_moving;
                    s.lastClickTolerance = 0.0f;
                } else {
                    s.lastClickTolerance += 0.1f;
                    if (s.lastClickTolerance > 0.7f) {
                        s.lastClickTolerance = 0.0f;
                        s.lastClickState = lastClick_init;
                    }
                }
            }

            s.lastClickdX = dX;
            s.lastClickdY = dY;
            s.lastClickdZ = dZ;

            if (fabsf(dX) >= 1.0f) {
                dX = (dX < 0.0f) ? ceilf(dX) : floorf(dX);
                newClick = true;
            } else if (fabsf(dY) >= 1.0f) {
                dY = (dY < 0.0f) ? ceilf(dY) : floorf(dY);
                newClick = true;
            } else if (fabsf(dZ) >= 1.0f) {
                dZ = (dZ < 0.0f) ? ceilf(dZ) : floorf(dZ);
                newClick = true;
            }

            if ((!newClick) && (s.lastClickTolerance > 0.0f)) {
                float fTarget = 1.0f - s.lastClickTolerance;
                if (fabsf(dX) >= fTarget) newClick = true;
                if (fabsf(dY) >= fTarget) newClick = true;
                if (fabsf(dZ) >= fTarget) newClick = true;
            }

            if (newClick) {
                s.lastClickX += dX;
                s.lastClickY += dY;
                s.lastClickZ += dZ;

                // Pick from where the new click should have come from
                delete s.hitResult;
                s.hitResult = pickFrom(P, L, s.lastClickX, s.lastClickY, s.lastClickZ, 1);

                handleMouseClick(button);

                if (s.lastClickState == lastClick_stopped) {
                    s.lastClickState = lastClick_init;
                    s.lastClickTolerance = 0.0f;
                } else {
                    s.lastClickState = lastClick_moving;
                    s.lastClickTolerance = 0.0f;
                }
            }
        } else {
            // First click, just record position and handle
            s.lastClickX = static_cast<float>(P->x);
            s.lastClickY = static_cast<float>(P->y);
            s.lastClickZ = static_cast<float>(P->z);
            bool itemPlaced = handleMouseClick(button);
            if (P->isSprinting() || P->isRiding() || P->isSleeping()) {
                s.lastClickState = lastClick_disabled;
            } else if (itemPlaced) {
                s.lastClickState = lastClick_init;
                s.lastClickTolerance = 0.0f;
            } else {
                // Probably a switch or a door, use the standard auto repeat
                s.lastClickState = lastClick_oldRepeat;
            }
            return true;
        }
    } else {
        s.lastClickState = lastClick_invalid;
    }
    return false;
}

} // namespace

void mcle_player_input_tick(const std::shared_ptr<ServerPlayer> &player, ServerLevel *level,
                            unsigned int buttons) {
    if (!player || !level || !player->gameMode) return;
    P = player;
    L = level;
    s.ticks++;

    const unsigned int pressed = buttons & ~s.prevButtons;
    s.prevButtons = buttons;

    // Minecraft::tick
    pick(1);

    int wheel = 0;
    if (pressed & kButtonLB) wheel = 1;
    else if (pressed & kButtonRB) wheel = -1;
    if (wheel != 0) P->inventory->swapPaint(wheel);

    // ullButtonsPressed and ButtonPressed both fire on the same press
    if (pressed & kButtonRT) {
        handleMouseClick(0);
        s.lastClickTick[0] = s.ticks;
    }
    if (pressed & kButtonRT) {
        handleMouseClick(0);
        s.lastClickTick[0] = s.ticks;
    }
    handleMouseDown(0, (buttons & kButtonRT) != 0);

    const bool useHeld = (buttons & kButtonLT) != 0;
    const int ticksPerSecond = 20;
    if (P->isUsingItem()) {
        if (!useHeld) P->releaseUsingItem();
    } else if (P->abilities.instabuild) {
        // Special creative handling, for placing blocks at regular intervals
        bool didClick = creativeModeHandleMouseClick(1, useHeld);
        if (s.lastClickState == lastClick_oldRepeat) {
            if (didClick) {
                s.lastClickTick[1] = s.ticks;
            } else if (useHeld && s.ticks - s.lastClickTick[1] >= ticksPerSecond / 4) {
                handleMouseClick(1);
                s.lastClickTick[1] = s.ticks;
            }
        }
    } else {
        // A click after a release, or auto-repeat every quarter second. No
        // repeat while riding, sprinting or asleep.
        bool firstClick = (s.lastClickTick[1] == 0);
        bool autoRepeat = s.ticks - s.lastClickTick[1] >= ticksPerSecond / 4;
        if (P->isRiding() || P->isSprinting() || P->isSleeping()) autoRepeat = false;
        if (useHeld) {
            if (P->isSleeping()) s.lastClickTick[1] = s.ticks + (ticksPerSecond * 2);
            if (firstClick || autoRepeat) {
                bool wasSleeping = P->isSleeping();
                handleMouseClick(1);
                if (wasSleeping) s.lastClickTick[1] = s.ticks + (ticksPerSecond * 2);
                else s.lastClickTick[1] = s.ticks;
            }
        } else {
            s.lastClickTick[1] = 0;
        }
    }

    if (s.missTime > 0) s.missTime--;
}

namespace {

// Input and the parts of LocalPlayer::aiStep that run before Player::aiStep
struct MoveState {
    float xa = 0, ya = 0;
    bool jumping = false;
    bool sneaking = false;
    unsigned int prevButtons = 0;
    int sprintTime = 0;
    int sprintTriggerTime = 0;
    bool sprintTriggerRegisteredReturn = false;
    int jumpTriggerTime = 0;
    bool twoJumpsRegistered = false;
};

MoveState m;

const int SPRINT_DURATION = 20 * 30;

// LocalPlayer::setSprinting, plus the server's START/STOP_SPRINTING
void setSprinting(bool value) {
    P->setSprinting(value);
    m.sprintTime = value ? SPRINT_DURATION : 0;
}

} // namespace

void mcle_player_move_tick(const std::shared_ptr<ServerPlayer> &player, float stickX, float stickY,
                           unsigned int buttons) {
    if (!player) return;
    P = player;

    const unsigned int pressed = buttons & ~m.prevButtons;
    m.prevButtons = buttons;

    if (m.sprintTime > 0) {
        m.sprintTime--;
        if (m.sprintTime == 0) setSprinting(false);
    }
    if (m.sprintTriggerTime > 0) m.sprintTriggerTime--;

    bool wasJumping = m.jumping;
    float runTreshold = 0.8f;
    bool wasRunning = m.ya >= runTreshold;

    // Input::tick. Minecraft movement is the wrong way round, so x is inverted.
    m.xa = -stickX;
    m.ya = stickY;

    // In flying mode don't toggle sneaking, unless riding (sneak dismounts)
    if (!P->abilities.flying || P->riding != nullptr) {
        if (pressed & kButtonRThumb) m.sneaking = !m.sneaking;
    }
    if (m.sneaking) {
        m.xa *= 0.3f;
        m.ya *= 0.3f;
    }
    m.jumping = (buttons & kButtonA) != 0;

    // Back in LocalPlayer::aiStep
    if (P->isUsingItem() && !P->isRiding()) {
        m.xa *= 0.2f;
        m.ya *= 0.2f;
        m.sprintTriggerTime = 0;
    }
    if (m.sneaking) {
        if (P->ySlideOffset < 0.2f) P->ySlideOffset = 0.2f;
    }

    // PlayerCommandPacket START/STOP_SNEAKING
    if (P->isSneaking() != m.sneaking) P->setSneaking(m.sneaking);

    bool enoughFoodToSprint = P->getFoodData()->getFoodLevel() > FoodConstants::MAX_FOOD * FoodConstants::FOOD_SATURATION_LOW;
    // Flyers can sprint without food, for survival saves reloaded in creative
    if (P->abilities.mayfly || P->isAllowedToFly()) enoughFoodToSprint = true;

    bool forwardEnoughToTriggerSprint = m.ya >= runTreshold;
    bool forwardReturnedToDeadzone = m.ya == 0.0f;
    bool forwardEnoughToContinueSprint = m.ya >= runTreshold;

    // Sprint is two pushes forward within 7 ticks, back to the deadzone in between
    if (P->onGround && !P->isSprinting() && enoughFoodToSprint && !P->isUsingItem() && !P->hasEffect(MobEffect::blindness)) {
        if (!wasRunning && forwardEnoughToTriggerSprint) {
            if (m.sprintTriggerTime == 0) {
                m.sprintTriggerTime = 7;
                m.sprintTriggerRegisteredReturn = false;
            } else if (m.sprintTriggerRegisteredReturn) {
                setSprinting(true);
                m.sprintTriggerTime = 0;
                m.sprintTriggerRegisteredReturn = false;
            }
        } else if ((m.sprintTriggerTime > 0) && forwardReturnedToDeadzone) {
            m.sprintTriggerRegisteredReturn = true;
        }
    }
    if (P->isSneaking()) m.sprintTriggerTime = 0;
    if (P->isSprinting() && (!forwardEnoughToContinueSprint || !enoughFoodToSprint || P->isSneaking() || P->isUsingItem())) {
        setSprinting(false);
    }

    // Creative flying toggles on two jump taps, released in between
    if (!P->isSleeping() && (P->abilities.mayfly || P->isAllowedToFly())) {
        if (!wasJumping && m.jumping) {
            if (m.jumpTriggerTime == 0) {
                m.jumpTriggerTime = 7;
                m.twoJumpsRegistered = false;
            } else {
                m.twoJumpsRegistered = true;
            }
        } else if (m.jumpTriggerTime > 0 && m.twoJumpsRegistered) {
            P->abilities.flying = !P->abilities.flying;
            m.jumpTriggerTime = 0;
            m.twoJumpsRegistered = false;
            if (P->abilities.flying) m.sneaking = false;
        }
    } else if (P->abilities.flying) {
        P->abilities.flying = false;
    }
    // Player::aiStep counts this down right after
    if (m.jumpTriggerTime > 0) m.jumpTriggerTime--;

    if (P->abilities.flying) {
        // While flying the sneak button just means it's held: go down. The
        // 0.42 matches what a jump adds in jumpFromGround.
        if (buttons & kButtonRThumb) P->yd -= (0.15 + 0.42);
        if (m.jumping) P->yd += 0.15;

        // D-pad strafes along the nearest axis
        float yRotSnapped = floorf((P->yRot / 90.0f) + 0.5f) * 90.0f;
        if (buttons & kButtonDpadRight) {
            P->xd = -0.15 * cos(yRotSnapped * PI / 180);
            P->zd = -0.15 * sin(yRotSnapped * PI / 180);
        } else if (buttons & kButtonDpadLeft) {
            P->xd = 0.15 * cos(yRotSnapped * PI / 180);
            P->zd = 0.15 * sin(yRotSnapped * PI / 180);
        }
    }

    // LocalPlayer::serverAiStep
    P->xxa = m.xa;
    P->yya = m.ya;
    P->setJumping(m.jumping);
}

bool mcle_player_hit_tile(int *x, int *y, int *z, int *f) {
    std::lock_guard<std::mutex> lock(g_hitMutex);
    if (!g_hitTile) return false;
    *x = g_hitX;
    *y = g_hitY;
    *z = g_hitZ;
    *f = g_hitF;
    return true;
}
