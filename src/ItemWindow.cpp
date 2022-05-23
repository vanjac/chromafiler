#include "ItemWindow.h"
#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <vssym32.h>

namespace chromabrowse {

// dimensions
const int RESIZE_MARGIN = 8; // TODO use some system metric?
const int CAPTION_PADDING = 8;
const int WINDOW_ICON_PADDING = 4;
const int SNAP_DISTANCE = 32;

static HFONT symbolFont;

long numOpenWindows;
CComPtr<ItemWindow> activeWindow;

LRESULT CALLBACK captionButtonProc(HWND hwnd, UINT message,
    WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

int ItemWindow::CAPTION_HEIGHT = 0;

void ItemWindow::init() {
    RECT adjustedRect = {};
    AdjustWindowRectEx(&adjustedRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    CAPTION_HEIGHT = -adjustedRect.top; // = 31

    // TODO only supported on windows 10!
    symbolFont = CreateFont(12, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
}

void ItemWindow::uninit() {
    DeleteObject(symbolFont);
}

WNDCLASS ItemWindow::createWindowClass(const wchar_t *name) {
    WNDCLASS wndClass = {};
    wndClass.lpfnWndProc = ItemWindow::windowProc;
    wndClass.hInstance = GetModuleHandle(NULL);
    wndClass.lpszClassName = name;
    wndClass.style = CS_HREDRAW; // ensure caption gets redrawn if width changes
    return wndClass;
}

LRESULT CALLBACK ItemWindow::windowProc(
        HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // DWM custom frame
    // https://docs.microsoft.com/en-us/windows/win32/dwm/customframe
    LRESULT dwmResult = 0;
    if (DwmDefWindowProc(hwnd, message, wParam, lParam, &dwmResult))
        return dwmResult;

    ItemWindow *self = nullptr;
    if (message == WM_NCCREATE) {
        CREATESTRUCT *create = (CREATESTRUCT*)lParam;
        self = (ItemWindow*)create->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd = hwnd;
    } else {
        self = (ItemWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }
    if (self) {
        return self->handleMessage(message, wParam, lParam);
    } else {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

ItemWindow::ItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
    : parent(parent)
    , item(item)
{}

ItemWindow::~ItemWindow() {
    if (iconLarge)
        DestroyIcon(iconLarge);
    if (iconSmall)
        DestroyIcon(iconSmall);
}

bool ItemWindow::create(RECT rect, int showCommand) {
    if (FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &title))) {
        debugPrintf(L"Unable to get item name\n");
        return false;
    }
    debugPrintf(L"Create %s\n", &*title);

    CComPtr<IExtractIcon> extractIcon;
    if (SUCCEEDED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&extractIcon)))) {
        wchar_t iconFile[MAX_PATH];
        int index;
        UINT flags;
        if (extractIcon->GetIconLocation(0, iconFile, MAX_PATH, &index, &flags) == S_OK) {
            UINT iconSizes = (GetSystemMetrics(SM_CXSMICON) << 16) + GetSystemMetrics(SM_CXICON);
            if (extractIcon->Extract(iconFile, index, &iconLarge, &iconSmall, iconSizes) != S_OK) {
                debugPrintf(L"IExtractIcon failed\n");
                // https://devblogs.microsoft.com/oldnewthing/20140501-00/?p=1103
                SHDefExtractIcon(iconFile, index, flags, &iconLarge, &iconSmall, iconSizes);
            }
        }
    }

    HWND createHwnd = CreateWindow(
        className(),
        title,
        // style
        // WS_CLIPCHILDREN fixes drawing glitches with the scrollbars
        (WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX & ~WS_MAXIMIZEBOX) | WS_CLIPCHILDREN,

        // position/size
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,

        nullptr,                // parent window
        nullptr,                // menu
        GetModuleHandle(NULL),  // instance handle
        this);                  // application data
    if (!createHwnd) {
        debugPrintf(L"Couldn't create window\n");
        return false;
    }

    ShowWindow(createHwnd, showCommand);

    AddRef(); // keep window alive while open
    InterlockedIncrement(&numOpenWindows);
    return true;
}

void ItemWindow::close() {
    PostMessage(hwnd, WM_CLOSE, 0, 0);
}

void ItemWindow::activate() {
    SetActiveWindow(hwnd);
}

void ItemWindow::setPos(POINT pos) {
    // TODO SWP_ASYNCWINDOWPOS?
    SetWindowPos(hwnd, 0, pos.x, pos.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ItemWindow::move(int x, int y) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    setPos({rect.left + x, rect.top + y});
}

RECT ItemWindow::windowBody() {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    return {0, CAPTION_HEIGHT, clientRect.right, clientRect.bottom};
}

LRESULT ItemWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            onCreate();
            // ensure WM_NCCALCSIZE gets called
            // for DWM custom frame
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        case WM_DESTROY:
            onDestroy();
            return 0;
        case WM_NCDESTROY:
            hwnd = nullptr;
            Release(); // allow window to be deleted
            if (InterlockedDecrement(&numOpenWindows) == 0)
                PostQuitMessage(0);
            return 0;
        case WM_ACTIVATE: {
            onActivate(wParam);
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
            LRESULT defHitTest = DefWindowProc(hwnd, message, wParam, lParam);
            if (defHitTest != HTCLIENT)
                return defHitTest;
            return hitTestNCA({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        }
        case WM_PAINT: {
            // for DWM custom frame
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            onPaint(paint);
            EndPaint(hwnd, &paint);
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
            onSize();
            return 0;
        }
        case WM_COMMAND: {
            if ((HWND)lParam == parentButton && HIWORD(wParam) == BN_CLICKED) {
                openParent();
                return 0;
            }
        }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

bool ItemWindow::handleTopLevelMessage(MSG *msg) {
    if (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) {
        WPARAM vk = msg->wParam;
        bool shift = GetKeyState(VK_SHIFT) & 0x8000;
        bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        bool alt = GetKeyState(VK_MENU) & 0x8000;
        if        ((vk == VK_TAB && !shift && !ctrl && !alt)
                || (vk == VK_DOWN && !shift && !ctrl && alt)) {
            if (child)
                child->activate();
            return true;
        } else if ((vk == VK_TAB && shift && !ctrl && !alt)
                || (vk == VK_UP && !shift && !ctrl && alt)) {
            if (parent)
                parent->activate();
            else
                openParent();
            return true;
        } else if (vk == 'W' && !shift && ctrl && !alt) {
            close();
            return true;
        }
    }
    return false;
}

void ItemWindow::onCreate() {
    BOOL disableAnimations = true;
    DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
        &disableAnimations, sizeof(disableAnimations));
    extendWindowFrame();

    if (iconLarge)
        PostMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconLarge);
    if (iconSmall)
        PostMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);

    CComPtr<IShellItem> parentItem;
    bool showParentButton = !parent && SUCCEEDED(item->GetParent(&parentItem));
    parentButton = CreateWindow(L"BUTTON", L"\uE96F", // ChevronLeftSmall
        (showParentButton ? WS_VISIBLE : 0) | WS_CHILD | BS_PUSHBUTTON,
        0, 0, GetSystemMetrics(SM_CXSIZE), CAPTION_HEIGHT,
        hwnd, nullptr, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    SetWindowSubclass(parentButton, captionButtonProc, 0, 0);
}

void ItemWindow::onDestroy() {
    debugPrintf(L"Cleanup %s\n", &*title);
    if (child) {
        child->parent = nullptr;
        child->close(); // recursive
    }
    child = nullptr;
    detachFromParent();
    if (activeWindow == this)
        activeWindow = nullptr;
}

void ItemWindow::onActivate(WPARAM wParam) {
    // for DWM custom frame
    // make sure frame is correct if window is maximized
    extendWindowFrame();

    if (wParam != WA_INACTIVE) {
        activeWindow = this;

        // bring children to front
        auto nextChild = child;
        while (nextChild) {
            SetWindowPos(nextChild->hwnd, hwnd, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            nextChild = nextChild->child;
        }
    }
}

void ItemWindow::onSize() {
    windowRectChanged();
}

void ItemWindow::windowRectChanged() {
    if (child) {
        child->setPos(childPos());
    }
}

void ItemWindow::extendWindowFrame() {
    MARGINS margins;
    margins.cxLeftWidth = 0;
    margins.cxRightWidth = 0;
    margins.cyTopHeight = CAPTION_HEIGHT;
    margins.cyBottomHeight = 0;
    if (FAILED(DwmExtendFrameIntoClientArea(hwnd, &margins))) {
        debugPrintf(L"Unable to create custom frame!\n");
    }
}

LRESULT ItemWindow::hitTestNCA(POINT cursor) {
    // from https://docs.microsoft.com/en-us/windows/win32/dwm/customframe?redirectedfrom=MSDN#appendix-c-hittestnca-function
    // the default window proc handles the left, right, and bottom edges
    // so only need to check top edge and caption
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);

    if (cursor.x < windowRect.left || cursor.x >= windowRect.right)
        return HTNOWHERE;

    if (cursor.y < windowRect.top || cursor.y >= windowRect.bottom) {
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
        return HTCAPTION;
    }
}

void ItemWindow::onPaint(PAINTSTRUCT paint) {
    // from https://docs.microsoft.com/en-us/windows/win32/dwm/customframe?redirectedfrom=MSDN#appendix-b-painting-the-caption-title
    // TODO clean this up
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    // the colors won't be right in many cases and it seems like there's no easy way to fix that
    // https://github.com/res2k/Windows10Colors
    HTHEME theme = OpenThemeData(hwnd, L"CompositedWindow::Window");
    if (!theme)
        return;

    HDC hdcPaint = CreateCompatibleDC(paint.hdc);
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

        HBITMAP bitmap = CreateDIBSection(paint.hdc, &bitmapInfo, DIB_RGB_COLORS,
                                          nullptr, nullptr, 0);
        if (bitmap) {
            HBITMAP oldBitmap = (HBITMAP)SelectObject(hdcPaint, bitmap);

            // Setup the theme drawing options.
            DTTOPTS textOpts = {sizeof(DTTOPTS)};
            textOpts.dwFlags = DTT_COMPOSITED;

            // Select a font.
            LOGFONT logFont;
            HFONT font = nullptr, oldFont = nullptr;
            if (SUCCEEDED(GetThemeSysFont(theme, TMT_CAPTIONFONT, &logFont))) {
                font = CreateFontIndirect(&logFont);
                oldFont = (HFONT)SelectObject(hdcPaint, font);
            }

            int iconSize = GetSystemMetrics(SM_CXSMICON);
            int buttonWidth = GetSystemMetrics(SM_CXSIZE);
            SIZE titleSize = {};
            GetTextExtentPoint32(hdcPaint, title, (int)wcslen(title), &titleSize);
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
            BitBlt(paint.hdc, 0, 0, width, height, hdcPaint, 0, 0, SRCCOPY);

            SelectObject(hdcPaint, oldBitmap);
            if (oldFont)
                SelectObject(hdcPaint, oldFont);
            if (font)
                DeleteObject(font);
            DeleteObject(bitmap);
        }
        DeleteDC(hdcPaint);
    }
    CloseThemeData(theme);
}

void ItemWindow::openChild(CComPtr<IShellItem> childItem) {
    childItem = resolveLink(childItem);
    if (child) {
        int compare;
        if (SUCCEEDED(child->item->Compare(childItem, SICHINT_CANONICAL, &compare))
                && compare == 0) {
            return; // already open
        }
        closeChild();
    }
    SFGAOF attr;
    if (SUCCEEDED(childItem->GetAttributes(SFGAO_FOLDER, &attr))) {
        if (attr & SFGAO_FOLDER) {
            child.Attach(new FolderWindow(this, childItem));
        } else {
            child.Attach(new ThumbnailWindow(this, childItem));
        }
        SIZE size = child->defaultSize();
        POINT pos = childPos();
        // will flush message queue
        child->create({pos.x, pos.y, pos.x + size.cx, pos.y + size.cy}, SW_SHOWNOACTIVATE);
    }
}

void ItemWindow::closeChild() {
    if (child) {
        child->parent = nullptr;
        child->close();
        child = nullptr;
    }
}

void ItemWindow::openParent() {
    CComPtr<IShellItem> parentItem;
    if (SUCCEEDED(item->GetParent(&parentItem))) {
        parent.Attach(new FolderWindow(nullptr, parentItem));
        parent->child = this;
        SIZE size = parent->defaultSize();
        POINT pos = parentPos();
        parent->create({pos.x - size.cx, pos.y, pos.x, pos.y + size.cy}, SW_SHOWNORMAL);
        ShowWindow(parentButton, SW_HIDE);
    }
}

void ItemWindow::detachFromParent() {
    if (parent && parent->child == this) {
        parent->child = nullptr;
        parent->onChildDetached();
    }
    parent = nullptr;
    ShowWindow(parentButton, SW_SHOW);
}

void ItemWindow::onChildDetached() {}

POINT ItemWindow::childPos() {
    RECT windowRect = {}, clientRect = {};
    // https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect
    // GetWindowRect includes the drop shadow! (why??)
    GetWindowRect(hwnd, &windowRect);
    GetClientRect(hwnd, &clientRect);
    return {windowRect.left + clientRect.right, windowRect.top};
}

POINT ItemWindow::parentPos() {
    RECT windowRect = {};
    GetWindowRect(hwnd, &windowRect);
    POINT shadow = {windowRect.left, windowRect.top};
    ScreenToClient(hwnd, &shadow); // determine size of drop shadow
    return {windowRect.left - shadow.x * 2, windowRect.top};
}

CComPtr<IShellItem> ItemWindow::resolveLink(CComPtr<IShellItem> linkItem) {
    // https://stackoverflow.com/a/46064112
    CComPtr<IShellLink> link;
    if (SUCCEEDED(linkItem->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&link)))) {
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
            debugPrintf(L"Could not resolve link\n");
        }
    }
    return linkItem;
}

/* IUnknown */

STDMETHODIMP ItemWindow::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        {},
    };
    return QISearch(this, interfaces, id, obj);
}

STDMETHODIMP_(ULONG) ItemWindow::AddRef() {
    return InterlockedIncrement(&refCount);
}

STDMETHODIMP_(ULONG) ItemWindow::Release() {
    long r = InterlockedDecrement(&refCount);
    if (r == 0) {
        debugPrintf(L"Delete %s\n", &*title);
        delete this; // TODO ???
    }
    return r;
}


LRESULT CALLBACK captionButtonProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_PAINT) {
        // TODO is there a better way to do this?
        int buttonState = Button_GetState(hwnd);
        int themeState;
        if (buttonState & BST_PUSHED)
            themeState = PBS_PRESSED;
        else if (buttonState & BST_HOT)
            themeState = PBS_HOT;
        else
            themeState = PBS_NORMAL;

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        HTHEME theme = OpenThemeData(hwnd, L"Button");
        if (theme) {
            RECT buttonRect;
            GetClientRect(hwnd, &buttonRect);
            buttonRect = {buttonRect.left - 1, buttonRect.top,
                          buttonRect.right + 1, buttonRect.bottom + 1};
            if (themeState == PBS_NORMAL) {
                // hacky way to hide button while still rendering text properly
                InflateRect(&buttonRect, 8, 8);
            }
            DrawThemeBackground(theme, hdc, BP_PUSHBUTTON, themeState, &buttonRect, &ps.rcPaint);

            RECT contentRect;
            if (SUCCEEDED(GetThemeBackgroundContentRect(theme, hdc, BP_PUSHBUTTON, 
                    themeState, &buttonRect, &contentRect))) {
                wchar_t buttonText[32];
                GetWindowText(hwnd, buttonText, 32);
                HFONT oldFont = (HFONT) SelectObject(hdc, symbolFont);
                SetTextColor(hdc, 0x333333);
                SetBkMode(hdc, TRANSPARENT);
                DrawText(hdc, buttonText, -1, &contentRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldFont);
            }

            CloseThemeData(theme);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace
