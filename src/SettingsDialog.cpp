#include "SettingsDialog.h"
#include "Settings.h"
#include "TrayWindow.h"
#include "CreateItemWindow.h"
#include "resource.h"
#include <atlbase.h>
#include <prsht.h>
#include <shellapi.h>
#include <shobjidl_core.h>

namespace chromabrowse {

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

INT_PTR generalProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
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
            CheckDlgButton(hwnd, IDC_TEXT_EDITOR_ENABLED,
                settings::getTextEditorEnabled() ? BST_CHECKED : BST_UNCHECKED);
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
                settings::setTextEditorEnabled(!!IsDlgButtonChecked(hwnd, IDC_TEXT_EDITOR_ENABLED));
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            } else if (notif->code == PSN_HELP) {
                ShellExecute(nullptr, L"open",
                    L"https://github.com/vanjac/chromabrowse/wiki/Settings#general",
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
                    || LOWORD(wParam) == IDC_PREVIEWS_ENABLED && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_TEXT_EDITOR_ENABLED && HIWORD(wParam) == BN_CLICKED) {
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

INT_PTR trayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
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
                return TRUE;
            } else if (notif->code == PSN_HELP) {
                ShellExecute(nullptr, L"open",
                    L"https://github.com/vanjac/chromabrowse/wiki/Settings#tray",
                    nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_OPEN_TRAY && HIWORD(wParam) == BN_CLICKED) {
                wchar_t path[MAX_PATH];
                if (GetDlgItemText(hwnd, IDC_TRAY_FOLDER_PATH, path, _countof(path)))
                    openTray(path);
            } else if (LOWORD(wParam) == IDC_TRAY_FOLDER_BROWSE && HIWORD(wParam) == BN_CLICKED) {
                CComHeapPtr<wchar_t> selected;
                if (chooseFolder(GetParent(hwnd), selected)) {
                    SetDlgItemText(hwnd, IDC_TRAY_FOLDER_PATH, selected);
                    PropSheet_Changed(GetParent(hwnd), hwnd);
                }
            } else if (LOWORD(wParam) == IDC_RESET_TRAY_POSITION && HIWORD(wParam) == BN_CLICKED) {
                settings::setTrayPosition(settings::DEFAULT_TRAY_POSITION);
                settings::setTraySize(settings::DEFAULT_TRAY_SIZE);
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

void openSettingsDialog() {
    if (settingsDialog) {
        SetActiveWindow(settingsDialog);
        return;
    }

    PROPSHEETPAGE pages[2];

    pages[0] = {sizeof(PROPSHEETPAGE)};
    pages[0].dwFlags = PSP_HASHELP;
    pages[0].hInstance = GetModuleHandle(nullptr);
    pages[0].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_GENERAL);
    pages[0].pfnDlgProc = generalProc;

    pages[1] = {sizeof(PROPSHEETPAGE)};
    pages[1].dwFlags = PSP_HASHELP;
    pages[1].hInstance = GetModuleHandle(nullptr);
    pages[1].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_TRAY);
    pages[1].pfnDlgProc = trayProc;

    PROPSHEETHEADER sheet = {sizeof(sheet)};
    sheet.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID | PSH_NOCONTEXTHELP | PSH_MODELESS;
    sheet.hInstance = GetModuleHandle(nullptr);
    sheet.pszIcon = MAKEINTRESOURCE(IDR_APP_ICON);
    sheet.pszCaption = MAKEINTRESOURCE(IDS_SETTINGS_CAPTION);
    sheet.nPages = _countof(pages);
    sheet.ppsp = pages;
    settingsDialog = (HWND)PropertySheet(&sheet);
}

bool handleSettingsDialogMessage(MSG *msg) {
    if (settingsDialog && PropSheet_IsDialogMessage(settingsDialog, msg)) {
        if (!PropSheet_GetCurrentPageHwnd(settingsDialog)) {
            DestroyWindow(settingsDialog);
            settingsDialog = nullptr;
        }
        return true;
    }
    return false;
}

} // namespace
