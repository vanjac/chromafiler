#include "ChainWindow.h"
#include "ItemWindow.h"

namespace chromafiler {

const wchar_t CHAIN_OWNER_CLASS[] = L"ChromaFile Chain";

void ChainWindow::init() {
    WNDCLASS chainClass = {};
    chainClass.lpszClassName = CHAIN_OWNER_CLASS;
    chainClass.lpfnWndProc = windowProc;
    chainClass.hInstance = GetModuleHandle(nullptr);
    RegisterClass(&chainClass);
}

ChainWindow::ChainWindow(ItemWindow *left, bool leftIsPopup, int showCommand)
        : left(left) {
    debugPrintf(L"Create chain\n");
    // there are special cases here for popup windows (ie. the tray) to fix DPI scaling bugs.
    // see ItemWindow::windowRectChanged() for details
    HWND window = checkLE(CreateWindowEx(leftIsPopup ? (WS_EX_LAYERED | WS_EX_TOOLWINDOW) : 0,
        CHAIN_OWNER_CLASS, nullptr, leftIsPopup ? WS_OVERLAPPED : WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandle(nullptr), (WindowImpl *)this));
    if (showCommand != -1)
        ShowWindow(window, showCommand); // show in taskbar
    if (leftIsPopup)
        SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA); // invisible but still drawn
}

ChainWindow::~ChainWindow() {
    debugPrintf(L"Destroy chain\n");
    setPreview(nullptr);
    DestroyWindow(hwnd);
}

HWND ChainWindow::getWnd() {
    return hwnd;
}

void ChainWindow::setLeft(ItemWindow *window) {
    left = window;
}

void ChainWindow::setPreview(HWND newPreview) {
    CComPtr<ITaskbarList4> taskbar;
    if (!checkHR(taskbar.CoCreateInstance(__uuidof(TaskbarList))))
        return;

    if (preview) {
        checkHR(taskbar->UnregisterTab(preview));
    }
    preview = newPreview;
    if (preview) {
        checkHR(taskbar->RegisterTab(preview, hwnd));
        checkHR(taskbar->SetTabOrder(preview, nullptr));
        checkHR(taskbar->SetTabProperties(preview, STPF_USEAPPPEEKALWAYS));
    }
}

void ChainWindow::setText(const wchar_t *text) {
    SetWindowText(hwnd, text);
}

void ChainWindow::setIcon(HICON smallIcon, HICON largeIcon) {
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
}

void ChainWindow::setEnabled(bool enabled) {
    for (ItemWindow *window = left; window; window = window->child)
        EnableWindow(window->hwnd, enabled);
}

LRESULT ChainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CLOSE:
            // default behavior is to destroy owned windows without calling WM_CLOSE.
            // instead close the left-most chain window to give user a chance to save.
            debugPrintf(L"Close chain\n");
            if (left->paletteWindow()) {
                if (left->child)
                    left->child->close();
            } else {
                left->close();
            }
            return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

} // namespace
