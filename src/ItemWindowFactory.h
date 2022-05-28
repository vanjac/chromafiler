#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromabrowse {

CComPtr<ItemWindow> createItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

} // namespace
