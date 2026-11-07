// The players' privileges. What the client's ClientConnection does with the
// PlayerInfoPacket and the LoginPacket's privileges, for the one player here:
// the host, network small id 0, whose client player is the server's own
// ServerPlayer.
// displayPrivilegeChanges is upstream's own, copied out by
// scripts/gen-privilege-changes.py.

#include "MCLEPlayerInfo.h"

#include <memory>
#include <string>

#include "../../../upstream/Minecraft.World/net.minecraft.world.entity.player.h"
#include "../../../upstream/Minecraft.World/PlayerInfoPacket.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"

std::shared_ptr<ServerPlayer> mcle_local_player(); // MCLEGameLoop.cpp
// MCLEGameLoop.cpp, with the Gui's lock held
void mcle_gui_add_message(const std::wstring &message, bool bIsDeathMessage);

struct MCLEPrivilegeGui {
    void addMessage(const wstring &message, int /*iPad*/) { mcle_gui_add_message(message, false); }
};

struct MCLEPrivilegeMinecraft {
    MCLEPrivilegeGui *gui;
};

// The ClientConnection members displayPrivilegeChanges uses
class MCLEPrivilegeConnection {
public:
    MCLEPrivilegeConnection() : minecraft(&m_minecraft) { m_minecraft.gui = &m_gui; }
    void displayPrivilegeChanges(shared_ptr<ServerPlayer> player, unsigned int oldPrivileges);

private:
    MCLEPrivilegeGui m_gui;
    MCLEPrivilegeMinecraft m_minecraft;
    MCLEPrivilegeMinecraft *minecraft;
    int m_userIndex = 0;
};

#include "../../../upstream/Minecraft.Client/iOS_PrivilegeChanges.inc"

namespace {

MCLEPrivilegeConnection g_connection;

} // namespace

void mcle_client_login_privileges(const std::shared_ptr<ServerPlayer> &player) {
    if (player == nullptr) return;
    // getSocket()->getSmallId(), the host's; the LoginPacket has the
    // player's index and privileges
    BYTE networkSmallId = 0;
    app.UpdatePlayerInfo(networkSmallId, player->getPlayerIndex(), player->getAllPlayerGamePrivileges());

    // Assume all privileges are on, so that the first message we see only indicates things that have been turned off
    unsigned int startingPrivileges = 0;
    Player::enableAllPlayerPrivileges(startingPrivileges, true);

    // networkPlayer->IsHost()
    Player::setPlayerGamePrivilege(startingPrivileges, Player::ePlayerGamePrivilege_HOST, 1);

    g_connection.displayPrivilegeChanges(player, startingPrivileges);
}

void mcle_client_player_info(std::shared_ptr<PlayerInfoPacket> packet) {
    unsigned int startingPrivileges = app.GetPlayerPrivileges(packet->m_networkSmallId);

    // g_NetworkManager.GetPlayerBySmallId: the only network player is the
    // host, on this system
    const bool networkPlayer = packet->m_networkSmallId == 0;

    if (networkPlayer) {
        // Some settings should always be considered on for the host player
        Player::enableAllPlayerPrivileges(startingPrivileges, true);
        Player::setPlayerGamePrivilege(startingPrivileges, Player::ePlayerGamePrivilege_HOST, 1);
    }

    // 4J Stu - Repurposed this packet for player info that we want
    app.UpdatePlayerInfo(packet->m_networkSmallId, packet->m_playerColourIndex, packet->m_playerPrivileges);

    // getEntity and localplayers[] both find the client's player, which is
    // the server's one here
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (player != nullptr && player->entityId == packet->m_entityId) {
        player->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_All, packet->m_playerPrivileges);
    }
    if (networkPlayer && player != nullptr) {
        player->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_All, packet->m_playerPrivileges);
        g_connection.displayPrivilegeChanges(player, startingPrivileges);
    }
}
