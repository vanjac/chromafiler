#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include <shlobj.h>

// https://docs.microsoft.com/en-us/windows/win32/controls/cookbook-overview
#pragma comment(linker,"\"/manifestdependency:type='win32' \
    name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
    processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifdef DEBUG
int main(int, char**) {
    wWinMain(nullptr, nullptr, nullptr, SW_SHOWNORMAL);
}
#endif

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand) {
    debugPrintf(L"omg hiiiii ^w^\n"); // DO NOT REMOVE!!
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLine(), &argc);

    if (FAILED(OleInitialize(0))) // needed for drag/drop
        return 0;

    INITCOMMONCONTROLSEX controls;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    chromabrowse::ItemWindow::init();
    chromabrowse::FolderWindow::init();
    chromabrowse::ThumbnailWindow::init();

    {
        CComPtr<IShellItem> startItem;
        if (argc > 1) {
            // TODO parse name vs display name https://stackoverflow.com/q/42966489
            if (FAILED(SHCreateItemFromParsingName(argv[1], nullptr, IID_PPV_ARGS(&startItem)))) {
                debugPrintf(L"Unable to locate item at path\n");
                return 0;
            }
        } else {
            if (FAILED(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr,
                    IID_PPV_ARGS(&startItem)))) {
                debugPrintf(L"Couldn't get desktop!\n");
                return 0;
            }
        }

        RECT workArea;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

        CComPtr<chromabrowse::FolderWindow> initialWindow;
        initialWindow.Attach(new chromabrowse::FolderWindow(nullptr, startItem));
        SIZE size = initialWindow->defaultSize();
        RECT windowRect = {workArea.left, workArea.bottom - size.cy,
                           workArea.left + size.cx, workArea.bottom};
        initialWindow->create(windowRect, showCommand);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (chromabrowse::activeWindow && chromabrowse::activeWindow->handleTopLevelMessage(&msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    chromabrowse::ItemWindow::uninit();
    OleUninitialize();
    return 0;
}
