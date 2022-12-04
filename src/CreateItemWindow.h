#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromafiler {

CComPtr<ItemWindow> createItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);
CComPtr<IShellItem> resolveLink(HWND hwnd, CComPtr<IShellItem> linkItem);
// displays error message if item can't be found
CComPtr<IShellItem> itemFromPath(wchar_t *path);
CComPtr<IShellItem> createScratchItem(CComPtr<IShellItem> folder, wchar_t *name, DWORD attr);

void debugDisplayNames(HWND hwnd, CComPtr<IShellItem> item);

} // namespace
