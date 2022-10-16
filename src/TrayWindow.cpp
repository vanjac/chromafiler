#include "TrayWindow.h"
#include "RectUtils.h"
#include "Settings.h"
#include "DPI.h"
#include "resource.h"
#include <windowsx.h>
#include <shellapi.h>

namespace chromafiler {

const wchar_t TRAY_WINDOW_CLASS[] = L"ChromaFile Tray";
const wchar_t MOVE_GRIP_CLASS[] = L"ChromaFile Move Grip";

const int HOTKEY_FOCUS_TRAY = 1;

// dimensions
static int SNAP_DISTANCE = 8;
static int CLOSE_BOX_MARGIN = 4;
static int DEFAULT_DIMEN = 500;
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

void snapAxis(LONG value, LONG edge, LONG *snapped, LONG *snapDist) {
    if (abs(value - edge) <= *snapDist) {
        *snapDist = abs(value - edge);
        *snapped = edge;
    }
}

void TrayWindow::init() {
    WNDCLASS wndClass = createWindowClass(TRAY_WINDOW_CLASS);
    wndClass.style = 0; // clear redraw style
    RegisterClass(&wndClass);

    WNDCLASS moveGripClass = {};
    moveGripClass.lpszClassName = MOVE_GRIP_CLASS;
    moveGripClass.lpfnWndProc = moveGripProc;
    moveGripClass.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    moveGripClass.hCursor = LoadCursor(nullptr, IDC_SIZEALL);
    RegisterClass(&moveGripClass);

    SNAP_DISTANCE = scaleDPI(SNAP_DISTANCE);
    CLOSE_BOX_MARGIN = scaleDPI(CLOSE_BOX_MARGIN);
    DEFAULT_DIMEN = scaleDPI(DEFAULT_DIMEN);
    MIN_TRAY_SIZE = scaleDPI(MIN_TRAY_SIZE);

    resetPositionMessage = checkLE(RegisterWindowMessage(L"chromafiler_TrayResetPosition"));
    taskbarCreatedMessage = checkLE(RegisterWindowMessage(L"TaskbarCreated"));
}

TrayWindow::TrayWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
        : FolderWindow(parent, item, propertyBag()) {}

const wchar_t * TrayWindow::className() {
    return TRAY_WINDOW_CLASS;
}

HWND TrayWindow::findTray() {
    return FindWindow(TRAY_WINDOW_CLASS, nullptr);
}

void TrayWindow::resetTrayPosition() {
    checkLE(SendNotifyMessage(HWND_BROADCAST, resetPositionMessage, 0, 0));
}

RECT getTaskbarRect() {
    APPBARDATA abData {sizeof(abData)};
    SHAppBarMessage(ABM_GETTASKBARPOS, &abData);
    return abData.rc; // TODO this is incorrect if DPI changes while app is running
}

SIZE TrayWindow::requestedSize() const {
    SIZE traySize = settings::getTraySize();
    if (traySize.cy == settings::DEFAULT_TRAY_SIZE.cy) {
        RECT taskbarRect = getTaskbarRect();
        if (rectWidth(taskbarRect) > rectHeight(taskbarRect)) {
            return {DEFAULT_DIMEN, rectHeight(taskbarRect)};
        } else {
            return {rectWidth(taskbarRect), DEFAULT_DIMEN};
        }
    } else {
        return sizeMulDiv(traySize, systemDPI, settings::getTrayDPI());
    }
}

RECT TrayWindow::requestedRect() {
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

bool TrayWindow::allowToolbar() const {
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

POINT TrayWindow::childPos(SIZE size) {
    RECT rect = windowRect();
    switch (settings::getTrayDirection()) {
        default: // TRAY_UP
            return {rect.left, rect.top - size.cy}; // ignore drop shadow, space is ok
        case settings::TRAY_DOWN:
            return {rect.left, rect.bottom};
        case settings::TRAY_RIGHT:
            return FolderWindow::childPos(size);
    }
}

wchar_t * TrayWindow::propertyBag() const {
    return L"chromafile.tray";
}

void TrayWindow::initDefaultView(CComPtr<IFolderView2> folderView) {
    checkHR(folderView->SetViewModeAndIconSize(FVM_LIST, SHELL_SMALL_ICON));
}

void TrayWindow::onCreate() {
    HMODULE instance = GetWindowInstance(hwnd);

    checkLE(CreateWindow(MOVE_GRIP_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, GetSystemMetrics(SM_CXVSCROLL), GetSystemMetrics(SM_CYHSCROLL),
        hwnd, nullptr, instance, nullptr));
    RECT clientRect = {};
    GetClientRect(hwnd, &clientRect);
    traySizeGrip = checkLE(CreateWindow(L"SCROLLBAR", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBS_SIZEBOX | SBS_SIZEBOXBOTTOMRIGHTALIGN,
        0, 0, clientRect.right, clientRect.bottom,
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

void TrayWindow::onSize(int width, int height) {
    FolderWindow::onSize(width, height);

    RECT gripRect = {};
    GetWindowRect(traySizeGrip, &gripRect);
    SetWindowPos(traySizeGrip, nullptr,
        width - rectWidth(gripRect), height - rectHeight(gripRect), 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void TrayWindow::onExitSizeMove(bool moved, bool sized) {
    FolderWindow::onExitSizeMove(moved, sized); // note: moving with grip will NOT set moved

    // save window position
    RECT rect = windowRect();
    settings::setTrayPosition({rect.left, rect.top});
    settings::setTraySize(rectSize(rect));
    settings::setTrayDPI(systemDPI);
}

bool TrayWindow::handleTopLevelMessage(MSG *msg) {
    if (msg->message == WM_SYSKEYDOWN && msg->wParam == VK_F4)
        return true; // block Alt-F4
    return FolderWindow::handleTopLevelMessage(msg);
}

LRESULT TrayWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
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
                POINT pos = {sizeRect->right, sizeRect->bottom};
                POINT snapDist = {SNAP_DISTANCE, SNAP_DISTANCE};
                snapAxis(pos.x, monitorInfo.rcMonitor.right,  &sizeRect->right,  &snapDist.x);
                snapAxis(pos.x, monitorInfo.rcWork.right,     &sizeRect->right,  &snapDist.x);
                snapAxis(pos.y, monitorInfo.rcMonitor.bottom, &sizeRect->bottom, &snapDist.y);
                snapAxis(pos.y, monitorInfo.rcWork.bottom,    &sizeRect->bottom, &snapDist.y);
            }
            break; // pass to FolderWindow
        case WM_DISPLAYCHANGE: // resolution changed OR monitor connected/disconnected
            setRect(requestedRect());
            return 0;
        case WM_HOTKEY:
            if (wParam == HOTKEY_FOCUS_TRAY) {
                SetForegroundWindow(hwnd);
                return 0;
            }
            break;
    }
    if (resetPositionMessage && message == resetPositionMessage) {
        setRect(requestedRect());
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

void TrayWindow::forceTopmost() {
    RECT rect = windowRect();
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

POINT snapWindowPosition(HWND hwnd, POINT pos) {
    RECT windowRect = {};
    GetWindowRect(hwnd, &windowRect);
    HMONITOR curMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    GetMonitorInfo(curMonitor, &monitorInfo);

    POINT snapped = pos;
    POINT snapDist = {SNAP_DISTANCE, SNAP_DISTANCE};
    // left edge
    snapAxis(pos.x, monitorInfo.rcMonitor.left, &snapped.x, &snapDist.x);
    snapAxis(pos.x, monitorInfo.rcWork.left,    &snapped.x, &snapDist.x);
    if (monitorInfo.rcWork.right < monitorInfo.rcMonitor.right)
        snapAxis(pos.x, monitorInfo.rcWork.right, &snapped.x, &snapDist.x);
    // top edge
    snapAxis(pos.y, monitorInfo.rcMonitor.top,  &snapped.y, &snapDist.y);
    snapAxis(pos.y, monitorInfo.rcWork.top,     &snapped.y, &snapDist.y);
    if (monitorInfo.rcWork.bottom < monitorInfo.rcMonitor.bottom)
        snapAxis(pos.y, monitorInfo.rcWork.bottom, &snapped.y, &snapDist.y);
    // right edge
    snapAxis(pos.x, monitorInfo.rcMonitor.right - rectWidth(windowRect),   &snapped.x, &snapDist.x);
    snapAxis(pos.x, monitorInfo.rcWork.right - rectWidth(windowRect),      &snapped.x, &snapDist.x);
    // bottom edge
    snapAxis(pos.y, monitorInfo.rcMonitor.bottom - rectHeight(windowRect), &snapped.y, &snapDist.y);
    snapAxis(pos.y, monitorInfo.rcWork.bottom - rectHeight(windowRect),    &snapped.y, &snapDist.y);
    return snapped;
}

LRESULT CALLBACK TrayWindow::moveGripProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, lParam); // cursor offset
            SendMessage(GetParent(hwnd), WM_ENTERSIZEMOVE, 0, 0);
            break;
        case WM_MOUSEMOVE:
            if (wParam & MK_LBUTTON) {
                POINT cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hwnd, &cursor);
                LPARAM offsetParam = GetWindowLongPtr(hwnd, GWLP_USERDATA);
                POINT offset = {GET_X_LPARAM(offsetParam), GET_Y_LPARAM(offsetParam)};
                HWND parent = GetParent(hwnd);
                MapWindowPoints(hwnd, parent, &offset, 1);
                POINT pos = {cursor.x - offset.x - 1, cursor.y - offset.y - 1};
                pos = snapWindowPosition(parent, pos);
                SetWindowPos(parent, nullptr, pos.x, pos.y, 0, 0,
                    SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            break;
        case WM_LBUTTONUP:
            ReleaseCapture();
            SendMessage(GetParent(hwnd), WM_EXITSIZEMOVE, 0, 0);
            break;
        case WM_RBUTTONUP:
            PostMessage(GetParent(hwnd), WM_SYSCOMMAND, SC_KEYMENU, ' '); // show system menu
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            RECT rect = {};
            GetClientRect(hwnd, &rect);
            InflateRect(&rect, -CLOSE_BOX_MARGIN, -CLOSE_BOX_MARGIN);
            HBRUSH brush = GetSysColorBrush(COLOR_BTNTEXT);
            HDC hdc = paint.hdc;
            RECT f = {rect.left, rect.top, rect.right, rect.top + 1};   FillRect(hdc, &f, brush);
            f = {rect.left, rect.bottom - 1, rect.right, rect.bottom};  FillRect(hdc, &f, brush);
            f = {rect.left, rect.top, rect.left + 1, rect.bottom};      FillRect(hdc, &f, brush);
            f = {rect.right - 1, rect.top, rect.right, rect.bottom};    FillRect(hdc, &f, brush);
            EndPaint(hwnd, &paint);
            break;
        }
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
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
