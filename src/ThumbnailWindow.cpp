#include "ThumbnailWindow.h"

namespace chromabrowse {

const wchar_t *THUMBNAIL_WINDOW_CLASS = L"Thumbnail Window";

void ThumbnailWindow::init() {
    WNDCLASS wndClass = createWindowClass(THUMBNAIL_WINDOW_CLASS);
    wndClass.hbrBackground = (HBRUSH)(COLOR_HIGHLIGHT + 1);
    RegisterClass(&wndClass);
}

ThumbnailWindow::ThumbnailWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
    : ItemWindow(parent, item)
{}

const wchar_t * ThumbnailWindow::className() {
    return THUMBNAIL_WINDOW_CLASS;
}

SIZE ThumbnailWindow::defaultSize() {
    return {450, 450};
}

} // namespace
