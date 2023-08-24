#include "ExecuteCommand.h"
#include "main.h"
#include "CreateItemWindow.h"

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

STDMETHODIMP CFExecute::Initialize(PCWSTR, IPropertyBag *) { return S_OK; }

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
STDMETHODIMP CFExecute::SetDirectory(LPCWSTR) { return S_OK; }

STDMETHODIMP CFExecute::SetPosition(POINT point) {
    position = point;
    return S_OK;
}

STDMETHODIMP CFExecute::SetShowWindow(int show) {
    showCommand = show;
    return S_OK;
}

STDMETHODIMP CFExecute::Execute() {
    if (!itemArray)
        return E_UNEXPECTED;
    HRESULT hr;
    CComPtr<IEnumShellItems> enumItems;
    if (SUCCEEDED(hr = itemArray->EnumItems(&enumItems))) {
        CComPtr<IShellItem> item;
        while (enumItems->Next(1, &item, nullptr) == S_OK) {
            item = resolveLink(item);
            CComPtr<ItemWindow> window = createItemWindow(nullptr, item);
            SIZE size = window->requestedSize();
            // TODO: better rect
            RECT rect = {position.x, position.y, position.x + size.cx, position.y + size.cy};
            window->create(rect, showCommand);
            item = nullptr;
        }
        return S_OK;
    }
    return hr;
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
