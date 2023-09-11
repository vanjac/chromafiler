#include "common.h"
#include <Windows.h>
#include <Shlwapi.h>

int __stdcall wWinMainCRTStartup() {
    HANDLE heap = GetProcessHeap();

    wchar_t exePath[MAX_PATH];
    exePath[0] = 0;
    GetModuleFileName(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpec(exePath);
    PathAppend(exePath, L"ChromaFiler.exe");

    const wchar_t *cmdLine = GetCommandLine();
    int cmdLineLen = lstrlen(cmdLine);

    const wchar_t APPEND[] = L" /text";
    int size = sizeof(wchar_t) * (cmdLineLen + _countof(APPEND) + 1);
    wchar_t *cmdLineFull = (wchar_t *)HeapAlloc(heap, 0, size);
    lstrcpy(cmdLineFull, cmdLine);
    lstrcpy(cmdLineFull + cmdLineLen, APPEND);
    OutputDebugString(cmdLineFull);

    STARTUPINFO startup;
    startup.cb = sizeof(startup);
    GetStartupInfo(&startup);

    PROCESS_INFORMATION info;
    info.hProcess = nullptr;
    info.hThread = nullptr;
    checkLE(CreateProcess(exePath, cmdLineFull, nullptr, nullptr, TRUE,
        DETACHED_PROCESS, nullptr, nullptr, &startup, &info));
    checkLE(CloseHandle(info.hProcess));
    checkLE(CloseHandle(info.hThread));

    HeapFree(heap, 0, cmdLineFull);
    return 0;
}
