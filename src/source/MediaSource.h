#pragma once
#include "SourceCommon.h"
#include "MediaStream.h"

namespace hndsrc
{
    // Live media source with one color video stream, as required by the Windows Frame Server
    // for a software virtual camera (MFCreateVirtualCamera).
    class MediaSource :
        public Microsoft::WRL::RuntimeClass<
            Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
            Microsoft::WRL::ChainInterfaces<IMFMediaSourceEx, IMFMediaSource, IMFMediaEventGenerator>,
            IMFAttributes,
            IMFGetService,
            IKsControl,
            IMFSampleAllocatorControl>
    {
    public:
        HRESULT RuntimeClassInitialize(IMFAttributes* activatorAttributes);

        // IMFMediaEventGenerator
        STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
        STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
        STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
        STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* eventValue) override;

        // IMFMediaSource
        STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) override;
        STDMETHODIMP GetCharacteristics(DWORD* characteristics) override;
        STDMETHODIMP Pause() override;
        STDMETHODIMP Shutdown() override;
        STDMETHODIMP Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat, const PROPVARIANT* startPosition) override;
        STDMETHODIMP Stop() override;

        // IMFMediaSourceEx
        STDMETHODIMP GetSourceAttributes(IMFAttributes** attributes) override;
        STDMETHODIMP GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) override;
        STDMETHODIMP SetD3DManager(IUnknown* manager) override;

        // IMFGetService
        STDMETHODIMP GetService(REFGUID service, REFIID riid, LPVOID* object) override;

        // IMFSampleAllocatorControl
        STDMETHODIMP SetDefaultAllocator(DWORD outputStreamId, IUnknown* allocator) override;
        STDMETHODIMP GetAllocatorUsage(DWORD outputStreamId, DWORD* inputStreamId, MFSampleAllocatorUsage* usage) override;

        // IKsControl
        STDMETHODIMP KsProperty(PKSPROPERTY property, ULONG propertyLength, LPVOID data, ULONG dataLength, ULONG* bytesReturned) override;
        STDMETHODIMP KsMethod(PKSMETHOD method, ULONG methodLength, LPVOID data, ULONG dataLength, ULONG* bytesReturned) override;
        STDMETHODIMP KsEvent(PKSEVENT event, ULONG eventLength, LPVOID data, ULONG dataLength, ULONG* bytesReturned) override;

        // IMFAttributes
        HNDSRC_FORWARD_ATTRIBUTES(_attributes)

    private:
        static constexpr DWORD StreamId = 0;

        ObjectCounter _counter;
        std::mutex _lock;
        ComPtr<IMFAttributes> _attributes;
        ComPtr<IMFMediaEventQueue> _queue;
        ComPtr<IMFPresentationDescriptor> _descriptor;
        ComPtr<MediaStream> _stream;
    };
}
