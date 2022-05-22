#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromabrowse {

class ThumbnailWindow : public ItemWindow {
public:
    static void init();

    ThumbnailWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

    SIZE defaultSize() override;

private:
    const wchar_t * className() override;
};

} // namespace
