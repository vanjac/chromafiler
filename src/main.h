#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromafiler {

extern CComPtr<ItemWindow> activeWindow;

// app exits when all windows are closed
void lockProcess();
void unlockProcess();

} // namespace
