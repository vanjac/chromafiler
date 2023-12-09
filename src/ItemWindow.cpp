#include "ItemWindow.h"
#include "CreateItemWindow.h"
#include "main.h"
#include "GeomUtils.h"
#include "GDIUtils.h"
#include "WinUtils.h"
#include "ShellUtils.h"
#include "Settings.h"
#include "DPI.h"
#include "UIStrings.h"
#include "Update.h"
#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <vssym32.h>
#include <shellapi.h>
#include <propkey.h>
#include <Propvarutil.h>
#include <VersionHelpers.h>

namespace chromafiler {

const wchar_t TESTPOS_CLASS[] = L"ChromaFiler Test Window";
const wchar_t WINDOW_THEME[] = L"CompositedWindow::Window";
const UINT SC_DRAGMOVE = SC_MOVE | 2; // https://stackoverflow.com/a/35880547/11525734

// property bag
const wchar_t PROP_POS[] = L"Pos";
const wchar_t PROP_SIZE[] = L"Size";
const wchar_t PROP_CHILD_SIZE[] = L"ChildSize";

// dimensions
static int PARENT_BUTTON_WIDTH = 34; // caption only, matches close button width in windows 10
static int COMP_CAPTION_VMARGIN = 1;
static int TOOLBAR_HEIGHT = 24;
static int STATUS_TEXT_MARGIN = 4;
static int STATUS_TOOLTIP_OFFSET = 2; // TODO not correct at higher DPIs
static int DETACH_DISTANCE = 32;

int ItemWindow::CAPTION_HEIGHT = 0; // calculated in init()

static LOGFONT SYMBOL_LOGFONT = {14, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
    ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets"};

// these are Windows metrics/colors that are not exposed through the API >:(
static int WIN10_CXSIZEFRAME = 8; // TODO not correct at higher DPIs

static local_wstr_ptr iconResource;
static HANDLE symbolFontHandle = nullptr;
static HFONT statusFont = nullptr;
static HFONT symbolFont = nullptr;
static HCURSOR rightSideCursor = nullptr;

static BOOL compositionEnabled = FALSE;

HACCEL ItemWindow::accelTable;

static bool highContrastEnabled() {
    HIGHCONTRAST highContrast = {sizeof(highContrast)};
    checkLE(SystemParametersInfo(SPI_GETHIGHCONTRAST, 0, &highContrast, 0));
    return highContrast.dwFlags & HCF_HIGHCONTRASTON;
}

static int windowResizeMargin() {
    return IsThemeActive() ? WIN10_CXSIZEFRAME : GetSystemMetrics(SM_CXSIZEFRAME);
}

static int captionTopMargin() {
    return compositionEnabled ? COMP_CAPTION_VMARGIN : 0;
}

static bool invisibleBorders() {
    return compositionEnabled && IsWindows10OrGreater() && !highContrastEnabled();
}

static int invisibleBorderDoubleSize(HWND hwnd, int dpi) {
    if (invisibleBorders()) {
        // https://stackoverflow.com/q/34139450/11525734
        RECT wndRect = windowRect(hwnd);
        RECT frame;
        if (checkHR(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                &frame, sizeof(frame))))
            return (MulDiv(frame.left, systemDPI, dpi) - wndRect.left)
                + (wndRect.right - MulDiv(frame.right, systemDPI, dpi)) + 2;
    }
    return 0;
}

int ItemWindow::cascadeSize() {
    return CAPTION_HEIGHT + (invisibleBorders() ? 0 : windowResizeMargin());
}

void ItemWindow::init() {
    ChainWindow::init();
    ProxyIcon::init();

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASS testPosClass = {};
    testPosClass.lpszClassName = TESTPOS_CLASS;
    testPosClass.lpfnWndProc = DefWindowProc;
    testPosClass.hInstance = hInstance;
    RegisterClass(&testPosClass);

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
    TOOLBAR_HEIGHT = scaleDPI(TOOLBAR_HEIGHT);
    STATUS_TEXT_MARGIN = scaleDPI(STATUS_TEXT_MARGIN);
    STATUS_TOOLTIP_OFFSET = scaleDPI(STATUS_TOOLTIP_OFFSET);
    DETACH_DISTANCE = scaleDPI(DETACH_DISTANCE);
    WIN10_CXSIZEFRAME = scaleDPI(WIN10_CXSIZEFRAME);
    SYMBOL_LOGFONT.lfHeight = scaleDPI(SYMBOL_LOGFONT.lfHeight);

    if (HTHEME theme = OpenThemeData(nullptr, WINDOW_THEME)) {
        LOGFONT logFont;
        if (checkHR(GetThemeSysFont(theme, TMT_STATUSFONT, &logFont)))
            statusFont = CreateFontIndirect(&logFont);
        ProxyIcon::initTheme(theme);
        checkHR(CloseThemeData(theme));
    } else {
        NONCLIENTMETRICS metrics = {sizeof(metrics)};
        SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        statusFont = CreateFontIndirect(&metrics.lfStatusFont);
        ProxyIcon::initMetrics(metrics);
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

    rightSideCursor = LoadCursor(hInstance, MAKEINTRESOURCE(IDC_RIGHT_SIDE));

    accelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ITEM_ACCEL));

    wchar_t exePath[MAX_PATH];
    if (checkLE(GetModuleFileName(nullptr, exePath, _countof(exePath)))) {
        iconResource = format(L"%1,-" XSTRINGIFY(IDR_APP_ICON), exePath);
    }
}

void ItemWindow::uninit() {
    ProxyIcon::uninit();
    if (symbolFont)
        DeleteFont(symbolFont);
    if (symbolFontHandle)
        RemoveFontMemResourceEx(symbolFontHandle);
}

WNDCLASS ItemWindow::createWindowClass(const wchar_t *name) {
    WNDCLASS wndClass = {};
    wndClass.lpfnWndProc = windowProc;
    wndClass.hInstance = GetModuleHandle(nullptr);
    wndClass.lpszClassName = name;
    if (!compositionEnabled)
        wndClass.style = CS_HREDRAW; // redraw caption when resizing
    // change toolbar color
    if (IsWindows10OrGreater())
        wndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    return wndClass;
}

void ItemWindow::flashWindow(HWND hwnd) {
    PostMessage(hwnd, MSG_FLASH_WINDOW, 0, 0);
}

ItemWindow::ItemWindow(ItemWindow *const parent, IShellItem *const item)
        : parent(parent),
          item(item),
          proxyIcon(this) {}

void ItemWindow::setScratch(bool value) {
    scratch = value;
}

bool ItemWindow::persistSizeInParent() const {
    return true;
}

SIZE ItemWindow::defaultSize() const {
    return scaleDPI(settings::getItemWindowSize());
}

const wchar_t * ItemWindow::propBagName() const {
#ifdef CHROMAFILER_DEBUG
    return settings::testMode ? L"chromafiler_test" : L"chromafiler";
#else
    return L"chromafiler";
#endif
}

const wchar_t * ItemWindow::appUserModelID() const {
    return APP_ID;
}

bool ItemWindow::isFolder() const {
    return false;
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

void ItemWindow::updateWindowPropStore(IPropertyStore *const propStore) {
    // https://learn.microsoft.com/en-us/windows/win32/shell/appids
    PROPVARIANT empty = {VT_EMPTY};
    propStore->SetValue(PKEY_AppUserModel_ID, empty); // use process explicit
    propStore->SetValue(PKEY_AppUserModel_RelaunchCommand, empty);
    propStore->SetValue(PKEY_AppUserModel_RelaunchDisplayNameResource, empty);
    if (iconResource)
        propStoreWriteString(propStore, PKEY_AppUserModel_RelaunchIconResource, iconResource.get());
    else
        propStore->SetValue(PKEY_AppUserModel_RelaunchIconResource, empty);
}

void ItemWindow::propStoreWriteString(IPropertyStore *const propStore,
        const PROPERTYKEY &key, const wchar_t *value) {
    PROPVARIANT propVar;
    if (checkHR(InitPropVariantFromString(value, &propVar))) {
        checkHR(propStore->SetValue(key, propVar));
        checkHR(PropVariantClear(&propVar));
    }
}

CComPtr<IPropertyBag> ItemWindow::getPropBag() {
    // local property bags can be found at:
    // HKEY_CURRENT_USER\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell\Bags
    if (!propBag) {
        CComHeapPtr<ITEMIDLIST> idList;
        if (checkHR(SHGetIDListFromObject(item, &idList))) {
            checkHR(SHGetViewStatePropertyBag(idList, propBagName(), SHGVSPB_FOLDERNODEFAULTS,
                IID_PPV_ARGS(&propBag)));
        }
    }
    return propBag;
}

void ItemWindow::resetViewState(uint32_t mask) {
    if (auto bag = getPropBag())
        clearViewState(bag, mask);
}

void ItemWindow::resetViewState() {
    resetViewState(~0u);
}

void ItemWindow::persistViewState() {
    if (dirtyViewState) {
        debugPrintf(L"Persist view state %x\n", dirtyViewState);
        if (auto bag = getPropBag())
            writeViewState(bag, dirtyViewState);
    }
}

void ItemWindow::clearViewState(IPropertyBag *const bag, uint32_t mask) {
    viewStateClean(mask);

    CComVariant empty;
    if (mask & (1 << STATE_POS))
        checkHR(bag->Write(PROP_POS, &empty));
    if (mask & (1 << STATE_SIZE))
        checkHR(bag->Write(PROP_SIZE, &empty));
    if (mask & (1 << STATE_CHILD_SIZE))
        checkHR(bag->Write(PROP_CHILD_SIZE, &empty));
}

void ItemWindow::writeViewState(IPropertyBag *const bag, uint32_t mask) {
    viewStateClean(mask);

    RECT rect = windowRect(hwnd);
    if ((mask & (1 << STATE_POS)) && !parent) {
        // unlike the tray we don't store the DPI along with unscaled positions
        // because we don't need to be pixel-perfect
        CComVariant posVar((unsigned long)MAKELONG(invScaleDPI(rect.left), invScaleDPI(rect.top)));
        checkHR(bag->Write(PROP_POS, &posVar));
    }
    if (mask & (1 << STATE_SIZE)) {
        SIZE size = rectSize(rect);
        CComVariant sizeVar((unsigned long)MAKELONG(invScaleDPI(size.cx), invScaleDPI(size.cy)));
        checkHR(bag->Write(PROP_SIZE, &sizeVar));
    }
    if ((mask & (1 << STATE_CHILD_SIZE)) && !sizeEqual(childSize, {0, 0})) {
        CComVariant sizeVar((unsigned long)MAKELONG(
            invScaleDPI(childSize.cx), invScaleDPI(childSize.cy)));
        checkHR(bag->Write(PROP_CHILD_SIZE, &sizeVar));
    }
}

void ItemWindow::viewStateDirty(uint32_t mask) {
    dirtyViewState |= mask;
}

void ItemWindow::viewStateClean(uint32_t mask) {
    dirtyViewState &= ~mask;
}

bool ItemWindow::isScratch() {
    return scratch;
}

void ItemWindow::onModify() {
    if (scratch)
        SHAddToRecentDocs(SHARD_APPIDINFO, tempPtr(SHARDAPPIDINFO{item, appUserModelID()}));
    scratch = false;
}

bool ItemWindow::create(RECT rect, int showCommand) {
    if (!checkHR(item->GetDisplayName(SIGDN_NORMALDISPLAY, &title)))
        return false;
    debugPrintf(L"Open %s\n", &*title);

    if (parent && !parent->paletteWindow()) {
        chain = parent->chain;
    } else if (child) {
        chain = child->chain;
        chain->setLeft(this);
    } else {
        chain.Attach(new ChainWindow(this, windowStyle() & WS_POPUP, showCommand));
    }

    HWND createHwnd = checkLE(CreateWindowEx(
        windowExStyle(), className(), title, windowStyle(),
        rect.left, rect.top, rectWidth(rect), rectHeight(rect),
        chain->getWnd(), nullptr, GetModuleHandle(nullptr), (WindowImpl *)this));
    if (!createHwnd)
        return false;

    // https://docs.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-itaskbarlist2-markfullscreenwindow#remarks
    if (windowExStyle() & WS_EX_TOPMOST) {
        checkLE(SetProp(hwnd, L"NonRudeHWND", (HANDLE)TRUE));
        checkLE(SetProp(chain->getWnd(), L"NonRudeHWND", (HANDLE)TRUE));
    }

    if (parent || child)
        enableTransitions(false); // disable open/close animation
    ShowWindow(createHwnd, showCommand);
    if (!(parent || child))
        enableTransitions(false); // disable close animation

    AddRef(); // keep window alive while open
    lockProcess();
    return true;
}

void ItemWindow::close() {
    // TODO: remove this method and use Post/Send directly
    PostMessage(hwnd, WM_CLOSE, 0, 0);
}

void ItemWindow::activate() {
    SetActiveWindow(hwnd);
}

void ItemWindow::setForeground() {
    SetForegroundWindow(hwnd);
}

void ItemWindow::setRect(RECT rect) {
    SetWindowPos(hwnd, nullptr, rect.left, rect.top, rectWidth(rect), rectHeight(rect),
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void ItemWindow::setPos(POINT pos) {
    SetWindowPos(hwnd, nullptr, pos.x, pos.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ItemWindow::setSize(SIZE size) {
    SetWindowPos(hwnd, nullptr, 0, 0, size.cx, size.cy, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

void ItemWindow::move(int x, int y) {
    RECT rect = windowRect(hwnd);
    setPos({rect.left + x, rect.top + y});
}

void ItemWindow::adjustSize(int *x, int *y) {
    SIZE size = rectSize(windowRect(hwnd));
    setSize({size.cx + *x, size.cy + *y});
    SIZE newSize = rectSize(windowRect(hwnd));
    *x = newSize.cx - size.cx;
    *y = newSize.cy - size.cy;
}

RECT ItemWindow::windowBody() {
    RECT rect = clientRect(hwnd);
    if (useCustomFrame())
        rect.top += CAPTION_HEIGHT;
    if (statusText || cmdToolbar)
        rect.top += TOOLBAR_HEIGHT;
    return rect;
}

void ItemWindow::fakeDragMove() {
    // https://stackoverflow.com/a/35880547/11525734
    SendMessage(hwnd, WM_SYSCOMMAND, SC_DRAGMOVE, 0);
}

void ItemWindow::enableTransitions(bool enabled) {
    if (compositionEnabled) {
        checkHR(DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
            tempPtr((BOOL)!enabled), sizeof(BOOL)));
    }
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
            proxyIcon.destroy();
            // don't need icon lock since icon thread is stopped
            if (iconLarge) {
                checkLE(DestroyIcon(iconLarge));
                CHROMAFILER_MEMLEAK_FREE;
            }
            if (iconSmall) {
                checkLE(DestroyIcon(iconSmall));
                CHROMAFILER_MEMLEAK_FREE;
            }
            hwnd = nullptr;
            unlockProcess();
            Release(); // allow window to be deleted
            return 0;
        case WM_ACTIVATE:
            onActivate(LOWORD(wParam), (HWND)lParam);
            return 0;
        case WM_NCACTIVATE: {
            proxyIcon.setActive(!!wParam);
            LRESULT res = DefWindowProc(hwnd, message, wParam, lParam);
            if (!compositionEnabled)
                proxyIcon.redrawToolbar();
            return res;
        }
        case WM_NCPAINT: {
            LRESULT res = DefWindowProc(hwnd, message, wParam, lParam);
            if (!compositionEnabled)
                proxyIcon.redrawToolbar();
            return res;
        }
        case WM_MOUSEACTIVATE: {
            POINT cursor;
            GetCursorPos(&cursor);
            if (proxyIcon.isToolbarWindow(ChildWindowFromPoint(hwnd, screenToClient(hwnd, cursor))))
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
            if (defHitTest != HTCLIENT && defHitTest != HTSYSMENU)
                return defHitTest;
            return hitTestNCA(pointFromLParam(lParam));
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTRIGHT && HIWORD(lParam) != 0 && child && stickToChild()) {
                SetCursor(rightSideCursor);
                return TRUE;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            onPaint(paint);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_THEMECHANGED:
            // TODO: duplicate code, must be kept in sync with onCreate()
            // reset fonts
            proxyIcon.onThemeChanged();
            if (statusText && statusFont)
                PostMessage(statusText, WM_SETFONT, (WPARAM)statusFont, TRUE);
            if (statusTooltip && statusFont)
                PostMessage(statusTooltip, WM_SETFONT, (WPARAM)statusFont, TRUE);
            if (cmdToolbar && symbolFont)
                PostMessage(cmdToolbar, WM_SETFONT, (WPARAM)symbolFont, TRUE);
            if (parentToolbar && symbolFont)
                PostMessage(parentToolbar, WM_SETFONT, (WPARAM)symbolFont, TRUE);
            // reset toolbar button sizes
            if (cmdToolbar)
                PostMessage(cmdToolbar, TB_SETBUTTONSIZE, 0,
                    MAKELPARAM(TOOLBAR_HEIGHT, TOOLBAR_HEIGHT));
            if (parentToolbar) {
                SIZE size = rectSize(windowRect(parentToolbar));
                PostMessage(parentToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(size.cx, size.cy));
            }
            break;
        case WM_CTLCOLORSTATIC: { // status text background color
            HDC hdc = (HDC)wParam;
            int colorI = IsWindows10OrGreater() ? COLOR_WINDOW : COLOR_3DFACE;
            SetBkColor(hdc, GetSysColor(colorI));
            return colorI + 1;
        }
        case WM_ENTERSIZEMOVE:
            moveAccum = {0, 0};
            return 0;
        case WM_WINDOWPOSCHANGED: {
            WINDOWPOS *winPos = (WINDOWPOS *)lParam;
            const auto checkFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED;
            if ((winPos->flags & checkFlags) != checkFlags)
                windowRectChanged();
            break; // pass to DefWindowProc
        }
        case WM_MOVING: {
            // https://www.drdobbs.com/make-it-snappy/184416407
            RECT *desiredRect = (RECT *)lParam;
            RECT curRect = windowRect(hwnd);
            POINT offset = moveAccum;
            moveAccum.x += desiredRect->left - curRect.left;
            moveAccum.y += desiredRect->top - curRect.top;
            if (parent) {
                int moveAmount = max(abs(moveAccum.x), abs(moveAccum.y));
                if (moveAmount > DETACH_DISTANCE) {
                    detachFromParent(GetKeyState(VK_SHIFT) < 0);
                    OffsetRect(desiredRect, offset.x, offset.y);
                } else {
                    *desiredRect = curRect;
                }
            } else {
                viewStateDirty(1 << STATE_POS);
            }
            // required for WM_ENTERSIZEMOVE to behave correctly
            return TRUE;
        }
        case WM_SIZING: {
            RECT *desiredRect = (RECT *)lParam;
            if (parent && parent->stickToChild()) {
                RECT curRect = windowRect(hwnd);
                // constrain top-left corner
                if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT) {
                    int moveY = desiredRect->top - curRect.top;
                    auto topParent = parent;
                    while (topParent->parent && topParent->parent->stickToChild())
                        topParent = topParent->parent;
                    topParent->move(0, moveY);
                }
                if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT) {
                    int sizeX = desiredRect->left - curRect.left, sizeY = 0;
                    parent->adjustSize(&sizeX, &sizeY);
                    desiredRect->left = curRect.left + sizeX;
                }
            }
            if (parent && persistSizeInParent())
                parent->onChildResized(rectSize(*desiredRect));
            viewStateDirty(1 << STATE_SIZE);
            return TRUE;
        }
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
        case MSG_SHELL_NOTIFY: {
            LONG event;
            ITEMIDLIST **idls;
            HANDLE lock = SHChangeNotification_Lock((HANDLE)wParam, (DWORD)lParam, &idls, &event);
            if (lock) {
                CComPtr<IShellItem> item1, item2;
                if (idls[0])
                    checkHR(SHCreateItemFromIDList(idls[0], IID_PPV_ARGS(&item1)));
                if (idls[1])
                    checkHR(SHCreateItemFromIDList(idls[1], IID_PPV_ARGS(&item2)));
                SHChangeNotification_Unlock(lock);
                int compare;
                // TODO SICHINT_TEST_FILESYSPATH_IF_NOT_EQUAL?
                if (item1 && checkHR(item1->Compare(item, SICHINT_CANONICAL, &compare))
                        && compare == 0) {
                    if ((event & (SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER)) && item2) {
                        debugPrintf(L"Item renamed!\n");
                        itemMoved(item2);
                    } else {
                        debugPrintf(L"Resolving item due to shell event\n");
                        resolveItem();
                    }
                }
            }
            return 0;
        }
        case MSG_UPDATE_ICONS: {
            AcquireSRWLockExclusive(&iconLock);
            SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconLarge);
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);
            if (!paletteWindow() && (!parent || parent->paletteWindow()))
                chain->setIcon(iconSmall, iconLarge);
            proxyIcon.setIcon(iconSmall);
            ReleaseSRWLockExclusive(&iconLock);

            autoSizeProxy(clientSize(hwnd).cx);
            return 0;
        }
        case MSG_UPDATE_DEFAULT_STATUS_TEXT:
            AcquireSRWLockExclusive(&defaultStatusTextLock);
            if (defaultStatusText) {
                setStatusText(defaultStatusText);
                defaultStatusText.Free();
            }
            ReleaseSRWLockExclusive(&defaultStatusTextLock);
            return 0;
        case MSG_FLASH_WINDOW: {
            FLASHWINFO flash = {sizeof(flash), hwnd, FLASHW_ALL, 3, 100};
            FlashWindowEx(&flash);
            return 0;
        }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

bool ItemWindow::handleTopLevelMessage(MSG *msg) {
    return !!TranslateAccelerator(hwnd, accelTable, msg);
}

void ItemWindow::onCreate() {
    iconThread.Attach(new IconThread(item, this));
    iconThread->start();

    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        if (checkHR(link.CoCreateInstance(__uuidof(ShellLink)))) {
            checkHR(link->SetIDList(idList));
        }
    }
    registerShellNotify();

    if (!paletteWindow() && (!parent || parent->paletteWindow()))
        setChainPreview();

    if (!child && !parent && !paletteWindow() && !isScratch())
        SHAddToRecentDocs(SHARD_APPIDINFO, tempPtr(SHARDAPPIDINFO{item, appUserModelID()}));

    HMODULE instance = GetWindowInstance(hwnd);
    if (useCustomFrame()) {
        MARGINS margins;
        margins.cxLeftWidth = 0;
        margins.cxRightWidth = 0;
        margins.cyTopHeight = CAPTION_HEIGHT;
        margins.cyBottomHeight = 0;
        if (compositionEnabled)
            checkHR(DwmExtendFrameIntoClientArea(hwnd, &margins));

        proxyIcon.create(hwnd, item, title,
            captionTopMargin(), CAPTION_HEIGHT - captionTopMargin());
    }

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
            statusTextThread.Attach(new StatusTextThread(item, this));
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

    if (!getShellViewDispatch())
        onViewReady();
}

bool ItemWindow::hasStatusText() {
    return statusText != nullptr;
}

void ItemWindow::setStatusText(const wchar_t *text) {
    if (statusText)
        SetWindowText(statusText, text);
    if (statusTooltip) {
        TOOLINFO toolInfo = {sizeof(toolInfo)};
        toolInfo.hwnd = hwnd;
        toolInfo.uId = (UINT_PTR)statusText;
        toolInfo.lpszText = (wchar_t *)text;
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
    if (isScratch()) // will be deleted
        enableTransitions(true);
    return true;
}

void ItemWindow::onDestroy() {
    debugPrintf(L"Close %s\n", &*title);
    persistViewState();

    if (!parent)
        chain->setLeft(child); // "just in case"
    clearParent();
    if (child)
        child->close(); // recursive
    // child will still have a reference to parent until it handles WM_CLOSE
    child = nullptr; // onChildDetached will not be called
    if (activeWindow == this)
        activeWindow = nullptr;

    unregisterShellNotify();
    unregisterShellWindow();

    SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0); // remove owner
    chain = nullptr; // chain will be destroyed if this is the last window

    if (isScratch()) {
        debugPrintf(L"Deleting scratch file %s\n", &*title);
        resetViewState();
        deleteProxy(); // this is done after unregisterShellNotify()
    }

    if (iconThread)
        iconThread->stop();
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
            proxyIcon.setPressedState(true);
            openProxyContextMenu();
            proxyIcon.setPressedState(false);
            return true;
        }
        case IDM_RENAME_PROXY:
            proxyIcon.beginRename();
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
    if (proxyIcon.onControlCommand(controlHwnd, notif)) {
        return true;
    } else if (statusText && controlHwnd == statusText && notif == STN_CLICKED) {
        POINT cursorPos = {};
        GetCursorPos(&cursorPos);
        if (DragDetect(hwnd, cursorPos))
            fakeDragMove();
        return true;
    }
    return false;
}

LRESULT ItemWindow::onNotify(NMHDR *nmHdr) {
    LRESULT proxyRes = proxyIcon.onNotify(nmHdr);
    if (proxyRes) {
        return proxyRes;
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
    } else if ((proxyIcon.isToolbarWindow(nmHdr->hwndFrom) || nmHdr->hwndFrom == parentToolbar)
            && nmHdr->code == NM_CUSTOMDRAW) {
        NMTBCUSTOMDRAW *customDraw = (NMTBCUSTOMDRAW *)nmHdr;
        if (customDraw->nmcd.dwDrawStage == CDDS_PREPAINT) {
            return CDRF_NOTIFYPOSTPAINT;
        } else if (customDraw->nmcd.dwDrawStage == CDDS_POSTPAINT) {
            // fix title bar rendering (when not layered)
            makeBitmapOpaque(customDraw->nmcd.hdc, clientRect(nmHdr->hwndFrom));
        }
        return CDRF_DODEFAULT;
    } else if (nmHdr->hwndFrom == parentToolbar && nmHdr->code == NM_LDOWN) {
        NMMOUSE *mouse = (NMMOUSE *)nmHdr;
        POINT screenPos = clientToScreen(parentToolbar, mouse->pt);
        if (DragDetect(hwnd, screenPos)) {
            fakeDragMove();
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
    autoSizeProxy(size.cx);

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

void ItemWindow::autoSizeProxy(LONG width) {
    TITLEBARINFOEX titleBar = {sizeof(titleBar)};
    SendMessage(hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&titleBar);
    int closeButtonWidth = rectWidth(titleBar.rgrect[5]);

    int left = centeredProxy() ? PARENT_BUTTON_WIDTH : 0;
    proxyIcon.autoSize(width, left, closeButtonWidth);
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
        MoveWindow(chain->getWnd(), rect.left, rect.top, rectWidth(rect), rectHeight(rect), FALSE);
    }
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
        FillRect(paint.hdc, &toolbarRect, (HBRUSH)(COLOR_3DFACE + 1));
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

void ItemWindow::limitChainWindowRect(RECT *rect) {
    RECT myRect = windowRect(hwnd);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    GetMonitorInfo(monitor, &monitorInfo);
    LONG maxBottom = max(myRect.bottom, monitorInfo.rcWork.bottom);
    if (rect->bottom > maxBottom)
        rect->bottom = maxBottom;
}

void ItemWindow::openChild(IShellItem *const childItem) {
    CComPtr<IShellItem> resolved = resolveLink(childItem);
    if (child) {
        const int compareFlags = SICHINT_CANONICAL | SICHINT_TEST_FILESYSPATH_IF_NOT_EQUAL;
        int compare;
        if (checkHR(child->item->Compare(resolved, compareFlags, &compare)) && compare == 0)
            return; // already open
        child->close();
    }
    unregisterShellWindow();
    child = createItemWindow(this, resolved);
    SIZE size = child->persistSizeInParent() ? requestedChildSize() : child->requestedSize();
    POINT pos = childPos(size);
    RECT rect = {pos.x, pos.y, pos.x + size.cx, pos.y + size.cy};
    if (stickToChild())
        limitChainWindowRect(&rect);
    // will flush message queue
    child->create(rect, SW_SHOWNOACTIVATE);
    if (paletteWindow())
        autoUpdateCheck();
}

void ItemWindow::closeChild() {
    if (child) {
        child->close();
        child = nullptr; // onChildDetached will not be called
        if (!parent && !paletteWindow())
            registerShellWindow();
    }
}

void ItemWindow::openParent() {
    CComPtr<IShellItem> parentItem;
    if (FAILED(item->GetParent(&parentItem)))
        return;
    parent = createItemWindow(nullptr, parentItem);
    parent->child = this;

    unregisterShellWindow();
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
    resetViewState(1 << STATE_POS); // forget window position
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
        chain.Attach(new ChainWindow(this));
        for (ItemWindow *next = this; next != nullptr; next = next->child) {
            SetWindowLongPtr(next->hwnd, GWLP_HWNDPARENT, (LONG_PTR)chain->getWnd());
            next->chain = chain;
        }
        setChainPreview();
        if (!child)
            registerShellWindow();
        ShowWindow(chain->getWnd(), SW_SHOWNORMAL);
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
    } else if (!rootParent->paletteWindow()) {
        rootParent->activate(); // no window is focused by default
    }
    activate(); // bring this chain to front

    viewStateDirty(1 << STATE_SIZE);
    SHAddToRecentDocs(SHARD_APPIDINFO, tempPtr(SHARDAPPIDINFO{item, appUserModelID()}));
}

void ItemWindow::onChildDetached() {}

void ItemWindow::onChildResized(SIZE size) {
    childSize = size;
    viewStateDirty(1 << STATE_CHILD_SIZE);
}

void ItemWindow::detachAndMove(bool closeParent) {
    ItemWindow *rootParent = this;
    while (rootParent->parent && !rootParent->parent->paletteWindow())
        rootParent = rootParent->parent;
    RECT rootRect = windowRect(rootParent->hwnd);

    detachFromParent(closeParent);

    if (closeParent) {
        setPos({rootRect.left, rootRect.top});
    } else {
        int cascade = cascadeSize();
        POINT pos = {rootRect.left + cascade, rootRect.top + cascade};

        HMONITOR curMonitor = MonitorFromWindow(rootParent->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo = {sizeof(monitorInfo)};
        GetMonitorInfo(curMonitor, &monitorInfo);
        if (pos.x + cascade > monitorInfo.rcWork.right)
            pos.x = monitorInfo.rcWork.left;
        if (pos.y + cascade > monitorInfo.rcWork.bottom)
            pos.y = monitorInfo.rcWork.top;
        setPos(pos);
    }
}

SIZE ItemWindow::requestedSize() {
    if (auto bag = getPropBag()) {
        VARIANT sizeVar = {VT_UI4};
        if (SUCCEEDED(bag->Read(PROP_SIZE, &sizeVar, nullptr))) {
            return scaleDPI(sizeFromLParam(sizeVar.ulVal));
        }
    }
    return defaultSize();
}

RECT ItemWindow::requestedRect(HMONITOR preferMonitor) {
    SIZE size = requestedSize();

    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    if (preferMonitor)
        GetMonitorInfo(preferMonitor, &monitorInfo);

    if (auto bag = getPropBag()) {
        VARIANT posVar = {VT_UI4};
        if (SUCCEEDED(bag->Read(PROP_POS, &posVar, nullptr))) {
            POINT pos = scaleDPI(pointFromLParam(posVar.ulVal));
            if (!preferMonitor) {
                preferMonitor = MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST);
                GetMonitorInfo(preferMonitor, &monitorInfo);
            }
            int cascade = cascadeSize();
            if (PtInRect(&monitorInfo.rcWork, pos)
                    && PtInRect(&monitorInfo.rcWork, {pos.x + cascade, pos.y + cascade})) {
                return RECT{pos.x, pos.y, pos.x + size.cx, pos.y + size.cy};
            }
        }
    }
    RECT rect;
    if (!preferMonitor) {
        rect = RECT{CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT + size.cx, CW_USEDEFAULT + size.cy};
    } else {
        // find a good position for the window on the preferred monitor
        // https://devblogs.microsoft.com/oldnewthing/20131122-00/?p=2593
        HINSTANCE inst = GetModuleHandle(nullptr);
        HWND owner = checkLE(CreateWindow(TESTPOS_CLASS, nullptr, WS_OVERLAPPED,
            monitorInfo.rcWork.left, monitorInfo.rcWork.top, 1, 1, nullptr, nullptr, inst, 0));
        HWND defWnd = checkLE(CreateWindow(TESTPOS_CLASS, nullptr, WS_OVERLAPPED,
            CW_USEDEFAULT, CW_USEDEFAULT, size.cx, size.cy, owner, nullptr, inst, 0));
        rect = windowRect(defWnd);
        checkLE(DestroyWindow(owner));
    }

    viewStateDirty(1 << STATE_POS);
    return rect;
}

SIZE ItemWindow::requestedChildSize() {
    if (sizeEqual(childSize, {0, 0})) {
        if (auto bag = getPropBag()) {
            VARIANT sizeVar = {VT_UI4};
            if (SUCCEEDED(bag->Read(PROP_CHILD_SIZE, &sizeVar, nullptr))) {
                childSize = scaleDPI(sizeFromLParam(sizeVar.ulVal));
                return childSize;
            }
        }
        childSize = scaleDPI(settings::getItemWindowSize());
    }
    return childSize;
}

POINT ItemWindow::childPos(SIZE size) {
    HMONITOR curMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    int curMonitorDPI = monitorDPI(curMonitor);

    // https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect
    // GetWindowRect includes the resize margin!
    RECT rect = windowRect(hwnd);
    POINT pos = {rect.right - invisibleBorderDoubleSize(hwnd, curMonitorDPI), rect.top};

    RECT childRect = {pos.x, pos.y, pos.x + size.cx, pos.y + size.cy};
    HMONITOR childMonitor = MonitorFromRect(&childRect, MONITOR_DEFAULTTONEAREST);
    return pointMulDiv(pos, curMonitorDPI, monitorDPI(childMonitor));
}

POINT ItemWindow::parentPos(SIZE size) {
    HMONITOR curMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    int curMonitorDPI = monitorDPI(curMonitor);

    RECT rect = windowRect(hwnd);
    POINT pos = {rect.left - size.cx + invisibleBorderDoubleSize(hwnd, curMonitorDPI), rect.top};

    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    GetMonitorInfo(curMonitor, &monitorInfo);
    if (pos.x < monitorInfo.rcWork.left)
        pos.x = monitorInfo.rcWork.left;
    if (pos.y + cascadeSize() > monitorInfo.rcWork.bottom)
        pos.y = monitorInfo.rcWork.bottom - cascadeSize();
    return pos;
}

void ItemWindow::enableChain(bool enabled) {
    chain->setEnabled(enabled);
}

void ItemWindow::setChainPreview() {
    // update app user model id
    CComPtr<IPropertyStore> propStore;
    if (checkHR(SHGetPropertyStoreForWindow(chain->getWnd(), IID_PPV_ARGS(&propStore))))
        updateWindowPropStore(propStore);
    // update taskbar preview
    chain->setPreview(hwnd);
    // update alt-tab
    chain->setText(title);
    AcquireSRWLockExclusive(&iconLock);
    chain->setIcon(iconSmall, iconLarge);
    ReleaseSRWLockExclusive(&iconLock);
}

IDispatch * ItemWindow::getShellViewDispatch() {
    return nullptr;
}

void ItemWindow::onViewReady() {
    if (shellWindowCookie) {
        // onItemChanged was called
        CComQIPtr<IPersistIDList> persistIDList(item);
        CComPtr<IShellWindows> shellWindows;
        if (persistIDList && checkHR(shellWindows.CoCreateInstance(CLSID_ShellWindows))) {
            CComVariant pidlVar(persistIDList);
            checkHR(shellWindows->OnNavigate(shellWindowCookie, &pidlVar));
        }
    } else if (!child && !parent && !paletteWindow()) {
        registerShellWindow();
    }
}

void ItemWindow::registerShellWindow() {
    // https://www.vbforums.com/showthread.php?894889-VB6-Using-IShellWindows-to-register-for-SHOpenFolderAndSelectItems
    // https://github.com/derceg/explorerplusplus/blob/55208360ccbad78f561f22bdb3572ed7b0780fa0/Explorer%2B%2B/Explorer%2B%2B/ShellBrowser/BrowsingHandler.cpp#L238
    if (shellWindowCookie)
        return;
    CComQIPtr<IPersistIDList> persistIDList(item);
    CComPtr<IShellWindows> shellWindows;
    if (persistIDList && checkHR(shellWindows.CoCreateInstance(CLSID_ShellWindows))) {
        CComVariant empty, pidlVar(persistIDList);
        checkHR(shellWindows->RegisterPending(GetCurrentThreadId(), &pidlVar, &empty,
            SWC_BROWSER, &shellWindowCookie));
        checkHR(shellWindows->Register(getShellViewDispatch(), HandleToLong(hwnd),
            SWC_BROWSER, &shellWindowCookie));
    }
}

void ItemWindow::unregisterShellWindow() {
    if (shellWindowCookie) {
        CComPtr<IShellWindows> shellWindows;
        if (checkHR(shellWindows.CoCreateInstance(CLSID_ShellWindows)))
            checkHR(shellWindows->Revoke(shellWindowCookie));
        shellWindowCookie = 0;
    }
}

void ItemWindow::registerShellNotify() {
    CComPtr<IShellItem> parentItem;
    CComHeapPtr<ITEMIDLIST> idList;
    if (SUCCEEDED(item->GetParent(&parentItem))
            && checkHR(SHGetIDListFromObject(parentItem, &idList))) {
        SHChangeNotifyEntry notifEntry = {idList, FALSE};
        shellNotifyID = SHChangeNotifyRegister(hwnd,
            SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
            SHCNE_DELETE | SHCNE_RENAMEITEM | (isFolder() ? (SHCNE_RMDIR | SHCNE_RENAMEFOLDER) : 0),
            MSG_SHELL_NOTIFY, 1, &notifEntry);
    }
}

void ItemWindow::unregisterShellNotify() {
    if (shellNotifyID) {
        SHChangeNotifyDeregister(shellNotifyID);
        shellNotifyID = 0;
    }
}

bool ItemWindow::resolveItem() {
    if (closing) // can happen when closing save prompt (window is activated)
        return true;
    SFGAOF attr;
    // check if item exists
    if (SUCCEEDED(item->GetAttributes(SFGAO_VALIDATE, &attr)))
        return true;
    resetViewState(); // clear view state of old item

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
                    if (checkHR(SHCreateItemFromIDList(newIDList, IID_PPV_ARGS(&newItem)))) {
                        item = newItem; // itemMoved() is unnecessary since we can reuse link
                        onItemChanged();
                        return true;
                    }
                }
            }
        }
    }

    debugPrintf(L"Item has been deleted!\n");
    scratch = false;
    enableTransitions(true); // emphasize window closing
    close();
    return false;
}

void ItemWindow::itemMoved(IShellItem *const newItem) {
    resetViewState(); // clear view state of old item
    item = newItem;
    link = nullptr;
    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        if (checkHR(link.CoCreateInstance(__uuidof(ShellLink)))) {
            checkHR(link->SetIDList(idList));
        }
    }
    onItemChanged();
}

void ItemWindow::onItemChanged() {
    CComHeapPtr<wchar_t> newTitle;
    if (checkHR(item->GetDisplayName(SIGDN_NORMALDISPLAY, &newTitle))) {
        title = newTitle;
        SetWindowText(hwnd, title);
        proxyIcon.setTitle(title);
        autoSizeProxy(clientSize(hwnd).cx);
        if (!paletteWindow() && (!parent || parent->paletteWindow()))
            chain->setText(title);
    }
    proxyIcon.setItem(item);
    propBag = nullptr;
    resetViewState();
    if (auto bag = getPropBag())
        writeViewState(bag, ~0u);

    unregisterShellNotify();
    registerShellNotify();
    if (!getShellViewDispatch())
        onViewReady();
}

void ItemWindow::refresh() {
    if (iconThread)
        iconThread->stop();
    iconThread.Attach(new IconThread(item, this));
    iconThread->start();
    if (hasStatusText() && useDefaultStatusText()) {
        if (statusTextThread)
            statusTextThread->stop();
        statusTextThread.Attach(new StatusTextThread(item, this));
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
        SHFILEINFO fileInfo = {};
        CComHeapPtr<ITEMIDLIST> idList;
        if (checkHR(SHGetIDListFromObject(curItem, &idList))) {
            checkLE(SHGetFileInfo((wchar_t *)(ITEMIDLIST *)idList, 0, &fileInfo, sizeof(fileInfo),
                SHGFI_PIDL | SHGFI_ICON | SHGFI_ADDOVERLAYS | SHGFI_SMALLICON));
        }
        if (fileInfo.hIcon) {
            // http://shellrevealed.com:80/blogs/shellblog/archive/2007/02/06/Vista-Style-Menus_2C00_-Part-1-_2D00_-Adding-icons-to-standard-menus.aspx
            // icon must have alpha channel; SHGetFileInfo doesn't do this
            MENUITEMINFO itemInfo = {sizeof(itemInfo)};
            itemInfo.fMask = MIIM_BITMAP;
            itemInfo.hbmpItem = iconToPARGB32Bitmap(fileInfo.hIcon, iconSize, iconSize);
            DestroyIcon(fileInfo.hIcon);
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

void ItemWindow::deleteProxy() {
    CComHeapPtr<ITEMIDLIST> idList;
    if (checkHR(SHGetIDListFromObject(item, &idList))) {
        SHELLEXECUTEINFO info = {sizeof(info)};
        info.fMask = SEE_MASK_INVOKEIDLIST;
        info.lpVerb = L"delete";
        info.lpIDList = idList;
        info.hwnd = hwnd;
        checkLE(ShellExecuteEx(&info));
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
    UINT contextFlags = CMF_ITEMMENU | (useCustomFrame() ? CMF_CANRENAME : 0);
    if (GetKeyState(VK_SHIFT) < 0)
        contextFlags |= CMF_EXTENDEDVERBS;
    if (!checkHR(contextMenu->QueryContextMenu(popupMenu, 0, IDM_SHELL_FIRST, IDM_SHELL_LAST,
            contextFlags))) {
        checkLE(DestroyMenu(popupMenu));
        return;
    }
    POINT point = proxyIcon.getMenuPoint(hwnd);
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
            proxyIcon.beginRename();
        } else {
            auto info = makeInvokeInfo(cmd, point);
            contextMenu->InvokeCommand((CMINVOKECOMMANDINFO *)&info);
        }
    }
    checkLE(DestroyMenu(popupMenu));
}

CMINVOKECOMMANDINFOEX ItemWindow::makeInvokeInfo(int cmd, POINT point) {
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
    return info;
}

void ItemWindow::proxyDrag(POINT offset) {
    // https://devblogs.microsoft.com/oldnewthing/20041206-00/?p=37133 and onward
    CComPtr<IDataObject> dataObject;
    if (!checkHR(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&dataObject))))
        return;
    proxyIcon.dragDrop(dataObject, offset);
}

void ItemWindow::proxyRename(const wchar_t *name) {
    CComPtr<IFileOperation> operation;
    if (!checkHR(operation.CoCreateInstance(__uuidof(FileOperation))))
        return;
    checkHR(operation->SetOperationFlags(
        IsWindows8OrGreater() ? FOFX_ADDUNDORECORD : FOF_ALLOWUNDO));
    NewItemSink eventSink;
    if (!checkHR(operation->RenameItem(item, name, &eventSink)))
        return;
    unregisterShellNotify();
    checkHR(operation->PerformOperations());
    if (eventSink.newItem) {
        itemMoved(eventSink.newItem); // will call registerShellNotify()
    } else {
        registerShellNotify();
    }
}

ItemWindow::IconThread::IconThread(IShellItem *const item, ItemWindow *const callbackWindow)
        : callbackWindow(callbackWindow) {
    checkHR(SHGetIDListFromObject(item, &itemIDList));
}

void ItemWindow::IconThread::run() {
    SHFILEINFO fileInfo = {};
    checkLE(SHGetFileInfo((wchar_t *)(ITEMIDLIST *)itemIDList, 0, &fileInfo, sizeof(fileInfo),
        SHGFI_PIDL | SHGFI_ICON | SHGFI_ADDOVERLAYS | SHGFI_SMALLICON));
    if (fileInfo.hIcon == nullptr) {
        // bug (possibly windows 7 only?) where the first call sometimes fails
        checkLE(SHGetFileInfo((wchar_t *)(ITEMIDLIST *)itemIDList, 0, &fileInfo, sizeof(fileInfo),
            SHGFI_PIDL | SHGFI_ICON | SHGFI_ADDOVERLAYS | SHGFI_SMALLICON));
    }
    HICON hIconSmall = fileInfo.hIcon;
    checkLE(SHGetFileInfo((wchar_t *)(ITEMIDLIST *)itemIDList, 0, &fileInfo, sizeof(fileInfo),
        SHGFI_PIDL | SHGFI_ICON | SHGFI_ADDOVERLAYS | SHGFI_LARGEICON));

    AcquireSRWLockExclusive(&stopLock);
    if (!isStopped()) {
        AcquireSRWLockExclusive(&callbackWindow->iconLock);
        if (callbackWindow->iconLarge) {
            checkLE(DestroyIcon(callbackWindow->iconLarge));
            CHROMAFILER_MEMLEAK_FREE;
        }
        callbackWindow->iconLarge = fileInfo.hIcon;
        if (fileInfo.hIcon) {
            CHROMAFILER_MEMLEAK_ALLOC;
        }
    
        if (callbackWindow->iconSmall) {
            checkLE(DestroyIcon(callbackWindow->iconSmall));
            CHROMAFILER_MEMLEAK_FREE;
        }
        callbackWindow->iconSmall = hIconSmall;
        if (hIconSmall) {
            CHROMAFILER_MEMLEAK_ALLOC;
        }
        ReleaseSRWLockExclusive(&callbackWindow->iconLock);

        PostMessage(callbackWindow->hwnd, MSG_UPDATE_ICONS, 0, 0);
    }
    ReleaseSRWLockExclusive(&stopLock);
}

ItemWindow::StatusTextThread::StatusTextThread(
        IShellItem *const item, ItemWindow *const callbackWindow)
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

    AcquireSRWLockExclusive(&stopLock);
    if (!isStopped()) {
        AcquireSRWLockExclusive(&callbackWindow->defaultStatusTextLock);
        callbackWindow->defaultStatusText = text; // transfer ownership
        ReleaseSRWLockExclusive(&callbackWindow->defaultStatusTextLock);
        PostMessage(callbackWindow->hwnd, MSG_UPDATE_DEFAULT_STATUS_TEXT, 0, 0);
    }
    ReleaseSRWLockExclusive(&stopLock);
}

} // namespace
