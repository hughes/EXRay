// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <dbghelp.h>
#include <windows.h>

namespace CrashHandler
{

inline bool g_showDialog = false;

inline LONG WINAPI Handler(EXCEPTION_POINTERS* ep)
{
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t dumpPath[MAX_PATH];
    swprintf_s(dumpPath, L"%sEXRay_crash_%04d%02d%02d_%02d%02d%02d_%lu.dmp", tempDir, st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());

    HANDLE hFile = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData |
                                                     MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                          &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }

    if (g_showDialog)
    {
        wchar_t msg[MAX_PATH + 256];
        swprintf_s(msg,
                   L"EXRay crashed. A diagnostic dump has been saved to:\n\n%s\n\n"
                   L"Please include this file when reporting the issue.",
                   dumpPath);
        MessageBoxW(nullptr, msg, L"EXRay - Crash", MB_ICONERROR | MB_OK);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

inline void Install() { SetUnhandledExceptionFilter(Handler); }

inline void EnableDialog() { g_showDialog = true; }

} // namespace CrashHandler
