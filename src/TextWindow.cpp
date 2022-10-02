#include "TextWindow.h"
#include "RectUtils.h"
#include "Settings.h"
#include "DPI.h"
#include "UIStrings.h"
#include "resource.h"
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

CComVariant MATCH_SPACE(L" \t");
CComVariant MATCH_TAB(L"\t");
CComVariant MATCH_NEWLINE(L"\n");

static HACCEL textAccelTable;
static UINT updateSettingsMessage, findReplaceMessage;

void TextWindow::init() {
    WNDCLASS wndClass = createWindowClass(TEXT_WINDOW_CLASS);
    RegisterClass(&wndClass);
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
    SendMessage(edit, EM_SETMODIFY, FALSE, 0);
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
        case WM_CLOSE:
            if (encoding != FAIL && SendMessage(edit, EM_GETMODIFY, 0, 0)) {
                SFGAOF attr;
                if (confirmSave(isUnsavedScratchFile
                        || FAILED(item->GetAttributes(SFGAO_VALIDATE, &attr)))) // doesn't exist
                    userSave();
            }
            break; // continue closing as normal
        case WM_QUERYENDSESSION:
            if (SendMessage(edit, EM_GETMODIFY, 0, 0)) {
                userSave();
            } else if (isUnsavedScratchFile) { // empty
                deleteProxy();
                isUnsavedScratchFile = false;
            }
            return TRUE;
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
        if (SendMessage(edit, EM_GETMODIFY, 0, 0))
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
    CComPtr<ITextDocument> document = getTOMDocument();
    if (!document) return;
    CComPtr<ITextSelection> selection;
    if (!checkHR(document->GetSelection(&selection))) return;
    CComPtr<ITextRange> range;
    if (!checkHR(selection->GetDuplicate(&range))) return;
    long start = 0, end = 0, line = 0, col = 0;
    checkHR(range->GetStart(&start));
    checkHR(range->GetEnd(&end));
    checkHR(range->GetIndex(tomParagraph, &line));
    checkHR(range->StartOf(tomParagraph, tomMove, &col));
    col = 1 - col;
    LocalHeapPtr<wchar_t> status;
    if (start == end) {
        formatMessage(status, STR_TEXT_STATUS, line, col);
    } else {
        formatMessage(status, STR_TEXT_STATUS_SEL, line, col, end - start);
    }
    setStatusText(status);
}

void TextWindow::userSave() {
    if (saveText()) {
        SendMessage(edit, EM_SETMODIFY, FALSE, 0);
        setToolbarButtonState(IDM_SAVE, 0);
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

LONG TextWindow::getTextLength() {
    // can't use WM_GETTEXTLENGTH because it counts CRLFs instead of LFs
    GETTEXTLENGTHEX getLength = {GTL_NUMCHARS | GTL_PRECISE, 1200};
    return (LONG)SendMessage(edit, EM_GETTEXTLENGTHEX, (WPARAM)&getLength, 0);
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
    LONG textLength = getTextLength();
    CComHeapPtr<wchar_t> buffer;
    buffer.Allocate(textLength + 1);
    GETTEXTEX getText = {};
    getText.cb = (textLength + 1) * sizeof(wchar_t);
    getText.codepage = 1200;
    SendMessage(edit, EM_GETTEXTEX, (WPARAM)&getText, (LPARAM)&*buffer);

    // other state
    BOOL modify = (BOOL)SendMessage(edit, EM_GETMODIFY, 0, 0);
    CHARRANGE sel;
    SendMessage(edit, EM_EXGETSEL, 0, (LPARAM)&sel);

    DestroyWindow(edit);
    edit = createRichEdit(wordWrap);
    RECT clientRect = {};
    GetClientRect(hwnd, &clientRect);
    onSize(rectWidth(clientRect), rectHeight(clientRect));

    SETTEXTEX setText = {ST_UNICODE, 1200};
    SendMessage(edit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM)&*buffer);
    SendMessage(edit, EM_SETMODIFY, modify, 0);
    SendMessage(edit, EM_EXSETSEL, 0, (LPARAM)&sel);

    SetFocus(edit);
}

void TextWindow::newLine() {
    CComPtr<ITextDocument> document = getTOMDocument();
    if (!document) return;
    CComPtr<ITextSelection> selection;
    if (!checkHR(document->GetSelection(&selection))) return;
    CComPtr<ITextRange> range;
    if (!checkHR(selection->GetDuplicate(&range))) return;
    checkHR(range->StartOf(tomParagraph, tomMove, nullptr));
    checkHR(range->MoveEndWhile(&MATCH_SPACE, tomForward, nullptr));
    CComBSTR indentStr;
    if (!checkHR(range->GetText(&indentStr))) return;

    checkHR(document->BeginEditCollection());
    checkHR(selection->TypeText(CComBSTR(L"\n")));
    if (indentStr.Length() != 0)
        checkHR(selection->TypeText(indentStr));
    checkHR(document->EndEditCollection());
}

void TextWindow::indentSelection(int dir) {
    CComPtr<ITextDocument> document = getTOMDocument();
    if (!document) return;
    CComPtr<ITextSelection> selection;
    if (!checkHR(document->GetSelection(&selection))) return;
    CComPtr<ITextRange> range;
    if (!checkHR(selection->GetDuplicate(&range))) return;
    long startLine = 0, endLine = 0;
    checkHR(range->GetIndex(tomParagraph, &startLine));
    checkHR(range->Collapse(tomEnd));
    if (dir == 1) {
        checkHR(range->GetIndex(tomParagraph, &endLine));
        if (startLine == endLine) {
            selection->TypeText(CComBSTR(L"\t"));
            return;
        }
    }
    checkHR(range->Move(tomCharacter, -1, nullptr));
    checkHR(range->GetIndex(tomParagraph, &endLine));
    range = nullptr;
    if (!checkHR(selection->GetDuplicate(&range))) return;
    checkHR(range->StartOf(tomParagraph, tomMove, nullptr));

    checkHR(document->BeginEditCollection());
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
    checkHR(document->EndEditCollection());
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
        CHARRANGE sel;
        SendMessage(edit, EM_EXGETSEL, 0, (LPARAM)&sel);
        int compare = 1;
        if (sel.cpMax != sel.cpMin && sel.cpMax - sel.cpMin < _countof(findBuffer) - 1) {
            wchar_t selText[_countof(findBuffer)];
            SendMessage(edit, EM_GETSELTEXT, 0, (LPARAM)selText);
            compare = (input->Flags & FR_MATCHCASE) ?
                lstrcmp(selText, input->lpstrFindWhat) : lstrcmpi(selText, input->lpstrFindWhat);
        }
        if (compare == 0)
            SendMessage(edit, EM_REPLACESEL, TRUE, (LPARAM)input->lpstrReplaceWith);
        findNext(input);
    } else if (input->Flags & FR_REPLACEALL) {
        replaceAll(input);
    }
}

void TextWindow::findNext(FINDREPLACE *input) {
    CComPtr<ITextDocument> document = getTOMDocument();
    if (!document) return;
    CComPtr<ITextSelection> selection;
    if (!checkHR(document->GetSelection(&selection))) return;
    long count = (input->Flags & FR_DOWN) ? tomForward : tomBackward;
    long flags = input->Flags & (tomMatchWord | tomMatchCase);
    HRESULT hr;
    checkHR(hr = selection->FindText(CComBSTR(input->lpstrFindWhat), count, flags, nullptr));
    if (hr == S_OK)
        return;
    // wrap around
    CComPtr<ITextRange> range;
    if (!checkHR(document->Range(0, 0, &range))) return;
    if (!(input->Flags & FR_DOWN))
        checkHR(range->EndOf(tomStory, tomMove, nullptr));
    checkHR(hr = range->FindText(CComBSTR(input->lpstrFindWhat), count, flags, nullptr));
    if (hr == S_OK) {
        range->Select();
    } else {
        if (hasStatusText()) {
            LocalHeapPtr<wchar_t> status;
            formatMessage(status, STR_TEXT_STATUS_CANT_FIND);
            setStatusText(status);
        }
        MessageBeep(MB_OK);
    }
}

int TextWindow::replaceAll(FINDREPLACE *input) {
    // count number of occurrences (to determine size of buffer)
    FINDTEXTEX findText;
    findText.lpstrText = input->lpstrFindWhat;
    findText.chrg = {0, -1};
    int numOccurrences = 0;
    while (SendMessage(edit, EM_FINDTEXTEXW, input->Flags, (LPARAM)&findText) != -1) {
        numOccurrences++;
        findText.chrg.cpMin = findText.chrgText.cpMax;
    }
    if (numOccurrences == 0) {
        if (hasStatusText()) {
            LocalHeapPtr<wchar_t> status;
            formatMessage(status, STR_TEXT_STATUS_CANT_FIND);
            setStatusText(status);
        }
        MessageBeep(MB_OK);
        return numOccurrences;
    }

    LONG textLength = getTextLength();
    int findLen = lstrlen(input->lpstrFindWhat);
    int replaceLen = lstrlen(input->lpstrReplaceWith);
    CComHeapPtr<wchar_t> buffer;
    buffer.Allocate(textLength + numOccurrences * (replaceLen - findLen) + 1);

    findText.chrg.cpMin = 0; // search starting location
    LONG bufferI = 0;
    for (int i = 0; i < numOccurrences; i++) {
        SendMessage(edit, EM_FINDTEXTEXW, input->Flags, (LPARAM)&findText);
        CHARRANGE beforeMatch = {findText.chrg.cpMin, findText.chrgText.cpMin};
        SendMessage(edit, EM_EXSETSEL, 0, (LPARAM)&beforeMatch);
        bufferI += (LONG)SendMessage(edit, EM_GETSELTEXT, 0, (LPARAM)(buffer + bufferI));
        CopyMemory(buffer + bufferI, input->lpstrReplaceWith, replaceLen * sizeof(wchar_t));
        bufferI += replaceLen;
        findText.chrg.cpMin = findText.chrgText.cpMax; // search after match
    }
    CHARRANGE afterLastMatch = {findText.chrg.cpMin, textLength};
    SendMessage(edit, EM_EXSETSEL, 0, (LPARAM)&afterLastMatch);
    bufferI += (LONG)SendMessage(edit, EM_GETSELTEXT, 0, (LPARAM)(buffer + bufferI));

    SETTEXTEX setText = {ST_KEEPUNDO | ST_UNICODE, 1200};
    SendMessage(edit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM)&*buffer);

    if (hasStatusText()) {
        LocalHeapPtr<wchar_t> status;
        formatMessage(status, STR_TEXT_STATUS_REPLACED, numOccurrences);
        setStatusText(status);
    }
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
            SendMessage(hwnd, EM_LINESCROLL, 0, -lines);
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
        if (SendMessage(hwnd, EM_GETOPTIONS, 0, 0) & ECO_READONLY)
            return 0;
        POINT pos = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (pos.x == -1 && pos.y == -1) {
            CHARRANGE sel;
            SendMessage(hwnd, EM_EXGETSEL, 0, (LPARAM)&sel);
            SendMessage(hwnd, EM_POSFROMCHAR, (WPARAM)&pos, sel.cpMin);
            ClientToScreen(hwnd, &pos);
        }
        HMENU menu = LoadMenu(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_TEXT_MENU));
        checkLE(TrackPopupMenuEx(GetSubMenu(menu, 0), TPM_RIGHTBUTTON,
            pos.x, pos.y, GetParent(hwnd), nullptr));
        checkLE(DestroyMenu(menu));
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace
