#include "ThumbnailWindow.h"
#include "RectUtils.h"
#include "GDIUtils.h"
#include <windowsx.h>

namespace chromabrowse {

const wchar_t THUMBNAIL_WINDOW_CLASS[] = L"Thumbnail Window";

void ThumbnailWindow::init() {
    WNDCLASS wndClass = createWindowClass(THUMBNAIL_WINDOW_CLASS);
    wndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wndClass.style |= CS_HREDRAW | CS_VREDRAW; // redraw whenever size changes
    RegisterClass(&wndClass);
}

ThumbnailWindow::ThumbnailWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
    : ItemWindow(parent, item) {}

const wchar_t * ThumbnailWindow::className() {
    return THUMBNAIL_WINDOW_CLASS;
}

void ThumbnailWindow::onPaint(PAINTSTRUCT paint) {
    ItemWindow::onPaint(paint);

    CComQIPtr<IShellItemImageFactory> imageFactory(item);
    if (!imageFactory)
        return;
    RECT bodyRect = windowBody();
    SIZE bodySize = rectSize(bodyRect);
    HBITMAP hBitmap;
    bool hasAlpha = true;
    // TODO this should be done asynchronously!!
    // and also the resulting bitmap should maybe be cached
    if (FAILED(imageFactory->GetImage(bodySize,
            SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &hBitmap))) {
        // no thumbnail, fallback to icon
        hasAlpha = false;
        int minDim = min(bodySize.cx, bodySize.cy);
        // SIIGBF_ICONBACKGROUND is much faster, but only works with a square rect
        // (requires Windows 8!)
        if (!checkHR(imageFactory->GetImage({minDim, minDim},
                SIIGBF_BIGGERSIZEOK | SIIGBF_ICONONLY | SIIGBF_ICONBACKGROUND, &hBitmap)))
            return;
    }
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

    if (hasAlpha)
        compositeBackground(bitmap); // TODO change color for high contrast themes

    HDC hdcMem = CreateCompatibleDC(paint.hdc);
    HBITMAP oldBitmap = SelectBitmap(hdcMem, hBitmap);
    SetStretchBltMode(paint.hdc, HALFTONE);
    StretchBlt(paint.hdc, xDest, yDest, wDest, hDest,
        hdcMem, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
    SelectBitmap(hdcMem, oldBitmap);
    DeleteDC(hdcMem);
    DeleteBitmap(hBitmap);
}

} // namespace
