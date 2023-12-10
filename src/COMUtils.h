#pragma once
#include <common.h>

#include "main.h"
#include <Unknwn.h>

namespace chromafiler {

class IUnknownImpl : public IUnknown {
public:
    virtual ~IUnknownImpl() = default;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

private:
    long refCount = 1;
};

template <typename T, bool keepOpen>
class ClassFactoryImpl : public IClassFactory {
    // IUnknown
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override {
        static const QITAB interfaces[] = {
            QITABENT(ClassFactoryImpl, IClassFactory),
            {},
        };
        return QISearch(this, interfaces, id, obj);
    }
    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown *outer, REFIID id, void **obj) override {
        *obj = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;
        CComPtr<T> inst;
        inst.Attach(new T());
        HRESULT hr = inst->QueryInterface(id, obj);
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (keepOpen) {
            if (lock)
                lockProcess();
            else
                unlockProcess();
        }
        return S_OK;
    }
};

class StoppableThread : public IUnknownImpl {
public:
    StoppableThread();
    virtual ~StoppableThread();
    void start(); // do not call multiple times!
    void stop();

protected:
    virtual void run() = 0;

    bool isStopped();
    HANDLE stopEvent;
    SRWLOCK stopLock = SRWLOCK_INIT; // thread will not be stopped while held

private:
    HANDLE thread = nullptr;

    static DWORD WINAPI threadProc(void *);
};

} // namespace
