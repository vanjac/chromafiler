#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromafiler {

class FolderWindow : public ItemWindow, public IServiceProvider, public ICommDlgBrowser2,
        public IExplorerBrowserEvents {
public:
    static void init();

    FolderWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item,
        const wchar_t *propBagOverride = nullptr);

    bool persistSizeInParent() const override;
    SIZE requestedSize() const override;
    SIZE requestedChildSize() const override;

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
    // ICommDlgBrowser2
    STDMETHODIMP GetDefaultMenuText(IShellView *view, wchar_t *text, int maxChars) override;
    STDMETHODIMP GetViewFlags(DWORD *flags) override;
    STDMETHODIMP Notify(IShellView *view, DWORD notifyType) override;
    // IExplorerBrowserEvents
    STDMETHODIMP OnNavigationPending(PCIDLIST_ABSOLUTE folder) override;
    STDMETHODIMP OnNavigationComplete(PCIDLIST_ABSOLUTE folder) override;
    STDMETHODIMP OnNavigationFailed(PCIDLIST_ABSOLUTE folder) override;
    STDMETHODIMP OnViewCreated(IShellView *shellView) override;

protected:
    enum UserMessage {
        MSG_SETUP_SCROLLBAR_SUBCLASS = ItemWindow::MSG_LAST,
        MSG_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    void onDestroy() override;
    bool onCommand(WORD command) override;
    LRESULT onNotify(NMHDR *nmHdr) override;
    void onActivate(WORD state, HWND prevWindow) override;
    void onSize(int width, int height) override;
    void onExitSizeMove(bool moved, bool sized) override;

    void addToolbarButtons(HWND tb) override;
    int getToolbarTooltip(WORD command) override;

    void onChildDetached() override;
    void onChildResized(SIZE size) override;

    void onItemChanged() override;
    void refresh() override;

private:
    const wchar_t * className() override;

    bool useDefaultStatusText() const override;

    virtual wchar_t * propertyBag() const;
    virtual void initDefaultView(CComPtr<IFolderView2> folderView);

    void setupScrollBarSubclass();
    static LRESULT CALLBACK scrollBarSubclassProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    void selectionChanged();
    void clearSelection();
    void updateStatus();

    CComPtr<IContextMenu> queryBackgroundMenu(HMENU *popupMenu);
    void newItem(const char *verb);
    void openNewItemMenu(POINT point);
    void openViewMenu(POINT point);
    void openBackgroundSubMenu(CComPtr<IContextMenu> contextMenu, HMENU subMenu, POINT point);

    CComPtr<IExplorerBrowser> browser; // will be null if browser can't be initialized!
    CComPtr<IShellView> shellView;
    CComPtr<IPropertyBag> propBag;
    DWORD eventsCookie = 0;

    CComPtr<IShellItem> selected;

    // jank flags
    bool ignoreNextSelection = false;
    bool updateSelectionOnActivate = false;
    bool activateOnShiftRelease = false;
    bool clickActivate = false, clickActivateRelease = false;
};

} // namespace
