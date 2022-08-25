#include "DPI.h"

namespace chromafile {

// DPI.h
int systemDPI;

// https://github.com/tringi/win32-dpi

void initDPI() {
    HDC screen = GetDC(0);
    systemDPI = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(0, screen);
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
