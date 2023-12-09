#pragma once
#include <common.h>

#include <memory>
#include <windows.h>

namespace chromafiler {

// https://stackoverflow.com/a/47459319
template <typename T>
const T* tempPtr(const T&& x) { return &x; }

using wstr_ptr = std::unique_ptr<wchar_t[]>;

/* win32 wrappers that return values directly instead of error codes */
RECT windowRect(HWND hwnd);
RECT clientRect(HWND hwnd); // client rect top-left is always at (0,0)
SIZE clientSize(HWND hwnd);
POINT screenToClient(HWND hwnd, POINT screenPt);
POINT clientToScreen(HWND hwnd, POINT clientPt);

class WindowImpl {
protected:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    virtual LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    virtual bool useCustomFrame() const;
    HWND hwnd = nullptr;
};

} // namespace
