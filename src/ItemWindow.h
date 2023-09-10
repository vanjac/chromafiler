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
    static int CAPTION_HEIGHT;
    static int cascadeSize();

public:
    static void init();
    static void uninit();

    static void flashWindow(HWND hwnd);

    ItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

    virtual SIZE requestedSize(); // called if (! persistSizeInParent())
    virtual RECT requestedRect(HMONITOR preferMonitor); // called for root windows
    virtual bool persistSizeInParent() const;

    void resetViewState(); // call immediately after constructing to reset all view state properties

    bool create(RECT rect, int showCommand);
    void close();
    void setForeground();

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
        // WPARAM: 0, LPARAM: 0
        MSG_UPDATE_ICONS = WM_USER,
        // WPARAM: 0, LPARAM: 0
        MSG_UPDATE_DEFAULT_STATUS_TEXT,
        MSG_FLASH_WINDOW,
        MSG_LAST
    };
    static WNDCLASS createWindowClass(const wchar_t *name);
    virtual LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    virtual SIZE defaultSize() const;
    virtual const wchar_t * propBagName() const;
    virtual const wchar_t * appUserModelID() const;
    virtual DWORD windowStyle() const;
    virtual DWORD windowExStyle() const;
    virtual bool useCustomFrame() const;
    // a window that stays open and is not shown in taskbar. currently only used by TrayWindow
    virtual bool paletteWindow() const;
    virtual bool stickToChild() const; // for windows that override childPos

    virtual bool useDefaultStatusText() const;
    virtual SettingsPage settingsStartPage() const;
    virtual const wchar_t * helpURL() const;

    CComPtr<IPropertyBag> getPropBag();
    virtual void resetPropBag(CComPtr<IPropertyBag> bag);

    // general window commands
    void activate();
    void setRect(RECT rect);
    void setPos(POINT pos);
    void move(int x, int y);
    virtual RECT windowBody();
    void enableTransitions(bool enabled);

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
    void setStatusText(const wchar_t *text);
    static TBBUTTON makeToolbarButton(const wchar_t *text, WORD command, BYTE style,
        BYTE state = TBSTATE_ENABLED);
    void setToolbarButtonState(WORD command, BYTE state);
    virtual void addToolbarButtons(HWND tb);
    virtual int getToolbarTooltip(WORD command);

    virtual void trackContextMenu(POINT pos);
    int trackContextMenu(POINT pos, HMENU menu); // will modify menu!

    void openChild(CComPtr<IShellItem> childItem);
    void closeChild();
    virtual void onChildDetached();
    SIZE requestedChildSize(); // called if child->persistSizeInParent()
    virtual POINT childPos(SIZE size);
    POINT parentPos(SIZE size);
    void enableChain(bool enabled);

    virtual IDispatch * getDispatch();
    void onViewReady();
    virtual void onItemChanged();
    virtual void refresh();

    void deleteProxy(bool resolve = true);
    CMINVOKECOMMANDINFOEX makeInvokeInfo(int cmd, POINT point);

    HWND hwnd;
    CComHeapPtr<wchar_t> title;

    CComPtr<ItemWindow> parent, child;

    // for handling delayed context menu messages while open (eg. for Open With menu)
    CComQIPtr<IContextMenu2> contextMenu2;
    CComQIPtr<IContextMenu3> contextMenu3;

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    virtual const wchar_t * className() const = 0;

    bool centeredProxy() const; // requires useCustomFrame() == true

    HWND createChainOwner(int showCommand);

    void windowRectChanged();
    void autoSizeProxy(LONG width);
    LRESULT hitTestNCA(POINT cursor);

    RECT titleRect();
    void limitChainWindowRect(RECT *rect);
    void openParent();
    void clearParent();
    void detachFromParent(bool closeParent); // updates UI state
    void onChildResized(SIZE size); // only called if child->persistSizeInParent()
    void detachAndMove(bool closeParent);

    // left-most non-palette window gets the chain preview
    void addChainPreview();
    void removeChainPreview();

    // only (non-palette) windows with no parent OR child are registered
    void registerShellWindow();
    void unregisterShellWindow();

    void openParentMenu();

    void invokeProxyDefaultVerb();
    void openProxyProperties();
    void openProxyContextMenu();
    void openProxyContextMenuFeedback();
    void proxyDrag(POINT offset); // specify offset from icon origin
    void beginRename();
    void completeRename();
    void cancelRename();

    static LRESULT CALLBACK chainWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static BOOL CALLBACK enumCloseChain(HWND, LPARAM lParam);

    // window subclasses
    static LRESULT CALLBACK renameBoxProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    CComPtr<IShellLink> link;
    CComPtr<IPropertyBag> propBag;

    HWND proxyToolbar = nullptr, proxyTooltip = nullptr;
    HWND parentToolbar = nullptr, renameBox = nullptr;
    HWND statusText = nullptr, statusTooltip = nullptr, cmdToolbar = nullptr;
    HIMAGELIST imageList = nullptr;
    CComPtr<IDropTarget> itemDropTarget;
    CComPtr<IDropTargetHelper> dropTargetHelper;
    long shellWindowCookie = 0;

    POINT moveAccum;
    SIZE lastSize;
    bool isChainPreview = false;
    bool firstActivate = false, closing = false;
    bool draggingObject = false;

    SRWLOCK iconLock = SRWLOCK_INIT;
    HICON iconLarge = nullptr, iconSmall = nullptr;

    SRWLOCK defaultStatusTextLock = SRWLOCK_INIT;
    CComHeapPtr<wchar_t> defaultStatusText;

    class IconThread : public StoppableThread {
    public:
        IconThread(CComPtr<IShellItem> item, ItemWindow *callbackWindow);
    protected:
        void run() override;
    private:
        CComHeapPtr<ITEMIDLIST> itemIDList;
        ItemWindow *callbackWindow;
    };
    CComPtr<IconThread> iconThread;

    class StatusTextThread : public StoppableThread {
    public:
        StatusTextThread(CComPtr<IShellItem> item, ItemWindow *callbackWindow);
    protected:
        void run() override;
    private:
        CComHeapPtr<ITEMIDLIST> itemIDList;
        ItemWindow *callbackWindow;
    };
    CComPtr<StatusTextThread> statusTextThread;
};

} // namespace
