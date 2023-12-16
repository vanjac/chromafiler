#pragma once
#include "common.h"

#include <atlbase.h>
#include <ShObjIdl_core.h>
#include "COMUtils.h"
#include "WinUtils.h"

namespace chromafiler {

class PreviewHandlerImpl : public WindowImpl, public UnknownImpl,
        public IObjectWithSite, public IInitializeWithItem, public IPreviewHandler {
public:
    virtual ~PreviewHandlerImpl();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IObjectWithSite
    STDMETHODIMP SetSite(IUnknown *) override;
    STDMETHODIMP GetSite(REFIID, void **) override;
    // IInitializeWithItem
    STDMETHODIMP Initialize(IShellItem *, DWORD) override;
    // IPreviewHandler
    STDMETHODIMP SetWindow(HWND, const RECT *) override;
    STDMETHODIMP SetRect(const RECT *) override;
    STDMETHODIMP DoPreview() override;
    STDMETHODIMP Unload() override;
    STDMETHODIMP SetFocus() override;
    STDMETHODIMP QueryFocus(HWND *hwnd) override;
    STDMETHODIMP TranslateAccelerator(MSG *msg) override;

protected:
    virtual const wchar_t * className() const = 0;
    virtual DWORD windowStyle() const;

    CComQIPtr<IPreviewHandlerFrame> frame;
    CComPtr<IShellItem> item;

private:
    HWND parent = nullptr;
    RECT area = {};
};

} // namespace
