#include "DPI.h"
#include <stdio.h>

namespace chromafile {

// https://github.com/tringi/win32-dpi

// requires Windows 8.1
HRESULT (WINAPI *ptrGetScaleFactorForMonitor)(HMONITOR hmonitor, int *scale) = nullptr;

// DPI.h
int systemDPI;

void initDPI() {
    HMODULE hShcore = LoadLibrary(L"Shcore");
    if (hShcore) {
        ptrGetScaleFactorForMonitor = (decltype(ptrGetScaleFactorForMonitor))
            GetProcAddress(hShcore, "GetScaleFactorForMonitor");
    }

    HDC screen = GetDC(nullptr);
    systemDPI = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
}

int monitorDPI(HMONITOR monitor) {
    int scale;
    if (ptrGetScaleFactorForMonitor && checkHR(ptrGetScaleFactorForMonitor(monitor, &scale)))
        return MulDiv(scale, BASE_DPI, 100);
    return systemDPI;
}

int scaleDPI(int dp) {
    return MulDiv(dp, systemDPI, BASE_DPI);
}

SIZE scaleDPI(SIZE size) {
    return {scaleDPI(size.cx), scaleDPI(size.cy)};
}

int invScaleDPI(int px) {
    return MulDiv(px, BASE_DPI, systemDPI);
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

} // namespace
