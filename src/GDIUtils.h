#pragma once
#include <common.h>

#include <windows.h>

namespace chromafiler {

void makeBitmapOpaque(HDC hdc, const RECT &rect);
HBITMAP iconToPARGB32Bitmap(HICON icon, int width, int height);
// use alpha channel to composite onto a white background
void compositeBackground(const BITMAP &bitmap);

} // namespace
