#include "PreviewHandler.h"
#include "GeomUtils.h"

namespace chromafiler {

PreviewHandlerImpl::~PreviewHandlerImpl() {
    if (hwnd)
        Unload();
}

DWORD PreviewHandlerImpl::windowStyle() const {
    return WS_VISIBLE | WS_CHILD;
}

/* IUnknown */

STDMETHODIMP_(ULONG) PreviewHandlerImpl::AddRef() { return UnknownImpl::AddRef(); }
STDMETHODIMP_(ULONG) PreviewHandlerImpl::Release() { return UnknownImpl::Release(); }

STDMETHODIMP PreviewHandlerImpl::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        QITABENT(PreviewHandlerImpl, IObjectWithSite),
        QITABENT(PreviewHandlerImpl, IInitializeWithItem),
        QITABENT(PreviewHandlerImpl, IPreviewHandler),
        {}
    };
    HRESULT hr = QISearch(this, interfaces, id, obj);
    if (SUCCEEDED(hr))
        return hr;
    return UnknownImpl::QueryInterface(id, obj);
}

/* IObjectWithSite */

STDMETHODIMP PreviewHandlerImpl::SetSite(IUnknown *site) {
    frame = site;
    return S_OK;
}

STDMETHODIMP PreviewHandlerImpl::GetSite(REFIID id, void **site) {
    if (frame)
        return frame->QueryInterface(id, site);
    *site = nullptr;
    return E_FAIL;
}

/* IInitializeWithItem */

STDMETHODIMP PreviewHandlerImpl::Initialize(IShellItem *initItem, DWORD) {
    item = initItem;
    return S_OK;
}

/* IPreviewHandler */

STDMETHODIMP PreviewHandlerImpl::SetWindow(HWND newParent, const RECT *rect) {
    parent = newParent;
    if (hwnd)
        checkLE(SetParent(hwnd, parent));
    return SetRect(rect);
}

STDMETHODIMP PreviewHandlerImpl::SetRect(const RECT *rect) {
    area = *rect;
    if (hwnd)
        MoveWindow(hwnd, area.left, area.top, rectWidth(area), rectHeight(area), TRUE);
    return S_OK;
}

STDMETHODIMP PreviewHandlerImpl::DoPreview() {
    CreateWindow(className(), nullptr, windowStyle(),
        area.left, area.top, rectWidth(area), rectHeight(area),
        parent, nullptr, GetModuleHandle(nullptr), (WindowImpl *)this);
    return S_OK;
}

STDMETHODIMP PreviewHandlerImpl::Unload() {
    DestroyWindow(hwnd);
    hwnd = nullptr;
    return S_OK;
}

STDMETHODIMP PreviewHandlerImpl::SetFocus() {
    return S_OK;
}

STDMETHODIMP PreviewHandlerImpl::QueryFocus(HWND *focusWnd) {
    *focusWnd = GetFocus();
    return S_OK;
}

STDMETHODIMP PreviewHandlerImpl::TranslateAccelerator(MSG *msg) {
    if (frame)
        return frame->TranslateAccelerator(msg); // TODO
    return S_FALSE;
}

} // namespace
