#pragma once
#include <common.h>

#include "message.h" // for convenience
#include <windows.h>
#include <atlmem.h>

namespace chromafile {

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

bool formatMessage(LocalHeapPtr<wchar_t> &message, DWORD messageId, ...);
void formatErrorMessage(LocalHeapPtr<wchar_t> &message, DWORD error);

} // namespace
