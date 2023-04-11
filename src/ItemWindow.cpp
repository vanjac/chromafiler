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

// dimensions
static int PARENT_BUTTON_WIDTH = 34; // matches close button width in windows 10
static int PARENT_BUTTON_MARGIN = 1;
static int WINDOW_ICON_PADDING = 4;
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
// this is the color used in every high-contrast theme
// regular light mode theme uses #999999
const COLORREF WIN10_INACTIVE_CAPTION_COLOR = 0x636363;

static HANDLE symbolFontHandle = nullptr;
static HFONT captionFont = nullptr, statusFont = nullptr, symbolFont = nullptr;

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

    RECT adjustedRect = {};
    AdjustWindowRectEx(&adjustedRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    CAPTION_HEIGHT = -adjustedRect.top; // = 31

    PARENT_BUTTON_WIDTH = scaleDPI(PARENT_BUTTON_WIDTH);
    PARENT_BUTTON_MARGIN = scaleDPI(PARENT_BUTTON_MARGIN);
    WINDOW_ICON_PADDING = scaleDPI(WINDOW_ICON_PADDING);
    TOOLBAR_HEIGHT = scaleDPI(TOOLBAR_HEIGHT);
    STATUS_TEXT_MARGIN = scaleDPI(STATUS_TEXT_MARGIN);
    STATUS_TOOLTIP_OFFSET = scaleDPI(STATUS_TOOLTIP_OFFSET);
    DETACH_DISTANCE = scaleDPI(DETACH_DISTANCE);
    WIN10_CXSIZEFRAME = scaleDPI(WIN10_CXSIZEFRAME);
    SYMBOL_LOGFONT.lfHeight = scaleDPI(SYMBOL_LOGFONT.lfHeight);

    checkHR(DwmIsCompositionEnabled(&compositionEnabled));

    // TODO: alternatively use SystemParametersInfo with SPI_GETNONCLIENTMETRICS
    if (HTHEME theme = OpenThemeData(nullptr, WINDOW_THEME)) {
        LOGFONT logFont;
        if (checkHR(GetThemeSysFont(theme, TMT_CAPTIONFONT, &logFont)))
            captionFont = CreateFontIndirect(&logFont);
        if (checkHR(GetThemeSysFont(theme, TMT_STATUSFONT, &logFont)))
            statusFont = CreateFontIndirect(&logFont);
        checkHR(CloseThemeData(theme));
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
    wndClass.style = CS_HREDRAW; // ensure caption gets redrawn if width changes
    wndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // for toolbar
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
    return !!compositionEnabled;
}

bool ItemWindow::allowToolbar() const {
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

SettingsPage ItemWindow::settingsStartPage() const {
    return SETTINGS_GENERAL;
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
    if (statusText || toolbar)
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
            checkLE(DestroyIcon((HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0)));
            checkLE(DestroyIcon((HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0)));
            hwnd = nullptr;
            windowClosed();
            Release(); // allow window to be deleted
            return 0;
        case WM_ACTIVATE:
            onActivate(LOWORD(wParam), (HWND)lParam);
            return 0;
        case WM_NCACTIVATE:
            if (useCustomFrame()) {
                RECT captionRect = {0, 0, clientSize(hwnd).cx, CAPTION_HEIGHT};
                InvalidateRect(hwnd, &captionRect, FALSE);
            }
            break; // pass to DefWindowProc
        case WM_NCCALCSIZE:
            if (wParam == TRUE && useCustomFrame()) {
                // allow resizing past the edge of the window by reducing client rect
                // TODO: revisit this
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
            return hitTestNCA(pointFromLParam(lParam));
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
        case WM_NCLBUTTONDOWN: {
            POINT cursor = pointFromLParam(lParam);
            POINT clientCursor = screenToClient(hwnd, cursor);
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
            POINT cursor = pointFromLParam(lParam);
            if (wParam == HTCAPTION && PtInRect(&proxyRect, screenToClient(hwnd, cursor))) {
                if (GetKeyState(VK_MENU) < 0)
                    openProxyProperties();
                else
                    invokeProxyDefaultVerb();
                return 0;
            }
            break;
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
                if (PtInRect(&proxyRect, clientCursor)) {
                    openProxyContextMenu(cursor);
                } else if (PtInRect(tempPtr(windowBody()), clientCursor)) {
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

    HMODULE instance = GetWindowInstance(hwnd);
    if (useCustomFrame()) {
        checkHR(DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
            tempPtr((BOOL)TRUE), sizeof(BOOL))); // disable animations
        MARGINS margins;
        margins.cxLeftWidth = 0;
        margins.cxRightWidth = 0;
        margins.cyTopHeight = CAPTION_HEIGHT;
        margins.cyBottomHeight = 0;
        checkHR(DwmExtendFrameIntoClientArea(hwnd, &margins));

        // will succeed for folders and EXEs, and fail for regular files
        if (SUCCEEDED(item->BindToHandler(nullptr, BHID_SFUIObject,
                IID_PPV_ARGS(&itemDropTarget)))) {
            checkHR(dropTargetHelper.CoCreateInstance(CLSID_DragDropHelper));
            checkHR(RegisterDragDrop(hwnd, this));
        }

        proxyTooltip = checkLE(CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, nullptr, instance, nullptr));
        SetWindowPos(proxyTooltip, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (captionFont)
            SendMessage(proxyTooltip, WM_SETFONT, (WPARAM)captionFont, FALSE);
        TOOLINFO toolInfo = {sizeof(toolInfo)};
        toolInfo.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
        toolInfo.hwnd = hwnd;
        toolInfo.lpszText = title;
        SendMessage(proxyTooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);

        CComPtr<IShellItem> parentItem;
        bool showParentButton = !parent && SUCCEEDED(item->GetParent(&parentItem));
        parentButton = checkLE(CreateWindow(L"BUTTON", nullptr,
            (showParentButton ? WS_VISIBLE : 0) | WS_CHILD | BS_PUSHBUTTON,
            0, PARENT_BUTTON_MARGIN, PARENT_BUTTON_WIDTH, CAPTION_HEIGHT - PARENT_BUTTON_MARGIN * 2,
            hwnd, (HMENU)IDM_PREV_WINDOW, instance, nullptr));
        SetWindowSubclass(parentButton, parentButtonProc, 0, (DWORD_PTR)this);

        // will be positioned in beginRename
        renameBox = checkLE(CreateWindow(L"EDIT", nullptr,
            WS_POPUP | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 0, 0,
            hwnd, nullptr, instance, nullptr));
        SetWindowSubclass(renameBox, renameBoxProc, 0, (DWORD_PTR)this);
        if (captionFont)
            SendMessage(renameBox, WM_SETFONT, (WPARAM)captionFont, FALSE);
    } // if (useCustomFrame())

    if (allowToolbar() && settings::getStatusTextEnabled()) {
        statusText = checkLE(CreateWindow(L"STATIC", nullptr,
            WS_VISIBLE | WS_CHILD | SS_WORDELLIPSIS | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX
                | SS_NOTIFY, // allows tooltips to work
            STATUS_TEXT_MARGIN, useCustomFrame() ? CAPTION_HEIGHT : 0, 0, TOOLBAR_HEIGHT,
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

    if (allowToolbar() && settings::getToolbarEnabled()) {
        toolbar = checkLE(CreateWindowEx(
            TBSTYLE_EX_MIXEDBUTTONS, TOOLBARCLASSNAME, nullptr,
            TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER
                | WS_VISIBLE | WS_CHILD,
            0, useCustomFrame() ? CAPTION_HEIGHT : 0, 0, TOOLBAR_HEIGHT,
            hwnd, nullptr, instance, nullptr));
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
    if (toolbar)
        SendMessage(toolbar, TB_SETSTATE, command, state);
}

void ItemWindow::addToolbarButtons(HWND tb) {
    TBBUTTON buttons[] = {
        makeToolbarButton(MDL2_MORE, IDM_CONTEXT_MENU, BTNS_WHOLEDROPDOWN),
    };
    SendMessage(tb, TB_ADDBUTTONS, _countof(buttons), (LPARAM)buttons);
}

int ItemWindow::getToolbarTooltip(WORD command) {
    switch (command) {
        case IDM_SETTINGS:
            return IDS_SETTINGS_COMMAND;
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

    if (itemDropTarget)
        checkHR(RevokeDragDrop(hwnd));

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
            POINT menuPos = useCustomFrame() ? POINT{proxyRect.right, proxyRect.top} : POINT{0, 0};
            openProxyContextMenu(clientToScreen(hwnd, menuPos));
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
            if (!rootParent->paletteWindow()) {
                POINT menuPos = {0, rootParent->useCustomFrame() ? CAPTION_HEIGHT : 0};
                rootParent->openParentMenu(clientToScreen(rootParent->hwnd, menuPos));
            }
            return true;
        }
        case IDM_HELP:
            ShellExecute(nullptr, L"open", L"https://github.com/vanjac/chromafiler/wiki",
                nullptr, nullptr, SW_SHOWNORMAL);
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
    }
    return false;
}

LRESULT ItemWindow::onNotify(NMHDR *nmHdr) {
    if (proxyTooltip && nmHdr->hwndFrom == proxyTooltip && nmHdr->code == TTN_SHOW) {
        // position tooltip on top of title
        RECT tooltipRect = titleRect;
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

    int toolbarLeft = size.cx;
    if (toolbar) {
        toolbarLeft = size.cx - clientSize(toolbar).cx;
        SetWindowPos(toolbar, nullptr, toolbarLeft, useCustomFrame() ? CAPTION_HEIGHT : 0, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (statusText) {
        SetWindowPos(statusText, nullptr,
            0, 0, toolbarLeft - STATUS_TEXT_MARGIN * 2, TOOLBAR_HEIGHT,
            SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
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

    HDC hdcPaint = CreateCompatibleDC(paint.hdc);
    if (!hdcPaint)
        return;

    int width = clientSize(hwnd).cx;
    int height = CAPTION_HEIGHT;

    // bitmap buffer for drawing caption
    // top-to-bottom order for DrawThemeTextEx()
    BITMAPINFO bitmapInfo = {{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB}};
    HBITMAP bitmap = checkLE(CreateDIBSection(paint.hdc, &bitmapInfo, DIB_RGB_COLORS,
                                              nullptr, nullptr, 0));
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

    if (proxyTooltip) {
        if (truncateTitle) {
            TOOLINFO toolInfo = {sizeof(toolInfo)};
            toolInfo.hwnd = hwnd;
            toolInfo.rect = proxyRect; // rect to trigger tooltip
            SendMessage(proxyTooltip, TTM_NEWTOOLRECT, 0, (LPARAM)&toolInfo);
            SendMessage(proxyTooltip, TTM_ACTIVATE, TRUE, 0);
        } else {
            SendMessage(proxyTooltip, TTM_ACTIVATE, FALSE, 0);
        }
    }

    iconRect.left = headerLeft;
    iconRect.top = (CAPTION_HEIGHT - iconSize) / 2;
    iconRect.right = iconRect.left + iconSize;
    iconRect.bottom = iconRect.top + iconSize;
    HICON icon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (icon) {
        checkLE(DrawIconEx(hdcPaint, iconRect.left, iconRect.top, icon,
                           iconSize, iconSize, 0, nullptr, DI_NORMAL));
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
        titleRect.right = width - closeButtonWidth; // close button width
        titleRect.bottom = titleRect.top + titleSize.cy;
        checkHR(DrawThemeTextEx(windowTheme, hdcPaint, 0, 0, title, -1,
                                DT_LEFT | DT_WORD_ELLIPSIS | DT_NOPREFIX, &titleRect, &textOpts));

        checkHR(CloseThemeData(windowTheme));
    }

    checkLE(BitBlt(paint.hdc, 0, 0, width, height, hdcPaint, 0, 0, SRCCOPY));

    if (oldFont)
        SelectFont(hdcPaint, oldFont);
    SelectBitmap(hdcPaint, oldBitmap);
    DeleteBitmap(bitmap);
    DeleteDC(hdcPaint);
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
    if (parentButton)
        ShowWindow(parentButton, SW_HIDE);

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
    if (parentButton && SUCCEEDED(item->GetParent(&parentItem)))
        ShowWindow(parentButton, SW_SHOW);
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
    if (pos.y + CAPTION_HEIGHT > monitorInfo.rcWork.bottom)
        pos.y = monitorInfo.rcWork.bottom - CAPTION_HEIGHT;
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
    if (useCustomFrame()) {
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
        if (proxyTooltip) {
            TOOLINFO toolInfo = {sizeof(toolInfo)};
            toolInfo.hwnd = hwnd;
            toolInfo.lpszText = title;
            SendMessage(proxyTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&toolInfo);
        }
    }
    if (useCustomFrame()) {
        // redraw caption
        InvalidateRect(hwnd, tempPtr(RECT{0, 0, clientSize(hwnd).cx, CAPTION_HEIGHT}), FALSE);

        if (itemDropTarget) {
            itemDropTarget = nullptr;
            if (!checkHR(item->BindToHandler(nullptr, BHID_SFUIObject,
                    IID_PPV_ARGS(&itemDropTarget))))
                checkHR(RevokeDragDrop(hwnd));
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

void ItemWindow::openProxyContextMenu(POINT point) {
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
            checkLE(GetIconInfo(icon, &iconInfo));
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
    // effect is supposed to be set to DROPEFFECT_MOVE if the target was unable to delete the
    // original, however the only time I could trigger this was moving a file into a ZIP folder,
    // which does successfully delete the original, only with a delay. So handling this as intended
    // would actually break dragging into ZIP folders and cause loss of data!
    if (DoDragDrop(dataObject, this, okEffects, &effect) == DRAGDROP_S_DROP) {
        // TODO: remove this once there's an automatic system for tracking files
        resolveItem();
    }
}

void ItemWindow::beginRename() {
    if (!renameBox)
        return;
    // update rename box rect
    int leftMargin = LOWORD(SendMessage(renameBox, EM_GETMARGINS, 0, 0));
    int renameHeight = rectHeight(titleRect) + 4; // NOT scaled with DPI
    POINT renamePos = {titleRect.left - leftMargin - 2,
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
    bool nowOverTarget = PtInRect(&proxyRect, screenToClient(hwnd, point));
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
    if (!PtInRect(&proxyRect, screenToClient(hwnd, point))) {
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
            RECT buttonRect = clientRect(hwnd);
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
        ((ItemWindow *)refData)->openParentMenu(clientToScreen(hwnd, pointFromLParam(lParam)));
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
