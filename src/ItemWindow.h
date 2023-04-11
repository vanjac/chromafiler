#pragma once
#include <common.h>

#include "COMUtils.h"
#include "SettingsDialog.h"
#include <windows.h>
#include <shobjidl.h>
#include <atlbase.h>

namespace chromafiler {

class ItemWindow : public IUnknownImpl, public IDropSource, public IDropTarget {
protected:
    static HACCEL accelTable;

public:
    static void init();
    static void uninit();

    ItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

    virtual SIZE requestedSize() const; // called if (! persistSizeInParent())
    virtual SIZE requestedChildSize() const; // called if child->persistSizeInParent()
    virtual bool persistSizeInParent() const;

    bool create(RECT rect, int showCommand);
    void close();

    // attempt to relocate item if it has been renamed, moved, or deleted
    bool resolveItem();

    virtual bool handleTopLevelMessage(MSG *msg);

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

    CComPtr<IShellItem> item;

protected:
    enum UserMessage {
        MSG_SET_STATUS_TEXT = WM_USER,
        MSG_LAST
    };
    static WNDCLASS createWindowClass(const wchar_t *name);
    virtual LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    virtual DWORD windowStyle() const;
    virtual DWORD windowExStyle() const;
    // a window that stays open and is not shown in taskbar. currently only used by TrayWindow
    virtual bool paletteWindow() const;

    // general window commands
    void activate();
    void setRect(RECT rect);
    void setPos(POINT pos);
    void move(int x, int y);
    virtual RECT windowBody();

    // message callbacks
    virtual void onCreate();
    virtual bool onCloseRequest(); // return false to block close (probably a bad idea)
    virtual void onDestroy();
    virtual bool onCommand(WORD command);
    virtual LRESULT onDropdown(int command, POINT pos);
    virtual bool onControlCommand(HWND controlHwnd, WORD notif);
    virtual LRESULT onNotify(NMHDR *nmHdr);
    virtual void onActivate(WORD state, HWND prevWindow);
    virtual void onSize(SIZE size);
    virtual void onExitSizeMove(bool moved, bool sized);
    virtual void onPaint(PAINTSTRUCT paint);

    bool hasStatusText();
    void setStatusText(wchar_t *text);
    static TBBUTTON makeToolbarButton(const wchar_t *text, WORD command, BYTE style,
        BYTE state = TBSTATE_ENABLED);
    void setToolbarButtonState(WORD command, BYTE state);
    virtual void addToolbarButtons(HWND tb);
    virtual int getToolbarTooltip(WORD command);

    void openChild(CComPtr<IShellItem> childItem);
    void closeChild();
    virtual void onChildDetached();
    virtual void onChildResized(SIZE size); // only called if child->persistSizeInParent()
    virtual POINT childPos(SIZE size);
    POINT parentPos(SIZE size);
    void enableChain(bool enabled);

    virtual void onItemChanged();
    virtual void refresh();

    void deleteProxy(bool resolve = true);
    void invokeContextMenuCommand(CComPtr<IContextMenu> contextMenu, int cmd, POINT point);

    HWND hwnd;
    CComHeapPtr<wchar_t> title;

    CComPtr<ItemWindow> parent, child;

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    virtual const wchar_t * className() = 0;

    virtual bool useCustomFrame() const;
    virtual bool allowToolbar() const;
    virtual bool stickToChild() const; // for windows that override childPos

    virtual bool useDefaultStatusText() const;
    virtual SettingsPage settingsStartPage() const;

    HWND createChainOwner(int showCommand);

    void windowRectChanged();
    LRESULT hitTestNCA(POINT cursor);

    void limitChainWindowRect(RECT *rect);
    void openParent();
    void clearParent();
    void detachFromParent(bool closeParent); // updates UI state
    void detachAndMove(bool closeParent);

    void addChainPreview();
    void removeChainPreview();

    void openParentMenu(POINT point);

    void invokeProxyDefaultVerb();
    void openProxyProperties();
    void openProxyContextMenu(POINT point);
    void proxyDrag(POINT offset); // specify offset from icon origin
    void beginRename();
    void completeRename();
    void cancelRename();

    static LRESULT CALLBACK chainWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static BOOL CALLBACK enumCloseChain(HWND, LPARAM lParam);

    // window subclasses
    static LRESULT CALLBACK parentButtonProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);
    static LRESULT CALLBACK renameBoxProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    CComPtr<IShellLink> link;

    HWND proxyTooltip = nullptr, parentButton = nullptr, renameBox = nullptr;
    HWND statusText = nullptr, statusTooltip = nullptr, toolbar = nullptr;
    RECT proxyRect{}, titleRect{}, iconRect{};
    CComPtr<IDropTarget> itemDropTarget;
    CComPtr<IDropTargetHelper> dropTargetHelper;
    // for handling delayed context menu messages while open (eg. for Open With menu)
    CComQIPtr<IContextMenu2> contextMenu2;
    CComQIPtr<IContextMenu3> contextMenu3;

    POINT moveAccum;
    SIZE lastSize;
    bool isChainPreview = false;
    bool firstActivate = false, closing = false;
    // drop target state
    IDataObject *dropDataObject;
    bool overDropTarget = false;

    class StatusTextThread : public StoppableThread {
    public:
        StatusTextThread(CComPtr<IShellItem> item, HWND callbackWindow);
    protected:
        void run() override;
    private:
        CComHeapPtr<ITEMIDLIST> itemIDList;
        const HWND callbackWindow;
    };
    CComPtr<StatusTextThread> statusTextThread;
};

} // namespace
