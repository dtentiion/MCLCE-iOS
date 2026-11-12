// The Windows64 build's stand-ins for the Xbox's QNet, as
// Minecraft.World/x64headers/extraX64.h declares them. That header isn't
// one this port includes, so they're here.
//
// What's behind IQNet and IQNetPlayer on Windows64 is in Extrax64Stubs.cpp,
// and under those Windows64/Network/WinsockNetLayer, which is LCEMP's:
// https://github.com/LCEMP/LCEMP

#pragma once

#ifdef __cplusplus

class INVITE_INFO;

class IQNetPlayer
{
public:
	BYTE GetSmallId();
	void SendData(IQNetPlayer *player, const void *pvData, DWORD dwDataSize, DWORD dwFlags);
	bool IsSameSystem(IQNetPlayer *player);
	DWORD GetSendQueueSize( IQNetPlayer *player, DWORD dwFlags );
	DWORD GetCurrentRtt();
	bool IsHost();
	bool IsGuest();
	bool IsLocal();
	PlayerUID GetXuid();
	LPCWSTR GetGamertag();
	int GetSessionIndex();
	bool IsTalking();
	bool IsMutedByLocalUser(DWORD dwUserIndex);
	bool HasVoice();
	bool HasCamera();
	int GetUserIndex();
	void SetCustomDataValue(ULONG_PTR ulpCustomDataValue);
	ULONG_PTR GetCustomDataValue();

    BYTE m_smallId;
    bool m_isRemote;
    bool m_isHostPlayer;
    PlayerUID m_resolvedXuid;
    wchar_t m_gamertag[32];
private:
	ULONG_PTR m_customData;
};

void Win64_SetupRemoteQNetPlayer(IQNetPlayer* player, BYTE smallId, bool isHost, bool isLocal);

const int QNET_GETSENDQUEUESIZE_SECONDARY_TYPE = 0;
const int QNET_GETSENDQUEUESIZE_MESSAGES = 0;
const int QNET_GETSENDQUEUESIZE_BYTES = 0;

typedef struct {
    BYTE bFlags;
    BYTE bReserved;
    WORD cProbesXmit;
    WORD cProbesRecv;
    WORD cbData;
    BYTE *pbData;
    WORD wRttMinInMsecs;
    WORD wRttMedInMsecs;
    DWORD dwUpBitsPerSec;
    DWORD dwDnBitsPerSec;
} XNQOSINFO;

typedef struct {
    UINT cxnqos;
    UINT cxnqosPending;
    XNQOSINFO axnqosinfo[1];
} XNQOS;

typedef struct _XOVERLAPPED {
} XOVERLAPPED, *PXOVERLAPPED;

typedef struct _XSESSION_SEARCHRESULT {
} XSESSION_SEARCHRESULT, *PXSESSION_SEARCHRESULT;

typedef struct _XSESSION_SEARCHRESULT_HEADER {
    DWORD dwSearchResults;
    XSESSION_SEARCHRESULT *pResults;
} XSESSION_SEARCHRESULT_HEADER, *PXSESSION_SEARCHRESULT_HEADER;

class IQNetCallbacks
{
};

class IQNetGameSearch
{
};

typedef enum _QNET_STATE
{
    QNET_STATE_IDLE,
    QNET_STATE_SESSION_HOSTING,
    QNET_STATE_SESSION_JOINING,
    QNET_STATE_GAME_LOBBY,
    QNET_STATE_SESSION_REGISTERING,
    QNET_STATE_SESSION_STARTING,
    QNET_STATE_GAME_PLAY,
    QNET_STATE_SESSION_ENDING,
    QNET_STATE_SESSION_LEAVING,
    QNET_STATE_SESSION_DELETING
} QNET_STATE, * PQNET_STATE;

class IQNet
{
public:
	HRESULT AddLocalPlayerByUserIndex(DWORD dwUserIndex);
	IQNetPlayer *GetHostPlayer();
	IQNetPlayer *GetLocalPlayerByUserIndex(DWORD dwUserIndex);
	IQNetPlayer *GetPlayerByIndex(DWORD dwPlayerIndex);
	IQNetPlayer *GetPlayerBySmallId(BYTE SmallId);
	IQNetPlayer *GetPlayerByXuid(PlayerUID xuid);
	DWORD GetPlayerCount();
	QNET_STATE GetState();
	bool IsHost();
	HRESULT JoinGameFromInviteInfo(DWORD dwUserIndex, DWORD dwUserMask, const INVITE_INFO *pInviteInfo);
	void HostGame();
    void ClientJoinGame();
    void EndGame();

    static IQNetPlayer m_player[MINECRAFT_NET_MAX_PLAYERS];
    static DWORD s_playerCount;
    static bool s_isHosting;
};

const int QNET_SENDDATA_LOW_PRIORITY = 0;
const int QNET_SENDDATA_SECONDARY = 0;
const int QNET_SENDDATA_RELIABLE = 0;
const int QNET_SENDDATA_SEQUENTIAL = 0;

struct XRNM_SEND_BUFFER
{
	DWORD dwDataSize;
	byte *pbyData;
};

#endif // __cplusplus
