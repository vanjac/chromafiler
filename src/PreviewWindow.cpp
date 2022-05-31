#include "PreviewWindow.h"
#include "RectUtil.h"
#include <shlobj.h>

// https://geelaw.blog/entries/ipreviewhandlerframe-wpf-2-interop/

namespace chromabrowse {

const wchar_t *PREVIEW_WINDOW_CLASS = L"Preview Window";
const wchar_t *PREVIEW_CONTAINER_CLASS = L"Preview Container";

void PreviewWindow::init() {
    WNDCLASS wndClass = createWindowClass(PREVIEW_WINDOW_CLASS);
    RegisterClass(&wndClass);

    WNDCLASS containerClass = {};
    containerClass.lpszClassName = PREVIEW_CONTAINER_CLASS;
    containerClass.lpfnWndProc = DefWindowProc;
    containerClass.hInstance = GetModuleHandle(NULL);
    RegisterClass(&containerClass);
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
        hwnd, nullptr, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    if (!initPreview())
        close();
}

void PreviewWindow::onDestroy() {
    ItemWindow::onDestroy();
    if (preview) {
        IUnknown_SetSite(preview, nullptr);
        preview->Unload();
    }
}

void PreviewWindow::onActivate(WORD state, HWND prevWindow) {
    ItemWindow::onActivate(state, prevWindow);
    if (state != WA_INACTIVE) {
        if (preview)
            preview->SetFocus();
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
        preview->SetRect(&containerClientRect);
    }
}

bool PreviewWindow::initPreview() {
    if (FAILED(preview.CoCreateInstance(previewID, nullptr, CLSCTX_LOCAL_SERVER))) {
        debugPrintf(L"Could not create preview handler");
        return false;
    }
    if (FAILED(IUnknown_SetSite(preview, (IPreviewHandlerFrame *)this))) {
        debugPrintf(L"Could not set preview handler site");
        return false;
    }

    if (!initPreviewWithItem())
        return false;

    CComQIPtr<IPreviewHandlerVisuals> visuals(preview);
    if (visuals) {
        visuals->SetBackgroundColor(GetSysColor(COLOR_WINDOW));
        visuals->SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
    }

    RECT containerClientRect;
    GetClientRect(container, &containerClientRect);
    preview->SetWindow(container, &containerClientRect);
    preview->DoPreview();
    return true;
}

bool PreviewWindow::initPreviewWithItem() {
    CComPtr<IBindCtx> context;
    if (SUCCEEDED(CreateBindCtx(0, &context))) {
        BIND_OPTS options = {sizeof(BIND_OPTS), 0, STGM_READ | STGM_SHARE_DENY_NONE, 0};
        context->SetBindOptions(&options);
    }
    // try using stream first, it will be more secure by limiting file operations
    CComPtr<IStream> stream;
    if (SUCCEEDED(item->BindToHandler(context, BHID_Stream, IID_PPV_ARGS(&stream)))) {
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
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        CComQIPtr<IInitializeWithFile> fileInit(preview);
        if (fileInit) {
            if (SUCCEEDED(fileInit->Initialize(path, STGM_READ))) {
                debugPrintf(L"Init with path\n");
                return true;
            }
        }
    }

    debugPrintf(L"Preview handler initialization failed!\n");
    return false;
}

void PreviewWindow::refresh() {
    if (preview) {
        IUnknown_SetSite(preview, nullptr);
        preview->Unload();
        preview = nullptr;
        if (!initPreview())
            close();
    }
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
