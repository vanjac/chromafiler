#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromabrowse {

class FolderWindow : public ItemWindow, public IServiceProvider, public ICommDlgBrowser {
public:
    static void init();

    FolderWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

    SIZE defaultSize() override;

    bool handleTopLevelMessage(MSG *msg) override;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IServiceProvider
    STDMETHODIMP QueryService(REFGUID guidService, REFIID riid, void **ppv) override;
    // ICommDlgBrowser
    STDMETHODIMP OnDefaultCommand(IShellView *view) override;
    STDMETHODIMP OnStateChange(IShellView *view, ULONG change) override;
    STDMETHODIMP IncludeObject(IShellView *view, PCUITEMID_CHILD pidl) override;

protected:
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    void onDestroy() override;
    void onActivate(WORD state, HWND prevWindow) override;
    void onSize(int width, int height) override;

    void onChildDetached() override;

private:
    const wchar_t * className() override;

    void selectionChanged();
    void resultsFolderFallback();

    CComPtr<IExplorerBrowser> browser;
    CComPtr<IShellView> shellView;

    // jank flags
    bool ignoreNextSelection = false;
    bool activateOnShiftRelease = false;
};

} // namespace
