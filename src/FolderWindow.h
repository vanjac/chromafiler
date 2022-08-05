#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromafile {

class FolderWindow : public ItemWindow, public IServiceProvider, public ICommDlgBrowser,
        public IExplorerBrowserEvents {
public:
    static void init();

    FolderWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item,
        const wchar_t *propBagOverride = nullptr);

    bool preserveSize() const override;
    SIZE requestedSize() const override;

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
    // IExplorerBrowserEvents
    STDMETHODIMP OnNavigationPending(PCIDLIST_ABSOLUTE folder) override;
    STDMETHODIMP OnNavigationComplete(PCIDLIST_ABSOLUTE folder) override;
    STDMETHODIMP OnNavigationFailed(PCIDLIST_ABSOLUTE folder) override;
    STDMETHODIMP OnViewCreated(IShellView *shellView) override;

protected:
    void onCreate() override;
    void onDestroy() override;
    bool onCommand(WORD command) override;
    LRESULT onNotify(NMHDR *nmHdr) override;
    void onActivate(WORD state, HWND prevWindow) override;
    void onSize(int width, int height) override;

    void addToolbarButtons(HWND tb) override;
    virtual int getToolbarTooltip(WORD command);

    void onChildDetached() override;

    void onItemChanged() override;
    void refresh() override;

private:
    const wchar_t * className() override;

    bool useDefaultStatusText() const override;

    virtual wchar_t * propertyBag() const;
    virtual void initDefaultView(CComPtr<IFolderView2> folderView);

    void selectionChanged();

    CComPtr<IContextMenu> queryBackgroundMenu(HMENU *popupMenu);
    void newFolder();
    void openNewItemMenu(POINT point);
    void openViewMenu(POINT point);
    void openBackgroundSubMenu(CComPtr<IContextMenu> contextMenu, HMENU subMenu, POINT point);

    CComPtr<IExplorerBrowser> browser; // will be null if browser can't be initialized!
    CComPtr<IShellView> shellView;
    CComPtr<IPropertyBag> propBag;
    DWORD eventsCookie = 0;

    CComPtr<IShellItem> selected;

    SIZE lastSize = {-1, -1};
    bool sizeChanged = false;
    SIZE oldStoredChildSize;

    // jank flags
    bool ignoreNextSelection = false;
    bool updateSelectionOnActivate = false;
    bool activateOnShiftRelease = false;
};

} // namespace
