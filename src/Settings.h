#pragma once
#include <common.h>

#include <WinUtils.h>

namespace chromafiler {

enum TrayDirection : DWORD { TRAY_UP, TRAY_DOWN, TRAY_RIGHT };
enum TextEncoding : DWORD { ENC_UNK, ENC_UTF8, ENC_UTF8BOM, ENC_UTF16LE, ENC_UTF16BE, ENC_ANSI };
enum TextNewlines : DWORD { NL_UNK, NL_CRLF, NL_LF, NL_CR };

namespace settings {

#ifdef CHROMAFILER_DEBUG
extern bool testMode;
#endif

// http://smallvoid.com/article/winnt-shell-keyword.html
const DWORD     DEFAULT_LAST_OPENED_VERSION = 0;
const bool      DEFAULT_UPDATE_CHECK_ENABLED= false;
const LONGLONG  DEFAULT_LAST_UPDATE_CHECK   = 0;
const LONGLONG  DEFAULT_UPDATE_CHECK_RATE   = 10000000LL * 60 * 60 * 24 * 7; // 1 week
const bool      DEFAULT_ADMIN_WARNING       = true;
const wchar_t   DEFAULT_STARTING_FOLDER[]   = L"shell:Desktop";
const wchar_t   DEFAULT_SCRATCH_FOLDER[]    = L"shell:Desktop";
const wchar_t   DEFAULT_SCRATCH_FILE_NAME[] = L"scratch.txt";
const SIZE      DEFAULT_ITEM_WINDOW_SIZE    = {435, 435};
const SIZE      DEFAULT_FOLDER_WINDOW_SIZE  = {210, 435};
const bool      DEFAULT_STATUS_TEXT_ENABLED = true;
const bool      DEFAULT_TOOLBAR_ENABLED     = true;
const bool      DEFAULT_PREVIEWS_ENABLED    = true;
const UINT      DEFAULT_OPEN_SELECTION_TIME = 100; // slower than key repeat 10 and above
const bool      DEFAULT_DESELECT_ON_OPEN    = true;
const bool      DEFAULT_TEXT_EDITOR_ENABLED = true;
const LOGFONT   DEFAULT_TEXT_FONT           = {
    11, 0, 0, 0, FW_REGULAR, FALSE, FALSE, FALSE,
    ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
    DEFAULT_PITCH | FF_DONTCARE, L"Consolas" };
const int       DEFAULT_TEXT_TAB_WIDTH      = 4;
const bool      DEFAULT_TEXT_WRAP           = false;
const bool      DEFAULT_TEXT_AUTO_INDENT    = true;
const TextEncoding  DEFAULT_TEXT_ENCODING   = ENC_UTF8;
const bool      DEFAULT_TEXT_AUTO_ENCODING  = true;
const UINT      DEFAULT_TEXT_ANSI_CODEPAGE  = CP_ACP;
const TextNewlines  DEFAULT_TEXT_NEWLINES   = NL_CRLF;
const bool      DEFAULT_TEXT_AUTO_NEWLINES  = true;
const wchar_t   DEFAULT_TRAY_FOLDER[]       = L"shell:Links";
const int       DEFAULT_TRAY_DPI            = 96;
const POINT     DEFAULT_TRAY_POSITION       = {CW_USEDEFAULT, CW_USEDEFAULT};
const SIZE      DEFAULT_TRAY_SIZE           = {CW_USEDEFAULT, CW_USEDEFAULT};
const TrayDirection DEFAULT_TRAY_DIRECTION  = TRAY_UP;
const bool      DEFAULT_TRAY_HOTKEY_ENABLED = true;

DWORD getLastOpenedVersion();
void setLastOpenedVersion(DWORD value);
bool getUpdateCheckEnabled();
void setUpdateCheckEnabled(bool value);
LONGLONG getLastUpdateCheck();
void setLastUpdateCheck(LONGLONG value);
LONGLONG getUpdateCheckRate();
void setUpdateCheckRate(LONGLONG value); // TODO: add to Settings

bool getAdminWarning();
void setAdminWarning(bool value);

wstr_ptr getStartingFolder();
void setStartingFolder(wchar_t *value);
wstr_ptr getScratchFolder();
void setScratchFolder(wchar_t *value);
wstr_ptr getScratchFileName();
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

UINT getOpenSelectionTime(); // milliseconds
void setOpenSelectionTime(UINT value); // TODO: add to Settings
bool getDeselectOnOpen();
void setDeselectOnOpen(bool value); // TODO: add to Settings

bool getTextEditorEnabled();
void setTextEditorEnabled(bool value);
LOGFONT getTextFont(); // height value is POSITIVE in POINTS (not typical)
void setTextFont(const LOGFONT &value);
int getTextTabWidth();
void setTextTabWidth(int value);
bool getTextWrap();
void setTextWrap(bool value);
bool getTextAutoIndent();
void setTextAutoIndent(bool value);
TextEncoding getTextDefaultEncoding();
void setTextDefaultEncoding(TextEncoding value);
bool getTextAutoEncoding();
void setTextAutoEncoding(bool value);
UINT getTextAnsiCodepage();
void setTextAnsiCodepage(UINT value); // TODO: add to Settings
TextNewlines getTextDefaultNewlines();
void setTextDefaultNewlines(TextNewlines value);
bool getTextAutoNewlines();
void setTextAutoNewlines(bool value);

bool getTrayOpenOnStartup();
void setTrayOpenOnStartup(bool value);
wstr_ptr getTrayFolder();
void setTrayFolder(wchar_t *value);
int getTrayDPI();
void setTrayDPI(int value);
POINT getTrayPosition(); // using TrayDPI
void setTrayPosition(POINT value);
SIZE getTraySize(); // using TrayDPI
void setTraySize(SIZE value);
TrayDirection getTrayDirection();
void setTrayDirection(TrayDirection value);
bool getTrayHotKeyEnabled();
void setTrayHotKeyEnabled(bool value);

bool supportsDefaultBrowser();
void setDefaultBrowser(bool value);

}} // namespace
