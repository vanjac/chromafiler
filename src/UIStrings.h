#pragma once
#include <common.h>

#include "resource.h"
#include <windows.h>
#include <atlmem.h>

namespace chromafiler {

class LocalAllocator {
public:
    static void* Allocate(_In_ size_t nBytes) throw() {
        return LocalAlloc( LMEM_FIXED, nBytes );
    }
    static void* Reallocate(
        _In_opt_ void* p,
        _In_ size_t nBytes) throw() {
        return LocalReAlloc(p, nBytes, 0);
    }
    static void Free(_In_opt_ void* p) throw() {
        LocalFree(p);
    }
};

template<typename T>
using LocalHeapPtr = CHeapPtr<T, LocalAllocator>;

const wchar_t * getString(UINT id);
bool formatString(LocalHeapPtr<wchar_t> &message, UINT id, ...);

void formatErrorMessage(LocalHeapPtr<wchar_t> &message, DWORD error);

void showDebugMessage(HWND owner, wchar_t *title, wchar_t *format, ...);

} // namespace
