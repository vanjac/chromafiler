#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromabrowse {

class PreviewWindow : public ItemWindow, public IPreviewHandlerFrame {
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
    void onCreate() override;
    void onDestroy() override;
    void onActivate(WORD state, HWND prevWindow) override;
    void onSize(int width, int height) override;

    void refresh() override;

private:
    const wchar_t * className() override;

    bool initPreview();
    bool initPreviewWithItem();

    CLSID previewID;
    CComPtr<IPreviewHandler> preview; // will be null if preview can't be loaded!
    HWND container;
};

} // namespace
