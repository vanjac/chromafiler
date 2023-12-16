#include "PreviewWindow.h"
#include "GeomUtils.h"
#include "WinUtils.h"
#include <windowsx.h>
#include <shlobj.h>

namespace chromafiler {

// https://geelaw.blog/entries/ipreviewhandlerframe-wpf-2-interop/

const wchar_t PREVIEW_CONTAINER_CLASS[] = L"ChromaFiler Preview Container";

const int FACTORY_CACHE_SIZE = 4;

enum WorkerUserMessage {
    // WPARAM: 0, LPARAM: InitPreviewRequest (calls free!)
    MSG_INIT_PREVIEW_REQUEST = WM_USER,
    // WPARAM: 0, LPARAM: IPreviewHandler (marshalled, calls Release!)
    MSG_RELEASE_PREVIEW
};

struct FactoryCacheEntry {
    CLSID clsid = {};
    CComPtr<IClassFactory> factory;
};

HANDLE PreviewWindow::initPreviewThread = nullptr;
// used by worker thread:
static FactoryCacheEntry factoryCache[FACTORY_CACHE_SIZE] = {};
static int factoryCacheIndex = 0;

void PreviewWindow::init() {
    WNDCLASS containerClass = {};
    containerClass.lpszClassName = PREVIEW_CONTAINER_CLASS;
    containerClass.lpfnWndProc = DefWindowProc;
    containerClass.hInstance = GetModuleHandle(nullptr);
    containerClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&containerClass);

    SHCreateThreadWithHandle(initPreviewThreadProc, nullptr, CTF_COINIT_STA, nullptr,
        &initPreviewThread);
}

void PreviewWindow::uninit() {
    if (initPreviewThread) {
        checkLE(PostThreadMessage(GetThreadId(initPreviewThread), WM_QUIT, 0, 0));
        // Wait for thread to exit
        // Avoid deadlock when destroying objects created on the main thread
        DWORD res;
        do {
            res = MsgWaitForMultipleObjects(1, &initPreviewThread, FALSE, INFINITE, QS_ALLINPUT);
            if (res == WAIT_OBJECT_0 + 1) {
                MSG msg;
                while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        } while (res != WAIT_OBJECT_0 && res != WAIT_FAILED);
        checkLE(CloseHandle(initPreviewThread));
    }
    for (auto &entry : factoryCache)
        entry.factory.Release();
}

PreviewWindow::PreviewWindow(ItemWindow *const parent, IShellItem *const item,
        CLSID previewID, bool async)
    : ItemWindow(parent, item),
      previewID(previewID),
      async(async) {}

void PreviewWindow::onCreate() {
    ItemWindow::onCreate();

    RECT previewRect = windowBody();
    previewRect.bottom += CAPTION_HEIGHT; // initial rect is wrong
    if (async) {
        // some preview handlers don't respect the given rect and always fill their window
        // so wrap the preview handler in a container window
        container = checkLE(CreateWindow(PREVIEW_CONTAINER_CLASS, nullptr, WS_VISIBLE | WS_CHILD,
            previewRect.left, previewRect.top, rectWidth(previewRect), rectHeight(previewRect),
            hwnd, nullptr, GetWindowInstance(hwnd), nullptr));
    }

    requestPreview(container ? clientRect(container) : previewRect);
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
    RECT previewRect = windowBody();
    if (container) {
        MoveWindow(container, previewRect.left, previewRect.top,
            rectWidth(previewRect), rectHeight(previewRect), TRUE);
        previewRect = {0, 0, rectWidth(previewRect), rectHeight(previewRect)};
    }
    if (preview)
        checkHR(preview->SetRect(&previewRect));
}

LRESULT PreviewWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == MSG_INIT_PREVIEW_COMPLETE) {
        AcquireSRWLockExclusive(&previewStreamLock);
        CComPtr<IStream> newPreviewStream = previewStream;
        previewStream = nullptr;
        ReleaseSRWLockExclusive(&previewStreamLock);
        if (!newPreviewStream)
            return 0;

        destroyPreview();
        if (!checkHR(CoUnmarshalInterface(newPreviewStream, IID_PPV_ARGS(&preview))))
            return 0;
        checkHR(IUnknown_SetSite(preview, (IPreviewHandlerFrame *)this));
        checkHR(preview->DoPreview());
        RECT previewRect = container ? clientRect(container) : windowBody();
        // required for some preview handlers to render correctly initially (eg. SumatraPDF)
        checkHR(preview->SetRect(&previewRect));
        return 0;
    }
    return ItemWindow::handleMessage(message, wParam, lParam);
}

void PreviewWindow::requestPreview(RECT rect) {
    initRequest.Attach(new InitPreviewRequest(item, previewID, this,
        container ? container : hwnd, rect));
    if (async && initPreviewThread) {
        (*initRequest).AddRef(); // keep alive
        checkLE(PostThreadMessage(GetThreadId(initPreviewThread),
            MSG_INIT_PREVIEW_REQUEST, 0, (LPARAM)&*initRequest));
    } else {
        initPreview(initRequest, false);
    }
}

void PreviewWindow::destroyPreview() {
    if (preview) {
        checkHR(IUnknown_SetSite(preview, nullptr));
        checkHR(preview->Unload());

        // Windows Media Player doesn't like if you delete another IPreviewHandler between
        // initializing and calling SetWindow. So ensure that preview handlers are deleted
        // synchronously with the worker thread!
        if (async && initPreviewThread) {
            CComPtr<IStream> previewHandlerStream; // no CComPtr
            checkHR(CoMarshalInterThreadInterfaceInStream(__uuidof(IPreviewHandler), preview,
                &previewHandlerStream));
            checkLE(PostThreadMessage(GetThreadId(initPreviewThread),
                MSG_RELEASE_PREVIEW, 0, (LPARAM)previewHandlerStream.Detach()));
            CHROMAFILER_MEMLEAK_ALLOC;
        }

        preview = nullptr;
    }
}

void PreviewWindow::refresh() {
    ItemWindow::refresh();
    initRequest->cancel();
    requestPreview(container ? clientRect(container) : windowBody());
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


PreviewWindow::InitPreviewRequest::InitPreviewRequest(IShellItem *const item, CLSID previewID,
        PreviewWindow *const callbackWindow, HWND parent, RECT rect)
        : previewID(previewID),
          callbackWindow(callbackWindow),
          parent(parent),
          rect(rect) {
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
            initPreview(request, true);
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

void PreviewWindow::initPreview(InitPreviewRequest *const request, bool async) {
    CComPtr<IShellItem> item;
    if (!checkHR(SHCreateItemFromIDList(request->itemIDList, IID_PPV_ARGS(&item))))
        return;
    request->itemIDList.Free();

    CComPtr<IPreviewHandler> preview;
    for (auto &entry : factoryCache) {
        if (entry.clsid == request->previewID && entry.factory) {
            debugPrintf(L"Found cached factory\n");
            if (!checkHR(entry.factory->CreateInstance(nullptr, IID_PPV_ARGS(&preview)))) {
                entry.clsid = {};
                entry.factory = nullptr;
            }
        }
    }
    if (!preview) {
        CComPtr<IClassFactory> factory;
        if (!checkHR(CoGetClassObject(request->previewID, CLSCTX_LOCAL_SERVER, nullptr,
                IID_PPV_ARGS(&factory))))
            return;
        if (!checkHR(factory->CreateInstance(nullptr, IID_PPV_ARGS(&preview))))
            return;
        if (async) {
            // https://stackoverflow.com/a/5002596/11525734
            factoryCache[factoryCacheIndex] = {request->previewID, factory};
            factoryCacheIndex = (factoryCacheIndex + 1) % FACTORY_CACHE_SIZE;
        }
    }

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

    checkHR(preview->SetWindow(request->parent, &request->rect));

    AcquireSRWLockExclusive(&request->callbackWindow->previewStreamLock);
    request->callbackWindow->previewStream = previewHandlerStream;
    ReleaseSRWLockExclusive(&request->callbackWindow->previewStreamLock);
    PostMessage(request->callbackWindow->hwnd, MSG_INIT_PREVIEW_COMPLETE, 0, 0);

    ReleaseSRWLockExclusive(&request->cancelLock);
}

bool PreviewWindow::initPreviewWithItem(IPreviewHandler *const preview, IShellItem *const item) {
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
            if (checkHR(streamInit->Initialize(stream, STGM_READ))) {
                debugPrintf(L"Init with stream\n");
                return true;
            }
        }
    }

    CComQIPtr<IInitializeWithItem> itemInit(preview);
    if (itemInit) {
        if (checkHR(itemInit->Initialize(item, STGM_READ))) {
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
