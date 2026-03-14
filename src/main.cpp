// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "app.h"
#include "benchmark.h"
#include "crash_handler.h"
#include "themes.h"
#include "timing.h"
#include "validate.h"

#include <crtdbg.h>
#include <string>
#include <windows.h>


static const wchar_t* const kAppMutexName = L"EXRay_{387a4a4c-d19e-4ae5-9bfc-bbaa59ceccb1}";

StartupTiming g_timing;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    // --- CRT debug heap: dump unfreed allocations at exit (debug builds only) ---
    // Reports written to %TEMP%\EXRay_debug.log AND OutputDebugString.
#ifdef _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    {
        wchar_t tempDir[MAX_PATH];
        GetTempPathW(MAX_PATH, tempDir);
        wchar_t logPath[MAX_PATH];
        swprintf_s(logPath, L"%sEXRay_debug.log", tempDir);
        HANDLE hLogFile = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ,
                                      nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hLogFile != INVALID_HANDLE_VALUE)
        {
            _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
            _CrtSetReportFile(_CRT_WARN, hLogFile);
            _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
            _CrtSetReportFile(_CRT_ERROR, hLogFile);
        }
    }
#endif

    CrashHandler::Install();

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

    // --benchmark <images_path> <output_file> [iterations]: headless benchmark mode.
    // Loads each MustLoad .exr multiple times, writes JSON results to <output_file>,
    // exits with 0 (success) or 1 (error).
    {
        std::wstring cmdLine = lpCmdLine ? lpCmdLine : L"";
        const std::wstring flag = L"--benchmark";
        auto pos = cmdLine.find(flag);
        if (pos != std::wstring::npos)
        {
            std::wstring rest = cmdLine.substr(pos + flag.size());
            size_t start = rest.find_first_not_of(L" \t");
            if (start != std::wstring::npos)
                rest = rest.substr(start);

            std::wstring imagesPath, outputFile;
            int iterations = 5;

            size_t sep = rest.find(L' ');
            if (sep != std::wstring::npos)
            {
                imagesPath = rest.substr(0, sep);
                std::wstring remainder = rest.substr(sep + 1);
                size_t s = remainder.find_first_not_of(L" \t");
                if (s != std::wstring::npos)
                    remainder = remainder.substr(s);

                size_t sep2 = remainder.find(L' ');
                if (sep2 != std::wstring::npos)
                {
                    outputFile = remainder.substr(0, sep2);
                    std::wstring iterStr = remainder.substr(sep2 + 1);
                    size_t s2 = iterStr.find_first_not_of(L" \t");
                    if (s2 != std::wstring::npos)
                    {
                        iterStr = iterStr.substr(s2);
                        int n = _wtoi(iterStr.c_str());
                        if (n > 0)
                            iterations = n;
                    }
                }
                else
                {
                    outputFile = remainder;
                }
            }
            else
            {
                imagesPath = rest;
            }

            auto stripQuotes = [](std::wstring& s) {
                if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
                    s = s.substr(1, s.size() - 2);
            };
            stripQuotes(imagesPath);
            stripQuotes(outputFile);

            if (outputFile.empty())
                outputFile = L"benchmark_results.json";

            return RunBenchmark(imagesPath, outputFile, iterations);
        }
    }

    // --smoke-test <file>: GUI smoke test mode.
    // Forces WARP, suppresses dialogs, exits after rendering one frame.
    // Exit code: 0 = success, 1 = failure.
    bool smokeTest = false;
    {
        std::wstring cmdLine = lpCmdLine ? lpCmdLine : L"";
        const std::wstring flag = L"--smoke-test";
        auto pos = cmdLine.find(flag);
        if (pos != std::wstring::npos)
        {
            smokeTest = true;
            // Strip the flag from the command line, leaving just the file path.
            std::wstring rest = cmdLine.substr(pos + flag.size());
            size_t start = rest.find_first_not_of(L" \t");
            if (start != std::wstring::npos)
                lpCmdLine = const_cast<LPWSTR>(lpCmdLine + pos + flag.size() + start);
            else
                lpCmdLine = const_cast<LPWSTR>(L"");
        }
    }

    // Single-instance check: if another instance is running, forward the file path to it.
    // Skip in smoke-test mode — CI may run multiple instances concurrently.
    HANDLE hMutex = nullptr;
    if (!smokeTest)
        hMutex = CreateMutexW(nullptr, FALSE, kAppMutexName);
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS)
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

    if (!smokeTest)
        CrashHandler::EnableDialog();

    g_timing.Init();
    g_timing.processStart = StartupTiming::Now();

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Theme::Init();

    App app;
    if (!app.Initialize(hInstance, nCmdShow, lpCmdLine, g_timing, smokeTest))
    {
        if (hMutex)
            CloseHandle(hMutex);
        return 1;
    }

    int result = app.Run();

    g_timing.LogToDebugOutput();
    if (hMutex)
        CloseHandle(hMutex);
    return result;
}
