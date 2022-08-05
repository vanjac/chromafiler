#include "FolderWindow.h"
#include "RectUtils.h"
#include "Settings.h"
#include "UIStrings.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <Propvarutil.h>

// Example of how to host an IExplorerBrowser:
// https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/shell/appplatform/ExplorerBrowserCustomContents

namespace chromafile {

const wchar_t FOLDER_WINDOW_CLASS[] = L"Folder Window";

const wchar_t PROP_VISITED[] = L"Visited";
const wchar_t PROP_SIZE[] = L"Size";
const wchar_t PROP_CHILD_SIZE[] = L"ChildSize";

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
}

FolderWindow::FolderWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item,
        const wchar_t *propBagOverride)
        : ItemWindow(parent, item) {
    // can't call virtual method in constructor!
    propBag = getItemPropertyBag(item, propBagOverride ? propBagOverride : propertyBag());
    if (propBag) {
        VARIANT sizeVar = {};
        sizeVar.vt = VT_UI4;
        if (SUCCEEDED(propBag->Read(PROP_CHILD_SIZE, &sizeVar, nullptr))) {
            // may be overwritten before create()
            storedChildSize = {GET_X_LPARAM(sizeVar.ulVal), GET_Y_LPARAM(sizeVar.ulVal)};
        }
    }
    oldStoredChildSize = storedChildSize;
}

const wchar_t * FolderWindow::className() {
    return FOLDER_WINDOW_CLASS;
}

bool FolderWindow::preserveSize() const {
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
            return {GET_X_LPARAM(sizeVar.ulVal), GET_Y_LPARAM(sizeVar.ulVal)};
        }
    }
    return settings::getFolderWindowSize();
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
    if (clickActivate && msg->message == WM_LBUTTONUP) {
        clickActivate = false;
        clickActivateRelease = true;
    } else {
        clickActivateRelease = false;
    }
    return false;
}

void FolderWindow::onCreate() {
    ItemWindow::onCreate();

    RECT browserRect = windowBody();
    browserRect.bottom += browserRect.top; // initial rect is wrong

    FOLDERSETTINGS folderSettings = {};
    folderSettings.ViewMode = FVM_SMALLICON; // doesn't work correctly (see initDefaultView)
    folderSettings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW | FWF_NOHEADERINALLVIEWS;
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
    if (!checkHR(browser->BrowseToObject(item, SBSP_ABSOLUTE))) {
        // eg. browsing a subdirectory in the recycle bin
        checkHR(browser->Destroy());
        browser = nullptr;
        return;
    }
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
    // TODO should this be the shell small icon size?
    // https://docs.microsoft.com/en-us/windows/win32/menurc/about-icons
    checkHR(folderView->SetViewModeAndIconSize(FVM_SMALLICON, GetSystemMetrics(SM_CXSMICON)));
}

void FolderWindow::onDestroy() {
    if (propBag) {
        VARIANT var = {};
        if (checkHR(InitVariantFromBoolean(TRUE, &var))) {
            checkHR(propBag->Write(PROP_VISITED, &var));
        }
        if (sizeChanged) {
            if (checkHR(InitVariantFromUInt32(MAKELONG(lastSize.cx, lastSize.cy), &var))) {
                debugPrintf(L"Write window size\n");
                checkHR(propBag->Write(PROP_SIZE, &var));
            }
        }
        if (!sizeEqual(storedChildSize, oldStoredChildSize)) {
            if (checkHR(InitVariantFromUInt32(
                    MAKELONG(storedChildSize.cx, storedChildSize.cy), &var))) {
                debugPrintf(L"Write child size\n");
                checkHR(propBag->Write(PROP_CHILD_SIZE, &var));
            }
        }
    }
    ItemWindow::onDestroy();
    if (browser) {
        checkHR(browser->Unadvise(eventsCookie));
        checkHR(IUnknown_SetSite(browser, nullptr));
        checkHR(browser->Destroy());
    }
}

bool FolderWindow::onCommand(WORD command) {
    switch (command) {
        case IDM_NEW_FOLDER:
            newFolder();
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
    clickActivate = (state == WA_CLICKACTIVE);
}

void FolderWindow::onSize(int width, int height) {
    ItemWindow::onSize(width, height);

    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    SIZE windowSize = rectSize(windowRect);
    if (lastSize.cx != -1 && !sizeEqual(windowSize, lastSize))
        sizeChanged = true;
    lastSize = windowSize;

    if (browser)
        checkHR(browser->SetRect(nullptr, windowBody()));
}

void FolderWindow::selectionChanged() {
    CComPtr<IFolderView2> folderView;
    if (checkHR(browser->GetCurrentView(IID_PPV_ARGS(&folderView)))) {
        int numSelected;
        if (checkHR(folderView->ItemCount(SVGIO_SELECTION, &numSelected)) && numSelected == 1) {
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
            if (numSelected == 0 && clickActivateRelease && selected) {
                debugPrintf(L"Blocking deselection\n");
                CComHeapPtr<ITEMID_CHILD> selectedID;
                checkHR(CComQIPtr<IParentAndItem>(selected)
                    ->GetParentAndItem(nullptr, nullptr, &selectedID));
                checkHR(shellView->SelectItem(selectedID, SVSI_SELECT | SVSI_NOTAKEFOCUS));
            } else {
                selected = nullptr;
                closeChild();
            }
        }
    }
}

void FolderWindow::onChildDetached() {
    if (shellView) {
        // clear selection
        checkHR(shellView->SelectItem(nullptr, SVSI_DESELECTOTHERS));
    }
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
        DestroyMenu(*popupMenu);
        return nullptr;
    }
    return contextMenu;
}

void FolderWindow::newFolder() {
    HMENU popupMenu;
    CComPtr<IContextMenu> contextMenu = queryBackgroundMenu(&popupMenu);
    if (!contextMenu)
        return;
    CMINVOKECOMMANDINFO info = {sizeof(info)};
    info.hwnd = hwnd;
    info.lpVerb = CMDSTR_NEWFOLDERA;
    CComPtr<IFolderView2> folderView;
    if (checkHR(browser->GetCurrentView(IID_PPV_ARGS(&folderView)))) {
        // https://stackoverflow.com/q/40497455
        // allow command to select new folder for renaming. not documented of course :/
        checkHR(IUnknown_SetSite(contextMenu, folderView));
    }
    checkHR(contextMenu->InvokeCommand(&info));
    checkHR(IUnknown_SetSite(contextMenu, nullptr));

    DestroyMenu(popupMenu);
}

HMENU findNewItemMenu(CComPtr<IContextMenu> contextMenu, HMENU popupMenu) {
    // search for the submenu that contains NewFolder verb as the first item (TODO: jank)
    CComQIPtr<IContextMenu3> contextMenu3(contextMenu);
    for (int i = 0, count = GetMenuItemCount(popupMenu); i < count; i++) {
        MENUITEMINFO itemInfo = {sizeof(itemInfo)};
        itemInfo.fMask = MIIM_SUBMENU;
        if (!GetMenuItemInfo(popupMenu, i, TRUE, &itemInfo))
            continue;
        if (!itemInfo.hSubMenu || GetMenuItemCount(itemInfo.hSubMenu) == 0)
            continue;
        if (contextMenu3) {
            contextMenu3->HandleMenuMsg2(WM_INITMENUPOPUP,
                (WPARAM)itemInfo.hSubMenu, i, nullptr);
        }
        MENUITEMINFO subItemInfo = {sizeof(subItemInfo)};
        subItemInfo.fMask = MIIM_ID;
        if (!GetMenuItemInfo(itemInfo.hSubMenu, 0, TRUE, &subItemInfo) || (int)subItemInfo.wID <= 0)
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
    DestroyMenu(popupMenu);
}

HMENU findViewMenu(CComPtr<IContextMenu> contextMenu, HMENU popupMenu) {
    for (int i = 0, count = GetMenuItemCount(popupMenu); i < count; i++) {
        MENUITEMINFO itemInfo = {sizeof(itemInfo)};
        itemInfo.fMask = MIIM_ID | MIIM_SUBMENU;
        if (!GetMenuItemInfo(popupMenu, i, TRUE, &itemInfo))
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
    DestroyMenu(popupMenu);
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
STDMETHODIMP FolderWindow::OnDefaultCommand(IShellView *) {
    return S_FALSE; // perform default action
}

STDMETHODIMP FolderWindow::OnStateChange(IShellView *, ULONG change) {
    if (change == CDBOSC_SELCHANGE) {
        if (!ignoreNextSelection) {
            if (GetActiveWindow() != hwnd) {
                // this could happen when dragging a file. don't try to create any windows yet
                updateSelectionOnActivate = true;
            } else {
                selectionChanged();
                updateSelectionOnActivate = false;
            }
        }
        ignoreNextSelection = false;

        // TODO: duplicate work in selectionChanged()
        CComPtr<IFolderView2> folderView;
        if (hasStatusText() && checkHR(browser->GetCurrentView(IID_PPV_ARGS(&folderView)))) {
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
    }
    return S_OK;
}

STDMETHODIMP FolderWindow::IncludeObject(IShellView *, PCUITEMID_CHILD) {
    return S_OK; // include all objects
}

/* IExplorerBrowserEvents */

STDMETHODIMP FolderWindow::OnNavigationPending(PCIDLIST_ABSOLUTE) {
    return S_OK;
}

STDMETHODIMP FolderWindow::OnNavigationComplete(PCIDLIST_ABSOLUTE) {
    return S_OK;
}

STDMETHODIMP FolderWindow::OnNavigationFailed(PCIDLIST_ABSOLUTE) {
    return S_OK;
}

STDMETHODIMP FolderWindow::OnViewCreated(IShellView *view) {
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

    shellView = view;
    return S_OK;
}

} // namespace
