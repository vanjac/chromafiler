#pragma once
#include <common.h>

#include "ItemWindow.h"
#include <ExDisp.h>

namespace chromafiler {

CComPtr<ItemWindow> createItemWindow(ItemWindow *parent, IShellItem *item);
bool showItemWindow(IShellItem *item, IShellWindows *shellWindows, int showCmd);
CComPtr<IShellItem> resolveLink(IShellItem *linkItem);
// displays error message if item can't be found
CComPtr<IShellItem> itemFromPath(wchar_t *path);
CComPtr<IShellItem> createScratchFile(IShellItem *folder);

void debugDisplayNames(HWND hwnd, IShellItem *item);

} // namespace
