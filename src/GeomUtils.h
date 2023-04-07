#pragma once
#include <common.h>

#include <windows.h>

namespace chromafiler {

constexpr int rectWidth(const RECT &rect) {
    return rect.right - rect.left;
}

constexpr int rectHeight(const RECT &rect) {
    return rect.bottom - rect.top;
}

constexpr SIZE rectSize(const RECT &rect) {
    return {rectWidth(rect), rectHeight(rect)};
}

constexpr bool pointEqual(POINT a, POINT b) {
    return a.x == b.x && a.y == b.y;
}

constexpr bool sizeEqual(SIZE a, SIZE b) {
    return a.cx == b.cx && a.cy == b.cy;
}

} // namespace
