#pragma once
#include <common.h>

#include "ItemWindow.h"
#include <ExDisp.h>

namespace chromafiler {

CComPtr<ItemWindow> createItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);
bool showItemWindow(CComPtr<IShellItem> item, CComPtr<IShellWindows> shellWindows, int showCmd);
CComPtr<IShellItem> resolveLink(CComPtr<IShellItem> linkItem);
// displays error message if item can't be found
CComPtr<IShellItem> itemFromPath(wchar_t *path);
CComPtr<IShellItem> createScratchFile(CComPtr<IShellItem> folder);

void debugDisplayNames(HWND hwnd, CComPtr<IShellItem> item);

} // namespace
