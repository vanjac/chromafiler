#pragma once

//#define DEBUG

#define APP_ID L"chroma.browse"

// win32
#ifndef UNICODE
#define UNICODE
#endif
#define STRICT
#define WIN32_LEAN_AND_MEAN
// target Windows 7
// https://docs.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601


#ifdef DEBUG
#define debugPrintf wprintf
#else
#define debugPrintf(...)
#endif
