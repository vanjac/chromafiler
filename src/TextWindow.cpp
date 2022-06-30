#include "TextWindow.h"
#include "RectUtils.h"
#include <windowsx.h>
#include <shlobj.h>
#include <Richedit.h>
#include <VersionHelpers.h>

namespace chromabrowse {

const wchar_t TEXT_WINDOW_CLASS[] = L"Text Window";

const wchar_t TEXT_FONT_FACE[] = L"Consolas";
const int TEXT_FONT_HEIGHT = 16;

static HFONT monoFont = 0;

void TextWindow::init() {
    WNDCLASS wndClass = createWindowClass(TEXT_WINDOW_CLASS);
    RegisterClass(&wndClass);
    // http://www.jose.it-berater.org/richedit/rich_edit_control.htm
    LoadLibrary(L"Msftedit.dll");

    monoFont = CreateFont(TEXT_FONT_HEIGHT, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
        DEFAULT_PITCH | FF_DONTCARE, TEXT_FONT_FACE);
}

void TextWindow::uninit() {
    if (monoFont)
        DeleteFont(monoFont);
}

TextWindow::TextWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
    : ItemWindow(parent, item) {}

const wchar_t * TextWindow::className() {
    return TEXT_WINDOW_CLASS;
}

void TextWindow::onCreate() {
    ItemWindow::onCreate();
    edit = CreateWindow(MSFTEDIT_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL
        | ES_NOHIDESEL | ES_SAVESEL | ES_SELECTIONBAR | ES_READONLY,
        0, 0, 0, 0,
        hwnd, nullptr, GetWindowInstance(hwnd), nullptr);
    SetWindowSubclass(edit, richEditProc, 0, (DWORD_PTR)this);
    if (monoFont)
        SendMessage(edit, WM_SETFONT, (WPARAM)monoFont, FALSE);
    SendMessage(edit, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    if (IsWindows8OrGreater())
        SendMessage(edit, EM_SETEDITSTYLE, SES_MULTISELECT, SES_MULTISELECT);

    CComHeapPtr<uint8_t> buffer;
    ULONG size;
    {
        CComPtr<IBindCtx> context;
        if (checkHR(CreateBindCtx(0, &context))) {
            BIND_OPTS options = {sizeof(BIND_OPTS), 0, STGM_READ | STGM_SHARE_DENY_NONE, 0};
            checkHR(context->SetBindOptions(&options));
        }
        CComPtr<IStream> stream;
        if (!checkHR(item->BindToHandler(context, BHID_Stream, IID_PPV_ARGS(&stream))))
            return;
        ULARGE_INTEGER largeSize;
        if (!checkHR(IStream_Size(stream, &largeSize)))
            return;
        if (largeSize.QuadPart > (ULONGLONG)(ULONG)-1) {
            debugPrintf(L"Too large!\n");
            return;
        }
        size = (ULONG)largeSize.QuadPart;
        buffer.AllocateBytes(size + 2);
        if (!checkHR(IStream_Read(stream, buffer, (ULONG)size)))
            return;
        buffer[size] = buffer[size + 1] = 0;
    }

    // https://docs.microsoft.com/en-us/windows/win32/intl/using-byte-order-marks
    if (size >= 2 && buffer[0] == 0xFF && buffer[1] == 0xFE) {
        debugPrintf(L"UTF-16 LE\n");
        encoding = UTF16LE;
        SendMessage(edit, WM_SETTEXT, 0, (LPARAM)(uint8_t *)(buffer + 2));
    } else if (size >= 2 && buffer[0] == 0xFE && buffer[1] == 0xFF) {
        debugPrintf(L"UTF-16 BE\n");
        encoding = UTF16BE;
        wchar_t *wcBuffer = (wchar_t *)(void *)buffer;
        ULONG wcSize = size / 2;
        for (ULONG i = 1; i < wcSize; i++)
            wcBuffer[i] = _byteswap_ushort(wcBuffer[i]);
        SendMessage(edit, WM_SETTEXT, 0, (LPARAM)(wcBuffer + 1));
    } else { // assume UTF-8
        uint8_t *utf8String = buffer;
        if (size >= 3 && buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF) {
            debugPrintf(L"UTF-8 BOM\n");
            encoding = UTF8BOM;
            utf8String += 3;
        } else {
            debugPrintf(L"UTF-8\n");
            encoding = UTF8;
        }
        SETTEXTEX setText = {};
        setText.codepage = CP_UTF8;
        SendMessage(edit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM)utf8String);
    }
}

void TextWindow::onActivate(WORD state, HWND prevWindow) {
    ItemWindow::onActivate(state, prevWindow);
    if (state != WA_INACTIVE) {
        SetFocus(edit);
    }
}

void TextWindow::onSize(int width, int height) {
    ItemWindow::onSize(width, height);
    RECT body = windowBody();
    MoveWindow(edit, body.left, body.top, rectWidth(body), rectHeight(body), TRUE);
}

bool TextWindow::handleTopLevelMessage(MSG *msg) {
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_TAB)
        return false;
    return ItemWindow::handleTopLevelMessage(msg);
}

LRESULT CALLBACK TextWindow::richEditProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    if (message == WM_MOUSEWHEEL) {
        // override smooth scrolling
        TextWindow *window = (TextWindow *)refData;
        window->scrollAccum += GET_WHEEL_DELTA_WPARAM(wParam);

        UINT linesPerClick = 3;
        SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &linesPerClick, 0);
        float lineDelta = (float)WHEEL_DELTA / linesPerClick;
        int lines = (int)floor(window->scrollAccum / lineDelta);
        window->scrollAccum -= (int)(lines * lineDelta);
        SendMessage(hwnd, EM_LINESCROLL, 0, -lines);
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace
