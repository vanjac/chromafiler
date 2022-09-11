#include "UIStrings.h"

namespace chromafile {

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
    FormatMessage(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, (wchar_t *)(wchar_t **)&message, 0, nullptr);
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

} // namespace
