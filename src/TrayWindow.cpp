#include "TrayWindow.h"
#include "RectUtils.h"
#include <windowsx.h>

namespace chromabrowse {

const wchar_t *TRAY_WINDOW_CLASS = L"Tray";
const wchar_t *MOVE_GRIP_CLASS = L"Move Grip";

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
    return {windowRect.left, windowRect.top - size.cy}; // ignore drop shadow, some space is good
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

    FolderWindow::onCreate();
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
    }
    return FolderWindow::handleMessage(message, wParam, lParam);
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

} // namespace
