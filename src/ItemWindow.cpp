#include "ItemWindow.h"
#include "CreateItemWindow.h"
#include "RectUtils.h"
#include "GDIUtils.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <vssym32.h>
#include <shellapi.h>
#include <strsafe.h>
#include <VersionHelpers.h>

namespace chromafile {

const wchar_t CHAIN_OWNER_CLASS[] = L"Chain";
const wchar_t WINDOW_THEME[] = L"CompositedWindow::Window";

// dimensions
const int PARENT_BUTTON_WIDTH = 34; // matches close button width in windows 10
const int WINDOW_ICON_PADDING = 4;
const int RENAME_BOX_PADDING = 2; // with border
const int TOOLBAR_HEIGHT = 24;
const int STATUS_TEXT_MARGIN = 4;
const int SYMBOL_FONT_HEIGHT = 14;
const int SNAP_DISTANCE = 32;

// these are Windows metrics/colors that are not exposed through the API >:(
const int WIN10_CXSIZEFRAME = 8;
// this is the color used in every high-contrast theme
// regular light mode theme uses #999999
const COLORREF WIN10_INACTIVE_CAPTION_COLOR = 0x636363;

static HANDLE symbolFontHandle = 0;
static HFONT captionFont = 0, statusFont = 0, symbolFont = 0;
// string resources
static wchar_t STR_SETTINGS_COMMAND[64] = {0};

// ItemWindow.h
long numOpenWindows;
CComPtr<ItemWindow> activeWindow;

int ItemWindow::CAPTION_HEIGHT = 0;
HACCEL ItemWindow::accelTable;

bool highContrastEnabled() {
    HIGHCONTRAST highContrast = {sizeof(highContrast)};
    SystemParametersInfo(SPI_GETHIGHCONTRAST, 0, &highContrast, 0);
    return highContrast.dwFlags & HCF_HIGHCONTRASTON;
}

int windowResizeMargin() {
    return IsThemeActive() ? WIN10_CXSIZEFRAME : GetSystemMetrics(SM_CXSIZEFRAME);
}

int windowBorderSize() {
    if (!IsWindows10OrGreater())
        return windowResizeMargin();
    if (highContrastEnabled()) {
        return WIN10_CXSIZEFRAME;
    } else {
        return 0;
    }
}

void ItemWindow::init() {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASS chainClass = {};
    chainClass.lpszClassName = CHAIN_OWNER_CLASS;
    chainClass.lpfnWndProc = chainWindowProc;
    chainClass.hInstance = hInstance;
    RegisterClass(&chainClass);

    RECT adjustedRect = {};
    AdjustWindowRectEx(&adjustedRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    CAPTION_HEIGHT = -adjustedRect.top; // = 31

    // TODO: alternatively use SystemParametersInfo with SPI_GETNONCLIENTMETRICS
    HTHEME theme = OpenThemeData(nullptr, WINDOW_THEME);
    if (theme) {
        LOGFONT logFont;
        if (checkHR(GetThemeSysFont(theme, TMT_CAPTIONFONT, &logFont)))
            captionFont = CreateFontIndirect(&logFont);
        if (checkHR(GetThemeSysFont(theme, TMT_STATUSFONT, &logFont)))
            statusFont = CreateFontIndirect(&logFont);
        checkHR(CloseThemeData(theme));
    }

    HRSRC symbolFontResource = FindResource(hInstance, MAKEINTRESOURCE(IDR_ICON_FONT), RT_FONT);
    HGLOBAL symbolFontAddr = LoadResource(hInstance, symbolFontResource);
    DWORD count = 1;
    symbolFontHandle = (HFONT)AddFontMemResourceEx(symbolFontAddr,
        SizeofResource(hInstance, symbolFontResource), 0, &count);
    symbolFont = CreateFont(SYMBOL_FONT_HEIGHT, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

    LoadString(hInstance, IDS_SETTINGS_COMMAND,
        STR_SETTINGS_COMMAND, _countof(STR_SETTINGS_COMMAND));

    accelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ITEM_ACCEL));
}

void ItemWindow::uninit() {
    if (captionFont)
        DeleteFont(captionFont);
    if (symbolFont)
        DeleteFont(symbolFont);
    if (symbolFontHandle)
        RemoveFontMemResourceEx(symbolFontHandle);
}

WNDCLASS ItemWindow::createWindowClass(const wchar_t *name) {
    WNDCLASS wndClass = {};
    wndClass.lpfnWndProc = ItemWindow::windowProc;
    wndClass.hInstance = GetModuleHandle(nullptr);
    wndClass.lpszClassName = name;
    wndClass.style = CS_HREDRAW; // ensure caption gets redrawn if width changes
    wndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // for toolbar
    return wndClass;
}

LRESULT CALLBACK ItemWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
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
        if (self->useCustomFrame()) {
            // https://docs.microsoft.com/en-us/windows/win32/dwm/customframe
            LRESULT dwmResult = 0;
            if (DwmDefWindowProc(hwnd, message, wParam, lParam, &dwmResult))
                return dwmResult;
        }
        return self->handleMessage(message, wParam, lParam);
    } else {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

ItemWindow::ItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
        : parent(parent),
          item(item) {
    storedChildSize = settings::getItemWindowSize();
}

bool ItemWindow::preserveSize() const {
    return true;
}

SIZE ItemWindow::requestedSize() const {
    return settings::getItemWindowSize();
}

DWORD ItemWindow::windowStyle() const {
    return WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX & ~WS_MAXIMIZEBOX;
}

bool ItemWindow::useCustomFrame() const {
    return true;
}

bool ItemWindow::alwaysOnTop() const {
    return false;
}

bool ItemWindow::stickToChild() const {
    return true;
}

bool ItemWindow::useDefaultStatusText() const {
    return true;
}

bool ItemWindow::create(RECT rect, int showCommand) {
    if (!checkHR(item->GetDisplayName(SIGDN_NORMALDISPLAY, &title)))
        return false;
    debugPrintf(L"Open %s\n", &*title);

    // keep window on screen
    if (!alwaysOnTop() && rect.left != CW_USEDEFAULT && rect.top != CW_USEDEFAULT) {
        POINT testPoint = {rect.left, rect.top + CAPTION_HEIGHT};
        HMONITOR nearestMonitor = MonitorFromPoint(testPoint, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo = {sizeof(monitorInfo)};
        GetMonitorInfo(nearestMonitor, &monitorInfo);
        if (testPoint.x < monitorInfo.rcWork.left)
            OffsetRect(&rect, monitorInfo.rcWork.left - testPoint.x, 0);
        if (testPoint.y > monitorInfo.rcWork.bottom)
            OffsetRect(&rect, 0, monitorInfo.rcWork.bottom - testPoint.y);
    }

    HWND owner;
    if (parent && !parent->alwaysOnTop())
        owner = GetWindowOwner(parent->hwnd);
    else if (child)
        owner = GetWindowOwner(child->hwnd);
    else
        owner = createChainOwner(showCommand);

    HWND createHwnd = CreateWindowEx(
        alwaysOnTop() ? WS_EX_TOPMOST : 0,
        // WS_CLIPCHILDREN fixes drawing glitches with the scrollbars
        className(), title, windowStyle() | WS_CLIPCHILDREN,
        rect.left, rect.top, rectWidth(rect), rectHeight(rect),
        owner, nullptr, GetModuleHandle(nullptr), this);
    if (!createHwnd) {
        debugPrintf(L"Couldn't create window\n");
        return false;
    }
    SetWindowLongPtr(owner, GWLP_USERDATA, GetWindowLongPtr(owner, GWLP_USERDATA) + 1);

    // https://docs.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-itaskbarlist2-markfullscreenwindow#remarks
    if (alwaysOnTop())
        SetProp(hwnd, L"NonRudeHWND", (HANDLE)TRUE);

    ShowWindow(createHwnd, showCommand);

    AddRef(); // keep window alive while open
    InterlockedIncrement(&numOpenWindows);
    return true;
}

HWND ItemWindow::createChainOwner(int showCommand) {
    HWND window = CreateWindow(CHAIN_OWNER_CLASS, nullptr, WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandle(nullptr), 0); // user data stores num owned windows
    if (!alwaysOnTop())
        ShowWindow(window, showCommand); // show in taskbar
    return window;
}

void ItemWindow::close() {
    PostMessage(hwnd, WM_CLOSE, 0, 0);
}

void ItemWindow::activate() {
    SetActiveWindow(hwnd);
}

void ItemWindow::setPos(POINT pos) {
    SetWindowPos(hwnd, nullptr, pos.x, pos.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ItemWindow::move(int x, int y) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    setPos({rect.left + x, rect.top + y});
}

RECT ItemWindow::windowBody() {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    if (!useCustomFrame())
        return clientRect;
    int top = (statusText || toolbar) ? (CAPTION_HEIGHT + TOOLBAR_HEIGHT) : CAPTION_HEIGHT;
    return RECT {0, top, clientRect.right, clientRect.bottom};
}

LRESULT ItemWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (contextMenu3) {
        LRESULT result;
        if (SUCCEEDED(contextMenu3->HandleMenuMsg2(message, wParam, lParam, &result)))
            return result;
    } else if (contextMenu2) {
        if (SUCCEEDED(contextMenu2->HandleMenuMsg(message, wParam, lParam)))
            return 0;
    }

    switch (message) {
        case WM_CREATE:
            onCreate();
            // ensure WM_NCCALCSIZE gets called; necessary for custom frame
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        case WM_CLOSE:
            // if the chain is in the foreground make sure it stays in the foreground.
            // once WM_DESTROY is called the active window will already be changed, so check here.
            if (parent && GetActiveWindow() == hwnd)
                parent->activate();
            break; // pass to DefWindowProc
        case WM_DESTROY:
            onDestroy();
            return 0;
        case WM_NCDESTROY:
            DestroyIcon((HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0));
            DestroyIcon((HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0));
            hwnd = nullptr;
            if (InterlockedDecrement(&numOpenWindows) == 0)
                PostQuitMessage(0);
            Release(); // allow window to be deleted
            return 0;
        case WM_ACTIVATE:
            onActivate(LOWORD(wParam), (HWND)lParam);
            return 0;
        case WM_NCACTIVATE:
            if (useCustomFrame()) {
                RECT captionRect;
                GetClientRect(hwnd, &captionRect);
                captionRect.bottom = CAPTION_HEIGHT;
                InvalidateRect(hwnd, &captionRect, FALSE);
            }
            break; // pass to DefWindowProc
        case WM_NCCALCSIZE:
            if (wParam == TRUE && useCustomFrame()) {
                // allow resizing past the edge of the window by reducing client rect
                NCCALCSIZE_PARAMS *params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                int resizeMargin = windowResizeMargin();
                params->rgrc[0].left = params->rgrc[0].left + resizeMargin;
                params->rgrc[0].top = params->rgrc[0].top;
                params->rgrc[0].right = params->rgrc[0].right - resizeMargin;
                params->rgrc[0].bottom = params->rgrc[0].bottom - resizeMargin;
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            LRESULT defHitTest = DefWindowProc(hwnd, message, wParam, lParam);
            if (defHitTest != HTCLIENT)
                return defHitTest;
            return hitTestNCA({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        }
        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            onPaint(paint);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            return COLOR_WINDOW + 1;
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
                int moveAmount = max(abs(moveAccum.x), abs(moveAccum.y));
                if (moveAmount > SNAP_DISTANCE) {
                    detachFromParent(GetKeyState(VK_SHIFT) < 0);
                    OffsetRect(desiredRect, moveAccum.x, moveAccum.y);
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
            if (parent && parent->stickToChild()) {
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
                    while (topParent->parent && topParent->parent->stickToChild())
                        topParent = topParent->parent;
                    topParent->move(moveX, moveY);
                }
            }
            return TRUE;
        case WM_SIZE: {
            onSize(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }
        case WM_NCLBUTTONDOWN: {
            POINT cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT clientCursor = cursor;
            ScreenToClient(hwnd, &clientCursor);
            if (wParam == HTCAPTION && PtInRect(&iconRect, clientCursor) &&
                    (GetKeyState(VK_SHIFT) < 0 || GetKeyState(VK_CONTROL) < 0
                    || GetKeyState(VK_MENU) < 0)) {
                if (DragDetect(hwnd, cursor)) {
                    proxyDrag({clientCursor.x - iconRect.left, clientCursor.y - iconRect.top});
                    return 0;
                }
            }
            break;
        }
        case WM_NCLBUTTONDBLCLK: {
            POINT cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT clientCursor = cursor;
            ScreenToClient(hwnd, &clientCursor);
            if (wParam == HTCAPTION && PtInRect(&proxyRect, clientCursor)) {
                if (GetKeyState(VK_MENU) < 0)
                    openProxyProperties();
                else
                    invokeProxyDefaultVerb(cursor);
                return 0;
            }
            break;
        }
        case WM_NCRBUTTONUP: { // WM_CONTEXTMENU doesn't seem to work in the caption
            POINT cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT clientCursor = cursor;
            ScreenToClient(hwnd, &clientCursor);
            if (wParam == HTCAPTION && PtInRect(&proxyRect, clientCursor)) {
                openProxyContextMenu(cursor);
                return 0;
            } else if (wParam == HTCAPTION) {
                PostMessage(hwnd, WM_SYSCOMMAND, SC_KEYMENU, ' '); // show system menu
                return 0;
            }
            break; // pass to DefWindowProc
        }
        case WM_NOTIFY:
            return onNotify((NMHDR *)lParam);
        case WM_COMMAND:
            if (lParam) {
                if (HIWORD(wParam) == 0 && LOWORD(wParam) != 0) { // special case for buttons, etc
                    if (onCommand(LOWORD(wParam)))
                        return 0;
                }
                if (onControlCommand((HWND)lParam, HIWORD(wParam)))
                    return 0;
            } else {
                if (onCommand(LOWORD(wParam)))
                    return 0;
            }
            break;
        case WM_SYSCOMMAND:
            if (LOWORD(wParam) == IDM_SETTINGS) {
                openSettingsDialog();
                return 0;
            }
            break;
        case MSG_SET_STATUS_TEXT: {
            CComHeapPtr<wchar_t> text;
            text.Attach((wchar_t *)lParam);
            setStatusText(text);
            return 0;
        }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

bool ItemWindow::handleTopLevelMessage(MSG *msg) {
    return !!TranslateAccelerator(hwnd, accelTable, msg);
}

void getItemIcons(CComPtr<IShellItem> item, HICON *iconLarge, HICON *iconSmall) {
    CComPtr<IExtractIcon> extractIcon;
    if (checkHR(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&extractIcon)))) {
        wchar_t iconFile[MAX_PATH];
        int index;
        UINT flags;
        if (extractIcon->GetIconLocation(0, iconFile, _countof(iconFile), &index, &flags) == S_OK) {
            UINT iconSizes = (GetSystemMetrics(SM_CXSMICON) << 16) + GetSystemMetrics(SM_CXICON);
            if (extractIcon->Extract(iconFile, index, iconLarge, iconSmall, iconSizes) != S_OK) {
                debugPrintf(L"IExtractIcon failed\n");
                // https://devblogs.microsoft.com/oldnewthing/20140501-00/?p=1103
                checkHR(SHDefExtractIcon(iconFile, index, flags, iconLarge, iconSmall, iconSizes));
            }
        }
    }
}

void ItemWindow::onCreate() {
    HICON iconLarge, iconSmall;
    getItemIcons(item, &iconLarge, &iconSmall);
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconLarge);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);

    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        if (checkHR(link.CoCreateInstance(__uuidof(ShellLink)))) {
            checkHR(link->SetIDList(idList));
        }
    }

    HMENU systemMenu = GetSystemMenu(hwnd, FALSE);
    AppendMenu(systemMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(systemMenu, MF_STRING, IDM_SETTINGS, STR_SETTINGS_COMMAND);

    if (!useCustomFrame())
        return; // !!
    /* everything below relates to caption */

    BOOL disableAnimations = true;
    checkHR(DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
        &disableAnimations, sizeof(disableAnimations)));
    MARGINS margins;
    margins.cxLeftWidth = 0;
    margins.cxRightWidth = 0;
    margins.cyTopHeight = CAPTION_HEIGHT;
    margins.cyBottomHeight = 0;
    checkHR(DwmExtendFrameIntoClientArea(hwnd, &margins));

    // will succeed for folders and EXEs, and fail for regular files
    if (SUCCEEDED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&itemDropTarget)))) {
        checkHR(dropTargetHelper.CoCreateInstance(CLSID_DragDropHelper));
        checkHR(RegisterDragDrop(hwnd, this));
    }

    HMODULE instance = GetWindowInstance(hwnd);
    proxyTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd, nullptr, instance, nullptr);
    SetWindowPos(proxyTooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (captionFont)
        SendMessage(proxyTooltip, WM_SETFONT, (WPARAM)captionFont, FALSE);
    TOOLINFO toolInfo = {sizeof(toolInfo)};
    toolInfo.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
    toolInfo.hwnd = hwnd;
    toolInfo.lpszText = title;
    SendMessage(proxyTooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);

    CComPtr<IShellItem> parentItem;
    bool showParentButton = !parent && SUCCEEDED(item->GetParent(&parentItem));
    parentButton = CreateWindow(L"BUTTON", nullptr,
        (showParentButton ? WS_VISIBLE : 0) | WS_CHILD | BS_PUSHBUTTON,
        0, 1, PARENT_BUTTON_WIDTH, CAPTION_HEIGHT - 2,
        hwnd, (HMENU)IDM_PREV_WINDOW, instance, nullptr);
    SetWindowSubclass(parentButton, parentButtonProc, 0, (DWORD_PTR)this);

    // will be positioned in beginRename
    renameBox = CreateWindow(L"EDIT", nullptr,
        WS_POPUP | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 0, 0,
        hwnd, nullptr, instance, nullptr);
    SetWindowSubclass(renameBox, renameBoxProc, 0, (DWORD_PTR)this);
    if (captionFont)
        SendMessage(renameBox, WM_SETFONT, (WPARAM)captionFont, FALSE);

    if (settings::getStatusTextEnabled()) {
        statusText = CreateWindow(L"STATIC", nullptr,
            WS_VISIBLE | WS_CHILD | SS_WORDELLIPSIS | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX
                | SS_NOTIFY, // allows tooltips to work
            STATUS_TEXT_MARGIN, CAPTION_HEIGHT, 0, TOOLBAR_HEIGHT,
            hwnd, nullptr, instance, nullptr);
        if (statusFont)
            SendMessage(statusText, WM_SETFONT, (WPARAM)statusFont, FALSE);
        if (useDefaultStatusText()) {
            statusTextThread.Attach(new StatusTextThread(item, hwnd));
            statusTextThread->start();
        }

        statusTooltip = CreateWindow(TOOLTIPS_CLASS, nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, nullptr, instance, nullptr);
        if (statusFont)
            SendMessage(statusTooltip, WM_SETFONT, (WPARAM)statusFont, FALSE);
        SendMessage(statusTooltip, TTM_SETMAXTIPWIDTH, 0, 0x7fff); // allow tabs
        toolInfo = {sizeof(toolInfo)};
        toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS | TTF_TRANSPARENT;
        toolInfo.hwnd = hwnd;
        toolInfo.uId = (UINT_PTR)statusText;
        toolInfo.lpszText = L"";
        SendMessage(statusTooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
    }

    if (settings::getToolbarEnabled()) {
        toolbar = CreateWindowEx(
            TBSTYLE_EX_MIXEDBUTTONS, TOOLBARCLASSNAME, nullptr,
            TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER
                | WS_VISIBLE | WS_CHILD,
            0, CAPTION_HEIGHT, 0, TOOLBAR_HEIGHT,
            hwnd, nullptr, instance, nullptr);
        SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
        SendMessage(toolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(TOOLBAR_HEIGHT, TOOLBAR_HEIGHT));
        SendMessage(toolbar, TB_SETBITMAPSIZE, 0, 0);
        if (symbolFont)
            SendMessage(toolbar, WM_SETFONT, (WPARAM)symbolFont, FALSE);
        addToolbarButtons(toolbar);
        SendMessage(toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(TOOLBAR_HEIGHT, TOOLBAR_HEIGHT));
        SIZE ideal;
        SendMessage(toolbar, TB_GETIDEALSIZE, FALSE, (LPARAM)&ideal);
        SetWindowPos(toolbar, nullptr, 0, 0, ideal.cx, TOOLBAR_HEIGHT,
            SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    }
}

bool ItemWindow::hasStatusText() {
    return statusText != nullptr;
}

void ItemWindow::setStatusText(wchar_t *text) {
    if (statusText)
        SetWindowText(statusText, text);
    if (statusTooltip) {
        TOOLINFO toolInfo = {sizeof(toolInfo)};
        toolInfo.hwnd = hwnd;
        toolInfo.uId = (UINT_PTR)statusText;
        toolInfo.lpszText = text;
        SendMessage(statusTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&toolInfo);
    }
}

TBBUTTON ItemWindow::makeToolbarButton(const wchar_t *text, WORD command, BYTE style, BYTE state) {
    return {I_IMAGENONE, command, state, (BYTE)(BTNS_SHOWTEXT | style), {}, 0, (INT_PTR)text};
}

void ItemWindow::setToolbarButtonState(WORD command, BYTE state) {
    SendMessage(toolbar, TB_SETSTATE, command, state);
}

void ItemWindow::addToolbarButtons(HWND tb) {
    TBBUTTON buttons[] = {
        makeToolbarButton(MDL2_REFRESH, IDM_REFRESH, 0),
        makeToolbarButton(MDL2_SETTING, IDM_SETTINGS, 0),
    };
    SendMessage(tb, TB_ADDBUTTONS, _countof(buttons), (LPARAM)buttons);
}

int ItemWindow::getToolbarTooltip(WORD command) {
    switch (command) {
        case IDM_REFRESH:
            return IDS_REFRESH_COMMAND;
        case IDM_SETTINGS:
            return IDS_SETTINGS_COMMAND;
    }
    return 0;
}

void ItemWindow::onDestroy() {
    debugPrintf(L"Close %s\n", &*title);
    clearParent();
    if (child)
        child->close(); // recursive
    child = nullptr; // onChildDetached will not be called
    if (activeWindow == this)
        activeWindow = nullptr;
    HWND owner = GetWindowOwner(hwnd);
    SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);
    if (SetWindowLongPtr(owner, GWLP_USERDATA, GetWindowLongPtr(owner, GWLP_USERDATA) - 1) == 1)
        DestroyWindow(owner); // last window in group

    if (itemDropTarget)
        RevokeDragDrop(hwnd);

    if (statusTextThread)
        statusTextThread->stop();
}

bool ItemWindow::onCommand(WORD command) {
    switch (command) {
        case IDM_NEXT_WINDOW:
            if (child)
                child->activate();
            return true;
        case IDM_PREV_WINDOW:
            if (parent)
                parent->activate();
            else if (!alwaysOnTop())
                openParent();
            return true;
        case IDM_DETACH:
            if (parent)
                detachAndMove(false);
            return true;
        case IDM_CLOSE_PARENT:
            if (parent)
                detachAndMove(true);
            return true;
        case IDM_CLOSE_WINDOW:
            close();
            return true;
        case IDM_REFRESH:
            if (resolveItem())
                refresh();
            return true;
        case IDM_PROXY_MENU:
            if (useCustomFrame()) {
                POINT menuPos = {proxyRect.right, proxyRect.top};
                ClientToScreen(hwnd, &menuPos);
                openProxyContextMenu(menuPos);
            }
            return true;
        case IDM_RENAME_PROXY:
            if (useCustomFrame())
                beginRename();
            return true;
        case IDM_DELETE_PROXY:
            if (!alwaysOnTop())
                deleteProxy();
            return true;
        case IDM_PARENT_MENU: {
            ItemWindow *rootParent = this;
            while (rootParent->parent)
                rootParent = rootParent->parent;
            if (rootParent->useCustomFrame()) {
                POINT menuPos = {0, CAPTION_HEIGHT};
                ClientToScreen(rootParent->hwnd, &menuPos);
                rootParent->openParentMenu(menuPos);
            }
            return true;
        }
        case IDM_HELP:
            ShellExecute(nullptr, L"open", L"https://github.com/vanjac/chromafile/wiki",
                nullptr, nullptr, SW_SHOWNORMAL);
            return true;
        case IDM_SETTINGS:
            openSettingsDialog();
            return true;
    }
    return false;
}

bool ItemWindow::onControlCommand(HWND controlHwnd, WORD notif) {
    if (controlHwnd == renameBox && notif == EN_KILLFOCUS) {
        if (IsWindowVisible(renameBox))
            completeRename();
        return true;
    }
    return false;
}

LRESULT ItemWindow::onNotify(NMHDR *nmHdr) {
    if (nmHdr->hwndFrom == proxyTooltip && nmHdr->code == TTN_SHOW) {
        // position tooltip on top of title
        RECT tooltipRect = titleRect;
        MapWindowRect(hwnd, nullptr, &tooltipRect);
        SendMessage(proxyTooltip, TTM_ADJUSTRECT, TRUE, (LPARAM)&tooltipRect);
        SetWindowPos(proxyTooltip, nullptr, tooltipRect.left, tooltipRect.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    } else if (nmHdr->hwndFrom == statusTooltip && nmHdr->code == TTN_SHOW) {
        RECT tooltipRect, statusRect;
        GetClientRect(statusTooltip, &tooltipRect);
        GetWindowRect(statusText, &statusRect);

        if (rectWidth(statusRect) > rectWidth(tooltipRect)) {
            // text is not truncated so tooltip does not need to be shown. move it offscreen
            SetWindowPos(statusTooltip, nullptr, -0x8000, -0x8000, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return TRUE;
        }
        int topY = statusRect.top + (TOOLBAR_HEIGHT - rectHeight(statusRect)) / 2 + 2;
        SendMessage(statusTooltip, TTM_ADJUSTRECT, TRUE, (LPARAM)&statusRect);
        OffsetRect(&statusRect, 0, topY - statusRect.top);
        SetWindowPos(statusTooltip, nullptr, statusRect.left, statusRect.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    } else if (nmHdr->code == TTN_GETDISPINFO) {
        NMTTDISPINFO *dispInfo = (NMTTDISPINFO *)nmHdr;
        if (!(dispInfo->uFlags & TTF_IDISHWND)) {
            int res = getToolbarTooltip((WORD)dispInfo->hdr.idFrom);
            if (res) {
                dispInfo->hinst = GetModuleHandle(nullptr);
                dispInfo->lpszText = MAKEINTRESOURCE(res);
                dispInfo->uFlags |= TTF_DI_SETITEM;
            }
        }
    }
    return 0;
}

void ItemWindow::onActivate(WORD state, HWND) {
    if (state != WA_INACTIVE) {
        activeWindow = this;
        HWND owner = GetWindowOwner(hwnd);
        SetWindowText(owner, title); // update taskbar / alt-tab
        SendMessage(owner, WM_SETICON, ICON_BIG, SendMessage(hwnd, WM_GETICON, ICON_BIG, 0));
        SendMessage(owner, WM_SETICON, ICON_SMALL, SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0));
        if (alwaysOnTop() && child) {
            SetWindowPos(child->hwnd, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

void ItemWindow::onSize(int width, int) {
    windowRectChanged();

    int toolbarLeft = width;
    if (toolbar) {
        RECT toolbarRect;
        GetClientRect(toolbar, &toolbarRect);
        toolbarLeft = width - rectWidth(toolbarRect);
        SetWindowPos(toolbar, nullptr, toolbarLeft, CAPTION_HEIGHT, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (statusText) {
        SetWindowPos(statusText, nullptr,
            0, 0, toolbarLeft - STATUS_TEXT_MARGIN * 2, TOOLBAR_HEIGHT,
            SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    }

    if (parent && preserveSize()) {
        RECT rect;
        GetWindowRect(hwnd, &rect);
        parent->storedChildSize = rectSize(rect);
    }
}

void ItemWindow::windowRectChanged() {
    if (child) {
        RECT childRect;
        GetWindowRect(child->hwnd, &childRect);
        child->setPos(childPos(rectSize(childRect)));
    }
}

LRESULT ItemWindow::hitTestNCA(POINT cursor) {
    // from https://docs.microsoft.com/en-us/windows/win32/dwm/customframe?redirectedfrom=MSDN#appendix-c-hittestnca-function
    // the default window proc handles the left, right, and bottom edges
    // so only need to check top edge and caption
    RECT screenRect;
    GetClientRect(hwnd, &screenRect);
    MapWindowRect(hwnd, nullptr, &screenRect);

    int resizeMargin = windowResizeMargin();
    if (cursor.y < screenRect.top + resizeMargin && useCustomFrame()) {
        // TODO window corners are a bit more complex than this
        if (cursor.x < screenRect.left + resizeMargin)
            return HTTOPLEFT;
        else if (cursor.x >= screenRect.right - resizeMargin)
            return HTTOPRIGHT;
        else
            return HTTOP;
    } else {
        return HTCAPTION; // can drag anywhere else in window to move!
    }
}

void ItemWindow::onPaint(PAINTSTRUCT paint) {
    if (!useCustomFrame())
        return;
    // from https://docs.microsoft.com/en-us/windows/win32/dwm/customframe?redirectedfrom=MSDN#appendix-b-painting-the-caption-title
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    HDC hdcPaint = CreateCompatibleDC(paint.hdc);
    if (!hdcPaint)
        return;

    int width = rectWidth(clientRect);
    int height = CAPTION_HEIGHT;

    // bitmap buffer for drawing caption
    // top-to-bottom order for DrawThemeTextEx()
    BITMAPINFO bitmapInfo = {{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB}};
    HBITMAP bitmap = CreateDIBSection(paint.hdc, &bitmapInfo, DIB_RGB_COLORS,
                                      nullptr, nullptr, 0);
    if (!bitmap) {
        DeleteDC(hdcPaint);
        return;
    }
    HBITMAP oldBitmap = SelectBitmap(hdcPaint, bitmap);

    int iconSize = GetSystemMetrics(SM_CXSMICON);
    TITLEBARINFOEX titleBar = {sizeof(titleBar)};
    SendMessage(hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&titleBar);
    int closeButtonWidth = rectWidth(titleBar.rgrect[5]);
    int reservedWidth = max(closeButtonWidth, PARENT_BUTTON_WIDTH);

    HFONT oldFont = nullptr;
    if (captionFont)
        oldFont = SelectFont(hdcPaint, captionFont); // must be set here for GetTextExtentPoint32

    SIZE titleSize = {};
    GetTextExtentPoint32(hdcPaint, title, (int)lstrlen(title), &titleSize);
    // include padding on the right side of the text; makes it look more centered
    int headerWidth = iconSize + WINDOW_ICON_PADDING * 2 + titleSize.cx;
    int headerLeft = (width - headerWidth) / 2;
    bool truncateTitle = headerLeft < reservedWidth + WINDOW_ICON_PADDING;
    if (truncateTitle) {
        headerLeft = reservedWidth + WINDOW_ICON_PADDING;
        headerWidth = width - closeButtonWidth - WINDOW_ICON_PADDING - headerLeft;
    }
    // store for hit testing proxy icon/text
    proxyRect = {headerLeft, 0, headerLeft + headerWidth, CAPTION_HEIGHT};

    if (truncateTitle) {
        TOOLINFO toolInfo = {sizeof(toolInfo)};
        toolInfo.hwnd = hwnd;
        toolInfo.rect = proxyRect;
        SendMessage(proxyTooltip, TTM_NEWTOOLRECT, 0, (LPARAM)&toolInfo); // rect to trigger tooltip
        SendMessage(proxyTooltip, TTM_ACTIVATE, TRUE, 0);
    } else {
        SendMessage(proxyTooltip, TTM_ACTIVATE, FALSE, 0);
    }

    iconRect.left = headerLeft;
    iconRect.top = (CAPTION_HEIGHT - iconSize) / 2;
    iconRect.right = iconRect.left + iconSize;
    iconRect.bottom = iconRect.top + iconSize;
    HICON icon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (icon) {
        DrawIconEx(hdcPaint, iconRect.left, iconRect.top, icon,
                   iconSize, iconSize, 0, nullptr, DI_NORMAL);
    }

    // the colors won't be right in many cases and it seems like there's no easy way to fix that
    // https://github.com/res2k/Windows10Colors
    HTHEME windowTheme = OpenThemeData(hwnd, WINDOW_THEME);
    if (windowTheme) {
        DTTOPTS textOpts = {sizeof(textOpts)};
        bool isActive = GetActiveWindow() == hwnd;
        if (IsWindows10OrGreater()) {
            // COLOR_INACTIVECAPTIONTEXT doesn't work in Windows 10
            // the documentation says COLOR_CAPTIONTEXT isn't supported either but it seems to work
            textOpts.crText = isActive ? GetSysColor(COLOR_CAPTIONTEXT)
                : WIN10_INACTIVE_CAPTION_COLOR;
        } else if (!isActive && highContrastEnabled()) {
            textOpts.crText = GetSysColor(COLOR_INACTIVECAPTIONTEXT);
        } else {
            textOpts.crText = GetSysColor(COLOR_CAPTIONTEXT);
        }
        textOpts.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;

        titleRect.left = headerLeft + iconSize + WINDOW_ICON_PADDING;
        titleRect.top = (CAPTION_HEIGHT - titleSize.cy) / 2;
        titleRect.right = clientRect.right - closeButtonWidth; // close button width
        titleRect.bottom = titleRect.top + titleSize.cy;
        checkHR(DrawThemeTextEx(windowTheme, hdcPaint, 0, 0, title, -1,
                                DT_LEFT | DT_WORD_ELLIPSIS, &titleRect, &textOpts));

        checkHR(CloseThemeData(windowTheme));
    }

    BitBlt(paint.hdc, 0, 0, width, height, hdcPaint, 0, 0, SRCCOPY);

    if (oldFont)
        SelectFont(hdcPaint, oldFont);
    SelectBitmap(hdcPaint, oldBitmap);
    DeleteBitmap(bitmap);
    DeleteDC(hdcPaint);
}

void ItemWindow::openChild(CComPtr<IShellItem> childItem) {
    childItem = resolveLink(hwnd, childItem);
    if (child) {
        const int compareFlags = SICHINT_CANONICAL | SICHINT_TEST_FILESYSPATH_IF_NOT_EQUAL;
        int compare;
        if (checkHR(child->item->Compare(childItem, compareFlags, &compare)) && compare == 0)
            return; // already open
        closeChild();
    }
    child = createItemWindow(this, childItem);
    SIZE size = child->preserveSize() ? storedChildSize : child->requestedSize();
    POINT pos = childPos(size);
    // will flush message queue
    child->create({pos.x, pos.y, pos.x + size.cx, pos.y + size.cy}, SW_SHOWNOACTIVATE);
}

void ItemWindow::closeChild() {
    if (child) {
        child->close();
        child = nullptr; // onChildDetached will not be called
    }
}

void ItemWindow::openParent() {
    CComPtr<IShellItem> parentItem;
    if (SUCCEEDED(item->GetParent(&parentItem))) {
        parent = createItemWindow(nullptr, parentItem);
        parent->child = this;
        if (preserveSize()) {
            RECT windowRect;
            GetWindowRect(hwnd, &windowRect);
            parent->storedChildSize = rectSize(windowRect);
        }

        SIZE size = parent->requestedSize();
        POINT pos = parentPos();
        parent->create({pos.x - size.cx, pos.y, pos.x, pos.y + size.cy}, SW_SHOWNORMAL);
        if (parentButton)
            ShowWindow(parentButton, SW_HIDE);
    }
}

void ItemWindow::clearParent() {
    if (parent && parent->child == this) {
        parent->child = nullptr;
        parent->onChildDetached();
    }
    parent = nullptr;
}

void ItemWindow::detachFromParent(bool closeParent) {
    CComPtr<ItemWindow> oldParent = parent;
    if (!parent->alwaysOnTop()) {
        HWND prevOwner = GetWindowOwner(hwnd);
        HWND owner = createChainOwner(SW_SHOWNORMAL);
        int numChildren = 0;
        for (ItemWindow *next = this; next != nullptr; next = next->child) {
            SetWindowLongPtr(next->hwnd, GWLP_HWNDPARENT, (LONG_PTR)owner);
            numChildren++;
        }
        SetWindowLongPtr(owner, GWLP_USERDATA, (LONG_PTR)numChildren);
        SetWindowLongPtr(prevOwner, GWLP_USERDATA,
            GetWindowLongPtr(prevOwner, GWLP_USERDATA) - numChildren);
    }
    clearParent();

    CComPtr<IShellItem> parentItem;
    if (parentButton && SUCCEEDED(item->GetParent(&parentItem)))
        ShowWindow(parentButton, SW_SHOW);
    if (closeParent) {
        ItemWindow *rootParent = oldParent;
        while (rootParent->parent && !rootParent->parent->alwaysOnTop())
            rootParent = rootParent->parent;
        if (!rootParent->alwaysOnTop())
            rootParent->close();
    } else {
        oldParent->activate(); // focus parent in chain
    }
    activate(); // bring this chain to front
}

void ItemWindow::onChildDetached() {}

void ItemWindow::detachAndMove(bool closeParent) {
    ItemWindow *rootParent = this;
    while (rootParent->parent && !rootParent->parent->alwaysOnTop())
        rootParent = rootParent->parent;
    RECT rootRect;
    GetWindowRect(rootParent->hwnd, &rootRect);

    detachFromParent(closeParent);

    if (closeParent) {
        setPos({rootRect.left, rootRect.top});
    } else {
        POINT pos = {rootRect.left + CAPTION_HEIGHT, rootRect.top + CAPTION_HEIGHT};

        HMONITOR curMonitor = MonitorFromWindow(rootParent->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo = {sizeof(monitorInfo)};
        GetMonitorInfo(curMonitor, &monitorInfo);
        if (pos.x + CAPTION_HEIGHT > monitorInfo.rcWork.right)
            pos.x = monitorInfo.rcWork.left;
        if (pos.y + CAPTION_HEIGHT > monitorInfo.rcWork.bottom)
            pos.y = monitorInfo.rcWork.top;
        setPos(pos);
    }
}

POINT ItemWindow::childPos(SIZE) {
    RECT windowRect = {}, clientRect = {};
    // https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect
    // GetWindowRect includes the drop shadow! (why??)
    GetWindowRect(hwnd, &windowRect);
    GetClientRect(hwnd, &clientRect);
    return {windowRect.left + clientRect.right + windowBorderSize() * 2, windowRect.top};
}

POINT ItemWindow::parentPos() {
    RECT windowRect = {};
    GetWindowRect(hwnd, &windowRect);
    POINT shadow = {windowRect.left, windowRect.top};
    ScreenToClient(hwnd, &shadow); // determine size of drop shadow
    return {windowRect.left - shadow.x * 2 - windowBorderSize() * 2, windowRect.top};
}

bool ItemWindow::resolveItem() {
    SFGAOF attr;
    // check if item exists
    if (SUCCEEDED(item->GetAttributes(SFGAO_VALIDATE, &attr)))
        return true;

    if (link) {
        checkHR(link->Resolve(nullptr, SLR_NO_UI));

        CComHeapPtr<ITEMIDLIST> currentIDList, newIDList;
        CComPtr<IShellFolder> desktopFolder;
        if (checkHR(SHGetIDListFromObject(item, &currentIDList))
                && checkHR(link->GetIDList(&newIDList))
                && checkHR(SHGetDesktopFolder(&desktopFolder))) {
            HRESULT compareHR;
            if (checkHR(compareHR = desktopFolder->CompareIDs(SHCIDS_CANONICALONLY,
                    currentIDList, newIDList))) {
                if ((short)HRESULT_CODE(compareHR) != 0) {
                    debugPrintf(L"Item has moved!\n");
                    CComPtr<IShellItem> newItem;
                    if (!checkHR(SHCreateItemFromIDList(newIDList, IID_PPV_ARGS(&newItem)))) {
                        close();
                        return false;
                    }
                    item = newItem;
                    onItemChanged();
                    return true;
                }
            }
        }
    }

    debugPrintf(L"Item has been deleted!\n");
    if (useCustomFrame()) {
        BOOL disableAnimations = false; // reenable animations to emphasize window closing
        checkHR(DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
            &disableAnimations, sizeof(disableAnimations)));
    }
    close();
    return false;
}

void ItemWindow::onItemChanged() {
    CComHeapPtr<wchar_t> newTitle;
    if (checkHR(item->GetDisplayName(SIGDN_NORMALDISPLAY, &newTitle))) {
        title = newTitle;
        SetWindowText(hwnd, title);
        TOOLINFO toolInfo = {sizeof(toolInfo)};
        toolInfo.hwnd = hwnd;
        toolInfo.lpszText = title;
        SendMessage(proxyTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&toolInfo);
    }
    if (useCustomFrame()) {
        // redraw caption
        RECT captionRect;
        GetClientRect(hwnd, &captionRect);
        captionRect.bottom = CAPTION_HEIGHT;
        InvalidateRect(hwnd, &captionRect, FALSE);

        if (itemDropTarget) {
            itemDropTarget = nullptr;
            if (!checkHR(item->BindToHandler(nullptr, BHID_SFUIObject,
                    IID_PPV_ARGS(&itemDropTarget))))
                RevokeDragDrop(hwnd);
        }
    }
}

void ItemWindow::refresh() {
    if (hasStatusText() && useDefaultStatusText()) {
        if (statusTextThread)
            statusTextThread->stop();
        statusTextThread.Attach(new StatusTextThread(item, hwnd));
        statusTextThread->start();
    }
}

void ItemWindow::openParentMenu(POINT point) {
    int iconSize = GetSystemMetrics(SM_CXSMICON);
    HMENU menu = CreatePopupMenu();
    int id = 0;
    CComPtr<IShellItem> curItem = item, parentItem;
    while (SUCCEEDED(curItem->GetParent(&parentItem))) {
        curItem = parentItem;
        parentItem = nullptr;
        id++;
        CComHeapPtr<wchar_t> name;
        if (!checkHR(curItem->GetDisplayName(SIGDN_NORMALDISPLAY, &name)))
            continue;
        AppendMenu(menu, MF_STRING, id, name);
        HICON itemIcon = nullptr;
        getItemIcons(curItem, nullptr, &itemIcon);
        if (itemIcon) {
            // http://shellrevealed.com:80/blogs/shellblog/archive/2007/02/06/Vista-Style-Menus_2C00_-Part-1-_2D00_-Adding-icons-to-standard-menus.aspx
            // icon must have alpha channel; GetIconInfo doesn't do this
            MENUITEMINFO itemInfo = {sizeof(itemInfo)};
            itemInfo.fMask = MIIM_BITMAP;
            itemInfo.hbmpItem = iconToPARGB32Bitmap(itemIcon, iconSize, iconSize);
            SetMenuItemInfo(menu, id, FALSE, &itemInfo);
        }
    }
    if (id == 0) { // empty
        DestroyMenu(menu);
        return;
    }
    int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, hwnd, nullptr);
    ItemWindow *parentWindow = this;
    for (int i = 0; i < cmd && parentWindow; i++) {
        if (!parentWindow->parent)
            parentWindow->openParent();
        parentWindow = parentWindow->parent;
    }

    // destroy bitmaps
    for (int i = 0, count = GetMenuItemCount(menu); i < count; i++) {
        MENUITEMINFO itemInfo = {sizeof(itemInfo)};
        itemInfo.fMask = MIIM_BITMAP;
        if (GetMenuItemInfo(menu, i, TRUE, &itemInfo) && itemInfo.hbmpItem)
            DeleteBitmap(itemInfo.hbmpItem);
    }
    DestroyMenu(menu);
}

void ItemWindow::invokeProxyDefaultVerb(POINT point) {
    // https://devblogs.microsoft.com/oldnewthing/20040930-00/?p=37693
    CComPtr<IContextMenu> contextMenu;
    if (!checkHR(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&contextMenu))))
        return;
    HMENU popupMenu = CreatePopupMenu();
    if (!popupMenu)
        return;
    if (checkHR(contextMenu->QueryContextMenu(popupMenu, 0, 1, 0x7FFF, CMF_DEFAULTONLY))) {
        UINT id = GetMenuDefaultItem(popupMenu, FALSE, 0);
        if (id != (UINT)-1)
            invokeContextMenuCommand(contextMenu, id - 1, point);
    }
    DestroyMenu(popupMenu);
}

void ItemWindow::openProxyProperties() {
    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        SHELLEXECUTEINFO info = {sizeof(info)};
        info.fMask = SEE_MASK_INVOKEIDLIST;
        info.lpVerb = L"properties";
        info.lpIDList = idList;
        info.hwnd = hwnd;
        ShellExecuteEx(&info);
    }
}

void ItemWindow::deleteProxy() {
    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        SHELLEXECUTEINFO info = {sizeof(info)};
        info.fMask = SEE_MASK_INVOKEIDLIST;
        info.lpVerb = L"delete";
        info.lpIDList = idList;
        info.hwnd = hwnd;
        ShellExecuteEx(&info);
        // TODO: remove this once there's an automatic system for tracking files
        resolveItem();
    }
}

void ItemWindow::openProxyContextMenu(POINT point) {
    // https://devblogs.microsoft.com/oldnewthing/20040920-00/?p=37823 and onward
    CComPtr<IContextMenu> contextMenu;
    if (!checkHR(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&contextMenu))))
        return;
    HMENU popupMenu = CreatePopupMenu();
    if (!popupMenu)
        return;
    UINT contextFlags = CMF_ITEMMENU | CMF_CANRENAME;
    if (GetKeyState(VK_SHIFT) < 0)
        contextFlags |= CMF_EXTENDEDVERBS;
    if (!checkHR(contextMenu->QueryContextMenu(popupMenu, 0, 1, 0x7FFF, contextFlags))) {
        DestroyMenu(popupMenu);
        return;
    }
    contextMenu2 = contextMenu;
    contextMenu3 = contextMenu;
    int cmd = TrackPopupMenuEx(popupMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, hwnd, nullptr);
    contextMenu2 = nullptr;
    contextMenu3 = nullptr;
    if (cmd > 0) {
        cmd -= 1; // idCmdFirst
        // https://groups.google.com/g/microsoft.public.win32.programmer.ui/c/PhXQcfhYPHQ
        wchar_t verb[64];
        verb[0] = 0; // some handlers may return S_OK without touching the buffer
        bool hasVerb = checkHR(contextMenu->GetCommandString(cmd, GCS_VERBW, nullptr,
            (char*)verb, _countof(verb)));
        if (hasVerb && lstrcmpi(verb, L"rename") == 0) {
            beginRename();
        } else {
            invokeContextMenuCommand(contextMenu, cmd, point);
        }

        if (hasVerb && lstrcmpi(verb, L"delete") == 0) {
            // TODO: remove this once there's an automatic system for tracking files
            resolveItem();
        }
    }
    DestroyMenu(popupMenu);
}

void ItemWindow::invokeContextMenuCommand(CComPtr<IContextMenu> contextMenu, int cmd, POINT point) {
    CMINVOKECOMMANDINFOEX info = {sizeof(info)};
    // TODO must set a thread reference
    // see https://docs.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-invokecommand
    info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE
        | CMIC_MASK_ASYNCOK | CMIC_MASK_FLAG_LOG_USAGE;
    if (GetKeyState(VK_CONTROL) < 0)
        info.fMask |= CMIC_MASK_CONTROL_DOWN;
    if (GetKeyState(VK_SHIFT) < 0)
        info.fMask |= CMIC_MASK_SHIFT_DOWN;
    info.hwnd = hwnd;
    info.lpVerb = MAKEINTRESOURCEA(cmd);
    info.lpVerbW = MAKEINTRESOURCEW(cmd);
    info.nShow = SW_SHOWNORMAL;
    info.ptInvoke = point;
    checkHR(contextMenu->InvokeCommand((CMINVOKECOMMANDINFO*)&info));
}

void ItemWindow::proxyDrag(POINT offset) {
    // https://devblogs.microsoft.com/oldnewthing/20041206-00/?p=37133 and onward
    CComPtr<IDataObject> dataObject;
    if (!checkHR(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&dataObject))))
        return;

    HICON icon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (icon) {
        CComPtr<IDragSourceHelper> dragHelper;
        // TODO could this reuse the existing helper?
        if (checkHR(dragHelper.CoCreateInstance(CLSID_DragDropHelper))) {
            ICONINFO iconInfo = {};
            GetIconInfo(icon, &iconInfo);
            int iconSize = GetSystemMetrics(SM_CXSMICON);
            SHDRAGIMAGE dragImage = {};
            dragImage.sizeDragImage = {iconSize, iconSize};
            dragImage.ptOffset = offset;
            dragImage.hbmpDragImage = iconInfo.hbmColor;
            dragHelper->InitializeFromBitmap(&dragImage, dataObject);
            DeleteBitmap(iconInfo.hbmColor);
            DeleteBitmap(iconInfo.hbmMask);
        }
    }

    DWORD okEffects = DROPEFFECT_COPY | DROPEFFECT_LINK | DROPEFFECT_MOVE;
    DWORD effect;
    checkHR(DoDragDrop(dataObject, this, okEffects, &effect));
    // effect is supposed to be set to DROPEFFECT_MOVE if the target was unable to delete the
    // original, however the only time I could trigger this was moving a file into a ZIP folder,
    // which does successfully delete the original, only with a delay. So handling this as intended
    // would actually break dragging into ZIP folders and cause loss of data!

    // TODO: remove this once there's an automatic system for tracking files
    resolveItem();
}

void ItemWindow::beginRename() {
    // update rename box rect
    int leftMargin = LOWORD(SendMessage(renameBox, EM_GETMARGINS, 0, 0));
    int renameHeight = rectHeight(titleRect) + RENAME_BOX_PADDING * 2;
    POINT renamePos = {titleRect.left - leftMargin - RENAME_BOX_PADDING,
                       (CAPTION_HEIGHT - renameHeight) / 2};
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    TITLEBARINFOEX titleBar = {sizeof(titleBar)};
    SendMessage(hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&titleBar);
    int renameWidth = clientRect.right - rectWidth(titleBar.rgrect[5]) - renamePos.x;
    ClientToScreen(hwnd, &renamePos);
    MoveWindow(renameBox, renamePos.x, renamePos.y, renameWidth, renameHeight, FALSE);

    SendMessage(renameBox, WM_SETTEXT, 0, (LPARAM)&*title);
    wchar_t *ext = PathFindExtension(title);
    if (ext == title) { // files that start with a dot
        SendMessage(renameBox, EM_SETSEL, 0, -1);
    } else {
        SendMessage(renameBox, EM_SETSEL, 0, ext - title);
    }
    ShowWindow(renameBox, SW_SHOW);
}

void ItemWindow::completeRename() {
    wchar_t newName[MAX_PATH];
    SendMessage(renameBox, WM_GETTEXT, _countof(newName), (LPARAM)newName);
    cancelRename();

    if (lstrcmp(newName, title) == 0)
        return; // names are identical, which would cause an unnecessary error message

    CComHeapPtr<wchar_t> fileName;
    // SIGDN_PARENTRELATIVEFORADDRESSBAR will always have the extension even if hidden in options
    // TODO: is this guaranteed?
    if (checkHR(item->GetDisplayName(SIGDN_PARENTRELATIVEFORADDRESSBAR, &fileName))) {
        int fileNameLen = lstrlen(fileName);
        int titleLen = lstrlen(title);
        if (fileNameLen > titleLen) { // if extensions are hidden in File Explorer Options
            debugPrintf(L"Appending extension %s\n", fileName + titleLen);
            checkHR(StringCchCat(newName, _countof(newName), fileName + titleLen));
        }
    }

    CComPtr<IFileOperation> operation;
    if (!checkHR(operation.CoCreateInstance(__uuidof(FileOperation))))
        return;
    // TODO: FOFX_ADDUNDORECORD requires Windows 8
    checkHR(operation->SetOperationFlags(FOFX_ADDUNDORECORD));
    if (!checkHR(operation->RenameItem(item, newName, nullptr)))
        return;
    checkHR(operation->PerformOperations());

    // TODO: remove this once there's an automatic system for tracking files
    resolveItem();
}

void ItemWindow::cancelRename() {
    ShowWindow(renameBox, SW_HIDE);
}

bool ItemWindow::dropAllowed(POINT point) {
    ScreenToClient(hwnd, &point);
    return PtInRect(&proxyRect, point);
}

/* IUnknown */

STDMETHODIMP ItemWindow::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        QITABENT(ItemWindow, IDropSource),
        QITABENT(ItemWindow, IDropTarget),
        {},
    };
    HRESULT hr = QISearch(this, interfaces, id, obj);
    if (SUCCEEDED(hr))
        return hr;
    return IUnknownImpl::QueryInterface(id, obj);
}

STDMETHODIMP_(ULONG) ItemWindow::AddRef() {
    return IUnknownImpl::AddRef(); // fix diamond inheritance
}

STDMETHODIMP_(ULONG) ItemWindow::Release() {
    return IUnknownImpl::Release();
}

/* IDropSource */

STDMETHODIMP ItemWindow::QueryContinueDrag(BOOL escapePressed, DWORD keyState) {
    if (escapePressed)
        return DRAGDROP_S_CANCEL;
    if (!(keyState & (MK_LBUTTON | MK_RBUTTON)))
        return DRAGDROP_S_DROP;
    return S_OK;
}

STDMETHODIMP ItemWindow::GiveFeedback(DWORD) {
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

/* IDropTarget */

STDMETHODIMP ItemWindow::DragEnter(IDataObject *dataObject, DWORD keyState, POINTL pt,
        DWORD *effect) {
    dropDataObject = dataObject;
    return DragOver(keyState, pt, effect);
}

STDMETHODIMP ItemWindow::DragLeave() {
    if (!itemDropTarget)
        return E_FAIL;
    if (overDropTarget) {
        if (!checkHR(itemDropTarget->DragLeave()))
            return E_FAIL;
        if (dropTargetHelper)
            checkHR(dropTargetHelper->DragLeave());
        overDropTarget = false;
    }
    return S_OK;
}

STDMETHODIMP ItemWindow::DragOver(DWORD keyState, POINTL pt, DWORD *effect) {
    if (!itemDropTarget)
        return E_FAIL;
    POINT point {pt.x, pt.y};
    bool nowOverTarget = dropAllowed(point);
    if (!nowOverTarget) {
        if (FAILED(DragLeave()))
            return E_FAIL;
        *effect = DROPEFFECT_NONE;
    } else if (!overDropTarget) {
        if (!checkHR(itemDropTarget->DragEnter(dropDataObject, keyState, pt, effect)))
            return E_FAIL;
        if (dropTargetHelper)
            checkHR(dropTargetHelper->DragEnter(hwnd, dropDataObject, &point, *effect));
    } else {
        if (!checkHR(itemDropTarget->DragOver(keyState, pt, effect)))
            return E_FAIL;
        if (dropTargetHelper)
            checkHR(dropTargetHelper->DragOver(&point, *effect));
    }
    overDropTarget = nowOverTarget;
    return S_OK;
}

STDMETHODIMP ItemWindow::Drop(IDataObject *dataObject, DWORD keyState, POINTL pt, DWORD *effect) {
    if (!itemDropTarget)
        return E_FAIL;
    POINT point {pt.x, pt.y};
    bool nowOverTarget = dropAllowed(point);
    if (!nowOverTarget) {
        if (FAILED(DragLeave()))
            return E_FAIL;
        *effect = DROPEFFECT_NONE;
    } else {
        if (!checkHR(itemDropTarget->Drop(dataObject, keyState, pt, effect)))
            return E_FAIL;
        if (dropTargetHelper)
            checkHR(dropTargetHelper->Drop(dataObject, &point, *effect));
    }
    overDropTarget = false;
    return S_OK;
}


BOOL CALLBACK ItemWindow::enumCloseChain(HWND hwnd, LPARAM lParam) {
    if (GetWindowOwner(hwnd) == (HWND)lParam) {
        ItemWindow *itemWindow = (ItemWindow *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (itemWindow && itemWindow->alwaysOnTop())
            itemWindow = itemWindow->child;
        if (itemWindow) {
            while (itemWindow->parent && !itemWindow->parent->alwaysOnTop())
                itemWindow = itemWindow->parent;
            itemWindow->close();
        }
        return FALSE;
    }
    return TRUE;
}

LRESULT CALLBACK ItemWindow::chainWindowProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) {
        // default behavior is to destroy owned windows without calling WM_CLOSE.
        // instead close the left-most chain window to give user a chance to save.
        // TODO this is awful
        EnumWindows(enumCloseChain, (LPARAM)hwnd);
        return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK ItemWindow::parentButtonProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
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
            InflateRect(&buttonRect, 1, 1);
            if (themeState == PBS_NORMAL) {
                FillRect(hdc, &ps.rcPaint, GetSysColorBrush(COLOR_WINDOW));
                makeBitmapOpaque(hdc, ps.rcPaint);
            } else {
                checkHR(DrawThemeBackground(theme, hdc, BP_PUSHBUTTON, themeState, &buttonRect,
                                            &ps.rcPaint));
            }

            RECT contentRect;
            if (checkHR(GetThemeBackgroundContentRect(theme, hdc, BP_PUSHBUTTON, 
                    themeState, &buttonRect, &contentRect))) {
                HFONT oldFont = SelectFont(hdc, symbolFont);
                SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
                SetBkMode(hdc, TRANSPARENT);
                DrawText(hdc, MDL2_CHEVRON_LEFT_MED, -1, &contentRect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectFont(hdc, oldFont);
                makeBitmapOpaque(hdc, ps.rcPaint);
            }

            checkHR(CloseThemeData(theme));
        }

        EndPaint(hwnd, &ps);
        return 0;
    } else if (message == WM_RBUTTONUP) {
        POINT cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ClientToScreen(hwnd, &cursor);
        ((ItemWindow *)refData)->openParentMenu(cursor);
        return 0;
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK ItemWindow::renameBoxProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    if (message == WM_CHAR && wParam == VK_RETURN) {
        ((ItemWindow *)refData)->completeRename();
        return 0;
    } else if (message == WM_CHAR && wParam == VK_ESCAPE) {
        ((ItemWindow *)refData)->cancelRename();
        return 0;
    } else if (message == WM_CLOSE) {
        // prevent user closing rename box (it will still be destroyed when owner is closed)
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

ItemWindow::StatusTextThread::StatusTextThread(CComPtr<IShellItem> item, HWND callbackWindow)
        : callbackWindow(callbackWindow) {
    checkHR(SHGetIDListFromObject(item, &itemIDList));
}

void ItemWindow::StatusTextThread::run() {
    CComPtr<IShellItem> localItem;
    if (!itemIDList || !checkHR(SHCreateItemFromIDList(itemIDList, IID_PPV_ARGS(&localItem))))
        return;
    itemIDList.Free();

    CComPtr<IQueryInfo> queryInfo;
    if (!checkHR(localItem->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&queryInfo))))
        return;
    
    CComHeapPtr<wchar_t> text;
    if (!checkHR(queryInfo->GetInfoTip(QITIPF_USESLOWTIP, &text)))
        return;
    for (wchar_t *c = text; *c; c++) {
        if (*c == '\n')
            *c = '\t';
    }

    EnterCriticalSection(&stopSection);
    if (!isStopped())
        PostMessage(callbackWindow, MSG_SET_STATUS_TEXT, 0, (LPARAM)text.Detach());
    LeaveCriticalSection(&stopSection);
}

} // namespace
