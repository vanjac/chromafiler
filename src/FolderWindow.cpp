#include "FolderWindow.h"
#include "RectUtils.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>
#include <Propvarutil.h>

// Example of how to host an IExplorerBrowser:
// https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/shell/appplatform/ExplorerBrowserCustomContents

namespace chromabrowse {

const wchar_t FOLDER_WINDOW_CLASS[] = L"Folder Window";

const wchar_t PROP_VISITED[] = L"chromabrowse.visited";
const wchar_t PROP_SIZE[] = L"chromabrowse.size";

// local property bags can be found at:
// HKEY_CURRENT_USER\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell\Bags
CComPtr<IPropertyBag> getItemPropertyBag(CComPtr<IShellItem> item, wchar_t *name) {
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

FolderWindow::FolderWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
        : ItemWindow(parent, item) {
    propBag = getItemPropertyBag(item, propertyBag());
}

const wchar_t * FolderWindow::className() {
    return FOLDER_WINDOW_CLASS;
}

bool FolderWindow::preserveSize() const {
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
    return {231, 450}; // just wide enough for scrollbar tooltips
}

wchar_t * FolderWindow::propertyBag() const {
    return L"chromabrowse";
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

void FolderWindow::initDefaultView(CComPtr<IFolderView2> folderView) {
    // FVM_SMALLICON only seems to work if it's also specified with an icon size
    // TODO should this be the shell small icon size?
    // https://docs.microsoft.com/en-us/windows/win32/menurc/about-icons
    checkHR(folderView->SetViewModeAndIconSize(FVM_SMALLICON, GetSystemMetrics(SM_CXSMICON)));
}

void FolderWindow::onDestroy() {
    VARIANT var = {};
    if (checkHR(InitVariantFromBoolean(TRUE, &var))) {
        checkHR(propBag->Write(PROP_VISITED, &var));
    }
    if (sizeChanged && propBag) {
        if (checkHR(InitVariantFromUInt32(MAKELONG(lastSize.cx, lastSize.cy), &var))) {
            debugPrintf(L"Write window size\n");
            checkHR(propBag->Write(PROP_SIZE, &var));
        }
    }
    ItemWindow::onDestroy();
    if (browser) {
        checkHR(browser->Unadvise(eventsCookie));
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
        if (updateSelectionOnActivate) {
            selectionChanged();
            updateSelectionOnActivate = false;
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
    CComPtr<IFolderView2> folderView;
    if (checkHR(browser->GetCurrentView(IID_PPV_ARGS(&folderView)))) {
        int numSelected;
        if (checkHR(folderView->ItemCount(SVGIO_SELECTION, &numSelected)) && numSelected == 1) {
            int index;
            // GetSelectedItem seems to ignore the iStart parameter!
            if (folderView->GetSelectedItem(-1, &index) == S_OK) {
                CComPtr<IShellItem> selected;
                if (checkHR(folderView->GetItem(index, IID_PPV_ARGS(&selected)))) {
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

void FolderWindow::onItemChanged() {
    ItemWindow::onItemChanged();
    propBag = getItemPropertyBag(item, propertyBag());
    shellView = nullptr;
    checkHR(browser->SetOptions(EBO_NOBORDER));
    checkHR(browser->BrowseToObject(item, SBSP_ABSOLUTE));
    checkHR(browser->SetOptions(EBO_NAVIGATEONCE | EBO_NOBORDER));
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
    bool visited = false; // folder has been visited by chromabrowse before
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
