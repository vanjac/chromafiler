#pragma once
#include <common.h>

#include <windows.h>

namespace chromafile {

void openSettingsDialog();
bool handleSettingsDialogMessage(MSG *msg);

} // namespace
