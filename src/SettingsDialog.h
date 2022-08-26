#pragma once
#include <common.h>

#include <windows.h>

namespace chromafile {

enum SettingsPage {
    SETTINGS_GENERAL, SETTINGS_TRAY, SETTINGS_ABOUT, NUM_SETTINGS_PAGES
};

void openSettingsDialog(SettingsPage page = SETTINGS_GENERAL);
bool handleSettingsDialogMessage(MSG *msg);

} // namespace
