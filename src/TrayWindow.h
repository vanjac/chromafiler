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

    TrayWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

    SIZE requestedSize() const override;
    RECT requestedRect();

    bool handleTopLevelMessage(MSG *msg) override;

protected:
    enum UserMessage {
        MSG_APPBAR_CALLBACK = FolderWindow::MSG_LAST,
        MSG_LAST
    };
    enum TimerID {
        TIMER_MAKE_TOPMOST = 1,
        TIMER_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    DWORD windowStyle() const override;
    DWORD windowExStyle() const override;
    bool paletteWindow() const override;

    RECT windowBody() override;

    void onCreate() override;
    void onDestroy() override;
    bool onCommand(WORD command) override;
    void onSize(int width, int height) override;
    void onExitSizeMove(bool moved, bool sized) override;

private:
    const wchar_t * className() override;

    bool useCustomFrame() const override;
    bool allowToolbar() const override;
    bool stickToChild() const override;
    SettingsPage settingsStartPage() const override;
    POINT childPos(SIZE size) override;
    wchar_t * propertyBag() const override;
    void initDefaultView(CComPtr<IFolderView2> folderView) override;

    void forceTopmost();

    static LRESULT CALLBACK moveGripProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK sizeGripProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    HWND traySizeGrip;
    bool fullScreen = false;
};

} // namespace
