#pragma once
#include "common.h"

#include "COMUtils.h"
#include "WinUtils.h"

namespace chromafiler {

class ItemWindow;

class ChainWindow : public WindowImpl, public UnknownImpl {
public:
    static void init();

    // window lifetime is controlled by object lifetime (unlike ItemWindow)
    ChainWindow(ItemWindow *left, bool leftIsPopup = false, int showCommand = -1);
    ~ChainWindow();

    HWND getWnd();

    void setLeft(ItemWindow *window);
    void setPreview(HWND newPreview);
    void setText(const wchar_t *text);
    void setIcon(HICON smallIcon, HICON largeIcon);
    void setEnabled(bool enabled);

protected:
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    ItemWindow *left; // left-most window
    HWND preview = nullptr; // taskbar preview window
};

} // namespace
