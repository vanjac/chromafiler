#pragma once
#include <common.h>

#include "FolderWindow.h"

namespace chromabrowse {

// about the name: https://devblogs.microsoft.com/oldnewthing/20030910-00/?p=42583
class TrayWindow : public FolderWindow {
public:
    static void init();

    TrayWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

    POINT requestedPosition();
    SIZE requestedSize() const override;

protected:
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    void onDestroy() override;
    void onSize(int width, int height) override;

private:
    const wchar_t * className() override;

    DWORD windowStyle() const override;
    bool useCustomFrame() const override;
    bool alwaysOnTop() const override;
    bool stickToChild() const override;
    POINT childPos(SIZE size) override;
    wchar_t * propertyBag() const override;
    void initDefaultView(CComPtr<IFolderView2> folderView) override;

    static LRESULT CALLBACK moveGripProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK sizeGripProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    HWND traySizeGrip;
};

} // namespace
