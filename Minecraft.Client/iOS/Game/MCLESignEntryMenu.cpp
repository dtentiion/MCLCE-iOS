// The sign's text entry. SignItem::useOn has the server player open it
// (ServerPlayer::openTextEdit), whose TileEditorOpenPacket comes here as
// ClientConnection::handleTileEditorOpen and LocalPlayer::openTextEdit.
// SignEntryScene stands in for UIScene_SignEntryMenu, its lines are typed
// on UIScene_Keyboard (MCLEKeyboard.cpp), and Done sends the
// SignUpdatePacket to the server's PlayerConnection::handleSignUpdate.

#include "MCLESignEntryMenu.h"
#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"
#include "MCLEKeyboard.h"

#include <memory>
#include <mutex>
#include <string>

// SignTileEntity.h names 4J_Input's string verify result in a callback
// that isn't used here; only the world build has the struct
struct _STRING_VERIFY_RESPONSE;
typedef struct _STRING_VERIFY_RESPONSE STRING_VERIFY_RESPONSE;

#include "../../../upstream/Minecraft.World/net.minecraft.world.level.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.level.tile.entity.h"
#include "../../../upstream/Minecraft.World/SignUpdatePacket.h"
#include "../../../upstream/Minecraft.World/TileEditorOpenPacket.h"
#include "../../../upstream/Minecraft.Client/MinecraftServer.h"
#include "../../../upstream/Minecraft.Client/ServerLevel.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"

namespace {

// PlayerConnection::handleSignUpdate, on the server's own player
void handleSignUpdate(const std::shared_ptr<ServerPlayer> &player, std::shared_ptr<SignUpdatePacket> packet) {
    player->resetLastActionTime();
    app.DebugPrintf("PlayerConnection::handleSignUpdate\n");

    MinecraftServer *server = MinecraftServer::getInstance();
    if (!server) return;
    ServerLevel *level = server->getLevel(player->dimension);
    if (!level) return;
    if (level->hasChunkAt(packet->x, packet->y, packet->z)) {
        std::shared_ptr<TileEntity> te = level->getTileEntity(packet->x, packet->y, packet->z);

        if (dynamic_pointer_cast<SignTileEntity>(te) != nullptr) {
            std::shared_ptr<SignTileEntity> ste = dynamic_pointer_cast<SignTileEntity>(te);
            if (!ste->isEditable() || ste->getPlayerWhoMayEdit() != player) {
                server->warn(L"Player " + player->getName() + L" just tried to change non-editable sign");
                return;
            }
        }

        // 4J-JEV: Changed to allow characters to display as a [].
        if (dynamic_pointer_cast<SignTileEntity>(te) != nullptr) {
            int x = packet->x;
            int y = packet->y;
            int z = packet->z;
            std::shared_ptr<SignTileEntity> ste = dynamic_pointer_cast<SignTileEntity>(te);
            for (int i = 0; i < 4; i++) {
                wstring lineText = packet->lines[i].substr(0, 15);
                ste->SetMessage(i, lineText);
            }
            ste->SetVerified(false);
            ste->setChanged();
            level->sendTileUpdated(x, y, z);
        }
    }
}

// UIScene_SignEntryMenu over the client's copy of the sign, which here is
// the server's own sign. The client sets the lines on its copy as it sends
// them and wipes them when backed out of; the server's sign only changes
// through handleSignUpdate, which checks this player may edit it, so the
// lines just go in the packet and the wipe is left out (a new sign is
// blank).
class SignEntryScene : public MCLEMenuScene {
public:
    SignEntryScene(int iPad, const std::shared_ptr<SignTileEntity> &sign)
        : m_buttonConfirm("Confirm"), m_labelMessage("Message"),
          m_textInputLines{MCLEUIControl("Line1"), MCLEUIControl("Line2"), MCLEUIControl("Line3"),
                           MCLEUIControl("Line4")} {
        m_iPad = iPad;
        m_sign = sign;

        m_bConfirmed = false;
        m_bIgnoreInput = false;
        m_iSignCursorFrame = 0;

        m_buttonConfirm.init(app.GetString(IDS_DONE), eControl_Confirm);
        m_labelMessage.init(app.GetString(IDS_EDIT_SIGN_MESSAGE));

        for (unsigned int i = 0; i < 4; ++i) {
            m_textInputLines[i].init(m_sign->GetMessage(i), i);
        }

        gainFocus();
    }

    ~SignEntryScene() override { m_sign->SetSelectedLine(-1); }

    // parentLayer->addComponent(iPad, eUIComponent_MenuBackground)
    bool hasMenuBackground() override { return true; }

    const char *movieName() override { return "SignEntryMenu1080.swf"; }

    // UIScene::reloadMovie: the controls go back on the movie, and the
    // focus where it was
    void reloadMovie() override {
        m_buttonConfirm.ReInit();
        m_labelMessage.init(m_labelMessage.label);
        for (MCLEUIControl &line : m_textInputLines) line.ReInit();
        mcle_ui_call("", "SetFocus", {mcle_ui_num(m_iFocusControl)});
        updateTooltips();
    }

    void tick(double now) override {
        // UIScene::tick sends the labels that changed
        m_buttonConfirm.tick();
        m_labelMessage.tick();
        for (MCLEUIControl &line : m_textInputLines) line.tick();

        // Blinking > text < cursor on the 3D sign
        m_iSignCursorFrame++;
        if (m_iSignCursorFrame / 6 % 2 == 0) {
            int focusedLine = -1;
            for (int i = eControl_Line1; i <= eControl_Line4; i++) {
                if (controlHasFocus(i)) {
                    focusedLine = i;
                    break;
                }
            }
            m_sign->SetSelectedLine(focusedLine);
        } else {
            m_sign->SetSelectedLine(-1);
        }

        if (m_bConfirmed) {
            m_bConfirmed = false;

            // Set the sign text here so we on;y call the verify once it has been set, not while we're typing in to it
            wstring lines[MAX_SIGN_LINES];
            for (int i = 0; i < 4; i++) {
                lines[i] = m_textInputLines[i].getLabel();
            }

            // need to send the new data
            std::shared_ptr<ServerPlayer> player = mcle_local_player();
            if (player != nullptr) {
                handleSignUpdate(player, std::make_shared<SignUpdatePacket>(m_sign->x, m_sign->y, m_sign->z,
                                                                            m_sign->IsVerified(),
                                                                            m_sign->IsCensored(), lines));
            }
            mcleContainerUI.CloseUIScenes(m_iPad);
        }
    }

    void handleInput(int key, bool repeat, bool pressed, bool released) override {
        if (m_bConfirmed || m_bIgnoreInput) return;

        mcleContainerUI.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

        switch (key) {
        case ACTION_MENU_CANCEL:
            if (pressed) {
                navigateBack();
                mcleContainerUI.PlayUISFX(eSFX_Back);
            }
            break;
        case ACTION_MENU_OK:
            sendInputToMovie(key, repeat, pressed, released);
            break;
        case ACTION_MENU_UP:
        case ACTION_MENU_DOWN:
            sendInputToMovie(key, repeat, pressed, released);
            break;
        }
    }

    void handlePress(int controlId, int childId) override {
        switch (controlId) {
        case eControl_Confirm:
            m_bConfirmed = true;
            break;
        case eControl_Line1:
        case eControl_Line2:
        case eControl_Line3:
        case eControl_Line4: {
            m_iEditingLine = controlId;
            // Windows64 without the keyboard and mouse: UIScene_Keyboard
            m_bIgnoreInput = true;
            MCLEKeyboardInitData kbData;
            kbData.title = app.GetString(IDS_SIGN_TITLE);
            kbData.defaultText = m_textInputLines[m_iEditingLine].getLabel();
            kbData.maxChars = 15;
            kbData.callback = &SignEntryScene::KeyboardCompleteCallback;
            kbData.lpParam = this;
            mcle_keyboard_navigate_to(m_iPad, kbData);
            break;
        }
        }
    }

    // InputManager.DestroyKeyboard is only on PS3, PS4 and Xbox One
    void handleDestroy() override {}

private:
    enum EControls {
        // Lines should be 0-3
        eControl_Line1,
        eControl_Line2,
        eControl_Line3,
        eControl_Line4,
        eControl_Confirm
    };

    std::shared_ptr<SignTileEntity> m_sign;
    int m_iEditingLine = 0;
    bool m_bConfirmed;
    bool m_bIgnoreInput;
    int m_iSignCursorFrame;

    MCLEUIControl m_buttonConfirm;
    MCLEUILabel m_labelMessage;
    MCLEUIControl m_textInputLines[4];

    void updateTooltips() override { mcleContainerUI.SetTooltips(m_iPad, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK); }

    static int KeyboardCompleteCallback(LPVOID lpParam, bool bRes) {
        const auto pClass = static_cast<SignEntryScene *>(lpParam);
        pClass->m_bIgnoreInput = false;
        if (bRes) {
            // Win64_GetKeyboardText
            pClass->m_textInputLines[pClass->m_iEditingLine].setLabel(mcle_keyboard_text());
        }
        return 0;
    }
};

// The TileEditorOpenPacket the sim thread passed on
std::mutex g_editorMutex;
bool g_editorPacket = false;
int g_editorType = 0;
int g_editorX = 0, g_editorY = 0, g_editorZ = 0;

// The sign LocalPlayer::openTextEdit loads the menu for
std::shared_ptr<SignTileEntity> g_signToOpen;

// LocalPlayer::openTextEdit. A command block would load its own menu
// (app.LoadCommandBlockMenu), which isn't here; TU19's server only sends
// the packet for signs.
bool openTextEdit(const std::shared_ptr<TileEntity> &tileEntity) {
    if (tileEntity->GetType() == eTYPE_SIGNTILEENTITY) {
        g_signToOpen = dynamic_pointer_cast<SignTileEntity>(tileEntity);
        return g_signToOpen != nullptr;
    }
    return false;
}

} // namespace

void mcle_sign_entry_tile_editor_open(int editorType, int x, int y, int z) {
    std::lock_guard<std::mutex> lock(g_editorMutex);
    g_editorPacket = true;
    g_editorType = editorType;
    g_editorX = x;
    g_editorY = y;
    g_editorZ = z;
}

// ClientConnection::handleTileEditorOpen. The client's level is the
// player's level on the server here.
bool mcle_sign_entry_take_request() {
    int editorType, x, y, z;
    {
        std::lock_guard<std::mutex> lock(g_editorMutex);
        if (!g_editorPacket) return false;
        g_editorPacket = false;
        editorType = g_editorType;
        x = g_editorX;
        y = g_editorY;
        z = g_editorZ;
    }
    // A packet that came in while a menu was up is dropped, like the
    // container menus' presses
    if (mcle_container_is_open()) return false;

    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player || !player->level) return false;
    Level *level = player->level;
    std::shared_ptr<TileEntity> tileEntity = level->getTileEntity(x, y, z);
    if (tileEntity != nullptr) {
        return openTextEdit(tileEntity);
    } else if (editorType == TileEditorOpenPacket::SIGN) {
        std::shared_ptr<SignTileEntity> localSignDummy = std::make_shared<SignTileEntity>();
        localSignDummy->setLevel(level);
        localSignDummy->x = x;
        localSignDummy->y = y;
        localSignDummy->z = z;
        return openTextEdit(localSignDummy);
    }
    return false;
}

// CMinecraftApp::LoadSignEntryMenu, and the press LocalPlayer::openTextEdit
// plays once it's open
bool mcle_sign_entry_open() {
    std::shared_ptr<SignTileEntity> sign;
    sign.swap(g_signToOpen);
    if (!sign || mcle_container_is_open()) return false;
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    if (!mcle_local_player()) return false;
    mcle_container_prepare();
    // NavigateToScene sets the menu displayed before it makes the scene
    mcle_game_set_menu_displayed(true);
    mcle_container_begin(new SignEntryScene(0, sign));
    mcleContainerUI.PlayUISFX(eSFX_Press);
    return true;
}
