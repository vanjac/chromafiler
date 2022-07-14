#pragma once
#include <common.h>

#include <windows.h>

namespace chromabrowse::settings {

SIZE getItemWindowSize();
void setItemWindowSize(SIZE value);

SIZE getFolderWindowSize();
void setFolderWindowSize(SIZE value);

POINT getTrayPosition();
void setTrayPosition(POINT value);
SIZE getTraySize();
void setTraySize(SIZE value);

} // namespace
