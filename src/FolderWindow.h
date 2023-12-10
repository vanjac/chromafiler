#pragma once
#include <common.h>

#include "ItemWindow.h"
#include <memory>
#include <ExDisp.h>
#include <shlobj_core.h>

namespace chromafiler {

class FolderWindow : public ItemWindow, public IServiceProvider, public ICommDlgBrowser2,
        public IExplorerBrowserEvents, public IShellFolderViewCB, public IWebBrowserApp {
public:
    static void init();

    FolderWindow(ItemWindow *parent, IShellItem *item);

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
    // IShellFolderViewCB
    STDMETHODIMP MessageSFVCB(UINT msg, WPARAM wParam, LPARAM lParam) override;
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
    enum ViewStateIndex {
        STATE_SHELL_VISITED = ItemWindow::STATE_LAST, // 0x8 - folder was visited by shell view
        STATE_ICON_POS, // 0x10
        STATE_LAST
    };
    enum TimerID {
        TIMER_UPDATE_SELECTION = 1,
        TIMER_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    static bool useCustomIconPersistence();
    static bool spatialView(IFolderView *folderView);

    bool useDefaultStatusText() const override;
    SIZE defaultSize() const override;
    bool isFolder() const override;

    void clearViewState(IPropertyBag *bag, uint32_t mask) override;
    void writeViewState(IPropertyBag *bag, uint32_t mask) override;

    virtual FOLDERSETTINGS folderSettings() const;
    virtual void initDefaultView(IFolderView2 *folderView);

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

    IDispatch * getShellViewDispatch() override;
    void onItemChanged() override;
    void refresh() override;

    HWND listView = nullptr;
    CComPtr<IShellView> shellView;

private:
    struct ShellViewState {
        FOLDERVIEWMODE viewMode = {};
        DWORD flags = 0;
        int iconSize = 0;

        UINT numColumns = 0;
        std::unique_ptr<PROPERTYKEY[]> columns;
        std::unique_ptr<UINT[]> columnWidths;

        int numSortColumns = 0;
        std::unique_ptr<SORTCOLUMN[]> sortColumns;

        PROPERTYKEY groupBy = {};
        BOOL groupAscending = false;

        int numItems = 0;
        std::unique_ptr<CComHeapPtr<ITEMID_CHILD>[]> itemIds;
        std::unique_ptr<POINT[]> itemPositions;
    };

    void listViewCreated();
    static LRESULT CALLBACK listViewSubclassProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);
    static LRESULT CALLBACK listViewOwnerProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    static void getViewState(IFolderView2 *folderView, ShellViewState *state);
    static void setViewState(IFolderView2 *folderView, const ShellViewState &state);

    bool writeIconPositions(IFolderView *folderView, IStream *stream);
    void loadViewState(IPropertyBag *bag);
    bool readIconPositions(IFolderView *folderView, IStream *stream);

    void selectionChanged();
    void scheduleUpdateSelection();
    void updateSelection();
    void clearSelection();
    void updateStatus();

    CComPtr<IContextMenu> queryBackgroundMenu(HMENU *popupMenu);
    void newItem(const char *verb);
    void openNewItemMenu(POINT point);
    void openViewMenu(POINT point);
    void openBackgroundSubMenu(IContextMenu *contextMenu, HMENU subMenu, POINT point);

    CComPtr<IExplorerBrowser> browser; // will be null if browser can't be initialized!
    DWORD eventsCookie = 0;
    CComPtr<IShellFolderViewCB> prevCB;

    CComPtr<IShellItem> selected; // links are not resolved unlike child->item

    // jank flags
    bool ignoreInitialSelection = false;
    bool updateSelectionOnActivate = false;
    bool activateOnShiftRelease = false;
    bool clickActivate = true;
    bool firstODDispInfo = false;
    bool handlingSetColumnWidth = false;
    bool handlingRButtonDown = false;
    bool selectedWhileHandlingRButtonDown = false;
    bool invokingDefaultVerb = false;

    DWORD clickTime = 0;
    POINT clickPos;

    std::unique_ptr<ShellViewState> storedViewState;
};

} // namespace
