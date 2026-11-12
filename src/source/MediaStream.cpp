#include "MediaStream.h"

using namespace hndcam;

namespace hndsrc
{
    namespace
    {
        constexpr UINT32 Width = HNDCAM_OUT_WIDTH;
        constexpr UINT32 Height = HNDCAM_OUT_HEIGHT;
        constexpr UINT32 Fps = HNDCAM_OUT_FPS;
        constexpr LONGLONG SampleDuration = 10'000'000 / Fps;

        HRESULT CreateVideoType(REFGUID subtype, IMFMediaType** type)
        {
            const bool nv12 = subtype == MFVideoFormat_NV12;
            const UINT32 stride = nv12 ? Width : Width * 4;
            const UINT32 sampleSize = nv12 ? Width * Height * 3 / 2 : Width * Height * 4;

            ComPtr<IMFMediaType> t;
            HRESULT hr = MFCreateMediaType(&t);
            if (SUCCEEDED(hr)) hr = t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) hr = t->SetGUID(MF_MT_SUBTYPE, subtype);
            if (SUCCEEDED(hr)) hr = MFSetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, Width, Height);
            if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, Fps, 1);
            if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(t.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            if (SUCCEEDED(hr)) hr = t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            if (SUCCEEDED(hr)) hr = t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
            if (SUCCEEDED(hr)) hr = t->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
            if (SUCCEEDED(hr)) hr = t->SetUINT32(MF_MT_DEFAULT_STRIDE, stride);
            if (SUCCEEDED(hr)) hr = t->SetUINT32(MF_MT_SAMPLE_SIZE, sampleSize);
            if (SUCCEEDED(hr)) hr = t->SetUINT32(MF_MT_AVG_BITRATE, sampleSize * 8 * Fps);
            if (SUCCEEDED(hr))
                *type = t.Detach();
            return hr;
        }
    }

    HRESULT MediaStream::RuntimeClassInitialize(IMFMediaSource* source, DWORD streamId)
    {
        if (!source)
            return E_POINTER;
        _source = source;
        _id = streamId;
        _frame.Resize(Width, Height);

        HRESULT hr = MFCreateAttributes(&_attributes, 8);
        if (SUCCEEDED(hr)) hr = _attributes->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
        if (SUCCEEDED(hr)) hr = _attributes->SetUINT32(MF_DEVICESTREAM_STREAM_ID, streamId);
        if (SUCCEEDED(hr)) hr = _attributes->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
        if (SUCCEEDED(hr)) hr = _attributes->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
        if (SUCCEEDED(hr)) hr = MFCreateEventQueue(&_queue);

        // NV12 first: it is the format most capture pipelines prefer
        ComPtr<IMFMediaType> types[2];
        if (SUCCEEDED(hr)) hr = CreateVideoType(MFVideoFormat_NV12, &types[0]);
        if (SUCCEEDED(hr)) hr = CreateVideoType(MFVideoFormat_RGB32, &types[1]);
        if (SUCCEEDED(hr))
        {
            IMFMediaType* raw[2] = { types[0].Get(), types[1].Get() };
            hr = MFCreateStreamDescriptor(streamId, 2, raw, &_descriptor);
        }

        ComPtr<IMFMediaTypeHandler> handler;
        if (SUCCEEDED(hr)) hr = _descriptor->GetMediaTypeHandler(&handler);
        if (SUCCEEDED(hr)) hr = handler->SetCurrentMediaType(types[0].Get());
        if (FAILED(hr))
            LogHr(L"MediaStream::RuntimeClassInitialize", hr);
        return hr;
    }

    // --- IMFMediaEventGenerator ------------------------------------------------------------------

    STDMETHODIMP MediaStream::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state)
    {
        std::lock_guard<std::mutex> lock(_lock);
        return _queue ? _queue->BeginGetEvent(callback, state) : MF_E_SHUTDOWN;
    }

    STDMETHODIMP MediaStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event)
    {
        std::lock_guard<std::mutex> lock(_lock);
        return _queue ? _queue->EndGetEvent(result, event) : MF_E_SHUTDOWN;
    }

    STDMETHODIMP MediaStream::GetEvent(DWORD flags, IMFMediaEvent** event)
    {
        // GetEvent may block: do not hold the lock while waiting
        ComPtr<IMFMediaEventQueue> queue;
        {
            std::lock_guard<std::mutex> lock(_lock);
            queue = _queue;
        }
        return queue ? queue->GetEvent(flags, event) : MF_E_SHUTDOWN;
    }

    STDMETHODIMP MediaStream::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* eventValue)
    {
        std::lock_guard<std::mutex> lock(_lock);
        return _queue ? _queue->QueueEventParamVar(type, extendedType, status, eventValue) : MF_E_SHUTDOWN;
    }

    // --- IMFMediaStream ----------------------------------------------------------------------------

    STDMETHODIMP MediaStream::GetMediaSource(IMFMediaSource** source)
    {
        if (!source)
            return E_POINTER;
        *source = nullptr;
        std::lock_guard<std::mutex> lock(_lock);
        if (!_source)
            return MF_E_SHUTDOWN;
        return _source.CopyTo(source);
    }

    STDMETHODIMP MediaStream::GetStreamDescriptor(IMFStreamDescriptor** descriptor)
    {
        if (!descriptor)
            return E_POINTER;
        *descriptor = nullptr;
        std::lock_guard<std::mutex> lock(_lock);
        if (!_descriptor)
            return MF_E_SHUTDOWN;
        return _descriptor.CopyTo(descriptor);
    }

    STDMETHODIMP MediaStream::RequestSample(IUnknown* token)
    {
        std::lock_guard<std::mutex> lock(_lock);
        if (!_queue)
            return MF_E_SHUTDOWN;
        if (_state != MF_STREAM_STATE_RUNNING)
            return MF_E_INVALIDREQUEST;

        ComPtr<IMFSample> sample;
        HRESULT hr = CreateSample(&sample);
        if (SUCCEEDED(hr)) hr = FillSample(sample.Get());
        if (SUCCEEDED(hr)) hr = sample->SetSampleTime(MFGetSystemTime());
        if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(SampleDuration);
        if (SUCCEEDED(hr) && token) hr = sample->SetUnknown(MFSampleExtension_Token, token);
        if (SUCCEEDED(hr)) hr = _queue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());

        if (FAILED(hr))
            LogHr(L"MediaStream::RequestSample", hr);
        else if (++_samples == 1 || _samples % 900 == 0)
            Log(L"MediaStream: %llu samples delivered", _samples);
        return hr;
    }

    HRESULT MediaStream::CreateSample(IMFSample** sample)
    {
        if (_allocator && _allocatorReady)
            return _allocator->AllocateSample(sample);

        // no allocator provided by the pipeline: build a system-memory sample
        ComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = MFCreate2DMediaBuffer(Width, Height, _subtype.Data1, FALSE, &buffer);
        ComPtr<IMFSample> s;
        if (SUCCEEDED(hr)) hr = MFCreateSample(&s);
        if (SUCCEEDED(hr)) hr = s->AddBuffer(buffer.Get());
        if (SUCCEEDED(hr))
            *sample = s.Detach();
        return hr;
    }

    HRESULT MediaStream::FillSample(IMFSample* sample)
    {
        if (_camera)
            _camera->CopyLatest(_frame);

        ComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = sample->GetBufferByIndex(0, &buffer);
        if (FAILED(hr))
            return hr;

        const bool nv12 = _subtype == MFVideoFormat_NV12;
        BYTE* scanline0 = nullptr;
        LONG pitch = 0;
        ComPtr<IMF2DBuffer2> buffer2d2;
        ComPtr<IMF2DBuffer> buffer2d;
        if (SUCCEEDED(buffer.As(&buffer2d2)))
        {
            BYTE* start = nullptr;
            DWORD length = 0;
            hr = buffer2d2->Lock2DSize(MF2DBuffer_LockFlags_Write, &scanline0, &pitch, &start, &length);
        }
        else if (SUCCEEDED(buffer.As(&buffer2d)))
        {
            hr = buffer2d->Lock2D(&scanline0, &pitch);
        }
        else
        {
            DWORD maxLength = 0;
            hr = buffer->Lock(&scanline0, &maxLength, nullptr);
            pitch = nv12 ? (LONG)Width : (LONG)Width * 4;
        }
        if (FAILED(hr))
            return hr;

        if (nv12)
            BgraToNv12(_frame, scanline0, pitch, scanline0 + (ptrdiff_t)pitch * Height, pitch);
        else
            BgraToRgb32(_frame, scanline0, pitch);

        if (buffer2d2)
            buffer2d2->Unlock2D();
        else if (buffer2d)
            buffer2d->Unlock2D();
        else
            buffer->Unlock();

        return buffer->SetCurrentLength(nv12 ? Width * Height * 3 / 2 : Width * Height * 4);
    }

    // --- IMFMediaStream2 ---------------------------------------------------------------------------

    STDMETHODIMP MediaStream::SetStreamState(MF_STREAM_STATE newState)
    {
        std::lock_guard<std::mutex> lock(_lock);
        if (!_queue)
            return MF_E_SHUTDOWN;
        if (newState == _state)
            return S_OK;

        Log(L"MediaStream::SetStreamState %d -> %d", (int)_state, (int)newState);
        switch (newState)
        {
        case MF_STREAM_STATE_PAUSED:
            if (_state != MF_STREAM_STATE_RUNNING)
                return MF_E_INVALID_STATE_TRANSITION;
            _state = newState;
            return S_OK;
        case MF_STREAM_STATE_RUNNING:
            return StartLocked(nullptr);
        case MF_STREAM_STATE_STOPPED:
            return StopLocked();
        default:
            return MF_E_INVALID_STATE_TRANSITION;
        }
    }

    STDMETHODIMP MediaStream::GetStreamState(MF_STREAM_STATE* currentState)
    {
        if (!currentState)
            return E_POINTER;
        std::lock_guard<std::mutex> lock(_lock);
        *currentState = _state;
        return S_OK;
    }

    // --- IKsControl: no camera controls exposed -------------------------------------------------------

    STDMETHODIMP MediaStream::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG* bytesReturned)
    {
        if (bytesReturned)
            *bytesReturned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }

    STDMETHODIMP MediaStream::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG* bytesReturned)
    {
        if (bytesReturned)
            *bytesReturned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }

    STDMETHODIMP MediaStream::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG* bytesReturned)
    {
        if (bytesReturned)
            *bytesReturned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }

    // --- control from the media source -------------------------------------------------------------

    HRESULT MediaStream::Start(IMFMediaType* type)
    {
        std::lock_guard<std::mutex> lock(_lock);
        if (!_queue)
            return MF_E_SHUTDOWN;
        return StartLocked(type);
    }

    HRESULT MediaStream::StartLocked(IMFMediaType* type)
    {
        if (type)
            _currentType = type;
        if (!_currentType)
        {
            ComPtr<IMFMediaTypeHandler> handler;
            HRESULT hr = _descriptor->GetMediaTypeHandler(&handler);
            if (SUCCEEDED(hr)) hr = handler->GetCurrentMediaType(&_currentType);
            if (FAILED(hr))
                return hr;
        }

        HRESULT hr = _currentType->GetGUID(MF_MT_SUBTYPE, &_subtype);
        if (FAILED(hr))
            return hr;
        if (_subtype != MFVideoFormat_NV12 && _subtype != MFVideoFormat_RGB32)
            return MF_E_INVALIDMEDIATYPE;

        if (_allocator)
        {
            if (_allocatorReady)
                _allocator->UninitializeSampleAllocator();
            hr = _allocator->InitializeSampleAllocator(10, _currentType.Get());
            _allocatorReady = SUCCEEDED(hr);
            if (FAILED(hr))
                LogHr(L"InitializeSampleAllocator (falling back to own samples)", hr);
        }

        if (!_camera)
        {
            const Settings settings = ReadSettings();
            _camera = std::make_unique<CameraClient>((int)Width, (int)Height);
            _camera->SetAutoRotate(settings.autoRotate);
            _camera->SetRotationOffset(settings.rotationOffset);
            Log(L"MediaStream: camera client created (autoRotate=%d offset=%.1f)", settings.autoRotate ? 1 : 0, settings.rotationOffset);
        }
        _camera->Start();

        Log(L"MediaStream: started, format %s", _subtype == MFVideoFormat_NV12 ? L"NV12" : L"RGB32");
        _state = MF_STREAM_STATE_RUNNING;
        return _queue->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
    }

    HRESULT MediaStream::Stop()
    {
        std::lock_guard<std::mutex> lock(_lock);
        if (!_queue)
            return MF_E_SHUTDOWN;
        return StopLocked();
    }

    HRESULT MediaStream::StopLocked()
    {
        if (_camera)
            _camera->Stop();
        if (_allocator && _allocatorReady)
        {
            _allocator->UninitializeSampleAllocator();
            _allocatorReady = false;
        }
        Log(L"MediaStream: stopped after %llu samples", _samples);
        _state = MF_STREAM_STATE_STOPPED;
        return _queue->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
    }

    void MediaStream::Shutdown()
    {
        std::unique_ptr<CameraClient> camera;
        {
            std::lock_guard<std::mutex> lock(_lock);
            if (_queue)
            {
                _queue->Shutdown();
                _queue.Reset();
            }
            camera = std::move(_camera);
            if (_allocator && _allocatorReady)
                _allocator->UninitializeSampleAllocator();
            _allocatorReady = false;
            _allocator.Reset();
            _descriptor.Reset();
            _currentType.Reset();
            _source.Reset(); // breaks the source <-> stream reference cycle
            _state = MF_STREAM_STATE_STOPPED;
        }
        // stop the worker thread outside the lock
        camera.reset();
    }

    HRESULT MediaStream::SetAllocator(IUnknown* allocator)
    {
        if (!allocator)
            return E_POINTER;
        std::lock_guard<std::mutex> lock(_lock);
        _allocator.Reset();
        _allocatorReady = false;
        return allocator->QueryInterface(IID_PPV_ARGS(&_allocator));
    }

    bool MediaStream::IsActive()
    {
        std::lock_guard<std::mutex> lock(_lock);
        return _state != MF_STREAM_STATE_STOPPED;
    }
}
