#pragma once
#include <common.h>

#include <windows.h>

namespace chromabrowse {

constexpr int rectWidth(const RECT &rect) {
    return rect.right - rect.left;
}

constexpr int rectHeight(const RECT &rect) {
    return rect.bottom - rect.top;
}

constexpr SIZE rectSize(const RECT &rect) {
    return {rectWidth(rect), rectHeight(rect)};
}

} // namespace
