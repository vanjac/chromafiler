#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromabrowse {

class ThumbnailWindow : public ItemWindow {
public:
    static void init();

    ThumbnailWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);
    ~ThumbnailWindow();

protected:
    enum UserMessage {
        MSG_SET_THUMBNAIL_BITMAP = ItemWindow::MSG_LAST,
        MSG_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    void onDestroy() override;
    void onSize(int width, int height) override;
    void onPaint(PAINTSTRUCT paint) override;

    void onItemChanged() override;
    void refresh() override;

private:
    const wchar_t * className() override;

    HBITMAP thumbnailBitmap = 0;

    class ThumbnailThread : public IUnknownImpl {
    public:
        ThumbnailThread(CComPtr<IShellItem> item, HWND callbackWindow);
        ~ThumbnailThread();
        void requestThumbnail(SIZE size);
        void stop();
    private:
        HANDLE thread;
        CComHeapPtr<ITEMIDLIST> itemIDList;
        const HWND callbackWindow;
        HANDLE requestThumbnailEvent, stopEvent;
        CRITICAL_SECTION requestThumbnailSection, stopSection;
        SIZE requestedSize;
        static DWORD WINAPI thumbnailThreadProc(void *);
        void run();
    };

    CComPtr<ThumbnailThread> thumbnailThread;
};

} // namespace
