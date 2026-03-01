// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "app.h"
#include "timing.h"
#include "validate.h"

#include <string>
#include <windows.h>

static const wchar_t* const kAppMutexName = L"EXRay_{387a4a4c-d19e-4ae5-9bfc-bbaa59ceccb1}";

StartupTiming g_timing;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    // --validate <images_path> <output_file>: headless test mode.
    // Loads every .exr in <images_path>, writes results to <output_file>,
    // exits with 0 (all pass) or 1 (any failure).
    {
        std::wstring cmdLine = lpCmdLine ? lpCmdLine : L"";
        const std::wstring flag = L"--validate";
        auto pos = cmdLine.find(flag);
        if (pos != std::wstring::npos)
        {
            std::wstring rest = cmdLine.substr(pos + flag.size());
            // Trim leading whitespace.
            size_t start = rest.find_first_not_of(L" \t");
            if (start != std::wstring::npos)
                rest = rest.substr(start);

            // Split into two space-separated arguments: <path> <output_file>
            std::wstring imagesPath, outputFile;
            size_t sep = rest.find(L' ');
            if (sep != std::wstring::npos)
            {
                imagesPath = rest.substr(0, sep);
                outputFile = rest.substr(sep + 1);
                // Trim output file.
                size_t s = outputFile.find_first_not_of(L" \t");
                if (s != std::wstring::npos)
                    outputFile = outputFile.substr(s);
            }
            else
            {
                imagesPath = rest;
            }

            // Strip quotes from both paths.
            auto stripQuotes = [](std::wstring& s) {
                if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
                    s = s.substr(1, s.size() - 2);
            };
            stripQuotes(imagesPath);
            stripQuotes(outputFile);

            if (outputFile.empty())
                outputFile = L"validate_results.txt";

            return RunValidation(imagesPath, outputFile);
        }
    }

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
