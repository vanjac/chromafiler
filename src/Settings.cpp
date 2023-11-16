#include "Settings.h"
#include <strsafe.h>
#include "UIStrings.h"

namespace chromafiler {
namespace settings {

const wchar_t KEY_SETTINGS[]            = L"Software\\ChromaFiler";

const wchar_t KEY_STARTUP[]             = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t VAL_STARTUP[]             = L"ChromaFiler";
const wchar_t KEY_DIRECTORY_VERB[]      = L"Directory\\shell\\chromafiler";
const wchar_t KEY_DIRECTORY_BROWSER[]   = L"Software\\Classes\\Directory\\Shell";
const wchar_t KEY_COMPRESSED_BROWSER[]  = L"Software\\Classes\\CompressedFolder\\Shell";
const wchar_t KEY_DRIVE_BROWSER[]       = L"Software\\Classes\\Drive\\Shell";
const wchar_t DATA_BROWSER_SET[]        = L"chromafiler";
const wchar_t DATA_BROWSER_CLEAR[]      = L"none";

// type should be a RRF_RT_* constant
// data should already contain default value
LSTATUS getSettingsValue(const wchar_t *name, DWORD type, void *data, DWORD size) {
    return RegGetValue(HKEY_CURRENT_USER, KEY_SETTINGS, name, type, nullptr, data, &size);
}

// type should be a REG_* constant (unlike getSettingsValue!)
LSTATUS setSettingsValue(const wchar_t *name, DWORD type, const void *data, DWORD size) {
    return RegSetKeyValue(HKEY_CURRENT_USER, KEY_SETTINGS, name, type, data, size);
}

wstr_ptr getSettingsString(const wchar_t *name, DWORD type, const wchar_t *defaultValue) {
    DWORD size;
    if (!RegGetValue(HKEY_CURRENT_USER, KEY_SETTINGS, name, type, nullptr, nullptr, &size)) {
        wstr_ptr buffer(new wchar_t[size / sizeof(wchar_t)]);
        RegGetValue(HKEY_CURRENT_USER, KEY_SETTINGS, name, type, nullptr, buffer.get(), &size);
        return buffer;
    } else {
        DWORD count = lstrlen(defaultValue) + 1;
        wstr_ptr buffer(new wchar_t[count]);
        CopyMemory(buffer.get(), defaultValue, count * sizeof(wchar_t));
        return buffer;
    }
}

void setSettingsString(const wchar_t *name, DWORD type, const wchar_t *value) {
    setSettingsValue(name, type, value, (lstrlen(value) + 1) * sizeof(wchar_t));
}

#define SETTINGS_DWORD_VALUE(funcName, type, valueName, defaultValue) \
    type get##funcName() {                                                                         \
        DWORD value = (defaultValue);                                                              \
        getSettingsValue((valueName), RRF_RT_DWORD, &value, sizeof(value));                        \
        return (type)value;                                                                        \
    }                                                                                              \
    void set##funcName(type value) {                                                               \
        DWORD dwValue = (DWORD)value;                                                              \
        setSettingsValue((valueName), REG_DWORD, &dwValue, sizeof(dwValue));                       \
    }

#define SETTINGS_QWORD_VALUE(funcName, type, valueName, defaultValue) \
    type get##funcName() {                                                                         \
        ULONGLONG value = (defaultValue);                                                          \
        getSettingsValue((valueName), RRF_RT_QWORD, &value, sizeof(value));                        \
        return (type)value;                                                                        \
    }                                                                                              \
    void set##funcName(type value) {                                                               \
        ULONGLONG dwValue = (ULONGLONG)value;                                                      \
        setSettingsValue((valueName), REG_QWORD, &dwValue, sizeof(dwValue));                       \
    }

#define SETTINGS_BOOL_VALUE(funcName, valueName, defaultValue) \
    SETTINGS_DWORD_VALUE(funcName, bool, valueName, defaultValue)

#define SETTINGS_SIZE_VALUE(funcName, valueName, defaultValue) \
    SIZE get##funcName() {                                                                         \
        SIZE value = (defaultValue);                                                               \
        getSettingsValue(valueName L"Width", RRF_RT_DWORD, &value.cx, sizeof(value.cx));           \
        getSettingsValue(valueName L"Height", RRF_RT_DWORD, &value.cy, sizeof(value.cy));          \
        return value;                                                                              \
    }                                                                                              \
    void set##funcName(SIZE value) {                                                               \
        setSettingsValue(valueName L"Width", REG_DWORD, &value.cx, sizeof(value.cx));              \
        setSettingsValue(valueName L"Height", REG_DWORD, &value.cy, sizeof(value.cy));             \
    }

#define SETTINGS_POINT_VALUE(funcName, valueName, defaultValue) \
    POINT get##funcName() {                                                                        \
        POINT value = (defaultValue);                                                              \
        getSettingsValue(valueName L"X", RRF_RT_DWORD, &value.x, sizeof(value.x));                 \
        getSettingsValue(valueName L"Y", RRF_RT_DWORD, &value.y, sizeof(value.y));                 \
        return value;                                                                              \
    }                                                                                              \
    void set##funcName(POINT value) {                                                              \
        setSettingsValue(valueName L"X", REG_DWORD, &value.x, sizeof(value.x));                    \
        setSettingsValue(valueName L"Y", REG_DWORD, &value.y, sizeof(value.y));                    \
    }

#define SETTINGS_STRING_VALUE(funcName, regType, valueName, defaultValue) \
    wstr_ptr get##funcName() {                                                                     \
        return getSettingsString((valueName), RRF_RT_REG_SZ, (defaultValue));                      \
    }                                                                                              \
    void set##funcName(wchar_t *value) {                                                           \
        setSettingsString((valueName), (regType), value);                                          \
    }

#define SETTINGS_FONT_VALUE(funcName, valueName, defaultValue) \
    LOGFONT get##funcName() {                                                                      \
        LOGFONT value = (defaultValue);                                                            \
        getSettingsValue( /* NOT getSettingsString since lfFaceName has a fixed size */            \
            valueName L"Face", RRF_RT_REG_SZ, value.lfFaceName, sizeof(value.lfFaceName));         \
        getSettingsValue(valueName L"Size", REG_DWORD, &value.lfHeight, sizeof(value.lfHeight));   \
        getSettingsValue(valueName L"Weight", REG_DWORD, &value.lfWeight, sizeof(value.lfWeight)); \
        DWORD italic = value.lfItalic;                                                             \
        getSettingsValue(valueName L"Italic", REG_DWORD, &italic, sizeof(italic));                 \
        value.lfItalic = (BYTE)italic;                                                             \
        return value;                                                                              \
    }                                                                                              \
    void set##funcName(const LOGFONT &value) {                                                     \
        setSettingsString(valueName L"Face", REG_SZ, value.lfFaceName);                            \
        setSettingsValue(valueName L"Size", REG_DWORD, &value.lfHeight, sizeof(value.lfHeight));   \
        setSettingsValue(valueName L"Weight", REG_DWORD, &value.lfWeight, sizeof(value.lfWeight)); \
        DWORD italic = value.lfItalic;                                                             \
        setSettingsValue(valueName L"Italic", REG_DWORD, &italic, sizeof(italic));                 \
    }


SETTINGS_DWORD_VALUE(LastOpenedVersion, DWORD, L"LastOpenedVersion", DEFAULT_LAST_OPENED_VERSION)
SETTINGS_BOOL_VALUE(UpdateCheckEnabled, L"UpdateCheckEnabled", DEFAULT_UPDATE_CHECK_ENABLED);
SETTINGS_QWORD_VALUE(LastUpdateCheck, LONGLONG, L"LastUpdateCheck", DEFAULT_LAST_UPDATE_CHECK)
SETTINGS_QWORD_VALUE(UpdateCheckRate, LONGLONG, L"UpdateCheckRate", DEFAULT_UPDATE_CHECK_RATE)

SETTINGS_STRING_VALUE(StartingFolder, REG_EXPAND_SZ, L"StartingFolder", DEFAULT_STARTING_FOLDER)
SETTINGS_STRING_VALUE(ScratchFolder, REG_EXPAND_SZ, L"ScratchFolder", DEFAULT_SCRATCH_FOLDER)
SETTINGS_STRING_VALUE(ScratchFileName, REG_SZ, L"ScratchFileName", DEFAULT_SCRATCH_FILE_NAME)

SETTINGS_SIZE_VALUE(ItemWindowSize, L"ItemWindow2", DEFAULT_ITEM_WINDOW_SIZE)
SETTINGS_SIZE_VALUE(FolderWindowSize, L"FolderWindow2", DEFAULT_FOLDER_WINDOW_SIZE)

SETTINGS_BOOL_VALUE(StatusTextEnabled, L"StatusTextEnabled", DEFAULT_STATUS_TEXT_ENABLED)
SETTINGS_BOOL_VALUE(ToolbarEnabled, L"ToolbarEnabled", DEFAULT_TOOLBAR_ENABLED)

SETTINGS_BOOL_VALUE(PreviewsEnabled, L"PreviewsEnabled", DEFAULT_PREVIEWS_ENABLED)

SETTINGS_DWORD_VALUE(OpenSelectionTime, UINT, L"OpenSelectionTime", DEFAULT_OPEN_SELECTION_TIME)
SETTINGS_BOOL_VALUE(DeselectOnOpen, L"DeselectOnOpen", DEFAULT_DESELECT_ON_OPEN)

SETTINGS_BOOL_VALUE(TextEditorEnabled, L"TextEditorEnabled2", DEFAULT_TEXT_EDITOR_ENABLED)
SETTINGS_FONT_VALUE(TextFont, L"TextFont", DEFAULT_TEXT_FONT)
SETTINGS_DWORD_VALUE(TextTabWidth, int, L"TextTabWidth", DEFAULT_TEXT_TAB_WIDTH)
SETTINGS_BOOL_VALUE(TextWrap, L"TextWrap", DEFAULT_TEXT_WRAP)
SETTINGS_BOOL_VALUE(TextAutoIndent, L"TextAutoIndent", DEFAULT_TEXT_AUTO_INDENT)
SETTINGS_DWORD_VALUE(TextDefaultEncoding, TextEncoding,
    L"TextDefaultEncoding", DEFAULT_TEXT_ENCODING)
SETTINGS_BOOL_VALUE(TextAutoEncoding, L"TextAutoEncoding", DEFAULT_TEXT_AUTO_ENCODING)
SETTINGS_DWORD_VALUE(TextAnsiCodepage, UINT, L"TextAnsiCodepage", DEFAULT_TEXT_ANSI_CODEPAGE)
SETTINGS_DWORD_VALUE(TextDefaultNewlines, TextNewlines,
    L"TextDefaultNewlines", DEFAULT_TEXT_NEWLINES)
SETTINGS_BOOL_VALUE(TextAutoNewlines, L"TextAutoNewlines", DEFAULT_TEXT_AUTO_NEWLINES)

bool getTrayOpenOnStartup() {
    return !RegGetValue(HKEY_CURRENT_USER, KEY_STARTUP, VAL_STARTUP,
        RRF_RT_ANY, nullptr, nullptr, nullptr);
}

void setTrayOpenOnStartup(bool value) {
    if (value) {
        if (getTrayOpenOnStartup())
            return; // don't overwrite existing command
        wchar_t exePath[MAX_PATH];
        if (checkLE(GetModuleFileName(nullptr, exePath, _countof(exePath)))) {
            local_wstr_ptr command = format(L"\"%1\" /tray", exePath);
            RegSetKeyValue(HKEY_CURRENT_USER, KEY_STARTUP, VAL_STARTUP, REG_EXPAND_SZ,
                command.get(), (lstrlen(command.get()) + 1) * sizeof(wchar_t));
        }
    } else {
        RegDeleteKeyValue(HKEY_CURRENT_USER, KEY_STARTUP, VAL_STARTUP);
    }
}

SETTINGS_STRING_VALUE(TrayFolder, REG_EXPAND_SZ, L"TrayFolder", DEFAULT_TRAY_FOLDER)
SETTINGS_DWORD_VALUE(TrayDPI, int, L"TrayDPI", DEFAULT_TRAY_DPI)
SETTINGS_POINT_VALUE(TrayPosition, L"Tray", DEFAULT_TRAY_POSITION)
SETTINGS_SIZE_VALUE(TraySize, L"Tray", DEFAULT_TRAY_SIZE)
SETTINGS_DWORD_VALUE(TrayDirection, TrayDirection, L"TrayDirection", DEFAULT_TRAY_DIRECTION)

bool supportsDefaultBrowser() {
    return !RegGetValue(HKEY_CLASSES_ROOT, KEY_DIRECTORY_VERB, L"",
        RRF_RT_ANY, nullptr, nullptr, nullptr);
}

void setDefaultBrowser(bool value) {
    const wchar_t *data = value ? DATA_BROWSER_SET : DATA_BROWSER_CLEAR;
    DWORD size = value ? sizeof(DATA_BROWSER_SET) : sizeof(DATA_BROWSER_CLEAR);
    RegSetKeyValue(HKEY_CURRENT_USER, KEY_DIRECTORY_BROWSER, L"", REG_SZ, data, size);
    RegSetKeyValue(HKEY_CURRENT_USER, KEY_COMPRESSED_BROWSER, L"", REG_SZ, data, size);
    RegSetKeyValue(HKEY_CURRENT_USER, KEY_DRIVE_BROWSER, L"", REG_SZ, data, size);
}

}} // namespace
