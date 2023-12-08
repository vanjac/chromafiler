#include "ShellUtils.h"

namespace chromafiler {

STDMETHODIMP NewItemSink::StartOperations() {return S_OK;}
STDMETHODIMP NewItemSink::FinishOperations(HRESULT) {return S_OK;}
STDMETHODIMP NewItemSink::PreRenameItem(DWORD, IShellItem *, LPCWSTR) {return S_OK;}
STDMETHODIMP NewItemSink::PreMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) {return S_OK;}
STDMETHODIMP NewItemSink::PreCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) {return S_OK;}
STDMETHODIMP NewItemSink::PreDeleteItem(DWORD, IShellItem *) {return S_OK;}
STDMETHODIMP NewItemSink::PostDeleteItem(DWORD, IShellItem *, HRESULT, IShellItem *) {return S_OK;}
STDMETHODIMP NewItemSink::PreNewItem(DWORD, IShellItem *, LPCWSTR) {return S_OK;}
STDMETHODIMP NewItemSink::UpdateProgress(UINT, UINT) {return S_OK;}
STDMETHODIMP NewItemSink::ResetTimer() {return S_OK;}
STDMETHODIMP NewItemSink::PauseTimer() {return S_OK;}
STDMETHODIMP NewItemSink::ResumeTimer() {return S_OK;}

STDMETHODIMP NewItemSink::PostRenameItem(DWORD, IShellItem *, LPCWSTR, HRESULT,
        IShellItem *const item) {
    newItem = item;
    return S_OK;
}
STDMETHODIMP NewItemSink::PostMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT,
        IShellItem *const item) {
    newItem = item;
    return S_OK;
}
STDMETHODIMP NewItemSink::PostCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT,
        IShellItem *const item) {
    newItem = item;
    return S_OK;
}
STDMETHODIMP NewItemSink::PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD, HRESULT,
        IShellItem *const item) {
    newItem = item;
    return S_OK;
}

STDMETHODIMP NewItemSink::QueryInterface(REFIID id, void **obj) {
    *obj = nullptr;
    if (id == __uuidof(IUnknown) || id == __uuidof(IFileOperationProgressSink)) {
        *obj = this;
        return S_OK;
    }
    return E_NOINTERFACE;
};

} // namespace
