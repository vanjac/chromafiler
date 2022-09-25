#pragma once
#include <common.h>

#include <windows.h>

namespace chromafiler {

enum SettingsPage {
    SETTINGS_GENERAL, SETTINGS_TEXT, SETTINGS_TRAY, SETTINGS_BROWSER, SETTINGS_ABOUT,
    NUM_SETTINGS_PAGES
};

void openSettingsDialog(SettingsPage page = SETTINGS_GENERAL);
bool handleSettingsDialogMessage(MSG *msg);

} // namespace
