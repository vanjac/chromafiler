#pragma once
#include <common.h>

#include <windows.h>

namespace chromafiler {

// modern DPI scaling methods were added in Windows 10 1607, but we are targeting Windows 7

extern int systemDPI;

// to be used in SetViewModeAndIconSize (does not scale with DPI)
const int SHELL_SMALL_ICON = 16;

void initDPI();
int monitorDPI(HMONITOR monitor);

int scaleDPI(int dp);
SIZE scaleDPI(SIZE size);
int invScaleDPI(int px);
SIZE invScaleDPI(SIZE size);
POINT pointMulDiv(POINT p, int num, int denom);
SIZE sizeMulDiv(SIZE s, int num, int denom);

// font
int pointsToPixels(int pt);
int pixelsToPoints(int px);

} // namespace
