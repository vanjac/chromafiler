#include "TrayWindow.h"
#include "GeomUtils.h"
#include "WinUtils.h"
#include "Settings.h"
#include "DPI.h"
#include "resource.h"
#include <windowsx.h>
#include <shellapi.h>

namespace chromafiler {

const wchar_t TRAY_WINDOW_CLASS[] = L"ChromaFile Tray";

const int HOTKEY_FOCUS_TRAY = 1;

// dimensions
static int SNAP_DISTANCE = 8;
static int CLOSE_BOX_MARGIN = 4;
static int DEFAULT_DIMEN = 400;
static SIZE MIN_TRAY_SIZE = {28, 28};

static UINT resetPositionMessage, taskbarCreatedMessage;

// https://walbourn.github.io/windows-sdk-for-windows-11/
inline bool IsWindows11OrGreater() {
    OSVERSIONINFOEXW version = { sizeof(version), 0, 0, 0, 0, {0}, 0, 0 };
    DWORDLONG const conditionMask = VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(
            0, VER_MAJORVERSION, VER_GREATER_EQUAL),
               VER_MINORVERSION, VER_GREATER_EQUAL),
               VER_BUILDNUMBER, VER_GREATER_EQUAL);
    version.dwMajorVersion = HIBYTE(_WIN32_WINNT_WIN10);
    version.dwMinorVersion = LOBYTE(_WIN32_WINNT_WIN10);
    version.dwBuildNumber = 22000;
    return !!VerifyVersionInfoW(&version,
        VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER, conditionMask);
}

static void snapAxis(LONG value, LONG edge, LONG *snapOffset, LONG *snapDist) {
    if (abs(value - edge) <= *snapDist) {
        *snapDist = abs(value - edge);
        *snapOffset = edge - value;
    }
}

void TrayWindow::init() {
    WNDCLASS wndClass = createWindowClass(TRAY_WINDOW_CLASS);
    wndClass.style = 0; // clear redraw style
    wndClass.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    RegisterClass(&wndClass);

    SNAP_DISTANCE = scaleDPI(SNAP_DISTANCE);
    CLOSE_BOX_MARGIN = scaleDPI(CLOSE_BOX_MARGIN);
    DEFAULT_DIMEN = scaleDPI(DEFAULT_DIMEN);
    MIN_TRAY_SIZE = scaleDPI(MIN_TRAY_SIZE);

    resetPositionMessage = checkLE(RegisterWindowMessage(L"chromafiler_TrayResetPosition"));
    taskbarCreatedMessage = checkLE(RegisterWindowMessage(L"TaskbarCreated"));
}

TrayWindow::TrayWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
        : FolderWindow(parent, item) {}

const wchar_t * TrayWindow::className() const {
    return TRAY_WINDOW_CLASS;
}

HWND TrayWindow::findTray() {
    return FindWindow(TRAY_WINDOW_CLASS, nullptr);
}

void TrayWindow::resetTrayPosition() {
    checkLE(SendNotifyMessage(HWND_BROADCAST, resetPositionMessage, 0, 0));
}

static RECT getTaskbarRect() {
    APPBARDATA abData {sizeof(abData)};
    SHAppBarMessage(ABM_GETTASKBARPOS, &abData);
    return abData.rc; // TODO this is incorrect if DPI changes while app is running
}

SIZE TrayWindow::requestedSize() {
    SIZE traySize = settings::getTraySize();
    if (traySize.cy == settings::DEFAULT_TRAY_SIZE.cy) {
        SIZE taskbarSize = rectSize(getTaskbarRect());
        if (taskbarSize.cx > taskbarSize.cy) {
            return {DEFAULT_DIMEN, taskbarSize.cy};
        } else {
            return {taskbarSize.cx, DEFAULT_DIMEN};
        }
    } else {
        return sizeMulDiv(traySize, systemDPI, settings::getTrayDPI());
    }
}

RECT TrayWindow::requestedRect(HMONITOR) {
    // ignore preferred monitor
    SIZE traySize = requestedSize();
    POINT trayPos = settings::getTrayPosition();
    if (pointEqual(trayPos, settings::DEFAULT_TRAY_POSITION)) {
        RECT taskbarRect = getTaskbarRect();
        trayPos = {taskbarRect.left, taskbarRect.top};
        if (!IsWindows11OrGreater()) { // center in taskbar
            if (rectWidth(taskbarRect) > rectHeight(taskbarRect)) {
                trayPos.x += (rectWidth(taskbarRect) - traySize.cx) / 2;;
            } else {
                trayPos.y += (rectHeight(taskbarRect) - traySize.cy) / 2;
            }
        }
    } else {
        trayPos = pointMulDiv(trayPos, systemDPI, settings::getTrayDPI());
    }

    RECT trayRect = {trayPos.x, trayPos.y, trayPos.x + traySize.cx, trayPos.y + traySize.cy};
    HMONITOR curMonitor = MonitorFromRect(&trayRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    GetMonitorInfo(curMonitor, &monitorInfo);
    if (trayRect.left < monitorInfo.rcMonitor.left) {
        OffsetRect(&trayRect, monitorInfo.rcMonitor.left - trayRect.left, 0);
    } else if (trayRect.left + MIN_TRAY_SIZE.cx > monitorInfo.rcMonitor.right) {
        OffsetRect(&trayRect, monitorInfo.rcMonitor.right - trayRect.right, 0);
    }
    if (trayRect.top < monitorInfo.rcMonitor.top) {
        OffsetRect(&trayRect, 0, monitorInfo.rcMonitor.top - trayRect.top);
    } else if (trayRect.top + MIN_TRAY_SIZE.cy > monitorInfo.rcMonitor.bottom) {
        OffsetRect(&trayRect, 0, monitorInfo.rcMonitor.bottom - trayRect.bottom);
    }
    return trayRect;
}

DWORD TrayWindow::windowStyle() const {
    return WS_POPUPWINDOW | WS_CLIPCHILDREN;
}

DWORD TrayWindow::windowExStyle() const {
    return WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
}

bool TrayWindow::useCustomFrame() const {
    return false;
}

bool TrayWindow::paletteWindow() const {
    return true;
}

bool TrayWindow::stickToChild() const {
    return false;
}

SettingsPage TrayWindow::settingsStartPage() const {
    return SETTINGS_TRAY;
}

RECT TrayWindow::windowBody() {
    SIZE size = clientSize(hwnd);
    if (size.cx > size.cy) {
        return {GetSystemMetrics(SM_CXVSCROLL), 0, size.cx, size.cy};
    } else {
        return {0, GetSystemMetrics(SM_CYHSCROLL), size.cx, size.cy};
    }
}

POINT TrayWindow::childPos(SIZE size) {
    RECT rect = windowRect(hwnd);
    switch (settings::getTrayDirection()) {
        default: // TRAY_UP
            return {rect.left, rect.top - size.cy}; // ignore drop shadow, space is ok
        case TRAY_DOWN:
            return {rect.left, rect.bottom};
        case TRAY_RIGHT:
            return FolderWindow::childPos(size);
    }
}

const wchar_t * TrayWindow::propBagName() const {
    return L"chromafiler.tray";
}

void TrayWindow::initDefaultView(CComPtr<IFolderView2> folderView) {
    checkHR(folderView->SetViewModeAndIconSize(FVM_ICON, SHELL_SMALL_ICON));
    checkHR(folderView->SetCurrentFolderFlags(FWF_AUTOARRANGE, FWF_AUTOARRANGE));
}

FOLDERSETTINGS TrayWindow::folderSettings() const {
    FOLDERSETTINGS settings = {};
    settings.ViewMode = FVM_ICON;
    settings.fFlags = FWF_NOWEBVIEW | FWF_NOCOLUMNHEADER | FWF_DESKTOP;
    if (useCustomIconPersistence()) // drag/drop is broken in Windows 10+
        settings.fFlags |= FWF_AUTOARRANGE;
    return settings;
}

void TrayWindow::onCreate() {
    HMODULE instance = GetWindowInstance(hwnd);
    SIZE size = clientSize(hwnd);
    traySizeGrip = checkLE(CreateWindow(L"SCROLLBAR", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBS_SIZEBOX | SBS_SIZEBOXBOTTOMRIGHTALIGN,
        0, 0, size.cx, size.cy,
        hwnd, nullptr, instance, nullptr));
    SetWindowSubclass(traySizeGrip, sizeGripProc, 0, 0);

    // TODO: SetCoalescableTimer on Windows 8 or later
    checkLE(SetTimer(hwnd, TIMER_MAKE_TOPMOST, 500, nullptr));
    // to receive fullscreen notifications:
    APPBARDATA abData = {sizeof(abData), hwnd};
    abData.uCallbackMessage = MSG_APPBAR_CALLBACK;
    SHAppBarMessage(ABM_NEW, &abData);

    checkLE(RegisterHotKey(hwnd, HOTKEY_FOCUS_TRAY, MOD_WIN | MOD_ALT, 'C'));

    FolderWindow::onCreate();
}

void TrayWindow::onDestroy() {
    FolderWindow::onDestroy();

    APPBARDATA abData = {sizeof(abData), hwnd};
    SHAppBarMessage(ABM_REMOVE, &abData);

    checkLE(UnregisterHotKey(hwnd, HOTKEY_FOCUS_TRAY));
}

void TrayWindow::onSize(SIZE size) {
    FolderWindow::onSize(size);

    SIZE gripSize = rectSize(windowRect(traySizeGrip));
    SetWindowPos(traySizeGrip, nullptr, size.cx - gripSize.cx, size.cy - gripSize.cy, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void TrayWindow::onExitSizeMove(bool moved, bool sized) {
    FolderWindow::onExitSizeMove(moved, sized);

    // save window position
    RECT rect = windowRect(hwnd);
    settings::setTrayPosition({rect.left, rect.top});
    settings::setTraySize(rectSize(rect));
    settings::setTrayDPI(systemDPI);
}

bool TrayWindow::handleTopLevelMessage(MSG *msg) {
    if (msg->message == WM_SYSKEYDOWN && msg->wParam == VK_F4)
        return true; // block Alt-F4
    return FolderWindow::handleTopLevelMessage(msg);
}

static void snapWindowPosition(HWND hwnd, RECT *rect) {
    HMONITOR curMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    GetMonitorInfo(curMonitor, &monitorInfo);

    POINT snap = {0, 0};
    POINT snapDist = {SNAP_DISTANCE, SNAP_DISTANCE};
    // left edge
    snapAxis(rect->left,   monitorInfo.rcMonitor.left,   &snap.x, &snapDist.x);
    snapAxis(rect->left,   monitorInfo.rcWork.left,      &snap.x, &snapDist.x);
    if (monitorInfo.rcWork.right < monitorInfo.rcMonitor.right)
        snapAxis(rect->left, monitorInfo.rcWork.right,   &snap.x, &snapDist.x);
    // top edge
    snapAxis(rect->top,    monitorInfo.rcMonitor.top,    &snap.y, &snapDist.y);
    snapAxis(rect->top,    monitorInfo.rcWork.top,       &snap.y, &snapDist.y);
    if (monitorInfo.rcWork.bottom < monitorInfo.rcMonitor.bottom)
        snapAxis(rect->top, monitorInfo.rcWork.bottom,   &snap.y, &snapDist.y);
    // right edge
    snapAxis(rect->right,  monitorInfo.rcMonitor.right,  &snap.x, &snapDist.x);
    snapAxis(rect->right,  monitorInfo.rcWork.right,     &snap.x, &snapDist.x);
    if (monitorInfo.rcWork.left > monitorInfo.rcMonitor.left)
        snapAxis(rect->right, monitorInfo.rcWork.left,   &snap.x, &snapDist.x);
    // bottom edge
    snapAxis(rect->bottom, monitorInfo.rcMonitor.bottom, &snap.y, &snapDist.y);
    snapAxis(rect->bottom, monitorInfo.rcWork.bottom,    &snap.y, &snapDist.y);
    if (monitorInfo.rcWork.top > monitorInfo.rcMonitor.top)
        snapAxis(rect->bottom, monitorInfo.rcWork.top,   &snap.y, &snapDist.y);
    OffsetRect(rect, snap.x, snap.y);
}

LRESULT TrayWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCAPTION && HIWORD(lParam) != 0) {
                SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                return TRUE;
            }
            break;
        case WM_GETMINMAXINFO: {
            MINMAXINFO *minMax = (LPMINMAXINFO)lParam;
            minMax->ptMinTrackSize = *(POINT *)&MIN_TRAY_SIZE;
            return 0;
        }
        case MSG_APPBAR_CALLBACK:
            if (wParam == ABN_FULLSCREENAPP) {
                fullScreen = !!lParam;
                SetWindowPos(hwnd, fullScreen ? HWND_BOTTOM : HWND_TOPMOST,
                    0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
            }
            return 0;
        case WM_TIMER:
            if (wParam == TIMER_MAKE_TOPMOST && !fullScreen) {
                GUITHREADINFO guiThread = {sizeof(guiThread)};
                // don't cover up eg. menus or drag overlays
                if (!(checkLE(GetGUIThreadInfo(0, &guiThread)) && guiThread.hwndCapture))
                    forceTopmost();
                return 0;
            }
            break;
        case WM_SIZING:
            if (wParam == WMSZ_BOTTOMRIGHT) {
                RECT *sizeRect = (RECT *)lParam;
                // snap to edges
                HMONITOR curMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO monitorInfo = {sizeof(monitorInfo)};
                GetMonitorInfo(curMonitor, &monitorInfo);
                POINT snap = {0, 0};
                POINT snapDist = {SNAP_DISTANCE, SNAP_DISTANCE};
                snapAxis(sizeRect->right,  monitorInfo.rcMonitor.right,  &snap.x, &snapDist.x);
                snapAxis(sizeRect->right,  monitorInfo.rcWork.right,     &snap.x, &snapDist.x);
                snapAxis(sizeRect->right,  monitorInfo.rcMonitor.left,   &snap.x, &snapDist.x);
                snapAxis(sizeRect->right,  monitorInfo.rcWork.left,      &snap.x, &snapDist.x);
                snapAxis(sizeRect->bottom, monitorInfo.rcMonitor.bottom, &snap.y, &snapDist.y);
                snapAxis(sizeRect->bottom, monitorInfo.rcWork.bottom,    &snap.y, &snapDist.y);
                snapAxis(sizeRect->bottom, monitorInfo.rcMonitor.top,    &snap.y, &snapDist.y);
                snapAxis(sizeRect->bottom, monitorInfo.rcWork.top,       &snap.y, &snapDist.y);
                sizeRect->right += snap.x; sizeRect->bottom += snap.y;
            }
            break; // pass to FolderWindow
        case WM_DISPLAYCHANGE: // resolution changed OR monitor connected/disconnected
            setRect(requestedRect(nullptr));
            return 0;
        case WM_HOTKEY:
            if (wParam == HOTKEY_FOCUS_TRAY) {
                SetForegroundWindow(hwnd);
                return 0;
            }
            break;
        case WM_ENTERSIZEMOVE: {
            RECT curRect = windowRect(hwnd);
            movePos = {curRect.left, curRect.top};
            break; // pass to ItemWindow
        }
        case WM_MOVING: {
            RECT *desiredRect = (RECT *)lParam;
            RECT curRect = windowRect(hwnd);
            OffsetRect(desiredRect, movePos.x - curRect.left, movePos.y - curRect.top);
            movePos = {desiredRect->left, desiredRect->top};
            snapWindowPosition(hwnd, desiredRect);
            break; // pass to ItemWindow
        }
    }
    if (resetPositionMessage && message == resetPositionMessage) {
        setRect(requestedRect(nullptr));
        return 0;
    } else if (taskbarCreatedMessage && message == taskbarCreatedMessage) {
        // https://learn.microsoft.com/en-us/windows/win32/shell/taskbar#taskbar-creation-notification
        // called when shell restarts or when DPI changes
        APPBARDATA abData = {sizeof(abData), hwnd};
        abData.uCallbackMessage = MSG_APPBAR_CALLBACK;
        SHAppBarMessage(ABM_NEW, &abData); // ok to call this even if already registered
    }
    return FolderWindow::handleMessage(message, wParam, lParam);
}

bool TrayWindow::onCommand(WORD command) {
    switch (command) {
        case IDM_PREV_WINDOW:
        case IDM_DETACH:
        case IDM_CLOSE_PARENT:
        case IDM_CLOSE_WINDOW:
        case IDM_RENAME_PROXY:
        case IDM_DELETE_PROXY:
        case IDM_PARENT_MENU:
            return true; // suppress commands
    }
    return FolderWindow::onCommand(command);
}

void TrayWindow::fixListViewColors() {
    if (listView) {
        ListView_SetBkColor(listView, GetSysColor(COLOR_WINDOW));
        ListView_SetTextColor(listView, GetSysColor(COLOR_WINDOWTEXT));
    }
}

void TrayWindow::forceTopmost() {
    RECT rect = windowRect(hwnd);
    POINT testPoint {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    if (GetAncestor(WindowFromPoint(testPoint), GA_ROOT) == hwnd)
        return; // already on top
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    if (GetAncestor(WindowFromPoint(testPoint), GA_ROOT) == hwnd)
        return; // success!
    debugPrintf(L"Forcing topmost\n");
    // ugly hack https://shlomio.wordpress.com/2012/09/04/solved-setforegroundwindow-win32-api-not-always-works/
    DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD threadID = GetCurrentThreadId();
    checkLE(AttachThreadInput(fgThread, threadID, true));
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    checkLE(AttachThreadInput(fgThread, threadID, false));
    return;
}

STDMETHODIMP TrayWindow::OnNavigationComplete(PCIDLIST_ABSOLUTE idList) {
    FolderWindow::OnNavigationComplete(idList);
    // must register drop target ourselves when using FWF_DESKTOP
    // https://www.codeproject.com/Questions/191728/Can-t-drop-files-to-IShellView-when-FWF-DESKTOP-fl
    if (shellView && listView) {
        CComQIPtr<IDropTarget> dropTarget(shellView);
        if (dropTarget) {
            RevokeDragDrop(listView);
            RegisterDragDrop(listView, dropTarget);
        }
    }
    if (listView) {
        DWORD style = GetWindowLong(listView, GWL_STYLE);
        style &= ~LVS_ALIGNLEFT;
        style |= LVS_ALIGNTOP | LVS_SHOWSELALWAYS;
        SetWindowLong(listView, GWL_STYLE, style);
        ListView_SetExtendedListViewStyleEx(listView, LVS_EX_MULTIWORKAREAS, 0);
    }
    fixListViewColors();
    return S_OK;
}

STDMETHODIMP TrayWindow::MessageSFVCB(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == 17) { // refresh
        if (!wParam) // post refresh
            fixListViewColors();
    }
    return FolderWindow::MessageSFVCB(msg, wParam, lParam);
}

LRESULT CALLBACK TrayWindow::sizeGripProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    switch (message) {
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
                return TRUE;
            }
            break;
        case WM_LBUTTONDBLCLK: // suppress default maximize behavior
        case WM_RBUTTONUP: // suppress right click menu
            return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace
