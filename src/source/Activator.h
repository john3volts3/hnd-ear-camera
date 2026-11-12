#pragma once
#include "SourceCommon.h"
#include "MediaSource.h"

namespace hndsrc
{
    // COM object registered under CLSID_HndCamSource: the Frame Server creates it, sets attributes
    // on it, then calls ActivateObject to get the media source.
    class Activator :
        public Microsoft::WRL::RuntimeClass<
            Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
            Microsoft::WRL::ChainInterfaces<IMFActivate, IMFAttributes>>
    {
    public:
        HRESULT RuntimeClassInitialize();

        // IMFActivate
        STDMETHODIMP ActivateObject(REFIID riid, void** object) override;
        STDMETHODIMP ShutdownObject() override;
        STDMETHODIMP DetachObject() override;

        // IMFAttributes
        HNDSRC_FORWARD_ATTRIBUTES(_attributes)

    private:
        ObjectCounter _counter;
        std::mutex _lock;
        ComPtr<IMFAttributes> _attributes;
        ComPtr<MediaSource> _source;
    };
}
