#pragma once
#include <common.h>

#include <windows.h>
#include <atlbase.h>

namespace chromabrowse {
namespace settings {

enum TrayDirection : DWORD { TRAY_UP, TRAY_DOWN, TRAY_RIGHT };

// http://smallvoid.com/article/winnt-shell-keyword.html
const wchar_t   DEFAULT_STARTING_FOLDER[]   = L"shell:Desktop";
const SIZE      DEFAULT_ITEM_WINDOW_SIZE    = {450, 450};
const SIZE      DEFAULT_FOLDER_WINDOW_SIZE  = {231, 450}; // just wide enough for scrollbar tooltips
const bool      DEFAULT_STATUS_TEXT_ENABLED = true;
const bool      DEFAULT_TOOLBAR_ENABLED     = true;
const bool      DEFAULT_PREVIEWS_ENABLED    = true;
const bool      DEFAULT_TEXT_EDITOR_ENABLED = false;
const wchar_t   DEFAULT_TRAY_FOLDER[]       = L"shell:Links";
const POINT     DEFAULT_TRAY_POSITION       = {CW_USEDEFAULT, CW_USEDEFAULT};
const SIZE      DEFAULT_TRAY_SIZE           = {600, 48};
const TrayDirection DEFAULT_TRAY_DIRECTION  = TRAY_UP;

void getStartingFolder(CComHeapPtr<wchar_t> &value);
void setStartingFolder(wchar_t *value);

SIZE getItemWindowSize();
void setItemWindowSize(SIZE value);

SIZE getFolderWindowSize();
void setFolderWindowSize(SIZE value);

bool getStatusTextEnabled();
void setStatusTextEnabled(bool value);
bool getToolbarEnabled();
void setToolbarEnabled(bool value);

bool getPreviewsEnabled();
void setPreviewsEnabled(bool value);

bool getTextEditorEnabled();
void setTextEditorEnabled(bool value);

bool getTrayOpenOnStartup();
void setTrayOpenOnStartup(bool value);
void getTrayFolder(CComHeapPtr<wchar_t> &value);
void setTrayFolder(wchar_t *value);
POINT getTrayPosition();
void setTrayPosition(POINT value);
SIZE getTraySize();
void setTraySize(SIZE value);

TrayDirection getTrayDirection();
void setTrayDirection(TrayDirection value);

}} // namespace
