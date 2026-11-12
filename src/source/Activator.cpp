#include "Activator.h"
#include "MediaSource.h"

namespace hndsrc
{
    HRESULT Activator::RuntimeClassInitialize()
    {
        HRESULT hr = MFCreateAttributes(&_attributes, 4);
        if (SUCCEEDED(hr)) hr = _attributes->SetUINT32(MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES, 1);
        if (SUCCEEDED(hr)) hr = _attributes->SetGUID(MFT_TRANSFORM_CLSID_Attribute, CLSID_HndCamSource);
        return hr;
    }

    STDMETHODIMP Activator::ActivateObject(REFIID riid, void** object)
    {
        if (!object)
            return E_POINTER;
        *object = nullptr;

        std::lock_guard<std::mutex> lock(_lock);
        if (!_source)
        {
            // the media source receives a copy of the attributes set on the activator
            HRESULT hr = Microsoft::WRL::MakeAndInitialize<MediaSource>(&_source, _attributes.Get());
            if (FAILED(hr))
            {
                LogHr(L"Activator::ActivateObject", hr);
                return hr;
            }
        }
        return _source->QueryInterface(riid, object);
    }

    STDMETHODIMP Activator::ShutdownObject()
    {
        // the client shuts the media source down itself; shutting it down twice prevents a clean removal
        return S_OK;
    }

    STDMETHODIMP Activator::DetachObject()
    {
        std::lock_guard<std::mutex> lock(_lock);
        _source.Reset();
        return S_OK;
    }
}
