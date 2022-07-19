#pragma once
#include <common.h>

#include <Unknwn.h>

namespace chromabrowse {

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

} // namespace
