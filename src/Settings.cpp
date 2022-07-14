#include "Settings.h"

namespace chromabrowse::settings {

const SIZE      DEFAULT_ITEM_WINDOW_SIZE    = {450, 450};
const SIZE      DEFAULT_FOLDER_WINDOW_SIZE  = {231, 450}; // just wide enough for scrollbar tooltips
const POINT     DEFAULT_TRAY_POSITION       = {CW_USEDEFAULT, CW_USEDEFAULT};
const SIZE      DEFAULT_TRAY_SIZE           = {600, 48};

const wchar_t KEY_SETTINGS[]    = L"Software\\chromabrowse";

const wchar_t VAL_ITEM_WINDOW_W[]   = L"ItemWindowWidth";
const wchar_t VAL_ITEM_WINDOW_H[]   = L"ItemWindowHeight";
const wchar_t VAL_FOLDER_WINDOW_W[] = L"FolderWindowWidth";
const wchar_t VAL_FOLDER_WINDOW_H[] = L"FolderWindowHeight";
const wchar_t VAL_TRAY_X[]          = L"TrayX";
const wchar_t VAL_TRAY_Y[]          = L"TrayY";
const wchar_t VAL_TRAY_W[]          = L"TrayWidth";
const wchar_t VAL_TRAY_H[]          = L"TrayHeight";

// type should be a RRF_RT_* constant
// data should already contain default value
void getSettingsValue(const wchar_t *name, DWORD type, void *data, DWORD size) {
    RegGetValue(HKEY_CURRENT_USER, KEY_SETTINGS, name, type, nullptr, data, &size);
}

// type should be a REG_* constant (unlike getSettingsValue!)
void setSettingsValue(const wchar_t *name, DWORD type, void *data, DWORD size) {
    RegSetKeyValue(HKEY_CURRENT_USER, KEY_SETTINGS, name, type, data, size);
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

} // namespace
