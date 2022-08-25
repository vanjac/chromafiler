#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include "PreviewWindow.h"
#include "TextWindow.h"
#include "TrayWindow.h"
#include "CreateItemWindow.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "UIStrings.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <propkey.h>
#include <Propvarutil.h>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")

using namespace chromafile;

const wchar_t APP_ID[] = L"chroma.file";
const wchar_t SHELL_PREFIX[] = L"shell:";

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
        bool tray = false, scratch = false;
        if (argc > 1 && lstrcmpi(argv[1], L"/tray") == 0) {
            settings::getTrayFolder(pathAlloc);
            path = pathAlloc;
            tray = true;
        } else if (argc > 1 && lstrcmpi(argv[1], L"/scratch") == 0) {
            settings::getScratchFolder(pathAlloc);
            path = pathAlloc;
            scratch = true;
        } else if (argc > 1) {
            wchar_t *relPath = argv[1];
            int relPathLen = lstrlen(relPath);
            if (relPath[relPathLen - 1] == '"')
                relPath[relPathLen - 1] = '\\'; // fix weird CommandLineToArgvW behavior with \"

            if ((relPathLen >= 1 && relPath[0] == ':') || (relPathLen >= _countof(SHELL_PREFIX)
                    && _memicmp(relPath, SHELL_PREFIX, _countof(SHELL_PREFIX)) == 0)) {
                path = relPath; // assume desktop absolute parsing name
            } else { // assume relative file system path
                pathAlloc.Allocate(MAX_PATH);
                path = pathAlloc;
                if (!GetFullPathName(relPath, MAX_PATH, path, nullptr))
                    path = relPath;
            }
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
        } else if (scratch) {
            CComPtr<IShellItem> scratchFile = createScratchFile(startItem);
            if (!scratchFile)
                return 0;
            initialWindow.Attach(new TextWindow(nullptr, scratchFile, true));
            pos = {CW_USEDEFAULT, CW_USEDEFAULT};
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
        return 0;
    checkHR(jumpList->SetAppID(APP_ID));
    UINT minSlots;
    CComPtr<IObjectArray> removedDestinations;
    checkHR(jumpList->BeginList(&minSlots, IID_PPV_ARGS(&removedDestinations)));

    CComPtr<IObjectCollection> tasks;
    if (!checkHR(tasks.CoCreateInstance(__uuidof(EnumerableObjectCollection))))
        return 0;
    
    CComPtr<IShellLink> scratchLink;
    if (checkHR(scratchLink.CoCreateInstance(__uuidof(ShellLink)))) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileName(GetModuleHandle(nullptr), exePath, MAX_PATH);

        checkHR(scratchLink->SetPath(exePath));
        checkHR(scratchLink->SetArguments(L"/scratch"));
        checkHR(scratchLink->SetIconLocation(exePath, IDR_APP_ICON));

        LocalHeapPtr<wchar_t> title;
        formatMessage(title, STR_NEW_SCRATCH_TASK);
        CComQIPtr<IPropertyStore> trayLinkProps(scratchLink);
        PROPVARIANT propVar;
        if (checkHR(InitPropVariantFromString(title, &propVar)))
            checkHR(trayLinkProps->SetValue(PKEY_Title, propVar));

        checkHR(tasks->AddObject(scratchLink));
    }

    checkHR(jumpList->AddUserTasks(tasks));
    checkHR(jumpList->CommitList());
    return 0;
}
