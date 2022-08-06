#include "TextWindow.h"
#include "RectUtils.h"
#include "UIStrings.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>

namespace chromafile {

const wchar_t TEXT_WINDOW_CLASS[] = L"Text Window";

const wchar_t TEXT_FONT_FACE[] = L"Consolas";
const int TEXT_FONT_HEIGHT = 16;

const uint8_t BOM_UTF8BOM[] = {0xEF, 0xBB, 0xBF};
const uint8_t BOM_UTF16LE[] = {0xFF, 0xFE};
const uint8_t BOM_UTF16BE[] = {0xFE, 0xFF};
#define CHECK_BOM(buffer, size, bom) \
    ((size) >= sizeof(bom) && memcmp((buffer), (bom), sizeof(bom)) == 0)

static HACCEL textAccelTable;
static HFONT monoFont = 0;
static UINT findReplaceMessage;

void TextWindow::init() {
    WNDCLASS wndClass = createWindowClass(TEXT_WINDOW_CLASS);
    RegisterClass(&wndClass);
    // http://www.jose.it-berater.org/richedit/rich_edit_control.htm
    LoadLibrary(L"Msftedit.dll");

    monoFont = CreateFont(TEXT_FONT_HEIGHT, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
        DEFAULT_PITCH | FF_DONTCARE, TEXT_FONT_FACE);

    textAccelTable = LoadAccelerators(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_TEXT_ACCEL));

    findReplaceMessage = RegisterWindowMessage(FINDMSGSTRING);
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

bool TextWindow::useDefaultStatusText() const {
    return false;
}

void TextWindow::onCreate() {
    ItemWindow::onCreate();
    edit = CreateWindow(MSFTEDIT_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL
        | ES_NOHIDESEL | ES_SAVESEL | ES_SELECTIONBAR,
        0, 0, 0, 0,
        hwnd, nullptr, GetWindowInstance(hwnd), nullptr);
    SetWindowSubclass(edit, richEditProc, 0, (DWORD_PTR)this);
    if (monoFont)
        SendMessage(edit, WM_SETFONT, (WPARAM)monoFont, FALSE);
    SendMessage(edit, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    SendMessage(edit, EM_SETEVENTMASK, 0, ENM_SELCHANGE);
    SendMessage(edit, EM_EXLIMITTEXT, 0, 0x7FFFFFFE); // maximum possible limit

    if (!loadText())
        SendMessage(edit, EM_SETOPTIONS, ECOOP_OR, ECO_READONLY);
    SendMessage(edit, EM_SETMODIFY, FALSE, 0);
    if (hasStatusText())
        updateStatus({0, 0});
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
    // TODO: this will only be handled if the text window is active
    if (findReplaceDialog && IsDialogMessage(findReplaceDialog, msg))
        return true;
    if (TranslateAccelerator(hwnd, textAccelTable, msg))
        return true;
    return ItemWindow::handleTopLevelMessage(msg);
}

void undoNameToString(UNDONAMEID id, LocalHeapPtr<wchar_t> &str) {
    if (id > UID_AUTOTABLE)
        id = UID_UNKNOWN;
    formatMessage(str, id + STR_TEXT_ACTION_UNKNOWN);
}

LRESULT TextWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CLOSE:
            if (encoding != FAIL && SendMessage(edit, EM_GETMODIFY, 0, 0)) {
                LocalHeapPtr<wchar_t> caption, text;
                formatMessage(caption, STR_SAVE_PROMPT_CAPTION);
                formatMessage(text, STR_SAVE_PROMPT, &*title);
                int result = MessageBox(hwnd, text, caption, MB_YESNO);
                if (result == IDYES)
                    saveText();
            }
            break; // continue closing as normal
        case WM_CONTEXTMENU: {
            POINT pos = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pos.x == -1 && pos.y == -1) {
                CHARRANGE sel;
                SendMessage(edit, EM_EXGETSEL, 0, (LPARAM)&sel);
                SendMessage(edit, EM_POSFROMCHAR, (WPARAM)&pos, sel.cpMin);
                ClientToScreen(edit, &pos);
            }
            HMENU menu = LoadMenu(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_TEXT_MENU));
            TrackPopupMenuEx(GetSubMenu(menu, 0), TPM_RIGHTBUTTON,
                pos.x, pos.y, hwnd, nullptr);
            DestroyMenu(menu);
            return 0;
        }
        case WM_INITMENUPOPUP: {
            HMENU menu = (HMENU)wParam;
            if (!SendMessage(edit, EM_CANUNDO, 0, 0)) {
                EnableMenuItem(menu, IDM_UNDO, MF_GRAYED);
            } else {
                LocalHeapPtr<wchar_t> undoName, undoMessage;
                undoNameToString((UNDONAMEID)SendMessage(edit, EM_GETUNDONAME, 0, 0), undoName);
                formatMessage(undoMessage, STR_TEXT_UNDO, &*undoName);
                ModifyMenu(menu, IDM_UNDO, MF_STRING, IDM_UNDO, undoMessage);
            }
            if (!SendMessage(edit, EM_CANREDO, 0, 0)) {
                EnableMenuItem(menu, IDM_REDO, MF_GRAYED);
            } else {
                LocalHeapPtr<wchar_t> redoName, redoMessage;
                undoNameToString((UNDONAMEID)SendMessage(edit, EM_GETREDONAME, 0, 0), redoName);
                formatMessage(redoMessage, STR_TEXT_REDO, &*redoName);
                ModifyMenu(menu, IDM_REDO, MF_STRING, IDM_REDO, redoMessage);
            }
            if (SendMessage(edit, EM_SELECTIONTYPE, 0, 0) == SEL_EMPTY) {
                EnableMenuItem(menu, IDM_CUT, MF_GRAYED);
                EnableMenuItem(menu, IDM_COPY, MF_GRAYED);
                EnableMenuItem(menu, IDM_DELETE, MF_GRAYED);
            }
            if (!SendMessage(edit, EM_CANPASTE, 0, 0))
                EnableMenuItem(menu, IDM_PASTE, MF_GRAYED);
            return 0;
        }
    }
    if (message == findReplaceMessage) {
        FINDREPLACE *input = (FINDREPLACE *)lParam;
        if (input->Flags & FR_DIALOGTERM) {
            findReplaceDialog = nullptr;
        } else if (input->Flags & FR_FINDNEXT) {
            FINDTEXTEX findText;
            findText.lpstrText = input->lpstrFindWhat;
            CHARRANGE sel;
            SendMessage(edit, EM_EXGETSEL, 0, (LPARAM)&sel);
            if (input->Flags & FR_DOWN) {
                findText.chrg = {sel.cpMax, -1};
            } else {
                findText.chrg = {sel.cpMin, 0};
            }
            if (SendMessage(edit, EM_FINDTEXTEXW, input->Flags, (LPARAM)&findText) != -1) {
                SendMessage(edit, EM_EXSETSEL, 0, (LPARAM)&findText.chrgText);
            } else {
                MessageBeep(MB_OK);
            }
        }
        return 0;
    }
    return ItemWindow::handleMessage(message, wParam, lParam);
}

bool TextWindow::onCommand(WORD command) {
    switch (command) {
        case IDM_SAVE:
            if (saveText())
                SendMessage(edit, EM_SETMODIFY, FALSE, 0);
            return true;
        case IDM_NEW_LINE:
            newLine();
            return true;
        case IDM_INDENT:
            indentSelection(1);
            return true;
        case IDM_UNINDENT:
            indentSelection(-1);
            return true;
        case IDM_FIND:
            openFindDialog();
            return true;
        case IDM_UNDO:
            SendMessage(edit, EM_UNDO, 0, 0);
            return true;
        case IDM_REDO:
            SendMessage(edit, EM_REDO, 0, 0);
            return true;
        case IDM_CUT:
            SendMessage(edit, WM_CUT, 0, 0);
            return true;
        case IDM_COPY:
            SendMessage(edit, WM_COPY, 0, 0);
            return true;
        case IDM_PASTE:
            SendMessage(edit, WM_PASTE, 0, 0);
            return true;
        case IDM_DELETE:
            SendMessage(edit, WM_CLEAR, 0, 0);
            return true;
        case IDM_SELECT_ALL: {
            CHARRANGE sel = {0, -1};
            SendMessage(edit, EM_EXSETSEL, 0, (LPARAM)&sel);
            return true;
        }
    }
    return ItemWindow::onCommand(command);
}

LRESULT TextWindow::onNotify(NMHDR *nmHdr) {
    if (nmHdr->hwndFrom == edit && nmHdr->code == EN_SELCHANGE && hasStatusText()) {
        updateStatus(((SELCHANGE *)nmHdr)->chrg);
        return 0;
    }
    return ItemWindow::onNotify(nmHdr);
}

void TextWindow::updateStatus(CHARRANGE range) {
    LONG line = 1 + (LONG)SendMessage(edit, EM_LINEFROMCHAR, (WPARAM)-1, 0);
    LONG lineIndex = (LONG)SendMessage(edit, EM_LINEINDEX, (WPARAM)-1, 0);
    LONG col = range.cpMin - lineIndex + 1;
    LocalHeapPtr<wchar_t> status;
    if (range.cpMin == range.cpMax) {
        formatMessage(status, STR_TEXT_STATUS, line, col);
    } else {
        formatMessage(status, STR_TEXT_STATUS_SEL, line, col, range.cpMax - range.cpMin);
    }
    setStatusText(status);
}

void TextWindow::newLine() {
    LONG line = (LONG)SendMessage(edit, EM_LINEFROMCHAR, (WPARAM)-1, 0);
    wchar_t newLineBuffer[1024];
    newLineBuffer[0] = '\n';
    newLineBuffer[1] = _countof(newLineBuffer) - 2; // allow room for null
    LRESULT lineLen = SendMessage(edit, EM_GETLINE, line, (LPARAM)(newLineBuffer + 1));
    int endIndent = 1;
    for (; endIndent < lineLen + 1; endIndent++) {
        if (newLineBuffer[endIndent] != ' ' && newLineBuffer[endIndent] != '\t')
            break;
    }
    newLineBuffer[endIndent] = 0;
    SendMessage(edit, EM_REPLACESEL, TRUE, (LPARAM)newLineBuffer);
}

void TextWindow::indentSelection(int dir) {
    CHARRANGE sel;
    SendMessage(edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    LONG startLine = (LONG)SendMessage(edit, EM_EXLINEFROMCHAR, 0, sel.cpMin);
    LONG endLine = (LONG)SendMessage(edit, EM_EXLINEFROMCHAR, 0, sel.cpMax - 1);
    if (dir == 1) {
        LONG maxLine = (LONG)SendMessage(edit, EM_EXLINEFROMCHAR, 0, sel.cpMax);
        if (startLine == maxLine) {
            SendMessage(edit, EM_REPLACESEL, TRUE, (LPARAM)L"\t");
            return;
        }
    }
    LONG startIndex = (LONG)SendMessage(edit, EM_LINEINDEX, startLine, 0);
    LONG endIndex = (LONG)SendMessage(edit, EM_LINEINDEX, endLine + 1, 0);
    if (endIndex == -1) { // last line
        GETTEXTLENGTHEX getLength = {GTL_NUMCHARS | GTL_PRECISE, 1200};
        endIndex = (LONG)SendMessage(edit, EM_GETTEXTLENGTHEX, (WPARAM)&getLength, 0);
    }
    LONG bufferSize = endIndex - startIndex + 1; // include null
    if (dir == 1)
        bufferSize += endLine - startLine + 1; // enough for tabs on each line
    CComHeapPtr<wchar_t> indentBuffer;
    indentBuffer.Allocate(bufferSize);
    LONG bufferI = 0;
    for (LONG line = startLine; line <= endLine; line++) {
        if (dir == 1)
            indentBuffer[bufferI++] = '\t';
        LONG availableSize = bufferSize - bufferI - 1;
        indentBuffer[bufferI] = availableSize < 65536 ? (wchar_t)availableSize : 65535;
        LONG lineLen = (LONG)SendMessage(edit, EM_GETLINE, line, (LPARAM)(indentBuffer + bufferI));
        if (dir == -1 && lineLen != 0 && indentBuffer[bufferI] == '\t') {
            MoveMemory(indentBuffer + bufferI, indentBuffer + bufferI + 1,
                (lineLen - 1) * sizeof(wchar_t));
            bufferI -= 1;
            if (line == startLine && sel.cpMin > startIndex)
                sel.cpMin -= 1;
            sel.cpMax -= 1;
        }
        bufferI += lineLen;
    }
    indentBuffer[bufferI] = 0;
    CHARRANGE indentRange = {startIndex, endIndex};
    SendMessage(edit, EM_EXSETSEL, 0, (LPARAM)&indentRange);
    SendMessage(edit, EM_REPLACESEL, TRUE, (LPARAM)&*indentBuffer);
    // restore selection, now shifted by tab characters
    if (dir == 1)
        sel = {sel.cpMin + 1, sel.cpMax + (endLine - startLine) + 1};
    SendMessage(edit, EM_EXSETSEL, 0, (LPARAM)&sel);
}

void TextWindow::openFindDialog() {
    if (findReplaceDialog)
        DestroyWindow(findReplaceDialog);
    findReplace = {sizeof(findReplace)};
    findReplace.hwndOwner = hwnd;
    findReplace.Flags = FR_DOWN;
    findBuffer[0] = 0;
    findReplace.lpstrFindWhat = findBuffer;
    findReplace.wFindWhatLen = _countof(findBuffer); // docs are wrong, this is in chars not bytes
    findReplaceDialog = FindText(&findReplace);
}

bool TextWindow::loadText() {
    encoding = FAIL;
    CComHeapPtr<uint8_t> buffer; // null terminated!
    ULONG size;
    {
        CComPtr<IBindCtx> context;
        if (checkHR(CreateBindCtx(0, &context))) {
            BIND_OPTS options = {sizeof(BIND_OPTS), 0, STGM_READ | STGM_SHARE_DENY_NONE, 0};
            checkHR(context->SetBindOptions(&options));
        }
        CComPtr<IStream> stream;
        if (!checkHR(item->BindToHandler(context, BHID_Stream, IID_PPV_ARGS(&stream))))
            return false;
        ULARGE_INTEGER largeSize;
        if (!checkHR(IStream_Size(stream, &largeSize)))
            return false;
        if (largeSize.QuadPart > (ULONGLONG)(ULONG)-1) {
            debugPrintf(L"Too large!\n");
            return false;
        }
        size = (ULONG)largeSize.QuadPart;
        buffer.AllocateBytes(size + 2); // 2 null bytes
        if (!checkHR(IStream_Read(stream, buffer, (ULONG)size)))
            return false;
        buffer[size] = buffer[size + 1] = 0;
    }

    // https://docs.microsoft.com/en-us/windows/win32/intl/using-byte-order-marks
    if (CHECK_BOM(buffer, size, BOM_UTF16LE)) {
        encoding = UTF16LE;
        SendMessage(edit, WM_SETTEXT, 0, (LPARAM)(buffer + sizeof(BOM_UTF16LE)));
    } else if (CHECK_BOM(buffer, size, BOM_UTF16BE)) {
        encoding = UTF16BE;
        wchar_t *wcBuffer = (wchar_t *)(void *)buffer; // includes BOM
        ULONG wcSize = size / sizeof(wchar_t);
        for (ULONG i = 1; i < wcSize; i++)
            wcBuffer[i] = _byteswap_ushort(wcBuffer[i]);
        SendMessage(edit, WM_SETTEXT, 0, (LPARAM)(wcBuffer + 1));
    } else { // assume UTF-8
        uint8_t *utf8String = buffer;
        if (CHECK_BOM(buffer, size, BOM_UTF8BOM)) {
            encoding = UTF8BOM;
            utf8String += sizeof(BOM_UTF8BOM);
        } else {
            encoding = UTF8;
        }
        SETTEXTEX setText = {};
        setText.codepage = CP_UTF8;
        SendMessage(edit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM)utf8String);
    }
    debugPrintf(L"Encoding %d\n", encoding);
    return true;
}

bool TextWindow::saveText() {
    debugPrintf(L"Saving!\n");

    CComHeapPtr<uint8_t> buffer; // not null terminated!
    ULONG size;
    if (encoding == FAIL) {
        return false;
    } else if (encoding == UTF8 || encoding == UTF8BOM) {
        GETTEXTLENGTHEX getLength = {};
        getLength.flags = GTL_USECRLF | GTL_NUMBYTES | GTL_CLOSE; // TODO line endings
        getLength.codepage = CP_UTF8;
        // because we're using GTL_CLOSE, utf8Size may be greater than actual size!
        ULONG utf8Size = (ULONG)SendMessage(edit, EM_GETTEXTLENGTHEX, (WPARAM)&getLength, 0);

        uint8_t *utf8String;
        if (encoding == UTF8BOM) {
            buffer.AllocateBytes(utf8Size + sizeof(BOM_UTF8BOM) + 1); // includes null (not written)
            utf8String = buffer + sizeof(BOM_UTF8BOM);
            CopyMemory(buffer, BOM_UTF8BOM, sizeof(BOM_UTF8BOM));
        } else {
            buffer.AllocateBytes(utf8Size + 1);
            utf8String = buffer;
        }

        GETTEXTEX getText = {};
        getText.cb = utf8Size + 1;
        getText.flags = GT_USECRLF; // TODO line endings
        getText.codepage = CP_UTF8;
        utf8Size = (ULONG)SendMessage(edit, EM_GETTEXTEX, (WPARAM)&getText, (LPARAM)utf8String);
        size = (encoding == UTF8BOM) ? (utf8Size + sizeof(BOM_UTF8BOM)) : utf8Size;
    } else if (encoding == UTF16LE || encoding == UTF16BE) {
        ULONG textLength = (ULONG)SendMessage(edit, WM_GETTEXTLENGTH, 0, 0);
        size = textLength * sizeof(wchar_t) + sizeof(BOM_UTF16LE);
        buffer.AllocateBytes(size + sizeof(wchar_t)); // null character (not written)
        CopyMemory(buffer, (encoding == UTF16BE) ? BOM_UTF16BE : BOM_UTF16LE, sizeof(BOM_UTF16LE));

        wchar_t *wcBuffer = (wchar_t *)(void *)(buffer + sizeof(BOM_UTF16LE)); // not including BOM
        SendMessage(edit, WM_GETTEXT, textLength + 1, (LPARAM)wcBuffer);
        if (encoding == UTF16BE) {
            for (ULONG i = 0; i < textLength; i++)
                wcBuffer[i] = _byteswap_ushort(wcBuffer[i]);
        }
    } else {
        debugPrintf(L"Unknown encoding\n");
        return false;
    }

    CComPtr<IBindCtx> context;
    if (checkHR(CreateBindCtx(0, &context))) {
        BIND_OPTS options = {sizeof(BIND_OPTS), 0,
            STGM_WRITE | STGM_CREATE | STGM_SHARE_DENY_NONE, 0};
        checkHR(context->SetBindOptions(&options));
    }
    CComPtr<IStream> stream;
    if (!checkHR(item->BindToHandler(context, BHID_Stream, IID_PPV_ARGS(&stream))))
        return false;
    if (!checkHR(IStream_Write(stream, buffer, size)))
        return false;
    return true;
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
