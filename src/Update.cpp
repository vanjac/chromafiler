#include "Update.h"
#include "Settings.h"
#include "resource.h"
#include <vector>
#include <wininet.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <CommCtrl.h>

namespace chromafiler {

const wchar_t UPDATE_URL[] = L"https://chroma.zone/dist/chromafiler-update-release.txt";
const int MAX_DOWNLOAD_SIZE = 1024;

static UpdateInfo updateInfo;
static HANDLE updateThread = nullptr;

DWORD WINAPI updateCheckProc(void *);

void autoUpdateCheck() {
    if (settings::getUpdateCheckEnabled()) {
        LONGLONG lastCheck = settings::getLastUpdateCheck();
        LONGLONG interval = settings::getUpdateCheckRate();
        FILETIME fileTime;
        GetSystemTimeAsFileTime(&fileTime); // https://stackoverflow.com/a/1695332/11525734
        LONGLONG curTime = (LONGLONG)fileTime.dwLowDateTime
            + ((LONGLONG)(fileTime.dwHighDateTime) << 32LL);
        if (curTime - lastCheck > interval) {
            if (updateThread) {
                if (WaitForSingleObject(updateThread, 0) == WAIT_OBJECT_0) {
                    checkLE(CloseHandle(updateThread));
                    updateThread = nullptr;
                } else {
                    return; // still running!
                }
            }
            settings::setLastUpdateCheck(curTime);
            SHCreateThreadWithHandle(updateCheckProc, nullptr, 0, nullptr, &updateThread);
        }
    }
}

void waitForUpdateThread() {
    if (updateThread) {
        WaitForSingleObject(updateThread, INFINITE);
        checkLE(CloseHandle(updateThread));
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

DWORD WINAPI updateCheckProc(void *) {
    debugPrintf(L"Checking for updates...\n");
    if (checkUpdate(&updateInfo) || !updateInfo.isNewer)
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

DWORD checkUpdate(UpdateInfo *info) {
    std::vector<char> data;
    DWORD dataSize = 0;
    DWORD error = (DWORD)E_FAIL;

    // https://learn.microsoft.com/en-us/windows/win32/wininet/http-sessions
    HINTERNET internet = InternetOpen(L"ChromaFiler/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr, nullptr, 0);
    if (!checkLE(internet))
        return GetLastError();
    HINTERNET connection = InternetOpenUrl(internet, UPDATE_URL, nullptr, 0,
        INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_RELOAD, 0);
    if (!checkLE(connection)) {
        error = GetLastError();
    } else {
        while (dataSize < MAX_DOWNLOAD_SIZE) {
            DWORD bufferSize, downloadedSize;
            if (!checkLE(InternetQueryDataAvailable(connection, &bufferSize, 0, 0))) {
                error = GetLastError();
                break;
            }
            if (dataSize + bufferSize > MAX_DOWNLOAD_SIZE)
                bufferSize = MAX_DOWNLOAD_SIZE - dataSize;
            data.resize(dataSize + bufferSize);
            if (!checkLE(InternetReadFile(connection, data.data() + dataSize, bufferSize,
                    &downloadedSize))) {
                error = GetLastError();
                break;
            }
            if (downloadedSize == 0)
                break;

            dataSize += downloadedSize;
            error = 0;
        }
        data.resize(dataSize + 1);
        data[dataSize] = 0; // null terminator
        InternetCloseHandle(connection);
    }
    InternetCloseHandle(internet);

    if (error)
        return error;
    debugPrintf(L"Downloaded content: %S\n", data.data());
    if (memcmp(data.data(), "CFUP", 4)) {
        debugPrintf(L"Update data is missing prefix!\n");
        return (DWORD)E_FAIL;
    }
    if (dataSize < 31) {
        debugPrintf(L"Update data is too small!\n");
        return (DWORD)E_FAIL;
    }
    char hexString[11] = "0x";
    CopyMemory(hexString + 2, data.data() + 5, 8);
    hexString[10] = 0;
    if (!StrToIntExA(hexString, STIF_SUPPORT_HEX, (int *)&info->version)) {
        debugPrintf(L"Can't parse update version!\n");
        return (DWORD)E_FAIL;
    }
    info->isNewer = info->version > makeVersion(CHROMAFILER_VERSION);

    char *url = data.data() + 14, *urlEnd = StrChrA(url, '\n');
    size_t urlLen = urlEnd ? (urlEnd - url) : lstrlenA(url);
    info->url = std::unique_ptr<char[]>(new char[urlLen + 1]);
    CopyMemory(info->url.get(), url, urlLen);
    info->url[urlLen] = 0;
    return S_OK;
}

void openUpdate(const UpdateInfo &info) {
    ShellExecuteA(nullptr, "open", info.url.get(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace
