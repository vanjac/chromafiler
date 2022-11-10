#pragma once
#include <common.h>

#include <windows.h>
#include <atlbase.h>

namespace chromafiler {
namespace settings {

enum TrayDirection : DWORD { TRAY_UP, TRAY_DOWN, TRAY_RIGHT };

// http://smallvoid.com/article/winnt-shell-keyword.html
const DWORD     DEFAULT_LAST_OPENED_VERSION = 0;
const DWORD     DEFAULT_LAST_UPDATE_VERSION = 0;
const bool      DEFAULT_UPDATE_CHECK_ENABLED= false;
const LONGLONG  DEFAULT_LAST_UPDATE_CHECK   = 0;
const LONGLONG  DEFAULT_UPDATE_CHECK_RATE   = 10000000LL * 60 * 60 * 24; // 1 day
const wchar_t   DEFAULT_STARTING_FOLDER[]   = L"shell:Desktop";
const wchar_t   DEFAULT_SCRATCH_FOLDER[]    = L"shell:Desktop";
const wchar_t   DEFAULT_SCRATCH_FILE_NAME[] = L"scratch.txt";
const SIZE      DEFAULT_ITEM_WINDOW_SIZE    = {450, 450};
const SIZE      DEFAULT_FOLDER_WINDOW_SIZE  = {210, 450};
const bool      DEFAULT_STATUS_TEXT_ENABLED = true;
const bool      DEFAULT_TOOLBAR_ENABLED     = true;
const bool      DEFAULT_PREVIEWS_ENABLED    = true;
const bool      DEFAULT_DESELECT_ON_OPEN    = true;
const bool      DEFAULT_TEXT_EDITOR_ENABLED = true;
const LOGFONT   DEFAULT_TEXT_FONT           = {
    11, 0, 0, 0, FW_REGULAR, FALSE, FALSE, FALSE,
    ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
    DEFAULT_PITCH | FF_DONTCARE, L"Consolas" };
const bool      DEFAULT_TEXT_WRAP           = false;
const bool      DEFAULT_TEXT_AUTO_INDENT    = true;
const wchar_t   DEFAULT_TRAY_FOLDER[]       = L"shell:Links";
const int       DEFAULT_TRAY_DPI            = 96;
const POINT     DEFAULT_TRAY_POSITION       = {CW_USEDEFAULT, CW_USEDEFAULT};
const SIZE      DEFAULT_TRAY_SIZE           = {CW_USEDEFAULT, CW_USEDEFAULT};
const TrayDirection DEFAULT_TRAY_DIRECTION  = TRAY_UP;

DWORD getLastOpenedVersion();
void setLastOpenedVersion(DWORD value);
DWORD getLastUpdateVersion();
void setLastUpdateVersion(DWORD value);
bool getUpdateCheckEnabled();
void setUpdateCheckEnabled(bool value);
LONGLONG getLastUpdateCheck();
void setLastUpdateCheck(LONGLONG value);
LONGLONG getUpdateCheckRate();
void setUpdateCheckRate(LONGLONG value);

void getStartingFolder(CComHeapPtr<wchar_t> &value);
void setStartingFolder(wchar_t *value);
void getScratchFolder(CComHeapPtr<wchar_t> &value);
void setScratchFolder(wchar_t *value);
void getScratchFileName(CComHeapPtr<wchar_t> &value);
void setScratchFileName(wchar_t *value);

SIZE getItemWindowSize(); // assuming 96 dpi
void setItemWindowSize(SIZE value);
SIZE getFolderWindowSize(); // assuming 96 dpi
void setFolderWindowSize(SIZE value);

bool getStatusTextEnabled();
void setStatusTextEnabled(bool value);
bool getToolbarEnabled();
void setToolbarEnabled(bool value);

bool getPreviewsEnabled();
void setPreviewsEnabled(bool value);

bool getDeselectOnOpen();
void setDeselectOnOpen(bool value);

bool getTextEditorEnabled();
void setTextEditorEnabled(bool value);
LOGFONT getTextFont(); // height value is POSITIVE in POINTS (not typical)
void setTextFont(const LOGFONT &value);
bool getTextWrap();
void setTextWrap(bool value);
bool getTextAutoIndent();
void setTextAutoIndent(bool value);

bool getTrayOpenOnStartup();
void setTrayOpenOnStartup(bool value);
void getTrayFolder(CComHeapPtr<wchar_t> &value);
void setTrayFolder(wchar_t *value);
int getTrayDPI();
void setTrayDPI(int value);
POINT getTrayPosition(); // using TrayDPI
void setTrayPosition(POINT value);
SIZE getTraySize(); // using TrayDPI
void setTraySize(SIZE value);
TrayDirection getTrayDirection();
void setTrayDirection(TrayDirection value);

bool supportsDefaultBrowser();
void setDefaultBrowser(bool value);

}} // namespace
