#include "ItemWindowFactory.h"
#include "FolderWindow.h"
#include "ThumbnailWindow.h"

namespace chromabrowse {

CComPtr<ItemWindow> createItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item) {
    CComPtr<ItemWindow> window;
    SFGAOF attr;
    if (SUCCEEDED(item->GetAttributes(SFGAO_FOLDER, &attr)) && (attr & SFGAO_FOLDER)) {
        window.Attach(new FolderWindow(parent, item));
    } else {
        window.Attach(new ThumbnailWindow(parent, item));
    }
    return window;
}

} // namespace
