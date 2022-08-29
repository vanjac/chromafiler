#include "SettingsDialog.h"
#include "Settings.h"
#include "TextWindow.h"
#include "TrayWindow.h"
#include "CreateItemWindow.h"
#include "main.h"
#include "UIStrings.h"
#include "DPI.h"
#include "resource.h"
#include <atlbase.h>
#include <prsht.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <commdlg.h>

namespace chromafile {

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

INT_PTR CALLBACK generalProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            for (int i = 0; i < _countof(SPECIAL_PATHS); i++) {
                SendDlgItemMessage(hwnd, IDC_START_FOLDER_PATH, CB_ADDSTRING, 0,
                    (LPARAM)SPECIAL_PATHS[i]);
            }
            CComHeapPtr<wchar_t> startingFolder;
            settings::getStartingFolder(startingFolder);
            SetDlgItemText(hwnd, IDC_START_FOLDER_PATH, startingFolder);
            SIZE folderWindowSize = settings::getFolderWindowSize();
            SetDlgItemInt(hwnd, IDC_FOLDER_WINDOW_WIDTH, folderWindowSize.cx, TRUE);
            SetDlgItemInt(hwnd, IDC_FOLDER_WINDOW_HEIGHT, folderWindowSize.cy, TRUE);
            SIZE itemWindowSize = settings::getItemWindowSize();
            SetDlgItemInt(hwnd, IDC_ITEM_WINDOW_WIDTH, itemWindowSize.cx, TRUE);
            SetDlgItemInt(hwnd, IDC_ITEM_WINDOW_HEIGHT, itemWindowSize.cy, TRUE);
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
                settings::setStatusTextEnabled(!!IsDlgButtonChecked(hwnd, IDC_STATUS_TEXT_ENABLED));
                settings::setToolbarEnabled(!!IsDlgButtonChecked(hwnd, IDC_TOOLBAR_ENABLED));
                settings::setPreviewsEnabled(!!IsDlgButtonChecked(hwnd, IDC_PREVIEWS_ENABLED));
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            } else if (notif->code == PSN_HELP) {
                ShellExecute(nullptr, L"open",
                    L"https://github.com/vanjac/chromafile/wiki/Settings#general",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_START_FOLDER_BROWSE && HIWORD(wParam) == BN_CLICKED) {
                CComHeapPtr<wchar_t> selected;
                if (chooseFolder(GetParent(hwnd), selected)) {
                    SetDlgItemText(hwnd, IDC_START_FOLDER_PATH, selected);
                    PropSheet_Changed(GetParent(hwnd), hwnd);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_EXPLORER_SETTINGS && HIWORD(wParam) == BN_CLICKED) {
                // https://docs.microsoft.com/en-us/windows/win32/shell/executing-control-panel-items#folder-options
                ShellExecute(nullptr, L"open",
                    L"rundll32.exe", L"shell32.dll,Options_RunDLL 7", nullptr, SW_SHOWNORMAL);
                return TRUE;
            } else if (HIWORD(wParam) == EN_CHANGE
                    || LOWORD(wParam) == IDC_START_FOLDER_PATH && HIWORD(wParam) == CBN_EDITCHANGE
                    || LOWORD(wParam) == IDC_START_FOLDER_PATH && HIWORD(wParam) == CBN_SELCHANGE
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
    LocalHeapPtr<wchar_t> fontName;
    formatMessage(fontName, STR_FONT_NAME, logFont.lfFaceName, logFont.lfHeight);
    SetDlgItemText(hwnd, IDC_TEXT_FONT_NAME, fontName);
}

INT_PTR CALLBACK textProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static LOGFONT logFont;

    switch (message) {
        case WM_INITDIALOG: {
            CheckDlgButton(hwnd, IDC_TEXT_EDITOR_ENABLED,
                settings::getTextEditorEnabled() ? BST_CHECKED : BST_UNCHECKED);
            logFont = settings::getTextFont();
            updateFontNameText(hwnd, logFont);
            CheckDlgButton(hwnd, IDC_TEXT_WRAP,
                settings::getTextWrap() ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_TEXT_AUTO_INDENT,
                settings::getTextAutoIndent() ? BST_CHECKED : BST_UNCHECKED);
            SendDlgItemMessage(hwnd, IDC_SCRATCH_FOLDER_PATH, CB_ADDSTRING, 0,
                (LPARAM)settings::DEFAULT_SCRATCH_FOLDER);
            CComHeapPtr<wchar_t> scratchFolder;
            settings::getScratchFolder(scratchFolder);
            SetDlgItemText(hwnd, IDC_SCRATCH_FOLDER_PATH, scratchFolder);
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
                settings::setTextWrap(!!IsDlgButtonChecked(hwnd, IDC_TEXT_WRAP));
                settings::setTextAutoIndent(!!IsDlgButtonChecked(hwnd, IDC_TEXT_AUTO_INDENT));
                wchar_t scratchFolder[MAX_PATH];
                if (GetDlgItemText(hwnd, IDC_SCRATCH_FOLDER_PATH,
                        scratchFolder, _countof(scratchFolder)))
                    settings::setScratchFolder(scratchFolder);
                TextWindow::updateAllSettings();
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            } else if (notif->code == PSN_HELP) {
                ShellExecute(nullptr, L"open",
                    L"https://github.com/vanjac/chromafile/wiki/Text-Editor",
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
                    SetDlgItemText(hwnd, IDC_SCRATCH_FOLDER_PATH, selected);
                    PropSheet_Changed(GetParent(hwnd), hwnd);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_TEXT_EDITOR_ENABLED && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_TEXT_WRAP && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_TEXT_AUTO_INDENT && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_SCRATCH_FOLDER_PATH && HIWORD(wParam) == CBN_EDITCHANGE
                    || LOWORD(wParam) == IDC_SCRATCH_FOLDER_PATH
                            && HIWORD(wParam) == CBN_SELCHANGE) {
                PropSheet_Changed(GetParent(hwnd), hwnd);
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

void openTray(wchar_t *path) {
    CComPtr<IShellItem> item = itemFromPath(path);
    if (!item)
        return;
    CComPtr<TrayWindow> tray;
    tray.Attach(new TrayWindow(nullptr, item));
    POINT pos = tray->requestedPosition();
    SIZE size = tray->requestedSize();
    tray->create({pos.x, pos.y, pos.x + size.cx, pos.y + size.cy}, SW_SHOWNORMAL);
}

INT_PTR CALLBACK trayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            CheckDlgButton(hwnd, IDC_TRAY_ENABLED,
                TrayWindow::findTray() ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_OPEN_TRAY_ON_STARTUP,
                settings::getTrayOpenOnStartup() ? BST_CHECKED : BST_UNCHECKED);
            for (int i = 0; i < _countof(SPECIAL_PATHS); i++) {
                SendDlgItemMessage(hwnd, IDC_TRAY_FOLDER_PATH, CB_ADDSTRING, 0,
                    (LPARAM)SPECIAL_PATHS[i]);
            }
            CComHeapPtr<wchar_t> trayFolder;
            settings::getTrayFolder(trayFolder);
            SetDlgItemText(hwnd, IDC_TRAY_FOLDER_PATH, trayFolder);
            switch (settings::getTrayDirection()) {
                case settings::TRAY_UP:
                    CheckDlgButton(hwnd, IDC_TRAY_DIR_ABOVE, BST_CHECKED);
                    break;
                case settings::TRAY_DOWN:
                    CheckDlgButton(hwnd, IDC_TRAY_DIR_BELOW, BST_CHECKED);
                    break;
                case settings::TRAY_RIGHT:
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
                wchar_t trayFolder[MAX_PATH];
                if (GetDlgItemText(hwnd, IDC_TRAY_FOLDER_PATH, trayFolder, _countof(trayFolder)))
                    settings::setTrayFolder(trayFolder);
                if (IsDlgButtonChecked(hwnd, IDC_TRAY_DIR_ABOVE))
                    settings::setTrayDirection(settings::TRAY_UP);
                else if (IsDlgButtonChecked(hwnd, IDC_TRAY_DIR_BELOW))
                    settings::setTrayDirection(settings::TRAY_DOWN);
                else if (IsDlgButtonChecked(hwnd, IDC_TRAY_DIR_RIGHT))
                    settings::setTrayDirection(settings::TRAY_RIGHT);
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            } else if (notif->code == PSN_HELP) {
                ShellExecute(nullptr, L"open",
                    L"https://github.com/vanjac/chromafile/wiki/Settings#tray",
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
                    SetDlgItemText(hwnd, IDC_TRAY_FOLDER_PATH, selected);
                    PropSheet_Changed(GetParent(hwnd), hwnd);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_RESET_TRAY_POSITION && HIWORD(wParam) == BN_CLICKED) {
                settings::setTrayPosition(settings::DEFAULT_TRAY_POSITION);
                settings::setTraySize(settings::DEFAULT_TRAY_SIZE);
                settings::setTrayDPI(settings::DEFAULT_TRAY_DPI);
                HWND tray = TrayWindow::findTray();
                if (tray) {
                    PostMessage(tray, WM_CLOSE, 0, 0);
                    wchar_t path[MAX_PATH];
                    if (GetDlgItemText(hwnd, IDC_TRAY_FOLDER_PATH, path, _countof(path)))
                        openTray(path);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_TRAY_FOLDER_PATH
                    && (HIWORD(wParam) == CBN_EDITCHANGE || HIWORD(wParam) == CBN_SELCHANGE)) {
                PropSheet_Changed(GetParent(hwnd), hwnd);
                return TRUE;
            } else if (HIWORD(wParam) == BN_CLICKED && (LOWORD(wParam) == IDC_OPEN_TRAY_ON_STARTUP
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

INT_PTR CALLBACK aboutProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NOTIFY: {
            NMHDR *notif = (NMHDR *)lParam;
            if (notif->code == PSN_KILLACTIVE) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, FALSE);
                return TRUE;
            } else if (notif->code == PSN_APPLY) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_UPDATES_LINK && HIWORD(wParam) == BN_CLICKED) {
                ShellExecute(nullptr, L"open", L"https://github.com/vanjac/chromafile/releases",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            } else if (LOWORD(wParam) == IDC_HELP_LINK && HIWORD(wParam) == BN_CLICKED) {
                ShellExecute(nullptr, L"open", L"https://github.com/vanjac/chromafile/wiki",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            } else if (LOWORD(wParam) == IDC_SOURCE_LINK && HIWORD(wParam) == BN_CLICKED) {
                ShellExecute(nullptr, L"open", L"https://github.com/vanjac/chromafile",
                    nullptr, nullptr, SW_SHOWNORMAL);
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

    PROPSHEETPAGE pages[NUM_SETTINGS_PAGES];

    pages[SETTINGS_GENERAL] = {sizeof(PROPSHEETPAGE)};
    pages[SETTINGS_GENERAL].dwFlags = PSP_HASHELP;
    pages[SETTINGS_GENERAL].hInstance = GetModuleHandle(nullptr);
    pages[SETTINGS_GENERAL].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_GENERAL);
    pages[SETTINGS_GENERAL].pfnDlgProc = generalProc;

    pages[SETTINGS_TEXT] = {sizeof(PROPSHEETPAGE)};
    pages[SETTINGS_TEXT].dwFlags = PSP_HASHELP;
    pages[SETTINGS_TEXT].hInstance = GetModuleHandle(nullptr);
    pages[SETTINGS_TEXT].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_TEXT);
    pages[SETTINGS_TEXT].pfnDlgProc = textProc;

    pages[SETTINGS_TRAY] = {sizeof(PROPSHEETPAGE)};
    pages[SETTINGS_TRAY].dwFlags = PSP_HASHELP;
    pages[SETTINGS_TRAY].hInstance = GetModuleHandle(nullptr);
    pages[SETTINGS_TRAY].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_TRAY);
    pages[SETTINGS_TRAY].pfnDlgProc = trayProc;

    pages[SETTINGS_ABOUT] = {sizeof(PROPSHEETPAGE)};
    pages[SETTINGS_ABOUT].hInstance = GetModuleHandle(nullptr);
    pages[SETTINGS_ABOUT].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_ABOUT);
    pages[SETTINGS_ABOUT].pfnDlgProc = aboutProc;

    PROPSHEETHEADER sheet = {sizeof(sheet)};
    sheet.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID | PSH_NOCONTEXTHELP | PSH_MODELESS;
    sheet.hInstance = GetModuleHandle(nullptr);
    sheet.pszIcon = MAKEINTRESOURCE(IDR_APP_ICON);
    sheet.pszCaption = MAKEINTRESOURCE(IDS_SETTINGS_CAPTION);
    sheet.nPages = _countof(pages);
    sheet.nStartPage = page;
    sheet.ppsp = pages;
    settingsDialog = (HWND)PropertySheet(&sheet);
    if (settingsDialog)
        windowOpened();
}

bool handleSettingsDialogMessage(MSG *msg) {
    if (!settingsDialog)
        return false;
    bool isDialogMessage = !!PropSheet_IsDialogMessage(settingsDialog, msg);
    if (!PropSheet_GetCurrentPageHwnd(settingsDialog)) {
        DestroyWindow(settingsDialog);
        settingsDialog = nullptr;
        windowClosed();
    }
    return isDialogMessage;
}

} // namespace
