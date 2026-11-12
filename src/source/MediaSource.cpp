#include "MediaSource.h"
#include "MediaStream.h"

namespace hndsrc
{
    HRESULT MediaSource::RuntimeClassInitialize(IMFAttributes* activatorAttributes)
    {
        HRESULT hr = MFCreateAttributes(&_attributes, 8);
        if (SUCCEEDED(hr) && activatorAttributes)
            hr = activatorAttributes->CopyAllItems(_attributes.Get());

        // legacy camera profile: any resolution, up to 30 fps
        ComPtr<IMFSensorProfileCollection> profiles;
        ComPtr<IMFSensorProfile> profile;
        if (SUCCEEDED(hr)) hr = MFCreateSensorProfileCollection(&profiles);
        if (SUCCEEDED(hr)) hr = MFCreateSensorProfile(KSCAMERAPROFILE_Legacy, 0, nullptr, &profile);
        if (SUCCEEDED(hr)) hr = profile->AddProfileFilter(StreamId, L"((RES==;FRT<=30,1;SUT==))");
        if (SUCCEEDED(hr)) hr = profiles->AddProfile(profile.Get());
        if (SUCCEEDED(hr)) hr = _attributes->SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, profiles.Get());

        if (SUCCEEDED(hr)) hr = Microsoft::WRL::MakeAndInitialize<MediaStream>(&_stream, this, StreamId);

        ComPtr<IMFStreamDescriptor> streamDescriptor;
        if (SUCCEEDED(hr)) hr = _stream->GetStreamDescriptor(&streamDescriptor);
        if (SUCCEEDED(hr))
        {
            IMFStreamDescriptor* raw = streamDescriptor.Get();
            hr = MFCreatePresentationDescriptor(1, &raw, &_descriptor);
        }
        if (SUCCEEDED(hr)) hr = MFCreateEventQueue(&_queue);

        if (FAILED(hr))
            LogHr(L"MediaSource::RuntimeClassInitialize", hr);
        else
            Log(L"MediaSource created");
        return hr;
    }

    // --- IMFMediaEventGenerator ------------------------------------------------------------------

    STDMETHODIMP MediaSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state)
    {
        std::lock_guard<std::mutex> lock(_lock);
        return _queue ? _queue->BeginGetEvent(callback, state) : MF_E_SHUTDOWN;
    }

    STDMETHODIMP MediaSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event)
    {
        std::lock_guard<std::mutex> lock(_lock);
        return _queue ? _queue->EndGetEvent(result, event) : MF_E_SHUTDOWN;
    }

    STDMETHODIMP MediaSource::GetEvent(DWORD flags, IMFMediaEvent** event)
    {
        ComPtr<IMFMediaEventQueue> queue;
        {
            std::lock_guard<std::mutex> lock(_lock);
            queue = _queue;
        }
        return queue ? queue->GetEvent(flags, event) : MF_E_SHUTDOWN;
    }

    STDMETHODIMP MediaSource::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* eventValue)
    {
        std::lock_guard<std::mutex> lock(_lock);
        return _queue ? _queue->QueueEventParamVar(type, extendedType, status, eventValue) : MF_E_SHUTDOWN;
    }

    // --- IMFMediaSource ----------------------------------------------------------------------------

    STDMETHODIMP MediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor)
    {
        if (!descriptor)
            return E_POINTER;
        *descriptor = nullptr;
        std::lock_guard<std::mutex> lock(_lock);
        return _descriptor ? _descriptor->Clone(descriptor) : MF_E_SHUTDOWN;
    }

    STDMETHODIMP MediaSource::GetCharacteristics(DWORD* characteristics)
    {
        if (!characteristics)
            return E_POINTER;
        *characteristics = MFMEDIASOURCE_IS_LIVE;
        return S_OK;
    }

    STDMETHODIMP MediaSource::Pause()
    {
        return MF_E_INVALID_STATE_TRANSITION; // live source
    }

    STDMETHODIMP MediaSource::Shutdown()
    {
        ComPtr<MediaStream> stream;
        {
            std::lock_guard<std::mutex> lock(_lock);
            if (!_queue)
                return MF_E_SHUTDOWN;
            _queue->Shutdown();
            _queue.Reset();
            _descriptor.Reset();
            stream = std::move(_stream);
        }
        if (stream)
            stream->Shutdown();
        Log(L"MediaSource shut down");
        return S_OK;
    }

    STDMETHODIMP MediaSource::Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat, const PROPVARIANT* startPosition)
    {
        if (!descriptor || !startPosition)
            return E_POINTER;
        if (timeFormat && *timeFormat != GUID_NULL)
            return MF_E_UNSUPPORTED_TIME_FORMAT;

        std::lock_guard<std::mutex> lock(_lock);
        if (!_queue || !_descriptor || !_stream)
            return MF_E_SHUTDOWN;

        DWORD count = 0;
        HRESULT hr = descriptor->GetStreamDescriptorCount(&count);
        if (FAILED(hr))
            return hr;
        if (count != 1)
            return E_INVALIDARG;

        BOOL selected = FALSE;
        ComPtr<IMFStreamDescriptor> streamDescriptor;
        hr = descriptor->GetStreamDescriptorByIndex(0, &selected, &streamDescriptor);
        if (FAILED(hr))
            return hr;

        if (selected)
        {
            const bool wasActive = _stream->IsActive();
            _descriptor->SelectStream(0);
            ComPtr<IUnknown> unk;
            _stream.As(&unk);
            hr = _queue->QueueEventParamUnk(wasActive ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK, unk.Get());

            ComPtr<IMFMediaTypeHandler> handler;
            ComPtr<IMFMediaType> type;
            if (SUCCEEDED(hr)) hr = streamDescriptor->GetMediaTypeHandler(&handler);
            if (SUCCEEDED(hr)) hr = handler->GetCurrentMediaType(&type);
            if (SUCCEEDED(hr)) hr = _stream->Start(type.Get());
        }
        else
        {
            _descriptor->DeselectStream(0);
            if (_stream->IsActive())
                hr = _stream->Stop();
        }

        if (SUCCEEDED(hr))
        {
            PROPVARIANT time;
            InitPropVariantFromInt64(MFGetSystemTime(), &time);
            hr = _queue->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &time);
            PropVariantClear(&time);
        }
        if (FAILED(hr))
            LogHr(L"MediaSource::Start", hr);
        return hr;
    }

    STDMETHODIMP MediaSource::Stop()
    {
        std::lock_guard<std::mutex> lock(_lock);
        if (!_queue || !_descriptor || !_stream)
            return MF_E_SHUTDOWN;

        HRESULT hr = S_OK;
        if (_stream->IsActive())
            hr = _stream->Stop();
        _descriptor->DeselectStream(0);

        if (SUCCEEDED(hr))
        {
            PROPVARIANT time;
            InitPropVariantFromInt64(MFGetSystemTime(), &time);
            hr = _queue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, &time);
            PropVariantClear(&time);
        }
        return hr;
    }

    // --- IMFMediaSourceEx --------------------------------------------------------------------------

    STDMETHODIMP MediaSource::GetSourceAttributes(IMFAttributes** attributes)
    {
        if (!attributes)
            return E_POINTER;
        return QueryInterface(IID_PPV_ARGS(attributes));
    }

    STDMETHODIMP MediaSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attributes)
    {
        if (!attributes)
            return E_POINTER;
        *attributes = nullptr;
        std::lock_guard<std::mutex> lock(_lock);
        if (!_stream)
            return MF_E_SHUTDOWN;
        if (streamId != StreamId)
            return MF_E_INVALIDSTREAMNUMBER;
        return _stream->QueryInterface(IID_PPV_ARGS(attributes));
    }

    STDMETHODIMP MediaSource::SetD3DManager(IUnknown*)
    {
        // frames are produced on the CPU (small resolution, low frame rate): the D3D manager is not used,
        // so the pipeline allocator keeps handing out system-memory samples
        return S_OK;
    }

    // --- IMFGetService -----------------------------------------------------------------------------

    STDMETHODIMP MediaSource::GetService(REFGUID, REFIID, LPVOID* object)
    {
        if (object)
            *object = nullptr;
        return MF_E_UNSUPPORTED_SERVICE;
    }

    // --- IMFSampleAllocatorControl -------------------------------------------------------------------

    STDMETHODIMP MediaSource::SetDefaultAllocator(DWORD outputStreamId, IUnknown* allocator)
    {
        std::lock_guard<std::mutex> lock(_lock);
        if (!_stream)
            return MF_E_SHUTDOWN;
        if (outputStreamId != StreamId)
            return MF_E_INVALIDSTREAMNUMBER;
        return _stream->SetAllocator(allocator);
    }

    STDMETHODIMP MediaSource::GetAllocatorUsage(DWORD outputStreamId, DWORD* inputStreamId, MFSampleAllocatorUsage* usage)
    {
        if (!inputStreamId || !usage)
            return E_POINTER;
        if (outputStreamId != StreamId)
            return MF_E_INVALIDSTREAMNUMBER;
        *inputStreamId = outputStreamId;
        *usage = MFSampleAllocatorUsage_UsesProvidedAllocator;
        return S_OK;
    }

    // --- IKsControl: no camera controls exposed -------------------------------------------------------

    STDMETHODIMP MediaSource::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG* bytesReturned)
    {
        if (bytesReturned)
            *bytesReturned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }

    STDMETHODIMP MediaSource::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG* bytesReturned)
    {
        if (bytesReturned)
            *bytesReturned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }

    STDMETHODIMP MediaSource::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG* bytesReturned)
    {
        if (bytesReturned)
            *bytesReturned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }
}
