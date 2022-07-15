#pragma once
#include <common.h>

#include <windows.h>

namespace chromabrowse {

void openSettingsDialog();
bool handleSettingsDialogMessage(MSG *msg);

} // namespace
