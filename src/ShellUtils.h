#pragma once
#include "common.h"

#include <atlbase.h>
#include <ShObjIdl.h>

namespace chromafiler {

// not reference counted! allocate on stack!
class NewItemSink : public IFileOperationProgressSink {
public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID, void **) override;
    STDMETHODIMP_(ULONG) AddRef() { return 2; };
    STDMETHODIMP_(ULONG) Release() { return 1; };

    // IFileOperationProgressSink
    STDMETHODIMP StartOperations() override;
    STDMETHODIMP FinishOperations(HRESULT) override;
    STDMETHODIMP PreRenameItem(DWORD, IShellItem *, LPCWSTR) override;
    STDMETHODIMP PostRenameItem(DWORD, IShellItem *, LPCWSTR, HRESULT, IShellItem *)
        override;
    STDMETHODIMP PreMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override;
    STDMETHODIMP PostMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *)
        override;
    STDMETHODIMP PreCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override;
    STDMETHODIMP PostCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *)
        override;
    STDMETHODIMP PreDeleteItem(DWORD, IShellItem *) override;
    STDMETHODIMP PostDeleteItem(DWORD, IShellItem *, HRESULT, IShellItem *) override;
    STDMETHODIMP PreNewItem(DWORD, IShellItem *, LPCWSTR) override;
    STDMETHODIMP PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem *)
        override;
    STDMETHODIMP UpdateProgress(UINT, UINT) override;
    STDMETHODIMP ResetTimer() override;
    STDMETHODIMP PauseTimer() override;
    STDMETHODIMP ResumeTimer() override;

    CComPtr<IShellItem> newItem;
};

} // namespace
