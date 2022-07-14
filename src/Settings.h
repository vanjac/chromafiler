#pragma once
#include <common.h>

#include <windows.h>

namespace chromabrowse::settings {

SIZE getItemWindowSize();
void setItemWindowSize(SIZE value);

SIZE getFolderWindowSize();
void setFolderWindowSize(SIZE value);

bool getPreviewsEnabled();
void setPreviewsEnabled(bool value);

bool getTextEditorEnabled();
void setTextEditorEnabled(bool value);

POINT getTrayPosition();
void setTrayPosition(POINT value);
SIZE getTraySize();
void setTraySize(SIZE value);

} // namespace
