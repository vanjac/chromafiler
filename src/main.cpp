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
#include "ExecuteCommand.h"
#include "DPI.h"
#include "UIStrings.h"
#include "WinUtils.h"
#include <shellapi.h>
#include <shlobj.h>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Wininet.lib")

using namespace chromafiler;

const wchar_t SHELL_PREFIX[] = L"shell:";

enum LaunchType {LAUNCH_FAIL, LAUNCH_HEADLESS, LAUNCH_AUTO, LAUNCH_TRAY, LAUNCH_TEXT};

#ifdef CHROMAFILER_DEBUG
int main(int, char**) {
    return wWinMain(nullptr, nullptr, nullptr, SW_SHOWNORMAL);
}

bool logHRESULT(long hr, const char *file, int line, const char *expr) {
    if (SUCCEEDED(hr))
        return true;
    local_wstr_ptr message = getErrorMessage(hr);
    debugPrintf(L"Error 0x%X: %s\n    in %S (%S:%d)\n", hr, message.get(), expr, file, line);
    return false;
}

void logLastError(const char *file, int line, const char *expr) {
    DWORD error = GetLastError();
    local_wstr_ptr message = getErrorMessage(error);
    debugPrintf(L"Error %d: %s\n    in %S (%S:%d)\n", error, message.get(), expr, file, line);
}
#endif

LaunchType createWindowFromCommandLine(int argc, wchar_t **argv, int showCommand);
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
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_WNDW);
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

    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLine(), &argc);
    LaunchType type = createWindowFromCommandLine(argc, argv, showCommand);
    LocalFree(argv);
    if (type == LAUNCH_FAIL)
        return 0;

    CFExecuteFactory executeFactory;
    HRESULT regHR = E_FAIL;
    DWORD regCookie = 0;
    if (type == LAUNCH_TRAY) {
        checkHR(RegisterApplicationRecoveryCallback(recoveryCallback, nullptr,
            RECOVERY_DEFAULT_PING_INTERVAL, 0));
    } else {
        // https://devblogs.microsoft.com/oldnewthing/20100503-00/?p=14183
        regHR = CoRegisterClassObject(CLSID_CFExecute, &executeFactory,
            CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &regCookie);
        checkHR(regHR);
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
#ifdef CHROMAFILER_DEBUG
        ULONGLONG tick = GetTickCount64();
#endif
        if (handleSettingsDialogMessage(&msg))
            continue;
        if (activeWindow && activeWindow->handleTopLevelMessage(&msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
#ifdef CHROMAFILER_DEBUG
        ULONGLONG time = GetTickCount64() - tick;
        if (time > 100 && msg.message != WM_NCLBUTTONDOWN) {
            wchar_t className[64] = L"Unknown Class";
            GetClassName(msg.hwnd, className, _countof(className));
            debugPrintf(L"%s took %lld ms to handle 0x%04X!\n", className, time, msg.message);
        }
#endif
    }

    if (SUCCEEDED(regHR))
        checkHR(CoRevokeClassObject(regCookie));

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

#ifdef CHROMAFILER_MEMLEAKS
    if (MEMLEAK_COUNT) {
        auto message = format(L"Detected memory leak (count: %1!d!)", MEMLEAK_COUNT);
        MessageBox(nullptr, message.get(), L"Memory leak!", MB_ICONERROR);
    }
#endif
    debugPrintf(L"Goodbye\n");
    return (int)msg.wParam;
}

LaunchType createWindowFromCommandLine(int argc, wchar_t **argv, int showCommand) {
    wstr_ptr pathAlloc;
    wchar_t *path = nullptr;
    LaunchType type = LAUNCH_AUTO;
    bool scratch = false;
    for (int i = 1; i < argc; i++) {
        wchar_t *arg = argv[i];
        if (lstrcmpi(arg, L"/tray") == 0) {
            type = LAUNCH_TRAY;
        } else if (lstrcmpi(arg, L"/scratch") == 0) { // deprecated
            type = LAUNCH_TEXT;
            scratch = true;
        } else if (lstrcmpi(arg, L"/text") == 0) {
            type = LAUNCH_TEXT;
        } else if (lstrcmpi(arg, L"/embedding") == 0 || lstrcmpi(arg, L"-embedding") == 0) {
            // https://devblogs.microsoft.com/oldnewthing/20100503-00/?p=14183
            debugPrintf(L"Started as local COM server\n");
            return LAUNCH_HEADLESS;
        } else if (!path) {
            int argLen = lstrlen(arg);
            if (arg[argLen - 1] == '"')
                arg[argLen - 1] = '\\'; // fix weird CommandLineToArgvW behavior with \"

            if ((argLen >= 1 && arg[0] == ':') || (argLen >= _countof(SHELL_PREFIX)
                    && _memicmp(arg, SHELL_PREFIX, _countof(SHELL_PREFIX)) == 0)) {
                path = arg; // assume desktop absolute parsing name
            } else { // assume relative file system path
                pathAlloc = wstr_ptr(new wchar_t[MAX_PATH]);
                path = pathAlloc.get();
                if (!GetFullPathName(arg, MAX_PATH, path, nullptr))
                    path = arg;
            }
        }
    }

    if (!path) {
        if (scratch) {
            pathAlloc = settings::getScratchFolder();
        } else if (type == LAUNCH_TEXT) {
            pathAlloc = settings::getScratchFolder();
            scratch = true;
        } else if (type == LAUNCH_TRAY) {
            pathAlloc = settings::getTrayFolder();
        } else {
            pathAlloc = settings::getStartingFolder();
        }
        path = pathAlloc.get();
    }

    CComPtr<IShellItem> startItem = itemFromPath(path);
    if (!startItem)
        return LAUNCH_FAIL;
    startItem = resolveLink(startItem);
    if (scratch)
        startItem = createScratchFile(startItem);
    if (!startItem)
        return LAUNCH_FAIL;

    CComPtr<ItemWindow> initialWindow;
    if (type == LAUNCH_TRAY) {
        initialWindow.Attach(new TrayWindow(nullptr, startItem));
    } else if (type == LAUNCH_TEXT) {
        initialWindow.Attach(new TextWindow(nullptr, startItem, scratch));
    } else {
        initialWindow = createItemWindow(nullptr, startItem);
    }
    if (scratch)
        initialWindow->resetViewState();
    // TODO can we get the monitor passed to ShellExecuteEx?
    initialWindow->create(initialWindow->requestedRect(nullptr), showCommand);
    return type;
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
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS
        | TDF_VERIFICATION_FLAG_CHECKED;
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
    config.pszVerificationText = MAKEINTRESOURCE(IDS_WELCOME_UPDATE);
    config.pfCallback = welcomeDialogCallback;

    BOOL autoUpdateChecked;
    if (checkHR(TaskDialogIndirect(&config, nullptr, nullptr, &autoUpdateChecked)))
        settings::setUpdateCheckEnabled(autoUpdateChecked);
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
                TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON, TD_WARNING_ICON, &result);
            if (result == IDYES) {
                settings::setDefaultBrowser(true);
                TaskDialog(GetParent(hwnd), instance, MAKEINTRESOURCE(IDS_SUCCESS_CAPTION),
                    nullptr, MAKEINTRESOURCE(IDS_BROWSER_SET), 0, nullptr, nullptr);
            }
        }
        return S_FALSE;
    }
    return S_OK;
}

void startTrayProcess() {
    wchar_t exePath[MAX_PATH];
    if (checkLE(GetModuleFileName(nullptr, exePath, _countof(exePath)))) {
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
    if (!updateInfo.isNewer)
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
    LoadString(instance, IDS_UPDATE_NOTIF_INFO, notify.szInfo, _countof(notify.szInfo));
    LoadString(instance, IDS_UPDATE_NOTIF_TITLE, notify.szInfoTitle, _countof(notify.szInfoTitle));
    checkHR(LoadIconMetric(instance, MAKEINTRESOURCE(IDR_APP_ICON), LIM_SMALL, &notify.hIcon));
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
    // previous versions had a jump list, this will clear it
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
#ifdef CHROMAFILER_MEMLEAKS
    long MEMLEAK_COUNT;
#endif

    static long lockCount = 0;

    void lockProcess() {
        InterlockedIncrement(&lockCount);
    }

    void unlockProcess() {
        if (InterlockedDecrement(&lockCount) == 0)
            PostQuitMessage(0);
    }
}
