#pragma once

// "if(!app.DLCInstallProcessCompleted()) app.StartInstallDLCProcess(iPad);"
// as UIScene_MainMenu and UIScene_PauseMenu have it. The packs load over the
// next few frames. Game/MCLEDLC.cpp.
void mcle_dlc_install_if_needed(int iPad);

// app.StartInstallDLCProcess, true when it started mounting the packs
bool mcle_dlc_start_install_process(int iPad);
