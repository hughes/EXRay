// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "app.h"
#include "timing.h"

#include <windows.h>

static const wchar_t* const kAppMutexName = L"EXRay_{387a4a4c-d19e-4ae5-9bfc-bbaa59ceccb1}";

StartupTiming g_timing;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    // Single-instance check: if another instance is running, forward the file path to it.
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, kAppMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (lpCmdLine && lpCmdLine[0] != L'\0')
        {
            HWND existing = FindWindowW(kWindowClass, nullptr);
            if (existing)
            {
                // Strip surrounding quotes if present
                std::wstring path = lpCmdLine;
                if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
                    path = path.substr(1, path.size() - 2);

                COPYDATASTRUCT cds = {};
                cds.dwData = kCopyDataOpenFile;
                cds.cbData = static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t));
                cds.lpData = const_cast<wchar_t*>(path.c_str());
                SendMessageW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));

                // Bring the existing window to the foreground
                if (IsIconic(existing))
                    ShowWindow(existing, SW_RESTORE);
                SetForegroundWindow(existing);
            }
        }
        CloseHandle(hMutex);
        return 0;
    }

    g_timing.Init();
    g_timing.processStart = StartupTiming::Now();

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    App app;
    if (!app.Initialize(hInstance, nCmdShow, lpCmdLine, g_timing))
    {
        CloseHandle(hMutex);
        return 1;
    }

    int result = app.Run();

    g_timing.LogToDebugOutput();
    CloseHandle(hMutex);
    return result;
}
