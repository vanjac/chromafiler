#include "main.h"
#include <stdexcept>
#include <cstdlib>
#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <vssym32.h>

// https://docs.microsoft.com/en-us/windows/win32/controls/cookbook-overview
#pragma comment(linker,"\"/manifestdependency:type='win32' \
    name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
    processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Example of how to host an IExplorerBrowser:
// https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/shell/appplatform/ExplorerBrowserCustomContents

namespace chromabrowse {

const wchar_t * FolderWindow::CLASS_NAME = L"Folder Window";

// dimensions
const int DEFAULT_WIDTH = 220;
const int DEFAULT_HEIGHT = 450;
const int RESIZE_MARGIN = 8; // TODO use some system metric?
const int CAPTION_PADDING = 8;
const int SNAP_DISTANCE = 32;
// calculated in registerClass()
static int CAPTION_HEIGHT;

static HINSTANCE globalHInstance;
static long numOpenWindows;
static CComPtr<IShellView> focusedShellView;

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
    , refCount(1)
{}

void FolderWindow::create(RECT rect, int showCommand) {
    if (FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &title)))
        throw std::runtime_error("Unable to get folder name");
    wcout << "Create " <<&title[0]<< "\n";

    HWND hwnd = CreateWindowEx(
        0,                      // extended style
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
                if (SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&focusedShellView)))) {
                    focusedShellView->UIActivate(SVUIA_ACTIVATE_FOCUS);
                } else {
                    focusedShellView = nullptr;
                }

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
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

void FolderWindow::setupWindow() {
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);

    if (SUCCEEDED(browser.CoCreateInstance(__uuidof(ExplorerBrowser)))) {
        IUnknown_SetSite(browser, (IServiceProvider *)this);

        // TODO ??
        RECT browserRect = windowRect;
        MapWindowRect(HWND_DESKTOP, hwnd, &browserRect);

        FOLDERSETTINGS folderSettings = {};
        folderSettings.ViewMode = FVM_SMALLICON; // doesn't work correctly (see below)
        folderSettings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW | FWF_NOHEADERINALLVIEWS;
        if (SUCCEEDED(browser->Initialize(hwnd, &browserRect, &folderSettings))) {
            browser->SetOptions(EBO_NAVIGATEONCE); // no navigation
            if (FAILED(browser->BrowseToObject(item, SBSP_ABSOLUTE))) {
                wcout << "Unable to browse to folder " <<&title[0]<< "\n";
                close();
            }

            CComPtr<IFolderView2> view;
            if (SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
                // FVM_SMALLICON only seems to work if it's also specified with an icon size
                view->SetViewModeAndIconSize(FVM_SMALLICON, GetSystemMetrics(SM_CXSMICON)); // = 16
            }
        }
    }

    BOOL disableAnimations = true;
    DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
        &disableAnimations, sizeof(disableAnimations));

    extendWindowFrame();

    // ensure WM_NCCALCSIZE gets called
    // for DWM custom frame
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FolderWindow::cleanupWindow() {
    wcout << "Cleanup " <<&title[0]<< "\n";
    IUnknown_SetSite(browser, nullptr);
    if (child) {
        child->parent = nullptr;
        child->close(); // recursive
    }
    child = nullptr;
    detachFromParent();
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
        int height = clientRect.bottom - clientRect.top;

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

            // Draw the title.
            RECT paintRect = clientRect;
            paintRect.top += CAPTION_PADDING;
            paintRect.right -= GetSystemMetrics(SM_CXSIZE); // close button width
            paintRect.left += CAPTION_PADDING;
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

void FolderWindow::detachFromParent() {
    if (parent && parent->child == this) {
        parent->child = nullptr;
    }
    parent = nullptr;
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
STDMETHODIMP FolderWindow::OnDefaultCommand(IShellView * view) {
    return S_FALSE; // perform default action
}

STDMETHODIMP FolderWindow::OnStateChange(IShellView * view, ULONG change) {
    if (change == CDBOSC_SELCHANGE) {
        // TODO this can hang the browser and should really be done asynchronously with a message
        // but that adds other complication
        selectionChanged();
    }
    return S_OK;
}

STDMETHODIMP FolderWindow::IncludeObject(IShellView * view, PCUITEMID_CHILD pidl) {
    return S_OK; // include all objects
}

} // namespace

#ifdef DEBUG
int main(int argc, char* argv[]) {
    wWinMain(nullptr, nullptr, L"", SW_SHOWNORMAL);
}
#endif

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int showCommand) {
    wcout << "omg hiiiii ^w^\n"; // DO NOT REMOVE!!

    chromabrowse::globalHInstance = hInstance;

    if (FAILED(OleInitialize(0))) // needed for drag/drop
        return 0;

    chromabrowse::FolderWindow::registerClass();

    {
        CComPtr<IShellItem> desktop;
        if (FAILED(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr,
                IID_PPV_ARGS(&desktop)))) {
            wcout << "Couldn't get desktop!\n";
            return 0;
        }

        RECT workArea;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
        RECT windowRect = {workArea.left, workArea.bottom - chromabrowse::DEFAULT_HEIGHT,
                           workArea.left + chromabrowse::DEFAULT_WIDTH, workArea.bottom};

        CComPtr<chromabrowse::FolderWindow> initialWindow;
        initialWindow.Attach(new chromabrowse::FolderWindow(nullptr, desktop));
        initialWindow->create(windowRect, showCommand);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (chromabrowse::focusedShellView &&
                chromabrowse::focusedShellView->TranslateAccelerator(&msg) == S_OK)
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    OleUninitialize();
    return 0;
}
