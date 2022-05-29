#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromabrowse {

class PreviewWindow : public ItemWindow, public IPreviewHandlerFrame {
public:
    static void init();

    PreviewWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item, CLSID previewID);

    SIZE defaultSize() override;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IPreviewHandlerFrame
    STDMETHODIMP GetWindowContext(PREVIEWHANDLERFRAMEINFO *info);
    STDMETHODIMP TranslateAccelerator(MSG *msg);

protected:
    void onCreate() override;
    void onDestroy() override;
    void onSize() override;

private:
    const wchar_t * className() override;

    bool initPreviewWithItem();

    CLSID previewID;
    CComPtr<IPreviewHandler> preview;
    HWND container;
};

} // namespace
