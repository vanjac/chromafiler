#pragma once
#include <common.h>

#include "COMUtils.h"
#include <windows.h>
#include <shobjidl.h>
#include <atlbase.h>

namespace chromabrowse {

class ItemWindow;

extern long numOpenWindows;
extern CComPtr<ItemWindow> activeWindow;

class ItemWindow : public IUnknownImpl, public IDropSource, public IDropTarget {
protected:
    // calculated in init()
    static int CAPTION_HEIGHT;
    static HACCEL accelTable;

public:
    static void init();
    static void uninit();
    static WNDCLASS createWindowClass(const wchar_t *name);

    ItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);
    virtual ~ItemWindow();

    virtual bool preserveSize() const; // if true, requested size will be ignored by parent
    virtual SIZE requestedSize() const;

    bool create(RECT rect, int showCommand);
    void setPos(POINT pos);

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
        MSG_APPBAR_CALLBACK = WM_USER,
        MSG_LAST
    };
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    RECT windowBody();

    virtual void onCreate();
    virtual void onDestroy();
    virtual bool onCommand(WORD command);
    virtual bool onControlCommand(HWND controlHwnd, WORD notif);
    virtual void onActivate(WORD state, HWND prevWindow);
    virtual void onSize(int width, int height);
    virtual void onPaint(PAINTSTRUCT paint);

    void openChild(CComPtr<IShellItem> childItem);
    void closeChild();
    virtual void onChildDetached();
    virtual POINT childPos(SIZE size); // top left corner of child
    POINT parentPos(); // top right corner of parent

    virtual void onItemChanged();
    virtual void refresh();

    HWND hwnd;
    CComHeapPtr<wchar_t> title;

    CComPtr<ItemWindow> parent, child;
    SIZE storedChildSize;

private:
    virtual const wchar_t * className() = 0;

    // extension points for TrayWindow
    virtual DWORD windowStyle() const;
    virtual bool useCustomFrame() const;
    virtual bool alwaysOnTop() const;
    virtual bool stickToChild() const;

    void close();
    void activate();
    void move(int x, int y);

    HWND createChainOwner(int showCommand);

    void windowRectChanged();
    LRESULT hitTestNCA(POINT cursor);

    void openParent();
    void clearParent();
    void detachFromParent(); // updates UI state

    bool resolveItem();

    void openParentMenu(POINT point);

    void invokeProxyDefaultVerb(POINT point);
    void openProxyProperties();
    void openProxyContextMenu(POINT point);
    void beginProxyDrag(POINT offset); // specify offset from icon origin
    void beginRename();
    void completeRename();
    void cancelRename();
    bool dropAllowed(POINT point);

    // window subclasses
    static LRESULT CALLBACK parentButtonProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);
    static LRESULT CALLBACK renameBoxProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    CComPtr<IShellLink> link;

    HICON iconLarge = nullptr, iconSmall = nullptr;

    HWND tooltip = nullptr, parentButton = nullptr, renameBox = nullptr;
    RECT proxyRect{}, titleRect{}, iconRect{};
    CComPtr<IDropTarget> itemDropTarget;
    CComPtr<IDropTargetHelper> dropTargetHelper;
    // for handling delayed context menu messages while open (eg. for Open With menu)
    CComQIPtr<IContextMenu2> contextMenu2;
    CComQIPtr<IContextMenu3> contextMenu3;

    POINT moveAccum;
    // drop target state
    IDataObject *dropDataObject;
    bool overDropTarget = false;
};

} // namespace
