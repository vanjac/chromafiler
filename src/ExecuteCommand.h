#pragma once
#include "common.h"

#include "COMUtils.h"
#include "ShObjIdl.h"
#include "WinUtils.h"
#include <atlbase.h>
#include <ExDisp.h>

namespace chromafiler {

// https://devblogs.microsoft.com/oldnewthing/20100312-01/?p=14623
// https://devblogs.microsoft.com/oldnewthing/20100503-00/?p=14183
// https://devblogs.microsoft.com/oldnewthing/20100528-01/?p=13883

// {87612720-a94e-4fd3-a1f6-b78d7768424f}
const CLSID CLSID_CFExecute =
    {0x87612720, 0xa94e, 0x4fd3, {0xa1, 0xf6, 0xb7, 0x8d, 0x77, 0x68, 0x42, 0x4f}};
class CFExecute : public IUnknownImpl, public IInitializeCommand,
        public IObjectWithSelection, public IExecuteCommand, public IDropTarget {
public:
    CFExecute();
    ~CFExecute();

    // IUnknown
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    // IInitializeCommand
    STDMETHODIMP Initialize(PCWSTR commandName, IPropertyBag *bag) override;
    // IObjectWithSelection
    STDMETHODIMP SetSelection(IShellItemArray *array) override;
    STDMETHODIMP GetSelection(REFIID id, void **obj) override;
    // IExecuteCommand
    STDMETHODIMP SetKeyState(DWORD) override;
    STDMETHODIMP SetParameters(const wchar_t *params) override;
    STDMETHODIMP SetPosition(POINT) override;
    STDMETHODIMP SetShowWindow(int) override;
    STDMETHODIMP SetNoShowUI(BOOL) override;
    STDMETHODIMP SetDirectory(const wchar_t *path) override;
    STDMETHODIMP Execute() override;
    // IDropTarget
    STDMETHODIMP DragEnter(IDataObject *dataObject, DWORD keyState, POINTL pt, DWORD *effect)
        override;
    STDMETHODIMP DragOver(DWORD keyState, POINTL pt, DWORD *effect) override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP Drop(IDataObject *dataObject, DWORD keyState, POINTL pt, DWORD *effect) override;

private:
    HRESULT openArray(CComPtr<IShellItemArray> array);
    void openItem(CComPtr<IShellItem> item, CComPtr<IShellWindows> shellWindows);

    CComPtr<IShellItemArray> selection;
    HMONITOR monitor = nullptr;
    int showCommand = SW_SHOWNORMAL;
    wstr_ptr workingDir;
    bool text = false;
};

class CFExecuteFactory : public IClassFactory {
public:
    // IUnknown
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown *outer, REFIID id, void **obj) override;
    STDMETHODIMP LockServer(BOOL lock) override;
};

} // namespace
