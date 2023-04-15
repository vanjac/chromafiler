#include "ItemWindow.h"
#include "CreateItemWindow.h"
#include "main.h"
#include "GeomUtils.h"
#include "GDIUtils.h"
#include "WinUtils.h"
#include "Settings.h"
#include "DPI.h"
#include "UIStrings.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <vssym32.h>
#include <shellapi.h>
#include <strsafe.h>
#include <VersionHelpers.h>

namespace chromafiler {

const wchar_t CHAIN_OWNER_CLASS[] = L"ChromaFile Chain";
const wchar_t WINDOW_THEME[] = L"CompositedWindow::Window";
const UINT SC_DRAGMOVE = SC_MOVE | 2; // https://stackoverflow.com/a/35880547/11525734

// dimensions
static int PARENT_BUTTON_WIDTH = 34; // caption only, matches close button width in windows 10
static int COMP_CAPTION_VMARGIN = 1;
static SIZE PROXY_PADDING = {7, 3};
static SIZE PROXY_INFLATE = {4, 1};
static int TOOLBAR_HEIGHT = 24;
static int STATUS_TEXT_MARGIN = 4;
static int STATUS_TOOLTIP_OFFSET = 2; // TODO not correct at higher DPIs
static int DETACH_DISTANCE = 32;

static int CAPTION_HEIGHT = 0; // calculated in init()

static LOGFONT SYMBOL_LOGFONT = {14, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
    ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets"};

// these are Windows metrics/colors that are not exposed through the API >:(
static int WIN10_CXSIZEFRAME = 8; // TODO not correct at higher DPIs
// this produces the color used in every high-contrast theme
// regular light mode theme uses #999999
const BYTE INACTIVE_CAPTION_ALPHA = 156;

const BYTE PROXY_BUTTON_STYLE = BTNS_DROPDOWN | BTNS_NOPREFIX;

static HANDLE symbolFontHandle = nullptr;
static HFONT captionFont = nullptr, statusFont = nullptr;
static HFONT symbolFont = nullptr;

static BOOL compositionEnabled = FALSE;

HACCEL ItemWindow::accelTable;

bool highContrastEnabled() {
    HIGHCONTRAST highContrast = {sizeof(highContrast)};
    checkLE(SystemParametersInfo(SPI_GETHIGHCONTRAST, 0, &highContrast, 0));
    return highContrast.dwFlags & HCF_HIGHCONTRASTON;
}

int windowResizeMargin() {
    return IsThemeActive() ? WIN10_CXSIZEFRAME : GetSystemMetrics(SM_CXSIZEFRAME);
}

int captionTopMargin() {
    return compositionEnabled ? COMP_CAPTION_VMARGIN : 0;
}

int windowBorderSize() {
    if (!IsWindows10OrGreater())
        return windowResizeMargin();
    if (highContrastEnabled()) {
        return WIN10_CXSIZEFRAME;
    } else {
        return 0; // TODO should be more space on Windows 11
    }
}

void ItemWindow::init() {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASS chainClass = {};
    chainClass.lpszClassName = CHAIN_OWNER_CLASS;
    chainClass.lpfnWndProc = chainWindowProc;
    chainClass.hInstance = hInstance;
    RegisterClass(&chainClass);

    checkHR(DwmIsCompositionEnabled(&compositionEnabled));

    if (compositionEnabled) {
        RECT adjustedRect = {};
        AdjustWindowRectEx(&adjustedRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
        CAPTION_HEIGHT = -adjustedRect.top; // = 31
    } else {
        CAPTION_HEIGHT = GetSystemMetrics(SM_CYCAPTION);
    }

    PARENT_BUTTON_WIDTH = scaleDPI(PARENT_BUTTON_WIDTH);
    COMP_CAPTION_VMARGIN = scaleDPI(COMP_CAPTION_VMARGIN);
    PROXY_PADDING = scaleDPI(PROXY_PADDING);
    PROXY_INFLATE = scaleDPI(PROXY_INFLATE);
    TOOLBAR_HEIGHT = scaleDPI(TOOLBAR_HEIGHT);
    STATUS_TEXT_MARGIN = scaleDPI(STATUS_TEXT_MARGIN);
    STATUS_TOOLTIP_OFFSET = scaleDPI(STATUS_TOOLTIP_OFFSET);
    DETACH_DISTANCE = scaleDPI(DETACH_DISTANCE);
    WIN10_CXSIZEFRAME = scaleDPI(WIN10_CXSIZEFRAME);
    SYMBOL_LOGFONT.lfHeight = scaleDPI(SYMBOL_LOGFONT.lfHeight);

    if (HTHEME theme = OpenThemeData(nullptr, WINDOW_THEME)) {
        LOGFONT logFont;
        if (checkHR(GetThemeSysFont(theme, TMT_CAPTIONFONT, &logFont)))
            captionFont = CreateFontIndirect(&logFont);
        if (checkHR(GetThemeSysFont(theme, TMT_STATUSFONT, &logFont)))
            statusFont = CreateFontIndirect(&logFont);
        checkHR(CloseThemeData(theme));
    } else {
        NONCLIENTMETRICS metrics = {sizeof(metrics)};
        SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        captionFont = CreateFontIndirect(&metrics.lfCaptionFont);
        statusFont = CreateFontIndirect(&metrics.lfStatusFont);
    }

    if (HRSRC symbolFontResource =
            checkLE(FindResource(hInstance, MAKEINTRESOURCE(IDR_ICON_FONT), RT_FONT))) {
        if (HGLOBAL symbolFontAddr = checkLE(LoadResource(hInstance, symbolFontResource))) {
            DWORD count = 1;
            symbolFontHandle = (HFONT)AddFontMemResourceEx(symbolFontAddr,
                SizeofResource(hInstance, symbolFontResource), 0, &count);
            symbolFont = CreateFontIndirect(&SYMBOL_LOGFONT);
        }
    }

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
    if (!compositionEnabled)
        wndClass.style = CS_HREDRAW; // redraw caption when resizing
    // change toolbar color
    if (IsWindows10OrGreater())
        wndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    return wndClass;
}

LRESULT CALLBACK ItemWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ItemWindow *self = nullptr;
    if (message == WM_NCCREATE) {
        CREATESTRUCT *create = (CREATESTRUCT*)lParam;
        self = (ItemWindow *)create->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd = hwnd;
    } else {
        self = (ItemWindow *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
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
          item(item) {}

bool ItemWindow::persistSizeInParent() const {
    return true;
}

SIZE ItemWindow::requestedSize() const {
    return scaleDPI(settings::getItemWindowSize());
}

SIZE ItemWindow::requestedChildSize() const {
    return scaleDPI(settings::getItemWindowSize());
}

DWORD ItemWindow::windowStyle() const {
    return WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX & ~WS_MAXIMIZEBOX;
}

DWORD ItemWindow::windowExStyle() const {
    return 0;
}

bool ItemWindow::useCustomFrame() const {
    return true;
}

bool ItemWindow::paletteWindow() const {
    return false;
}

bool ItemWindow::stickToChild() const {
    return true;
}

bool ItemWindow::useDefaultStatusText() const {
    return true;
}

bool ItemWindow::centeredProxy() const {
    return compositionEnabled;
}

SettingsPage ItemWindow::settingsStartPage() const {
    return SETTINGS_GENERAL;
}

const wchar_t * ItemWindow::helpURL() const {
    return L"https://github.com/vanjac/chromafiler/wiki";
}

bool ItemWindow::create(RECT rect, int showCommand) {
    if (!checkHR(item->GetDisplayName(SIGDN_NORMALDISPLAY, &title)))
        return false;
    debugPrintf(L"Open %s\n", &*title);

    HWND owner;
    if (parent && !parent->paletteWindow())
        owner = checkLE(GetWindowOwner(parent->hwnd));
    else if (child)
        owner = checkLE(GetWindowOwner(child->hwnd));
    else
        owner = createChainOwner(showCommand);

    HWND createHwnd = checkLE(CreateWindowEx(
        windowExStyle(), className(), title, windowStyle(),
        rect.left, rect.top, rectWidth(rect), rectHeight(rect),
        owner, nullptr, GetModuleHandle(nullptr), this));
    if (!createHwnd)
        return false;
    SetWindowLongPtr(owner, GWLP_USERDATA, GetWindowLongPtr(owner, GWLP_USERDATA) + 1);

    // https://docs.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-itaskbarlist2-markfullscreenwindow#remarks
    if (windowExStyle() & WS_EX_TOPMOST) {
        checkLE(SetProp(hwnd, L"NonRudeHWND", (HANDLE)TRUE));
        checkLE(SetProp(owner, L"NonRudeHWND", (HANDLE)TRUE));
    }

    ShowWindow(createHwnd, showCommand);

    AddRef(); // keep window alive while open
    windowOpened();
    return true;
}

HWND ItemWindow::createChainOwner(int showCommand) {
    // there are special cases here for popup windows (ie. the tray) to fix DPI scaling bugs.
    // see windowRectChanged() for details
    bool isPopup = windowStyle() & WS_POPUP;
    HWND window = checkLE(CreateWindowEx(isPopup ? (WS_EX_LAYERED | WS_EX_TOOLWINDOW) : 0,
        CHAIN_OWNER_CLASS, nullptr, isPopup ? WS_OVERLAPPED : WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandle(nullptr), 0)); // user data stores num owned windows
    ShowWindow(window, showCommand); // show in taskbar
    if (isPopup)
        SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA); // invisible but still drawn
    return window;
}

void ItemWindow::close() {
    PostMessage(hwnd, WM_CLOSE, 0, 0);
}

void ItemWindow::activate() {
    SetActiveWindow(hwnd);
}

void ItemWindow::setRect(RECT rect) {
    SetWindowPos(hwnd, nullptr, rect.left, rect.top, rectWidth(rect), rectHeight(rect),
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void ItemWindow::setPos(POINT pos) {
    SetWindowPos(hwnd, nullptr, pos.x, pos.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ItemWindow::move(int x, int y) {
    RECT rect = windowRect(hwnd);
    setPos({rect.left + x, rect.top + y});
}

RECT ItemWindow::windowBody() {
    RECT rect = clientRect(hwnd);
    if (useCustomFrame())
        rect.top += CAPTION_HEIGHT;
    if (statusText || cmdToolbar)
        rect.top += TOOLBAR_HEIGHT;
    return rect;
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
            if (closing)
                return 0;
            closing = true;
            if (!onCloseRequest()) {
                closing = false;
                return 0; // don't close
            }
            break; // pass to DefWindowProc
        case WM_DESTROY:
            onDestroy();
            return 0;
        case WM_NCDESTROY:
            if (imageList)
                checkLE(ImageList_Destroy(imageList));
            checkLE(DestroyIcon((HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0)));
            checkLE(DestroyIcon((HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0)));
            hwnd = nullptr;
            windowClosed();
            Release(); // allow window to be deleted
            return 0;
        case WM_ACTIVATE:
            onActivate(LOWORD(wParam), (HWND)lParam);
            return 0;
        case WM_NCACTIVATE: {
            if (proxyToolbar && IsWindows8OrGreater()) {
                BYTE alpha = wParam ? 255 : INACTIVE_CAPTION_ALPHA;
                SetLayeredWindowAttributes(proxyToolbar, 0, alpha, LWA_ALPHA);
            }
            LRESULT res = DefWindowProc(hwnd, message, wParam, lParam);
            if (!compositionEnabled && proxyToolbar)
                RedrawWindow(proxyToolbar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            return res;
        }
        case WM_NCPAINT: {
            LRESULT res = DefWindowProc(hwnd, message, wParam, lParam);
            if (!compositionEnabled && proxyToolbar)
                RedrawWindow(proxyToolbar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            return res;
        }
        case WM_MOUSEACTIVATE: {
            POINT cursor;
            GetCursorPos(&cursor);
            if (ChildWindowFromPoint(hwnd, screenToClient(hwnd, cursor)) == proxyToolbar)
                return MA_NOACTIVATE; // allow dragging without activating
            break;
        }
        case WM_NCCALCSIZE:
            if (wParam == TRUE && useCustomFrame()) {
                // allow resizing past the edge of the window by reducing client rect
                // TODO: revisit this
                NCCALCSIZE_PARAMS *params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                int resizeMargin = windowResizeMargin();
                params->rgrc[0].left = params->rgrc[0].left + resizeMargin;
                if (compositionEnabled)
                    params->rgrc[0].top = params->rgrc[0].top;
                else
                    params->rgrc[0].top = params->rgrc[0].top + resizeMargin;
                params->rgrc[0].right = params->rgrc[0].right - resizeMargin;
                params->rgrc[0].bottom = params->rgrc[0].bottom - resizeMargin;
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            LRESULT defHitTest = DefWindowProc(hwnd, message, wParam, lParam);
            if (defHitTest != HTCLIENT)
                return defHitTest;
            return hitTestNCA(pointFromLParam(lParam));
        }
        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            onPaint(paint);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_CTLCOLORSTATIC: { // status text background color
            HDC hdc = (HDC)wParam;
            int colorI = IsWindows10OrGreater() ? COLOR_WINDOW : COLOR_3DFACE;
            SetBkColor(hdc, GetSysColor(colorI));
            return colorI + 1;
        }
        case WM_ENTERSIZEMOVE: {
            moveAccum = {0, 0};
            lastSize = rectSize(windowRect(hwnd));
            return 0;
        }
        case WM_EXITSIZEMOVE: {
            onExitSizeMove(!pointEqual(moveAccum, {0, 0}),
                !sizeEqual(rectSize(windowRect(hwnd)), lastSize));
            return 0;
        }
        case WM_MOVING: {
            // https://www.drdobbs.com/make-it-snappy/184416407
            RECT *desiredRect = (RECT *)lParam;
            RECT curRect = windowRect(hwnd);
            moveAccum.x += desiredRect->left - curRect.left;
            moveAccum.y += desiredRect->top - curRect.top;
            if (parent) {
                int moveAmount = max(abs(moveAccum.x), abs(moveAccum.y));
                if (moveAmount > DETACH_DISTANCE) {
                    detachFromParent(GetKeyState(VK_SHIFT) < 0);
                    OffsetRect(desiredRect, moveAccum.x, moveAccum.y);
                } else {
                    *desiredRect = curRect;
                }
            }
            // required for WM_ENTERSIZEMOVE to behave correctly
            return TRUE;
        }
        case WM_MOVE:
            windowRectChanged();
            return 0;
        case WM_SIZING:
            if (parent && parent->stickToChild()) {
                RECT *desiredRect = (RECT *)lParam;
                RECT curRect = windowRect(hwnd);
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
            onSize(sizeFromLParam(lParam));
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT pos = pointFromLParam(lParam);
            if (pos.x == -1 && pos.y == -1) {
                RECT body = windowBody();
                trackContextMenu(clientToScreen(hwnd, {body.left, body.top}));
                return 0;
            }
            break;
        }
        case WM_NCRBUTTONUP: { // WM_CONTEXTMENU doesn't seem to work in the caption
            POINT cursor = pointFromLParam(lParam);
            POINT clientCursor = screenToClient(hwnd, cursor);
            if (wParam == HTCAPTION) {
                if (PtInRect(tempPtr(windowBody()), clientCursor)) {
                    trackContextMenu(cursor);
                } else {
                    PostMessage(hwnd, WM_SYSCOMMAND, SC_KEYMENU, ' '); // show system menu
                }
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
    HICON iconLarge = nullptr, iconSmall = nullptr;
    getItemIcons(item, &iconLarge, &iconSmall);
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconLarge);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);

    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        if (checkHR(link.CoCreateInstance(__uuidof(ShellLink)))) {
            checkHR(link->SetIDList(idList));
        }
    }

    if (!paletteWindow() && (!parent || parent->paletteWindow()))
        addChainPreview();

    if (compositionEnabled) {
        checkHR(DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
            tempPtr((BOOL)TRUE), sizeof(BOOL))); // disable animations
    }

    HMODULE instance = GetWindowInstance(hwnd);
    if (useCustomFrame()) {
        MARGINS margins;
        margins.cxLeftWidth = 0;
        margins.cxRightWidth = 0;
        margins.cyTopHeight = CAPTION_HEIGHT;
        margins.cyBottomHeight = 0;
        if (compositionEnabled)
            checkHR(DwmExtendFrameIntoClientArea(hwnd, &margins));

        int iconSize = GetSystemMetrics(SM_CXSMICON);
        imageList = ImageList_Create(iconSize, iconSize, ILC_MASK | ILC_COLOR32, 1, 0);
        ImageList_AddIcon(imageList, iconSmall);

        bool layered = IsWindows8OrGreater();
        proxyToolbar = CreateWindowEx(layered ? WS_EX_LAYERED : 0, TOOLBARCLASSNAME, nullptr,
            TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_REGISTERDROP
                | CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER | WS_VISIBLE | WS_CHILD,
            0, 0, 0, 0, hwnd, nullptr, instance, nullptr);
        if (layered)
            SetLayeredWindowAttributes(proxyToolbar, 0, INACTIVE_CAPTION_ALPHA, LWA_ALPHA);
        SendMessage(proxyToolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
        SendMessage(proxyToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
        if (captionFont)
            SendMessage(proxyToolbar, WM_SETFONT, (WPARAM)captionFont, FALSE);
        SendMessage(proxyToolbar, TB_SETIMAGELIST, 0, (LPARAM)imageList);
        SendMessage(proxyToolbar, TB_SETPADDING, 0, MAKELPARAM(PROXY_PADDING.cx, PROXY_PADDING.cy));
        TBBUTTON proxyButton = {0, IDM_PROXY_BUTTON, TBSTATE_ENABLED,
            PROXY_BUTTON_STYLE | BTNS_AUTOSIZE, {}, 0, (INT_PTR)(wchar_t *)title};
        SendMessage(proxyToolbar, TB_ADDBUTTONS, 1, (LPARAM)&proxyButton);
        SIZE ideal;
        SendMessage(proxyToolbar, TB_GETIDEALSIZE, FALSE, (LPARAM)&ideal);
        int top = captionTopMargin();
        int height = CAPTION_HEIGHT - captionTopMargin();
        if (IsWindows10OrGreater()) {
            // center vertically
            int buttonHeight = GET_Y_LPARAM(SendMessage(proxyToolbar, TB_GETBUTTONSIZE, 0, 0));
            top += max(0, (height - buttonHeight) / 2);
            height = min(height, buttonHeight);
        }
        SetWindowPos(proxyToolbar, nullptr, PARENT_BUTTON_WIDTH, top, ideal.cx, height,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // will succeed for folders and EXEs, and fail for regular files
        item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&itemDropTarget));
        checkHR(dropTargetHelper.CoCreateInstance(CLSID_DragDropHelper));

        proxyTooltip = checkLE(CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, nullptr, instance, nullptr));
        SetWindowPos(proxyTooltip, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (captionFont)
            SendMessage(proxyTooltip, WM_SETFONT, (WPARAM)captionFont, FALSE);
        TOOLINFO toolInfo = {sizeof(toolInfo)};
        toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS | TTF_TRANSPARENT;
        toolInfo.hwnd = hwnd;
        toolInfo.uId = (UINT_PTR)proxyToolbar;
        toolInfo.lpszText = title;
        SendMessage(proxyTooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);

        // will be positioned in beginRename
        renameBox = checkLE(CreateWindow(L"EDIT", nullptr,
            WS_POPUP | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 0, 0,
            hwnd, nullptr, instance, nullptr));
        SetWindowSubclass(renameBox, renameBoxProc, 0, (DWORD_PTR)this);
        if (captionFont)
            SendMessage(renameBox, WM_SETFONT, (WPARAM)captionFont, FALSE);
    } // if (useCustomFrame())

    if (useCustomFrame() && settings::getStatusTextEnabled()) {
        // potentially leave room for parent button
        int left = (centeredProxy() ? 0 : TOOLBAR_HEIGHT) + STATUS_TEXT_MARGIN;
        statusText = checkLE(CreateWindow(L"STATIC", nullptr,
            WS_VISIBLE | WS_CHILD | SS_WORDELLIPSIS | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX
                | SS_NOTIFY, // allows tooltips to work
            left, useCustomFrame() ? CAPTION_HEIGHT : 0, 0, TOOLBAR_HEIGHT,
            hwnd, nullptr, instance, nullptr));
        if (statusFont)
            SendMessage(statusText, WM_SETFONT, (WPARAM)statusFont, FALSE);
        if (useDefaultStatusText()) {
            statusTextThread.Attach(new StatusTextThread(item, hwnd));
            statusTextThread->start();
        }

        statusTooltip = checkLE(CreateWindow(TOOLTIPS_CLASS, nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, nullptr, instance, nullptr));
        if (statusFont)
            SendMessage(statusTooltip, WM_SETFONT, (WPARAM)statusFont, FALSE);
        SendMessage(statusTooltip, TTM_SETMAXTIPWIDTH, 0, 0x7fff); // allow tabs
        TOOLINFO toolInfo = {sizeof(toolInfo)};
        toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS | TTF_TRANSPARENT;
        toolInfo.hwnd = hwnd;
        toolInfo.uId = (UINT_PTR)statusText;
        toolInfo.lpszText = L"";
        SendMessage(statusTooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
    }

    if (useCustomFrame() && settings::getToolbarEnabled()) {
        cmdToolbar = checkLE(CreateWindowEx(
            TBSTYLE_EX_MIXEDBUTTONS, TOOLBARCLASSNAME, nullptr,
            TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER
                | WS_VISIBLE | WS_CHILD,
            0, useCustomFrame() ? CAPTION_HEIGHT : 0, 0, TOOLBAR_HEIGHT,
            hwnd, nullptr, instance, nullptr));
        SendMessage(cmdToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
        SendMessage(cmdToolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(TOOLBAR_HEIGHT, TOOLBAR_HEIGHT));
        SendMessage(cmdToolbar, TB_SETBITMAPSIZE, 0, 0);
        if (symbolFont)
            SendMessage(cmdToolbar, WM_SETFONT, (WPARAM)symbolFont, FALSE);
        addToolbarButtons(cmdToolbar);
        SendMessage(cmdToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(TOOLBAR_HEIGHT, TOOLBAR_HEIGHT));
        SIZE ideal;
        SendMessage(cmdToolbar, TB_GETIDEALSIZE, FALSE, (LPARAM)&ideal);
        SetWindowPos(cmdToolbar, nullptr, 0, 0, ideal.cx, TOOLBAR_HEIGHT,
            SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    }

    if (useCustomFrame() && (centeredProxy() || statusText || cmdToolbar)) {
        CComPtr<IShellItem> parentItem;
        bool showParentButton = !parent && SUCCEEDED(item->GetParent(&parentItem));
        // put button in caption with centered proxy, otherwise in status area
        int top = centeredProxy() ? captionTopMargin() : (useCustomFrame() ? CAPTION_HEIGHT : 0);
        int width = centeredProxy() ? PARENT_BUTTON_WIDTH : TOOLBAR_HEIGHT;
        int height = centeredProxy() ? (CAPTION_HEIGHT - captionTopMargin()) : TOOLBAR_HEIGHT;
        parentToolbar = CreateWindowEx(TBSTYLE_EX_MIXEDBUTTONS, TOOLBARCLASSNAME, nullptr,
            TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER
                | (showParentButton ? WS_VISIBLE : 0) | WS_CHILD,
            0, top, width, height, hwnd, nullptr, instance, nullptr);
        SendMessage(parentToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
        SendMessage(parentToolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(width, width));
        SendMessage(parentToolbar, TB_SETBITMAPSIZE, 0, 0);
        if (symbolFont)
            SendMessage(parentToolbar, WM_SETFONT, (WPARAM)symbolFont, FALSE);
        TBBUTTON parentButton = {I_IMAGENONE, IDM_PREV_WINDOW, TBSTATE_ENABLED,
            BTNS_SHOWTEXT, {}, 0, (INT_PTR)MDL2_CHEVRON_LEFT_MED};
        SendMessage(parentToolbar, TB_ADDBUTTONS, 1, (LPARAM)&parentButton);
        SendMessage(parentToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(width, height));
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
    if (cmdToolbar)
        SendMessage(cmdToolbar, TB_SETSTATE, command, state);
}

void ItemWindow::addToolbarButtons(HWND tb) {
    TBBUTTON buttons[] = {
        makeToolbarButton(MDL2_MORE, IDM_CONTEXT_MENU, BTNS_WHOLEDROPDOWN),
    };
    SendMessage(tb, TB_ADDBUTTONS, _countof(buttons), (LPARAM)buttons);
}

int ItemWindow::getToolbarTooltip(WORD command) {
    switch (command) {
        case IDM_PREV_WINDOW:
            return IDS_OPEN_PARENT_COMMAND;
        case IDM_CONTEXT_MENU:
            return IDS_MENU_COMMAND;
    }
    return 0;
}

void ItemWindow::trackContextMenu(POINT pos) {
    HMENU menu = CreatePopupMenu();
    trackContextMenu(pos, menu);
    checkLE(DestroyMenu(menu));
}

int ItemWindow::trackContextMenu(POINT pos, HMENU menu) {
    if (GetMenuItemCount(menu) != 0)
        AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
    HMENU common = LoadMenu(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_ITEM_MENU));
    Shell_MergeMenus(menu, common, (UINT)-1, 0, 0xFFFF, MM_ADDSEPARATOR);
    int cmd = TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pos.x, pos.y, hwnd, nullptr);
    onCommand((WORD)cmd);
    checkLE(DestroyMenu(common));
    return cmd;
}

bool ItemWindow::onCloseRequest() {
    // if the chain is in the foreground make sure it stays in the foreground.
    // once onDestroy() is called the active window will already be changed, so check here instead
    if (parent && GetActiveWindow() == hwnd)
        parent->activate();
    return true;
}

void ItemWindow::onDestroy() {
    debugPrintf(L"Close %s\n", &*title);
    clearParent();
    if (child)
        child->close(); // recursive
    child = nullptr; // onChildDetached will not be called
    if (activeWindow == this)
        activeWindow = nullptr;

    removeChainPreview();
    if (HWND owner = checkLE(GetWindowOwner(hwnd))) {
        SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);
        if (SetWindowLongPtr(owner, GWLP_USERDATA, GetWindowLongPtr(owner, GWLP_USERDATA) - 1) == 1)
            DestroyWindow(owner); // last window in group
    }

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
            else
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
        case IDM_PROXY_MENU: {
            openProxyContextMenuFeedback();
            return true;
        }
        case IDM_RENAME_PROXY:
            beginRename();
            return true;
        case IDM_DELETE_PROXY:
            deleteProxy();
            return true;
        case IDM_PARENT_MENU: {
            ItemWindow *rootParent = this;
            while (rootParent->parent)
                rootParent = rootParent->parent;
            if (!rootParent->paletteWindow())
                rootParent->openParentMenu();
            return true;
        }
        case IDM_HELP:
            ShellExecute(nullptr, L"open", helpURL(), nullptr, nullptr, SW_SHOWNORMAL);
            return true;
        case IDM_SETTINGS:
            openSettingsDialog(settingsStartPage());
            return true;
        case IDM_DEBUG_NAMES:
            debugDisplayNames(hwnd, item);
            return true;
    }
    return false;
}

LRESULT ItemWindow::onDropdown(int command, POINT pos) {
    switch (command) {
        case IDM_PROXY_BUTTON:
            openProxyContextMenu();
            return TBDDRET_DEFAULT;
        case IDM_CONTEXT_MENU:
            trackContextMenu(pos);
            return TBDDRET_DEFAULT;
    }
    return TBDDRET_NODEFAULT;
}

bool ItemWindow::onControlCommand(HWND controlHwnd, WORD notif) {
    if (renameBox && controlHwnd == renameBox && notif == EN_KILLFOCUS) {
        if (IsWindowVisible(renameBox))
            completeRename();
        return true;
    } else if (statusText && controlHwnd == statusText && notif == STN_CLICKED) {
        POINT cursorPos = {};
        GetCursorPos(&cursorPos);
        if (DragDetect(hwnd, cursorPos))
            SendMessage(hwnd, WM_SYSCOMMAND, SC_DRAGMOVE, 0);
        return true;
    }
    return false;
}

LRESULT ItemWindow::onNotify(NMHDR *nmHdr) {
    if (proxyTooltip && nmHdr->hwndFrom == proxyTooltip && nmHdr->code == TTN_SHOW) {
        // position tooltip on top of title
        RECT tooltipRect = titleRect();
        MapWindowRect(hwnd, nullptr, &tooltipRect);
        SendMessage(proxyTooltip, TTM_ADJUSTRECT, TRUE, (LPARAM)&tooltipRect);
        SetWindowPos(proxyTooltip, nullptr, tooltipRect.left, tooltipRect.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    } else if (statusTooltip && nmHdr->hwndFrom == statusTooltip && nmHdr->code == TTN_SHOW) {
        RECT statusRect = windowRect(statusText);

        if (rectWidth(statusRect) > clientSize(statusTooltip).cx) {
            // text is not truncated so tooltip does not need to be shown. move it offscreen
            SetWindowPos(statusTooltip, nullptr, -0x8000, -0x8000, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return TRUE;
        }
        int topY = statusRect.top + (TOOLBAR_HEIGHT - rectHeight(statusRect)) / 2
            + STATUS_TOOLTIP_OFFSET;
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
    } else if (nmHdr->code == TBN_DROPDOWN) {
        NMTOOLBAR *nmToolbar = (NMTOOLBAR *)nmHdr;
        POINT menuPos = {nmToolbar->rcButton.left, nmToolbar->rcButton.bottom};
        return onDropdown(nmToolbar->iItem, clientToScreen(nmHdr->hwndFrom, menuPos));
    } else if ((nmHdr->hwndFrom == proxyToolbar || nmHdr->hwndFrom == parentToolbar)
            && nmHdr->code == NM_CUSTOMDRAW) {
        NMTBCUSTOMDRAW *customDraw = (NMTBCUSTOMDRAW *)nmHdr;
        if (customDraw->nmcd.dwDrawStage == CDDS_PREPAINT) {
            return CDRF_NOTIFYPOSTPAINT;
        } else if (customDraw->nmcd.dwDrawStage == CDDS_POSTPAINT) {
            // fix title bar rendering (when not layered)
            makeBitmapOpaque(customDraw->nmcd.hdc, clientRect(nmHdr->hwndFrom));
        }
        return CDRF_DODEFAULT;
    } else if (nmHdr->hwndFrom == proxyToolbar && nmHdr->code == NM_LDOWN) {
        if (IsWindowVisible(renameBox))
            return FALSE; // don't steal focus
        NMMOUSE *mouse = (NMMOUSE *)nmHdr;
        POINT screenPos = clientToScreen(proxyToolbar, mouse->pt);
        if (DragDetect(hwnd, screenPos)) {
            POINT newCursorPos = {};
            GetCursorPos(&newCursorPos);
            // detect click-and-hold
            // https://devblogs.microsoft.com/oldnewthing/20100304-00/?p=14733
            RECT dragRect = {screenPos.x, screenPos.y, screenPos.x, screenPos.y};
            InflateRect(&dragRect, GetSystemMetrics(SM_CXDRAG), GetSystemMetrics(SM_CYDRAG));
            if (PtInRect(&dragRect, newCursorPos)
                    || GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0) {
                RECT toolbarRect = windowRect(proxyToolbar);
                proxyDrag({screenPos.x - toolbarRect.left, screenPos.y - toolbarRect.top});
            } else {
                SetActiveWindow(hwnd); // wasn't activated due to handling WM_MOUSEACTIVATE
                // https://stackoverflow.com/a/35880547/11525734
                SendMessage(hwnd, WM_SYSCOMMAND, SC_DRAGMOVE, 0);
                return TRUE;
            }
        } else {
            SetActiveWindow(hwnd); // wasn't activated due to handling WM_MOUSEACTIVATE
        }
        return FALSE;
    } else if (nmHdr->hwndFrom == proxyToolbar && nmHdr->code == NM_CLICK) {
        // actually a double-click, since we captured the mouse in the NM_LDOWN handler (???)
        if (IsWindowVisible(renameBox))
            return FALSE;
        else if (GetKeyState(VK_MENU) < 0)
            openProxyProperties();
        else
            invokeProxyDefaultVerb();
        return TRUE;
    } else if (nmHdr->hwndFrom == proxyToolbar && nmHdr->code == NM_RCLICK) {
        openProxyContextMenuFeedback();
        return TRUE;
    } else if (nmHdr->hwndFrom == proxyToolbar && nmHdr->code == TBN_GETOBJECT) {
        NMOBJECTNOTIFY *objNotif = (NMOBJECTNOTIFY *)nmHdr;
        if (itemDropTarget || draggingObject) {
            objNotif->pObject = (IDropTarget *)this;
            objNotif->hResult = S_OK;
            AddRef();
        } else {
            objNotif->pObject = nullptr;
            objNotif->hResult = E_FAIL;
        }
        return 0;
    } else if (nmHdr->hwndFrom == parentToolbar && nmHdr->code == NM_LDOWN) {
        NMMOUSE *mouse = (NMMOUSE *)nmHdr;
        POINT screenPos = clientToScreen(parentToolbar, mouse->pt);
        if (DragDetect(hwnd, screenPos)) {
            SendMessage(hwnd, WM_SYSCOMMAND, SC_DRAGMOVE, 0);
        } else {
            onCommand((WORD)mouse->dwItemSpec); // button clicked normally
        }
        return TRUE;
    } else if (nmHdr->hwndFrom == parentToolbar && nmHdr->code == NM_RCLICK) {
        openParentMenu();
        return TRUE;
    }
    return 0;
}

void ItemWindow::onActivate(WORD state, HWND) {
    if (state != WA_INACTIVE) {
        activeWindow = this;
        if (paletteWindow() && child) {
            SetWindowPos(child->hwnd, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        if (firstActivate && !parent)
            resolveItem();
        firstActivate = true;
    }
}

void ItemWindow::onSize(SIZE size) {
    windowRectChanged();

    if (proxyToolbar) {
        TITLEBARINFOEX titleBar = {sizeof(titleBar)};
        SendMessage(hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&titleBar);
        int closeButtonWidth = rectWidth(titleBar.rgrect[5]);

        // turn on autosize to calculate the ideal width
        TBBUTTONINFO buttonInfo = {sizeof(buttonInfo)};
        buttonInfo.dwMask = TBIF_STYLE;
        buttonInfo.fsStyle = PROXY_BUTTON_STYLE | BTNS_AUTOSIZE;
        SendMessage(proxyToolbar, TB_SETBUTTONINFO, IDM_PROXY_BUTTON, (LPARAM)&buttonInfo);
        SIZE ideal;
        SendMessage(proxyToolbar, TB_GETIDEALSIZE, FALSE, (LPARAM)&ideal);
        int actualLeft;
        if (centeredProxy()) {
            int idealLeft = (size.cx - ideal.cx) / 2;
            actualLeft = max(PARENT_BUTTON_WIDTH, idealLeft);
        } else {
            actualLeft = 0; // cover actual window title/icon
        }
        int maxWidth = size.cx - actualLeft - closeButtonWidth;
        int actualWidth = min(ideal.cx, maxWidth);

        // turn off autosize to set exact width
        buttonInfo.dwMask = TBIF_STYLE | TBIF_SIZE;
        buttonInfo.fsStyle = PROXY_BUTTON_STYLE;
        buttonInfo.cx = (WORD)actualWidth;
        SendMessage(proxyToolbar, TB_SETBUTTONINFO, IDM_PROXY_BUTTON, (LPARAM)&buttonInfo);

        RECT rect = windowRect(proxyToolbar);
        MapWindowRect(nullptr, hwnd, &rect);
        SetWindowPos(proxyToolbar, nullptr, actualLeft, rect.top,
            actualWidth, rectHeight(rect), SWP_NOZORDER | SWP_NOACTIVATE);

        // show/hide tooltip if text truncated
        SendMessage(proxyTooltip, TTM_ACTIVATE, ideal.cx > maxWidth, 0);
    }

    int toolbarLeft = size.cx;
    if (cmdToolbar) {
        toolbarLeft = size.cx - clientSize(cmdToolbar).cx;
        SetWindowPos(cmdToolbar, nullptr, toolbarLeft, useCustomFrame() ? CAPTION_HEIGHT : 0, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (statusText) {
        RECT statusRect = windowRect(statusText);
        MapWindowRect(nullptr, hwnd, &statusRect);
        SetWindowPos(statusText, nullptr,
            0, 0, toolbarLeft - STATUS_TEXT_MARGIN - statusRect.left, TOOLBAR_HEIGHT,
            SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
        InvalidateRect(statusText, nullptr, FALSE);
    }
}

void ItemWindow::windowRectChanged() {
    if (child)
        child->setPos(childPos(rectSize(windowRect(child->hwnd))));
    if (windowStyle() & WS_POPUP) {
        // unlike overlapped windows, popups do not use the current monitor DPI, instead they use
        // the DPI of the most recently active overlapped window. this will always be the owner if
        // it's visible and overlapped. here we move the owner to match the popup's position as it
        // moves so it uses the correct DPI
        RECT rect = windowRect(hwnd);
        // must have visible client area to affect DPI scaling
        int minHeight = GetSystemMetrics(SM_CYMINTRACK) + 1;
        if (rectHeight(rect) < minHeight)
            rect.bottom = rect.top + minHeight;
        MoveWindow(checkLE(GetWindowOwner(hwnd)), rect.left, rect.top,
            rectWidth(rect), rectHeight(rect), FALSE);
    }
}

void ItemWindow::onExitSizeMove(bool, bool sized) {
    if (sized && parent && persistSizeInParent())
        parent->onChildResized(rectSize(windowRect(hwnd)));
}

LRESULT ItemWindow::hitTestNCA(POINT cursor) {
    // from https://docs.microsoft.com/en-us/windows/win32/dwm/customframe?redirectedfrom=MSDN#appendix-c-hittestnca-function
    // the default window proc handles the left, right, and bottom edges
    // so only need to check top edge and caption
    RECT screenRect = clientRect(hwnd);
    MapWindowRect(hwnd, nullptr, &screenRect);

    int resizeMargin = windowResizeMargin();
    int captionTop = compositionEnabled ? (screenRect.top + resizeMargin) : screenRect.top;
    if (cursor.y < captionTop && useCustomFrame()) {
        // TODO window corners are a bit more complex than this
        if (cursor.x < screenRect.left + resizeMargin)
            return HTTOPLEFT;
        else if (cursor.x >= screenRect.right - resizeMargin)
            return HTTOPRIGHT;
        else
            return HTTOP;
    } else if (useCustomFrame() && !IsThemeActive()
            && cursor.x >= screenRect.right - GetSystemMetrics(SM_CXSIZE)
            && cursor.y < captionTop + CAPTION_HEIGHT) {
        return HTCLOSE;
    } else {
        return HTCAPTION; // can drag anywhere else in window to move!
    }
}

void ItemWindow::onPaint(PAINTSTRUCT paint) {
    // on Windows 10+ we set the hbrBackground of the class so this isn't necessary
    if ((statusText || cmdToolbar) && !IsWindows10OrGreater()) {
        int top = useCustomFrame() ? CAPTION_HEIGHT : 0;
        RECT toolbarRect = {0, top, clientSize(hwnd).cx, top + TOOLBAR_HEIGHT};
        FillRect(paint.hdc, &toolbarRect, GetSysColorBrush(COLOR_3DFACE));
    }
    if (useCustomFrame() && compositionEnabled) {
        // clear alpha channel
        BITMAPINFO bitmapInfo = {{sizeof(BITMAPINFOHEADER), 1, 1, 1, 32, BI_RGB}};
        RGBQUAD bitmapBits = { 0x00, 0x00, 0x00, 0x00 };
        StretchDIBits(paint.hdc, 0, 0, clientSize(hwnd).cx, CAPTION_HEIGHT,
                    0, 0, 1, 1, &bitmapBits, &bitmapInfo,
                    DIB_RGB_COLORS, SRCCOPY);
    }
}

RECT ItemWindow::titleRect() {
    RECT rect;
    SendMessage(proxyToolbar, TB_GETRECT, IDM_PROXY_BUTTON, (LPARAM)&rect);
    // hardcoded nonsense
    SIZE offset = IsThemeActive() ? SIZE{7, 5} : SIZE{3, 2};
    InflateRect(&rect, -PROXY_INFLATE.cx - offset.cx, -PROXY_INFLATE.cy - offset.cy);
    int iconSize = GetSystemMetrics(SM_CXSMICON);
    rect.left += iconSize;
    MapWindowRect(proxyToolbar, hwnd, &rect);
    return rect;
}

void ItemWindow::limitChainWindowRect(RECT *rect) {
    RECT myRect = windowRect(hwnd);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    GetMonitorInfo(monitor, &monitorInfo);
    LONG maxBottom = max(myRect.bottom, monitorInfo.rcWork.bottom);
    if (rect->bottom > maxBottom)
        rect->bottom = maxBottom;
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
    SIZE size = child->persistSizeInParent() ? requestedChildSize() : child->requestedSize();
    POINT pos = childPos(size);
    RECT rect = {pos.x, pos.y, pos.x + size.cx, pos.y + size.cy};
    if (stickToChild())
        limitChainWindowRect(&rect);
    // will flush message queue
    child->create(rect, SW_SHOWNOACTIVATE);
}

void ItemWindow::closeChild() {
    if (child) {
        child->close();
        child = nullptr; // onChildDetached will not be called
    }
}

void ItemWindow::openParent() {
    CComPtr<IShellItem> parentItem;
    if (FAILED(item->GetParent(&parentItem)))
        return;
    parent = createItemWindow(nullptr, parentItem);
    parent->child = this;

    removeChainPreview();
    SIZE size = parent->requestedSize();
    POINT pos = parentPos(size);
    RECT rect = {pos.x, pos.y, pos.x + size.cx, pos.y + size.cy};
    if (stickToChild())
        limitChainWindowRect(&rect);
    parent->create(rect, SW_SHOWNORMAL);
    if (parentToolbar)
        ShowWindow(parentToolbar, SW_HIDE);

    if (persistSizeInParent())
        parent->onChildResized(rectSize(windowRect(hwnd)));
}

void ItemWindow::clearParent() {
    if (parent && parent->child == this) {
        parent->child = nullptr;
        parent->onChildDetached();
    }
    parent = nullptr;
}

void ItemWindow::detachFromParent(bool closeParent) {
    ItemWindow *rootParent = parent;
    if (!parent->paletteWindow()) {
        HWND prevOwner = checkLE(GetWindowOwner(hwnd));
        HWND owner = createChainOwner(SW_SHOWNORMAL);
        int numChildren = 0;
        for (ItemWindow *next = this; next != nullptr; next = next->child) {
            SetWindowLongPtr(next->hwnd, GWLP_HWNDPARENT, (LONG_PTR)owner);
            numChildren++;
        }
        SetWindowLongPtr(owner, GWLP_USERDATA, (LONG_PTR)numChildren);
        SetWindowLongPtr(prevOwner, GWLP_USERDATA,
            GetWindowLongPtr(prevOwner, GWLP_USERDATA) - numChildren);
        addChainPreview();
    }
    clearParent();

    CComPtr<IShellItem> parentItem;
    if (parentToolbar && SUCCEEDED(item->GetParent(&parentItem)))
        ShowWindow(parentToolbar, SW_SHOW);
    if (closeParent) {
        while (rootParent->parent && !rootParent->parent->paletteWindow())
            rootParent = rootParent->parent;
        if (!rootParent->paletteWindow())
            rootParent->close();
    }
    activate(); // bring this chain to front
}

void ItemWindow::onChildDetached() {}
void ItemWindow::onChildResized(SIZE) {}

void ItemWindow::detachAndMove(bool closeParent) {
    ItemWindow *rootParent = this;
    while (rootParent->parent && !rootParent->parent->paletteWindow())
        rootParent = rootParent->parent;
    RECT rootRect = windowRect(rootParent->hwnd);

    detachFromParent(closeParent);

    if (closeParent) {
        setPos({rootRect.left, rootRect.top});
    } else {
        int ncCaption = CAPTION_HEIGHT + windowBorderSize();
        POINT pos = {rootRect.left + ncCaption, rootRect.top + ncCaption};

        HMONITOR curMonitor = MonitorFromWindow(rootParent->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo = {sizeof(monitorInfo)};
        GetMonitorInfo(curMonitor, &monitorInfo);
        if (pos.x + ncCaption > monitorInfo.rcWork.right)
            pos.x = monitorInfo.rcWork.left;
        if (pos.y + ncCaption > monitorInfo.rcWork.bottom)
            pos.y = monitorInfo.rcWork.top;
        setPos(pos);
    }
}

POINT ItemWindow::childPos(SIZE size) {
    // https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect
    // GetWindowRect includes the resize margin!
    RECT rect = windowRect(hwnd);
    POINT pos = {rect.left + clientSize(hwnd).cx + windowBorderSize() * 2, rect.top};

    HMONITOR curMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    RECT childRect = {pos.x, pos.y, pos.x + size.cx, pos.y + size.cy};
    HMONITOR childMonitor = MonitorFromRect(&childRect, MONITOR_DEFAULTTONEAREST);
    return pointMulDiv(pos, monitorDPI(curMonitor), monitorDPI(childMonitor));
}

POINT ItemWindow::parentPos(SIZE size) {
    RECT rect = windowRect(hwnd);
    LONG margin = screenToClient(hwnd, {rect.left, 0}).x; // determine size of resize margin
    POINT pos = {rect.left - margin * 2 - windowBorderSize() * 2 - size.cx, rect.top};

    HMONITOR curMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    GetMonitorInfo(curMonitor, &monitorInfo);
    if (pos.x < monitorInfo.rcWork.left)
        pos.x = monitorInfo.rcWork.left;
    int ncCaption = CAPTION_HEIGHT + windowBorderSize();
    if (pos.y + ncCaption > monitorInfo.rcWork.bottom)
        pos.y = monitorInfo.rcWork.bottom - ncCaption;
    return pos;
}

void ItemWindow::enableChain(bool enabled) {
    for (ItemWindow *nextWindow = this; nextWindow; nextWindow = nextWindow->parent)
        EnableWindow(nextWindow->hwnd, enabled);
    for (ItemWindow *nextWindow = child; nextWindow; nextWindow = nextWindow->child)
        EnableWindow(nextWindow->hwnd, enabled);
}

void ItemWindow::addChainPreview() {
    HWND owner = checkLE(GetWindowOwner(hwnd));
    CComPtr<ITaskbarList4> taskbar;
    if (checkHR(taskbar.CoCreateInstance(__uuidof(TaskbarList)))) {
        checkHR(taskbar->RegisterTab(hwnd, owner));
        checkHR(taskbar->SetTabOrder(hwnd, nullptr));
        checkHR(taskbar->SetTabProperties(hwnd, STPF_USEAPPPEEKALWAYS));
        isChainPreview = true;
    }
    // update alt-tab
    SetWindowText(owner, title);
    SendMessage(owner, WM_SETICON, ICON_BIG, SendMessage(hwnd, WM_GETICON, ICON_BIG, 0));
    SendMessage(owner, WM_SETICON, ICON_SMALL, SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0));
}

void ItemWindow::removeChainPreview() {
    if (isChainPreview) {
        CComPtr<ITaskbarList4> taskbar;
        if (checkHR(taskbar.CoCreateInstance(__uuidof(TaskbarList)))) {
            checkHR(taskbar->UnregisterTab(hwnd));
            isChainPreview = false;
        }
    }
}

bool ItemWindow::resolveItem() {
    if (closing) // can happen when closing save prompt (window is activated)
        return true;
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
    if (compositionEnabled) {
        // reenable animations to emphasize window closing
        checkHR(DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
            tempPtr((BOOL)FALSE), sizeof(BOOL)));
    }
    close();
    return false;
}

void ItemWindow::onItemChanged() {
    CComHeapPtr<wchar_t> newTitle;
    if (checkHR(item->GetDisplayName(SIGDN_NORMALDISPLAY, &newTitle))) {
        title = newTitle;
        SetWindowText(hwnd, title);
        if (proxyToolbar) {
            TBBUTTONINFO buttonInfo = {sizeof(buttonInfo)};
            buttonInfo.dwMask = TBIF_TEXT;
            buttonInfo.pszText = title;
            SendMessage(proxyToolbar, TB_SETBUTTONINFO, IDM_PROXY_BUTTON, (LPARAM)&buttonInfo);
        }
        if (proxyTooltip) {
            TOOLINFO toolInfo = {sizeof(toolInfo)};
            toolInfo.hwnd = hwnd;
            toolInfo.uId = (UINT_PTR)proxyToolbar;
            toolInfo.lpszText = title;
            SendMessage(proxyTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&toolInfo);
        }
        onSize(clientSize(hwnd));
    }
    if (proxyToolbar) {
        itemDropTarget = nullptr;
        item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&itemDropTarget));
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

void ItemWindow::openParentMenu() {
    int iconSize = GetSystemMetrics(SM_CXSMICON);
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
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
        checkLE(DestroyMenu(menu));
        return;
    }
    POINT point;
    LONG_PTR buttonState = 0;
    if (parentToolbar) {
        RECT buttonRect;
        SendMessage(parentToolbar, TB_GETRECT, IDM_PREV_WINDOW, (LPARAM)&buttonRect);
        point = clientToScreen(parentToolbar, {buttonRect.left, buttonRect.bottom});
        buttonState = SendMessage(parentToolbar, TB_GETSTATE, IDM_PREV_WINDOW, 0);
        SendMessage(parentToolbar, TB_SETSTATE, IDM_PREV_WINDOW, buttonState | TBSTATE_PRESSED);
    } else {
        point = clientToScreen(hwnd, {0, 0});
    }
    int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, hwnd, nullptr);
    if (parentToolbar)
        SendMessage(parentToolbar, TB_SETSTATE, IDM_PREV_WINDOW, buttonState);
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
        if (checkLE(GetMenuItemInfo(menu, i, TRUE, &itemInfo)) && itemInfo.hbmpItem)
            DeleteBitmap(itemInfo.hbmpItem);
    }
    checkLE(DestroyMenu(menu));
}

void ItemWindow::invokeProxyDefaultVerb() {
    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        SHELLEXECUTEINFO info = {sizeof(info)};
        info.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_LOG_USAGE;
        info.lpIDList = idList;
        info.hwnd = hwnd;
        info.nShow = SW_SHOWNORMAL;
        checkLE(ShellExecuteEx(&info));
    }
}

void ItemWindow::openProxyProperties() {
    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        SHELLEXECUTEINFO info = {sizeof(info)};
        info.fMask = SEE_MASK_INVOKEIDLIST;
        info.lpVerb = L"properties";
        info.lpIDList = idList;
        info.hwnd = hwnd;
        checkLE(ShellExecuteEx(&info));
    }
}

void ItemWindow::deleteProxy(bool resolve) {
    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        SHELLEXECUTEINFO info = {sizeof(info)};
        info.fMask = SEE_MASK_INVOKEIDLIST;
        info.lpVerb = L"delete";
        info.lpIDList = idList;
        info.hwnd = hwnd;
        checkLE(ShellExecuteEx(&info));
        // TODO: remove this once there's an automatic system for tracking files
        if (resolve)
            resolveItem();
    }
}

void ItemWindow::openProxyContextMenu() {
    // https://devblogs.microsoft.com/oldnewthing/20040920-00/?p=37823 and onward
    CComPtr<IContextMenu> contextMenu;
    if (!checkHR(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&contextMenu))))
        return;
    HMENU popupMenu = CreatePopupMenu();
    if (!popupMenu)
        return;
    UINT contextFlags = CMF_ITEMMENU | (renameBox ? CMF_CANRENAME : 0);
    if (GetKeyState(VK_SHIFT) < 0)
        contextFlags |= CMF_EXTENDEDVERBS;
    if (!checkHR(contextMenu->QueryContextMenu(popupMenu, 0, IDM_SHELL_FIRST, IDM_SHELL_LAST,
            contextFlags))) {
        checkLE(DestroyMenu(popupMenu));
        return;
    }
    POINT point;
    if (proxyToolbar) {
        RECT buttonRect;
        SendMessage(proxyToolbar, TB_GETRECT, IDM_PROXY_BUTTON, (LPARAM)&buttonRect);
        point = clientToScreen(proxyToolbar, {buttonRect.left, buttonRect.bottom});
    } else {
        point = clientToScreen(hwnd, {0, 0});
    }
    contextMenu2 = contextMenu;
    contextMenu3 = contextMenu;
    int cmd = TrackPopupMenuEx(popupMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, hwnd, nullptr);
    contextMenu2 = nullptr;
    contextMenu3 = nullptr;
    if (cmd >= IDM_SHELL_FIRST && cmd <= IDM_SHELL_LAST) {
        cmd -= IDM_SHELL_FIRST;
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
    checkLE(DestroyMenu(popupMenu));
}

void ItemWindow::openProxyContextMenuFeedback() {
    LONG_PTR state = 0;
    if (proxyToolbar) {
        state = SendMessage(proxyToolbar, TB_GETSTATE, IDM_PROXY_BUTTON, 0);
        SendMessage(proxyToolbar, TB_SETSTATE, IDM_PROXY_BUTTON, state | TBSTATE_PRESSED);
    }
    openProxyContextMenu();
    if (proxyToolbar)
        SendMessage(proxyToolbar, TB_SETSTATE, IDM_PROXY_BUTTON, state);
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
            dragHelper->InitializeFromWindow(proxyToolbar, &offset, dataObject);
        }
    }

    DWORD okEffects = DROPEFFECT_COPY | DROPEFFECT_LINK | DROPEFFECT_MOVE;
    DWORD effect;
    draggingObject = true;
    // effect is supposed to be set to DROPEFFECT_MOVE if the target was unable to delete the
    // original, however the only time I could trigger this was moving a file into a ZIP folder,
    // which does successfully delete the original, only with a delay. So handling this as intended
    // would actually break dragging into ZIP folders and cause loss of data!
    if (DoDragDrop(dataObject, this, okEffects, &effect) == DRAGDROP_S_DROP) {
        // TODO: remove this once there's an automatic system for tracking files
        resolveItem();
    }
    draggingObject = false;
}

void ItemWindow::beginRename() {
    if (!renameBox)
        return;
    // update rename box rect
    int leftMargin = LOWORD(SendMessage(renameBox, EM_GETMARGINS, 0, 0));
    RECT textRect = titleRect();
    int renameHeight = rectHeight(textRect) + 4; // NOT scaled with DPI
    POINT renamePos = {textRect.left - leftMargin - 2,
                       (CAPTION_HEIGHT - renameHeight) / 2};
    TITLEBARINFOEX titleBar = {sizeof(titleBar)};
    SendMessage(hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&titleBar);
    int renameWidth = clientSize(hwnd).cx - rectWidth(titleBar.rgrect[5]) - renamePos.x;
    renamePos = clientToScreen(hwnd, renamePos);
    MoveWindow(renameBox, renamePos.x, renamePos.y, renameWidth, renameHeight, FALSE);

    SendMessage(renameBox, WM_SETTEXT, 0, (LPARAM)&*title);
    wchar_t *ext = PathFindExtension(title);
    if (ext == title) { // files that start with a dot
        Edit_SetSel(renameBox, 0, -1);
    } else {
        Edit_SetSel(renameBox, 0, ext - title);
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
            if (!checkHR(StringCchCat(newName, _countof(newName), fileName + titleLen)))
                return;
        }
    }

    CComPtr<IFileOperation> operation;
    if (!checkHR(operation.CoCreateInstance(__uuidof(FileOperation))))
        return;
    // TODO: FOFX_ADDUNDORECORD requires Windows 8
    checkHR(operation->SetOperationFlags(
        IsWindows8OrGreater() ? FOFX_ADDUNDORECORD : FOF_ALLOWUNDO));
    if (!checkHR(operation->RenameItem(item, newName, nullptr)))
        return;
    checkHR(operation->PerformOperations());

    // TODO: remove this once there's an automatic system for tracking files
    resolveItem();
}

void ItemWindow::cancelRename() {
    ShowWindow(renameBox, SW_HIDE);
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

DWORD getDropEffect(DWORD keyState) {
    bool ctrl = (keyState & MK_CONTROL), shift = (keyState & MK_SHIFT), alt = (keyState & MK_ALT);
    if ((ctrl && shift && !alt) || (!ctrl && !shift && alt))
        return DROPEFFECT_LINK;
    else if (ctrl && !shift && !alt)
        return DROPEFFECT_COPY;
    else
        return DROPEFFECT_MOVE;
}

STDMETHODIMP ItemWindow::DragEnter(IDataObject *dataObject, DWORD keyState, POINTL pt,
        DWORD *effect) {
    if (draggingObject) {
        *effect = getDropEffect(keyState); // pretend we can drop so the drag image is visible
    } else {
        if (!itemDropTarget
                || !checkHR(itemDropTarget->DragEnter(dataObject, keyState, pt, effect)))
            return E_FAIL;
    }
    POINT point {pt.x, pt.y};
    if (dropTargetHelper)
        checkHR(dropTargetHelper->DragEnter(hwnd, dataObject, &point, *effect));
    return S_OK;
}

STDMETHODIMP ItemWindow::DragLeave() {
    if (!draggingObject) {
        if (!itemDropTarget || !checkHR(itemDropTarget->DragLeave()))
            return E_FAIL;
    }
    if (dropTargetHelper)
        checkHR(dropTargetHelper->DragLeave());
    return S_OK;
}

STDMETHODIMP ItemWindow::DragOver(DWORD keyState, POINTL pt, DWORD *effect) {
    if (draggingObject) {
        *effect = getDropEffect(keyState);
    } else {
        if (!itemDropTarget || !checkHR(itemDropTarget->DragOver(keyState, pt, effect)))
            return E_FAIL;
    }
    POINT point {pt.x, pt.y};
    if (dropTargetHelper)
        checkHR(dropTargetHelper->DragOver(&point, *effect));
    return S_OK;
}

STDMETHODIMP ItemWindow::Drop(IDataObject *dataObject, DWORD keyState, POINTL pt, DWORD *effect) {
    if (draggingObject) {
        *effect = DROPEFFECT_NONE; // can't drop item onto itself
    } else {
        if (!itemDropTarget || !checkHR(itemDropTarget->Drop(dataObject, keyState, pt, effect)))
            return E_FAIL;
    }
    POINT point {pt.x, pt.y};
    if (dropTargetHelper)
        checkHR(dropTargetHelper->Drop(dataObject, &point, *effect));
    return S_OK;
}


BOOL CALLBACK ItemWindow::enumCloseChain(HWND hwnd, LPARAM lParam) {
    if (GetWindowOwner(hwnd) == (HWND)lParam) {
        ItemWindow *itemWindow = (ItemWindow *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (itemWindow && itemWindow->paletteWindow())
            itemWindow = itemWindow->child;
        if (itemWindow) {
            while (itemWindow->parent && !itemWindow->parent->paletteWindow())
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
        checkLE(EnumWindows(enumCloseChain, (LPARAM)hwnd));
        return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
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
    if (!checkHR(queryInfo->GetInfoTip(QITIPF_USESLOWTIP, &text)) || !text)
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
