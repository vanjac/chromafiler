#pragma once
#include <common.h>

#include "PreviewHandler.h"

namespace chromafiler {

// {80f502d3-92c4-4773-9333-1edd9e48d7d3}
const CLSID CLSID_ThumbnailView =
    {0x80f502d3, 0x92c4, 0x4773, {0x93, 0x33, 0x1e, 0xdd, 0x9e, 0x48, 0xd7, 0xd3}};
class ThumbnailView : public PreviewHandlerImpl {
public:
    static void init();
    static void uninit();

    ~ThumbnailView();

protected:
    enum UserMessage {
        // WPARAM: 0, LPARAM: 0
        MSG_UPDATE_THUMBNAIL_BITMAP = WM_USER,
        MSG_LAST
    };
    const wchar_t * className() const override;
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    void onPaint(PAINTSTRUCT paint);

    SRWLOCK thumbnailBitmapLock = SRWLOCK_INIT;
    HBITMAP thumbnailBitmap = nullptr;

    class ThumbnailThread : public StoppableThread {
    public:
        ThumbnailThread(IShellItem *item, ThumbnailView *callbackWindow);
        ~ThumbnailThread();
        void requestThumbnail(SIZE size);
    protected:
        void run() override;
    private:
        CComHeapPtr<ITEMIDLIST> itemIDList;
        ThumbnailView *callbackWindow;
        HANDLE requestThumbnailEvent;
        SRWLOCK requestThumbnailLock = SRWLOCK_INIT;
        SIZE requestedSize;
    };

    CComPtr<ThumbnailThread> thumbnailThread;
};

} // namespace
