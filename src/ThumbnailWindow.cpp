#include "ThumbnailWindow.h"
#include "RectUtils.h"
#include "GDIUtils.h"
#include <windowsx.h>

namespace chromabrowse {

const wchar_t THUMBNAIL_WINDOW_CLASS[] = L"Thumbnail Window";

void ThumbnailWindow::init() {
    WNDCLASS wndClass = createWindowClass(THUMBNAIL_WINDOW_CLASS);
    wndClass.style |= CS_HREDRAW | CS_VREDRAW; // redraw whenever size changes
    RegisterClass(&wndClass);
}

ThumbnailWindow::ThumbnailWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
    : ItemWindow(parent, item) {}

ThumbnailWindow::~ThumbnailWindow() {
    if (thumbnailBitmap)
        DeleteBitmap(thumbnailBitmap);
}

const wchar_t * ThumbnailWindow::className() {
    return THUMBNAIL_WINDOW_CLASS;
}

void ThumbnailWindow::onCreate() {
    ItemWindow::onCreate();
    thumbnailThread = new ThumbnailThread(item, hwnd);
    thumbnailThread->start();
}

void ThumbnailWindow::onDestroy() {
    ItemWindow::onDestroy();
    thumbnailThread->stop();
}

void ThumbnailWindow::onSize(int width, int height) {
    ItemWindow::onSize(width, height);
    RECT bodyRect = windowBody();
    SIZE bodySize = rectSize(bodyRect);
    thumbnailThread->requestThumbnail(bodySize);
}

void ThumbnailWindow::onItemChanged() {
    ItemWindow::onItemChanged();
    thumbnailThread->stop();
    thumbnailThread = new ThumbnailThread(item, hwnd);
    thumbnailThread->start();
}

void ThumbnailWindow::refresh() {
    RECT bodyRect = windowBody();
    SIZE bodySize = rectSize(bodyRect);
    thumbnailThread->requestThumbnail(bodySize);
}

LRESULT ThumbnailWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == MSG_SET_THUMBNAIL_BITMAP) {
        if (thumbnailBitmap)
            DeleteBitmap(thumbnailBitmap);
        thumbnailBitmap = (HBITMAP)lParam;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return ItemWindow::handleMessage(message, wParam, lParam);
}

void ThumbnailWindow::onPaint(PAINTSTRUCT paint) {
    ItemWindow::onPaint(paint);

    if (!thumbnailBitmap)
        return;
    RECT bodyRect = windowBody();
    SIZE bodySize = rectSize(bodyRect);

    BITMAP bitmap;
    GetObject(thumbnailBitmap, sizeof(bitmap), &bitmap);
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
    HBITMAP oldBitmap = SelectBitmap(hdcMem, thumbnailBitmap);
    SetStretchBltMode(paint.hdc, HALFTONE);
    StretchBlt(paint.hdc, xDest, yDest, wDest, hDest,
        hdcMem, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
    SelectBitmap(hdcMem, oldBitmap);
    DeleteDC(hdcMem);
}

ThumbnailWindow::ThumbnailThread::ThumbnailThread(CComPtr<IShellItem> item, HWND callbackWindow)
        : callbackWindow(callbackWindow) {
    checkHR(SHGetIDListFromObject(item, &itemIDList));
    requestThumbnailEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    InitializeCriticalSectionAndSpinCount(&requestThumbnailSection, 4000);
}

ThumbnailWindow::ThumbnailThread::~ThumbnailThread() {
    CloseHandle(requestThumbnailEvent);
    DeleteCriticalSection(&requestThumbnailSection);
}

void ThumbnailWindow::ThumbnailThread::requestThumbnail(SIZE size) {
    EnterCriticalSection(&requestThumbnailSection);
    requestedSize = size;
    SetEvent(requestThumbnailEvent);
    LeaveCriticalSection(&requestThumbnailSection);
}

void ThumbnailWindow::ThumbnailThread::run() {
    CComPtr<IShellItemImageFactory> imageFactory;
    if (!itemIDList || !checkHR(SHCreateItemFromIDList(itemIDList, IID_PPV_ARGS(&imageFactory))))
        return;
    itemIDList.Free();

    HANDLE waitObjects[] = {requestThumbnailEvent, stopEvent};
    int event;
    while ((event = WaitForMultipleObjects(
            _countof(waitObjects), waitObjects, FALSE, INFINITE)) != WAIT_FAILED) {
        if (event == WAIT_OBJECT_0) {
            SIZE size;
            EnterCriticalSection(&requestThumbnailSection);
            size = requestedSize;
            ResetEvent(requestThumbnailEvent);
            LeaveCriticalSection(&requestThumbnailSection);

            HBITMAP hBitmap;
            if (!checkHR(imageFactory->GetImage(size, SIIGBF_BIGGERSIZEOK, &hBitmap)))
                return;
            BITMAP bitmap;
            GetObject(hBitmap, sizeof(bitmap), &bitmap);
            compositeBackground(bitmap); // TODO change color for high contrast themes

            // ensure the window is not closed before the message is posted
            EnterCriticalSection(&stopSection);
            if (isStopped()) {
                DeleteBitmap(hBitmap);
            } else {
                PostMessage(callbackWindow, MSG_SET_THUMBNAIL_BITMAP, 0, (LPARAM)hBitmap);
            }
            LeaveCriticalSection(&stopSection);
        } else if (event == WAIT_OBJECT_0 + 1) {
            return; // stop
        }
    }
    return;
}

} // namespace
