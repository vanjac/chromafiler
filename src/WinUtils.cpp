#include "WinUtils.h"
#include <dwmapi.h>

namespace chromafiler {

RECT windowRect(HWND hwnd) {
    RECT rect = {};
    checkLE(GetWindowRect(hwnd, &rect));
    return rect;
}

RECT clientRect(HWND hwnd) {
    RECT rect = {};
    checkLE(GetClientRect(hwnd, &rect));
    return rect;
}

SIZE clientSize(HWND hwnd) {
    RECT rect = clientRect(hwnd);
    return {rect.right, rect.bottom};
}

POINT screenToClient(HWND hwnd, POINT screenPt) {
    checkLE(ScreenToClient(hwnd, &screenPt));
    return screenPt;
}

POINT clientToScreen(HWND hwnd, POINT clientPt) {
    checkLE(ClientToScreen(hwnd, &clientPt));
    return clientPt;
}

LRESULT CALLBACK WindowImpl::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WindowImpl *self;
    if (message == WM_NCCREATE) {
        self = (WindowImpl *)((CREATESTRUCT *)lParam)->lpCreateParams;
        self->hwnd = hwnd;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (WindowImpl *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
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

LRESULT WindowImpl::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hwnd, message, wParam, lParam);
}

bool WindowImpl::useCustomFrame() const {
    return false;
}

} // namespace
