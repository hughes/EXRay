#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <cstdint>
#include <cstdio>
#include <windows.h>

struct StartupTiming
{
    int64_t frequency = 0;
    int64_t processStart = 0;
    int64_t fileReadStarted = 0;
    int64_t windowVisible = 0;
    int64_t d3dReady = 0;
    int64_t exrLoaded = 0;
    int64_t textureUploaded = 0;
    int64_t firstPresent = 0;

    void Init()
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        frequency = freq.QuadPart;
    }

    static int64_t Now()
    {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return t.QuadPart;
    }

    double MsSince(int64_t start) const
    {
        if (start == 0 || frequency == 0)
            return -1.0;
        return static_cast<double>(Now() - start) * 1000.0 / static_cast<double>(frequency);
    }

    double MsBetween(int64_t start, int64_t end) const
    {
        if (start == 0 || end == 0 || frequency == 0)
            return -1.0;
        return static_cast<double>(end - start) * 1000.0 / static_cast<double>(frequency);
    }

    void LogToDebugOutput() const
    {
        wchar_t buf[512];

        OutputDebugStringW(L"[EXRay] === Startup Timing ===\n");

        if (windowVisible)
        {
            swprintf_s(buf, L"[EXRay] Window visible:     %6.1f ms\n", MsBetween(processStart, windowVisible));
            OutputDebugStringW(buf);
        }

        if (d3dReady)
        {
            swprintf_s(buf, L"[EXRay] D3D11 ready:        %6.1f ms\n", MsBetween(processStart, d3dReady));
            OutputDebugStringW(buf);
        }

        if (exrLoaded)
        {
            swprintf_s(buf, L"[EXRay] EXR loaded:         %6.1f ms\n", MsBetween(processStart, exrLoaded));
            OutputDebugStringW(buf);
        }

        if (textureUploaded)
        {
            swprintf_s(buf, L"[EXRay] Texture uploaded:   %6.1f ms\n", MsBetween(processStart, textureUploaded));
            OutputDebugStringW(buf);
        }

        if (firstPresent)
        {
            swprintf_s(buf, L"[EXRay] First present:      %6.1f ms  << TIME TO PIXELS\n",
                       MsBetween(processStart, firstPresent));
            OutputDebugStringW(buf);
        }

        OutputDebugStringW(L"[EXRay] ========================\n");
    }
};
