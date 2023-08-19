#include "COMUtils.h"
#include <shlwapi.h>

namespace chromafiler {

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
    stopEvent = checkLE(CreateEvent(nullptr, TRUE, FALSE, nullptr));
}

StoppableThread::~StoppableThread() {
    checkLE(CloseHandle(thread));
    checkLE(CloseHandle(stopEvent));
}

void StoppableThread::start() {
    AddRef();
    if (!SHCreateThreadWithHandle(threadProc, this, CTF_COINIT_STA, nullptr, &thread))
        Release();
}

void StoppableThread::stop() {
    AcquireSRWLockExclusive(&stopLock);
    checkLE(SetEvent(stopEvent));
    ReleaseSRWLockExclusive(&stopLock);
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
