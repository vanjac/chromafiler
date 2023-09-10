#include "FolderWindow.h"
#include "GeomUtils.h"
#include "WinUtils.h"
#include "Settings.h"
#include "DPI.h"
#include "UIStrings.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <propkey.h>
#include <Propvarutil.h>
#include <VersionHelpers.h>

// Example of how to host an IExplorerBrowser:
// https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/shell/appplatform/ExplorerBrowserCustomContents

namespace chromafiler {

const wchar_t FOLDER_WINDOW_CLASS[] = L"ChromaFile Folder";

const wchar_t PROP_VISITED[] = L"Visited";

const EXPLORER_BROWSER_OPTIONS BROWSER_OPTIONS = EBO_NOBORDER | EBO_NOTRAVELLOG;

// on the Desktop only
const wchar_t * const HIDDEN_ITEM_PARSE_NAMES[] = {
    L"::{26EE0668-A00A-44D7-9371-BEB064C98683}", // Control Panel (must be 0)
    L"::{018D5C66-4533-4307-9B53-224DE2ED1FE6}", // OneDrive (may fail if not installed)
    L"::{031E4825-7B94-4DC3-B131-E946B44C8DD5}", // Libraries
    // added in build 22621.675  >:(
    L"::{F874310E-B6B7-47DC-BC84-B9E6B38F5903}", // Home
    L"::{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}", // Desktop (why???)
    L"::{A8CDFF1C-4878-43BE-B5FD-F8091C1C60D0}", // Documents
    L"::{374DE290-123F-4565-9164-39C4925E467B}", // Downloads
    L"::{1CF1260C-4DD0-4EBB-811F-33C572699FDE}", // Music
    L"::{3ADD1653-EB32-4CB0-BBD7-DFA0ABB5ACCA}", // Pictures
    L"::{A0953C92-50DC-43BF-BE83-3742FED03C9C}", // Videos
    // special items that are NOT hidden: user folder, This PC, Network, Recycle Bin, Linux
};
static CComPtr<IShellItem> controlPanelItem;
static CComHeapPtr<ITEMID_CHILD> hiddenItemIDs[_countof(HIDDEN_ITEM_PARSE_NAMES)];

void FolderWindow::init() {
    RegisterClass(tempPtr(createWindowClass(FOLDER_WINDOW_CLASS)));

    for (int i = 0; i < _countof(HIDDEN_ITEM_PARSE_NAMES); i++) {
        CComPtr<IShellItem> item;
        if (SUCCEEDED(SHCreateItemFromParsingName(HIDDEN_ITEM_PARSE_NAMES[i], nullptr,
                IID_PPV_ARGS(&item)))) {
            checkHR(CComQIPtr<IParentAndItem>(item)
                ->GetParentAndItem(nullptr, nullptr, &hiddenItemIDs[i]));
            if (i == 0)
                controlPanelItem = item;
        }
    }
}

FolderWindow::FolderWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
        : ItemWindow(parent, item) {}

const wchar_t * FolderWindow::className() const {
    return FOLDER_WINDOW_CLASS;
}

bool FolderWindow::persistSizeInParent() const {
    return false;
}

bool FolderWindow::useDefaultStatusText() const {
    return false;
}

SIZE FolderWindow::defaultSize() const {
    return scaleDPI(settings::getFolderWindowSize());
}

void FolderWindow::resetPropBag(CComPtr<IPropertyBag> bag) {
    ItemWindow::resetPropBag(bag);
    CComVariant visitedVar(false);
    checkHR(bag->Write(PROP_VISITED, &visitedVar));
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
    browserRect.bottom += CAPTION_HEIGHT; // initial rect is wrong

    if (!checkHR(browser.CoCreateInstance(__uuidof(ExplorerBrowser))))
        return;
    checkHR(browser->SetOptions(BROWSER_OPTIONS));
    CComQIPtr<IFolderViewOptions> options(browser);
    if (options) {
        const auto enableFlags = FVO_VISTALAYOUT | FVO_CUSTOMPOSITION | FVO_CUSTOMORDERING;
        options->SetFolderViewOptions(enableFlags, enableFlags);
    }
    if (!checkHR(browser->Initialize(hwnd, &browserRect, tempPtr(folderSettings())))) {
        browser = nullptr;
        return;
    }

    // AWFUL HACK: IExplorerBrowser performs the first navigation synchronously and subsequent ones
    // asynchronously. So to force it to browse asynchronously, we first tell it to browse to
    // control panel, which it can't load since frames aren't enabled.
    checkHR(browser->BrowseToObject(controlPanelItem, SBSP_ABSOLUTE));

    checkHR(IUnknown_SetSite(browser, (IServiceProvider *)this));
    checkHR(browser->Advise(this, &eventsCookie));
    checkHR(browser->SetPropertyBag(propBagName()));
    // will call IExplorerBrowserEvents callbacks
    HRESULT hr;
    if (!checkHR(hr = browser->BrowseToObject(item, SBSP_ABSOLUTE))) {
        if (hasStatusText())
            setStatusText(getErrorMessage(hr).get());
    }
    checkHR(browser->SetOptions(BROWSER_OPTIONS | EBO_NAVIGATEONCE)); // no further navigation
}

void FolderWindow::addToolbarButtons(HWND tb) {
    TBBUTTON buttons[] = {
        makeToolbarButton(MDL2_CALCULATOR_ADDITION, IDM_NEW_ITEM_MENU, BTNS_WHOLEDROPDOWN),
        makeToolbarButton(MDL2_VIEW, IDM_VIEW_MENU, BTNS_WHOLEDROPDOWN),
        makeToolbarButton(MDL2_REFRESH, IDM_REFRESH, 0),
    };
    SendMessage(tb, TB_ADDBUTTONS, _countof(buttons), (LPARAM)buttons);
    ItemWindow::addToolbarButtons(tb);
}

int FolderWindow::getToolbarTooltip(WORD command) {
    switch (command) {
        case IDM_REFRESH:
            return IDS_REFRESH_COMMAND;
        case IDM_NEW_ITEM_MENU:
            return IDS_NEW_ITEM_COMMAND;
        case IDM_VIEW_MENU:
            return IDS_VIEW_COMMAND;
    }
    return ItemWindow::getToolbarTooltip(command);
}

FOLDERSETTINGS FolderWindow::folderSettings() const {
    FOLDERSETTINGS settings = {};
    settings.ViewMode = FVM_DETAILS; // also set in initDefaultView
    settings.fFlags = FWF_NOWEBVIEW | FWF_NOHEADERINALLVIEWS;
    return settings;
}

void FolderWindow::initDefaultView(CComPtr<IFolderView2> folderView) {
    // FVM_SMALLICON only seems to work if it's also specified with an icon size
    // https://docs.microsoft.com/en-us/windows/win32/menurc/about-icons
    checkHR(folderView->SetCurrentViewMode(FVM_DETAILS));
    checkHR(folderView->SetCurrentFolderFlags(FWF_AUTOARRANGE, FWF_AUTOARRANGE));
    CComQIPtr<IColumnManager> columnMgr(folderView);
    if (columnMgr) {
        PROPERTYKEY keys[] = {PKEY_ItemNameDisplay};
        columnMgr->SetColumns(keys, _countof(keys));
    }
}

void FolderWindow::listViewCreated() {
    if (!browser)
        return;
    firstODDispInfo = false;
    HWND browserControl = FindWindowEx(hwnd, nullptr, L"ExplorerBrowserControl", nullptr);
    if (!checkLE(browserControl)) return;
    HWND defView = FindWindowEx(browserControl, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!checkLE(defView)) return;
    listView = FindWindowEx(defView, nullptr, WC_LISTVIEW, nullptr);
    if (!checkLE(listView)) return;
    ListView_SetExtendedListViewStyleEx(listView, LVS_EX_COLUMNSNAPPOINTS, 0);
    SetWindowSubclass(listView, listViewSubclassProc, 0, (DWORD_PTR)this);
    SetWindowSubclass(defView, listViewOwnerProc, 0, (DWORD_PTR)this);
    SIZE size = clientSize(listView);
    SendMessage(listView, WM_SIZE, SIZE_RESTORED, MAKELPARAM(size.cx, size.cy));
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

        // sometimes a double click isn't registered after a preview handler is opened
        // https://devblogs.microsoft.com/oldnewthing/20041018-00/?p=37543
        DWORD time = GetMessageTime();
        POINT pos = pointFromLParam(lParam);
        if (time - window->clickTime <= GetDoubleClickTime()
                && abs(pos.x - window->clickPos.x) <= GetSystemMetrics(SM_CXDOUBLECLK)/2
                && abs(pos.y - window->clickPos.y) <= GetSystemMetrics(SM_CYDOUBLECLK)/2) {
            debugPrintf(L"Recovered lost double-click\n");
            return SendMessage(hwnd, WM_LBUTTONDBLCLK, wParam, lParam);
        }
        window->clickTime = time;
        window->clickPos = pos;
    } else if (message == WM_LBUTTONDBLCLK) {
        ((FolderWindow *)refData)->clickTime = 0;
    } else if (message == WM_SIZE) {
        if (ListView_GetView(hwnd) == LV_VIEW_DETAILS
                && !((FolderWindow *)refData)->handlingSetColumnWidth) {
            if (HWND header = ListView_GetHeader(hwnd)) {
                if (Header_GetItemCount(header) == 1) {
                    // post instead of send to reduce scrollbar flicker
                    PostMessage(hwnd, LVM_SETCOLUMNWIDTH, 0, MAKELPARAM(LOWORD(lParam) - 1, 0));
                }
            }
        }
    } else if (message == WM_CHAR && wParam == VK_ESCAPE) {
        ((FolderWindow *)refData)->clearSelection();
        // pass to superclass which will also cancel current cut operation
    } else if (message == LVM_SETCOLUMNWIDTH) {
        ((FolderWindow *)refData)->handlingSetColumnWidth = true;
        LRESULT res = DefSubclassProc(hwnd, message, wParam, lParam);
        ((FolderWindow *)refData)->handlingSetColumnWidth = false;
        return res;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK FolderWindow::listViewOwnerProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    if (message == WM_NOTIFY) {
        NMHDR *nmHdr = (NMHDR *)lParam;
        FolderWindow *window = (FolderWindow *)refData;
        if (nmHdr->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *nmLV = (NMLISTVIEW *)nmHdr;
            if ((nmLV->uChanged & LVIF_STATE)) {
                if ((nmLV->uOldState & LVIS_SELECTED) != (nmLV->uNewState & LVIS_SELECTED)) {
                    window->selectionChanged();
                } else if (((nmLV->uOldState ^ nmLV->uNewState) & LVIS_FOCUSED)
                        && nmLV->iItem == -1) {
                    // seems to happen when window contents are modified while in the background
                    // TODO: this only works if window has been active once before
                    if (window->hasStatusText())
                        window->updateStatus();
                }
            }
        } else if (nmHdr->code == LVN_ODSTATECHANGED) {
            NMLVODSTATECHANGE *nmOD = (NMLVODSTATECHANGE *)nmHdr;
            if ((nmOD->uOldState & LVIS_SELECTED) != (nmOD->uNewState & LVIS_SELECTED))
                window->selectionChanged();
        } else if (nmHdr->code == LVN_GETDISPINFO && !window->firstODDispInfo) {
            window->firstODDispInfo = true;
            LRESULT res = DefSubclassProc(hwnd, message, wParam, lParam);
            // initial item count should be known at this point
            if (window->hasStatusText())
                window->updateStatus();
            return res;
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void FolderWindow::onDestroy() {
    if (shellView) {
        if (auto bag = getPropBag()) {
            // view settings are only written when shell view is destroyed
            CComVariant visitedVar(true);
            checkHR(bag->Write(PROP_VISITED, &visitedVar));
        }
    } else {
        // prevent view settings from being trashed before navigation complete
        checkHR(browser->SetPropertyBag(L""));
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

LRESULT FolderWindow::onDropdown(int command, POINT pos) {
    switch (command) {
        case IDM_NEW_ITEM_MENU:
            openNewItemMenu(pos);
            return TBDDRET_DEFAULT;
        case IDM_VIEW_MENU:
            openViewMenu(pos);
            return TBDDRET_DEFAULT;
    }
    return ItemWindow::onDropdown(command, pos);
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
            updateSelection();
            updateSelectionOnActivate = false;
        }
    }
    if (state == WA_CLICKACTIVE) {
        POINT cursor = {};
        GetCursorPos(&cursor);
        if (PtInRect(tempPtr(windowBody()), screenToClient(hwnd, cursor)))
            clickActivate = true;
    }
}

void FolderWindow::onSize(SIZE size) {
    ItemWindow::onSize(size);

    if (browser)
        checkHR(browser->SetRect(nullptr, windowBody()));
}

void FolderWindow::selectionChanged() {
    if (!ignoreInitialSelection) {
        updateSelectionOnActivate = false;
        if (GetActiveWindow() != hwnd) { // in background
            // this could happen when dragging a file. don't try to create any windows yet
            // sometimes items also become deselected and then reselected
            // eg. when a file is deleted from a folder
            // Note: this also happens when certain operations create a progress window
            updateSelectionOnActivate = true;
        } else if (!selectionDirty) {
            selectionDirty = true;
            PostMessage(hwnd, MSG_SELECTION_CHANGED, 0, 0);
        }
    } else if (hasStatusText()) {
        updateStatus(); // make sure parent shows 1 selected
    }
    ignoreInitialSelection = false;
}

LRESULT FolderWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == MSG_SELECTION_CHANGED) {
        selectionDirty = false;
        updateSelection();
    }
    return ItemWindow::handleMessage(message, wParam, lParam);
}

void FolderWindow::updateSelection() {
    CComPtr<IFolderView2> folderView;
    if (FAILED(browser->GetCurrentView(IID_PPV_ARGS(&folderView))))
        return;
    int numSelected;
    if (!checkHR(folderView->ItemCount(SVGIO_SELECTION, &numSelected)))
        return;
    if (numSelected == 1) {
        CComPtr<IShellItemArray> selection;
        if (folderView->GetSelection(FALSE, &selection) == S_OK) {
            CComPtr<IShellItem> newSelected;
            if (checkHR(selection->GetItemAt(0, &newSelected))) {
                int compare = 1;
                if (selected)
                    checkHR(newSelected->Compare(selected, SICHINT_CANONICAL, &compare));
                selected = newSelected;
                // openChild() could cause a permission dialog to appear,
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

    // note: sometimes the first selection change event occurs before navigation is complete and
    // updateStatus() will fail (often when visiting a folder for the first time)
    if (hasStatusText())
        updateStatus();
}

void FolderWindow::updateStatus() {
    // TODO: duplicate work in updateSelection()
    CComPtr<IFolderView2> folderView;
    if (FAILED(browser->GetCurrentView(IID_PPV_ARGS(&folderView))))
        return;
    int numItems = 0, numSelected = 0;
    checkHR(folderView->ItemCount(SVGIO_ALLVIEW, &numItems));
    checkHR(folderView->ItemCount(SVGIO_SELECTION, &numSelected));
    local_wstr_ptr status;
    if (numSelected == 0) {
        status = formatString(IDS_FOLDER_STATUS, numItems);
    } else {
        status = formatString(IDS_FOLDER_STATUS_SEL, numItems, numSelected);
    }
    setStatusText(status.get());
}

void FolderWindow::clearSelection() {
    if (shellView)
        checkHR(shellView->SelectItem(nullptr, SVSI_DESELECTOTHERS)); // keep focus
    selected = nullptr; // in case this happened in the background
}

void FolderWindow::onChildDetached() {
    ItemWindow::onChildDetached();
    clearSelection();
}

IDispatch * FolderWindow::getDispatch() {
    return this;
}

void FolderWindow::onItemChanged() {
    ItemWindow::onItemChanged();
    if (browser) {
        shellView = nullptr;
        checkHR(browser->SetOptions(BROWSER_OPTIONS)); // temporarily enable navigation
        checkHR(browser->BrowseToObject(item, SBSP_ABSOLUTE));
        checkHR(browser->SetOptions(BROWSER_OPTIONS | EBO_NAVIGATEONCE));
    }
}

void FolderWindow::refresh() {
    ItemWindow::refresh();
    if (listView && ListView_GetView(listView) == LV_VIEW_DETAILS)
        SendMessage(listView, WM_VSCROLL, SB_TOP, 0); // fix drawing glitch on refresh
    if (shellView)
        checkHR(shellView->Refresh()); // TODO: invoke context menu verb instead?
    firstODDispInfo = false; // TODO won't be called if invoked from context menu!
}

CComPtr<IContextMenu> FolderWindow::queryBackgroundMenu(HMENU *popupMenu) {
    if (!shellView)
        return nullptr;
    CComPtr<IContextMenu> contextMenu;
    if (!checkHR(shellView->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&contextMenu))))
        return nullptr;
    if ((*popupMenu = CreatePopupMenu()) == nullptr)
        return nullptr;
    if (!checkHR(contextMenu->QueryContextMenu(*popupMenu, 0, IDM_SHELL_FIRST, IDM_SHELL_LAST,
            CMF_OPTIMIZEFORINVOKE))) {
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

void FolderWindow::trackContextMenu(POINT pos) {
    if (!shellView)
        return;
    UINT contextFlags = CMF_CANRENAME;
    if (GetKeyState(VK_SHIFT) < 0)
        contextFlags |= CMF_EXTENDEDVERBS;
    CComPtr<IContextMenu> contextMenu;
    if (SUCCEEDED(shellView->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&contextMenu)))) {
        contextFlags |= CMF_ITEMMENU;
    } else if (checkHR(shellView->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&contextMenu)))) {
        contextFlags |= CMF_NODEFAULT;
    } else {
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (menu && checkHR(contextMenu->QueryContextMenu(menu, 0, IDM_SHELL_FIRST, IDM_SHELL_LAST,
            contextFlags))) {
        contextMenu2 = contextMenu;
        contextMenu3 = contextMenu;
        int cmd = ItemWindow::trackContextMenu(pos, menu);
        contextMenu2 = nullptr;
        contextMenu3 = nullptr;
        if (cmd >= IDM_SHELL_FIRST && cmd <= IDM_SHELL_LAST) {
            auto info = makeInvokeInfo(cmd - IDM_SHELL_FIRST, pos);
            CComHeapPtr<wchar_t> path;
            checkHR(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &path));
            info.lpDirectoryW = path;
            // convert to ANSI
            int pathASize = WideCharToMultiByte(CP_ACP, 0, path, -1, nullptr, 0, nullptr, nullptr);
            if (checkLE(pathASize)) {
                std::unique_ptr<char[]> pathA(new char[pathASize]);
                checkLE(WideCharToMultiByte(CP_ACP, 0, path, -1,
                    pathA.get(), pathASize, nullptr, nullptr));
                info.lpDirectory = pathA.get();
            }
            CComPtr<IFolderView2> folderView;
            if (checkHR(browser->GetCurrentView(IID_PPV_ARGS(&folderView))))
                checkHR(IUnknown_SetSite(contextMenu, folderView));
            contextMenu->InvokeCommand((CMINVOKECOMMANDINFO *)&info);
            checkHR(IUnknown_SetSite(contextMenu, nullptr));
        }
    }
    DestroyMenu(menu);
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
        if (!GetMenuItemInfo(itemInfo.hSubMenu, 0, TRUE, &subItemInfo) || (int)subItemInfo.wID <= 0)
            continue;
        wchar_t verb[64];
        verb[0] = 0;
        if (SUCCEEDED(contextMenu->GetCommandString(subItemInfo.wID - IDM_SHELL_FIRST, GCS_VERBW,
                nullptr, (char*)verb, _countof(verb))) && lstrcmpi(verb, CMDSTR_NEWFOLDERW) == 0) {
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
    if (!IsWindows8OrGreater())
        return GetSubMenu(popupMenu, 0);
    for (int i = 0, count = GetMenuItemCount(popupMenu); i < count; i++) {
        MENUITEMINFO itemInfo = {sizeof(itemInfo)};
        itemInfo.fMask = MIIM_ID | MIIM_SUBMENU;
        if (!checkLE(GetMenuItemInfo(popupMenu, i, TRUE, &itemInfo)))
            continue;
        if (!itemInfo.hSubMenu || itemInfo.wID <= 0)
            continue;
        wchar_t verb[64];
        verb[0] = 0;
        if (SUCCEEDED(contextMenu->GetCommandString(itemInfo.wID - IDM_SHELL_FIRST, GCS_VERBW,
                nullptr, (char*)verb, _countof(verb))) && lstrcmpi(verb, L"view") == 0) {
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
    if (cmd >= IDM_SHELL_FIRST && cmd <= IDM_SHELL_LAST) {
        auto info = makeInvokeInfo(cmd - IDM_SHELL_FIRST, point);
        CComPtr<IFolderView2> folderView;
        if (checkHR(browser->GetCurrentView(IID_PPV_ARGS(&folderView))))
            checkHR(IUnknown_SetSite(contextMenu, folderView));
        contextMenu->InvokeCommand((CMINVOKECOMMANDINFO *)&info);
        checkHR(IUnknown_SetSite(contextMenu, nullptr));
    }
}

/* IUnknown */

STDMETHODIMP FolderWindow::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        QITABENT(FolderWindow, IServiceProvider),
        QITABENT(FolderWindow, ICommDlgBrowser),
        QITABENT(FolderWindow, ICommDlgBrowser2),
        QITABENT(FolderWindow, IExplorerBrowserEvents),
        QITABENT(FolderWindow, IDispatch),
        QITABENT(FolderWindow, IWebBrowser),
        QITABENT(FolderWindow, IWebBrowserApp),
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
    *ppv = nullptr;
    if (guidService == SID_SExplorerBrowserFrame) {
        return QueryInterface(riid, ppv); // ICommDlgBrowser
    } else if (guidService == SID_SFolderView) {
        if (shellView)
            return shellView->QueryInterface(riid, ppv); // for SHOpenFolderAndSelectItems
    }
    // SID_STopLevelBrowser is also requested...
    // TODO: forward to IExplorerBrowser?
    return E_NOINTERFACE;
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

STDMETHODIMP FolderWindow::OnStateChange(IShellView *, ULONG change) {
    // CDBOSC_SELCHANGE is unreliable with the old-style ListView
    if (change == CDBOSC_RENAME) {
        // TODO: remove this once there's an automatic system for tracking files
        if (child)
            child->resolveItem();
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

// order: OnNavigationPending, OnViewCreated, OnNavigationComplete
// OR: OnNavigationPending, OnNavigationFailed
STDMETHODIMP FolderWindow::OnNavigationPending(PCIDLIST_ABSOLUTE) {
    return S_OK;
}

STDMETHODIMP FolderWindow::OnNavigationComplete(PCIDLIST_ABSOLUTE) {
    listViewCreated();

    if (child && shellView) {
        // window was created by clicking the parent button OR onItemChanged was called
        ignoreInitialSelection = true; // TODO jank
        CComHeapPtr<ITEMID_CHILD> childID;
        if (checkHR(CComQIPtr<IParentAndItem>(child->item)
                ->GetParentAndItem(nullptr, nullptr, &childID))) {
            if (checkHR(shellView->SelectItem(childID,
                    SVSI_SELECT | SVSI_FOCUSED | SVSI_ENSUREVISIBLE | SVSI_NOTAKEFOCUS)))
                selected = child->item;
        }
    }

    if (shellView && GetActiveWindow() == hwnd)
        checkHR(shellView->UIActivate(SVUIA_ACTIVATE_FOCUS));

    // item count will often be incorrect at this point; see listViewOwnerProc
    if (hasStatusText())
        updateStatus();

    onViewReady();
    return S_OK;
}

STDMETHODIMP FolderWindow::OnNavigationFailed(PCIDLIST_ABSOLUTE) {
    setStatusText(getString(IDS_FOLDER_ERROR));
    return S_OK;
}

STDMETHODIMP FolderWindow::OnViewCreated(IShellView *view) {
    shellView = view;

    bool visited = false; // folder has been visited before
    if (auto bag = getPropBag()) {
        VARIANT var = {VT_BOOL};
        if (SUCCEEDED(bag->Read(PROP_VISITED, &var, nullptr)))
            visited = !!var.boolVal;
    }
    if (!visited) {
        CComQIPtr<IFolderView2> folderView(view);
        if (folderView)
            initDefaultView(folderView);
    }

    return S_OK;
}

/* IDispatch */

STDMETHODIMP FolderWindow::GetTypeInfoCount(UINT *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::GetTypeInfo(UINT, LCID, ITypeInfo **) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::GetIDsOfNames(
    REFIID, LPOLESTR *, UINT, LCID, DISPID *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::Invoke(
    DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *) { return E_NOTIMPL; }

/* IWebBrowser */

STDMETHODIMP FolderWindow::get_Document(IDispatch **dispatch) {
    return QueryInterface(__uuidof(IDispatch), (void **)dispatch); // for SHOpenFolderAndSelectItems
}

STDMETHODIMP FolderWindow::GoBack() { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::GoForward() { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::GoHome() { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::GoSearch() { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::Navigate(
    BSTR, VARIANT *, VARIANT *, VARIANT *, VARIANT *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::Refresh() { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::Refresh2(VARIANT *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::Stop() { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Application(IDispatch **) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Parent(IDispatch **) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Container(IDispatch **) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_TopLevelContainer(VARIANT_BOOL *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Type(BSTR *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Left(long *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::put_Left(long) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Top(long *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::put_Top(long) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Width(long *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::put_Width(long) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Height(long *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::put_Height(long) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_LocationName(BSTR *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_LocationURL(BSTR *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Busy(VARIANT_BOOL *) { return E_NOTIMPL; }

/* IWebBrowserApp */

STDMETHODIMP FolderWindow::get_HWND(SHANDLE_PTR *pHWND) {
    // this window is brought to the foreground when a Shell Window is activated
    *pHWND = (SHANDLE_PTR)hwnd;
    return S_OK;
}

STDMETHODIMP FolderWindow::Quit() { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::ClientToWindow(int *, int *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::PutProperty(BSTR, VARIANT) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::GetProperty(BSTR, VARIANT *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Name(BSTR *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_FullName(BSTR *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Path(BSTR *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_Visible(VARIANT_BOOL *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::put_Visible(VARIANT_BOOL) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_StatusBar(VARIANT_BOOL *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::put_StatusBar(VARIANT_BOOL) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_StatusText(BSTR *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::put_StatusText(BSTR) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_ToolBar(int *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::put_ToolBar(int) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_MenuBar(VARIANT_BOOL *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::put_MenuBar(VARIANT_BOOL) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::get_FullScreen(VARIANT_BOOL *) { return E_NOTIMPL; }
STDMETHODIMP FolderWindow::put_FullScreen(VARIANT_BOOL) { return E_NOTIMPL; }

} // namespace
