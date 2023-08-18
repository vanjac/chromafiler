#include "UIStrings.h"
#include "DPI.h"

namespace chromafiler {

const wchar_t * getString(UINT id) {
    // must use /n flag with rc.exe!
    wchar_t *str = NULL;
    LoadString(GetModuleHandle(nullptr), id, (wchar_t *)&str, 0);
    return str;
}

local_wstr_ptr formatArgs(const wchar_t *format, va_list *args) {
    wchar_t *buffer = nullptr;
    FormatMessage(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        format, 0, 0, (wchar_t *)&buffer, 0, args);
    return local_wstr_ptr(buffer);
}

local_wstr_ptr format(const wchar_t *format, ...) {
    va_list args = nullptr;
    va_start(args, format);
    local_wstr_ptr str = formatArgs(format, &args);
    va_end(args);
    return str;
}

local_wstr_ptr formatString(UINT id, ...) {
    va_list args = nullptr;
    va_start(args, id);
    local_wstr_ptr str = formatArgs(getString(id), &args);
    va_end(args);
    return str;
}

local_wstr_ptr getErrorMessage(DWORD error) {
    // based on _com_error::ErrorMessage()  (comdef.h)
    HMODULE mod = nullptr;
    if (error >= 12000 && error <= 12190)
        mod = GetModuleHandle(L"wininet.dll");
    wchar_t *buffer = nullptr;
    DWORD result = FormatMessage((mod ? FORMAT_MESSAGE_FROM_HMODULE : FORMAT_MESSAGE_FROM_SYSTEM)
        | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
        mod, error, 0, (wchar_t *)&buffer, 0, nullptr);
    if (result && buffer) {
        int len = lstrlen(buffer);
        if (len > 1 && buffer[len - 1] == '\n') {
            buffer[len - 1] = 0;
            if (buffer[len - 2] == '\r')
                buffer[len - 2] = 0;
        }
        return local_wstr_ptr(buffer);
    } else {
        if (buffer)
            LocalFree(buffer);
        return formatString(IDS_UNKNOWN_ERROR);
    }
}


void showDebugMessage(HWND owner, const wchar_t *title, const wchar_t *format, ...) {
    va_list args = nullptr;
    va_start(args, format);
    local_wstr_ptr str = formatArgs(format, &args);
    va_end(args);

    HWND edit = checkLE(CreateWindow(L"EDIT", title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VSCROLL | WS_HSCROLL
        | ES_AUTOHSCROLL | ES_LEFT | ES_MULTILINE | ES_READONLY,
        CW_USEDEFAULT, CW_USEDEFAULT, scaleDPI(400), scaleDPI(200),
        owner, nullptr, GetModuleHandle(nullptr), nullptr));
    SendMessage(edit, WM_SETTEXT, 0, (LPARAM)str.get());
    ShowWindow(edit, SW_SHOWNORMAL);
}

} // namespace
