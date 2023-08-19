#include "PreviewWindow.h"
#include "GeomUtils.h"
#include "WinUtils.h"
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

namespace chromafiler {

const wchar_t PREVIEW_WINDOW_CLASS[] = L"ChromaFile Preview";
const wchar_t PREVIEW_CONTAINER_CLASS[] = L"ChromaFile Preview Container";

enum WorkerUserMessage {
    // WPARAM: 0, LPARAM: InitPreviewRequest (calls free!)
    MSG_INIT_PREVIEW_REQUEST = WM_USER,
    // WPARAM: 0, LPARAM: IPreviewHandler (marshalled, calls Release!)
    MSG_RELEASE_PREVIEW
};

HANDLE PreviewWindow::initPreviewThread = nullptr;
// used by worker thread:
static std::unordered_map<CLSID, CComPtr<IClassFactory>> previewFactoryCache;

void PreviewWindow::init() {
    RegisterClass(tempPtr(createWindowClass(PREVIEW_WINDOW_CLASS)));

    WNDCLASS containerClass = {};
    containerClass.lpszClassName = PREVIEW_CONTAINER_CLASS;
    containerClass.lpfnWndProc = DefWindowProc;
    containerClass.hInstance = GetModuleHandle(nullptr);
    RegisterClass(&containerClass);

    SHCreateThreadWithHandle(initPreviewThreadProc, nullptr, CTF_COINIT_STA, nullptr,
        &initPreviewThread);
}

void PreviewWindow::uninit() {
    if (initPreviewThread) {
        checkLE(PostThreadMessage(GetThreadId(initPreviewThread), WM_QUIT, 0, 0));
        WaitForSingleObject(initPreviewThread, INFINITE);
        checkLE(CloseHandle(initPreviewThread));
    }
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
    // some preview handlers don't respect the given rect and always fill their window
    // so wrap the preview handler in a container window
    container = checkLE(CreateWindow(PREVIEW_CONTAINER_CLASS, nullptr, WS_VISIBLE | WS_CHILD,
        previewRect.left, previewRect.top, rectWidth(previewRect), rectHeight(previewRect),
        hwnd, nullptr, GetWindowInstance(hwnd), nullptr));

    // don't use Attach() for an additional AddRef() -- keep request alive until received by thread
    initRequest = new InitPreviewRequest(item, previewID, this, container);
    if (initPreviewThread) {
        checkLE(PostThreadMessage(GetThreadId(initPreviewThread),
            MSG_INIT_PREVIEW_REQUEST, 0, (LPARAM)&*initRequest));
    }
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

void PreviewWindow::onSize(SIZE size) {
    ItemWindow::onSize(size);
    if (preview) {
        RECT previewRect = windowBody();
        MoveWindow(container, previewRect.left, previewRect.top,
            rectWidth(previewRect), rectHeight(previewRect), TRUE);
        RECT containerClientRect = {0, 0, rectWidth(previewRect), rectHeight(previewRect)};
        checkHR(preview->SetRect(&containerClientRect));
    }
}

LRESULT PreviewWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == MSG_INIT_PREVIEW_COMPLETE) {
        AcquireSRWLockExclusive(&previewStreamLock);
        CComPtr<IStream> newPreviewStream = previewStream;
        previewStream = nullptr;
        ReleaseSRWLockExclusive(&previewStreamLock);

        destroyPreview();
        if (!checkHR(CoUnmarshalInterface(newPreviewStream, IID_PPV_ARGS(&preview))))
            return 0;
        checkHR(IUnknown_SetSite(preview, (IPreviewHandlerFrame *)this));
        checkHR(preview->DoPreview());
        // required for some preview handlers to render correctly initially (eg. SumatraPDF)
        checkHR(preview->SetRect(tempPtr(clientRect(container))));
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
        CComPtr<IStream> previewHandlerStream; // no CComPtr
        checkHR(CoMarshalInterThreadInterfaceInStream(__uuidof(IPreviewHandler), preview,
            &previewHandlerStream));
        checkLE(PostThreadMessage(GetThreadId(initPreviewThread),
            MSG_RELEASE_PREVIEW, 0, (LPARAM)previewHandlerStream.Detach()));
        CHROMAFILER_MEMLEAK_ALLOC;

        preview = nullptr;
    }
}

void PreviewWindow::refresh() {
    ItemWindow::refresh();
    initRequest->cancel();
    initRequest = new InitPreviewRequest(item, previewID, this, container);
    if (initPreviewThread) {
        checkLE(PostThreadMessage(GetThreadId(initPreviewThread),
            MSG_INIT_PREVIEW_REQUEST, 0, (LPARAM)&*initRequest));
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


PreviewWindow::InitPreviewRequest::InitPreviewRequest(CComPtr<IShellItem> item, CLSID previewID,
        PreviewWindow *callbackWindow, HWND container)
        : previewID(previewID),
          callbackWindow(callbackWindow),
          container(container) {
    checkHR(SHGetIDListFromObject(item, &itemIDList));
    cancelEvent = checkLE(CreateEvent(nullptr, TRUE, FALSE, nullptr));
}

PreviewWindow::InitPreviewRequest::~InitPreviewRequest() {
    checkLE(CloseHandle(cancelEvent));
}

void PreviewWindow::InitPreviewRequest::cancel() {
    AcquireSRWLockExclusive(&cancelLock);
    checkLE(SetEvent(cancelEvent));
    ReleaseSRWLockExclusive(&cancelLock);
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
            CHROMAFILER_MEMLEAK_FREE; // and immediately goes out of scope
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
    if (!checkHR(SHCreateItemFromIDList(request->itemIDList, IID_PPV_ARGS(&item))))
        return;
    request->itemIDList.Free();

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

    CComPtr<IStream> previewHandlerStream;
    checkHR(CoMarshalInterThreadInterfaceInStream(__uuidof(IPreviewHandler), preview,
        &previewHandlerStream));

    // ensure the window is not closed before the message is posted
    AcquireSRWLockExclusive(&request->cancelLock);
    if (WaitForSingleObject(request->cancelEvent, 0) == WAIT_OBJECT_0) {
        ReleaseSRWLockExclusive(&request->cancelLock);
        checkHR(preview->Unload());
        return; // early exit
    }

    checkHR(preview->SetWindow(request->container, tempPtr(clientRect(request->container))));

    AcquireSRWLockExclusive(&request->callbackWindow->previewStreamLock);
    request->callbackWindow->previewStream = previewHandlerStream;
    ReleaseSRWLockExclusive(&request->callbackWindow->previewStreamLock);
    PostMessage(request->callbackWindow->hwnd, MSG_INIT_PREVIEW_COMPLETE, 0, 0);

    ReleaseSRWLockExclusive(&request->cancelLock);
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
