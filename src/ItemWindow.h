#pragma once
#include <common.h>

#include "COMUtils.h"
#include "ChainWindow.h"
#include "ProxyIcon.h"
#include "SettingsDialog.h"
#include "WinUtils.h"
#include <cstdint>
#include <windows.h>
#include <shobjidl.h>
#include <atlbase.h>

namespace chromafiler {

class ItemWindow : public WindowImpl, public IUnknownImpl {
    friend ChainWindow;
    friend ProxyIcon;
protected:
    static HACCEL accelTable;
    static int CAPTION_HEIGHT;
    static int cascadeSize();

public:
    static void init();
    static void uninit();

    static void flashWindow(HWND hwnd);

    ItemWindow(ItemWindow *parent, IShellItem *item);

    virtual SIZE requestedSize(); // called if (! persistSizeInParent())
    virtual RECT requestedRect(HMONITOR preferMonitor); // called for root windows
    virtual bool persistSizeInParent() const;

    void setScratch(bool scratch);
    void resetViewState(); // call immediately after constructing to reset all view state properties

    bool create(RECT rect, int showCommand);
    void close();
    void setForeground();

    // attempt to relocate item if it has been renamed, moved, or deleted
    bool resolveItem();

    virtual bool handleTopLevelMessage(MSG *msg);

    CComPtr<IShellItem> item;

protected:
    enum ViewStateIndex {
        STATE_POS, // 0x1
        STATE_SIZE, // 0x2
        STATE_CHILD_SIZE, // 0x4
        STATE_LAST
    };
    enum UserMessage {
        // WPARAM: 0, LPARAM: 0
        MSG_UPDATE_ICONS = WM_USER,
        // WPARAM: 0, LPARAM: 0
        MSG_UPDATE_DEFAULT_STATUS_TEXT,
        // see SHChangeNotification_Lock
        MSG_SHELL_NOTIFY,
        // WPARAM: 0, LPARAM: 0
        MSG_FLASH_WINDOW,
        MSG_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    virtual SIZE defaultSize() const;
    virtual const wchar_t * propBagName() const;
    virtual const wchar_t * appUserModelID() const;
    virtual bool isFolder() const;
    virtual DWORD windowStyle() const;
    virtual DWORD windowExStyle() const;
    bool useCustomFrame() const override;
    // a window that stays open and is not shown in taskbar. currently only used by TrayWindow
    virtual bool paletteWindow() const;
    virtual bool stickToChild() const; // for windows that override childPos

    virtual bool useDefaultStatusText() const;
    virtual SettingsPage settingsStartPage() const;
    virtual const wchar_t * helpURL() const;

    virtual void updateWindowPropStore(IPropertyStore *propStore);
    static void propStoreWriteString(IPropertyStore *propStore,
        const PROPERTYKEY &key, const wchar_t *value);

    CComPtr<IPropertyBag> getPropBag();
    void resetViewState(uint32_t mask);
    void persistViewState();
    virtual void clearViewState(IPropertyBag *bag, uint32_t mask);
    virtual void writeViewState(IPropertyBag *bag, uint32_t mask);
    void viewStateDirty(uint32_t mask);
    void viewStateClean(uint32_t mask);

    bool isScratch();
    void onModify();

    // general window commands
    void activate();
    void setRect(RECT rect);
    void setPos(POINT pos);
    void setSize(SIZE size);
    void move(int x, int y);
    void adjustSize(int *x, int *y);
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

    void openChild(IShellItem *childItem);
    void closeChild();
    virtual void onChildDetached();
    SIZE requestedChildSize(); // called if child->persistSizeInParent()
    virtual POINT childPos(SIZE size);
    POINT parentPos(SIZE size);
    void enableChain(bool enabled);

    virtual IDispatch * getShellViewDispatch();
    void onViewReady();
    virtual void onItemChanged();
    virtual void refresh();

    void deleteProxy();
    CMINVOKECOMMANDINFOEX makeInvokeInfo(int cmd, POINT point);

    CComHeapPtr<wchar_t> title;

    CComPtr<ItemWindow> parent, child;

    // for handling delayed context menu messages while open (eg. for Open With menu)
    CComQIPtr<IContextMenu2> contextMenu2;
    CComQIPtr<IContextMenu3> contextMenu3;

private:
    virtual const wchar_t * className() const;

    bool centeredProxy() const; // requires useCustomFrame() == true

    void fakeDragMove();
    void enableTransitions(bool enabled);
    void windowRectChanged();
    void autoSizeProxy(LONG width);
    LRESULT hitTestNCA(POINT cursor);

    void limitChainWindowRect(RECT *rect);
    void openParent();
    void clearParent();
    void detachFromParent(bool closeParent); // updates UI state
    void onChildResized(SIZE size); // only called if child->persistSizeInParent()
    void detachAndMove(bool closeParent);

    void setChainPreview(); // left-most non-palette window gets the chain preview

    // only (non-palette) windows with no parent OR child are registered
    void registerShellWindow();
    void unregisterShellWindow();

    void registerShellNotify();
    void unregisterShellNotify();

    void itemMoved(IShellItem *newItem);

    void openParentMenu();

    void invokeProxyDefaultVerb();
    void openProxyProperties();
    void openProxyContextMenu();
    void proxyDrag(POINT offset); // specify offset from icon origin
    void proxyRename(const wchar_t *name);

    CComPtr<IShellLink> link;
    CComPtr<IPropertyBag> propBag;
    bool scratch = false;
    uint32_t dirtyViewState = 0; // bit field indexed by ViewStateIndex

    ProxyIcon proxyIcon;
    HWND parentToolbar = nullptr, cmdToolbar = nullptr;
    HWND statusText = nullptr, statusTooltip = nullptr;
    long shellWindowCookie = 0;
    ULONG shellNotifyID = 0;

    CComPtr<ChainWindow> chain;
    SIZE childSize = {0, 0};
    POINT moveAccum;
    bool firstActivate = false, closing = false;

    SRWLOCK iconLock = SRWLOCK_INIT;
    HICON iconLarge = nullptr, iconSmall = nullptr;

    SRWLOCK defaultStatusTextLock = SRWLOCK_INIT;
    CComHeapPtr<wchar_t> defaultStatusText;

    class IconThread : public StoppableThread {
    public:
        IconThread(IShellItem *item, ItemWindow *callbackWindow);
    protected:
        void run() override;
    private:
        CComHeapPtr<ITEMIDLIST> itemIDList;
        ItemWindow *callbackWindow;
    };
    CComPtr<IconThread> iconThread;

    class StatusTextThread : public StoppableThread {
    public:
        StatusTextThread(IShellItem *item, ItemWindow *callbackWindow);
    protected:
        void run() override;
    private:
        CComHeapPtr<ITEMIDLIST> itemIDList;
        ItemWindow *callbackWindow;
    };
    CComPtr<StatusTextThread> statusTextThread;
};

} // namespace
