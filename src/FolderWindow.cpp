#include "FolderWindow.h"
#include "RectUtils.h"
#include "Settings.h"
#include "DPI.h"
#include "UIStrings.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <Propvarutil.h>

// Example of how to host an IExplorerBrowser:
// https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/shell/appplatform/ExplorerBrowserCustomContents

namespace chromafiler {

const wchar_t FOLDER_WINDOW_CLASS[] = L"ChromaFile Folder";

const wchar_t PROP_VISITED[] = L"Visited";
const wchar_t PROP_SIZE[] = L"Size";
const wchar_t PROP_CHILD_SIZE[] = L"ChildSize";

const wchar_t * const HIDDEN_ITEM_PARSE_NAMES[] = {
    L"::{26EE0668-A00A-44D7-9371-BEB064C98683}", // Control Panel
    L"::{018D5C66-4533-4307-9B53-224DE2ED1FE6}", // OneDrive (may fail if not installed)
    L"::{031E4825-7B94-4DC3-B131-E946B44C8DD5}", // Libraries
};
static CComHeapPtr<ITEMID_CHILD> hiddenItemIDs[_countof(HIDDEN_ITEM_PARSE_NAMES)];

// local property bags can be found at:
// HKEY_CURRENT_USER\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell\Bags
CComPtr<IPropertyBag> getItemPropertyBag(CComPtr<IShellItem> item, const wchar_t *name) {
    CComPtr<IPropertyBag> propBag;
    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        checkHR(SHGetViewStatePropertyBag(idList, name, SHGVSPB_FOLDERNODEFAULTS,
            IID_PPV_ARGS(&propBag)));
    }
    return propBag;
}

void FolderWindow::init() {
    WNDCLASS wndClass = createWindowClass(FOLDER_WINDOW_CLASS);
    RegisterClass(&wndClass);

    for (int i = 0; i < _countof(HIDDEN_ITEM_PARSE_NAMES); i++) {
        CComPtr<IShellItem> item;
        if (SUCCEEDED(SHCreateItemFromParsingName(HIDDEN_ITEM_PARSE_NAMES[i], nullptr,
                IID_PPV_ARGS(&item)))) {
            checkHR(CComQIPtr<IParentAndItem>(item)
                ->GetParentAndItem(nullptr, nullptr, &hiddenItemIDs[i]));
        }
    }
}

FolderWindow::FolderWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item,
        const wchar_t *propBagOverride)
        : ItemWindow(parent, item) {
    // can't call virtual method in constructor!
    propBag = getItemPropertyBag(item, propBagOverride ? propBagOverride : propertyBag());
}

const wchar_t * FolderWindow::className() {
    return FOLDER_WINDOW_CLASS;
}

bool FolderWindow::persistSizeInParent() const {
    return false;
}

bool FolderWindow::useDefaultStatusText() const {
    return false;
}

SIZE FolderWindow::requestedSize() const {
    if (propBag) {
        VARIANT sizeVar = {};
        sizeVar.vt = VT_UI4;
        if (SUCCEEDED(propBag->Read(PROP_SIZE, &sizeVar, nullptr))) {
            return scaleDPI({GET_X_LPARAM(sizeVar.ulVal), GET_Y_LPARAM(sizeVar.ulVal)});
        }
    }
    return scaleDPI(settings::getFolderWindowSize());
}

SIZE FolderWindow::requestedChildSize() const {
    if (propBag) {
        VARIANT sizeVar = {};
        sizeVar.vt = VT_UI4;
        if (SUCCEEDED(propBag->Read(PROP_CHILD_SIZE, &sizeVar, nullptr))) {
            return scaleDPI({GET_X_LPARAM(sizeVar.ulVal), GET_Y_LPARAM(sizeVar.ulVal)});
        }
    }
    return ItemWindow::requestedChildSize();
}

wchar_t * FolderWindow::propertyBag() const {
    return L"chromafile";
}

bool FolderWindow::handleTopLevelMessage(MSG *msg) {
    if (ItemWindow::handleTopLevelMessage(msg))
        return true;
    if (activateOnShiftRelease && msg->message == WM_KEYUP && msg->wParam == VK_SHIFT) {
        if (shellView)
            checkHR(shellView->UIActivate(SVUIA_ACTIVATE_FOCUS));
        activateOnShiftRelease = false;
        // don't return true
    }
    if (shellView && shellView->TranslateAccelerator(msg) == S_OK)
        return true;
    return false;
}

void FolderWindow::onCreate() {
    ItemWindow::onCreate();

    RECT browserRect = windowBody();
    browserRect.bottom += browserRect.top; // initial rect is wrong

    FOLDERSETTINGS folderSettings = {};
    folderSettings.ViewMode = FVM_SMALLICON; // doesn't work correctly (see initDefaultView)
    folderSettings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW | FWF_NOHEADERINALLVIEWS
        | FWF_ALIGNLEFT; // use old ListView style!
    if (!checkHR(browser.CoCreateInstance(__uuidof(ExplorerBrowser))))
        return;
    checkHR(browser->SetOptions(EBO_NAVIGATEONCE | EBO_NOBORDER)); // no navigation
    if (!checkHR(browser->Initialize(hwnd, &browserRect, &folderSettings))) {
        browser = nullptr;
        return;
    }

    checkHR(IUnknown_SetSite(browser, (IServiceProvider *)this));
    checkHR(browser->Advise(this, &eventsCookie));
    checkHR(browser->SetPropertyBag(propertyBag()));
    // will call IExplorerBrowserEvents callbacks
    HRESULT hr;
    if (!checkHR(hr = browser->BrowseToObject(item, SBSP_ABSOLUTE))) {
        // eg. browsing a subdirectory in the recycle bin, or access denied
        checkHR(browser->Destroy());
        browser = nullptr;
        if (hasStatusText()) {
            LocalHeapPtr<wchar_t> message;
            formatErrorMessage(message, hr);
            setStatusText(message);
        }
        return;
    }
    setupListView();
}

void FolderWindow::addToolbarButtons(HWND tb) {
    TBBUTTON buttons[] = {
        makeToolbarButton(MDL2_CALCULATOR_ADDITION, IDM_NEW_ITEM_MENU, BTNS_WHOLEDROPDOWN),
        makeToolbarButton(MDL2_VIEW, IDM_VIEW_MENU, BTNS_WHOLEDROPDOWN),
    };
    SendMessage(tb, TB_ADDBUTTONS, _countof(buttons), (LPARAM)buttons);
    ItemWindow::addToolbarButtons(tb);
}

int FolderWindow::getToolbarTooltip(WORD command) {
    switch (command) {
        case IDM_NEW_ITEM_MENU:
            return IDS_NEW_ITEM_COMMAND;
        case IDM_VIEW_MENU:
            return IDS_VIEW_COMMAND;
    }
    return ItemWindow::getToolbarTooltip(command);
}

void FolderWindow::initDefaultView(CComPtr<IFolderView2> folderView) {
    // FVM_SMALLICON only seems to work if it's also specified with an icon size
    // https://docs.microsoft.com/en-us/windows/win32/menurc/about-icons
    checkHR(folderView->SetViewModeAndIconSize(FVM_SMALLICON, SHELL_SMALL_ICON));
}

void FolderWindow::setupListView() {
    if (!browser)
        return;
    HWND browserControl = FindWindowEx(hwnd, nullptr, L"ExplorerBrowserControl", nullptr);
    if (!checkLE(browserControl)) return;
    HWND defView = FindWindowEx(browserControl, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!checkLE(defView)) return;
    HWND listView = FindWindowEx(defView, nullptr, L"SysListView32", nullptr);
    if (!checkLE(listView)) return;
    DWORD style = GetWindowLong(listView, GWL_STYLE);
    style &= ~LVS_ALIGNLEFT;
    style |= LVS_ALIGNTOP;
    SetWindowLong(listView, GWL_STYLE, style);
    SetWindowSubclass(defView, shellViewSubclassProc, 0, 0);
    SetWindowSubclass(listView, listViewSubclassProc, 0, (DWORD_PTR)this);
}

LRESULT CALLBACK FolderWindow::shellViewSubclassProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_NOTIFY) {
        NMHDR *nmHdr = (NMHDR *)lParam;
        if (nmHdr->code == LVN_BEGINSCROLL || nmHdr->code == LVN_ENDSCROLL) {
            if (ListView_GetView(nmHdr->hwndFrom) != LV_VIEW_LIST)
                SendMessage(nmHdr->hwndFrom, WM_HSCROLL, SB_LEFT, 0);
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK FolderWindow::listViewSubclassProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    if (message == WM_LBUTTONDOWN) {
        FolderWindow *window = (FolderWindow *)refData;
        if (window->clickActivate) {
            window->clickActivate = false;
            LVHITTESTINFO hitTest = { {LOWORD(lParam), HIWORD(lParam)} };
            if (!window->paletteWindow() && ListView_GetSelectedCount(hwnd) == 1
                    && ListView_HitTest(hwnd, &hitTest) == -1) {
                debugPrintf(L"Blocking deselection!\n");
                return 0;
            }
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void FolderWindow::onDestroy() {
    if (propBag) {
        // view settings are only written when shell view is destroyed
        CComVariant visitedVar(true);
        checkHR(propBag->Write(PROP_VISITED, &visitedVar));
    }
    ItemWindow::onDestroy();
    if (browser) {
        checkHR(browser->Unadvise(eventsCookie));
        checkHR(IUnknown_SetSite(browser, nullptr));
        checkHR(browser->Destroy());
        browser = nullptr;
    }
}

bool FolderWindow::onCommand(WORD command) {
    switch (command) {
        case IDM_NEW_FOLDER:
            newItem(CMDSTR_NEWFOLDERA);
            return true;
        case IDM_NEW_TEXT_FILE:
            newItem(".txt");
            return true;
    }
    return ItemWindow::onCommand(command);
}

LRESULT FolderWindow::onNotify(NMHDR *nmHdr) {
    if (nmHdr->code == TBN_DROPDOWN) {
        NMTOOLBAR *nmToolbar = (NMTOOLBAR *)nmHdr;
        POINT menuPos = {nmToolbar->rcButton.left, nmToolbar->rcButton.bottom};
        ClientToScreen(nmHdr->hwndFrom, &menuPos);
        if (nmToolbar->iItem == IDM_NEW_ITEM_MENU) {
            openNewItemMenu(menuPos);
        } else if (nmToolbar->iItem == IDM_VIEW_MENU) {
            openViewMenu(menuPos);
        }
    }
    return ItemWindow::onNotify(nmHdr);
}

void FolderWindow::onActivate(WORD state, HWND prevWindow) {
    ItemWindow::onActivate(state, prevWindow);
    if (state != WA_INACTIVE) {
        if (shellView) {
            // override behavior causing sort columns to be focused when shift is held
            activateOnShiftRelease = GetKeyState(VK_SHIFT) < 0;
            checkHR(shellView->UIActivate(SVUIA_ACTIVATE_FOCUS));
        }
        if (updateSelectionOnActivate) {
            selectionChanged();
            updateSelectionOnActivate = false;
        }
    }
    if (state == WA_CLICKACTIVE) {
        POINT cursor = {};
        GetCursorPos(&cursor);
        ScreenToClient(hwnd, &cursor);
        RECT body = windowBody();
        if (PtInRect(&body, cursor))
            clickActivate = true;
    }
}

void FolderWindow::onSize(int width, int height) {
    ItemWindow::onSize(width, height);

    if (browser)
        checkHR(browser->SetRect(nullptr, windowBody()));
}

void FolderWindow::onExitSizeMove(bool moved, bool sized) {
    ItemWindow::onExitSizeMove(moved, sized);
    if (sized) {
        SIZE size = rectSize(windowRect());
        CComVariant sizeVar((unsigned long)MAKELONG(invScaleDPI(size.cx), invScaleDPI(size.cy)));
        checkHR(propBag->Write(PROP_SIZE, &sizeVar));
    }
}

void FolderWindow::onChildResized(SIZE size) {
    ItemWindow::onChildResized(size);
    CComVariant sizeVar((unsigned long)MAKELONG(invScaleDPI(size.cx), invScaleDPI(size.cy)));
    checkHR(propBag->Write(PROP_CHILD_SIZE, &sizeVar));
}

void FolderWindow::selectionChanged() {
    CComPtr<IFolderView2> folderView;
    if (FAILED(browser->GetCurrentView(IID_PPV_ARGS(&folderView))))
        return;
    int numSelected;
    if (!checkHR(folderView->ItemCount(SVGIO_SELECTION, &numSelected)))
        return;
    if (numSelected == 1) {
        int index;
        // GetSelectedItem seems to ignore the iStart parameter!
        if (folderView->GetSelectedItem(-1, &index) == S_OK) {
            CComPtr<IShellItem> newSelected;
            if (checkHR(folderView->GetItem(index, IID_PPV_ARGS(&newSelected)))) {
                int compare = 1;
                if (selected)
                    checkHR(newSelected->Compare(selected, SICHINT_CANONICAL, &compare));
                selected = newSelected;
                // openChild() could cause a "Problem with Shortcut" error dialog to appear,
                // so don't call it more than necessary!
                if (compare)
                    openChild(selected);
            }
        }
    } else {
        // 0 or more than 1 item selected
        selected = nullptr;
        closeChild();
    }
}

void FolderWindow::updateStatus() {
    // TODO: duplicate work in selectionChanged()
    CComPtr<IFolderView2> folderView;
    if (FAILED(browser->GetCurrentView(IID_PPV_ARGS(&folderView))))
        return;
    int numItems = 0, numSelected = 0;
    checkHR(folderView->ItemCount(SVGIO_ALLVIEW, &numItems));
    checkHR(folderView->ItemCount(SVGIO_SELECTION, &numSelected));
    LocalHeapPtr<wchar_t> status;
    if (numSelected == 0) {
        formatMessage(status, STR_FOLDER_STATUS, numItems);
    } else {
        formatMessage(status, STR_FOLDER_STATUS_SEL, numItems, numSelected);
    }
    setStatusText(status);
}

void FolderWindow::clearSelection() {
    if (shellView)
        checkHR(shellView->SelectItem(nullptr, SVSI_DESELECTOTHERS)); // keep focus
}

void FolderWindow::onChildDetached() {
    ItemWindow::onChildDetached();
    clearSelection();
}

void FolderWindow::onItemChanged() {
    ItemWindow::onItemChanged();
    propBag = getItemPropertyBag(item, propertyBag());
    shellView = nullptr;
    checkHR(browser->SetOptions(EBO_NOBORDER));
    checkHR(browser->BrowseToObject(item, SBSP_ABSOLUTE));
    checkHR(browser->SetOptions(EBO_NAVIGATEONCE | EBO_NOBORDER));
}

void FolderWindow::refresh() {
    ItemWindow::refresh();
    if (shellView) {
        checkHR(shellView->Refresh());
        // TODO: this is here to fix a crash (exception code c0000374 STATUS_HEAP_CORRUPTION)
        ignoreNextSelection = true;
    }
}

CComPtr<IContextMenu> FolderWindow::queryBackgroundMenu(HMENU *popupMenu) {
    if (!shellView)
        return nullptr;
    CComPtr<IContextMenu> contextMenu;
    if (!checkHR(shellView->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&contextMenu))))
        return nullptr;
    if ((*popupMenu = CreatePopupMenu()) == nullptr)
        return nullptr;
    if (!checkHR(contextMenu->QueryContextMenu(*popupMenu, 0, 1, 0x7FFF, CMF_OPTIMIZEFORINVOKE))) {
        checkLE(DestroyMenu(*popupMenu));
        return nullptr;
    }
    return contextMenu;
}

void FolderWindow::newItem(const char *verb) {
    HMENU popupMenu;
    CComPtr<IContextMenu> contextMenu = queryBackgroundMenu(&popupMenu);
    if (!contextMenu)
        return;
    CMINVOKECOMMANDINFO info = {sizeof(info)};
    info.hwnd = hwnd;
    info.lpVerb = verb;
    CComPtr<IFolderView2> folderView;
    if (checkHR(browser->GetCurrentView(IID_PPV_ARGS(&folderView)))) {
        // https://stackoverflow.com/q/40497455
        // allow command to select new folder for renaming. not documented of course :/
        checkHR(IUnknown_SetSite(contextMenu, folderView));
    }
    checkHR(contextMenu->InvokeCommand(&info));
    checkHR(IUnknown_SetSite(contextMenu, nullptr));

    checkLE(DestroyMenu(popupMenu));
}

HMENU findNewItemMenu(CComPtr<IContextMenu> contextMenu, HMENU popupMenu) {
    // search for the submenu that contains NewFolder verb as the first item (TODO: jank)
    CComQIPtr<IContextMenu3> contextMenu3(contextMenu);
    for (int i = 0, count = GetMenuItemCount(popupMenu); i < count; i++) {
        MENUITEMINFO itemInfo = {sizeof(itemInfo)};
        itemInfo.fMask = MIIM_SUBMENU;
        if (!checkLE(GetMenuItemInfo(popupMenu, i, TRUE, &itemInfo)))
            continue;
        if (!itemInfo.hSubMenu || GetMenuItemCount(itemInfo.hSubMenu) == 0)
            continue;
        if (contextMenu3) {
            contextMenu3->HandleMenuMsg2(WM_INITMENUPOPUP,
                (WPARAM)itemInfo.hSubMenu, i, nullptr);
        }
        MENUITEMINFO subItemInfo = {sizeof(subItemInfo)};
        subItemInfo.fMask = MIIM_ID;
        if (!checkLE(GetMenuItemInfo(itemInfo.hSubMenu, 0, TRUE, &subItemInfo))
                || (int)subItemInfo.wID <= 0)
            continue;
        wchar_t verb[64];
        verb[0] = 0;
        if (SUCCEEDED(contextMenu->GetCommandString(subItemInfo.wID - 1, GCS_VERBW, nullptr,
                (char*)verb, _countof(verb))) && lstrcmpi(verb, CMDSTR_NEWFOLDERW) == 0) {
            return itemInfo.hSubMenu;
        }
    }
    return nullptr;
}

void FolderWindow::openNewItemMenu(POINT point) {
    HMENU popupMenu;
    CComPtr<IContextMenu> contextMenu = queryBackgroundMenu(&popupMenu);
    if (!contextMenu)
        return;
    HMENU newItemMenu = findNewItemMenu(contextMenu, popupMenu);
    if (newItemMenu)
        openBackgroundSubMenu(contextMenu, newItemMenu, point);
    checkLE(DestroyMenu(popupMenu));
}

HMENU findViewMenu(CComPtr<IContextMenu> contextMenu, HMENU popupMenu) {
    for (int i = 0, count = GetMenuItemCount(popupMenu); i < count; i++) {
        MENUITEMINFO itemInfo = {sizeof(itemInfo)};
        itemInfo.fMask = MIIM_ID | MIIM_SUBMENU;
        if (!checkLE(GetMenuItemInfo(popupMenu, i, TRUE, &itemInfo)))
            continue;
        if (!itemInfo.hSubMenu || itemInfo.wID <= 0)
            continue;
        wchar_t verb[64];
        verb[0] = 0;
        if (SUCCEEDED(contextMenu->GetCommandString(itemInfo.wID - 1, GCS_VERBW, nullptr,
                (char*)verb, _countof(verb))) && lstrcmpi(verb, L"view") == 0) {
            return itemInfo.hSubMenu;
        }
    }
    return nullptr;
}

void FolderWindow::openViewMenu(POINT point) {
    HMENU popupMenu;
    CComPtr<IContextMenu> contextMenu = queryBackgroundMenu(&popupMenu);
    if (!contextMenu)
        return;
    HMENU viewMenu = findViewMenu(contextMenu, popupMenu);
    if (viewMenu)
        openBackgroundSubMenu(contextMenu, viewMenu, point);
    checkLE(DestroyMenu(popupMenu));
}

void FolderWindow::openBackgroundSubMenu(CComPtr<IContextMenu> contextMenu, HMENU subMenu,
        POINT point) {
    int cmd = TrackPopupMenuEx(subMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, hwnd, nullptr);
    if (cmd > 0) {
        CComPtr<IFolderView2> folderView;
        if (checkHR(browser->GetCurrentView(IID_PPV_ARGS(&folderView))))
            checkHR(IUnknown_SetSite(contextMenu, folderView));
        invokeContextMenuCommand(contextMenu, cmd - 1, point);
        checkHR(IUnknown_SetSite(contextMenu, nullptr));
    }
}

/* IUnknown */

STDMETHODIMP FolderWindow::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        QITABENT(FolderWindow, IServiceProvider),
        QITABENT(FolderWindow, ICommDlgBrowser),
        QITABENT(FolderWindow, ICommDlgBrowser2),
        {},
    };
    HRESULT hr = QISearch(this, interfaces, id, obj);
    if (SUCCEEDED(hr))
        return hr;
    return ItemWindow::QueryInterface(id, obj);
}

STDMETHODIMP_(ULONG) FolderWindow::AddRef() {
    return ItemWindow::AddRef();
}

STDMETHODIMP_(ULONG) FolderWindow::Release() {
    return ItemWindow::Release();
}

/* IServiceProvider */

STDMETHODIMP FolderWindow::QueryService(REFGUID guidService, REFIID riid, void **ppv) {
    HRESULT result = E_NOINTERFACE;
    *ppv = nullptr;

    if (guidService == SID_SExplorerBrowserFrame) {
        // use ICommDlgBrowser implementation to receive selection events
        result = QueryInterface(riid, ppv);
    }
    return result;
}

/* ICommDlgBrowser */

// called when double-clicking a file
STDMETHODIMP FolderWindow::OnDefaultCommand(IShellView *view) {
    if (selected && settings::getDeselectOnOpen()) { // single selection
        selected = nullptr; // prevent recursion
        CComQIPtr<IFolderView2> folderView(view);
        if (folderView)
            folderView->InvokeVerbOnSelection(nullptr);
        clearSelection();
        return S_OK;
    }
    return S_FALSE; // perform default action
}

STDMETHODIMP FolderWindow::OnStateChange(IShellView *view, ULONG change) {
    if (change == CDBOSC_SELCHANGE) {
        if (!ignoreNextSelection) {
            updateSelectionOnActivate = false;
            if (GetActiveWindow() != hwnd) { // in background
                CComQIPtr<IFolderView2> folderView(view);
                int numSelected;
                if (folderView && checkHR(folderView->ItemCount(SVGIO_SELECTION, &numSelected))
                        && numSelected == 1) {
                    // this could happen when dragging a file. don't try to create any windows yet
                    updateSelectionOnActivate = true;
                }
            }
            if (!updateSelectionOnActivate)
                selectionChanged();
        }
        ignoreNextSelection = false;

        // note: sometimes the first SELCHANGE event occurs before navigation is complete and
        // updateStatus() will fail (often when visiting a folder for the first time)
        if (hasStatusText())
            updateStatus();
    }
    return S_OK;
}

STDMETHODIMP FolderWindow::IncludeObject(IShellView *, PCUITEMID_CHILD childID) {
    // will only be called on Desktop, thanks to CDB2GVF_NOINCLUDEITEM
    for (int i = 0; i < _countof(hiddenItemIDs); i++) {
        if (hiddenItemIDs[i] && ILIsEqual(childID, hiddenItemIDs[i]))
            return S_FALSE;
    }
    return S_OK;
}

/* ICommDlgBrowser2 */

STDMETHODIMP FolderWindow::GetDefaultMenuText(IShellView *, wchar_t *, int) {
    return S_FALSE;
}

STDMETHODIMP FolderWindow::GetViewFlags(DWORD *flags) {
    *flags = CDB2GVF_NOSELECTVERB | CDB2GVF_NOINCLUDEITEM;
    return S_OK;
}

STDMETHODIMP FolderWindow::Notify(IShellView *, DWORD) {
    return S_OK;
}

/* IExplorerBrowserEvents */

STDMETHODIMP FolderWindow::OnNavigationPending(PCIDLIST_ABSOLUTE) {
    return S_OK;
}

STDMETHODIMP FolderWindow::OnNavigationComplete(PCIDLIST_ABSOLUTE) {
    // note: often the item count will be incorrect at this point (esp. if the folder is already
    // visited), but between this and OnStateChange, we'll usually end up with the right value.
    if (hasStatusText())
        updateStatus();
    return S_OK;
}

STDMETHODIMP FolderWindow::OnNavigationFailed(PCIDLIST_ABSOLUTE) {
    return S_OK;
}

STDMETHODIMP FolderWindow::OnViewCreated(IShellView *view) {
    shellView = view;

    bool visited = false; // folder has been visited before
    if (propBag) {
        VARIANT var = {};
        var.vt = VT_BOOL;
        if (SUCCEEDED(propBag->Read(PROP_VISITED, &var, nullptr)))
            visited = !!var.boolVal;
    }
    if (!visited) {
        CComQIPtr<IFolderView2> folderView(view);
        if (folderView)
            initDefaultView(folderView);
    }

    if (child) {
        // window was created by clicking the parent button OR onItemChanged was called
        ignoreNextSelection = true; // TODO jank
        CComHeapPtr<ITEMID_CHILD> childID;
        checkHR(CComQIPtr<IParentAndItem>(child->item)
            ->GetParentAndItem(nullptr, nullptr, &childID));
        checkHR(view->SelectItem(childID,
            SVSI_SELECT | SVSI_FOCUSED | SVSI_ENSUREVISIBLE | SVSI_NOTAKEFOCUS));
    }

    return S_OK;
}

} // namespace
