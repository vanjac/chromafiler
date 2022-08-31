#include "GDIUtils.h"
#include "RectUtils.h"
#include <windowsx.h>
#include <cstdint>

namespace chromafile {

void makeBitmapOpaque(HDC hdc, const RECT &rect) {
    // https://devblogs.microsoft.com/oldnewthing/20210915-00/?p=105687
    // thank you Raymond Chen :)
    BITMAPINFO bitmapInfo = {{sizeof(BITMAPINFOHEADER), 1, 1, 1, 32, BI_RGB}};
    RGBQUAD bitmapBits = { 0x00, 0x00, 0x00, 0xFF };
    StretchDIBits(hdc, rect.left, rect.top, rectWidth(rect), rectHeight(rect),
                  0, 0, 1, 1, &bitmapBits, &bitmapInfo,
                  DIB_RGB_COLORS, SRCPAINT);
}

HBITMAP iconToPARGB32Bitmap(HICON icon, int width, int height) {
    HDC hdcMem = CreateCompatibleDC(nullptr);
    BITMAPINFO bitmapInfo = {{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB}};
    HBITMAP bitmap = nullptr;
    if ((bitmap = checkLE(CreateDIBSection(hdcMem, &bitmapInfo, DIB_RGB_COLORS,
                                           nullptr, nullptr, 0))) != nullptr) {
        SelectBitmap(hdcMem, bitmap);
        checkLE(DrawIconEx(hdcMem, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL));
        // TODO convert to premultiplied alpha?
    }
    DeleteDC(hdcMem);
    return bitmap;
}

void compositeBackground(const BITMAP &bitmap) {
    uint8_t *pixels = (uint8_t *)bitmap.bmBits;
    int rowIndex = 0;
    for (int y = 0; y < bitmap.bmHeight; y++, rowIndex += bitmap.bmWidthBytes) {
        int i = rowIndex;
        for (int x = 0; x < bitmap.bmWidth; x++, i += 4) {
            int alpha = pixels[i + 3];
            if (alpha == 255)
                continue;
            int c = 255 - alpha;
            pixels[i + 0] = (uint8_t)((int)(pixels[i + 0]) * alpha / 255 + c);
            pixels[i + 1] = (uint8_t)((int)(pixels[i + 1]) * alpha / 255 + c);
            pixels[i + 2] = (uint8_t)((int)(pixels[i + 2]) * alpha / 255 + c);
        }
    }
}

} // namespace
