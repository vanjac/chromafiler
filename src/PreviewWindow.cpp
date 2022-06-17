#include "PreviewWindow.h"
#include "RectUtil.h"
#include <windowsx.h>
#include <shlobj.h>
#include <unordered_map>

template<>
struct std::hash<CLSID> {
    std::size_t operator() (const CLSID &key) const {
        RPC_STATUS status;
        return std::hash<unsigned short>()(UuidHash((UUID *)&key, &status));
    }
};

// https://geelaw.blog/entries/ipreviewhandlerframe-wpf-2-interop/

namespace chromabrowse {

const wchar_t *PREVIEW_WINDOW_CLASS = L"Preview Window";
const wchar_t *PREVIEW_CONTAINER_CLASS = L"Preview Container";

static std::unordered_map<CLSID, CComPtr<IClassFactory>> previewFactoryCache;

void PreviewWindow::init() {
    WNDCLASS wndClass = createWindowClass(PREVIEW_WINDOW_CLASS);
    RegisterClass(&wndClass);

    WNDCLASS containerClass = {};
    containerClass.lpszClassName = PREVIEW_CONTAINER_CLASS;
    containerClass.lpfnWndProc = DefWindowProc;
    containerClass.hInstance = GetModuleHandle(nullptr);
    RegisterClass(&containerClass);
}

void PreviewWindow::uninit() {
    previewFactoryCache.clear();
}

PreviewWindow::PreviewWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item, CLSID previewID)
    : ItemWindow(parent, item)
    , previewID(previewID)
{}

const wchar_t * PreviewWindow::className() {
    return PREVIEW_WINDOW_CLASS;
}

void PreviewWindow::onCreate() {
    ItemWindow::onCreate();

    RECT previewRect = windowBody();
    previewRect.bottom += previewRect.top; // initial rect is wrong
    RECT containerClientRect = {0, 0, rectWidth(previewRect), rectHeight(previewRect)};
    // some preview handlers don't respect the given rect and always fill their window
    // so wrap the preview handler in a container window
    container = CreateWindow(PREVIEW_CONTAINER_CLASS, nullptr, WS_VISIBLE | WS_CHILD,
        previewRect.left, previewRect.top, containerClientRect.right, containerClientRect.bottom,
        hwnd, nullptr, GetWindowInstance(hwnd), nullptr);

    initPreview(); // ignore error
}

void PreviewWindow::onDestroy() {
    ItemWindow::onDestroy();
    if (preview) {
        checkHR(IUnknown_SetSite(preview, nullptr));
        checkHR(preview->Unload());
    }
}

void PreviewWindow::onActivate(WORD state, HWND prevWindow) {
    ItemWindow::onActivate(state, prevWindow);
    if (state != WA_INACTIVE) {
        if (preview)
            checkHR(preview->SetFocus());
    }
}

void PreviewWindow::onSize(int width, int height) {
    ItemWindow::onSize(width, height);
    if (preview) {
        RECT previewRect = windowBody();
        RECT containerClientRect = {0, 0, rectWidth(previewRect), rectHeight(previewRect)};
        SetWindowPos(container, 0,
            previewRect.left, previewRect.top,
            containerClientRect.right, containerClientRect.bottom,
            SWP_NOZORDER | SWP_NOACTIVATE);
        checkHR(preview->SetRect(&containerClientRect));
    }
}

bool PreviewWindow::initPreview() {
    CComPtr<IClassFactory> factory;
    auto it = previewFactoryCache.find(previewID);
    if (it != previewFactoryCache.end()) {
        debugPrintf(L"Reusing already-loaded factory\n");
        factory = it->second;
    } else {
        if (!checkHR(CoGetClassObject(previewID, CLSCTX_LOCAL_SERVER, nullptr,
                IID_PPV_ARGS(&factory)))) {
            return nullptr;
        }
        previewFactoryCache[previewID] = factory;
    }
    if (!checkHR(factory->CreateInstance(nullptr, IID_PPV_ARGS(&preview))))
        return false;
    if (!checkHR(IUnknown_SetSite(preview, (IPreviewHandlerFrame *)this))) {
        checkHR(preview->Unload());
        preview = nullptr;
        return false;
    }

    if (!initPreviewWithItem()) {
        checkHR(IUnknown_SetSite(preview, nullptr));
        checkHR(preview->Unload());
        preview = nullptr;
        return false;
    }

    CComQIPtr<IPreviewHandlerVisuals> visuals(preview);
    if (visuals) {
        // either of these may not be implemented
        visuals->SetBackgroundColor(GetSysColor(COLOR_WINDOW));
        visuals->SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
    }

    RECT containerClientRect;
    GetClientRect(container, &containerClientRect);
    checkHR(preview->SetWindow(container, &containerClientRect));
    checkHR(preview->DoPreview());
    return true;
}

bool PreviewWindow::initPreviewWithItem() {
    CComPtr<IBindCtx> context;
    if (checkHR(CreateBindCtx(0, &context))) {
        BIND_OPTS options = {sizeof(BIND_OPTS), 0, STGM_READ | STGM_SHARE_DENY_NONE, 0};
        checkHR(context->SetBindOptions(&options));
    }
    // try using stream first, it will be more secure by limiting file operations
    CComPtr<IStream> stream;
    if (checkHR(item->BindToHandler(context, BHID_Stream, IID_PPV_ARGS(&stream)))) {
        CComQIPtr<IInitializeWithStream> streamInit(preview);
        if (streamInit) {
            if (SUCCEEDED(streamInit->Initialize(stream, STGM_READ))) {
                debugPrintf(L"Init with stream\n");
                return true;
            }
        }
    }

    CComQIPtr<IInitializeWithItem> itemInit(preview);
    if (itemInit) {
        if (SUCCEEDED(itemInit->Initialize(item, STGM_READ))) {
            debugPrintf(L"Init with item\n");
            return true;
        }
    }

    CComHeapPtr<wchar_t> path;
    if (checkHR(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        CComQIPtr<IInitializeWithFile> fileInit(preview);
        if (fileInit) {
            if (checkHR(fileInit->Initialize(path, STGM_READ))) {
                debugPrintf(L"Init with path\n");
                return true;
            }
        }
    }
    return false;
}

void PreviewWindow::refresh() {
    if (preview) {
        checkHR(IUnknown_SetSite(preview, nullptr));
        checkHR(preview->Unload());
        preview = nullptr;
    }
    initPreview(); // ignore error
}

/* IUnknown */

STDMETHODIMP PreviewWindow::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        QITABENT(PreviewWindow, IPreviewHandlerFrame),
        {},
    };
    HRESULT hr = QISearch(this, interfaces, id, obj);
    if (SUCCEEDED(hr))
        return hr;
    return ItemWindow::QueryInterface(id, obj);
}

STDMETHODIMP_(ULONG) PreviewWindow::AddRef() {
    return ItemWindow::AddRef();
}

STDMETHODIMP_(ULONG) PreviewWindow::Release() {
    return ItemWindow::Release();
}

/* IPreviewHandlerFrame */

STDMETHODIMP PreviewWindow::GetWindowContext(PREVIEWHANDLERFRAMEINFO *info) {
    // fixes shortcuts in eg. Excel handler
    info->cAccelEntries = CopyAcceleratorTable(accelTable, nullptr, 0);
    info->haccel = accelTable;
    return S_OK;
}

STDMETHODIMP PreviewWindow::TranslateAccelerator(MSG *msg) {
    // fixes shortcuts in eg. windows Mime handler (.mht)
    return handleTopLevelMessage(msg) ? S_OK : S_FALSE;
}

} // namespace
