#include "TextWindow.h"
#include "GeomUtils.h"
#include "WinUtils.h"
#include "Settings.h"
#include "DPI.h"
#include "UIStrings.h"
#include <cstdint>
#include <windowsx.h>
#include <shlobj.h>
#include <propkey.h>
#include <propvarutil.h>

namespace chromafiler {

const wchar_t PROP_WORD_WRAP[] = L"WordWrap";

const ULONG MAX_FILE_SIZE = 50'000'000;

const UINT CP_UTF16LE = 1200;

const uint8_t BOM_UTF8BOM[] = {0xEF, 0xBB, 0xBF};
const uint8_t BOM_UTF16LE[] = {0xFF, 0xFE};
const uint8_t BOM_UTF16BE[] = {0xFE, 0xFF};
#define CHECK_BOM(buffer, size, bom) \
    ((size) >= sizeof(bom) && memcmp((buffer), (bom), sizeof(bom)) == 0)

static CComVariant MATCH_SPACE(L" \t");
static CComVariant MATCH_TAB(L"\t");
static CComVariant MATCH_NEWLINE(L"\n");

static wchar_t textExePath[MAX_PATH];
static HACCEL textAccelTable;
static UINT updateSettingsMessage, findReplaceMessage;
static HMODULE hMsftedit = nullptr;

void TextWindow::init() {
    textAccelTable = LoadAccelerators(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_TEXT_ACCEL));

    updateSettingsMessage = checkLE(RegisterWindowMessage(L"chromafiler_TextUpdateSettings"));
    findReplaceMessage = checkLE(RegisterWindowMessage(FINDMSGSTRING));

    if (checkLE(GetModuleFileName(nullptr, textExePath, _countof(textExePath)))) {
        PathRemoveFileSpec(textExePath);
        PathAppend(textExePath, L"ChromaText.exe");
        if (!PathFileExists(textExePath)) {
            debugPrintf(L"Couldn't find ChromaText.exe\n");
            textExePath[0] = 0;
        }
    } else {
        textExePath[0] = 0;
    }
}

TextWindow::TextWindow(ItemWindow *const parent, IShellItem *const item)
        : ItemWindow(parent, item) {
    findBuffer[0] = 0;
    replaceBuffer[0] = 0;
}

const wchar_t * TextWindow::appUserModelID() const {
    return L"chroma.text";
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

void TextWindow::updateWindowPropStore(IPropertyStore *const propStore) {
    if (!textExePath[0]) {
        ItemWindow::updateWindowPropStore(propStore);
        return;
    }
    // matches ChromaText.lnk created by the installer
    // these properties will be used if ChromaText was not added to the start menu
    propStoreWriteString(propStore, PKEY_AppUserModel_ID, appUserModelID());
    propStoreWriteString(propStore, PKEY_AppUserModel_RelaunchCommand, textExePath);
    propStoreWriteString(propStore, PKEY_AppUserModel_RelaunchDisplayNameResource,
        getString(IDS_CHROMATEXT));
    local_wstr_ptr iconResource = format(L"%1,-101", textExePath);
    propStoreWriteString(propStore, PKEY_AppUserModel_RelaunchIconResource, iconResource.get());
}

void TextWindow::clearViewState(IPropertyBag *const bag, uint32_t mask) {
    ItemWindow::clearViewState(bag, mask);
    CComVariant empty;
    if (mask & (1 << STATE_WORD_WRAP))
        checkHR(bag->Write(PROP_WORD_WRAP, &empty));
}

void TextWindow::writeViewState(IPropertyBag *const bag, uint32_t mask) {
    ItemWindow::writeViewState(bag, mask);
    if (mask & (1 << STATE_WORD_WRAP)) {
        CComVariant wordWrapVar(isWordWrap());
        checkHR(bag->Write(PROP_WORD_WRAP, &wordWrapVar));
    }
}

void TextWindow::updateAllSettings() {
    // https://stackoverflow.com/q/15987051
    checkLE(SendNotifyMessage(HWND_BROADCAST, updateSettingsMessage, 0, 0));
}

void TextWindow::onCreate() {
    ItemWindow::onCreate();

    // http://www.jose.it-berater.org/richedit/rich_edit_control.htm
    // https://devblogs.microsoft.com/math-in-office/richedit-hot-keys/
    if (!hMsftedit)
        hMsftedit = checkLE(LoadLibrary(L"Msftedit.dll"));

    logFont = settings::getTextFont();
    updateFont();
    auto bag = getPropBag();
    VARIANT wordWrapVar = {VT_BOOL};
    if (!(bag && SUCCEEDED(bag->Read(PROP_WORD_WRAP, &wordWrapVar, nullptr))))
        wordWrapVar.boolVal = settings::getTextWrap();
    edit = createRichEdit(true, wordWrapVar.boolVal);
    setStatusText(getString(IDS_TEXT_LOADING));

    loadThread.Attach(new LoadThread(item, this));
    loadThread->start();
}

static void applyEditFont(HWND edit, HFONT font) {
    if (edit && font) {
        SendMessage(edit, WM_SETFONT, (WPARAM)font, FALSE);
        Edit_SetTabStops(edit, 1, tempPtr(4 * settings::getTextTabWidth()));
    }
}

HWND TextWindow::createRichEdit(bool readOnly, bool wordWrap) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL
        | ES_NOHIDESEL | ES_SAVESEL | ES_SELECTIONBAR;
    if (readOnly)
        style |= ES_READONLY;
    if (!wordWrap)
        style |= WS_HSCROLL | ES_AUTOHSCROLL;
    HWND control = checkLE(CreateWindow(MSFTEDIT_CLASS, nullptr, style,
        0, 0, 0, 0,
        hwnd, nullptr, GetWindowInstance(hwnd), nullptr));
    SetWindowSubclass(control, richEditProc, 0, (DWORD_PTR)this);
    applyEditFont(control, font);
    SendMessage(control, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    SendMessage(control, EM_SETEVENTMASK, 0, ENM_SELCHANGE | ENM_CHANGE);
    SendMessage(control, EM_EXLIMITTEXT, 0, MAX_FILE_SIZE);
    SendMessage(control, EM_SETEDITSTYLE, SES_XLTCRCRLFTOCR, SES_XLTCRCRLFTOCR);
    return control;
}

bool TextWindow::isEditable() {
    return !(SendMessage(edit, EM_GETOPTIONS, 0, 0) & ECO_READONLY);
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
    applyEditFont(edit, font);
}

bool TextWindow::onCloseRequest() {
    if (isEditable() && Edit_GetModify(edit)) {
        SFGAOF attr;
        if (confirmSave(isScratch()
                || FAILED(item->GetAttributes(SFGAO_VALIDATE, &attr)))) // doesn't exist
            userSave();
    }
    return ItemWindow::onCloseRequest();
}

void TextWindow::onDestroy() {
    ItemWindow::onDestroy();
    if (font)
        DeleteFont(font);
    if (loadThread)
        loadThread->stop();
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
    if (!isEditable()) {
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
    updateEditSize();
}

void TextWindow::updateEditSize() {
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

static const wchar_t * undoNameToString(UNDONAMEID id) {
    if (id > UID_AUTOTABLE)
        id = UID_UNKNOWN;
    return getString(id + IDS_TEXT_UNDO_UNKNOWN);
}

LRESULT TextWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case MSG_LOAD_COMPLETE: {
            AcquireSRWLockExclusive(&asyncLoadResultLock);
            LoadResult result = std::move(asyncLoadResult);
            asyncLoadResult = {};
            ReleaseSRWLockExclusive(&asyncLoadResultLock);
            if (result.textStart) {
                SendMessage(edit, EM_SETTEXTEX, (WPARAM)&result.setText, (LPARAM)result.textStart);
                Edit_SetReadOnly(edit, FALSE);
                CHARRANGE sel = {0, 0};
                SendMessage(edit, EM_EXSETSEL, 0, (LPARAM)&sel);
                Edit_SetModify(edit, FALSE);
                setToolbarButtonState(IDM_SAVE, 0);
                detectEncoding = result.encoding;
                detectNewlines = result.newlines;
                debugPrintf(L"Detected encoding %d\n", detectEncoding);
                debugPrintf(L"Detected newlines %d\n", detectNewlines);
                updateStatus();
            }
            return 0;
        }
        case MSG_LOAD_FAIL:
            if (hasStatusText())
                setStatusText(getErrorMessage((HRESULT)wParam).get());
            return 0;
        case WM_QUERYENDSESSION:
            if (isEditable() && Edit_GetModify(edit)) {
                userSave();
            } else if (isScratch()) { // empty
                deleteProxy();
            }
            return TRUE;
        case WM_INITMENUPOPUP: {
            HMENU menu = (HMENU)wParam;
            if (!Edit_GetModify(edit))
                EnableMenuItem(menu, IDM_SAVE, MF_GRAYED);
            if (!Edit_CanUndo(edit)) {
                EnableMenuItem(menu, IDM_UNDO, MF_GRAYED);
            } else {
                UNDONAMEID undoId = (UNDONAMEID)SendMessage(edit, EM_GETUNDONAME, 0, 0);
                local_wstr_ptr undoMessage = formatString(IDS_TEXT_UNDO, undoNameToString(undoId));
                ModifyMenu(menu, IDM_UNDO, MF_STRING, IDM_UNDO, undoMessage.get());
            }
            if (!SendMessage(edit, EM_CANREDO, 0, 0)) {
                EnableMenuItem(menu, IDM_REDO, MF_GRAYED);
            } else {
                UNDONAMEID redoId = (UNDONAMEID)SendMessage(edit, EM_GETREDONAME, 0, 0);
                local_wstr_ptr redoMessage = formatString(IDS_TEXT_REDO, undoNameToString(redoId));
                ModifyMenu(menu, IDM_REDO, MF_STRING, IDM_REDO, redoMessage.get());
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
        LOGFONT newLogFont = settings::getTextFont();
        if (memcmp(&newLogFont, &logFont, sizeof(logFont)) != 0) {
            debugPrintf(L"Font changed\n");
            logFont = newLogFont;
            updateFont();
        } else {
            applyEditFont(edit, font); // for tab size
        }
        return 0;
    } else if (findReplaceMessage && message == findReplaceMessage) {
        handleFindReplace((FINDREPLACE *)lParam);
        return 0;
    }
    return ItemWindow::handleMessage(message, wParam, lParam);
}

bool TextWindow::onCommand(WORD command) {
    if (!isEditable())
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
            viewStateDirty(1 << STATE_WORD_WRAP);
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
            return true;
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
    if (nmHdr->hwndFrom == edit && nmHdr->code == EN_SELCHANGE) {
        updateStatus();
        return 0;
    }
    return ItemWindow::onNotify(nmHdr);
}

void TextWindow::updateStatus() {
    if (!hasStatusText() || !isEditable())
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
    local_wstr_ptr status;
    if (start == end) {
        status = formatString(IDS_TEXT_STATUS, line, 1 - toStart);
    } else {
        status = formatString(IDS_TEXT_STATUS_SEL, line, 1 - toStart, end - start);
    }
    setStatusText(status.get());
}

void TextWindow::userSave() {
    HRESULT hr;
    if (checkHR(hr = saveText())) {
        Edit_SetModify(edit, FALSE);
        setToolbarButtonState(IDM_SAVE, 0);
    } else {
        enableChain(false);
        checkHR(TaskDialog(hwnd, GetModuleHandle(nullptr), title, MAKEINTRESOURCE(IDS_SAVE_ERROR),
            getErrorMessage(hr).get(), TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr));
        enableChain(true);
    }
    onModify();
}

bool TextWindow::confirmSave(bool willDelete) {
    // alternative to MB_TASKMODAL http://www.verycomputer.com/5_86324e67adeedf52_1.htm
    enableChain(false);

    local_wstr_ptr text = formatString(willDelete ? IDS_DELETE_PROMPT : IDS_SAVE_PROMPT, &*title);
    TASKDIALOGCONFIG config = {sizeof(config)};
    config.hInstance = GetModuleHandle(nullptr);
    config.hwndParent = hwnd;
    config.dwFlags = TDF_USE_HICON_MAIN | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = title;
    config.hMainIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
    config.pszMainInstruction = MAKEINTRESOURCE(IDS_UNSAVED_CAPTION);
    config.pszContent = text.get();
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
    if (!isEditable())
        return;
    // can't use WM_GETTEXTLENGTH because it counts CRLFs instead of LFs
    GETTEXTLENGTHEX getLength = {GTL_NUMCHARS | GTL_PRECISE, CP_UTF16LE};
    LONG textLength = (LONG)SendMessage(edit, EM_GETTEXTLENGTHEX, (WPARAM)&getLength, 0);
    wstr_ptr buffer(new wchar_t[textLength + 1]);
    GETTEXTEX getText = {};
    getText.cb = (textLength + 1) * sizeof(wchar_t);
    getText.codepage = CP_UTF16LE;
    SendMessage(edit, EM_GETTEXTEX, (WPARAM)&getText, (LPARAM)buffer.get());

    // other state
    BOOL modify = Edit_GetModify(edit);
    CHARRANGE sel;
    SendMessage(edit, EM_EXGETSEL, 0, (LPARAM)&sel);

    DestroyWindow(edit);
    edit = createRichEdit(false, wordWrap);
    updateEditSize();

    SETTEXTEX setText = {ST_UNICODE, CP_UTF16LE};
    SendMessage(edit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM)buffer.get());
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
            setStatusText(getString(IDS_TEXT_CANT_FIND));
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
        if (numOccurrences == 0) {
            setStatusText(getString(IDS_TEXT_CANT_FIND));
        } else {
            setStatusText(formatString(IDS_TEXT_STATUS_REPLACE, numOccurrences).get());
        }
    }
    if (numOccurrences == 0)
        MessageBeep(MB_OK);
    return numOccurrences;
}

template<typename T>
TextNewlines detectNewlineType(T *start, T*end) {
    for (T *c = start; c < end; c++) {
        if (*c == '\n') {
            return NL_LF;
        } else if (*c == '\r') {
            if (*(c + 1) == '\n') // this is safe since buffer has extra null character
                return NL_CRLF;
            else
                return NL_CR;
        }
    }
    return NL_UNK;
}

HRESULT TextWindow::loadText(IShellItem *const item, LoadResult *result) {
    HRESULT hr;
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
        result->buffer = std::unique_ptr<uint8_t[]>(new uint8_t[size + 2]); // 2 null bytes
        if (!checkHR(hr = IStream_Read(stream, result->buffer.get(), (ULONG)size)))
            return hr;
        result->buffer[size] = result->buffer[size + 1] = 0;
    }

    // https://docs.microsoft.com/en-us/windows/win32/intl/using-byte-order-marks
    if (CHECK_BOM(result->buffer.get(), size, BOM_UTF16BE)) {
        result->encoding = ENC_UTF16BE;
    } else if (CHECK_BOM(result->buffer.get(), size, BOM_UTF16LE)) {
        result->encoding = ENC_UTF16LE;
    } else if (CHECK_BOM(result->buffer.get(), size, BOM_UTF8BOM)) {
        result->encoding = ENC_UTF8BOM;
    } else if (size > 0) {
        result->encoding = ENC_UTF8; // could be ANSI!
    } else {
        result->encoding = ENC_UNK;
    }

    if (result->encoding == ENC_UTF16BE || result->encoding == ENC_UTF16LE) {
        wchar_t *wcString = ((wchar_t *)(void *)result->buffer.get()) + 1; // skip BOM
        wchar_t *wcEnd = (wchar_t *)(void *)(result->buffer.get() + size);
        for (wchar_t *c = wcString; c < wcEnd; c++) {
            if (*c == 0)
                *c = L' ';
            else if (result->encoding == ENC_UTF16BE)
                *c = _byteswap_ushort(*c);
        }
        result->newlines = detectNewlineType(wcString, wcEnd);
        result->textStart = (uint8_t *)wcString;
        result->setText = {ST_UNICODE, CP_UTF16LE};
    } else { // UTF-8 or ANSI
        result->textStart = result->buffer.get() +
            ((result->encoding == ENC_UTF8BOM) ? sizeof(BOM_UTF8BOM) : 0);
        // replace null bytes with spaces and validate UTF-8 encoding
        // https://en.wikipedia.org/wiki/UTF-8#Encoding
        // TODO: check for overlong encodings and invalid code points
        uint8_t *textEnd = result->buffer.get() + size;
        char continuation = 0; // num continuation bytes remaining in code point
        for (uint8_t *c = result->textStart; c < textEnd; c++) {
            if (*c == 0)
                *c = ' ';
            if (result->encoding == ENC_UTF8) {
                if (*c & 0x80) {
                    if (*c & 0x40) { // leading byte
                        if (continuation) // incomplete code point
                            result->encoding = ENC_ANSI;
                        else if (!(*c & 0x20))
                            continuation = 1;
                        else if (!(*c & 0x10))
                            continuation = 2;
                        else if (!(*c & 0x0F))
                            continuation = 3;
                        else // invalid byte
                            result->encoding = ENC_ANSI;
                    } else if (continuation) {
                        continuation--;
                    } else {
                        result->encoding = ENC_ANSI; // unexpected continuation
                    }
                } else if (continuation) { // incomplete code point
                    result->encoding = ENC_ANSI;
                }
            }
        }
        if (continuation) // incomplete code point
            result->encoding = ENC_ANSI;

        result->newlines = detectNewlineType(result->textStart, textEnd);
        result->setText.codepage = (result->encoding == ENC_ANSI) ?
            settings::getTextAnsiCodepage() : CP_UTF8;
    }
    return S_OK;
}

HRESULT TextWindow::saveText() {
    debugPrintf(L"Saving!\n");

    TextEncoding saveEncoding = detectEncoding;
    if (saveEncoding == ENC_UNK || !settings::getTextAutoEncoding())
        saveEncoding = settings::getTextDefaultEncoding();
    bool isUtf16 = saveEncoding == ENC_UTF16LE || saveEncoding == ENC_UTF16BE;
    TextNewlines saveNewlines = detectNewlines;
    if (saveNewlines == NL_UNK || !settings::getTextAutoNewlines())
        saveNewlines = settings::getTextDefaultNewlines();

    GETTEXTLENGTHEX getLength = {};
    getLength.flags = (isUtf16 ? GTL_NUMCHARS : GTL_NUMBYTES) | GTL_CLOSE;
    if (saveNewlines == NL_CRLF) getLength.flags |= GTL_USECRLF;
    if (isUtf16)
        getLength.codepage = CP_UTF16LE; // 1201 (big-endian) doesn't work!
    else if (saveEncoding == ENC_ANSI)
        getLength.codepage = settings::getTextAnsiCodepage();
    else
        getLength.codepage = CP_UTF8;
    // may be greater than actual size, because we're using GTL_CLOSE
    ULONG numChars = (ULONG)SendMessage(edit, EM_GETTEXTLENGTHEX, (WPARAM)&getLength, 0);
    if (numChars == E_INVALIDARG)
        return (HRESULT)numChars;

    ULONG bufSize = isUtf16 ? ((numChars + 1) * sizeof(wchar_t)) : (numChars + 1); // room for null
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[bufSize]);

    GETTEXTEX getText = {};
    getText.cb = bufSize;
    getText.flags = (saveNewlines == NL_CRLF) ? GT_USECRLF : 0;
    getText.codepage = getLength.codepage;
    numChars = (ULONG)SendMessage(edit, EM_GETTEXTEX, (WPARAM)&getText, (LPARAM)buffer.get());
    ULONG writeLen = isUtf16 ? (numChars * sizeof(wchar_t)) : numChars;

    if (saveEncoding == ENC_UTF16BE) {
        for (wchar_t *c = (wchar_t *)buffer.get(), *end = c + numChars; c < end; c++) {
            if (saveNewlines == NL_LF && *c == L'\r')
                *c = 0x0A00;
            else
                *c = _byteswap_ushort(*c);
        }
    } else if (saveEncoding == ENC_UTF16LE && saveNewlines == NL_LF) {
        for (wchar_t *c = (wchar_t *)buffer.get(), *end = c + numChars; c < end; c++) {
            if (*c == L'\r') *c = L'\n';
        }
    } else if (saveNewlines == NL_LF) {
        for (uint8_t *c = buffer.get(); c < buffer.get() + numChars; c++) {
            if (*c == '\r') *c = '\n';
        }
    }

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

    switch (saveEncoding) {
        case ENC_UTF8BOM:   hr = IStream_Write(stream, BOM_UTF8BOM, sizeof(BOM_UTF8BOM));   break;
        case ENC_UTF16LE:   hr = IStream_Write(stream, BOM_UTF16LE, sizeof(BOM_UTF16LE));   break;
        case ENC_UTF16BE:   hr = IStream_Write(stream, BOM_UTF16BE, sizeof(BOM_UTF16BE));   break;
        default:            hr = S_OK;
    }
    if (!checkHR(hr))
        return hr;

    if (!checkHR(hr = IStream_Write(stream, buffer.get(), writeLen)))
        return hr;

    detectEncoding = saveEncoding;
    detectNewlines = saveNewlines;
    return S_OK;
}

TextWindow::LoadThread::LoadThread(IShellItem *const item, TextWindow *const callbackWindow)
        : callbackWindow(callbackWindow) {
    checkHR(SHGetIDListFromObject(item, &itemIDList));
}

void TextWindow::LoadThread::run() {
    CComPtr<IShellItem> localItem;
    if (!itemIDList || !checkHR(SHCreateItemFromIDList(itemIDList, IID_PPV_ARGS(&localItem))))
        return;
    itemIDList.Free();

    LoadResult result;
    HRESULT hr = loadText(localItem, &result);

    AcquireSRWLockExclusive(&stopLock);
    if (!isStopped()) {
        if (checkHR(hr)) {
            AcquireSRWLockExclusive(&callbackWindow->asyncLoadResultLock);
            callbackWindow->asyncLoadResult = std::move(result);
            ReleaseSRWLockExclusive(&callbackWindow->asyncLoadResultLock);
            PostMessage(callbackWindow->hwnd, MSG_LOAD_COMPLETE, 0, 0);
        } else {
            PostMessage(callbackWindow->hwnd, MSG_LOAD_FAIL, hr, 0);
        }
    }
    ReleaseSRWLockExclusive(&stopLock);
}

static int scrollAccumLines(int *scrollAccum) {
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
