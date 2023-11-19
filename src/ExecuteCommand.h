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

// Two CLSIDs for the same class, depending on how it's invoked:
// {87612720-a94e-4fd3-a1f6-b78d7768424f}
const CLSID CLSID_CFExecute =
    {0x87612720, 0xa94e, 0x4fd3, {0xa1, 0xf6, 0xb7, 0x8d, 0x77, 0x68, 0x42, 0x4f}};
// {14c46e3b-9015-4bbb-9f1f-9178a94f856f}
const CLSID CLSID_CFExecuteText =
    {0x14c46e3b, 0x9015, 0x4bbb, {0x9f, 0x1f, 0x91, 0x78, 0xa9, 0x4f, 0x85, 0x6f}};
class CFExecute : public IUnknownImpl, public IObjectWithSelection, public IExecuteCommand,
        public IDropTarget {
public:
    CFExecute(bool text);
    ~CFExecute();

    // IUnknown
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
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

    const bool text;
    CComPtr<IShellItemArray> selection;
    HMONITOR monitor = nullptr;
    int showCommand = SW_SHOWNORMAL;
    wstr_ptr workingDir;
};

class CFExecuteFactory : public IClassFactory {
public:
    CFExecuteFactory(bool text);

    // IUnknown
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown *outer, REFIID id, void **obj) override;
    STDMETHODIMP LockServer(BOOL lock) override;

private:
    const bool text;
};

} // namespace
