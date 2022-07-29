#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromafile {

class PreviewWindow : public ItemWindow, public IPreviewHandlerFrame {

    struct InitPreviewRequest : public IUnknownImpl {
        InitPreviewRequest(CComPtr<IShellItem> item, CLSID previewID,
            HWND callbackWindow, HWND container);
        ~InitPreviewRequest();
        void cancel(); // ok to call this multiple times

        CComHeapPtr<ITEMIDLIST> itemIDList;
        const CLSID previewID;
        const HWND callbackWindow, container;
        HANDLE cancelEvent;
        CRITICAL_SECTION cancelSection;
    };

public:
    static void init();
    static void uninit();

    PreviewWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item, CLSID previewID);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IPreviewHandlerFrame
    STDMETHODIMP GetWindowContext(PREVIEWHANDLERFRAMEINFO *info);
    STDMETHODIMP TranslateAccelerator(MSG *msg);

protected:
    enum UserMessage {
        MSG_INIT_PREVIEW_COMPLETE = ItemWindow::MSG_LAST,
        MSG_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    void onDestroy() override;
    void onActivate(WORD state, HWND prevWindow) override;
    void onSize(int width, int height) override;

    void refresh() override;

private:
    const wchar_t * className() override;

    void destroyPreview();

    CLSID previewID;
    CComPtr<InitPreviewRequest> initRequest;
    CComPtr<IPreviewHandler> preview; // will be null if preview can't be loaded!
    HWND container;

    // worker thread
    static HANDLE initPreviewThread;
    static DWORD WINAPI initPreviewThreadProc(void *);
    static void initPreview(CComPtr<InitPreviewRequest> request);
    static bool initPreviewWithItem(CComPtr<IPreviewHandler> preview, CComPtr<IShellItem> item);
};

} // namespace
