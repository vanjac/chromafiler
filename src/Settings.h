#pragma once
#include <common.h>

#include <windows.h>

namespace chromabrowse::settings {

POINT getTrayPosition();
void setTrayPosition(POINT value);
SIZE getTraySize();
void setTraySize(SIZE value);

} // namespace
