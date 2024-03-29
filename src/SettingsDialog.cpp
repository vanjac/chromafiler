#include "SettingsDialog.h"
#include "Settings.h"
#include "TextWindow.h"
#include "TrayWindow.h"
#include "CreateItemWindow.h"
#include "Update.h"
#include "main.h"
#include "WinUtils.h"
#include "UIStrings.h"
#include "DPI.h"
#include <atlbase.h>
#include <prsht.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <commdlg.h>

namespace chromafiler {

const wchar_t *SPECIAL_PATHS[] = {
    L"shell:Desktop",
    L"shell:MyComputerFolder",
    L"shell:Links",
    L"shell:Recent",
    L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}" // Quick access
};

static HWND settingsDialog = nullptr;

bool chooseFolder(HWND owner, CComHeapPtr<wchar_t> &pathOut) {
    CComPtr<IFileOpenDialog> openDialog;
    if (!checkHR(openDialog.CoCreateInstance(__uuidof(FileOpenDialog))))
        return false;
    FILEOPENDIALOGOPTIONS opts;
    if (!checkHR(openDialog->GetOptions(&opts)))
        return false;
    if (!checkHR(openDialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_ALLNONSTORAGEITEMS)))
        return false;
    if (!checkHR(openDialog->Show(GetParent(owner))))
        return false;
    CComPtr<IShellItem> item;
    if (!checkHR(openDialog->GetResult(&item)))
        return false;
    return checkHR(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &pathOut));
}

void setCBPath(HWND hwnd, const wchar_t *path) {
    COMBOBOXEXITEM item = {CBEIF_TEXT, -1, (wchar_t *)path};
    SendMessage(hwnd, CBEM_SETITEM, 0, (LPARAM)&item);
}

LRESULT CALLBACK pathCBProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR) {
    switch (message) {
        case WM_DROPFILES: {
            wchar_t path[MAX_PATH];
            if (DragQueryFile((HDROP)wParam, 0, path, _countof(path))) {
                setCBPath(hwnd, path);
                SendMessage(GetParent(hwnd), WM_COMMAND,
                    MAKEWPARAM(GetDlgCtrlID(hwnd), CBN_SELCHANGE), (LPARAM)hwnd);
            }
            DragFinish((HDROP)wParam);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void setupPathCB(HWND hwnd, const wchar_t *path) {
    checkHR(SHAutoComplete((HWND)SendMessage(hwnd, CBEM_GETEDITCONTROL, 0, 0), SHACF_FILESYS_DIRS));
    SetWindowSubclass(hwnd, pathCBProc, 0, 0);
    DragAcceptFiles(hwnd, TRUE); // TODO: this doesn't work between processes!
    setCBPath(hwnd, path);
}

bool pathCBChanged(WPARAM wParam, LPARAM lParam) {
    return HIWORD(wParam) == CBN_EDITCHANGE && SendMessage((HWND)lParam, CBEM_HASEDITCHANGED, 0, 0)
        || HIWORD(wParam) == CBN_SELCHANGE;
}

INT_PTR CALLBACK generalProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            for (int i = 0; i < _countof(SPECIAL_PATHS); i++) {
                COMBOBOXEXITEM item = {CBEIF_TEXT, -1, (wchar_t *)SPECIAL_PATHS[i]};
                SendDlgItemMessage(hwnd, IDC_START_FOLDER_PATH, CBEM_INSERTITEM, 0, (LPARAM)&item);
            }
            setupPathCB(GetDlgItem(hwnd, IDC_START_FOLDER_PATH),
                settings::getStartingFolder().get());
            SIZE folderWindowSize = settings::getFolderWindowSize();
            SetDlgItemInt(hwnd, IDC_FOLDER_WINDOW_WIDTH, folderWindowSize.cx, TRUE);
            SetDlgItemInt(hwnd, IDC_FOLDER_WINDOW_HEIGHT, folderWindowSize.cy, TRUE);
            SIZE itemWindowSize = settings::getItemWindowSize();
            SetDlgItemInt(hwnd, IDC_ITEM_WINDOW_WIDTH, itemWindowSize.cx, TRUE);
            SetDlgItemInt(hwnd, IDC_ITEM_WINDOW_HEIGHT, itemWindowSize.cy, TRUE);
            SetDlgItemInt(hwnd, IDC_SELECTION_DELAY, settings::getOpenSelectionTime(), FALSE);
            SendDlgItemMessage(hwnd, IDC_SELECTION_DELAY_UD, UDM_SETRANGE32,
                USER_TIMER_MINIMUM, 999);
            CheckDlgButton(hwnd, IDC_STATUS_TEXT_ENABLED,
                settings::getStatusTextEnabled() ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_TOOLBAR_ENABLED,
                settings::getToolbarEnabled() ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_PREVIEWS_ENABLED,
                settings::getPreviewsEnabled() ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        case WM_NOTIFY: {
            NMHDR *notif = (NMHDR *)lParam;
            if (notif->code == PSN_KILLACTIVE) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, FALSE);
                return TRUE;
            } else if (notif->code == PSN_APPLY) {
                wchar_t startingFolder[MAX_PATH];
                if (GetDlgItemText(hwnd, IDC_START_FOLDER_PATH,
                        startingFolder, _countof(startingFolder)))
                    settings::setStartingFolder(startingFolder);
                BOOL success;
                SIZE size;
                size.cx = GetDlgItemInt(hwnd, IDC_FOLDER_WINDOW_WIDTH, &success, TRUE);
                if (success) {
                    size.cy = GetDlgItemInt(hwnd, IDC_FOLDER_WINDOW_HEIGHT, &success, TRUE);
                    if (success)
                        settings::setFolderWindowSize(size);
                }
                size.cx = GetDlgItemInt(hwnd, IDC_ITEM_WINDOW_WIDTH, &success, TRUE);
                if (success) {
                    size.cy = GetDlgItemInt(hwnd, IDC_ITEM_WINDOW_HEIGHT, &success, TRUE);
                    if (success)
                        settings::setItemWindowSize(size);
                }
                int selectionDelay = GetDlgItemInt(hwnd, IDC_SELECTION_DELAY, &success, FALSE);
                if (success) {
                    if (selectionDelay < USER_TIMER_MINIMUM) {
                        selectionDelay = USER_TIMER_MINIMUM;
                        SetDlgItemInt(hwnd, IDC_SELECTION_DELAY, selectionDelay, FALSE);
                    }
                    settings::setOpenSelectionTime(selectionDelay);
                }
                settings::setStatusTextEnabled(!!IsDlgButtonChecked(hwnd, IDC_STATUS_TEXT_ENABLED));
                settings::setToolbarEnabled(!!IsDlgButtonChecked(hwnd, IDC_TOOLBAR_ENABLED));
                settings::setPreviewsEnabled(!!IsDlgButtonChecked(hwnd, IDC_PREVIEWS_ENABLED));
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            } else if (notif->code == PSN_HELP) {
                ShellExecute(nullptr, L"open",
                    L"https://github.com/vanjac/chromafiler/wiki/Settings#general",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_START_FOLDER_BROWSE && HIWORD(wParam) == BN_CLICKED) {
                CComHeapPtr<wchar_t> selected;
                if (chooseFolder(GetParent(hwnd), selected)) {
                    setCBPath(GetDlgItem(hwnd, IDC_START_FOLDER_PATH), selected);
                    PropSheet_Changed(GetParent(hwnd), hwnd);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_EXPLORER_SETTINGS && HIWORD(wParam) == BN_CLICKED) {
                // https://docs.microsoft.com/en-us/windows/win32/shell/executing-control-panel-items#folder-options
                ShellExecute(nullptr, L"open",
                    L"rundll32.exe", L"shell32.dll,Options_RunDLL 7", nullptr, SW_SHOWNORMAL);
                return TRUE;
            } else if (HIWORD(wParam) == EN_CHANGE
                    || LOWORD(wParam) == IDC_START_FOLDER_PATH && pathCBChanged(wParam, lParam)
                    || LOWORD(wParam) == IDC_STATUS_TEXT_ENABLED && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_TOOLBAR_ENABLED && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_PREVIEWS_ENABLED && HIWORD(wParam) == BN_CLICKED) {
                PropSheet_Changed(GetParent(hwnd), hwnd);
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

void updateFontNameText(HWND hwnd, const LOGFONT &logFont) {
    local_wstr_ptr fontName = formatString(IDS_FONT_NAME, logFont.lfFaceName, logFont.lfHeight);
    SetDlgItemText(hwnd, IDC_TEXT_FONT_NAME, fontName.get());
}

INT_PTR CALLBACK textProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static LOGFONT logFont;

    switch (message) {
        case WM_INITDIALOG: {
            CheckDlgButton(hwnd, IDC_TEXT_EDITOR_ENABLED,
                settings::getTextEditorEnabled() ? BST_CHECKED : BST_UNCHECKED);
            logFont = settings::getTextFont();
            updateFontNameText(hwnd, logFont);
            SetDlgItemInt(hwnd, IDC_TEXT_TAB_SIZE, settings::getTextTabWidth(), FALSE);
            SendDlgItemMessage(hwnd, IDC_TEXT_TAB_SIZE_UD, UDM_SETRANGE32, 0, 16);
            CheckDlgButton(hwnd, IDC_TEXT_AUTO_INDENT,
                settings::getTextAutoIndent() ? BST_CHECKED : BST_UNCHECKED);
            for (int i = 0; i < IDS_NEWLINES_COUNT; i++) {
                SendDlgItemMessage(hwnd, IDC_TEXT_NEWLINES, CB_ADDSTRING, 0,
                    (LPARAM)getString(IDS_NEWLINES_CRLF + i));
            }
            SendDlgItemMessage(hwnd, IDC_TEXT_NEWLINES, CB_SETCURSEL,
                settings::getTextDefaultNewlines() - NL_CRLF, 0);
            CheckDlgButton(hwnd, IDC_TEXT_AUTO_NEWLINES, settings::getTextAutoNewlines());
            for (int i = 0; i < IDS_ENCODING_COUNT; i++) {
                SendDlgItemMessage(hwnd, IDC_TEXT_ENCODING, CB_ADDSTRING, 0,
                    (LPARAM)getString(IDS_ENCODING_UTF8 + i));
            }
            SendDlgItemMessage(hwnd, IDC_TEXT_ENCODING, CB_SETCURSEL,
                settings::getTextDefaultEncoding() - ENC_UTF8, 0);
            CheckDlgButton(hwnd, IDC_TEXT_AUTO_ENCODING, settings::getTextAutoEncoding());
            COMBOBOXEXITEM item = {CBEIF_TEXT, -1, (wchar_t *)settings::DEFAULT_SCRATCH_FOLDER};
            SendDlgItemMessage(hwnd, IDC_SCRATCH_FOLDER_PATH, CBEM_INSERTITEM, 0, (LPARAM)&item);
            setupPathCB(GetDlgItem(hwnd, IDC_SCRATCH_FOLDER_PATH),
                settings::getScratchFolder().get());
            checkHR(SHAutoComplete(GetDlgItem(hwnd, IDC_SCRATCH_FILE_NAME),
                SHACF_AUTOAPPEND_FORCE_OFF | SHACF_AUTOSUGGEST_FORCE_OFF));
            SetDlgItemText(hwnd, IDC_SCRATCH_FILE_NAME, settings::getScratchFileName().get());
            return TRUE;
        }
        case WM_NOTIFY: {
            NMHDR *notif = (NMHDR *)lParam;
            if (notif->code == PSN_KILLACTIVE) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, FALSE);
                return TRUE;
            } else if (notif->code == PSN_APPLY) {
                settings::setTextEditorEnabled(!!IsDlgButtonChecked(hwnd, IDC_TEXT_EDITOR_ENABLED));
                settings::setTextFont(logFont);
                BOOL success;
                int tabWidth = GetDlgItemInt(hwnd, IDC_TEXT_TAB_SIZE, &success, FALSE);
                if (success)
                    settings::setTextTabWidth(tabWidth);
                settings::setTextAutoIndent(!!IsDlgButtonChecked(hwnd, IDC_TEXT_AUTO_INDENT));
                settings::setTextDefaultNewlines((TextNewlines)(NL_CRLF +
                    SendDlgItemMessage(hwnd, IDC_TEXT_NEWLINES, CB_GETCURSEL, 0, 0)));
                settings::setTextAutoNewlines(!!IsDlgButtonChecked(hwnd, IDC_TEXT_AUTO_NEWLINES));
                settings::setTextDefaultEncoding((TextEncoding)(ENC_UTF8 +
                    SendDlgItemMessage(hwnd, IDC_TEXT_ENCODING, CB_GETCURSEL, 0, 0)));
                settings::setTextAutoEncoding(!!IsDlgButtonChecked(hwnd, IDC_TEXT_AUTO_ENCODING));
                wchar_t scratchFolder[MAX_PATH], scratchFileName[MAX_PATH];
                if (GetDlgItemText(hwnd, IDC_SCRATCH_FOLDER_PATH,
                        scratchFolder, _countof(scratchFolder)))
                    settings::setScratchFolder(scratchFolder);
                if (GetDlgItemText(hwnd, IDC_SCRATCH_FILE_NAME,
                        scratchFileName, _countof(scratchFileName)))
                    settings::setScratchFileName(scratchFileName);
                TextWindow::updateAllSettings();
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            } else if (notif->code == PSN_HELP) {
                ShellExecute(nullptr, L"open",
                    L"https://github.com/vanjac/chromafiler/wiki/Settings#text-editor",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_TEXT_FONT && HIWORD(wParam) == BN_CLICKED) {
                CHOOSEFONT chooseFont = {sizeof(chooseFont)};
                chooseFont.hwndOwner = GetParent(hwnd);
                chooseFont.lpLogFont = &logFont;
                chooseFont.iPointSize = logFont.lfHeight * 10; // store in case cancelled
                chooseFont.Flags = CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL;
                logFont.lfHeight = -pointsToPixels(logFont.lfHeight);
                if (ChooseFont(&chooseFont)) {
                    logFont.lfHeight = chooseFont.iPointSize / 10;
                    updateFontNameText(hwnd, logFont);
                    PropSheet_Changed(GetParent(hwnd), hwnd);
                } else {
                    logFont.lfHeight = chooseFont.iPointSize / 10;
                }
            } else if (LOWORD(wParam) == IDC_SCRATCH_FOLDER_BROWSE
                    && HIWORD(wParam) == BN_CLICKED) {
                CComHeapPtr<wchar_t> selected;
                if (chooseFolder(GetParent(hwnd), selected)) {
                    setCBPath(GetDlgItem(hwnd, IDC_SCRATCH_FOLDER_PATH), selected);
                    PropSheet_Changed(GetParent(hwnd), hwnd);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_TEXT_EDITOR_ENABLED && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_TEXT_AUTO_INDENT && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_TEXT_AUTO_NEWLINES && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_TEXT_AUTO_ENCODING && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_TEXT_NEWLINES && HIWORD(wParam) == CBN_SELCHANGE
                    || LOWORD(wParam) == IDC_TEXT_ENCODING && HIWORD(wParam) == CBN_SELCHANGE
                    || LOWORD(wParam) == IDC_SCRATCH_FOLDER_PATH && pathCBChanged(wParam, lParam)
                    || LOWORD(wParam) == IDC_TEXT_TAB_SIZE && HIWORD(wParam) == EN_CHANGE
                    || LOWORD(wParam) == IDC_SCRATCH_FILE_NAME && HIWORD(wParam) == EN_CHANGE) {
                PropSheet_Changed(GetParent(hwnd), hwnd);
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

void openTray(wchar_t *path) {
    wchar_t exePath[MAX_PATH];
    if (checkLE(GetModuleFileName(nullptr, exePath, _countof(exePath)))) {
        local_wstr_ptr command = format(L"ChromaFiler.exe /tray \"%1\"", path);
        STARTUPINFO startup = {sizeof(startup)};
        PROCESS_INFORMATION info = {};
        checkLE(CreateProcess(exePath, command.get(), nullptr, nullptr, FALSE,
            DETACHED_PROCESS, nullptr, nullptr, &startup, &info));
        checkLE(CloseHandle(info.hProcess));
        checkLE(CloseHandle(info.hThread));
    }
}

INT_PTR CALLBACK trayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            CheckDlgButton(hwnd, IDC_TRAY_ENABLED,
                TrayWindow::findTray() ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_OPEN_TRAY_ON_STARTUP,
                settings::getTrayOpenOnStartup() ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_TRAY_HOTKEY,
                settings::getTrayHotKeyEnabled() ? BST_CHECKED : BST_UNCHECKED);
            for (int i = 0; i < _countof(SPECIAL_PATHS); i++) {
                COMBOBOXEXITEM item = {CBEIF_TEXT, -1, (wchar_t *)SPECIAL_PATHS[i]};
                SendDlgItemMessage(hwnd, IDC_TRAY_FOLDER_PATH, CBEM_INSERTITEM, 0, (LPARAM)&item);
            }
            setupPathCB(GetDlgItem(hwnd, IDC_TRAY_FOLDER_PATH), settings::getTrayFolder().get());
            switch (settings::getTrayDirection()) {
                case TRAY_UP:
                    CheckDlgButton(hwnd, IDC_TRAY_DIR_ABOVE, BST_CHECKED);
                    break;
                case TRAY_DOWN:
                    CheckDlgButton(hwnd, IDC_TRAY_DIR_BELOW, BST_CHECKED);
                    break;
                case TRAY_RIGHT:
                    CheckDlgButton(hwnd, IDC_TRAY_DIR_RIGHT, BST_CHECKED);
                    break;
            }
            return TRUE;
        }
        case WM_NOTIFY: {
            NMHDR *notif = (NMHDR *)lParam;
            if (notif->code == PSN_KILLACTIVE) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, FALSE);
                return TRUE;
            } else if (notif->code == PSN_APPLY) {
                settings::setTrayOpenOnStartup(
                    !!IsDlgButtonChecked(hwnd, IDC_OPEN_TRAY_ON_STARTUP));
                settings::setTrayHotKeyEnabled(!!IsDlgButtonChecked(hwnd, IDC_TRAY_HOTKEY));
                wchar_t trayFolder[MAX_PATH];
                if (GetDlgItemText(hwnd, IDC_TRAY_FOLDER_PATH, trayFolder, _countof(trayFolder)))
                    settings::setTrayFolder(trayFolder);
                if (IsDlgButtonChecked(hwnd, IDC_TRAY_DIR_ABOVE))
                    settings::setTrayDirection(TRAY_UP);
                else if (IsDlgButtonChecked(hwnd, IDC_TRAY_DIR_BELOW))
                    settings::setTrayDirection(TRAY_DOWN);
                else if (IsDlgButtonChecked(hwnd, IDC_TRAY_DIR_RIGHT))
                    settings::setTrayDirection(TRAY_RIGHT);
                TrayWindow::updateAllSettings();
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            } else if (notif->code == PSN_HELP) {
                ShellExecute(nullptr, L"open",
                    L"https://github.com/vanjac/chromafiler/wiki/Settings#tray",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_TRAY_ENABLED && HIWORD(wParam) == BN_CLICKED) {
                bool checked = !!IsDlgButtonChecked(hwnd, IDC_TRAY_ENABLED);
                HWND tray = TrayWindow::findTray();
                if (checked && !tray) {
                    wchar_t path[MAX_PATH];
                    if (GetDlgItemText(hwnd, IDC_TRAY_FOLDER_PATH, path, _countof(path)))
                        openTray(path);
                } else if (!checked && tray) {
                    PostMessage(tray, WM_CLOSE, 0, 0);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_TRAY_FOLDER_BROWSE && HIWORD(wParam) == BN_CLICKED) {
                CComHeapPtr<wchar_t> selected;
                if (chooseFolder(GetParent(hwnd), selected)) {
                    setCBPath(GetDlgItem(hwnd, IDC_TRAY_FOLDER_PATH), selected);
                    PropSheet_Changed(GetParent(hwnd), hwnd);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_RESET_TRAY_POSITION && HIWORD(wParam) == BN_CLICKED) {
                settings::setTrayPosition(settings::DEFAULT_TRAY_POSITION);
                settings::setTraySize(settings::DEFAULT_TRAY_SIZE);
                settings::setTrayDPI(settings::DEFAULT_TRAY_DPI);
                TrayWindow::resetTrayPosition();
                return TRUE;
            } else if (LOWORD(wParam) == IDC_TRAY_FOLDER_PATH && pathCBChanged(wParam, lParam)) {
                PropSheet_Changed(GetParent(hwnd), hwnd);
                return TRUE;
            } else if (HIWORD(wParam) == BN_CLICKED &&
                    (LOWORD(wParam) == IDC_OPEN_TRAY_ON_STARTUP || LOWORD(wParam) == IDC_TRAY_HOTKEY
                    || LOWORD(wParam) == IDC_TRAY_DIR_ABOVE || LOWORD(wParam) == IDC_TRAY_DIR_BELOW
                    || LOWORD(wParam) == IDC_TRAY_DIR_RIGHT)) {
                PropSheet_Changed(GetParent(hwnd), hwnd);
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

INT_PTR CALLBACK browserProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NOTIFY: {
            NMHDR *notif = (NMHDR *)lParam;
            if (notif->code == PSN_KILLACTIVE) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, FALSE);
                return TRUE;
            } else if (notif->code == PSN_APPLY) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            } else if (notif->code == PSN_HELP) {
                ShellExecute(nullptr, L"open",
                    L"https://github.com/vanjac/chromafiler/wiki/Settings#default-browser",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            HINSTANCE instance = GetModuleHandle(nullptr);
            if (LOWORD(wParam) == IDC_SET_DEFAULT_BROWSER && HIWORD(wParam) == BN_CLICKED) {
                if (settings::supportsDefaultBrowser()) {
                    settings::setDefaultBrowser(true);
                    TaskDialog(GetParent(hwnd), instance, MAKEINTRESOURCE(IDS_SUCCESS_CAPTION),
                        nullptr, MAKEINTRESOURCE(IDS_BROWSER_SET), 0, nullptr, nullptr);
                } else {
                    TaskDialog(GetParent(hwnd), instance, MAKEINTRESOURCE(IDS_BROWSER_SET_FAILED),
                        nullptr, MAKEINTRESOURCE(IDS_REQUIRE_CONTEXT), 0, TD_ERROR_ICON, nullptr);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_RESET_DEFAULT_BROWSER
                    && HIWORD(wParam) == BN_CLICKED) {
                if (settings::supportsDefaultBrowser()) {
                    settings::setDefaultBrowser(false);
                    TaskDialog(GetParent(hwnd), instance, MAKEINTRESOURCE(IDS_SUCCESS_CAPTION),
                        nullptr, MAKEINTRESOURCE(IDS_BROWSER_RESET), 0, nullptr, nullptr);
                } else {
                    TaskDialog(GetParent(hwnd), instance, MAKEINTRESOURCE(IDS_BROWSER_SET_FAILED),
                        nullptr, MAKEINTRESOURCE(IDS_REQUIRE_CONTEXT), 0, TD_ERROR_ICON, nullptr);
                }
                return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}

INT_PTR CALLBACK aboutProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            SendDlgItemMessage(hwnd, IDC_LEGAL_INFO, EM_SETTABSTOPS, 1, (LPARAM)tempPtr(16u));
            CheckDlgButton(hwnd, IDC_AUTO_UPDATE,
                settings::getUpdateCheckEnabled() ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemText(hwnd, IDC_VERSION, _T(CHROMAFILER_VERSION_STRING));
            SetDlgItemText(hwnd, IDC_LEGAL_INFO, getString(IDS_LEGAL_INFO));
            return TRUE;
        }
        case WM_NOTIFY: {
            NMHDR *notif = (NMHDR *)lParam;
            if (notif->code == PSN_KILLACTIVE) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, FALSE);
                return TRUE;
            } else if (notif->code == PSN_APPLY) {
                settings::setUpdateCheckEnabled(!!IsDlgButtonChecked(hwnd, IDC_AUTO_UPDATE));
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            } else if (notif->code == PSN_HELP) {
                ShellExecute(nullptr, L"open",
                    L"https://github.com/vanjac/chromafiler/wiki/Settings#updateabout",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_UPDATES_LINK && HIWORD(wParam) == BN_CLICKED) {
                HINSTANCE instance = GetModuleHandle(nullptr);
                UpdateInfo info;
                if (DWORD error = checkUpdate(&info)) {
                    TaskDialog(GetParent(hwnd), instance, MAKEINTRESOURCE(IDS_UPDATE_ERROR),
                        nullptr, getErrorMessage(error).get(), 0, nullptr, nullptr);
                    return TRUE;
                }
                if (info.isNewer) {
                    openUpdate(info);
                } else {
                    TaskDialog(GetParent(hwnd), instance, MAKEINTRESOURCE(IDS_NO_UPDATE_CAPTION),
                        nullptr, MAKEINTRESOURCE(IDS_NO_UPDATE), 0, nullptr, nullptr);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_HELP_LINK && HIWORD(wParam) == BN_CLICKED) {
                ShellExecute(nullptr, L"open", L"https://github.com/vanjac/chromafiler/wiki",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            } else if (LOWORD(wParam) == IDC_WEBSITE_LINK && HIWORD(wParam) == BN_CLICKED) {
                ShellExecute(nullptr, L"open", L"https://chroma.zone/chromafiler/",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            } else if (LOWORD(wParam) == IDC_DONATE_LINK && HIWORD(wParam) == BN_CLICKED) {
                ShellExecute(nullptr, L"open", L"https://chroma.zone/donate",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            } else if (LOWORD(wParam) == IDC_AUTO_UPDATE && HIWORD(wParam) == BN_CLICKED) {
                PropSheet_Changed(GetParent(hwnd), hwnd);
                return TRUE;
            } else if (HIWORD(wParam) == CBN_SELCHANGE) {
                SendMessage(GetParent(hwnd), LOWORD(wParam), lParam, (LPARAM)tempPtr(0xDD8AD83Ell)); 
                return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}

void openSettingsDialog(SettingsPage page) {
    if (settingsDialog) {
        SetActiveWindow(settingsDialog);
        PropSheet_SetCurSel(settingsDialog, nullptr, page);
        return;
    }

    HINSTANCE hInstance = GetModuleHandle(nullptr);
    PROPSHEETPAGE pages[NUM_SETTINGS_PAGES];

    pages[SETTINGS_GENERAL] = {sizeof(PROPSHEETPAGE)};
    pages[SETTINGS_GENERAL].dwFlags = PSP_HASHELP;
    pages[SETTINGS_GENERAL].hInstance = hInstance;
    pages[SETTINGS_GENERAL].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_GENERAL);
    pages[SETTINGS_GENERAL].pfnDlgProc = generalProc;

    pages[SETTINGS_TEXT] = {sizeof(PROPSHEETPAGE)};
    pages[SETTINGS_TEXT].dwFlags = PSP_HASHELP;
    pages[SETTINGS_TEXT].hInstance = hInstance;
    pages[SETTINGS_TEXT].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_TEXT);
    pages[SETTINGS_TEXT].pfnDlgProc = textProc;

    pages[SETTINGS_TRAY] = {sizeof(PROPSHEETPAGE)};
    pages[SETTINGS_TRAY].dwFlags = PSP_HASHELP;
    pages[SETTINGS_TRAY].hInstance = hInstance;
    pages[SETTINGS_TRAY].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_TRAY);
    pages[SETTINGS_TRAY].pfnDlgProc = trayProc;

    pages[SETTINGS_BROWSER] = {sizeof(PROPSHEETPAGE)};
    pages[SETTINGS_BROWSER].dwFlags = PSP_HASHELP;
    pages[SETTINGS_BROWSER].hInstance = hInstance;
    pages[SETTINGS_BROWSER].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_BROWSER);
    pages[SETTINGS_BROWSER].pfnDlgProc = browserProc;

    pages[SETTINGS_ABOUT] = {sizeof(PROPSHEETPAGE)};
    pages[SETTINGS_ABOUT].dwFlags = PSP_HASHELP;
    pages[SETTINGS_ABOUT].hInstance = hInstance;
    pages[SETTINGS_ABOUT].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_ABOUT);
    pages[SETTINGS_ABOUT].pfnDlgProc = aboutProc;

    PROPSHEETHEADER sheet = {sizeof(sheet)};
    sheet.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID | PSH_NOCONTEXTHELP | PSH_MODELESS;
    sheet.hInstance = hInstance;
    sheet.pszIcon = MAKEINTRESOURCE(IDR_APP_ICON);
    sheet.pszCaption = MAKEINTRESOURCE(IDS_SETTINGS_CAPTION);
    sheet.nPages = _countof(pages);
    sheet.nStartPage = page;
    sheet.ppsp = pages;
    settingsDialog = (HWND)PropertySheet(&sheet);
    if (settingsDialog)
        lockProcess();
}

bool handleSettingsDialogMessage(MSG *msg) {
    if (!settingsDialog)
        return false;
    bool isDialogMessage = !!PropSheet_IsDialogMessage(settingsDialog, msg);
    if (!PropSheet_GetCurrentPageHwnd(settingsDialog)) {
        DestroyWindow(settingsDialog);
        settingsDialog = nullptr;
        unlockProcess();
    }
    return isDialogMessage;
}

} // namespace
