#include "SettingsDialog.h"
#include "resource.h"
#include <atlbase.h>
#include <prsht.h>
#include <shobjidl_core.h>

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
                CComPtr<IOpenControlPanel> openControlPanel;
                if (checkHR(openControlPanel.CoCreateInstance(__uuidof(OpenControlPanel)))) {
                    checkHR(openControlPanel->Open(L"Microsoft.FolderOptions", nullptr, nullptr));
                }
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
    sheet.dwFlags = PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP;
    sheet.hwndParent = owner;
    sheet.hInstance = GetModuleHandle(nullptr);
    sheet.pszCaption = MAKEINTRESOURCE(IDS_SETTINGS_CAPTION);
    sheet.nPages = 1;
    sheet.ppsp = &generalPage;
    PropertySheet(&sheet);
}

} // namespace
