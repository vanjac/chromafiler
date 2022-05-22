#include "ThumbnailWindow.h"

namespace chromabrowse {

const wchar_t *THUMBNAIL_WINDOW_CLASS = L"Thumbnail Window";

void ThumbnailWindow::init() {
    WNDCLASS wndClass = createWindowClass(THUMBNAIL_WINDOW_CLASS);
    wndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wndClass.style |= CS_HREDRAW | CS_VREDRAW; // redraw whenever size changes
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

void ThumbnailWindow::onPaint(PAINTSTRUCT paint) {
    ItemWindow::onPaint(paint);

    CComQIPtr<IShellItemImageFactory> imageFactory(item);
    if (!imageFactory)
        return;
    RECT bodyRect = windowBody();
    SIZE bodySize = {bodyRect.right - bodyRect.left, bodyRect.bottom - bodyRect.top};
    HBITMAP hBitmap;
    // TODO this should be done asynchronously!!
    // and also the resulting bitmap should be cached
    if (FAILED(imageFactory->GetImage(bodySize, SIIGBF_BIGGERSIZEOK, &hBitmap)))
        return;
    BITMAP bitmap;
    GetObject(hBitmap, sizeof(bitmap), &bitmap);
    debugPrintf(L"Scale thumbnail from %dx%d to %dx%d\n",
        bitmap.bmWidth, bitmap.bmHeight, bodySize.cx, bodySize.cy);
    int xDest, yDest, wDest, hDest;
    float wScale = (float)bodySize.cx / bitmap.bmWidth;
    float hScale = (float)bodySize.cy / bitmap.bmHeight;
    if (wScale < hScale) {
        xDest = bodyRect.left;
        wDest = bodySize.cx;
        yDest = bodyRect.top + (int)((bodySize.cy - wScale*bitmap.bmHeight) / 2);
        hDest = (int)(wScale*bitmap.bmHeight);
    } else {
        xDest = bodyRect.left + (int)((bodySize.cx - hScale*bitmap.bmWidth) / 2);
        wDest = (int)(hScale*bitmap.bmWidth);
        yDest = bodyRect.top;
        hDest = bodySize.cy;
    }

    HDC hdcMem = CreateCompatibleDC(paint.hdc);
    HGDIOBJ oldBitmap = SelectObject(hdcMem, hBitmap);
    AlphaBlend(paint.hdc, xDest, yDest, wDest, hDest,
        hdcMem, 0, 0, bitmap.bmWidth, bitmap.bmHeight,
        {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA});
    SelectObject(hdcMem, oldBitmap);
    DeleteDC(hdcMem);
    DeleteObject(hBitmap);
}

} // namespace
