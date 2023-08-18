#pragma once
#include <common.h>

#include "resource.h"
#include <memory>
#include <windows.h>

namespace chromafiler {

struct LocalDeleter {
    template <typename T>
    void operator()(T* p) {
        LocalFree(p);
    }
};

using local_wstr_ptr = std::unique_ptr<wchar_t[], LocalDeleter>;

const wchar_t * getString(UINT id);
local_wstr_ptr formatString(UINT id, ...);

local_wstr_ptr formatErrorMessage(DWORD error);

void showDebugMessage(HWND owner, wchar_t *title, wchar_t *format, ...);

} // namespace
