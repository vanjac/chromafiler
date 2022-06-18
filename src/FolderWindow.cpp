#include "FolderWindow.h"
#include "RectUtil.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>
#include <Propvarutil.h>

// Example of how to host an IExplorerBrowser:
// https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/shell/appplatform/ExplorerBrowserCustomContents

namespace chromabrowse {

const wchar_t *FOLDER_WINDOW_CLASS = L"Folder Window";
const wchar_t *PROPERTY_BAG = L"chromabrowse";
const wchar_t *PROP_VISITED = L"chromabrowse.visited";
const wchar_t *PROP_SIZE = L"chromabrowse.size";

void FolderWindow::init() {
    WNDCLASS wndClass = createWindowClass(FOLDER_WINDOW_CLASS);
    RegisterClass(&wndClass);
}

FolderWindow::FolderWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
        : ItemWindow(parent, item) {
    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        checkHR(SHGetViewStatePropertyBag(idList, PROPERTY_BAG, SHGVSPB_FOLDERNODEFAULTS,
            IID_PPV_ARGS(&propBag)));
    }
}

const wchar_t * FolderWindow::className() {
    return FOLDER_WINDOW_CLASS;
}

bool FolderWindow::preserveSize() {
    return false;
}

SIZE FolderWindow::requestedSize() {
    if (propBag) {
        VARIANT sizeVar = {};
        sizeVar.vt = VT_UI4;
        if (SUCCEEDED(propBag->Read(PROP_SIZE, &sizeVar, nullptr))) {
            return {GET_X_LPARAM(sizeVar.ulVal), GET_Y_LPARAM(sizeVar.ulVal)};
        }
    }
    return {231, 450}; // just wide enough for scrollbar tooltips
}

LRESULT FolderWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_NEW_FOLDER:
                    newFolder();
                    return 0;
            }
            break;
    }
    return ItemWindow::handleMessage(message, wParam, lParam);
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
    folderSettings.ViewMode = FVM_SMALLICON; // doesn't work correctly (see below)
    folderSettings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW | FWF_NOHEADERINALLVIEWS;
    if (!checkHR(browser.CoCreateInstance(__uuidof(ExplorerBrowser))))
        return;
    if (!checkHR(browser->Initialize(hwnd, &browserRect, &folderSettings))) {
        browser = nullptr;
        return;
    }
    checkHR(browser->SetOptions(EBO_NAVIGATEONCE)); // no navigation

    checkHR(browser->SetPropertyBag(PROPERTY_BAG));
    if (!checkHR(browser->BrowseToObject(item, SBSP_ABSOLUTE))) {
        // eg. browsing a subdirectory in the recycle bin
        checkHR(browser->Destroy());
        browser = nullptr;
        return;
    }

    bool visited = false; // folder has been visited by chromabrowse before
    if (propBag) {
        VARIANT var = {};
        if (SUCCEEDED(propBag->Read(PROP_VISITED, &var, nullptr))) {
            visited = true;
        } else {
            if (checkHR(InitVariantFromBoolean(TRUE, &var))) {
                checkHR(propBag->Write(PROP_VISITED, &var));
            }
        }
    }

    CComPtr<IFolderView2> view;
    if (!visited && checkHR(browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
        // FVM_SMALLICON only seems to work if it's also specified with an icon size
        // TODO should this be the shell small icon size?
        // https://docs.microsoft.com/en-us/windows/win32/menurc/about-icons
        checkHR(view->SetViewModeAndIconSize(FVM_SMALLICON, GetSystemMetrics(SM_CXSMICON))); // = 16
    }

    if (checkHR(browser->GetCurrentView(IID_PPV_ARGS(&shellView)))) {
        if (child) {
            // window was created by clicking the parent button
            ignoreNextSelection = true; // TODO jank
            CComHeapPtr<ITEMID_CHILD> childID;
            checkHR(CComQIPtr<IParentAndItem>(child->item)
                ->GetParentAndItem(nullptr, nullptr, &childID));
            checkHR(shellView->SelectItem(childID,
                SVSI_SELECT | SVSI_FOCUSED | SVSI_ENSUREVISIBLE | SVSI_NOTAKEFOCUS));
        }
    }

    checkHR(IUnknown_SetSite(browser, (IServiceProvider *)this));
}

void FolderWindow::onDestroy() {
    if (sizeChanged && propBag) {
        VARIANT var = {};
        if (checkHR(InitVariantFromUInt32(MAKELONG(lastSize.cx, lastSize.cy), &var))) {
            debugPrintf(L"Write window size\n");
            checkHR(propBag->Write(PROP_SIZE, &var));
        }
    }
    ItemWindow::onDestroy();
    if (browser) {
        checkHR(IUnknown_SetSite(browser, nullptr));
        checkHR(browser->Destroy());
    }
}

void FolderWindow::onActivate(WORD state, HWND prevWindow) {
    ItemWindow::onActivate(state, prevWindow);
    if (state != WA_INACTIVE) {
        if (shellView) {
            // override behavior causing sort columns to be focused when shift is held
            activateOnShiftRelease = GetKeyState(VK_SHIFT) < 0;
            checkHR(shellView->UIActivate(SVUIA_ACTIVATE_FOCUS));
        }
    }
}

void FolderWindow::onSize(int width, int height) {
    ItemWindow::onSize(width, height);

    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    SIZE windowSize = rectSize(windowRect);
    if (lastSize.cx != -1 && (windowSize.cx != lastSize.cx || windowSize.cy != lastSize.cy))
        sizeChanged = true;
    lastSize = windowSize;

    if (browser)
        checkHR(browser->SetRect(nullptr, windowBody()));
}

void FolderWindow::selectionChanged() {
    CComPtr<IFolderView2> view;
    if (checkHR(browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
        int numSelected;
        if (checkHR(view->ItemCount(SVGIO_SELECTION, &numSelected)) && numSelected == 1) {
            int index;
            // GetSelectedItem seems to ignore the iStart parameter!
            if (view->GetSelectedItem(-1, &index) == S_OK) {
                CComPtr<IShellItem> selected;
                if (checkHR(view->GetItem(index, IID_PPV_ARGS(&selected)))) {
                    openChild(selected);
                }
            }
        } else {
            // 0 or more than 1 item selected
            closeChild();
        }
    }
}

void FolderWindow::onChildDetached() {
    if (shellView) {
        // clear selection
        checkHR(shellView->SelectItem(nullptr, SVSI_DESELECTOTHERS));
    }
}

void FolderWindow::refresh() {
    if (shellView) {
        checkHR(shellView->Refresh());
        ignoreNextSelection = true; // fix crash
    }
}

void FolderWindow::newFolder() {
    if (!shellView)
        return;
    CComPtr<IContextMenu> contextMenu;
    if (!checkHR(shellView->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&contextMenu))))
        return;
    HMENU popupMenu = CreatePopupMenu();
    if (!popupMenu)
        return;
    if (checkHR(contextMenu->QueryContextMenu(popupMenu, 0, 1, 0x7FFF, CMF_OPTIMIZEFORINVOKE))) {
        CMINVOKECOMMANDINFO info = {};
        info.cbSize = sizeof(info);
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
    }
    DestroyMenu(popupMenu);
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
    return ItemWindow::AddRef(); // fix diamond inheritance
}

STDMETHODIMP_(ULONG) FolderWindow::Release() {
    return ItemWindow::Release();
}

/* IServiceProvider */

STDMETHODIMP FolderWindow::QueryService(REFGUID guidService, REFIID riid, void **ppv) {
    // services that IExplorerBrowser queries for:
    // 2BD6990F-2D1A-4B5A-9D61-AB6229A36CAA     ??
    // 4C96BE40-915C-11CF-99D3-00AA004AE837     STopLevelBrowser (IShellBrowser, IShellBrowserService...)
    // 2E228BA3-EA25-4378-97B6-D574FAEBA356     ??
    // 3934E4C2-8143-4E4C-A1DC-718F8563F337     ??
    // A36A3ACE-8332-45CE-AA29-503CB76B2587     ??
    // 04BA120E-AD52-4A2D-9807-2DA178D0C3E1     IFolderTypeModifier
    // DD1E21CC-E2C7-402C-BF05-10328D3F6BAD     IBrowserSettings
    // 16770868-239C-445B-A01D-F26C7FBBF26C     ??
    // 6CCB7BE0-6807-11D0-B810-00C04FD706EC     IShellTaskScheduler
    // 000214F1-0000-0000-C000-000000000046     ICommDlgBrowser / SExplorerBrowserFrame
    // 05A89298-6246-4C63-BB0D-9BDAF140BF3B     IBrowserWithActivationNotification
    // 00021400-0000-0000-C000-000000000046     Desktop
    // E38FE0F3-3DB0-47EE-A314-25CF7F4BF521     IInfoBarHost https://stackoverflow.com/a/63954982
    // D7F81F62-491F-49BC-891D-5665085DF969     ??
    // FAD451C2-AF58-4161-B9FF-57AFBBED0AD2     ??
    // 9EA5491C-89C8-4BEF-93D3-7F665FB82A33     IFileDialogPrivate
    // 0AEE655F-9B0F-493F-9843-A4A0ABBE928F     ??
    // 31E4FA78-02B4-419F-9430-7B7585237C77     IFrameManager
    // 5C65B0D2-FC07-48B6-BA51-4597DAC84747     ??

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
            // TODO this can hang the browser and should really be done asynchronously with a message
            // but that adds other complication
            selectionChanged();
        }
        ignoreNextSelection = false;
    }
    return S_OK;
}

STDMETHODIMP FolderWindow::IncludeObject(IShellView *, PCUITEMID_CHILD) {
    return S_OK; // include all objects
}

} // namespace
