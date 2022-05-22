#pragma once
#include <common.h>

#include <windows.h>
#include <shobjidl.h>
#include <atlbase.h>

namespace chromabrowse {

class FolderWindow : public IServiceProvider, public ICommDlgBrowser {
public:
    static const wchar_t *CLASS_NAME;
    // user messages
    static const UINT MSG_FORCE_SORT = WM_USER;

    static void registerClass();

    FolderWindow(FolderWindow *parent, CComPtr<IShellItem> item);
    ~FolderWindow();

    bool create(RECT rect, int showCommand);
    void close();
    void activate();
    void setPos(POINT pos);
    void move(int x, int y);
    void clearSelection();

    bool handleTopLevelMessage(MSG *msg);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void **obj);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();
    // IServiceProvider
    STDMETHODIMP QueryService(REFGUID guidService, REFIID riid, void **ppv);
    // ICommDlgBrowser
    STDMETHODIMP OnDefaultCommand(IShellView *view);
    STDMETHODIMP OnStateChange(IShellView *view, ULONG change);
    STDMETHODIMP IncludeObject(IShellView *view, PCUITEMID_CHILD pidl);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    
    void setupWindow();
    void cleanupWindow();
    void windowRectChanged();
    // for DWM custom frame:
    void extendWindowFrame();
    LRESULT hitTestNCA(POINT cursor);
    void paintCustomCaption(HDC hdc);

    void selectionChanged();

    void resultsFolderFallback();
    void closeChild();
    void openChild(CComPtr<IShellItem> childItem);
    void openParent();
    CComPtr<IShellItem> resolveLink(CComPtr<IShellItem> linkItem);
    POINT childPos(); // top left corner of child
    POINT parentPos(); // top right corner of parent
    void detachFromParent();

    HWND hwnd;
    CComPtr<IShellItem> item;
    CComPtr<IExplorerBrowser> browser;
    CComPtr<IShellView> shellView;
    CComHeapPtr<wchar_t> title;
    HICON iconLarge, iconSmall;

    HWND parentButton;

    CComPtr<FolderWindow> parent;
    CComPtr<FolderWindow> child;

    POINT moveAccum;
    bool ignoreNextSelection;

    // IUnknown
    long refCount;
};

} // namespace
