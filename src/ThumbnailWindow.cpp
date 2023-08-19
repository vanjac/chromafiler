#include "ThumbnailWindow.h"
#include "GeomUtils.h"
#include "GDIUtils.h"
#include "WinUtils.h"
#include <windowsx.h>

namespace chromafiler {

const wchar_t THUMBNAIL_WINDOW_CLASS[] = L"ChromaFile Thumbnail";

void ThumbnailWindow::init() {
    WNDCLASS wndClass = createWindowClass(THUMBNAIL_WINDOW_CLASS);
    wndClass.style |= CS_HREDRAW | CS_VREDRAW; // redraw whenever size changes
    RegisterClass(&wndClass);
}

ThumbnailWindow::ThumbnailWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
    : ItemWindow(parent, item) {}

ThumbnailWindow::~ThumbnailWindow() {
    // don't need to acquire lock
    if (thumbnailBitmap) {
        DeleteBitmap(thumbnailBitmap);
        CHROMAFILER_MEMLEAK_FREE;
    }
}

const wchar_t * ThumbnailWindow::className() {
    return THUMBNAIL_WINDOW_CLASS;
}

void ThumbnailWindow::onCreate() {
    ItemWindow::onCreate();
    thumbnailThread.Attach(new ThumbnailThread(item, this));
    thumbnailThread->start();
}

void ThumbnailWindow::onDestroy() {
    ItemWindow::onDestroy();
    thumbnailThread->stop();
}

void ThumbnailWindow::onSize(SIZE size) {
    ItemWindow::onSize(size);
    RECT bodyRect = windowBody();
    SIZE bodySize = rectSize(bodyRect);
    thumbnailThread->requestThumbnail(bodySize);
}

void ThumbnailWindow::onItemChanged() {
    ItemWindow::onItemChanged();
    thumbnailThread->stop();
    thumbnailThread.Attach(new ThumbnailThread(item, this));
    thumbnailThread->start();
}

void ThumbnailWindow::refresh() {
    ItemWindow::refresh();
    RECT bodyRect = windowBody();
    SIZE bodySize = rectSize(bodyRect);
    thumbnailThread->requestThumbnail(bodySize);
}

LRESULT ThumbnailWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == MSG_UPDATE_THUMBNAIL_BITMAP) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return ItemWindow::handleMessage(message, wParam, lParam);
}

void ThumbnailWindow::onPaint(PAINTSTRUCT paint) {
    ItemWindow::onPaint(paint);

    AcquireSRWLockExclusive(&thumbnailBitmapLock);
    if (!thumbnailBitmap) {
        ReleaseSRWLockExclusive(&thumbnailBitmapLock);
        return;
    }
    RECT body = windowBody();
    SIZE bodySize = rectSize(body);

    HBRUSH bg = (HBRUSH)(COLOR_WINDOW + 1);
    BITMAP bitmap;
    GetObject(thumbnailBitmap, sizeof(bitmap), &bitmap);
    int xDest, yDest, wDest, hDest;
    float wScale = (float)bodySize.cx / bitmap.bmWidth;
    float hScale = (float)bodySize.cy / bitmap.bmHeight;
    if (wScale < hScale) {
        xDest = body.left;
        wDest = bodySize.cx;
        yDest = body.top + (int)((bodySize.cy - wScale*bitmap.bmHeight) / 2);
        hDest = (int)(wScale*bitmap.bmHeight);
        FillRect(paint.hdc, tempPtr(RECT{body.left, body.top, body.right, yDest}), bg);
        FillRect(paint.hdc, tempPtr(RECT{body.left, yDest + hDest, body.right, body.bottom}), bg);
    } else {
        xDest = body.left + (int)((bodySize.cx - hScale*bitmap.bmWidth) / 2);
        wDest = (int)(hScale*bitmap.bmWidth);
        yDest = body.top;
        hDest = bodySize.cy;
        FillRect(paint.hdc, tempPtr(RECT{body.left, body.top, xDest, body.bottom}), bg);
        FillRect(paint.hdc, tempPtr(RECT{xDest + wDest, body.top, body.right, body.bottom}), bg);
    }

    HDC hdcMem = CreateCompatibleDC(paint.hdc);
    HBITMAP oldBitmap = SelectBitmap(hdcMem, thumbnailBitmap);
    SetStretchBltMode(paint.hdc, HALFTONE);
    StretchBlt(paint.hdc, xDest, yDest, wDest, hDest,
        hdcMem, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
    SelectBitmap(hdcMem, oldBitmap);
    DeleteDC(hdcMem);
    ReleaseSRWLockExclusive(&thumbnailBitmapLock);
}

ThumbnailWindow::ThumbnailThread::ThumbnailThread(
        CComPtr<IShellItem> item, ThumbnailWindow *callbackWindow)
        : callbackWindow(callbackWindow) {
    checkHR(SHGetIDListFromObject(item, &itemIDList));
    requestThumbnailEvent = checkLE(CreateEvent(nullptr, TRUE, FALSE, nullptr));
}

ThumbnailWindow::ThumbnailThread::~ThumbnailThread() {
    checkLE(CloseHandle(requestThumbnailEvent));
}

void ThumbnailWindow::ThumbnailThread::requestThumbnail(SIZE size) {
    AcquireSRWLockExclusive(&requestThumbnailLock);
    requestedSize = size;
    checkLE(SetEvent(requestThumbnailEvent));
    ReleaseSRWLockExclusive(&requestThumbnailLock);
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
            AcquireSRWLockExclusive(&requestThumbnailLock);
            size = requestedSize;
            checkLE(ResetEvent(requestThumbnailEvent));
            ReleaseSRWLockExclusive(&requestThumbnailLock);

            HBITMAP hBitmap;
            if (FAILED(imageFactory->GetImage(size,
                    SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &hBitmap))) {
                // no thumbnail, fallback to icon
                int minDim = max(1, min(size.cx, size.cy)); // make square (for Windows 7)
                if (!checkHR(imageFactory->GetImage({minDim, minDim},
                        SIIGBF_BIGGERSIZEOK | SIIGBF_ICONONLY, &hBitmap)))
                    return;
            }
            BITMAP bitmap;
            GetObject(hBitmap, sizeof(bitmap), &bitmap);
            compositeBackground(bitmap); // TODO change color for high contrast themes

            // ensure the window is not closed before the message is posted
            AcquireSRWLockExclusive(&stopLock);
            if (isStopped()) {
                DeleteBitmap(hBitmap);
            } else {
                AcquireSRWLockExclusive(&callbackWindow->thumbnailBitmapLock);
                if (callbackWindow->thumbnailBitmap) {
                    DeleteBitmap(callbackWindow->thumbnailBitmap);
                    CHROMAFILER_MEMLEAK_FREE;
                }
                callbackWindow->thumbnailBitmap = hBitmap;
                PostMessage(callbackWindow->hwnd, MSG_UPDATE_THUMBNAIL_BITMAP, 0, 0);
                CHROMAFILER_MEMLEAK_ALLOC;
                ReleaseSRWLockExclusive(&callbackWindow->thumbnailBitmapLock);
            }
            ReleaseSRWLockExclusive(&stopLock);
        } else if (event == WAIT_OBJECT_0 + 1) {
            return; // stop
        }
    }
    return;
}

} // namespace
