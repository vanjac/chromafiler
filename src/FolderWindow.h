#pragma once
#include <common.h>

#include "ItemWindow.h"
#include <ExDisp.h>

namespace chromafiler {

class FolderWindow : public ItemWindow, public IServiceProvider, public ICommDlgBrowser2,
        public IExplorerBrowserEvents, public IWebBrowserApp {
public:
    static void init();

    FolderWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

    bool persistSizeInParent() const override;

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
    // IDispatch
    STDMETHODIMP GetTypeInfoCount(UINT *) override;
    STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo **) override;
    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override;
    STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *)
        override;
    // IWebBrowser
    STDMETHODIMP GoBack() override;
    STDMETHODIMP GoForward() override;
    STDMETHODIMP GoHome() override;
    STDMETHODIMP GoSearch() override;
    STDMETHODIMP Navigate(BSTR, VARIANT *, VARIANT *, VARIANT *, VARIANT *) override;
    STDMETHODIMP Refresh() override;
    STDMETHODIMP Refresh2(VARIANT *) override;
    STDMETHODIMP Stop() override;
    STDMETHODIMP get_Application(IDispatch **) override;
    STDMETHODIMP get_Parent(IDispatch **) override;
    STDMETHODIMP get_Container(IDispatch **) override;
    STDMETHODIMP get_Document(IDispatch **) override;
    STDMETHODIMP get_TopLevelContainer(VARIANT_BOOL *) override;
    STDMETHODIMP get_Type(BSTR *) override;
    STDMETHODIMP get_Left(long *) override;
    STDMETHODIMP put_Left(long) override;
    STDMETHODIMP get_Top(long *) override;
    STDMETHODIMP put_Top(long) override;
    STDMETHODIMP get_Width(long *) override;
    STDMETHODIMP put_Width(long) override;
    STDMETHODIMP get_Height(long *) override;
    STDMETHODIMP put_Height(long) override;
    STDMETHODIMP get_LocationName(BSTR *) override;
    STDMETHODIMP get_LocationURL(BSTR *) override;
    STDMETHODIMP get_Busy(VARIANT_BOOL *) override;
    // IWebBrowserApp
    STDMETHODIMP Quit() override;
    STDMETHODIMP ClientToWindow(int *, int *) override;
    STDMETHODIMP PutProperty(BSTR, VARIANT) override;
    STDMETHODIMP GetProperty(BSTR, VARIANT *) override;
    STDMETHODIMP get_Name(BSTR *) override;
    STDMETHODIMP get_HWND(SHANDLE_PTR *) override;
    STDMETHODIMP get_FullName(BSTR *) override;
    STDMETHODIMP get_Path(BSTR *) override;
    STDMETHODIMP get_Visible(VARIANT_BOOL *) override;
    STDMETHODIMP put_Visible(VARIANT_BOOL) override;
    STDMETHODIMP get_StatusBar(VARIANT_BOOL *) override;
    STDMETHODIMP put_StatusBar(VARIANT_BOOL) override;
    STDMETHODIMP get_StatusText(BSTR *) override;
    STDMETHODIMP put_StatusText(BSTR) override;
    STDMETHODIMP get_ToolBar(int *) override;
    STDMETHODIMP put_ToolBar(int) override;
    STDMETHODIMP get_MenuBar(VARIANT_BOOL *) override;
    STDMETHODIMP put_MenuBar(VARIANT_BOOL) override;
    STDMETHODIMP get_FullScreen(VARIANT_BOOL *) override;
    STDMETHODIMP put_FullScreen(VARIANT_BOOL) override;

protected:
    enum UserMessage {
        // WPARAM: 0, LPARAM: 0
        MSG_SELECTION_CHANGED = ItemWindow::MSG_LAST,
        MSG_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    bool useDefaultStatusText() const override;
    SIZE defaultSize() const override;

    virtual FOLDERSETTINGS folderSettings() const;
    virtual void initDefaultView(CComPtr<IFolderView2> folderView);

    void onCreate() override;
    void onDestroy() override;
    bool onCommand(WORD command) override;
    LRESULT onDropdown(int command, POINT pos) override;
    void onActivate(WORD state, HWND prevWindow) override;
    void onSize(SIZE size) override;

    void addToolbarButtons(HWND tb) override;
    int getToolbarTooltip(WORD command) override;

    void trackContextMenu(POINT pos) override;

    void onChildDetached() override;

    IDispatch * getDispatch() override;
    void onItemChanged() override;
    void refresh() override;

    HWND listView = nullptr;
    CComPtr<IShellView> shellView;

private:
    const wchar_t * className() const override;

    void listViewCreated();
    static LRESULT CALLBACK listViewSubclassProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);
    static LRESULT CALLBACK listViewOwnerProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    void selectionChanged();
    void updateSelection();
    void clearSelection();
    void updateStatus();

    CComPtr<IContextMenu> queryBackgroundMenu(HMENU *popupMenu);
    void newItem(const char *verb);
    void openNewItemMenu(POINT point);
    void openViewMenu(POINT point);
    void openBackgroundSubMenu(CComPtr<IContextMenu> contextMenu, HMENU subMenu, POINT point);

    CComPtr<IExplorerBrowser> browser; // will be null if browser can't be initialized!
    DWORD eventsCookie = 0;

    CComPtr<IShellItem> selected; // links are not resolved unlike child->item

    // jank flags
    bool selectionDirty = false;
    bool ignoreInitialSelection = false;
    bool updateSelectionOnActivate = false;
    bool activateOnShiftRelease = false;
    bool clickActivate = true;
    bool firstODDispInfo = false;
    bool handlingSetColumnWidth = false;

    DWORD clickTime = 0;
    POINT clickPos;
};

} // namespace
