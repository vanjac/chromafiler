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

    void create(RECT rect, int showCommand);
    void close();
    void activate();
    void setPos(POINT pos);
    void move(int x, int y);

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

    CComPtr<IShellView> shellView;

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
    void openChild(CComPtr<IShellItem> item);
    CComPtr<IShellItem> resolveLink(CComPtr<IShellItem> item);
    POINT childPos();
    void detachFromParent();

    HWND hwnd;
    CComPtr<IShellItem> item;
    CComPtr<IExplorerBrowser> browser;
    CComHeapPtr<wchar_t> title;

    CComPtr<FolderWindow> parent;
    CComPtr<FolderWindow> child;

    POINT moveAccum;

    // IUnknown
    long refCount;
};

} // namespace
