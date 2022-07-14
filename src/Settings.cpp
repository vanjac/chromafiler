#include "Settings.h"

namespace chromabrowse::settings {

// http://smallvoid.com/article/winnt-shell-keyword.html
const wchar_t   DEFAULT_STARTING_FOLDER[]   = L"shell:Desktop";
const SIZE      DEFAULT_ITEM_WINDOW_SIZE    = {450, 450};
const SIZE      DEFAULT_FOLDER_WINDOW_SIZE  = {231, 450}; // just wide enough for scrollbar tooltips
const bool      DEFAULT_PREVIEWS_ENABLED    = true;
const bool      DEFAULT_TEXT_EDITOR_ENABLED = false;
const wchar_t   DEFAULT_TRAY_FOLDER[]       = L"shell:Links";
const POINT     DEFAULT_TRAY_POSITION       = {CW_USEDEFAULT, CW_USEDEFAULT};
const SIZE      DEFAULT_TRAY_SIZE           = {600, 48};
const TrayDirection DEFAULT_TRAY_DIRECTION  = TRAY_UP;

const wchar_t KEY_SETTINGS[]    = L"Software\\chromabrowse";

const wchar_t VAL_STARTING_FOLDER[]     = L"StartingFolder";
const wchar_t VAL_ITEM_WINDOW_W[]       = L"ItemWindowWidth";
const wchar_t VAL_ITEM_WINDOW_H[]       = L"ItemWindowHeight";
const wchar_t VAL_FOLDER_WINDOW_W[]     = L"FolderWindowWidth";
const wchar_t VAL_FOLDER_WINDOW_H[]     = L"FolderWindowHeight";
const wchar_t VAL_PREVIEWS_ENABLED[]    = L"PreviewsEnabled";
const wchar_t VAL_TEXT_EDITOR_ENABLED[] = L"TextEditorEnabled";
const wchar_t VAL_TRAY_FOLDER[]         = L"TrayFolder";
const wchar_t VAL_TRAY_X[]              = L"TrayX";
const wchar_t VAL_TRAY_Y[]              = L"TrayY";
const wchar_t VAL_TRAY_W[]              = L"TrayWidth";
const wchar_t VAL_TRAY_H[]              = L"TrayHeight";
const wchar_t VAL_TRAY_DIRECTION[]      = L"TrayDirection";

// type should be a RRF_RT_* constant
// data should already contain default value
LSTATUS getSettingsValue(const wchar_t *name, DWORD type, void *data, DWORD size) {
    return RegGetValue(HKEY_CURRENT_USER, KEY_SETTINGS, name, type, nullptr, data, &size);
}

// type should be a REG_* constant (unlike getSettingsValue!)
LSTATUS setSettingsValue(const wchar_t *name, DWORD type, void *data, DWORD size) {
    return RegSetKeyValue(HKEY_CURRENT_USER, KEY_SETTINGS, name, type, data, size);
}

void getSettingsString(const wchar_t *name, DWORD type, const wchar_t *defaultValue,
        CComHeapPtr<wchar_t> &valueOut) {
    DWORD size;
    if (!RegGetValue(HKEY_CURRENT_USER, KEY_SETTINGS, name, type, nullptr, nullptr, &size)) {
        valueOut.AllocateBytes(size);
        RegGetValue(HKEY_CURRENT_USER, KEY_SETTINGS, name, type, nullptr, valueOut, &size);
    } else {
        DWORD count = lstrlen(defaultValue) + 1;
        valueOut.Allocate(count);
        CopyMemory(valueOut, defaultValue, count * sizeof(wchar_t));
    }
}

void setSettingsString(const wchar_t *name, DWORD type, wchar_t *value) {
    setSettingsValue(name, type, value, (lstrlen(value) + 1) * sizeof(wchar_t));
}


void getStartingFolder(CComHeapPtr<wchar_t> &value) {
    return getSettingsString(VAL_STARTING_FOLDER, RRF_RT_REG_SZ, DEFAULT_STARTING_FOLDER, value);
}

void setStartingFolder(wchar_t *value) {
    setSettingsString(VAL_STARTING_FOLDER, REG_EXPAND_SZ, value);
}

SIZE getItemWindowSize() {
    SIZE value = DEFAULT_ITEM_WINDOW_SIZE;
    getSettingsValue(VAL_ITEM_WINDOW_W, RRF_RT_DWORD, &value.cx, sizeof(value.cx));
    getSettingsValue(VAL_ITEM_WINDOW_H, RRF_RT_DWORD, &value.cy, sizeof(value.cy));
    return value;
}

void setItemWindowSize(SIZE value) {
    setSettingsValue(VAL_ITEM_WINDOW_W, REG_DWORD, &value.cx, sizeof(value.cx));
    setSettingsValue(VAL_ITEM_WINDOW_H, REG_DWORD, &value.cy, sizeof(value.cy));
}

SIZE getFolderWindowSize() {
    SIZE value = DEFAULT_FOLDER_WINDOW_SIZE;
    getSettingsValue(VAL_FOLDER_WINDOW_W, RRF_RT_DWORD, &value.cx, sizeof(value.cx));
    getSettingsValue(VAL_FOLDER_WINDOW_H, RRF_RT_DWORD, &value.cy, sizeof(value.cy));
    return value;
}

void setFolderWindowSize(SIZE value) {
    setSettingsValue(VAL_FOLDER_WINDOW_W, REG_DWORD, &value.cx, sizeof(value.cx));
    setSettingsValue(VAL_FOLDER_WINDOW_H, REG_DWORD, &value.cy, sizeof(value.cy));
}

bool getPreviewsEnabled() {
    DWORD value = DEFAULT_PREVIEWS_ENABLED;
    getSettingsValue(VAL_PREVIEWS_ENABLED, RRF_RT_DWORD, &value, sizeof(value));
    return value;
}

void setPreviewsEnabled(bool value) {
    DWORD dwValue = value;
    setSettingsValue(VAL_PREVIEWS_ENABLED, REG_DWORD, &dwValue, sizeof(dwValue));
}

bool getTextEditorEnabled() {
    DWORD value = DEFAULT_TEXT_EDITOR_ENABLED;
    getSettingsValue(VAL_TEXT_EDITOR_ENABLED, RRF_RT_DWORD, &value, sizeof(value));
    return value;
}

void setTextEditorEnabled(bool value) {
    DWORD dwValue = value;
    setSettingsValue(VAL_TEXT_EDITOR_ENABLED, REG_DWORD, &dwValue, sizeof(dwValue));
}

void getTrayFolder(CComHeapPtr<wchar_t> &value) {
    return getSettingsString(VAL_TRAY_FOLDER, RRF_RT_REG_SZ, DEFAULT_TRAY_FOLDER, value);
}

void setTrayFolder(wchar_t *value) {
    setSettingsString(VAL_TRAY_FOLDER, REG_EXPAND_SZ, value);
}

POINT getTrayPosition() {
    POINT value = DEFAULT_TRAY_POSITION;
    getSettingsValue(VAL_TRAY_X, RRF_RT_DWORD, &value.x, sizeof(value.x));
    getSettingsValue(VAL_TRAY_Y, RRF_RT_DWORD, &value.y, sizeof(value.y));
    return value;
}

void setTrayPosition(POINT value) {
    setSettingsValue(VAL_TRAY_X, REG_DWORD, &value.x, sizeof(value.x));
    setSettingsValue(VAL_TRAY_Y, REG_DWORD, &value.y, sizeof(value.y));
}

SIZE getTraySize() {
    SIZE value = DEFAULT_TRAY_SIZE;
    getSettingsValue(VAL_TRAY_W, RRF_RT_DWORD, &value.cx, sizeof(value.cx));
    getSettingsValue(VAL_TRAY_H, RRF_RT_DWORD, &value.cy, sizeof(value.cy));
    return value;
}

void setTraySize(SIZE value) {
    setSettingsValue(VAL_TRAY_W, REG_DWORD, &value.cx, sizeof(value.cx));
    setSettingsValue(VAL_TRAY_H, REG_DWORD, &value.cy, sizeof(value.cy));
}

TrayDirection getTrayDirection() {
    DWORD value = DEFAULT_TRAY_DIRECTION;
    getSettingsValue(VAL_TRAY_DIRECTION, RRF_RT_DWORD, &value, sizeof(value));
    return (TrayDirection)value;
}

void setTrayDirection(TrayDirection value) {
    setSettingsValue(VAL_TRAY_DIRECTION, REG_DWORD, &value, sizeof(value));
}

} // namespace
