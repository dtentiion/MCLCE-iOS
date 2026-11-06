#pragma once

// What DLCManager.cpp gets on iOS in place of the Iggy UI header
// (scripts/patch-dlc-ios.py).

// A .pck keeps its strings in DLC_FILE_PARAM / DLC_FILE_DETAILS as UTF-16,
// laid out for Windows' 2 byte WCHAR. wchar_t is 4 bytes here, so the file
// is rewritten with this build's structs before DLCManager reads it. Takes
// the buffer (new[]) and gives back the one to use in its place.
// Game/MCLEDLC.cpp.
PBYTE mcle_dlc_widen_pck(PBYTE pbData, DWORD &dwLength);

// UIController::RequestErrorMessage for the corrupt DLC message. There's no
// message box to show it in from here.
struct McleDLCManagerUI {
    template <class... A> C4JStorage::EMessageResult RequestErrorMessage(A...) {
        return C4JStorage::EMessage_ResultAccept;
    }
};
static McleDLCManagerUI ui;
