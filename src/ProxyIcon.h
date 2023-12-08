#pragma once
#include "common.h"

#include <Windows.h>
#include <Uxtheme.h>
#include <Shobjidl.h>
#include <atlbase.h>

namespace chromafiler {

class ItemWindow;

class ProxyIcon : public IDropSource, public IDropTarget {
public:
    static void init();
    static void initTheme(HTHEME theme);
    static void initMetrics(const NONCLIENTMETRICS &metrics);
    static void uninit();

    ProxyIcon(ItemWindow *outer);

    void create(HWND parent, IShellItem *item, wchar_t *title, int top, int height);
    void destroy(); // may be called without create()

    bool isToolbarWindow(HWND hwnd) const;
    POINT getMenuPoint(HWND parent);

    void setItem(IShellItem *item);
    void setTitle(wchar_t *title);
    void setIcon(HICON icon); // does not take ownership!
    void setActive(bool active);
    void setPressedState(bool pressed);
    void autoSize(LONG parentWidth, LONG captionLeft, LONG captionRight);
    void redrawToolbar();

    void dragDrop(IDataObject *dataObject, POINT offset);
    void beginRename();
    bool isRenaming();

    bool onControlCommand(HWND controlHwnd, WORD notif);
    LRESULT onNotify(NMHDR *nmHdr);
    void onThemeChanged();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IDropSource
    STDMETHODIMP QueryContinueDrag(BOOL escapePressed, DWORD keyState) override;
    STDMETHODIMP GiveFeedback(DWORD effect) override;
    // IDropTarget
    STDMETHODIMP DragEnter(IDataObject *dataObject, DWORD keyState, POINTL pt, DWORD *effect)
        override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP DragOver(DWORD keyState, POINTL pt, DWORD *effect) override;
    STDMETHODIMP Drop(IDataObject *dataObject, DWORD keyState, POINTL pt, DWORD *effect) override;

private:
    RECT titleRect();

    void completeRename();
    void cancelRename();

    // window subclasses
    static LRESULT CALLBACK renameBoxProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    ItemWindow * const outer;

    HWND toolbar = nullptr, tooltip = nullptr, renameBox = nullptr;
    HIMAGELIST imageList = nullptr;
    CComPtr<IDropTarget> dropTarget;

    bool dragging = false;
};

} // namespace
