#include "WinUtils.h"

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

} // namespace
