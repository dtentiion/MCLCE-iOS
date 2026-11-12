// The game's network manager, for a world this device hosts.
//
// g_NetworkManager is upstream's CGameNetworkManager. Most of its
// Common/Network/GameNetworkManager.cpp is the client's side (starting a
// client connection, joining, invites, the progress screens) and doesn't
// build here, where the player is the server's own. So
// scripts/gen-network-manager.py takes the members a host uses into
// iOS_GameNetworkManager.inc as they are, and the few that had something of
// the client's in them are below, each saying what was left out.
//
// Under it is upstream's CPlatformNetworkManagerStub, the Windows64 build's
// IQNet (Extrax64Stubs.cpp, taken into iOS_QNet.inc) and
// Windows64/Network/WinsockNetLayer, which is LCEMP's:
// https://github.com/LCEMP/LCEMP

#include <string>
#include <vector>

#include "../../../upstream/Minecraft.World/Socket.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/MinecraftServer.h"
#include "../../../upstream/Minecraft.Client/PlayerList.h"
#include "../../../upstream/Minecraft.Client/PlayerConnection.h"
#include "../../../upstream/Minecraft.Client/LevelRenderer.h"
#include "../../../upstream/Minecraft.Client/TexturePackRepository.h"
#include "../../../upstream/Minecraft.Client/TexturePack.h"
#include "../../../upstream/Minecraft.Client/Common/Network/PlatformNetworkManagerStub.h"
#include "../../../upstream/Minecraft.Client/Xbox/Network/NetworkPlayerXbox.h"
#include "../../../upstream/Minecraft.Client/Windows64/Network/WinsockNetLayer.h"
#include "../../../upstream/Minecraft.Client/Windows64/Windows64_Xuid.h"

// Windows64_Minecraft.cpp's name for the player, which IQNet puts in slot 0
wchar_t g_Win64UsernameW[17] = { 0 };

#include "../../../upstream/Minecraft.Client/iOS_QNet.inc"

// Global instance
CGameNetworkManager g_NetworkManager;

CPlatformNetworkManager *CGameNetworkManager::s_pPlatformNetworkManager;

int64_t CGameNetworkManager::messageQueue[512];
int64_t CGameNetworkManager::byteQueue[512];
int CGameNetworkManager::messageQueuePos = 0;

#include "../../../upstream/Minecraft.Client/iOS_GameNetworkManager.inc"

// The platform manager is always the stub one here, as on Windows64
void CGameNetworkManager::Initialise()
{
	ServerStoppedCreate( false );
	ServerReadyCreate( false );
	int flagIndexSize = LevelRenderer::getGlobalChunkCount() / (Level::maxBuildHeight / 16);		// dividing here by number of renderer chunks in one column
	s_pPlatformNetworkManager = new CPlatformNetworkManagerStub();
	s_pPlatformNetworkManager->Initialise( this, flagIndexSize );
	m_bNetworkThreadRunning = false;
	m_bInitialised = true;
}

// Without Minecraft::clearConnectionFailed, the client's connection errors
void CGameNetworkManager::HostGame(int localUsersMask, bool bOnlineGame, bool bIsPrivate, unsigned char publicSlots, unsigned char privateSlots)
{
	s_pPlatformNetworkManager->HostGame( localUsersMask, bOnlineGame, bIsPrivate, publicSlots, privateSlots );
}

// Without the client Gui's clearMessages
bool CGameNetworkManager::LeaveGame(bool bMigrateHost)
{
	return s_pPlatformNetworkManager->LeaveGame( bMigrateHost );
}

// The host's part. StartNetworkGame, which starts the server thread and the
// local players' client connections, is what the game loop does itself.
bool CGameNetworkManager::_RunNetworkGame(LPVOID lpParameter)
{
	bool success = s_pPlatformNetworkManager->_RunNetworkGame();
	if(!success) return false;

	if( g_NetworkManager.IsLeavingGame() ) return false;

	app.SetGameStarted(true);

	return success;
}

// The server thread is never waited for from the thread that draws here, so
// there's no frame to keep going while it stops
void CGameNetworkManager::ServerStoppedWait()
{
	m_hServerStoppedEvent->WaitForSignal(INFINITE);
}

// The host's part: a socket for the player who has connected, handed to the
// server once the game is going. The rest is a local player's client
// connection.
void CGameNetworkManager::CreateSocket( INetworkPlayer *pNetworkPlayer, bool localPlayer )
{
	if( localPlayer ) return;

	Socket *socket = new Socket( pNetworkPlayer, g_NetworkManager.IsHost(), g_NetworkManager.IsHost() && localPlayer );
	pNetworkPlayer->SetSocket( socket );

	// 4J Stu - May be other states we want to accept aswell
	// Add this user to the game server if the game is started already
	if( g_NetworkManager.IsHost() && g_NetworkManager.IsInGameplay() )
	{
		Socket::addIncomingSocket(socket);
	}
}

// Rich presence and telemetry, neither of which there is here
void CGameNetworkManager::PlayerJoining( INetworkPlayer *pNetworkPlayer )
{
}

void CGameNetworkManager::PlayerLeaving( INetworkPlayer *pNetworkPlayer )
{
}

// The local player's connection: null, or the zeroed stand-in its respawn
// and crafting run with. A player who joined has a Connection behind theirs.
bool mcle_connection_is_local(const PlayerConnection *connection)
{
	return connection == nullptr || connection->connection == nullptr;
}

// What Windows64_Minecraft.cpp does once at startup: the manager, the
// player slots with the player's name in the first, and the net layer
extern "C" const wchar_t *mcle_username(void); // Game/MCLEStrings.cpp

void mcle_network_init(void)
{
	static bool done = false;
	if (done) return;
	done = true;

	const wchar_t *name = mcle_username();
	int n = 0;
	for (; n < 16 && name && name[n] != 0; ++n) g_Win64UsernameW[n] = name[n];
	g_Win64UsernameW[n] = 0;

	g_NetworkManager.Initialise();

	for (int i = 0; i < MINECRAFT_NET_MAX_PLAYERS; i++)
	{
		IQNet::m_player[i].m_smallId = static_cast<BYTE>(i);
		IQNet::m_player[i].m_isRemote = false;
		IQNet::m_player[i].m_isHostPlayer = (i == 0);
		swprintf_s(IQNet::m_player[i].m_gamertag, 32, L"Player%d", i);
	}
	wcscpy_s(IQNet::m_player[0].m_gamertag, 32, g_Win64UsernameW);

	WinsockNetLayer::Initialize();
}
