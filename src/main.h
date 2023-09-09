#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromafiler {

const wchar_t APP_ID[] = L"chroma.file";

extern CComPtr<ItemWindow> activeWindow;

// app exits when all windows are closed
void lockProcess();
void unlockProcess();

} // namespace
