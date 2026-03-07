// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <propsys.h>
#include <thumbcache.h>
#include <shlobj.h>

// {7B5E2C4A-9F1D-4E8B-A6C3-D2F5E8B1A4C7}
DEFINE_GUID(CLSID_EXRayThumbnailProvider,
    0x7b5e2c4a, 0x9f1d, 0x4e8b, 0xa6, 0xc3, 0xd2, 0xf5, 0xe8, 0xb1, 0xa4, 0xc7);

extern long g_dllRefCount;

class EXRayThumbnailProvider : public IThumbnailProvider, public IInitializeWithStream
{
  public:
    EXRayThumbnailProvider();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* pstream, DWORD grfMode) override;

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override;

  private:
    ~EXRayThumbnailProvider();
    long m_refCount;
    IStream* m_pStream;
};

class EXRayClassFactory : public IClassFactory
{
  public:
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    IFACEMETHODIMP LockServer(BOOL fLock) override;
};
