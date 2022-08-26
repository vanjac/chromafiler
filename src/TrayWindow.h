#pragma once
#include <common.h>

#include "FolderWindow.h"

namespace chromafile {

// about the name: https://devblogs.microsoft.com/oldnewthing/20030910-00/?p=42583
class TrayWindow : public FolderWindow {
public:
    static void init();

    static HWND findTray();

    TrayWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

    POINT requestedPosition();
    SIZE requestedSize() const override;

    bool handleTopLevelMessage(MSG *msg) override;

protected:
    enum UserMessage {
        MSG_APPBAR_CALLBACK = ItemWindow::MSG_LAST,
        MSG_LAST
    };
    enum TimerID {
        TIMER_MAKE_TOPMOST = 1,
        TIMER_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    void onDestroy() override;
    bool onCommand(WORD command) override;
    void onSize(int width, int height) override;

private:
    const wchar_t * className() override;

    DWORD windowStyle() const override;
    bool useCustomFrame() const override;
    bool allowToolbar() const override;
    bool alwaysOnTop() const override;
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
