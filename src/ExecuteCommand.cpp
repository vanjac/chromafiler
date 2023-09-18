#include "ExecuteCommand.h"
#include "main.h"
#include "CreateItemWindow.h"
#include "TextWindow.h"
#include "Update.h"

namespace chromafiler {

CFExecute::CFExecute() {
    lockProcess();
}

CFExecute::~CFExecute() {
    unlockProcess();
}

STDMETHODIMP_(ULONG) CFExecute::AddRef() { return IUnknownImpl::AddRef(); }
STDMETHODIMP_(ULONG) CFExecute::Release() { return IUnknownImpl::Release(); }

STDMETHODIMP CFExecute::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        QITABENT(CFExecute, IExecuteCommand),
        QITABENT(CFExecute, IInitializeCommand),
        QITABENT(CFExecute, IObjectWithSelection),
        {},
    };
    HRESULT hr = QISearch(this, interfaces, id, obj);
    if (SUCCEEDED(hr))
        return hr;
    return IUnknownImpl::QueryInterface(id, obj);
}

/* IInitializeCommand */

STDMETHODIMP CFExecute::Initialize(PCWSTR, IPropertyBag *bag) {
    VARIANT typeVar = {VT_BSTR};
    if (SUCCEEDED(bag->Read(L"CFType", &typeVar, nullptr))) {
        if (lstrcmpi(typeVar.bstrVal, L"text") == 0)
            text = true;
        VariantClear(&typeVar);
    }
    return S_OK;
}

/* IObjectWithSelection */

STDMETHODIMP CFExecute::SetSelection(IShellItemArray *array) {
    itemArray = array;
    return S_OK;
}

STDMETHODIMP CFExecute::GetSelection(REFIID id, void **obj) {
    if (itemArray)
        return itemArray->QueryInterface(id, obj);
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
    CComPtr<IShellWindows> shellWindows;
    checkHR(shellWindows.CoCreateInstance(CLSID_ShellWindows));
    if (!itemArray) {
        if (!workingDir)
            return E_UNEXPECTED;
        // this happens when invoked on background
        CComPtr<IShellItem> item;
        if (checkHR(SHCreateItemFromParsingName(workingDir.get(), nullptr, IID_PPV_ARGS(&item))))
            openItem(item, shellWindows);
        return S_OK;
    }
    HRESULT hr;
    CComPtr<IEnumShellItems> enumItems;
    if (checkHR(hr = itemArray->EnumItems(&enumItems))) {
        CComPtr<IShellItem> item;
        while (enumItems->Next(1, &item, nullptr) == S_OK) {
            openItem(item, shellWindows);
            item = nullptr;
        }
        autoUpdateCheck();
        return S_OK;
    }
    return hr;
}

void CFExecute::openItem(CComPtr<IShellItem> item, CComPtr<IShellWindows> shellWindows) {
    item = resolveLink(item);

    if (shellWindows && showItemWindow(item, shellWindows, showCommand))
        return;

    CComPtr<ItemWindow> window;
    if (text) {
        window.Attach(new TextWindow(nullptr, item));
    } else {
        window = createItemWindow(nullptr, item);
    }
    window->create(window->requestedRect(monitor), showCommand);
    // fix issue when invoking 64-bit ChromaFiler from 32-bit app
    if (showCommand == SW_SHOWNORMAL)
        window->setForeground();
}

/* Factory */

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

STDMETHODIMP CFExecuteFactory::CreateInstance(IUnknown *outer, REFIID id, void **obj) {
    *obj = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    CFExecute *ext = new CFExecute();
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
