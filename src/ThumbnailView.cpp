#include "ThumbnailView.h"
#include "GeomUtils.h"
#include "GDIUtils.h"
#include "WinUtils.h"
#include <windowsx.h>

namespace chromafiler {

const wchar_t THUMBNAIL_CLASS[] = L"ChromaFiler Thumbnail";

static ClassFactoryImpl<ThumbnailView, false> factory;
static DWORD regCookie = 0;

void ThumbnailView::init() {
    WNDCLASS thumbClass = {};
    thumbClass.lpfnWndProc = windowProc;
    thumbClass.hInstance = GetModuleHandle(nullptr);
    thumbClass.lpszClassName = THUMBNAIL_CLASS;
    thumbClass.style = CS_HREDRAW | CS_VREDRAW;
    thumbClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&thumbClass);

    checkHR(CoRegisterClassObject(CLSID_ThumbnailView, &factory,
        CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &regCookie));
}

void ThumbnailView::uninit() {
    checkHR(CoRevokeClassObject(regCookie));
}

ThumbnailView::~ThumbnailView() {
    // don't need to acquire lock since thread is stopped
    if (thumbnailBitmap) {
        DeleteBitmap(thumbnailBitmap);
        CHROMAFILER_MEMLEAK_FREE;
    }
}

const wchar_t * ThumbnailView::className() const {
    return THUMBNAIL_CLASS;
}

LRESULT ThumbnailView::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            thumbnailThread.Attach(new ThumbnailThread(item, this));
            thumbnailThread->start();
            return 0;
        case WM_DESTROY:
            thumbnailThread->stop();
            return 0;
        case WM_SIZE:
            thumbnailThread->requestThumbnail(clientSize(hwnd));
            return 0;
        case WM_PAINT:
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            onPaint(paint);
            EndPaint(hwnd, &paint);
            return 0;
        case MSG_UPDATE_THUMBNAIL_BITMAP:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

void ThumbnailView::onPaint(PAINTSTRUCT paint) {
    AcquireSRWLockExclusive(&thumbnailBitmapLock);
    if (!thumbnailBitmap) {
        ReleaseSRWLockExclusive(&thumbnailBitmapLock);
        return;
    }
    SIZE size = clientSize(hwnd);

    HBRUSH bg = (HBRUSH)(COLOR_WINDOW + 1);
    BITMAP bitmap;
    GetObject(thumbnailBitmap, sizeof(bitmap), &bitmap);
    int xDest, yDest, wDest, hDest;
    float wScale = (float)size.cx / bitmap.bmWidth;
    float hScale = (float)size.cy / bitmap.bmHeight;
    if (wScale < hScale) {
        xDest = 0;
        wDest = size.cx;
        yDest = (int)((size.cy - wScale*bitmap.bmHeight) / 2);
        hDest = (int)(wScale*bitmap.bmHeight);
        FillRect(paint.hdc, tempPtr(RECT{0, 0, size.cx, yDest}), bg);
        FillRect(paint.hdc, tempPtr(RECT{0, yDest + hDest, size.cx, size.cy}), bg);
    } else {
        xDest = (int)((size.cx - hScale*bitmap.bmWidth) / 2);
        wDest = (int)(hScale*bitmap.bmWidth);
        yDest = 0;
        hDest = size.cy;
        FillRect(paint.hdc, tempPtr(RECT{0, 0, xDest, size.cy}), bg);
        FillRect(paint.hdc, tempPtr(RECT{xDest + wDest, 0, size.cx, size.cy}), bg);
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

ThumbnailView::ThumbnailThread::ThumbnailThread(
        IShellItem *const item, ThumbnailView *const callbackWindow)
        : callbackWindow(callbackWindow) {
    checkHR(SHGetIDListFromObject(item, &itemIDList));
    requestThumbnailEvent = checkLE(CreateEvent(nullptr, TRUE, FALSE, nullptr));
}

ThumbnailView::ThumbnailThread::~ThumbnailThread() {
    checkLE(CloseHandle(requestThumbnailEvent));
}

void ThumbnailView::ThumbnailThread::requestThumbnail(SIZE size) {
    AcquireSRWLockExclusive(&requestThumbnailLock);
    requestedSize = size;
    checkLE(SetEvent(requestThumbnailEvent));
    ReleaseSRWLockExclusive(&requestThumbnailLock);
}

void ThumbnailView::ThumbnailThread::run() {
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
