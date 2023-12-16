#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromafiler {

class PreviewWindow : public ItemWindow, public IPreviewHandlerFrame {

    struct InitPreviewRequest : public IUnknownImpl {
        InitPreviewRequest(IShellItem *item, CLSID previewID,
            PreviewWindow *callbackWindow, HWND parent, RECT rect);
        ~InitPreviewRequest();
        void cancel(); // ok to call this multiple times

        CComHeapPtr<ITEMIDLIST> itemIDList;
        const CLSID previewID;
        PreviewWindow *const callbackWindow;
        const HWND parent;
        const RECT rect;
        HANDLE cancelEvent;
        SRWLOCK cancelLock = SRWLOCK_INIT;
    };

public:
    static void init();
    static void uninit();

    PreviewWindow(ItemWindow *parent, IShellItem *item, CLSID previewID, bool async = true);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IPreviewHandlerFrame
    STDMETHODIMP GetWindowContext(PREVIEWHANDLERFRAMEINFO *info);
    STDMETHODIMP TranslateAccelerator(MSG *msg);

protected:
    enum UserMessage {
        // WPARAM: 0, LPARAM: 0
        MSG_INIT_PREVIEW_COMPLETE = ItemWindow::MSG_LAST,
        MSG_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    void onDestroy() override;
    void onActivate(WORD state, HWND prevWindow) override;
    void onSize(SIZE size) override;

    void refresh() override;
    void onItemChanged() override;

private:
    void requestPreview(RECT rect);
    void destroyPreview();

    const bool async;
    const CLSID previewID;
    CComPtr<InitPreviewRequest> initRequest;
    CComPtr<IPreviewHandler> preview; // will be null if preview can't be loaded!
    HWND container = nullptr;

    SRWLOCK previewStreamLock = SRWLOCK_INIT;
    CComPtr<IStream> previewStream;

    // worker thread
    static HANDLE initPreviewThread;
    static DWORD WINAPI initPreviewThreadProc(void *);
    static void initPreview(InitPreviewRequest *request, bool async);
    static bool initPreviewWithItem(IPreviewHandler *preview, IShellItem *item);
};

} // namespace
