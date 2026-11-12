#pragma once
#include "SourceCommon.h"
#include "CameraClient.h"
#include <memory>

namespace hndsrc
{
    // The single video stream of the virtual camera. Frames come from hndcam::CameraClient,
    // converted on the CPU to the media type selected by the client (NV12 or RGB32).
    class MediaStream :
        public Microsoft::WRL::RuntimeClass<
            Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
            Microsoft::WRL::ChainInterfaces<IMFMediaStream2, IMFMediaStream, IMFMediaEventGenerator>,
            IMFAttributes,
            IKsControl>
    {
    public:
        HRESULT RuntimeClassInitialize(IMFMediaSource* source, DWORD streamId);

        // IMFMediaEventGenerator
        STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
        STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
        STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
        STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* eventValue) override;

        // IMFMediaStream
        STDMETHODIMP GetMediaSource(IMFMediaSource** source) override;
        STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override;
        STDMETHODIMP RequestSample(IUnknown* token) override;

        // IMFMediaStream2
        STDMETHODIMP SetStreamState(MF_STREAM_STATE newState) override;
        STDMETHODIMP GetStreamState(MF_STREAM_STATE* currentState) override;

        // IKsControl
        STDMETHODIMP KsProperty(PKSPROPERTY property, ULONG propertyLength, LPVOID data, ULONG dataLength, ULONG* bytesReturned) override;
        STDMETHODIMP KsMethod(PKSMETHOD method, ULONG methodLength, LPVOID data, ULONG dataLength, ULONG* bytesReturned) override;
        STDMETHODIMP KsEvent(PKSEVENT event, ULONG eventLength, LPVOID data, ULONG dataLength, ULONG* bytesReturned) override;

        // IMFAttributes
        HNDSRC_FORWARD_ATTRIBUTES(_attributes)

        // called by MediaSource
        HRESULT Start(IMFMediaType* type);
        HRESULT Stop();
        void Shutdown();
        HRESULT SetAllocator(IUnknown* allocator);
        bool IsActive();

    private:
        HRESULT StartLocked(IMFMediaType* type);
        HRESULT StopLocked();
        HRESULT CreateSample(IMFSample** sample);
        HRESULT FillSample(IMFSample* sample);

        ObjectCounter _counter;
        std::mutex _lock;
        DWORD _id = 0;
        MF_STREAM_STATE _state = MF_STREAM_STATE_STOPPED;
        GUID _subtype = GUID_NULL;
        ComPtr<IMFAttributes> _attributes;
        ComPtr<IMFMediaSource> _source;
        ComPtr<IMFMediaEventQueue> _queue;
        ComPtr<IMFStreamDescriptor> _descriptor;
        ComPtr<IMFMediaType> _currentType;
        ComPtr<IMFVideoSampleAllocatorEx> _allocator;
        bool _allocatorReady = false;
        std::unique_ptr<hndcam::CameraClient> _camera;
        hndcam::Image _frame;
        uint64_t _samples = 0;
    };
}
