#pragma once
#include <common.h>

namespace chromafiler {

const wchar_t APP_ID[] = L"chroma.file";

// app exits when all windows are closed
void lockProcess();
void unlockProcess();

} // namespace
