#include "PreviewWindow.h"
#include "RectUtils.h"
#include <windowsx.h>
#include <shlobj.h>
#include <unordered_map>

// operator== is implemented for GUIDs in guiddef.h
template<>
struct std::hash<CLSID> {
    std::size_t operator() (const CLSID &key) const {
        // https://stackoverflow.com/a/263416
        size_t hash = 17;
        for (int i = 0; i < 4; i++) {
            uint32_t dword = ((uint32_t *)&key)[i];
            hash = hash * 23 + std::hash<uint32_t>()(dword);
        }
        return hash;
    }
};

// https://geelaw.blog/entries/ipreviewhandlerframe-wpf-2-interop/

namespace chromabrowse {

const wchar_t PREVIEW_WINDOW_CLASS[] = L"Preview Window";
const wchar_t PREVIEW_CONTAINER_CLASS[] = L"Preview Container";

enum WorkerUserMessage {
    MSG_INIT_PREVIEW_REQUEST = WM_USER,
    MSG_RELEASE_PREVIEW
};

HANDLE PreviewWindow::initPreviewThread = nullptr;
// used by worker thread:
static std::unordered_map<CLSID, CComPtr<IClassFactory>> previewFactoryCache;

void PreviewWindow::init() {
    WNDCLASS wndClass = createWindowClass(PREVIEW_WINDOW_CLASS);
    RegisterClass(&wndClass);

    WNDCLASS containerClass = {};
    containerClass.lpszClassName = PREVIEW_CONTAINER_CLASS;
    containerClass.lpfnWndProc = DefWindowProc;
    containerClass.hInstance = GetModuleHandle(nullptr);
    RegisterClass(&containerClass);

    SHCreateThreadWithHandle(initPreviewThreadProc, nullptr, CTF_COINIT_STA, nullptr,
        &initPreviewThread);
}

void PreviewWindow::uninit() {
    PostThreadMessage(GetThreadId(initPreviewThread), WM_QUIT, 0, 0);
    WaitForSingleObject(initPreviewThread, INFINITE);
    CloseHandle(initPreviewThread);
    previewFactoryCache.clear();
}

PreviewWindow::PreviewWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item, CLSID previewID)
    : ItemWindow(parent, item),
      previewID(previewID) {}

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

    // don't use Attach() for an additional AddRef() -- keep request alive until received by thread
    initRequest = new InitPreviewRequest(item, previewID, hwnd, container);
    PostThreadMessage(GetThreadId(initPreviewThread),
        MSG_INIT_PREVIEW_REQUEST, 0, (LPARAM)&*initRequest);
}

void PreviewWindow::onDestroy() {
    ItemWindow::onDestroy();
    destroyPreview();
    initRequest->cancel();
}

void PreviewWindow::onActivate(WORD state, HWND prevWindow) {
    ItemWindow::onActivate(state, prevWindow);
    if (state != WA_INACTIVE) {
        // when clicking in the preview window this produces an RPC_E_CANTCALLOUT_ININPUTSYNCCALL
        // error, but it seems harmless
        if (preview)
            preview->SetFocus();
    }
}

void PreviewWindow::onSize(int width, int height) {
    ItemWindow::onSize(width, height);
    if (preview) {
        RECT previewRect = windowBody();
        RECT containerClientRect = {0, 0, rectWidth(previewRect), rectHeight(previewRect)};
        MoveWindow(container, previewRect.left, previewRect.top,
            containerClientRect.right, containerClientRect.bottom, TRUE);
        checkHR(preview->SetRect(&containerClientRect));
    }
}

LRESULT PreviewWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == MSG_INIT_PREVIEW_COMPLETE) {
        preview = nullptr;
        if (!checkHR(CoGetInterfaceAndReleaseStream((IStream*)lParam, IID_PPV_ARGS(&preview))))
            return 0;
        checkHR(IUnknown_SetSite(preview, (IPreviewHandlerFrame *)this));
        checkHR(preview->DoPreview());
        // required for some preview handlers to render correctly initially (eg. SumatraPDF)
        RECT containerClientRect;
        GetClientRect(container, &containerClientRect);
        checkHR(preview->SetRect(&containerClientRect));
        return 0;
    }
    return ItemWindow::handleMessage(message, wParam, lParam);
}

void PreviewWindow::destroyPreview() {
    if (preview) {
        checkHR(IUnknown_SetSite(preview, nullptr));
        checkHR(preview->Unload());

        // Windows Media Player doesn't like if you delete another IPreviewHandler between
        // initializing and calling SetWindow. So ensure that preview handlers are deleted
        // synchronously with the worker thread!
        IStream *previewHandlerStream; // no CComPtr
        checkHR(CoMarshalInterThreadInterfaceInStream(__uuidof(IPreviewHandler), preview,
            &previewHandlerStream));
        PostThreadMessage(GetThreadId(initPreviewThread),
            MSG_RELEASE_PREVIEW, 0, (LPARAM)previewHandlerStream);

        preview = nullptr;
    }
}

void PreviewWindow::refresh() {
    ItemWindow::refresh();
    destroyPreview();
    initRequest->cancel();
    initRequest = new InitPreviewRequest(item, previewID, hwnd, container);
    PostThreadMessage(GetThreadId(initPreviewThread),
        MSG_INIT_PREVIEW_REQUEST, 0, (LPARAM)&*initRequest);
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


PreviewWindow::InitPreviewRequest::InitPreviewRequest(CComPtr<IShellItem> item, CLSID previewID,
        HWND callbackWindow, HWND container)
        : previewID(previewID),
          callbackWindow(callbackWindow),
          container(container) {
    checkHR(CoMarshalInterThreadInterfaceInStream(__uuidof(IShellItem), item, &itemStream));
    cancelEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    InitializeCriticalSectionAndSpinCount(&cancelSection, 4000);
}

PreviewWindow::InitPreviewRequest::~InitPreviewRequest() {
    CloseHandle(cancelEvent);
    DeleteCriticalSection(&cancelSection);
}

void PreviewWindow::InitPreviewRequest::cancel() {
    EnterCriticalSection(&cancelSection);
    SetEvent(cancelEvent);
    LeaveCriticalSection(&cancelSection);
}

DWORD WINAPI PreviewWindow::initPreviewThreadProc(void *) {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.hwnd == nullptr && msg.message == MSG_INIT_PREVIEW_REQUEST) {
            CComPtr<InitPreviewRequest> request;
            request.Attach((InitPreviewRequest *)msg.lParam);
            initPreview(request);
        } else if (msg.hwnd == nullptr && msg.message == MSG_RELEASE_PREVIEW) {
            CComPtr<IPreviewHandler> preview;
            checkHR(CoGetInterfaceAndReleaseStream((IStream*)msg.lParam, IID_PPV_ARGS(&preview)));
            // and immediately goes out of scope
        } else {
            // regular message loop is required by some preview handlers (eg. Windows Mime handler)
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return 0;
}

void PreviewWindow::initPreview(CComPtr<InitPreviewRequest> request) {
    CComPtr<IShellItem> item;
    // don't call CoGetInterfaceAndReleaseStream since itemStream is a CComPtr
    if (!checkHR(CoUnmarshalInterface(request->itemStream, IID_PPV_ARGS(&item))))
        return;

    CComPtr<IClassFactory> factory;
    auto it = previewFactoryCache.find(request->previewID);
    if (it != previewFactoryCache.end()) {
        debugPrintf(L"Reusing already-loaded factory\n");
        factory = it->second;
    } else {
        if (!checkHR(CoGetClassObject(request->previewID, CLSCTX_LOCAL_SERVER, nullptr,
                IID_PPV_ARGS(&factory)))) {
            return;
        }
        previewFactoryCache[request->previewID] = factory;
    }

    CComPtr<IPreviewHandler> preview;
    if (!checkHR(factory->CreateInstance(nullptr, IID_PPV_ARGS(&preview))))
        return;

    if (WaitForSingleObject(request->cancelEvent, 0) == WAIT_OBJECT_0)
        return; // early exit
    if (!initPreviewWithItem(preview, item))
        return; // not initialized yet, no need to call Unload()

    CComQIPtr<IPreviewHandlerVisuals> visuals(preview);
    if (visuals) {
        checkHR(visuals->SetBackgroundColor(GetSysColor(COLOR_WINDOW)));
        visuals->SetTextColor(GetSysColor(COLOR_WINDOWTEXT)); // may not be implemented
    }

    // ensure the window is not closed before the message is posted
    EnterCriticalSection(&request->cancelSection);
    if (WaitForSingleObject(request->cancelEvent, 0) == WAIT_OBJECT_0) {
        LeaveCriticalSection(&request->cancelSection);
        checkHR(preview->Unload());
        return; // early exit
    }

    RECT containerClientRect;
    GetClientRect(request->container, &containerClientRect);
    checkHR(preview->SetWindow(request->container, &containerClientRect));

    IStream *previewHandlerStream; // no CComPtr
    checkHR(CoMarshalInterThreadInterfaceInStream(__uuidof(IPreviewHandler), preview,
        &previewHandlerStream));
    PostMessage(request->callbackWindow,
        MSG_INIT_PREVIEW_COMPLETE, 0, (LPARAM)previewHandlerStream);

    LeaveCriticalSection(&request->cancelSection);
}

bool PreviewWindow::initPreviewWithItem(CComPtr<IPreviewHandler> preview,
        CComPtr<IShellItem> item) {
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
                debugPrintf(L"Init with path %s\n", &*path);
                return true;
            }
        }
    }
    return false;
}

} // namespace
