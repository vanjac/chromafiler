#include "DPI.h"

namespace chromafiler {

// https://github.com/tringi/win32-dpi

// requires Windows 8.1
static HRESULT (WINAPI *ptrGetScaleFactorForMonitor)(HMONITOR hmonitor, int *scale) = nullptr;

// DPI.h
int systemDPI = USER_DEFAULT_SCREEN_DPI;

void initDPI() {
    if (HMODULE hShcore = checkLE(LoadLibrary(L"Shcore"))) {
        ptrGetScaleFactorForMonitor = (decltype(ptrGetScaleFactorForMonitor))
            checkLE(GetProcAddress(hShcore, "GetScaleFactorForMonitor"));
    }

    if (HDC screen = checkLE(GetDC(nullptr))) {
        systemDPI = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
    }
}

int monitorDPI(HMONITOR monitor) {
    int scale;
    if (ptrGetScaleFactorForMonitor && checkHR(ptrGetScaleFactorForMonitor(monitor, &scale)))
        return MulDiv(scale, USER_DEFAULT_SCREEN_DPI, 100);
    return systemDPI;
}

int scaleDPI(int dp) {
    return MulDiv(dp, systemDPI, USER_DEFAULT_SCREEN_DPI);
}

SIZE scaleDPI(SIZE size) {
    return {scaleDPI(size.cx), scaleDPI(size.cy)};
}

int invScaleDPI(int px) {
    return MulDiv(px, USER_DEFAULT_SCREEN_DPI, systemDPI);
}

SIZE invScaleDPI(SIZE size) {
    return {invScaleDPI(size.cx), invScaleDPI(size.cy)};
}

POINT pointMulDiv(POINT p, int num, int denom) {
    return {MulDiv(p.x, num, denom), MulDiv(p.y, num, denom)};
}

SIZE sizeMulDiv(SIZE s, int num, int denom) {
    return {MulDiv(s.cx, num, denom), MulDiv(s.cy, num, denom)};
}

int pointsToPixels(int pt) {
    return MulDiv(pt, systemDPI, 72);
}

int pixelsToPoints(int px) {
    return MulDiv(px, 72, systemDPI);
}

} // namespace
