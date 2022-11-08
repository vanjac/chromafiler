#include "Update.h"
#include "resource.h"
#include <wininet.h>
#include <shellapi.h>

namespace chromafiler {

const wchar_t UPDATE_URL[] = L"https://chroma.zone/dist/chromafiler-update-release.txt";
const int MAX_DOWNLOAD_SIZE = 1024;

DWORD checkUpdate(UpdateInfo *info) {
    CComHeapPtr<char> data;
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
            CComHeapPtr<char> buffer;
            buffer.AllocateBytes(bufferSize);
            if (!checkLE(InternetReadFile(connection, buffer, bufferSize, &downloadedSize))) {
                error = GetLastError();
                break;
            }
            if (downloadedSize == 0)
                break;

            data.ReallocateBytes(dataSize + downloadedSize + 1);
            CopyMemory(data + dataSize, buffer, downloadedSize);
            dataSize += downloadedSize;
            error = 0;
        }
        data[dataSize] = 0; // null terminator
        InternetCloseHandle(connection);
    }
    InternetCloseHandle(internet);

    if (error)
        return error;
    debugPrintf(L"Downloaded content: %S\n", &*data);
    if (memcmp(data, "CFUP", 4)) {
        debugPrintf(L"Update data is missing prefix!\n");
        return (DWORD)E_FAIL;
    }
    if (dataSize < 31) {
        debugPrintf(L"Update data is too small!\n");
        return (DWORD)E_FAIL;
    }
    char hexString[11] = "0x";
    CopyMemory(hexString + 2, data + 5, 8);
    hexString[10] = 0;
    if (!StrToIntExA(hexString, STIF_SUPPORT_HEX, (int *)&info->version)) {
        debugPrintf(L"Can't parse update version!\n");
        return (DWORD)E_FAIL;
    }
    info->isNewer = info->version > makeVersion(CHROMAFILER_VERSION);

    char *url = data + 14, *urlEnd = StrChrA(url, '\n');
    size_t urlLen = urlEnd ? (urlEnd - url) : lstrlenA(url);
    info->url.Allocate(urlLen + 1);
    CopyMemory(info->url, url, urlLen);
    info->url[urlLen] = 0;
    return S_OK;
}

void openUpdate(const UpdateInfo &info) {
    ShellExecuteA(nullptr, "open", &*info.url, nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace
