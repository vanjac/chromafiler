#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromafiler {

class ThumbnailWindow : public ItemWindow {
public:
    ThumbnailWindow(ItemWindow *parent, IShellItem *item);
    ~ThumbnailWindow();

protected:
    enum UserMessage {
        // WPARAM: 0, LPARAM: 0
        MSG_UPDATE_THUMBNAIL_BITMAP = ItemWindow::MSG_LAST,
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
    SRWLOCK thumbnailBitmapLock = SRWLOCK_INIT;
    HBITMAP thumbnailBitmap = nullptr;

    class ThumbnailThread : public StoppableThread {
    public:
        ThumbnailThread(IShellItem *item, ThumbnailWindow *callbackWindow);
        ~ThumbnailThread();
        void requestThumbnail(SIZE size);
    protected:
        void run() override;
    private:
        CComHeapPtr<ITEMIDLIST> itemIDList;
        ThumbnailWindow *callbackWindow;
        HANDLE requestThumbnailEvent;
        SRWLOCK requestThumbnailLock = SRWLOCK_INIT;
        SIZE requestedSize;
    };

    CComPtr<ThumbnailThread> thumbnailThread;
};

} // namespace
