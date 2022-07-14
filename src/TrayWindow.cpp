#include "TrayWindow.h"
#include "RectUtils.h"
#include "Settings.h"
#include <windowsx.h>
#include <shellapi.h>

namespace chromabrowse {

const wchar_t TRAY_WINDOW_CLASS[] = L"Tray";
const wchar_t MOVE_GRIP_CLASS[] = L"Move Grip";

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
}

TrayWindow::TrayWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
        : FolderWindow(parent, item) {}

const wchar_t * TrayWindow::className() {
    return TRAY_WINDOW_CLASS;
}

POINT TrayWindow::requestedPosition() {
    return settings::getTrayPosition();
}

SIZE TrayWindow::requestedSize() const {
    return settings::getTraySize();
}

DWORD TrayWindow::windowStyle() const {
    return WS_POPUP | WS_SYSMENU | WS_BORDER;
}

bool TrayWindow::useCustomFrame() const {
    return false;
}

bool TrayWindow::alwaysOnTop() const {
    return true;
}

bool TrayWindow::stickToChild() const {
    return false;
}

POINT TrayWindow::childPos(SIZE size) {
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    if (rectHeight(windowRect) > rectWidth(windowRect)) {
        return FolderWindow::childPos(size); // open to the right
    } else {
        // open above
        return {windowRect.left, windowRect.top - size.cy}; // ignore drop shadow, space is ok
    }
}

wchar_t * TrayWindow::propertyBag() const {
    return L"chromabrowse.tray";
}

void TrayWindow::initDefaultView(CComPtr<IFolderView2> folderView) {
    checkHR(folderView->SetViewModeAndIconSize(FVM_LIST, GetSystemMetrics(SM_CXSMICON)));
}

void TrayWindow::onCreate() {
    HMODULE instance = GetWindowInstance(hwnd);

    CreateWindow(MOVE_GRIP_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, GetSystemMetrics(SM_CXVSCROLL), GetSystemMetrics(SM_CYHSCROLL),
        hwnd, nullptr, instance, nullptr);
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    traySizeGrip = CreateWindow(L"SCROLLBAR", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBS_SIZEBOX | SBS_SIZEBOXBOTTOMRIGHTALIGN,
        0, 0, clientRect.right, clientRect.bottom,
        hwnd, nullptr, instance, nullptr);
    SetWindowSubclass(traySizeGrip, sizeGripProc, 0, 0);

    SetCoalescableTimer(hwnd, TIMER_MAKE_TOPMOST, 500, nullptr, 250);
    // to receive fullscreen notifications:
    APPBARDATA abData = {sizeof(abData), hwnd};
    abData.uCallbackMessage = MSG_APPBAR_CALLBACK;
    SHAppBarMessage(ABM_NEW, &abData);

    FolderWindow::onCreate();
}

void TrayWindow::onDestroy() {
    FolderWindow::onDestroy();

    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    settings::setTrayPosition({windowRect.left, windowRect.top});
    settings::setTraySize(rectSize(windowRect));

    APPBARDATA abData = {sizeof(abData), hwnd};
    SHAppBarMessage(ABM_REMOVE, &abData);
}

void TrayWindow::onSize(int width, int height) {
    FolderWindow::onSize(width, height);

    RECT gripRect;
    GetWindowRect(traySizeGrip, &gripRect);
    SetWindowPos(traySizeGrip, nullptr,
        width - rectWidth(gripRect), height - rectHeight(gripRect), 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT TrayWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_GETMINMAXINFO: {
            MINMAXINFO *minMax = (LPMINMAXINFO)lParam;
            minMax->ptMinTrackSize.x = 28;
            minMax->ptMinTrackSize.y = 28;
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
                if (!(GetGUIThreadInfo(0, &guiThread) && guiThread.hwndCapture))
                    forceTopmost();
                return 0;
            }
            break;
    }
    return FolderWindow::handleMessage(message, wParam, lParam);
}

void TrayWindow::forceTopmost() {
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    POINT testPoint {(windowRect.left + windowRect.right) / 2,
                     (windowRect.top + windowRect.bottom) / 2};
    if (GetAncestor(WindowFromPoint(testPoint), GA_ROOT) == hwnd)
        return; // already on top
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    if (GetAncestor(WindowFromPoint(testPoint), GA_ROOT) == hwnd)
        return; // success!
    debugPrintf(L"Forcing topmost\n");
    // ugly hack https://shlomio.wordpress.com/2012/09/04/solved-setforegroundwindow-win32-api-not-always-works/
    DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    AttachThreadInput(fgThread, GetCurrentThreadId(), true);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    AttachThreadInput(fgThread, GetCurrentThreadId(), false);
    return;
}

LRESULT CALLBACK TrayWindow::moveGripProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, lParam); // cursor offset
            break;
        case WM_MOUSEMOVE:
            if (wParam & MK_LBUTTON) {
                POINT cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hwnd, &cursor);
                LPARAM offsetParam = GetWindowLongPtr(hwnd, GWLP_USERDATA);
                POINT offset = {GET_X_LPARAM(offsetParam), GET_Y_LPARAM(offsetParam)};
                HWND parent = GetParent(hwnd);
                MapWindowPoints(hwnd, parent, &offset, 1);
                SetWindowPos(parent, nullptr, cursor.x - offset.x - 1, cursor.y - offset.y - 1,
                    0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            break;
        case WM_LBUTTONUP:
            ReleaseCapture();
            break;
        case WM_RBUTTONUP:
            PostMessage(GetParent(hwnd), WM_SYSCOMMAND, SC_KEYMENU, ' '); // show system menu
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            RECT rect;
            GetClientRect(hwnd, &rect);
            InflateRect(&rect, -4, -4);
            HBRUSH oldBrush = SelectBrush(paint.hdc, GetStockBrush(NULL_BRUSH));
            HPEN oldPen = SelectPen(paint.hdc, GetStockPen(BLACK_PEN));
            Rectangle(paint.hdc, rect.left, rect.top, rect.right, rect.bottom);
            SelectBrush(paint.hdc, oldBrush);
            SelectPen(paint.hdc, oldPen);
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
