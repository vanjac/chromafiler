#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromabrowse {

// TODO: ATL bugs with returning CComPtrs?
CComPtr<ItemWindow> createItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);
CComPtr<IShellItem> resolveLink(HWND hwnd, CComPtr<IShellItem> linkItem);

} // namespace
