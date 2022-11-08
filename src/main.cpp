#include "main.h"
#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include "PreviewWindow.h"
#include "TextWindow.h"
#include "TrayWindow.h"
#include "CreateItemWindow.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "Update.h"
#include "DPI.h"
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
#pragma comment(lib, "Wininet.lib")

using namespace chromafiler;

const wchar_t APP_ID[] = L"chroma.file";
const wchar_t SHELL_PREFIX[] = L"shell:";

#ifdef CHROMAFILER_DEBUG
int main(int, char**) {
    return wWinMain(nullptr, nullptr, nullptr, SW_SHOWNORMAL);
}

bool logHRESULT(long hr, const char *file, int line, const char *expr) {
    if (SUCCEEDED(hr))
        return true;
    LocalHeapPtr<wchar_t> message;
    formatErrorMessage(message, hr);
    debugPrintf(L"Error 0x%X: %s\n    in %S (%S:%d)\n", hr, &*message, expr, file, line);
    return false;
}

void logLastError(const char *file, int line, const char *expr) {
    DWORD error = GetLastError();
    LocalHeapPtr<wchar_t> message;
    formatErrorMessage(message, error);
    debugPrintf(L"Error %d: %s\n    in %S (%S:%d)\n", error, &*message, expr, file, line);
}
#endif

DWORD WINAPI checkLastVersion(void *);
void showWelcomeDialog();
HRESULT WINAPI welcomeDialogCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LONG_PTR data);
void startTrayProcess();
DWORD WINAPI checkForUpdates(void *);
DWORD WINAPI updateJumpList(void *);
DWORD WINAPI recoveryCallback(void *);

static UpdateInfo updateInfo;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand) {
    OutputDebugString(L"hiiiii ^w^\n"); // DO NOT REMOVE!!

#ifdef CHROMAFILER_MEMLEAKS
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    debugPrintf(L"Compiled with memory leak detection\n");
#endif

    if (!checkHR(OleInitialize(0))) // needed for drag/drop
        return 0;

    INITCOMMONCONTROLSEX controls = {sizeof(controls)};
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    initDPI();
    ItemWindow::init();
    FolderWindow::init();
    ThumbnailWindow::init();
    PreviewWindow::init();
    TextWindow::init();
    TrayWindow::init();

    // https://docs.microsoft.com/en-us/windows/win32/shell/appids
    checkHR(SetCurrentProcessExplicitAppUserModelID(APP_ID));

    {
        int argc;
        wchar_t **argv = CommandLineToArgvW(GetCommandLine(), &argc);

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
        if (tray) {
            initialWindow.Attach(new TrayWindow(nullptr, startItem));
            checkHR(RegisterApplicationRecoveryCallback(recoveryCallback, nullptr,
                RECOVERY_DEFAULT_PING_INTERVAL, 0));
        } else if (scratch) {
            CComPtr<IShellItem> scratchFile = createScratchFile(startItem);
            if (!scratchFile)
                return 0;
            initialWindow.Attach(new TextWindow(nullptr, scratchFile, true));
        } else {
            initialWindow = createItemWindow(nullptr, startItem);
        }
        RECT rect;
        if (tray) {
            rect = ((TrayWindow *)initialWindow.p)->requestedRect();
        } else {
            SIZE size = initialWindow->requestedSize();
            rect = {CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT + size.cx, CW_USEDEFAULT + size.cy};
        }
        initialWindow->create(rect, showCommand);

        LocalFree(argv);
    }

    HANDLE jumpListThread = nullptr, versionThread = nullptr, updateCheckThread = nullptr;
    SHCreateThreadWithHandle(updateJumpList, nullptr, CTF_COINIT_STA, nullptr, &jumpListThread);
    SHCreateThreadWithHandle(checkLastVersion, nullptr, 0, nullptr, &versionThread);
    if (settings::getUpdateCheckEnabled()) {
        LONGLONG lastCheck = settings::getLastUpdateCheck();
        LONGLONG interval = settings::getUpdateCheckRate();
        FILETIME fileTime;
        GetSystemTimeAsFileTime(&fileTime); // https://stackoverflow.com/a/1695332/11525734
        LONGLONG curTime = (LONGLONG)fileTime.dwLowDateTime
            + ((LONGLONG)(fileTime.dwHighDateTime) << 32LL);
        if (curTime - lastCheck > interval) {
            settings::setLastUpdateCheck(curTime);
            SHCreateThreadWithHandle(checkForUpdates, nullptr, 0, nullptr, &updateCheckThread);
        }
    }

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
    WaitForSingleObject(versionThread, INFINITE);
    if (updateCheckThread) {
        WaitForSingleObject(updateCheckThread, INFINITE);
        checkLE(CloseHandle(updateCheckThread));
    }
    checkLE(CloseHandle(jumpListThread));
    checkLE(CloseHandle(versionThread));

    ItemWindow::uninit();
    PreviewWindow::uninit();
    OleUninitialize();

    return (int)msg.wParam;
}

DWORD WINAPI checkLastVersion(void *) {
    DWORD lastVersion = settings::getLastOpenedVersion();
    DWORD curVersion = makeVersion(CHROMAFILER_VERSION);
    if (lastVersion != curVersion) {
        settings::setLastOpenedVersion(curVersion);
        if (lastVersion == settings::DEFAULT_LAST_OPENED_VERSION)
            showWelcomeDialog();
    }
    return 0;
}

void showWelcomeDialog() {
    TASKDIALOGCONFIG config = {sizeof(config)};
    config.hInstance = GetModuleHandle(nullptr);
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    config.pszWindowTitle = MAKEINTRESOURCE(IDS_WELCOME_CAPTION);
    config.pszMainIcon = MAKEINTRESOURCE(IDR_APP_ICON);
    config.pszMainInstruction = MAKEINTRESOURCE(IDS_WELCOME_HEADER);
    config.pszContent = MAKEINTRESOURCE(IDS_WELCOME_BODY);
    TASKDIALOG_BUTTON buttons[] = {
        {IDS_WELCOME_TUTORIAL, MAKEINTRESOURCE(IDS_WELCOME_TUTORIAL)},
        {IDS_WELCOME_TRAY, MAKEINTRESOURCE(IDS_WELCOME_TRAY)},
        {IDS_WELCOME_BROWSER, MAKEINTRESOURCE(IDS_WELCOME_BROWSER)}};
    config.cButtons = _countof(buttons);
    config.pButtons = buttons;
    config.nDefaultButton = IDCLOSE;
    config.pfCallback = welcomeDialogCallback;
    checkHR(TaskDialogIndirect(&config, nullptr, nullptr, nullptr));
}

HRESULT WINAPI welcomeDialogCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM, LONG_PTR) {
    if (msg == TDN_BUTTON_CLICKED && wParam == IDS_WELCOME_TUTORIAL) {
        ShellExecute(nullptr, L"open", L"https://github.com/vanjac/chromafiler/wiki/Tutorial",
            nullptr, nullptr, SW_SHOWNORMAL);
        return S_FALSE;
    } else if (msg == TDN_BUTTON_CLICKED && wParam == IDS_WELCOME_TRAY) {
        if (!TrayWindow::findTray()) {
            startTrayProcess();
        }
        settings::setTrayOpenOnStartup(true);
        return S_FALSE;
    } else if (msg == TDN_BUTTON_CLICKED && wParam == IDS_WELCOME_BROWSER) {
        HINSTANCE instance = GetModuleHandle(nullptr);
        if (!settings::supportsDefaultBrowser()) {
            TaskDialog(hwnd, instance, MAKEINTRESOURCE(IDS_BROWSER_SET_FAILED),
                nullptr, MAKEINTRESOURCE(IDS_REQUIRE_CONTEXT), 0, TD_ERROR_ICON, nullptr);
        } else {
            int result = 0;
            TaskDialog(hwnd, instance, MAKEINTRESOURCE(IDS_CONFIRM_CAPTION),
                nullptr, MAKEINTRESOURCE(IDS_BROWSER_SET_CONFIRM),
                TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON, nullptr, &result);
            if (result == IDYES)
                settings::setDefaultBrowser(true);
        }
        return S_FALSE;
    }
    return S_OK;
}

void startTrayProcess() {
    wchar_t exePath[MAX_PATH];
    if (checkLE(GetModuleFileName(GetModuleHandle(nullptr), exePath, MAX_PATH))) {
        STARTUPINFO startup = {sizeof(startup)};
        PROCESS_INFORMATION info = {};
        checkLE(CreateProcess(exePath, L"ChromaFiler.exe /tray", nullptr, nullptr, FALSE,
            DETACHED_PROCESS, nullptr, nullptr, &startup, &info));
        checkLE(CloseHandle(info.hProcess));
        checkLE(CloseHandle(info.hThread));
    }
}

LRESULT CALLBACK notifyWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_USER) {
        if (LOWORD(lParam) == NIN_BALLOONUSERCLICK) {
            openUpdate(updateInfo);
            PostQuitMessage(0);
        } else if (LOWORD(lParam) == NIN_BALLOONTIMEOUT || LOWORD(lParam) == NIN_BALLOONHIDE) {
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

DWORD WINAPI checkForUpdates(void *) {
    debugPrintf(L"Checking for updates...\n");
    if (checkUpdate(&updateInfo))
        return 0;
    DWORD lastVersion = settings::getLastUpdateVersion();
    settings::setLastUpdateVersion(updateInfo.version);
    if (!updateInfo.isNewer || updateInfo.version <= lastVersion)
        return 0;
    debugPrintf(L"Update available!\n");

    HINSTANCE instance = GetModuleHandle(nullptr);
    WNDCLASS notifyClass = {};
    notifyClass.lpfnWndProc = notifyWindowProc;
    notifyClass.hInstance = instance;
    notifyClass.lpszClassName = L"ChromaFile Update Notify";
    if (!checkLE(RegisterClass(&notifyClass)))
        return 0;
    HWND messageWindow = checkLE(CreateWindow(notifyClass.lpszClassName, L"ChromaFiler", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr));
    if (!messageWindow)
        return 0;

    // https://learn.microsoft.com/en-us/windows/win32/shell/notification-area
    NOTIFYICONDATA notify = {sizeof(notify)};
    notify.uFlags = NIF_INFO | NIF_ICON | NIF_MESSAGE;
    notify.uCallbackMessage = WM_USER;
    notify.hWnd = messageWindow;
    notify.dwInfoFlags = NIIF_RESPECT_QUIET_TIME;
    notify.uVersion = NOTIFYICON_VERSION_4;
    wchar_t content[] =
        L"A new version of ChromaFiler is available. Click the icon below to download.";
    wchar_t title[] = L"ChromaFiler update";
    CopyMemory(notify.szInfo, content, sizeof(content));
    CopyMemory(notify.szInfoTitle, title, sizeof(title));
    checkHR(LoadIconMetric(instance, MAKEINTRESOURCE(IDR_APP_ICON), LIM_SMALL,
        &notify.hIcon));
    if (!checkLE(Shell_NotifyIcon(NIM_ADD, &notify)))
        return 0;
    checkLE(Shell_NotifyIcon(NIM_SETVERSION, &notify));

    // keep thread running to keep message window open
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    checkLE(Shell_NotifyIcon(NIM_DELETE, &notify));
    return 0;
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
        if (checkLE(GetModuleFileName(GetModuleHandle(nullptr), exePath, MAX_PATH))) {
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
    }

    checkHR(jumpList->AddUserTasks(tasks));
    checkHR(jumpList->CommitList());
    return 0;
}

DWORD WINAPI recoveryCallback(void *) {
    BOOL cancelled;
    ApplicationRecoveryInProgress(&cancelled);
    startTrayProcess();
    ApplicationRecoveryFinished(true);
    return 0;
}

namespace chromafiler {    
    // main.h
    CComPtr<ItemWindow> activeWindow;

    static long numOpenWindows = 0;

    void windowOpened() {
        InterlockedIncrement(&numOpenWindows);
    }

    void windowClosed() {
        if (InterlockedDecrement(&numOpenWindows) == 0)
            PostQuitMessage(0);
    }
}
