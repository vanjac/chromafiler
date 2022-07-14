#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include "PreviewWindow.h"
#include "TextWindow.h"
#include "TrayWindow.h"
#include "CreateItemWindow.h"
#include "Settings.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <propkey.h>
#include <Propvarutil.h>
#include <strsafe.h>
#include <VersionHelpers.h>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Comctl32.lib")

using namespace chromabrowse;

const wchar_t APP_ID[] = L"chroma.browse";

#ifdef CHROMABROWSE_DEBUG
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

#ifdef CHROMABROWSE_MEMLEAKS
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    debugPrintf(L"Compiled with memory leak detection\n");
#endif

    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLine(), &argc);

    if (!checkHR(OleInitialize(0))) // needed for drag/drop
        return 0;

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
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

        CComPtr<IShellItem> startItem;
        // parse name vs display name https://stackoverflow.com/q/42966489
        if (FAILED(SHCreateItemFromParsingName(path, nullptr, IID_PPV_ARGS(&startItem)))) {
            debugPrintf(L"Unable to locate item at path %s\n", path);
            return 0;
        }
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
        if (activeWindow && activeWindow->handleTopLevelMessage(&msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WaitForSingleObject(jumpListThread, INFINITE);
    CloseHandle(jumpListThread);
    updateJumpList(nullptr);

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

    CComPtr<IObjectCollection> tasks;
    if (!checkHR(tasks.CoCreateInstance(__uuidof(EnumerableObjectCollection))))
        return false;

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(GetModuleHandle(nullptr), exePath, MAX_PATH);

    CComPtr<IShellLink> trayLink;
    if (checkHR(trayLink.CoCreateInstance(__uuidof(ShellLink)))) {
        checkHR(trayLink->SetPath(exePath));
        checkHR(trayLink->SetArguments(L"/tray"));
        checkHR(trayLink->SetIconLocation(exePath, IDR_APP_ICON));
        CComQIPtr<IPropertyStore> trayLinkProps(trayLink);
        PROPVARIANT propVar;
        if (checkHR(InitPropVariantFromString(L"Open Tray", &propVar))) // TODO string resource
            trayLinkProps->SetValue(PKEY_Title, propVar);
        checkHR(tasks->AddObject(trayLink));
    }

    CComPtr<IShellItem> favoritesFolder;
    if (IsWindows10OrGreater()) {
        // Quick Access
        checkHR(SHCreateItemFromParsingName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}",
            nullptr, IID_PPV_ARGS(&favoritesFolder)));
    } else {
        checkHR(SHGetKnownFolderItem(FOLDERID_Links, KF_FLAG_DEFAULT, nullptr,
            IID_PPV_ARGS(&favoritesFolder)));
    }
    if (!favoritesFolder)
        return false;
    CComPtr<IEnumShellItems> enumItems;
    if (!checkHR(favoritesFolder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&enumItems))))
        return false;

    if (IsWindows10OrGreater()) {
        // TODO: hack!! the first 20 items in Quick Access are recents
        for (int i = 0; i < 20; i++) {
            CComPtr<IShellItem> tempItem;
            checkHR(enumItems->Next(1, &tempItem, nullptr));
        }
    }
    CComPtr<IShellItem> childItem;
    while (enumItems->Next(1, &childItem, nullptr) == S_OK) {
        CComHeapPtr<wchar_t> displayName, parsingName;
        if (!checkHR(childItem->GetDisplayName(SIGDN_NORMALDISPLAY, &displayName))
                || !checkHR(childItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsingName)))
            continue;
        wchar_t args[MAX_PATH];
        args[0] = L'"';
        checkHR(StringCchCopy(args + 1, MAX_PATH - 2, parsingName));
        checkHR(StringCchCat(args, MAX_PATH, L"\""));

        CComPtr<IShellLink> link;
        if (!checkHR(link.CoCreateInstance(__uuidof(ShellLink))))
            continue;
        checkHR(link->SetPath(exePath));
        checkHR(link->SetArguments(args));
        checkHR(link->SetIconLocation(exePath, IDR_APP_ICON));
        CComQIPtr<IPropertyStore> linkProps(link);
        PROPVARIANT propVar;
        if (checkHR(InitPropVariantFromString(displayName, &propVar)))
            linkProps->SetValue(PKEY_Title, propVar);
        CComPtr<IExtractIcon> extractIcon;
        if (checkHR(childItem->BindToHandler(nullptr, BHID_SFUIObject,
                IID_PPV_ARGS(&extractIcon)))) {
            wchar_t iconFile[MAX_PATH];
            int index;
            UINT flags;
            if (extractIcon->GetIconLocation(0, iconFile, MAX_PATH, &index, &flags) == S_OK) {
                if (!(flags & GIL_NOTFILENAME))
                    checkHR(link->SetIconLocation(iconFile, index));
            }
        }

        checkHR(tasks->AddObject(link));
        childItem = nullptr; // CComPtr isn't very smart
    }

    if (!checkHR(jumpList->AddUserTasks(tasks)))
        return false;
    if (!checkHR(jumpList->CommitList()))
        return false;

    debugPrintf(L"Successfully updated jump list\n");
    return true;
}
