#include "TextWindow.h"
#include "GeomUtils.h"
#include "WinUtils.h"
#include "Settings.h"
#include "DPI.h"
#include "UIStrings.h"
#include "resource.h"
#include <cstdint>
#include <windowsx.h>
#include <shlobj.h>
#include <propvarutil.h>

namespace chromafiler {

const wchar_t TEXT_WINDOW_CLASS[] = L"ChromaFile Text";

const ULONG MAX_FILE_SIZE = 50'000'000;

const uint8_t BOM_UTF8BOM[] = {0xEF, 0xBB, 0xBF};
const uint8_t BOM_UTF16LE[] = {0xFF, 0xFE};
const uint8_t BOM_UTF16BE[] = {0xFE, 0xFF};
#define CHECK_BOM(buffer, size, bom) \
    ((size) >= sizeof(bom) && memcmp((buffer), (bom), sizeof(bom)) == 0)

static CComVariant MATCH_SPACE(L" \t");
static CComVariant MATCH_TAB(L"\t");
static CComVariant MATCH_NEWLINE(L"\n");

static HACCEL textAccelTable;
static UINT updateSettingsMessage, findReplaceMessage;

void TextWindow::init() {
    RegisterClass(tempPtr(createWindowClass(TEXT_WINDOW_CLASS)));
    // http://www.jose.it-berater.org/richedit/rich_edit_control.htm
    checkLE(LoadLibrary(L"Msftedit.dll"));

    textAccelTable = LoadAccelerators(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_TEXT_ACCEL));

    updateSettingsMessage = checkLE(RegisterWindowMessage(L"chromafiler_TextUpdateSettings"));
    findReplaceMessage = checkLE(RegisterWindowMessage(FINDMSGSTRING));
}

TextWindow::TextWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item, bool scratch)
        : ItemWindow(parent, item),
          isUnsavedScratchFile(scratch) {
    findBuffer[0] = 0;
    replaceBuffer[0] = 0;
}

const wchar_t * TextWindow::className() {
    return TEXT_WINDOW_CLASS;
}

bool TextWindow::useDefaultStatusText() const {
    return false;
}

SettingsPage TextWindow::settingsStartPage() const {
    return SETTINGS_TEXT;
}

const wchar_t * TextWindow::helpURL() const {
    return L"https://github.com/vanjac/chromafiler/wiki/Text-Editor";
}

void TextWindow::updateAllSettings() {
    // https://stackoverflow.com/q/15987051
    checkLE(SendNotifyMessage(HWND_BROADCAST, updateSettingsMessage, 0, 0));
}

void TextWindow::onCreate() {
    ItemWindow::onCreate();

    logFont = settings::getTextFont();
    updateFont();
    edit = createRichEdit(settings::getTextWrap());

    HRESULT hr;
    if (!checkHR(hr = loadText())) {
        SendMessage(edit, EM_SETOPTIONS, ECOOP_OR, ECO_READONLY);
        if (hasStatusText()) {
            LocalHeapPtr<wchar_t> status;
            formatErrorMessage(status, hr);
            setStatusText(status);
        }
    }
    Edit_SetModify(edit, FALSE);
    if (hasStatusText())
        updateStatus();
}

HWND TextWindow::createRichEdit(bool wordWrap) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL
        | ES_NOHIDESEL | ES_SAVESEL | ES_SELECTIONBAR;
    if (!wordWrap)
        style |= WS_HSCROLL | ES_AUTOHSCROLL;
    HWND control = checkLE(CreateWindow(MSFTEDIT_CLASS, nullptr, style,
        0, 0, 0, 0,
        hwnd, nullptr, GetWindowInstance(hwnd), nullptr));
    SetWindowSubclass(control, richEditProc, 0, (DWORD_PTR)this);
    if (font)
        SendMessage(control, WM_SETFONT, (WPARAM)font, FALSE);
    SendMessage(control, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    SendMessage(control, EM_SETEVENTMASK, 0, ENM_SELCHANGE | ENM_CHANGE);
    SendMessage(control, EM_EXLIMITTEXT, 0, MAX_FILE_SIZE);
    // TODO: SES_XLTCRCRLFTOCR?
    return control;
}

CComPtr<ITextDocument> TextWindow::getTOMDocument() {
    CComPtr<IUnknown> ole;
    if (!SendMessage(edit, EM_GETOLEINTERFACE, 0, (LPARAM)&ole))
        return nullptr;
    CComQIPtr<ITextDocument> document(ole);
    return document;
}

void TextWindow::updateFont() {
    if (font)
        DeleteFont(font);
    LOGFONT scaledLogFont = logFont;
    scaledLogFont.lfHeight = -pointsToPixels(logFont.lfHeight);
    font = CreateFontIndirect(&scaledLogFont);
    if (edit)
        SendMessage(edit, WM_SETFONT, (WPARAM)font, FALSE);
}

bool TextWindow::onCloseRequest() {
    if (encoding != FAIL && Edit_GetModify(edit)) {
        SFGAOF attr;
        if (confirmSave(isUnsavedScratchFile
                || FAILED(item->GetAttributes(SFGAO_VALIDATE, &attr)))) // doesn't exist
            userSave();
    }
    return ItemWindow::onCloseRequest();
}

void TextWindow::onDestroy() {
    if (isUnsavedScratchFile)
        deleteProxy(false);
    if (font)
        DeleteFont(font);
    ItemWindow::onDestroy();
}

void TextWindow::addToolbarButtons(HWND tb) {
    TBBUTTON buttons[] = {
        makeToolbarButton(MDL2_SAVE, IDM_SAVE, 0, 0),
        makeToolbarButton(MDL2_DELETE, IDM_DELETE_PROXY, 0),
    };
    SendMessage(tb, TB_ADDBUTTONS, _countof(buttons), (LPARAM)buttons);
    ItemWindow::addToolbarButtons(tb);
}

int TextWindow::getToolbarTooltip(WORD command) {
    switch (command) {
        case IDM_DELETE_PROXY:
            return IDS_DELETE_COMMAND;
        case IDM_SAVE:
            return IDS_SAVE_COMMAND;
    }
    return ItemWindow::getToolbarTooltip(command);
}

void TextWindow::trackContextMenu(POINT pos) {
    if (SendMessage(edit, EM_GETOPTIONS, 0, 0) & ECO_READONLY) {
        ItemWindow::trackContextMenu(pos);
        return;
    }
    HMENU menu = LoadMenu(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_TEXT_MENU));
    ItemWindow::trackContextMenu(pos, GetSubMenu(menu, 0));
    checkLE(DestroyMenu(menu));
}

void TextWindow::onActivate(WORD state, HWND prevWindow) {
    ItemWindow::onActivate(state, prevWindow);
    if (state != WA_INACTIVE) {
        SetFocus(edit);
    }
}

void TextWindow::onSize(SIZE size) {
    ItemWindow::onSize(size);
    RECT body = windowBody();
    MoveWindow(edit, body.left, body.top, rectWidth(body), rectHeight(body), TRUE);
}

bool TextWindow::handleTopLevelMessage(MSG *msg) {
    // TODO: this will only be handled if the text window is active
    if (findReplaceDialog && IsDialogMessage(findReplaceDialog, msg))
        return true;
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_TAB
            && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0)
        return false; // allow rich edit subclass to handle these
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
        case WM_QUERYENDSESSION:
            if (Edit_GetModify(edit)) {
                userSave();
            } else if (isUnsavedScratchFile) { // empty
                deleteProxy();
                isUnsavedScratchFile = false;
            }
            return TRUE;
        case WM_INITMENUPOPUP: {
            HMENU menu = (HMENU)wParam;
            if (!Edit_CanUndo(edit)) {
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
            if (findBuffer[0] == 0) {
                EnableMenuItem(menu, IDM_FIND_NEXT, MF_GRAYED);
                EnableMenuItem(menu, IDM_FIND_PREV, MF_GRAYED);
            }
            if (isWordWrap())
                CheckMenuItem(menu, IDM_WORD_WRAP, MF_CHECKED);
            return 0;
        }
    }
    if (updateSettingsMessage && message == updateSettingsMessage) {
        bool wordWrap = settings::getTextWrap();
        if (wordWrap != isWordWrap())
            setWordWrap(wordWrap);
        LOGFONT newLogFont = settings::getTextFont();
        if (memcmp(&newLogFont, &logFont, sizeof(logFont)) != 0) {
            debugPrintf(L"Font changed\n");
            logFont = newLogFont;
            updateFont();
        }
        return 0;
    } else if (findReplaceMessage && message == findReplaceMessage) {
        handleFindReplace((FINDREPLACE *)lParam);
        return 0;
    }
    return ItemWindow::handleMessage(message, wParam, lParam);
}

bool TextWindow::onCommand(WORD command) {
    if (encoding == FAIL)
        return ItemWindow::onCommand(command);
    switch (command) {
        case IDM_SAVE:
            userSave();
            return true;
        case IDM_FIND:
            openFindDialog(false);
            return true;
        case IDM_FIND_NEXT:
            if (findBuffer[0] == 0) {
                openFindDialog(false);
            } else {
                findReplace.Flags |= FR_DOWN;
                findNext(&findReplace);
            }
            return true;
        case IDM_FIND_PREV:
            if (findBuffer[0] == 0) {
                openFindDialog(false);
            } else {
                findReplace.Flags &= ~FR_DOWN;
                findNext(&findReplace);
            }
            return true;
        case IDM_REPLACE:
            openFindDialog(true);
            return true;
        case IDM_UNDO:
            Edit_Undo(edit);
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
        case IDM_LINE_SELECT:
            lineSelect();
            return true;
        case IDM_WORD_WRAP: {
            bool wordWrap = !isWordWrap();
            setWordWrap(wordWrap);
            settings::setTextWrap(wordWrap);
            return true;
        }
        case IDM_ZOOM_IN:
            changeFontSize(1);
            return true;
        case IDM_ZOOM_OUT:
            changeFontSize(-1);
            return true;
        case IDM_ZOOM_RESET:
            logFont.lfHeight = settings::DEFAULT_TEXT_FONT.lfHeight;
            updateFont();
            settings::setTextFont(logFont);
    }
    return ItemWindow::onCommand(command);
}

bool TextWindow::onControlCommand(HWND controlHwnd, WORD notif) {
    if (controlHwnd == edit && notif == EN_CHANGE) {
        if (Edit_GetModify(edit))
            setToolbarButtonState(IDM_SAVE, TBSTATE_ENABLED);
    }
    return ItemWindow::onControlCommand(controlHwnd, notif);
}

LRESULT TextWindow::onNotify(NMHDR *nmHdr) {
    if (nmHdr->hwndFrom == edit && nmHdr->code == EN_SELCHANGE && hasStatusText()) {
        updateStatus();
        return 0;
    }
    return ItemWindow::onNotify(nmHdr);
}

void TextWindow::updateStatus() {
    if (encoding == FAIL)
        return;
    CComPtr<ITextDocument> doc = getTOMDocument();
    CComPtr<ITextSelection> sel;
    CComPtr<ITextRange> range;
    if (!doc || !checkHR(doc->GetSelection(&sel)) || !checkHR(sel->GetDuplicate(&range))) return;
    long start = 0, end = 0, line = 0, toStart = 0;
    checkHR(range->GetStart(&start));
    checkHR(range->GetEnd(&end));
    checkHR(range->GetIndex(tomParagraph, &line));
    checkHR(range->StartOf(tomParagraph, tomMove, &toStart));
    LocalHeapPtr<wchar_t> status;
    if (start == end) {
        formatMessage(status, STR_TEXT_STATUS, line, 1 - toStart);
    } else {
        formatMessage(status, STR_TEXT_STATUS_SEL, line, 1 - toStart, end - start);
    }
    setStatusText(status);
}

void TextWindow::userSave() {
    HRESULT hr;
    if (checkHR(hr = saveText())) {
        Edit_SetModify(edit, FALSE);
        setToolbarButtonState(IDM_SAVE, 0);
    } else {
        LocalHeapPtr<wchar_t> message;
        formatErrorMessage(message, hr);
        enableChain(false);
        checkHR(TaskDialog(hwnd, GetModuleHandle(nullptr), title,
            MAKEINTRESOURCE(IDS_SAVE_ERROR), message, TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr));
        enableChain(true);
    }
    isUnsavedScratchFile = false;
}

bool TextWindow::confirmSave(bool willDelete) {
    // alternative to MB_TASKMODAL http://www.verycomputer.com/5_86324e67adeedf52_1.htm
    enableChain(false);

    LocalHeapPtr<wchar_t> text;
    formatMessage(text, willDelete ? STR_DELETE_PROMPT : STR_SAVE_PROMPT, &*title);
    TASKDIALOGCONFIG config = {sizeof(config)};
    config.hInstance = GetModuleHandle(nullptr);
    config.hwndParent = hwnd;
    config.dwFlags = TDF_USE_HICON_MAIN | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = title;
    config.hMainIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
    config.pszMainInstruction = MAKEINTRESOURCE(IDS_UNSAVED_CAPTION);
    config.pszContent = text;
    TASKDIALOG_BUTTON buttons[] = {{IDYES, MAKEINTRESOURCE(IDS_SAVE_BUTTON)},
        {IDNO, MAKEINTRESOURCE(willDelete ? IDS_DELETE_BUTTON : IDS_DONT_SAVE_BUTTON)}};
    config.cButtons = _countof(buttons);
    config.pButtons = buttons;
    int result = 0;
    checkHR(TaskDialogIndirect(&config, &result, nullptr, nullptr));

    enableChain(true);
    return result == IDYES;
}

void TextWindow::changeFontSize(int amount) {
    logFont.lfHeight = max(logFont.lfHeight + amount, 1);
    updateFont();
    settings::setTextFont(logFont);
}

bool TextWindow::isWordWrap() {
    return !(GetWindowLongPtr(edit, GWL_STYLE) & ES_AUTOHSCROLL);
}

void TextWindow::setWordWrap(bool wordWrap) {
    if (encoding == FAIL)
        return;
    // can't use WM_GETTEXTLENGTH because it counts CRLFs instead of LFs
    GETTEXTLENGTHEX getLength = {GTL_NUMCHARS | GTL_PRECISE, 1200};
    LONG textLength = (LONG)SendMessage(edit, EM_GETTEXTLENGTHEX, (WPARAM)&getLength, 0);
    CComHeapPtr<wchar_t> buffer;
    buffer.Allocate(textLength + 1);
    GETTEXTEX getText = {};
    getText.cb = (textLength + 1) * sizeof(wchar_t);
    getText.codepage = 1200;
    SendMessage(edit, EM_GETTEXTEX, (WPARAM)&getText, (LPARAM)&*buffer);

    // other state
    BOOL modify = Edit_GetModify(edit);
    CHARRANGE sel;
    SendMessage(edit, EM_EXGETSEL, 0, (LPARAM)&sel);

    DestroyWindow(edit);
    edit = createRichEdit(wordWrap);
    onSize(clientSize(hwnd));

    SETTEXTEX setText = {ST_UNICODE, 1200};
    SendMessage(edit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM)&*buffer);
    Edit_SetModify(edit, modify);
    SendMessage(edit, EM_EXSETSEL, 0, (LPARAM)&sel);

    SetFocus(edit);
    // weird redraw issue when desktop composition disabled
    RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
}

void TextWindow::newLine() {
    CComPtr<ITextDocument> doc = getTOMDocument();
    CComPtr<ITextSelection> sel;
    CComPtr<ITextRange> range;
    if (!doc || !checkHR(doc->GetSelection(&sel)) || !checkHR(sel->GetDuplicate(&range))) return;
    checkHR(range->StartOf(tomParagraph, tomMove, nullptr));
    checkHR(range->MoveEndWhile(&MATCH_SPACE, tomForward, nullptr));
    CComBSTR indentStr;
    if (!checkHR(range->GetText(&indentStr))) return;

    checkHR(doc->BeginEditCollection()); // requires Windows 8!
    checkHR(sel->TypeText(CComBSTR(L"\n")));
    if (indentStr.Length() != 0)
        checkHR(sel->TypeText(indentStr));
    checkHR(doc->EndEditCollection());
}

void TextWindow::indentSelection(int dir) {
    CComPtr<ITextDocument> doc = getTOMDocument();
    CComPtr<ITextSelection> sel;
    CComPtr<ITextRange> range;
    if (!doc || !checkHR(doc->GetSelection(&sel)) || !checkHR(sel->GetDuplicate(&range))) return;
    long startLine = 0, endLine = 0;
    checkHR(range->GetIndex(tomParagraph, &startLine));
    checkHR(range->Collapse(tomEnd));
    if (dir == 1) {
        checkHR(range->GetIndex(tomParagraph, &endLine));
        if (startLine == endLine) {
            sel->TypeText(CComBSTR(L"\t"));
            return;
        }
    }
    checkHR(range->Move(tomCharacter, -1, nullptr));
    checkHR(range->GetIndex(tomParagraph, &endLine));
    range = nullptr;
    if (!checkHR(sel->GetDuplicate(&range))) return;
    checkHR(range->StartOf(tomParagraph, tomMove, nullptr));

    checkHR(doc->BeginEditCollection());
    for (int line = startLine; line <= endLine; line++) {
        if (dir == 1) {
            checkHR(range->SetText(CComBSTR(L"\t")));
            checkHR(range->Collapse(tomStart));
        } else if (dir == -1) {
            checkHR(range->MoveEndWhile(&MATCH_TAB, 1, nullptr));
            checkHR(range->Delete(tomCharacter, 0, nullptr));
        }
        range->Move(tomParagraph, 1, nullptr);
    }
    checkHR(doc->EndEditCollection());
}

void TextWindow::lineSelect() {
    CComPtr<ITextDocument> doc = getTOMDocument();
    CComPtr<ITextSelection> sel;
    if (!doc || !checkHR(doc->GetSelection(&sel))) return;
    checkHR(sel->Expand(tomParagraph, nullptr));
}

void TextWindow::openFindDialog(bool replace) {
    if (findReplaceDialog)
        DestroyWindow(findReplaceDialog);
    findReplace = {sizeof(findReplace)};
    findReplace.hwndOwner = hwnd;
    findReplace.Flags = FR_DOWN;
    findReplace.wFindWhatLen = _countof(findBuffer); // docs are wrong, this is in chars not bytes
    CHARRANGE sel;
    SendMessage(edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (sel.cpMax != sel.cpMin && sel.cpMax - sel.cpMin < _countof(findBuffer) - 1)
        SendMessage(edit, EM_GETSELTEXT, 0, (LPARAM)findBuffer);
    // otherwise keep previous
    findReplace.lpstrFindWhat = findBuffer;
    findReplace.wReplaceWithLen = _countof(replaceBuffer);
    findReplace.lpstrReplaceWith = replaceBuffer;
    if (replace) {
        findReplaceDialog = ReplaceText(&findReplace);
    } else {
        findReplaceDialog = FindText(&findReplace);
    }
}

void TextWindow::handleFindReplace(FINDREPLACE *input) {
    if (input->Flags & FR_DIALOGTERM) {
        findReplaceDialog = nullptr;
    } else if (input->Flags & FR_FINDNEXT) {
        findNext(input);
    } else if (input->Flags & FR_REPLACE) {
        replace(input);
    } else if (input->Flags & FR_REPLACEALL) {
        replaceAll(input);
    }
}

void TextWindow::findNext(FINDREPLACE *input) {
    CComPtr<ITextDocument> doc = getTOMDocument();
    CComPtr<ITextSelection> sel;
    CComPtr<ITextRange> range;
    if (!doc || !checkHR(doc->GetSelection(&sel)) || !checkHR(sel->GetDuplicate(&range))) return;
    checkHR(range->Collapse((input->Flags & FR_DOWN) ? tomEnd : tomStart));
    long count = (input->Flags & FR_DOWN) ? tomForward : tomBackward;
    long flags = input->Flags & (tomMatchWord | tomMatchCase);
    HRESULT hr;
    checkHR(hr = range->FindText(CComBSTR(input->lpstrFindWhat), count, flags, nullptr));
    if (hr != S_OK) { // wrap around
        if (!(input->Flags & FR_DOWN))
            checkHR(range->EndOf(tomStory, tomMove, nullptr));
        else
            checkHR(range->StartOf(tomStory, tomMove, nullptr));
        checkHR(hr = range->FindText(CComBSTR(input->lpstrFindWhat), count, flags, nullptr));
        if (hr != S_OK) {
            if (hasStatusText()) {
                LocalHeapPtr<wchar_t> status;
                formatMessage(status, STR_TEXT_STATUS_CANT_FIND);
                setStatusText(status);
            }
            MessageBeep(MB_OK);
            return;
        }
    }
    range->Select();
}

void TextWindow::replace(FINDREPLACE *input) {
    CComPtr<ITextDocument> doc = getTOMDocument();
    CComPtr<ITextSelection> sel;
    if (!doc || !checkHR(doc->GetSelection(&sel))) return;
    CComBSTR selText;
    if (checkHR(sel->GetText(&selText))) {
        int compare = (input->Flags & FR_MATCHCASE) ?
            lstrcmp(selText, input->lpstrFindWhat) : lstrcmpi(selText, input->lpstrFindWhat);
        if (compare == 0)
            checkHR(sel->SetText(CComBSTR(input->lpstrReplaceWith)));
    }
    findNext(input);
}

int TextWindow::replaceAll(FINDREPLACE *input) {
    CComPtr<ITextDocument> document = getTOMDocument();
    CComPtr<ITextRange> range;
    if (!document || !checkHR(document->Range(0, 0, &range))) return 0;
    CComBSTR replaceText(input->lpstrReplaceWith);

    checkHR(document->BeginEditCollection());
    int numOccurrences;
    for (numOccurrences = 0; true; numOccurrences++) {
        HRESULT hr;
        checkHR(hr = range->FindText(CComBSTR(input->lpstrFindWhat), tomForward,
            input->Flags & (tomMatchWord | tomMatchCase), nullptr));
        if (hr != S_OK)
            break;
        checkHR(range->SetText(replaceText));
        checkHR(range->Collapse(tomEnd));
    }
    checkHR(document->EndEditCollection());

    if (hasStatusText()) {
        LocalHeapPtr<wchar_t> status;
        if (numOccurrences == 0)
            formatMessage(status, STR_TEXT_STATUS_CANT_FIND);
        else
            formatMessage(status, STR_TEXT_STATUS_REPLACED, numOccurrences);
        setStatusText(status);
    }
    if (numOccurrences == 0)
        MessageBeep(MB_OK);
    return numOccurrences;
}

HRESULT TextWindow::loadText() {
    HRESULT hr;
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
        if (!checkHR(hr = item->BindToHandler(context, BHID_Stream, IID_PPV_ARGS(&stream))))
            return hr;
        ULARGE_INTEGER largeSize;
        if (!checkHR(hr = IStream_Size(stream, &largeSize)))
            return hr;
        if (largeSize.QuadPart > (ULONGLONG)MAX_FILE_SIZE)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        size = (ULONG)largeSize.QuadPart;
        buffer.AllocateBytes(size + 2); // 2 null bytes
        if (!checkHR(hr = IStream_Read(stream, buffer, (ULONG)size)))
            return hr;
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
    return S_OK;
}

HRESULT TextWindow::saveText() {
    debugPrintf(L"Saving!\n");
    HRESULT hr;
    CComPtr<IBindCtx> context;
    if (checkHR(CreateBindCtx(0, &context))) {
        BIND_OPTS options = {sizeof(BIND_OPTS), 0,
            STGM_WRITE | STGM_CREATE | STGM_SHARE_DENY_NONE, 0};
        checkHR(context->SetBindOptions(&options));
    }
    CComPtr<IStream> stream;
    if (!checkHR(hr = item->BindToHandler(context, BHID_Stream, IID_PPV_ARGS(&stream))))
        return hr;

    switch (encoding) {
        case UTF8BOM:
            hr = IStream_Write(stream, BOM_UTF8BOM, sizeof(BOM_UTF8BOM));
            break;
        case UTF16LE:
            hr = IStream_Write(stream, BOM_UTF16LE, sizeof(BOM_UTF16LE));
            break;
        case UTF16BE:
            hr = IStream_Write(stream, BOM_UTF16BE, sizeof(BOM_UTF16BE));
            break;
        default:
            hr = S_OK;
    }
    if (!checkHR(hr))
        return hr;

    WPARAM format = SF_TEXT;
    if (encoding == UTF8 || encoding == UTF8BOM) {
        format |= SF_USECODEPAGE | (CP_UTF8 << 16);
    } else if (encoding == UTF16LE || encoding == UTF16BE) {
        format |= SF_UNICODE;
    }
    StreamInfo info = {stream, encoding};
    EDITSTREAM editStream = {(DWORD_PTR)&info, 0, streamOutCallback};
    SendMessage(edit, EM_STREAMOUT, format, (LPARAM)&editStream);

    return editStream.dwError;
}

DWORD CALLBACK TextWindow::streamOutCallback(DWORD_PTR cookie, LPBYTE buffer,
        LONG numBytes, LONG *bytesWritten) {
    StreamInfo *info = (StreamInfo *)cookie;
    if (info->encoding == UTF16BE) {
        numBytes &= ~1; // only write even number of bytes
        for (int i = 0; i < numBytes; i += 2)
            *(wchar_t *)(buffer + i) = _byteswap_ushort(*(wchar_t *)(buffer + i));
    }
    HRESULT hr;
    if (!checkHR(hr = info->stream->Write(buffer, numBytes, (ULONG *)bytesWritten)))
        return hr;
    return S_OK;
}

int scrollAccumLines(int *scrollAccum) {
    UINT linesPerClick = 3;
    checkLE(SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &linesPerClick, 0));
    float lineDelta = (float)WHEEL_DELTA / linesPerClick;
    int lines = (int)floor(*scrollAccum / lineDelta);
    *scrollAccum -= (int)(lines * lineDelta);
    return lines;
}

LRESULT CALLBACK TextWindow::richEditProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    if (message == WM_MOUSEWHEEL) {
        // override smooth scrolling
        TextWindow *window = (TextWindow *)refData;
        window->vScrollAccum += GET_WHEEL_DELTA_WPARAM(wParam);
        int lines = scrollAccumLines(&window->vScrollAccum);
        if (GetKeyState(VK_CONTROL) < 0) {
            window->changeFontSize(lines); // TODO should not be affected by scroll lines setting
        } else {
            Edit_Scroll(hwnd, -lines, 0);
        }
        return 0;
    } else if (message == WM_MOUSEHWHEEL) {
        TextWindow *window = (TextWindow *)refData;
        window->hScrollAccum += GET_WHEEL_DELTA_WPARAM(wParam);
        int lines = scrollAccumLines(&window->hScrollAccum);
        while (lines > 0) {
            SendMessage(hwnd, WM_HSCROLL, SB_LINERIGHT, 0);
            lines--;
        }
        while (lines < 0) {
            SendMessage(hwnd, WM_HSCROLL, SB_LINELEFT, 0);
            lines++;
        }
        return 0;
    } else if (message == WM_KEYDOWN && wParam == VK_RETURN && settings::getTextAutoIndent()) {
        ((TextWindow *)refData)->newLine();
        return 0;
    } else if (message == WM_CHAR && wParam == VK_TAB) {
        ((TextWindow *)refData)->indentSelection((GetKeyState(VK_SHIFT) < 0) ? -1 : 1);
        return 0;
    } else if (message == WM_CONTEXTMENU) {
        POINT pos = pointFromLParam(lParam);
        if (pos.x == -1 && pos.y == -1) {
            CHARRANGE sel;
            SendMessage(hwnd, EM_EXGETSEL, 0, (LPARAM)&sel);
            SendMessage(hwnd, EM_POSFROMCHAR, (WPARAM)&pos, sel.cpMin);
            pos = clientToScreen(hwnd, pos);
        }
        ((TextWindow *)refData)->trackContextMenu(pos);
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace
