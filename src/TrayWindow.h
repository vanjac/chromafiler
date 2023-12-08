#pragma once
#include <common.h>

#include "FolderWindow.h"

namespace chromafiler {

// about the name: https://devblogs.microsoft.com/oldnewthing/20030910-00/?p=42583
class TrayWindow : public FolderWindow {
public:
    static void init();

    static HWND findTray();
    static void resetTrayPosition();
    static void updateAllSettings();

    TrayWindow(ItemWindow *parent, IShellItem *item);

    SIZE requestedSize() override;
    RECT requestedRect(HMONITOR preferMonitor) override;

    bool handleTopLevelMessage(MSG *msg) override;

    STDMETHODIMP OnNavigationComplete(PCIDLIST_ABSOLUTE folder) override;
    STDMETHODIMP MessageSFVCB(UINT msg, WPARAM wParam, LPARAM lParam) override;

protected:
    enum UserMessage {
        MSG_APPBAR_CALLBACK = FolderWindow::MSG_LAST,
        MSG_LAST
    };
    enum TimerID {
        TIMER_MAKE_TOPMOST = FolderWindow::TIMER_LAST,
        TIMER_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    DWORD windowStyle() const override;
    DWORD windowExStyle() const override;
    bool useCustomFrame() const override;
    bool paletteWindow() const override;
    bool stickToChild() const override;
    SettingsPage settingsStartPage() const override;

    POINT childPos(SIZE size) override;
    const wchar_t * propBagName() const override;
    void initDefaultView(IFolderView2 *folderView) override;
    FOLDERSETTINGS folderSettings() const override;

    RECT windowBody() override;

    void onCreate() override;
    void onDestroy() override;
    bool onCommand(WORD command) override;
    void onSize(SIZE size) override;

private:
    const wchar_t * className() const override;

    void updateSettings();
    void fixListViewColors();

    void forceTopmost();

    static LRESULT CALLBACK sizeGripProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    HWND traySizeGrip;
    bool fullScreen = false;
    POINT movePos;
};

} // namespace
