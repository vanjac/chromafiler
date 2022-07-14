#include "SettingsDialog.h"
#include "resource.h"
#include <atlbase.h>
#include <prsht.h>
#include <shellapi.h>

namespace chromabrowse {

INT_PTR generalProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG:
            return TRUE;
        case WM_NOTIFY: {
            NMHDR *notif = (NMHDR *)lParam;
            if (notif->code == PSN_KILLACTIVE) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, FALSE);
                return TRUE;
            } else if (notif->code == PSN_APPLY) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            } else if (notif->code == PSN_RESET) {
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_EXPLORER_SETTINGS && HIWORD(wParam) == BN_CLICKED) {
                // https://docs.microsoft.com/en-us/windows/win32/shell/executing-control-panel-items#folder-options
                ShellExecute(nullptr, L"open",
                    L"rundll32.exe", L"shell32.dll,Options_RunDLL 7", nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

void openSettingsDialog(HWND owner) {
    PROPSHEETPAGE generalPage = {sizeof(generalPage)};
    generalPage.hInstance = GetModuleHandle(nullptr);
    generalPage.pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_GENERAL);
    generalPage.pfnDlgProc = generalProc;

    PROPSHEETHEADER sheet = {sizeof(sheet)};
    sheet.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID | PSH_NOCONTEXTHELP;
    sheet.hwndParent = owner;
    sheet.hInstance = GetModuleHandle(nullptr);
    sheet.pszIcon = MAKEINTRESOURCE(IDR_APP_ICON);
    sheet.pszCaption = MAKEINTRESOURCE(IDS_SETTINGS_CAPTION);
    sheet.nPages = 1;
    sheet.ppsp = &generalPage;
    PropertySheet(&sheet);
}

} // namespace
