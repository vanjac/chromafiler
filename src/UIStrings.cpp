#include "UIStrings.h"
#include "DPI.h"

namespace chromafiler {

const wchar_t * getString(UINT id) {
    // must use /n flag with rc.exe!
    wchar_t *str = NULL;
    LoadString(GetModuleHandle(nullptr), id, (wchar_t *)&str, 0);
    return str;
}

bool formatMessage(LocalHeapPtr<wchar_t> &message, DWORD messageId, ...) {
    // https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage#examples
    va_list args = nullptr;
    va_start(args, messageId);
    DWORD result = FormatMessage(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        nullptr, messageId, 0, (wchar_t *)(wchar_t **)&message, 0, &args);
    va_end(args);
    return !!checkLE(result);
}

void formatErrorMessage(LocalHeapPtr<wchar_t> &message, DWORD error) {
    // based on _com_error::ErrorMessage()  (comdef.h)
    HMODULE mod = nullptr;
    if (error >= 12000 && error <= 12190)
        mod = GetModuleHandle(L"wininet.dll");
    FormatMessage((mod ? FORMAT_MESSAGE_FROM_HMODULE : FORMAT_MESSAGE_FROM_SYSTEM)
        | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
        mod, error, 0, (wchar_t *)(wchar_t **)&message, 0, nullptr);
    if (message) {
        int len = lstrlen(message);
        if (len > 1 && message[len - 1] == '\n') {
            message[len - 1] = 0;
            if (message[len - 2] == '\r')
                message[len - 2] = 0;
        }
    } else {
        formatMessage(message, STR_UNKNOWN_ERROR);
    }
}


void showDebugMessage(HWND owner, wchar_t *title, wchar_t *format, ...) {
    va_list args = nullptr;
    va_start(args, format);
    LocalHeapPtr<wchar_t> message;
    FormatMessage(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        format, 0, 0, (wchar_t *)(wchar_t **)&message, 0, &args);
    va_end(args);

    HWND edit = checkLE(CreateWindow(L"EDIT", title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VSCROLL | WS_HSCROLL
        | ES_AUTOHSCROLL | ES_LEFT | ES_MULTILINE | ES_READONLY,
        CW_USEDEFAULT, CW_USEDEFAULT, scaleDPI(400), scaleDPI(200),
        owner, nullptr, GetModuleHandle(nullptr), nullptr));
    SendMessage(edit, WM_SETTEXT, 0, (LPARAM)&*message);
    ShowWindow(edit, SW_SHOWNORMAL);
}

} // namespace
