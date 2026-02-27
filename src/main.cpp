#ifndef UNICODE
#define UNICODE
#endif

#include "app.h"
#include "timing.h"

#include <windows.h>

StartupTiming g_timing;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    g_timing.Init();
    g_timing.processStart = StartupTiming::Now();

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    App app;
    if (!app.Initialize(hInstance, nCmdShow, lpCmdLine, g_timing))
        return 1;

    int result = app.Run();

    g_timing.LogToDebugOutput();
    return result;
}
