#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include "PreviewWindow.h"
#include "TextWindow.h"
#include "TrayWindow.h"
#include "CreateItemWindow.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Comctl32.lib")

using namespace chromafile;

const wchar_t APP_ID[] = L"chroma.browse";

#ifdef CHROMAFILE_DEBUG
int main(int, char**) {
    return wWinMain(nullptr, nullptr, nullptr, SW_SHOWNORMAL);
}

bool logHRESULT(long hr, const char *file, int line, const char *expr) {
    if (SUCCEEDED(hr)) {
        return true;
    } else {
        debugPrintf(L"Error 0x%X in %S (%S:%d)\n", hr, expr, file, line);
        return false;
    } 
}
#endif

DWORD WINAPI updateJumpList(void *);

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand) {
    debugPrintf(L"omg hiiiii ^w^\n"); // DO NOT REMOVE!!

#ifdef CHROMAFILE_MEMLEAKS
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    debugPrintf(L"Compiled with memory leak detection\n");
#endif

    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLine(), &argc);

    if (!checkHR(OleInitialize(0))) // needed for drag/drop
        return 0;

    INITCOMMONCONTROLSEX controls = {sizeof(controls)};
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    ItemWindow::init();
    FolderWindow::init();
    ThumbnailWindow::init();
    PreviewWindow::init();
    TextWindow::init();
    TrayWindow::init();

    // https://docs.microsoft.com/en-us/windows/win32/shell/appids
    checkHR(SetCurrentProcessExplicitAppUserModelID(APP_ID));

    HANDLE jumpListThread = nullptr;
    SHCreateThreadWithHandle(updateJumpList, nullptr, CTF_COINIT_STA, nullptr, &jumpListThread);

    {
        CComHeapPtr<wchar_t> pathAlloc;
        wchar_t *path;
        bool tray = false;
        if (argc > 1 && lstrcmpi(argv[1], L"/tray") == 0) {
            settings::getTrayFolder(pathAlloc);
            path = pathAlloc;
            tray = true;
        } else if (argc > 1) {
            path = argv[1];
            int pathLen = lstrlen(path);
            if (path[pathLen - 1] == '"')
                path[pathLen - 1] = '\\'; // fix weird CommandLineToArgvW behavior with \"
        } else {
            settings::getStartingFolder(pathAlloc);
            path = pathAlloc;
        }

        CComPtr<IShellItem> startItem = itemFromPath(path);
        if (!startItem)
            return 0;
        startItem = resolveLink(nullptr, startItem);

        CComPtr<ItemWindow> initialWindow;
        POINT pos;
        if (tray) {
            initialWindow.Attach(new TrayWindow(nullptr, startItem));
            pos = ((TrayWindow *)initialWindow.p)->requestedPosition();
        } else {
            initialWindow = createItemWindow(nullptr, startItem);
            pos = {CW_USEDEFAULT, CW_USEDEFAULT};
        }
        SIZE size = initialWindow->requestedSize();
        initialWindow->create({pos.x, pos.y, pos.x + size.cx, pos.y + size.cy}, showCommand);
    }
    LocalFree(argv);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (handleSettingsDialogMessage(&msg))
            continue;
        if (activeWindow && activeWindow->handleTopLevelMessage(&msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WaitForSingleObject(jumpListThread, INFINITE);
    CloseHandle(jumpListThread);

    ItemWindow::uninit();
    PreviewWindow::uninit();
    TextWindow::uninit();
    OleUninitialize();

    return (int)msg.wParam;
}

DWORD WINAPI updateJumpList(void *) {
    CComPtr<ICustomDestinationList> jumpList;
    if (!checkHR(jumpList.CoCreateInstance(__uuidof(DestinationList))))
        return false;
    checkHR(jumpList->SetAppID(APP_ID));
    UINT minSlots;
    CComPtr<IObjectArray> removedDestinations;
    checkHR(jumpList->BeginList(&minSlots, IID_PPV_ARGS(&removedDestinations)));
    // previous versions had a jump list but for now it is removed (#105)
    // so this only serves to clear any existing jump lists
    return checkHR(jumpList->CommitList());
}
