#include "COMUtil.h"

namespace chromabrowse {

// https://docs.microsoft.com/en-us/office/client-developer/outlook/mapi/implementing-iunknown-in-c-plus-plus

STDMETHODIMP IUnknownImpl::QueryInterface(REFIID id, void **obj) {
    if (!obj)
        return E_INVALIDARG;
    *obj = nullptr;
    if (id == __uuidof(IUnknown)) {
        *obj = this;
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) IUnknownImpl::AddRef() {
    return InterlockedIncrement(&refCount);
}

STDMETHODIMP_(ULONG) IUnknownImpl::Release() {
    long r = InterlockedDecrement(&refCount);
    if (r == 0) {
        delete this;
    }
    return r;
}

} // namespace
