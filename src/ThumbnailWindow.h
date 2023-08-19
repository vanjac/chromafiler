#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromafiler {

class ThumbnailWindow : public ItemWindow {
public:
    static void init();

    ThumbnailWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);
    ~ThumbnailWindow();

protected:
    enum UserMessage {
        // WPARAM: 0, LPARAM: HBITMAP (calls DeleteObject!)
        MSG_SET_THUMBNAIL_BITMAP = ItemWindow::MSG_LAST,
        MSG_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    void onDestroy() override;
    void onSize(SIZE size) override;
    void onPaint(PAINTSTRUCT paint) override;

    void onItemChanged() override;
    void refresh() override;

private:
    const wchar_t * className() override;

    HBITMAP thumbnailBitmap = nullptr;

    class ThumbnailThread : public StoppableThread {
    public:
        ThumbnailThread(CComPtr<IShellItem> item, HWND callbackWindow);
        ~ThumbnailThread();
        void requestThumbnail(SIZE size);
    protected:
        void run() override;
    private:
        CComHeapPtr<ITEMIDLIST> itemIDList;
        const HWND callbackWindow;
        HANDLE requestThumbnailEvent;
        SRWLOCK requestThumbnailLock = SRWLOCK_INIT;
        SIZE requestedSize;
    };

    CComPtr<ThumbnailThread> thumbnailThread;
};

} // namespace
