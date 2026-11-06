// Chat for the local player. Its ServerPlayer has no client connection, so
// the chat packets sent or broadcast to it come here, to a copy of
// ClientConnection::handleChat (scripts/gen-chat-handler.py) that puts the
// message on the Gui the same way.

#include <atomic>
#include <memory>
#include <regex>
#include <string>

#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/Common/App_enums.h"
#include "../../../upstream/Minecraft.World/ChatPacket.h"
#include "../../../upstream/Minecraft.World/StringHelpers.h"

// Game/MCLEHowToPlay.cpp's copies of the CMinecraftApp text helpers
std::wstring mcle_escape_html_string(const std::wstring &desc);
std::wstring mcle_format_chat_message(const std::wstring &desc);
std::wstring mcle_entity_name(eINSTANCEOF type);
// Game/MCLEGameLoop.cpp, with the Gui's lock held
void mcle_gui_add_message(const std::wstring &message, bool bIsDeathMessage);
void mcle_gui_set_action_bar_message(const std::wstring &message);

// CMinecraftApp::SetGlobalXuiAction. The only action chat raises is the
// lava near spawn warning, which the host never gets (mayInteract lets the
// host build anywhere), so it is only kept.
static std::atomic<int> s_globalXuiAction{eAppAction_Idle};
void mcle_set_global_xui_action(eXuiAction action) {
    s_globalXuiAction = static_cast<int>(action);
}

struct MCLEChatGui {
    void addMessage(const wstring &message, int /*iPad*/, bool bIsDeathMessage = false) {
        mcle_gui_add_message(message, bIsDeathMessage);
    }
    void setActionBarMessage(const wstring &message) {
        mcle_gui_set_action_bar_message(message);
    }
};

struct MCLEChatMinecraft {
    MCLEChatGui *gui;
};

// The ClientConnection members handleChat uses
class MCLEChatConnection {
public:
    MCLEChatConnection() : minecraft(&m_minecraft) { m_minecraft.gui = &m_gui; }
    void handleChat(shared_ptr<ChatPacket> packet);

private:
    // ClientConnection::GetDisplayNameByGamertag off Durango
    wstring GetDisplayNameByGamertag(wstring gamertag) { return gamertag; }

    MCLEChatGui m_gui;
    MCLEChatMinecraft m_minecraft;
    MCLEChatMinecraft *minecraft;
    int m_userIndex = 0;
};

#include "../../../upstream/Minecraft.Client/iOS_HandleChat.inc"

void mcle_handle_chat_packet(shared_ptr<ChatPacket> packet) {
    static MCLEChatConnection s_connection;
    s_connection.handleChat(packet);
}
