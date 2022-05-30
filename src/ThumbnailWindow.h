#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromabrowse {

class ThumbnailWindow : public ItemWindow {
public:
    static void init();

    ThumbnailWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

protected:
    void onPaint(PAINTSTRUCT paint) override;

private:
    const wchar_t * className() override;
};

} // namespace
