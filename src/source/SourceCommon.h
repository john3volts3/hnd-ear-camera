#pragma once

// Common includes for the media source DLL.
#include <winsock2.h>
#include <windows.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <strsafe.h>
#include <mutex>
#include "HndCam.h"
#include "Log.h"
#include <initguid.h> // instantiate the GUIDs declared by the headers below
#include <propvarutil.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfvirtualcamera.h>
// ks.h redefines GUID_NULL as a macro: it must come after every header that pulls cguid.h
#include <ks.h>
#include <ksproxy.h>
#include <ksmedia.h>

using Microsoft::WRL::ComPtr;

namespace hndsrc
{
    using hndcam::Log;
    using hndcam::LogHr;

    // Live COM object count, for DllCanUnloadNow
    void ObjectCreated();
    void ObjectDestroyed();

    // Settings read from HKLM\Software\HND Ear Camera (written by the setup)
    struct Settings
    {
        bool autoRotate = true;
        double rotationOffset = 0;
    };
    Settings ReadSettings();

    struct ObjectCounter
    {
        ObjectCounter() { ObjectCreated(); }
        ~ObjectCounter() { ObjectDestroyed(); }
    };
}

// Forwards every IMFAttributes method to a ComPtr<IMFAttributes> member.
#define HNDSRC_FORWARD_ATTRIBUTES(inner) \
    STDMETHODIMP GetItem(REFGUID k, PROPVARIANT* v) override { return inner->GetItem(k, v); } \
    STDMETHODIMP GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t) override { return inner->GetItemType(k, t); } \
    STDMETHODIMP CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r) override { return inner->CompareItem(k, v, r); } \
    STDMETHODIMP Compare(IMFAttributes* a, MF_ATTRIBUTES_MATCH_TYPE m, BOOL* r) override { return inner->Compare(a, m, r); } \
    STDMETHODIMP GetUINT32(REFGUID k, UINT32* v) override { return inner->GetUINT32(k, v); } \
    STDMETHODIMP GetUINT64(REFGUID k, UINT64* v) override { return inner->GetUINT64(k, v); } \
    STDMETHODIMP GetDouble(REFGUID k, double* v) override { return inner->GetDouble(k, v); } \
    STDMETHODIMP GetGUID(REFGUID k, GUID* v) override { return inner->GetGUID(k, v); } \
    STDMETHODIMP GetStringLength(REFGUID k, UINT32* l) override { return inner->GetStringLength(k, l); } \
    STDMETHODIMP GetString(REFGUID k, LPWSTR v, UINT32 c, UINT32* l) override { return inner->GetString(k, v, c, l); } \
    STDMETHODIMP GetAllocatedString(REFGUID k, LPWSTR* v, UINT32* l) override { return inner->GetAllocatedString(k, v, l); } \
    STDMETHODIMP GetBlobSize(REFGUID k, UINT32* s) override { return inner->GetBlobSize(k, s); } \
    STDMETHODIMP GetBlob(REFGUID k, UINT8* b, UINT32 c, UINT32* s) override { return inner->GetBlob(k, b, c, s); } \
    STDMETHODIMP GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* s) override { return inner->GetAllocatedBlob(k, b, s); } \
    STDMETHODIMP GetUnknown(REFGUID k, REFIID r, LPVOID* v) override { return inner->GetUnknown(k, r, v); } \
    STDMETHODIMP SetItem(REFGUID k, REFPROPVARIANT v) override { return inner->SetItem(k, v); } \
    STDMETHODIMP DeleteItem(REFGUID k) override { return inner->DeleteItem(k); } \
    STDMETHODIMP DeleteAllItems() override { return inner->DeleteAllItems(); } \
    STDMETHODIMP SetUINT32(REFGUID k, UINT32 v) override { return inner->SetUINT32(k, v); } \
    STDMETHODIMP SetUINT64(REFGUID k, UINT64 v) override { return inner->SetUINT64(k, v); } \
    STDMETHODIMP SetDouble(REFGUID k, double v) override { return inner->SetDouble(k, v); } \
    STDMETHODIMP SetGUID(REFGUID k, REFGUID v) override { return inner->SetGUID(k, v); } \
    STDMETHODIMP SetString(REFGUID k, LPCWSTR v) override { return inner->SetString(k, v); } \
    STDMETHODIMP SetBlob(REFGUID k, const UINT8* b, UINT32 c) override { return inner->SetBlob(k, b, c); } \
    STDMETHODIMP SetUnknown(REFGUID k, IUnknown* v) override { return inner->SetUnknown(k, v); } \
    STDMETHODIMP LockStore() override { return inner->LockStore(); } \
    STDMETHODIMP UnlockStore() override { return inner->UnlockStore(); } \
    STDMETHODIMP GetCount(UINT32* c) override { return inner->GetCount(c); } \
    STDMETHODIMP GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v) override { return inner->GetItemByIndex(i, k, v); } \
    STDMETHODIMP CopyAllItems(IMFAttributes* d) override { return inner->CopyAllItems(d); }
