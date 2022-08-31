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

} // namespace
