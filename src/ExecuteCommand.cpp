#include "ExecuteCommand.h"
#include "main.h"
#include "CreateItemWindow.h"
#include "TextWindow.h"
#include "Update.h"

namespace chromafiler {

CFExecute::CFExecute(bool text) : text(text) {
    lockProcess();
    if (text) {
        debugPrintf(L"Invoked for ChromaText\n");
    }
}

CFExecute::~CFExecute() {
    unlockProcess();
}

STDMETHODIMP_(ULONG) CFExecute::AddRef() { return IUnknownImpl::AddRef(); }
STDMETHODIMP_(ULONG) CFExecute::Release() { return IUnknownImpl::Release(); }

STDMETHODIMP CFExecute::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        QITABENT(CFExecute, IObjectWithSelection),
        QITABENT(CFExecute, IExecuteCommand),
        QITABENT(CFExecute, IDropTarget),
        {},
    };
    HRESULT hr = QISearch(this, interfaces, id, obj);
    if (SUCCEEDED(hr))
        return hr;
    return IUnknownImpl::QueryInterface(id, obj);
}

/* IObjectWithSelection */

STDMETHODIMP CFExecute::SetSelection(IShellItemArray *const array) {
    selection = array;
    return S_OK;
}

STDMETHODIMP CFExecute::GetSelection(REFIID id, void **obj) {
    if (selection)
        return selection->QueryInterface(id, obj);
    *obj = nullptr;
    return E_NOINTERFACE;
}

/* IExecuteCommand */

STDMETHODIMP CFExecute::SetKeyState(DWORD) { return S_OK; }
STDMETHODIMP CFExecute::SetParameters(const wchar_t *) { return S_OK; }
STDMETHODIMP CFExecute::SetNoShowUI(BOOL) { return S_OK; }

STDMETHODIMP CFExecute::SetDirectory(const wchar_t *path) {
    int size = lstrlen(path) + 1;
    workingDir = wstr_ptr(new wchar_t[size]);
    CopyMemory(workingDir.get(), path, size * sizeof(wchar_t));
    return S_OK;
}

STDMETHODIMP CFExecute::SetPosition(POINT point) {
    monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    return S_OK;
}

STDMETHODIMP CFExecute::SetShowWindow(int show) {
    showCommand = show;
    return S_OK;
}

STDMETHODIMP CFExecute::Execute() {
    debugPrintf(L"Invoked with DelegateExecute\n");
    if (!selection) {
        if (!workingDir)
            return E_UNEXPECTED;
        // this happens when invoked on background
        CComPtr<IShellItem> item;
        if (checkHR(SHCreateItemFromParsingName(workingDir.get(), nullptr, IID_PPV_ARGS(&item)))) {
            CComPtr<IShellWindows> shellWindows;
            checkHR(shellWindows.CoCreateInstance(CLSID_ShellWindows));
            openItem(item, shellWindows);
        }
    } else {
        HRESULT hr;
        if (FAILED(hr = openArray(selection))) return hr;
    }
    autoUpdateCheck();
    return S_OK;
}

/* IDropTarget */

STDMETHODIMP CFExecute::DragEnter(IDataObject *, DWORD, POINTL, DWORD *effect) {
    *effect &= DROPEFFECT_LINK;
    return S_OK;
}

STDMETHODIMP CFExecute::DragOver(DWORD, POINTL, DWORD *effect) {
    *effect &= DROPEFFECT_LINK;
    return S_OK;
}

STDMETHODIMP CFExecute::DragLeave() {
    return S_OK;
}

STDMETHODIMP CFExecute::Drop(IDataObject *const dataObject, DWORD keyState, POINTL pt,
        DWORD *effect) {
    debugPrintf(L"Invoked with DropTarget\n");
    // https://devblogs.microsoft.com/oldnewthing/20130204-00/?p=5363
    SetKeyState(keyState);
    SetPosition({pt.x, pt.y});
    HRESULT hr;
    CComPtr<IShellItemArray> itemArray;
    if (!checkHR(hr = SHCreateShellItemArrayFromDataObject(dataObject, IID_PPV_ARGS(&itemArray))))
        return hr;
    if (FAILED(hr = openArray(itemArray)))
        return hr;
    *effect &= DROPEFFECT_LINK;
    autoUpdateCheck();
    return S_OK;
}

HRESULT CFExecute::openArray(IShellItemArray *const array) {
    CComPtr<IShellWindows> shellWindows;
    checkHR(shellWindows.CoCreateInstance(CLSID_ShellWindows));

    HRESULT hr;
    CComPtr<IEnumShellItems> enumItems;
    if (!checkHR(hr = array->EnumItems(&enumItems)))
        return hr;
    CComPtr<IShellItem> item;
    while (enumItems->Next(1, &item, nullptr) == S_OK) {
        openItem(item, shellWindows);
        item = nullptr;
    }
    return S_OK;
}

void CFExecute::openItem(IShellItem *const item, IShellWindows *const shellWindows) {
    CComPtr<IShellItem> resolved = resolveLink(item);

    if (shellWindows && showItemWindow(resolved, shellWindows, showCommand))
        return;

    CComPtr<ItemWindow> window;
    if (text) {
        window.Attach(new TextWindow(nullptr, resolved));
    } else {
        window = createItemWindow(nullptr, resolved);
    }
    window->create(window->requestedRect(monitor), showCommand);
    // fix issue when invoking 64-bit ChromaFiler from 32-bit app
    if (showCommand == SW_SHOWNORMAL)
        window->setForeground();
}

/* Factory */

CFExecuteFactory::CFExecuteFactory(bool text) : text(text) {}

STDMETHODIMP_(ULONG) CFExecuteFactory::AddRef() {
    return 2;
}

STDMETHODIMP_(ULONG) CFExecuteFactory::Release() {
    return 1;
}

STDMETHODIMP CFExecuteFactory::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        QITABENT(CFExecuteFactory, IClassFactory),
        {},
    };
    return QISearch(this, interfaces, id, obj);
}

STDMETHODIMP CFExecuteFactory::CreateInstance(IUnknown *const outer, REFIID id, void **obj) {
    *obj = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    CFExecute *ext = new CFExecute(text);
    HRESULT hr = ext->QueryInterface(id, obj);
    ext->Release();
    return hr;
}

STDMETHODIMP CFExecuteFactory::LockServer(BOOL lock) {
    if (lock)
        lockProcess();
    else
        unlockProcess();
    return S_OK;
}

} // namespace
