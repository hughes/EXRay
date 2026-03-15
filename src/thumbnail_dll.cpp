// SPDX-License-Identifier: GPL-3.0-or-later

// clang-format off
#include <initguid.h>  // must precede thumbnail_provider.h to define (not just declare) the GUID
// clang-format on

#include "thumbnail_provider.h"

#include <strsafe.h>

long g_dllRefCount = 0;
static HMODULE g_hModule = nullptr;

static const wchar_t* kCLSID = L"{7B5E2C4A-9F1D-4E8B-A6C3-D2F5E8B1A4C7}";
static const wchar_t* kFriendlyName = L"EXRay EXR Thumbnail Provider";

// Thumbnail handler shell extension category
static const wchar_t* kThumbnailHandlerKey =
    L"Software\\Classes\\.exr\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}";

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
{
    if (clsid != CLSID_EXRayThumbnailProvider)
        return CLASS_E_CLASSNOTAVAILABLE;

    static EXRayClassFactory factory;
    return factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_dllRefCount == 0 ? S_OK : S_FALSE; }

// Registry helpers

static HRESULT SetRegValue(HKEY root, const wchar_t* subKey, const wchar_t* name, const wchar_t* data)
{
    HKEY hk;
    LONG rc = RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr);
    if (rc != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(rc);
    rc = RegSetValueExW(hk, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(data),
                        static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(hk);
    return HRESULT_FROM_WIN32(rc);
}

static void DeleteRegTree(HKEY root, const wchar_t* subKey) { RegDeleteTreeW(root, subKey); }

STDAPI DllRegisterServer()
{
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);

    // HKCU\Software\Classes\CLSID\{...}
    wchar_t clsidKey[128];
    StringCchPrintfW(clsidKey, ARRAYSIZE(clsidKey), L"Software\\Classes\\CLSID\\%s", kCLSID);

    HRESULT hr = SetRegValue(HKEY_CURRENT_USER, clsidKey, nullptr, kFriendlyName);
    if (FAILED(hr))
        return hr;

    wchar_t inprocKey[256];
    StringCchPrintfW(inprocKey, ARRAYSIZE(inprocKey), L"%s\\InprocServer32", clsidKey);

    hr = SetRegValue(HKEY_CURRENT_USER, inprocKey, nullptr, dllPath);
    if (FAILED(hr))
        return hr;

    hr = SetRegValue(HKEY_CURRENT_USER, inprocKey, L"ThreadingModel", L"Apartment");
    if (FAILED(hr))
        return hr;

    // Register as thumbnail handler for .exr
    hr = SetRegValue(HKEY_CURRENT_USER, kThumbnailHandlerKey, nullptr, kCLSID);
    if (FAILED(hr))
        return hr;

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    wchar_t clsidKey[128];
    StringCchPrintfW(clsidKey, ARRAYSIZE(clsidKey), L"Software\\Classes\\CLSID\\%s", kCLSID);

    DeleteRegTree(HKEY_CURRENT_USER, clsidKey);
    DeleteRegTree(HKEY_CURRENT_USER, kThumbnailHandlerKey);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
