#include "main.h"
#include <stdexcept>
#include <cstdlib>
#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <vssym32.h>
#include <propkey.h>

// https://docs.microsoft.com/en-us/windows/win32/controls/cookbook-overview
#pragma comment(linker,"\"/manifestdependency:type='win32' \
    name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
    processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Example of how to host an IExplorerBrowser:
// https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/shell/appplatform/ExplorerBrowserCustomContents

namespace chromabrowse {

const wchar_t * FolderWindow::CLASS_NAME = L"Folder Window";

// dimensions
const int DEFAULT_WIDTH = 231; // just large enough for scrollbar tooltips
const int DEFAULT_HEIGHT = 450;
const int RESIZE_MARGIN = 8; // TODO use some system metric?
const int CAPTION_PADDING = 8;
const int WINDOW_ICON_PADDING = 4;
const int SNAP_DISTANCE = 32;
// calculated in registerClass()
static int CAPTION_HEIGHT;

static HINSTANCE globalHInstance;
static long numOpenWindows;
static CComPtr<FolderWindow> activeWindow;

void FolderWindow::registerClass() {
    WNDCLASS wndClass = {};
    wndClass.lpfnWndProc = windowProc;
    wndClass.hInstance = globalHInstance;
    wndClass.lpszClassName = CLASS_NAME;
    wndClass.style = CS_HREDRAW; // ensure caption gets redrawn if width changes
    RegisterClass(&wndClass);

    RECT adjustedRect = {};
    AdjustWindowRectEx(&adjustedRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    CAPTION_HEIGHT = -adjustedRect.top; // = 31
}

LRESULT CALLBACK FolderWindow::windowProc(
        HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    FolderWindow *self = nullptr;
    if (message == WM_NCCREATE) {
        CREATESTRUCT *create = (CREATESTRUCT*)lParam;
        self = (FolderWindow*)create->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd = hwnd;
    } else {
        self = (FolderWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }
    if (self) {
        return self->handleMessage(message, wParam, lParam);
    } else {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

FolderWindow::FolderWindow(FolderWindow *parent, CComPtr<IShellItem> item)
    : parent(parent)
    , child(nullptr)
    , item(item)
    , iconLarge(nullptr)
    , iconSmall(nullptr)
    , ignoreNextSelection(false)
    , refCount(1)
{}

FolderWindow::~FolderWindow() {
    if (iconLarge)
        DestroyIcon(iconLarge);
    if (iconSmall)
        DestroyIcon(iconSmall);
}

void FolderWindow::create(RECT rect, int showCommand) {
    if (FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &title)))
        throw std::runtime_error("Unable to get folder name");
    wcout << "Create " <<&title[0]<< "\n";

    CComPtr<IExtractIcon> extractIcon;
    if (SUCCEEDED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&extractIcon)))) {
        wchar_t iconFile[MAX_PATH];
        int index;
        UINT flags;
        if (extractIcon->GetIconLocation(0, iconFile, MAX_PATH, &index, &flags) == S_OK) {
            UINT iconSizes = (GetSystemMetrics(SM_CXSMICON) << 16) + GetSystemMetrics(SM_CXICON);
            if (extractIcon->Extract(iconFile, index, &iconLarge, &iconSmall, iconSizes) != S_OK) {
                wcout << "IExtractIcon failed\n";
                // https://devblogs.microsoft.com/oldnewthing/20140501-00/?p=1103
                SHDefExtractIcon(iconFile, index, flags, &iconLarge, &iconSmall, iconSizes);
            }
        }
    }

    HWND hwnd = CreateWindow(
        CLASS_NAME,             // class name
        title,                  // title
        // style
        // WS_CLIPCHILDREN fixes drawing glitches with the scrollbars
        (WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX & ~WS_MAXIMIZEBOX) | WS_CLIPCHILDREN,

        // position/size
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,

        nullptr,                // parent window
        nullptr,                // menu
        globalHInstance,        // instance handle
        this);                  // application data
    if (!hwnd)
        throw std::runtime_error("Couldn't create window");

    ShowWindow(hwnd, showCommand);

    AddRef(); // keep window alive while open
    InterlockedIncrement(&numOpenWindows);
}

void FolderWindow::close() {
    PostMessage(hwnd, WM_CLOSE, 0, 0);
}

void FolderWindow::activate() {
    SetActiveWindow(hwnd);
}

void FolderWindow::setPos(POINT pos) {
    // TODO SWP_ASYNCWINDOWPOS?
    SetWindowPos(hwnd, 0, pos.x, pos.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

void FolderWindow::move(int x, int y) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    setPos({rect.left + x, rect.top + y});
}

LRESULT FolderWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    // DWM custom frame
    // https://docs.microsoft.com/en-us/windows/win32/dwm/customframe
    LRESULT dwmResult = 0;
    if (DwmDefWindowProc(hwnd, message, wParam, lParam, &dwmResult))
        return dwmResult;

    switch (message) {
        case WM_CREATE:
            setupWindow();
            return 0;
        case WM_DESTROY:
            cleanupWindow();
            return 0;
        case WM_NCDESTROY:
            hwnd = nullptr;
            Release(); // allow window to be deleted
            if (InterlockedDecrement(&numOpenWindows) == 0)
                PostQuitMessage(0);
            return 0;
        case WM_ACTIVATE: {
            // for DWM custom frame
            // make sure frame is correct if window is maximized
            extendWindowFrame();

            if (wParam != WA_INACTIVE) {
                activeWindow = this;
                if (shellView)
                    shellView->UIActivate(SVUIA_ACTIVATE_FOCUS);

                // bring children to front
                auto nextChild = child;
                while (nextChild) {
                    SetWindowPos(nextChild->hwnd, hwnd, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    nextChild = nextChild->child;
                }
            }
            return 0;
        }
        case WM_NCCALCSIZE:
            if (wParam == TRUE) {
                // allow resizing past the edge of the window by reducing client rect
                NCCALCSIZE_PARAMS *params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                params->rgrc[0].left = params->rgrc[0].left + RESIZE_MARGIN;
                params->rgrc[0].top = params->rgrc[0].top;
                params->rgrc[0].right = params->rgrc[0].right - RESIZE_MARGIN;
                params->rgrc[0].bottom = params->rgrc[0].bottom - RESIZE_MARGIN;
                // for DWM custom frame
                return 0;
            }
        case WM_NCHITTEST: {
            // for DWM custom frame
            HRESULT defHitTest = DefWindowProc(hwnd, message, wParam, lParam);
            if (defHitTest != HTCLIENT)
                return defHitTest;
            return hitTestNCA({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        }
        case WM_PAINT: {
            // for DWM custom frame
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            paintCustomCaption(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ENTERSIZEMOVE:
            moveAccum = {0, 0};
            return 0;
        case WM_MOVING:
            // https://www.drdobbs.com/make-it-snappy/184416407
            if (parent) {
                RECT *desiredRect = (RECT *)lParam;
                RECT curRect;
                GetWindowRect(hwnd, &curRect);
                moveAccum.x += desiredRect->left - curRect.left;
                moveAccum.y += desiredRect->top - curRect.top;
                // windows max macro prevents me from using std::max :')
                int moveAmount = max(abs(moveAccum.x), abs(moveAccum.y));
                if (moveAmount > SNAP_DISTANCE) {
                    detachFromParent();
                    desiredRect->left = curRect.left + moveAccum.x;
                    desiredRect->right = curRect.right + moveAccum.x;
                    desiredRect->top = curRect.top + moveAccum.y;
                    desiredRect->bottom = curRect.bottom + moveAccum.y;
                } else {
                    *desiredRect = curRect;
                }
            }
            // required for WM_ENTERSIZEMOVE to behave correctly
            return TRUE;
        case WM_MOVE:
            windowRectChanged();
            return 0;
        case WM_SIZING:
            if (parent) {
                RECT *desiredRect = (RECT *)lParam;
                RECT curRect;
                GetWindowRect(hwnd, &curRect);
                // constrain top-left corner
                int moveX = 0, moveY = 0;
                if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT)
                    moveX = desiredRect->left - curRect.left;
                if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT)
                    moveY = desiredRect->top - curRect.top;
                if (moveX != 0 || moveY != 0) {
                    auto topParent = parent;
                    while (topParent->parent)
                        topParent = topParent->parent;
                    topParent->move(moveX, moveY);
                }
            }
            return TRUE;
        case WM_SIZE: {
            RECT browserRect {0, CAPTION_HEIGHT, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            browser->SetRect(nullptr, browserRect);
            windowRectChanged();
            return 0;
        }
        case WM_COMMAND: {
            if ((HWND)lParam == parentButton && HIWORD(wParam) == BN_CLICKED) {
                openParent();
                return 0;
            }
        }
        /* user messages */
        case MSG_FORCE_SORT: {
            CComPtr<IFolderView2> view;
            if (SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
                SORTCOLUMN column = {PKEY_ItemNameDisplay, SORT_ASCENDING};
                view->SetSortColumns(&column, 1);
            }
            return 0;
        }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

bool FolderWindow::handleTopLevelMessage(MSG *msg) {
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_TAB) {
        if (GetKeyState(VK_SHIFT) & 0x8000) {
            if (parent)
                parent->activate();
            else
                openParent();
        } else {
            if (child)
                child->activate();
        }
        return true;
    }
    return false;
}

void FolderWindow::setupWindow() {
    BOOL disableAnimations = true;
    DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
        &disableAnimations, sizeof(disableAnimations));
    extendWindowFrame();

    if (iconLarge)
        PostMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconLarge);
    if (iconSmall)
        PostMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);

    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    // TODO ??
    RECT browserRect = windowRect;
    MapWindowRect(HWND_DESKTOP, hwnd, &browserRect);

    FOLDERSETTINGS folderSettings = {};
    folderSettings.ViewMode = FVM_SMALLICON; // doesn't work correctly (see below)
    folderSettings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW | FWF_NOHEADERINALLVIEWS;
    EXPLORER_BROWSER_OPTIONS browserOptions = EBO_NAVIGATEONCE; // no navigation

    if (FAILED(browser.CoCreateInstance(__uuidof(ExplorerBrowser)))
            || FAILED(browser->Initialize(hwnd, &browserRect, &folderSettings))) {
        close();
        return;
    }
    browser->SetOptions(browserOptions);
    if (FAILED(browser->BrowseToObject(item, SBSP_ABSOLUTE))) {
        // eg. browsing a subdirectory in the recycle bin
        wcout << "Unable to browse to folder " <<&title[0]<< "\n";
        close();
        return;
    }

    CComPtr<IFolderView2> view;
    if (SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
        int itemCount;
        if (FAILED(view->ItemCount(SVGIO_ALLVIEW, &itemCount))) {
            view = nullptr;
            // will fail for control panel
            browser->Destroy(); // destroy and recreate browser
            if (FAILED(browser.CoCreateInstance(__uuidof(ExplorerBrowser)))
                    || FAILED(browser->Initialize(hwnd, &browserRect, &folderSettings))) {
                close();
                return;
            }
            browser->SetOptions(browserOptions);
            resultsFolderFallback();
        }
    }

    if (SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
        // FVM_SMALLICON only seems to work if it's also specified with an icon size
        // TODO should this be the shell small icon size?
        // https://docs.microsoft.com/en-us/windows/win32/menurc/about-icons
        view->SetViewModeAndIconSize(FVM_SMALLICON, GetSystemMetrics(SM_CXSMICON)); // = 16
    }

    if (SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&shellView)))) {
        if (child) {
            // window was created by clicking the parent button
            ignoreNextSelection = true; // TODO jank
            CComHeapPtr<ITEMID_CHILD> childID;
            CComQIPtr<IParentAndItem>(child->item)->GetParentAndItem(nullptr, nullptr, &childID);
            shellView->SelectItem(childID, SVSI_SELECT | SVSI_FOCUSED);
        }
    }

    IUnknown_SetSite(browser, (IServiceProvider *)this);

    CComPtr<IShellItem> parentItem;
    bool showParentButton = !parent && SUCCEEDED(item->GetParent(&parentItem));
    parentButton = CreateWindow(L"BUTTON", L"<-",
        (showParentButton ? WS_VISIBLE : 0) | WS_CHILD | BS_PUSHBUTTON,
        0, 0, GetSystemMetrics(SM_CXSIZE), CAPTION_HEIGHT,
        hwnd, nullptr, globalHInstance, nullptr);

    // ensure WM_NCCALCSIZE gets called
    // for DWM custom frame
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FolderWindow::cleanupWindow() {
    wcout << "Cleanup " <<&title[0]<< "\n";
    IUnknown_SetSite(browser, nullptr);
    browser->Destroy();
    if (child) {
        child->parent = nullptr;
        child->close(); // recursive
    }
    child = nullptr;
    detachFromParent();
    if (activeWindow == this)
        activeWindow = nullptr;
}

void FolderWindow::windowRectChanged() {
    if (child) {
        child->setPos(childPos());
    }
}

void FolderWindow::extendWindowFrame() {
    MARGINS margins;
    margins.cxLeftWidth = 0;
    margins.cxRightWidth = 0;
    margins.cyTopHeight = CAPTION_HEIGHT;
    margins.cyBottomHeight = 0;
    if (FAILED(DwmExtendFrameIntoClientArea(hwnd, &margins))) {
        wcout << "Unable to create custom frame!\n";
    }
}

LRESULT FolderWindow::hitTestNCA(POINT cursor) {
    // from https://docs.microsoft.com/en-us/windows/win32/dwm/customframe?redirectedfrom=MSDN#appendix-c-hittestnca-function
    // the default window proc handles the left, right, and bottom edges
    // so only need to check top edge and caption
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);

    if (cursor.x < windowRect.left || cursor.x > windowRect.right)
        return HTNOWHERE;

    if (cursor.y < windowRect.top) {
        return HTNOWHERE;
    } else if (cursor.y < windowRect.top + RESIZE_MARGIN) {
        if (cursor.x < windowRect.left + RESIZE_MARGIN * 2)
            return HTTOPLEFT;
        else if (cursor.x >= windowRect.right - RESIZE_MARGIN * 2)
            return HTTOPRIGHT;
        else
            return HTTOP;
    } else if (cursor.y < windowRect.top + CAPTION_HEIGHT) {
        return HTCAPTION;
    } else {
        return HTNOWHERE;
    }
}

void FolderWindow::paintCustomCaption(HDC hdc) {
    // from https://docs.microsoft.com/en-us/windows/win32/dwm/customframe?redirectedfrom=MSDN#appendix-b-painting-the-caption-title
    // TODO clean this up
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    HTHEME theme = OpenThemeData(NULL, L"CompositedWindow::Window");
    if (!theme)
        return;

    HDC hdcPaint = CreateCompatibleDC(hdc);
    if (hdcPaint) {
        int width = clientRect.right - clientRect.left;
        int height = CAPTION_HEIGHT;

        // Define the BITMAPINFO structure used to draw text.
        // Note that biHeight is negative. This is done because
        // DrawThemeTextEx() needs the bitmap to be in top-to-bottom
        // order.
        BITMAPINFO bitmapInfo = {};
        bitmapInfo.bmiHeader.biSize            = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth           = width;
        bitmapInfo.bmiHeader.biHeight          = -height;
        bitmapInfo.bmiHeader.biPlanes          = 1;
        bitmapInfo.bmiHeader.biBitCount        = 32;
        bitmapInfo.bmiHeader.biCompression     = BI_RGB;

        HBITMAP bitmap = CreateDIBSection(hdc, &bitmapInfo, DIB_RGB_COLORS, NULL, NULL, 0);
        if (bitmap) {
            HBITMAP oldBitmap = (HBITMAP)SelectObject(hdcPaint, bitmap);

            // Setup the theme drawing options.
            DTTOPTS textOpts = {sizeof(DTTOPTS)};
            textOpts.dwFlags = DTT_COMPOSITED | DTT_GLOWSIZE;
            textOpts.iGlowSize = 15;

            // Select a font.
            LOGFONT logFont;
            HFONT oldFont = NULL;
            if (SUCCEEDED(GetThemeSysFont(theme, TMT_CAPTIONFONT, &logFont))) {
                HFONT font = CreateFontIndirect(&logFont);
                oldFont = (HFONT) SelectObject(hdcPaint, font);
            }

            int iconSize = GetSystemMetrics(SM_CXSMICON);
            int buttonWidth = GetSystemMetrics(SM_CXSIZE);
            SIZE titleSize = {};
            GetTextExtentPoint32(hdcPaint, title, wcslen(title), &titleSize);
            // include padding on the right side of the text; makes it look more centered
            int headerWidth = iconSize + WINDOW_ICON_PADDING * 2 + titleSize.cx;
            int headerLeft = (width - headerWidth) / 2;
            if (headerLeft < buttonWidth + WINDOW_ICON_PADDING)
                headerLeft = buttonWidth + WINDOW_ICON_PADDING;

            DrawIconEx(hdcPaint, clientRect.left + headerLeft, clientRect.top + CAPTION_PADDING,
                iconSmall, iconSize, iconSize, 0, nullptr, DI_NORMAL);

            // Draw the title.
            RECT paintRect = clientRect;
            paintRect.top += CAPTION_PADDING;
            paintRect.right -= buttonWidth; // close button width
            paintRect.left += headerLeft + iconSize + WINDOW_ICON_PADDING;
            paintRect.bottom = CAPTION_HEIGHT;
            DrawThemeTextEx(theme, hdcPaint, 0, 0, title, -1,
                            DT_LEFT | DT_WORD_ELLIPSIS, &paintRect, &textOpts);

            // Blit text to the frame.
            BitBlt(hdc, 0, 0, width, height, hdcPaint, 0, 0, SRCCOPY);

            SelectObject(hdcPaint, oldBitmap);
            if (oldFont) {
                SelectObject(hdcPaint, oldFont);
            }
            DeleteObject(bitmap);
        }
        DeleteDC(hdcPaint);
    }
    CloseThemeData(theme);
}

void FolderWindow::clearSelection() {
    if (shellView) {
        shellView->SelectItem(nullptr, SVSI_DESELECTOTHERS);
    }
}

void FolderWindow::selectionChanged() {
    CComPtr<IFolderView2> view;
    if (SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
        int numSelected;
        if (SUCCEEDED(view->ItemCount(SVGIO_SELECTION, &numSelected)) && numSelected == 1) {
            int index;
            // GetSelectedItem seems to ignore the iStart parameter!
            if (view->GetSelectedItem(-1, &index) == S_OK) {
                CComPtr<IShellItem> selected;
                if (SUCCEEDED(view->GetItem(index, IID_PPV_ARGS(&selected)))) {
                    openChild(selected);
                }
            }
        } else {
            // 0 or more than 1 item selected
            closeChild();
        }
    }
}

void FolderWindow::resultsFolderFallback() {
    wcout << "Using results folder fallback\n";
    CComPtr<IEnumShellItems> enumItems;
    if (SUCCEEDED(item->BindToHandler(nullptr, BHID_EnumItems,
            IID_PPV_ARGS(&enumItems)))) {
        // create empty ResultsFolder
        if (SUCCEEDED(browser->FillFromObject(nullptr, EBF_NODROPTARGET))) {
            CComPtr<IFolderView2> view;
            if (SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
                CComPtr<IResultsFolder> results;
                if (SUCCEEDED(view->GetFolder(IID_PPV_ARGS(&results)))) {
                    CComPtr<IShellItem> childItem;
                    while (enumItems->Next(1, &childItem, nullptr) == S_OK) {
                        results->AddItem(childItem);
                    }
                    // for some reason changing the sort columns immediately after adding items
                    // breaks the folder view, so delay it until the browser has had a chance to
                    // process some messages
                    PostMessage(hwnd, MSG_FORCE_SORT, 0, 0);
                }
            }
        }
    }
}

void FolderWindow::closeChild() {
    if (child) {
        child->parent = nullptr;
        child->close();
        child = nullptr;
    }
}

void FolderWindow::openChild(CComPtr<IShellItem> item) {
    item = resolveLink(item);
    if (child) {
        int compare;
        if (SUCCEEDED(child->item->Compare(item, SICHINT_CANONICAL, &compare)) && compare == 0) {
            return; // already open
        }
        closeChild();
    }
    SFGAOF attr;
    if (SUCCEEDED(item->GetAttributes(SFGAO_FOLDER, &attr))) {
        if (attr & SFGAO_FOLDER) {
            child.Attach(new FolderWindow(this, item));
            // will flush message queue
            POINT pos = childPos();
            child->create({pos.x, pos.y, pos.x + DEFAULT_WIDTH, pos.y + DEFAULT_HEIGHT},
                          SW_SHOWNOACTIVATE);
        }
    }
}

void FolderWindow::openParent() {
    CComPtr<IShellItem> parentItem;
    if (SUCCEEDED(item->GetParent(&parentItem))) {
        parent.Attach(new FolderWindow(nullptr, parentItem));
        parent->child = this;
        POINT pos = parentPos();
        parent->create({pos.x - DEFAULT_WIDTH, pos.y, pos.x, pos.y + DEFAULT_HEIGHT},
                       SW_SHOWNORMAL);
        ShowWindow(parentButton, SW_HIDE);
    }
}

CComPtr<IShellItem> FolderWindow::resolveLink(CComPtr<IShellItem> item) {
    // https://stackoverflow.com/a/46064112
    CComPtr<IShellLink> link;
    if (SUCCEEDED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&link)))) {
        if (SUCCEEDED(link->Resolve(hwnd, SLR_UPDATE))) {
            CComHeapPtr<ITEMIDLIST> targetPIDL;
            if (SUCCEEDED(link->GetIDList(&targetPIDL))) {
                CComPtr<IShellItem> targetItem;
                if (SUCCEEDED(SHCreateShellItem(nullptr, nullptr, targetPIDL, &targetItem))) {
                    // don't need to recurse, shortcuts to shortcuts are not allowed
                    return targetItem;
                }
            }
        } else {
            wcout << "Could not resolve link\n";
        }
    }
    return item;
}

POINT FolderWindow::childPos() {
    RECT shadowRect = {};
    // https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect
    // GetWindowRect includes the drop shadow! (why??)
    GetWindowRect(hwnd, &shadowRect);
    RECT frameRect = {};
    // TODO not DPI aware!!
    DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frameRect, sizeof(frameRect));
    int shadowLeft = frameRect.left - shadowRect.left;
    int shadowTop = frameRect.top - shadowRect.top;
    return {frameRect.right - shadowLeft, frameRect.top - shadowTop};
}

POINT FolderWindow::parentPos() {
    RECT shadowRect = {};
    GetWindowRect(hwnd, &shadowRect);
    RECT frameRect = {};
    // TODO not DPI aware!!
    DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frameRect, sizeof(frameRect));
    int shadowRight = shadowRect.right - frameRect.right;
    int shadowTop = frameRect.top - shadowRect.top;
    return {frameRect.left + shadowRight, frameRect.top - shadowTop};
}

void FolderWindow::detachFromParent() {
    if (parent && parent->child == this) {
        parent->child = nullptr;
        parent->clearSelection();
    }
    parent = nullptr;
    ShowWindow(parentButton, SW_SHOW);
}

/* IUnknown */

STDMETHODIMP FolderWindow::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        QITABENT(FolderWindow, IServiceProvider),
        QITABENT(FolderWindow, ICommDlgBrowser),
        {},
    };
    return QISearch(this, interfaces, id, obj);
}

STDMETHODIMP_(ULONG) FolderWindow::AddRef() {
    return InterlockedIncrement(&refCount);
}

STDMETHODIMP_(ULONG) FolderWindow::Release() {
    long r = InterlockedDecrement(&refCount);
    if (r == 0) {
        wcout << "Delete " <<&title[0]<< "\n";
        delete this; // TODO ???
    }
    return r;
}

/* IServiceProvider */

STDMETHODIMP FolderWindow::QueryService(REFGUID guidService, REFIID riid, void **ppv) {
    // services that IExplorerBrowser queries for:
    // 2BD6990F-2D1A-4B5A-9D61-AB6229A36CAA     ??
    // 4C96BE40-915C-11CF-99D3-00AA004AE837     TopLevelBrowser
    // 2E228BA3-EA25-4378-97B6-D574FAEBA356     ??
    // 3934E4C2-8143-4E4C-A1DC-718F8563F337     ??
    // A36A3ACE-8332-45CE-AA29-503CB76B2587     ??
    // 04BA120E-AD52-4A2D-9807-2DA178D0C3E1
    // DD1E21CC-E2C7-402C-BF05-10328D3F6BAD     ??
    // 16770868-239C-445B-A01D-F26C7FBBF26C     ??
    // 6CCB7BE0-6807-11D0-B810-00C04FD706EC     IShellTaskScheduler
    // 000214F1-0000-0000-C000-000000000046     ICommDlgBrowser / SExplorerBrowserFrame
    // 05A89298-6246-4C63-BB0D-9BDAF140BF3B     IBrowserWithActivationNotification
    // 00021400-0000-0000-C000-000000000046     Desktop
    // E38FE0F3-3DB0-47EE-A314-25CF7F4BF521     IInfoBarHost https://stackoverflow.com/a/63954982
    // D7F81F62-491F-49BC-891D-5665085DF969     ??
    // FAD451C2-AF58-4161-B9FF-57AFBBED0AD2     ??
    // 9EA5491C-89C8-4BEF-93D3-7F665FB82A33     ??

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
    return S_FALSE; // perform default action
}

STDMETHODIMP FolderWindow::OnStateChange(IShellView *view, ULONG change) {
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

STDMETHODIMP FolderWindow::IncludeObject(IShellView *view, PCUITEMID_CHILD pidl) {
    return S_OK; // include all objects
}

} // namespace

#ifdef DEBUG
int main(int argc, char* argv[]) {
    wWinMain(nullptr, nullptr, nullptr, SW_SHOWNORMAL);
}
#endif

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int showCommand) {
    wcout << "omg hiiiii ^w^\n"; // DO NOT REMOVE!!
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLine(), &argc);

    chromabrowse::globalHInstance = hInstance;

    if (FAILED(OleInitialize(0))) // needed for drag/drop
        return 0;

    chromabrowse::FolderWindow::registerClass();

    {
        CComPtr<IShellItem> startItem;
        if (argc > 1) {
            // TODO parse name vs display name https://stackoverflow.com/q/42966489
            if (FAILED(SHCreateItemFromParsingName(argv[1], nullptr, IID_PPV_ARGS(&startItem)))) {
                wcout << "Unable to locate item at path\n";
                return 0;
            }
        } else {
            if (FAILED(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr,
                    IID_PPV_ARGS(&startItem)))) {
                wcout << "Couldn't get desktop!\n";
                return 0;
            }
        }

        RECT workArea;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
        RECT windowRect = {workArea.left, workArea.bottom - chromabrowse::DEFAULT_HEIGHT,
                           workArea.left + chromabrowse::DEFAULT_WIDTH, workArea.bottom};

        CComPtr<chromabrowse::FolderWindow> initialWindow;
        initialWindow.Attach(new chromabrowse::FolderWindow(nullptr, startItem));
        initialWindow->create(windowRect, showCommand);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (chromabrowse::activeWindow && chromabrowse::activeWindow->handleTopLevelMessage(&msg))
            continue;
        if (chromabrowse::activeWindow && chromabrowse::activeWindow->shellView
                && chromabrowse::activeWindow->shellView->TranslateAccelerator(&msg) == S_OK)
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    OleUninitialize();
    return 0;
}
