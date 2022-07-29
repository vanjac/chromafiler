#include "COMUtils.h"
#include <shlwapi.h>

namespace chromafile {

// https://docs.microsoft.com/en-us/office/client-developer/outlook/mapi/implementing-iunknown-in-c-plus-plus

STDMETHODIMP IUnknownImpl::QueryInterface(REFIID id, void **obj) {
    if (!obj)
        return E_INVALIDARG;
    *obj = nullptr;
    if (id == __uuidof(IUnknown)) {
        *obj = this;
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) IUnknownImpl::AddRef() {
    return InterlockedIncrement(&refCount);
}

STDMETHODIMP_(ULONG) IUnknownImpl::Release() {
    long r = InterlockedDecrement(&refCount);
    if (r == 0) {
        delete this;
    }
    return r;
}


StoppableThread::StoppableThread() {
    stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    InitializeCriticalSectionAndSpinCount(&stopSection, 4000);
}

StoppableThread::~StoppableThread() {
    CloseHandle(thread);
    CloseHandle(stopEvent);
    DeleteCriticalSection(&stopSection);
}

void StoppableThread::start() {
    if (SHCreateThreadWithHandle(threadProc, this, CTF_COINIT_STA, nullptr, &thread))
        AddRef();
}

void StoppableThread::stop() {
    EnterCriticalSection(&stopSection);
    SetEvent(stopEvent);
    LeaveCriticalSection(&stopSection);
}

bool StoppableThread::isStopped() {
    return WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0;
}

DWORD WINAPI StoppableThread::threadProc(void *data) {
    StoppableThread *self = (StoppableThread *)data;
    self->run();
    self->Release();
    return 0;
}

} // namespace
