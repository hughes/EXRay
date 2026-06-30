// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "app.h"

#include "display_mode.h"
#include "resource.h"

#include <ImfThreading.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <shlobj.h>
#include <thread>

static const wchar_t* ExtractFilename(const std::wstring& path)
{
    const wchar_t* fn = wcsrchr(path.c_str(), L'\\');
    if (!fn)
        fn = wcsrchr(path.c_str(), L'/');
    return fn ? fn + 1 : path.c_str();
}

static std::wstring FormatSequenceTabTitle(const std::wstring& path, const SequenceInfo& seq);

bool App::Initialize(HINSTANCE hInstance, int nCmdShow, LPWSTR cmdLine, StartupTiming& timing, bool smokeTest)
{
    m_hInstance = hInstance;
    m_timing = &timing;
    m_smokeTest = smokeTest;

    // Enable OpenEXR's parallel scanline decompression. Set once, process-wide,
    // before any LoadEXR call. This is the single biggest lever on decode time.
    unsigned hwThreads = std::thread::hardware_concurrency();
    Imf::setGlobalThreadCount(static_cast<int>(hwThreads > 0 ? hwThreads : 4));

    // If a file path was passed on the command line, start loading immediately
    // on a background thread — before window/D3D11 init.
    std::wstring cmdLinePath;
    if (cmdLine && cmdLine[0] != L'\0')
    {
        cmdLinePath = cmdLine;
        // Trim surrounding whitespace and quotes. A stray trailing space (e.g. from
        // a launcher) still opens via CreateFile but would corrupt the sequence
        // suffix in DetectSequence, silently disabling sequence mode.
        auto trim = [](std::wstring& s)
        {
            size_t a = s.find_first_not_of(L" \t");
            size_t b = s.find_last_not_of(L" \t");
            s = (a == std::wstring::npos) ? L"" : s.substr(a, b - a + 1);
        };
        trim(cmdLinePath);
        if (cmdLinePath.size() >= 2 && cmdLinePath.front() == L'"' && cmdLinePath.back() == L'"')
        {
            cmdLinePath = cmdLinePath.substr(1, cmdLinePath.size() - 2);
            trim(cmdLinePath);
        }

        m_timing->fileReadStarted = StartupTiming::Now();
        m_loadComplete = false;

        auto* timing_ptr = m_timing;
        m_loadThread = std::thread(
            [this, path = cmdLinePath, timing_ptr]()
            {
                ImageLoader::LoadEXR(path, m_pendingImage, m_loadError);
                timing_ptr->exrLoaded = StartupTiming::Now();
                m_loadComplete = true;
            });
    }

    // Window + D3D11 init on main thread, overlapping with file I/O
    if (!m_window.Create(
            hInstance, nCmdShow, [this](int id) { OnCommand(id); }, [this](int w, int h) { OnResize(w, h); }))
    {
        if (!m_smokeTest)
            MessageBoxW(nullptr, L"Failed to create window.", L"EXRay", MB_ICONERROR);
        return false;
    }

    // Wire up input callbacks
    m_window.onMouseWheel = [this](int x, int y, int delta, bool ctrl, bool shift)
    {
        if (shift)
        {
            m_viewport.AdjustExposure(delta > 0 ? 0.25f : -0.25f);
            SyncSidebar();
            UpdateImageStatusText();
        }
        else
        {
            m_viewport.ZoomAt(static_cast<float>(x), static_cast<float>(y), static_cast<float>(delta));
        }
        m_needsRedraw = true;
    };

    m_window.onMouseHWheel = [this](int /*x*/, int /*y*/, int delta)
    {
        m_viewport.AdjustExposure(delta > 0 ? 0.25f : -0.25f);
        SyncSidebar();
        UpdateImageStatusText();
        m_needsRedraw = true;
    };

    m_window.onPinchZoom = [this](int cx, int cy, float scale)
    {
        m_viewport.ZoomAtScale(static_cast<float>(cx), static_cast<float>(cy), scale);
        m_needsRedraw = true;
    };

    m_window.onMiddleButton = [this](int dx, int dy, bool dragging)
    {
        if (dragging)
        {
            m_viewport.Pan(static_cast<float>(dx), static_cast<float>(dy));
            m_needsRedraw = true;
        }
    };

    m_window.onMouseMove = [this](int x, int y)
    {
        if (!m_image.IsLoaded())
            return;

        float imgX, imgY;
        m_viewport.ScreenToImage(static_cast<float>(x), static_cast<float>(y), imgX, imgY);

        int ix = static_cast<int>(imgX);
        int iy = static_cast<int>(imgY);

        if (ix >= 0 && iy >= 0 && ix < m_image.width && iy < m_image.height)
        {
            const half* px = m_image.PixelAt(ix, iy);
            wchar_t coordBuf[32];
            swprintf_s(coordBuf, L" (%d, %d)", ix, iy);
            m_window.SetStatusText(0, coordBuf);

            wchar_t rgbaBuf[128];
            swprintf_s(rgbaBuf, L" R: %.4f  G: %.4f  B: %.4f  A: %.4f", float(px[0]), float(px[1]), float(px[2]),
                       float(px[3]));
            m_window.SetStatusText(1, rgbaBuf);
        }
        else
        {
            m_window.SetStatusText(0, L"");
            m_window.SetStatusText(1, L"");
        }
    };

    m_window.onKeyDown = [this](int vk)
    {
        if (vk == VK_OEM_PLUS || vk == VK_ADD)
        {
            m_viewport.AdjustExposure(0.25f);
            SyncSidebar();
            UpdateImageStatusText();
            m_needsRedraw = true;
        }
        else if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT)
        {
            m_viewport.AdjustExposure(-0.25f);
            SyncSidebar();
            UpdateImageStatusText();
            m_needsRedraw = true;
        }
        else if ((vk == VK_OEM_6 || vk == VK_OEM_4) && !m_viewport.isHDR) // [/] — gamma (SDR only)
        {
            m_viewport.AdjustGamma(vk == VK_OEM_6 ? 0.05f : -0.05f);
            SyncSidebar();
            UpdateImageStatusText();
            m_needsRedraw = true;
        }
        else if (vk == 'C')
        {
            m_histogramChannel = (m_histogramChannel + 1) % 5;
            SavePreferences();
            SyncSidebar();
        }
        else if ((GetKeyState(VK_SHIFT) & 0x8000) && (vk == VK_OEM_3 || (vk >= '1' && vk <= '5')))
        {
            m_displayMode = (vk == VK_OEM_3) ? 0 : (vk == '5') ? 5 : (vk - '0');
            m_window.UpdateMenuChecks(m_showGrid, m_displayMode);
            m_needsRedraw = true;
        }
        else if (vk == VK_LEFT && IsSequenceTab())
        {
            NavigateSequence(-1);
        }
        else if (vk == VK_RIGHT && IsSequenceTab())
        {
            NavigateSequence(1);
        }
        else if (vk == VK_HOME && IsSequenceTab())
        {
            NavigateSequence(-m_openTabs[m_activeTab].sequence.currentIndex);
        }
        else if (vk == VK_END && IsSequenceTab())
        {
            auto& seq = m_openTabs[m_activeTab].sequence;
            NavigateSequence(seq.FrameCount() - 1 - seq.currentIndex);
        }
        else if (vk == VK_SPACE && IsSequenceTab())
        {
            m_window.GetSidebar().TogglePlayback();
        }
        else if (vk == 'G')
        {
            m_showGrid = !m_showGrid;
            SavePreferences();
            m_window.UpdateMenuChecks(m_showGrid, m_displayMode);
            m_needsRedraw = true;
        }
        else if (vk == VK_F11)
        {
            m_window.ToggleFullscreen();
        }
        else if (vk == VK_ESCAPE)
        {
            if (m_compareMode != CompareMode::Off)
                ExitCompare();
            else if (m_window.IsFullscreen())
                m_window.ToggleFullscreen();
            else
                DestroyWindow(m_window.GetHwnd());
        }
    };

    m_window.onDrop = [this](const wchar_t* path) { OpenFile(path); };

    m_window.onDisplayChange = [this]()
    {
        bool changed = m_renderer.RefreshHDRInfo(m_window.GetRenderHwnd());
        if (!changed)
            return;
        if (m_renderer.IsHDREnabled() && !m_renderer.GetHDRInfo().isHDRCapable)
        {
            m_renderer.SetHDRMode(false);
            m_viewport.isHDR = false;
            m_viewport.displayMaxNits = 80.0f;
        }
        else if (m_renderer.IsHDREnabled())
        {
            m_viewport.displayMaxNits = m_renderer.GetHDRInfo().maxLuminance;
        }
        m_window.UpdateHDRMenu(m_renderer.GetHDRInfo().isHDRCapable, m_renderer.IsHDREnabled());
        SyncSidebar();
        UpdateImageStatusText();
        m_needsRedraw = true;
    };
    m_window.onTabChange = [this](int index) { SwitchToTab(index); };
    m_window.onTabClose = [this](int index) { CloseTabAtIndex(index); };

    // Sidebar callbacks
    Sidebar& sidebar = m_window.GetSidebar();
    sidebar.onExposureChange = [this](float ev)
    {
        m_viewport.exposure = ev;
        UpdateImageStatusText();
        m_needsRedraw = true;
    };
    sidebar.onGammaChange = [this](float g)
    {
        m_viewport.gamma = g;
        UpdateImageStatusText();
        m_needsRedraw = true;
    };
    sidebar.onAutoExposure = [this]()
    {
        if (m_histogram.isValid)
        {
            m_viewport.exposure = m_histogram.autoExposure;
            SyncSidebar();
            UpdateImageStatusText();
            m_needsRedraw = true;
        }
    };
    sidebar.onHistogramChannel = [this](int channel)
    {
        m_histogramChannel = channel;
        SavePreferences();
        m_window.UpdateMenuChecks(m_showGrid, m_displayMode);
        SyncSidebar();
        m_needsRedraw = true;
    };
    sidebar.onLayerSelect = [this](int layerIndex) { LoadLayer(layerIndex); };
    sidebar.onSequenceNav = [this](int delta) { NavigateSequence(delta); };
    sidebar.onSequenceFirst = [this]()
    {
        if (IsSequenceTab())
            NavigateSequence(-m_openTabs[m_activeTab].sequence.currentIndex);
    };
    sidebar.onSequenceLast = [this]()
    {
        if (IsSequenceTab())
        {
            auto& seq = m_openTabs[m_activeTab].sequence;
            NavigateSequence(seq.FrameCount() - 1 - seq.currentIndex);
        }
    };
    sidebar.onSequenceJump = [this](int frameNumber) { JumpToFrame(frameNumber); };
    sidebar.onQueryCacheStates = [this](std::vector<uint8_t>& out) { m_seqCache.SnapshotStates(out); };
    sidebar.onLoopModeChanged = [this](int mode)
    {
        // Ping-pong (mode 2) plays backward too, so keep a reverse buffer;
        // Loop/Once only go forward, so prefetch pure forward-wrap.
        m_seqCache.SetReverseLookback(mode == 2 ? 8 : 0);
    };
    sidebar.onCompareMode = [this](int mode)
    {
        SetCompareMode(static_cast<CompareMode>(mode));
    };
    sidebar.onCompareSwap = [this]() { SwapCompareSources(); };
    sidebar.onCompareFocusA = [this]()
    {
        if (m_compareMode != CompareMode::Off)
            SetCompareFocus(0);
    };
    sidebar.onCompareFocusB = [this]()
    {
        if (m_compareMode != CompareMode::Off)
            SetCompareFocus(1);
    };

    m_window.onContextMenu = [this](int screenX, int screenY)
    {
        if (!m_image.IsLoaded())
            return;

        POINT pt = {screenX, screenY};
        ScreenToClient(m_window.GetRenderHwnd(), &pt);

        float imgX, imgY;
        m_viewport.ScreenToImage(static_cast<float>(pt.x), static_cast<float>(pt.y), imgX, imgY);

        int ix = static_cast<int>(imgX);
        int iy = static_cast<int>(imgY);

        if (ix < 0 || iy < 0 || ix >= m_image.width || iy >= m_image.height)
            return;

        const half* px = m_image.PixelAt(ix, iy);

        HMENU popup = CreatePopupMenu();
        wchar_t label[256];
        swprintf_s(label, L"Copy Pixel Value  (%.4f, %.4f, %.4f, %.4f)", float(px[0]), float(px[1]), float(px[2]),
                   float(px[3]));
        AppendMenuW(popup, MF_STRING, 1, label);

        int cmd =
            TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, m_window.GetHwnd(), nullptr);
        DestroyMenu(popup);

        if (cmd == 1)
        {
            wchar_t clipText[128];
            swprintf_s(clipText, L"R: %.4f  G: %.4f  B: %.4f  A: %.4f", float(px[0]), float(px[1]), float(px[2]),
                       float(px[3]));

            if (OpenClipboard(m_window.GetHwnd()))
            {
                EmptyClipboard();
                size_t len = (wcslen(clipText) + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
                if (hMem)
                {
                    memcpy(GlobalLock(hMem), clipText, len);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
            }
        }
    };

    m_timing->windowVisible = StartupTiming::Now();

    if (!m_renderer.Initialize(m_window.GetRenderHwnd(), m_smokeTest))
    {
        if (!m_smokeTest)
            MessageBoxW(nullptr, L"Failed to initialize D3D11.", L"EXRay", MB_ICONERROR);
        return false;
    }

    m_timing->d3dReady = StartupTiming::Now();

    if (!m_smokeTest)
    {
        LoadRecentFiles();
        LoadPreferences();
    }
    // Propagate HDR state to viewport (before SyncSidebar so gamma slider visibility is correct)
    if (m_renderer.IsHDREnabled())
    {
        m_viewport.isHDR = true;
        const auto& hdrInfo = m_renderer.GetHDRInfo();
        m_viewport.displayMaxNits = hdrInfo.maxLuminance;
    }
    m_window.UpdateHDRMenu(m_renderer.GetHDRInfo().isHDRCapable, m_renderer.IsHDREnabled());
    m_window.UpdateMenuChecks(m_showGrid, m_displayMode);
    SyncSidebar();

    // Set initial client size in viewport
    int cw, ch;
    m_window.GetClientSize(cw, ch);
    m_viewport.clientWidth = static_cast<float>(cw);
    m_viewport.clientHeight = static_cast<float>(ch);

    // If background load was started, wait for it and upload
    if (m_loadThread.joinable())
    {
        m_loadThread.join();

        if (m_pendingImage.IsLoaded())
        {
            m_image = std::move(m_pendingImage);
            m_renderer.UploadImage(m_image);
            m_histogram = HistogramComputer::Compute(m_image);

            // Auto-select "RGB (ignore alpha)" when alpha is all zeros
            m_displayMode = AutoSelectDisplayMode(kDisplayModeRGB, m_image.alphaAllZero);
            m_window.UpdateMenuChecks(m_showGrid, m_displayMode);

            m_timing->textureUploaded = StartupTiming::Now();

            m_viewport.imageWidth = static_cast<float>(m_image.width);
            m_viewport.imageHeight = static_cast<float>(m_image.height);
            m_viewport.exposure = m_histogram.autoExposure;
            m_viewport.FitToWindow();

            // Scan layers for initial file
            std::string layerError;
            ImageLoader::ScanLayers(cmdLinePath, m_layerInfo, layerError);
            m_activeLayer = 0;
            ResetLayerExposures();

            // Detect whether the launched file belongs to a numbered sequence so
            // command-line / double-click opens enable sequence navigation (and
            // the prefetch cache), matching the File>Open path.
            SequenceInfo seq = DetectSequence(cmdLinePath);

            OpenTab startupTab{cmdLinePath,
                               m_viewport.exposure,
                               m_viewport.zoom,
                               m_viewport.panX,
                               m_viewport.panY,
                               m_viewport.gamma,
                               {},
                               {},
                               m_layerInfo,
                               m_activeLayer};
            startupTab.sequence = std::move(seq);
            std::wstring tabTitle = FormatSequenceTabTitle(cmdLinePath, startupTab.sequence);
            m_openTabs.push_back(std::move(startupTab));
            m_activeTab = 0;
            m_window.AddTab(0, tabTitle.c_str());
            m_window.SetActiveTab(0);

            m_viewport.FitToWindow();

            wchar_t title[MAX_PATH + 16];
            swprintf_s(title, L"EXRay - %s", cmdLinePath.c_str());
            m_window.SetTitle(title);

            SyncSidebar();
            UpdateImageStatusText();
            SyncSequencePanel();
            EnsureSequenceCache();
            AddToRecentFiles(cmdLinePath);
        }
        else if (!m_loadError.empty())
        {
            if (m_smokeTest)
                return false;
            int len = MultiByteToWideChar(CP_UTF8, 0, m_loadError.c_str(), -1, nullptr, 0);
            std::wstring wideError(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, m_loadError.c_str(), -1, wideError.data(), len);
            MessageBoxW(m_window.GetHwnd(), wideError.c_str(), L"EXRay - Error", MB_ICONERROR);
        }
    }

    if (!m_smokeTest)
        StartUpdateCheck();

    return true;
}

void App::StartUpdateCheck()
{
    m_updateCheckComplete = false;
    HWND hwnd = m_window.GetHwnd();

    m_updateThread = std::thread(
        [this, hwnd]()
        {
            m_updateResult = UpdateChecker::Check(EXRAY_VERSION_MAJOR, EXRAY_VERSION_MINOR, EXRAY_VERSION_PATCH);
            m_updateCheckComplete = true;
            PostMessageW(hwnd, WM_APP + 1, 0, 0);
        });
}

void App::SetPlayBenchmark(const std::wstring& outPath, int frames, int fps)
{
    m_playBench = true;
    m_playBenchOut = outPath;
    m_playBenchTarget = frames;
    m_playBenchFps = fps;
}

void App::RecordPlayBenchSample(double ms, bool cacheHit, int64_t tEnd)
{
    if (m_playBenchMs.empty())
        m_playBenchFirstTick = tEnd;
    m_playBenchLastTick = tEnd;
    m_playBenchMs.push_back(ms);
    m_playBenchHit.push_back(cacheHit);

    if (static_cast<int>(m_playBenchMs.size()) >= m_playBenchTarget)
    {
        WritePlayBenchReport();
        PostQuitMessage(0);
    }
}

void App::WritePlayBenchReport()
{
    int seqLen = IsSequenceTab() ? m_openTabs[m_activeTab].sequence.FrameCount() : 0;
    int n = static_cast<int>(m_playBenchMs.size());

    auto stats = [&](int lo, int hi, double& median, double& maxv, int& misses)
    {
        std::vector<double> v;
        misses = 0;
        for (int i = lo; i < hi && i < n; ++i)
        {
            v.push_back(m_playBenchMs[i]);
            if (!m_playBenchHit[i])
                misses++;
        }
        if (v.empty())
        {
            median = maxv = 0;
            return;
        }
        std::sort(v.begin(), v.end());
        median = v[v.size() / 2];
        maxv = v.back();
    };

    double m1 = 0, x1 = 0, mr = 0, xr = 0;
    int miss1 = 0, missR = 0;
    int loop1End = (seqLen > 0 && seqLen < n) ? seqLen : n;
    stats(0, loop1End, m1, x1, miss1);
    stats(loop1End, n, mr, xr, missR);

    double wallMs = m_timing->MsBetween(m_playBenchFirstTick, m_playBenchLastTick);
    double fps = (wallMs > 0 && n > 1) ? (n - 1) * 1000.0 / wallMs : 0.0;

    FILE* f = _wfopen(m_playBenchOut.c_str(), L"w");
    if (!f)
        return;
    fprintf(f, "{\n");
    fprintf(f, "  \"frames_played\": %d,\n", n);
    fprintf(f, "  \"sequence_length\": %d,\n", seqLen);
    fprintf(f, "  \"target_fps\": %d,\n", m_playBenchFps);
    fprintf(f, "  \"effective_fps\": %.2f,\n", fps);
    fprintf(f, "  \"first_loop\":  { \"frames\": %d, \"median_ms\": %.1f, \"max_ms\": %.1f, \"cache_misses\": %d },\n",
            loop1End, m1, x1, miss1);
    fprintf(f, "  \"later_loops\": { \"frames\": %d, \"median_ms\": %.1f, \"max_ms\": %.1f, \"cache_misses\": %d }\n",
            n - loop1End, mr, xr, missR);
    fprintf(f, "}\n");
    fclose(f);
}

int App::Run()
{
    Render();
    if (!m_timing->firstPresent)
    {
        m_timing->firstPresent = StartupTiming::Now();
        m_timing->LogToDebugOutput();
    }
    m_needsRedraw = false;

    // Smoke test: one frame rendered successfully — exit immediately.
    if (m_smokeTest)
        return 0;

    // Headless playback benchmark: kick off playback from frame 1 at the target fps.
    if (m_playBench && IsSequenceTab())
    {
        m_window.GetSidebar().SetFps(m_playBenchFps);
        m_window.GetSidebar().TogglePlayback();
    }

    MSG msg = {};
    while (true)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                if (m_updateThread.joinable())
                    m_updateThread.join();
                return static_cast<int>(msg.wParam);
            }
            if (!TranslateAcceleratorW(m_window.GetHwnd(), m_window.GetAccelTable(), &msg))
            {
                // Forward keyboard input from other controls (e.g. tab bar) to the render area
                // so application hotkeys work regardless of which child has focus.
                if (msg.message == WM_KEYDOWN && msg.hwnd != m_window.GetRenderHwnd())
                    SendMessageW(m_window.GetRenderHwnd(), msg.message, msg.wParam, msg.lParam);

                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        // Handle update check completion
        if (m_updateCheckComplete && !m_updateAvailable)
        {
            if (m_updateThread.joinable())
                m_updateThread.join();
            if (m_updateResult.updateAvailable)
            {
                m_updateAvailable = true;
                m_updateVersion = m_updateResult.newVersion;
                m_window.MarkHelpMenuUpdate(true);
            }
            m_updateCheckComplete = false;
        }

        if (m_needsRedraw)
        {
            Render();
            m_needsRedraw = false;
        }
        else if (m_histogramDirty)
        {
            // Sequence stepping/playback deferred the histogram (a ~150ms recompute
            // that would otherwise stutter navigation). Only recompute once
            // navigation has been quiet for a moment; otherwise sleep until then
            // (or until input arrives) so playback ticks aren't blocked.
            constexpr double kQuietMs = 250.0;
            double sinceNav = m_timing->MsSince(m_lastNavTick);
            if (sinceNav < 0.0 || sinceNav >= kQuietMs)
            {
                RecomputeHistogramIfDirty();
            }
            else
            {
                DWORD waitMs = static_cast<DWORD>(kQuietMs - sinceNav) + 5;
                MsgWaitForMultipleObjectsEx(0, nullptr, waitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        }
        else
        {
            WaitMessage();
        }
    }
}

// Pack a row-major 3x3 matrix into the padded float4x3 layout for HLSL
static void PackColorMatrix(const float src[9], float dst[12])
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = 0.0f;
    dst[4] = src[3];
    dst[5] = src[4];
    dst[6] = src[5];
    dst[7] = 0.0f;
    dst[8] = src[6];
    dst[9] = src[7];
    dst[10] = src[8];
    dst[11] = 0.0f;
}

void App::Render()
{
    m_renderer.BeginFrame(0.18f, 0.18f, 0.18f);

    if (m_renderer.HasImage())
    {
        ViewportCB vp = m_viewport.ToViewportCB();
        vp.showGrid = m_showGrid ? 1 : 0;
        vp.displayMode = m_displayMode;

        // Compare mode parameters
        vp.compareMode = static_cast<int>(m_compareMode);
        vp.splitX = m_splitX * m_viewport.clientWidth;
        vp.compareGain = m_compareGain;

        if (m_compareMode != CompareMode::Off &&
            m_compareBase.image.IsLoaded() && m_compareBlend.image.IsLoaded())
        {
            PackColorMatrix(m_compareBase.image.colorMatrix, vp.colorMatrix);
            PackColorMatrix(m_compareBlend.image.colorMatrix, vp.colorMatrixB);
        }
        else
        {
            PackColorMatrix(m_image.colorMatrix, vp.colorMatrix);
        }

        m_renderer.RenderImage(vp);
    }

    m_renderer.EndFrame();
}

void App::OnCommand(int commandId)
{
    // Handle recent file commands
    if (commandId >= IDM_FILE_RECENT_BASE && commandId <= IDM_FILE_RECENT_MAX)
    {
        int index = commandId - IDM_FILE_RECENT_BASE;
        if (index >= 0 && index < static_cast<int>(m_recentFiles.size()))
            OpenFile(m_recentFiles[index]);
        return;
    }

    switch (commandId)
    {
    case IDM_FILE_OPEN:
        OpenFileDialog();
        break;

    case IDM_FILE_RELOAD:
        if (m_activeTab >= 0)
        {
            LoadFile(m_openTabs[m_activeTab].path);
            m_openTabs[m_activeTab].exposure = m_viewport.exposure;
        }
        break;

    case IDM_FILE_CLOSE:
        CloseCurrentTab();
        break;

    case IDM_FILE_NEXT_TAB:
        if (!m_openTabs.empty())
        {
            int next = (m_activeTab + 1) % static_cast<int>(m_openTabs.size());
            SwitchToTab(next);
        }
        break;

    case IDM_FILE_PREV_TAB:
        if (!m_openTabs.empty())
        {
            int prev = (m_activeTab - 1 + static_cast<int>(m_openTabs.size())) % static_cast<int>(m_openTabs.size());
            SwitchToTab(prev);
        }
        break;

    case IDM_FILE_EXIT:
        DestroyWindow(m_window.GetHwnd());
        break;

    case IDM_VIEW_FIT:
        m_viewport.FitToWindow();
        m_needsRedraw = true;
        break;

    case IDM_VIEW_ACTUAL:
        m_viewport.ActualSize();
        m_needsRedraw = true;
        break;

    case IDM_VIEW_GRID:
        m_showGrid = !m_showGrid;
        SavePreferences();
        m_window.UpdateMenuChecks(m_showGrid, m_displayMode);
        m_needsRedraw = true;
        break;

    case IDM_VIEW_CHANNEL_RGB:
    case IDM_VIEW_CHANNEL_R:
    case IDM_VIEW_CHANNEL_G:
    case IDM_VIEW_CHANNEL_B:
    case IDM_VIEW_CHANNEL_A:
    case IDM_VIEW_CHANNEL_RGB_NO_ALPHA:
        m_displayMode = (commandId == IDM_VIEW_CHANNEL_RGB_NO_ALPHA) ? 5 : (commandId - IDM_VIEW_CHANNEL_RGB);
        m_window.UpdateMenuChecks(m_showGrid, m_displayMode);
        m_needsRedraw = true;
        break;

    case IDM_VIEW_HDR:
    {
        bool newState = !m_renderer.IsHDREnabled();
        if (m_renderer.SetHDRMode(newState))
        {
            m_viewport.isHDR = newState;
            if (newState)
                m_viewport.displayMaxNits = m_renderer.GetHDRInfo().maxLuminance;
            else
                m_viewport.displayMaxNits = 80.0f;
            m_window.UpdateHDRMenu(true, newState);
            SyncSidebar();
            UpdateImageStatusText();
            m_needsRedraw = true;
        }
        break;
    }

    case IDM_VIEW_EXPOSURE_UP:
        m_viewport.AdjustExposure(0.25f);
        SyncSidebar();
        UpdateImageStatusText();
        m_needsRedraw = true;
        break;

    case IDM_VIEW_EXPOSURE_DOWN:
        m_viewport.AdjustExposure(-0.25f);
        SyncSidebar();
        UpdateImageStatusText();
        m_needsRedraw = true;
        break;

    case IDM_VIEW_GAMMA_UP:
        if (!m_viewport.isHDR)
        {
            m_viewport.AdjustGamma(0.05f);
            SyncSidebar();
            UpdateImageStatusText();
            m_needsRedraw = true;
        }
        break;

    case IDM_VIEW_GAMMA_DOWN:
        if (!m_viewport.isHDR)
        {
            m_viewport.AdjustGamma(-0.05f);
            SyncSidebar();
            UpdateImageStatusText();
            m_needsRedraw = true;
        }
        break;

    case IDM_VIEW_FULLSCREEN:
        m_window.ToggleFullscreen();
        break;

    case IDM_COMPARE_SPLIT:
        SetCompareMode(CompareMode::Split);
        break;

    case IDM_COMPARE_DIFF:
        SetCompareMode(CompareMode::Difference);
        break;

    case IDM_COMPARE_OFF:
        ExitCompare();
        break;

    case IDM_COMPARE_SWAP:
        SwapCompareSources();
        break;

    case IDM_COMPARE_SET_A:
        if (m_compareMode != CompareMode::Off)
            SetCompareFocus(0);
        break;

    case IDM_COMPARE_SET_B:
        if (m_compareMode != CompareMode::Off)
            SetCompareFocus(1);
        break;

    case IDM_HELP_ABOUT:
    {
        HICON bigIcon = static_cast<HICON>(
            LoadImageW(m_hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR));

        std::wstring content = L"Version " EXRAY_VERSION_WSTR L"\n" EXRAY_COPYRIGHT_W L"\n\n"
                               L"<a href=\"https://github.com/hughes/EXRay\">github.com/hughes/EXRay</a>\n"
                               L"Open-source. Free of ads, forever.";

        if (m_updateAvailable)
        {
            wchar_t verW[32];
            MultiByteToWideChar(CP_UTF8, 0, m_updateVersion.c_str(), -1, verW, 32);
            content += L"\n\n<a href=\"https://github.com/hughes/EXRay/releases/latest\">"
                       L"Update available: v";
            content += verW;
            content += L"</a>";
        }

        TASKDIALOGCONFIG tdc = {sizeof(tdc)};
        tdc.hwndParent = m_window.GetHwnd();
        tdc.hInstance = m_hInstance;
        tdc.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_HICON_MAIN;
        tdc.dwCommonButtons = TDCBF_OK_BUTTON;
        tdc.hMainIcon = bigIcon;
        tdc.pszWindowTitle = L"About EXRay";
        tdc.pszMainInstruction = L"EXRay";
        tdc.pszContent = content.c_str();
        tdc.pfCallback = [](HWND hwnd, UINT msg, WPARAM, LPARAM lParam, LONG_PTR) -> HRESULT
        {
            if (msg == TDN_HYPERLINK_CLICKED)
                ShellExecuteW(hwnd, L"open", reinterpret_cast<LPCWSTR>(lParam), nullptr, nullptr, SW_SHOW);
            return S_OK;
        };
        TaskDialogIndirect(&tdc, nullptr, nullptr, nullptr);
        if (bigIcon)
            DestroyIcon(bigIcon);
        break;
    }
    }
}

void App::OnResize(int width, int height)
{
    m_renderer.Resize(width, height);

    float newW = static_cast<float>(width);
    float newH = static_cast<float>(height);

    // Shift pan so the image point at screen center stays centered.
    // Zoom (source pixels per display pixel) is preserved.
    m_viewport.panX += (newW - m_viewport.clientWidth) * 0.5f;
    m_viewport.panY += (newH - m_viewport.clientHeight) * 0.5f;

    m_viewport.clientWidth = newW;
    m_viewport.clientHeight = newH;

    // Render immediately so the image redraws during modal resize drag
    // (Windows runs its own message loop while dragging, so our Run() loop
    // doesn't get to call Render()).  Guard against early WM_SIZE during
    // window creation when the renderer isn't initialized yet.
    // Present without vsync so the frame appears instantly — with vsync the
    // frame is queued for the next vblank, by which time the window has
    // already moved to a new size and the compositor stretches the stale frame.
    if (m_renderer.GetDevice())
    {
        m_renderer.BeginFrame(0.18f, 0.18f, 0.18f);
        if (m_renderer.HasImage())
        {
            ViewportCB vp = m_viewport.ToViewportCB();
            vp.showGrid = m_showGrid ? 1 : 0;
            vp.displayMode = m_displayMode;

            vp.compareMode = static_cast<int>(m_compareMode);
            vp.splitX = m_splitX * m_viewport.clientWidth;
            vp.compareGain = m_compareGain;

            if (m_compareMode != CompareMode::Off &&
                m_compareBase.image.IsLoaded() && m_compareBlend.image.IsLoaded())
            {
                PackColorMatrix(m_compareBase.image.colorMatrix, vp.colorMatrix);
                PackColorMatrix(m_compareBlend.image.colorMatrix, vp.colorMatrixB);
            }
            else
            {
                PackColorMatrix(m_image.colorMatrix, vp.colorMatrix);
            }

            m_renderer.RenderImage(vp);
        }
        m_renderer.EndFrame(false);
        m_needsRedraw = false;
    }
}

bool App::LoadFile(const std::wstring& path, bool isSequenceStep)
{
    std::string errorMsg;
    ImageData newImage;

    if (ImageLoader::LoadEXR(path, newImage, errorMsg))
    {
        ApplyLoadedImage(std::move(newImage), path, isSequenceStep);
        return true;
    }
    else
    {
        if (!m_smokeTest)
        {
            int len = MultiByteToWideChar(CP_UTF8, 0, errorMsg.c_str(), -1, nullptr, 0);
            std::wstring wideError(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, errorMsg.c_str(), -1, wideError.data(), len);
            MessageBoxW(m_window.GetHwnd(), wideError.c_str(), L"EXRay - Error", MB_ICONERROR);
        }
        return false;
    }
}

// Install an already-decoded image as the current frame: GPU upload, histogram /
// layer handling, viewport adjustment and UI refresh. Shared by the synchronous
// decode path (LoadFile) and the prefetch-cache hit path (NavigateSequence).
void App::ApplyLoadedImage(ImageData&& img, const std::wstring& path, bool isSequenceStep)
{
    m_image = std::move(img);
    m_renderer.UploadImage(m_image);

    // During sequence stepping, exposure/zoom are locked and the layer
    // structure is identical across frames, so skip the histogram recompute
    // (deferred to idle) and the redundant per-frame layer rescan.
    if (isSequenceStep)
    {
        m_histogramDirty = true;
        // Keep the selected layer across frames (NavigateSequence loaded the right
        // layer); the layer structure is identical from frame to frame.
    }
    else
    {
        m_histogram = HistogramComputer::Compute(m_image);

        // Scan layers (metadata only)
        std::string layerError;
        ImageLoader::ScanLayers(path, m_layerInfo, layerError);
        m_activeLayer = 0;
    }

    // Auto-select "RGB (ignore alpha)" when alpha is all zeros
    m_displayMode = AutoSelectDisplayMode(kDisplayModeRGB, m_image.alphaAllZero);
    m_window.UpdateMenuChecks(m_showGrid, m_displayMode);

    // In compare mode or sequence navigation, keep viewport locked.
    // For sequences, preserve exposure/zoom/pan across frames.
    if (m_compareMode == CompareMode::Off && !IsSequenceTab())
    {
        m_viewport.imageWidth = static_cast<float>(m_image.width);
        m_viewport.imageHeight = static_cast<float>(m_image.height);
        m_viewport.exposure = m_histogram.autoExposure;
        m_viewport.FitToWindow();
    }
    else if (m_compareMode == CompareMode::Off && IsSequenceTab())
    {
        // Sequence: update dimensions, keep exposure/zoom, adjust pan to keep center fixed
        float newW = static_cast<float>(m_image.width);
        float newH = static_cast<float>(m_image.height);
        if (newW != m_viewport.imageWidth || newH != m_viewport.imageHeight)
        {
            float cx = m_viewport.panX + m_viewport.imageWidth * m_viewport.zoom * 0.5f;
            float cy = m_viewport.panY + m_viewport.imageHeight * m_viewport.zoom * 0.5f;
            m_viewport.imageWidth = newW;
            m_viewport.imageHeight = newH;
            m_viewport.panX = cx - newW * m_viewport.zoom * 0.5f;
            m_viewport.panY = cy - newH * m_viewport.zoom * 0.5f;
        }
    }

    // A full (non-sequence) load rescanned the layers — reset per-layer exposures,
    // seeding the default layer with the exposure just applied.
    if (!isSequenceStep)
        ResetLayerExposures();

    wchar_t title[MAX_PATH + 16];
    swprintf_s(title, L"EXRay - %s", path.c_str());
    m_window.SetTitle(title);

    SyncSidebar();
    UpdateImageStatusText();
    m_needsRedraw = true;
}

void App::ResetLayerExposures()
{
    m_layerExposure.assign(m_layerInfo.layers.size(), std::numeric_limits<float>::quiet_NaN());
    if (m_activeLayer >= 0 && m_activeLayer < static_cast<int>(m_layerExposure.size()))
        m_layerExposure[m_activeLayer] = m_viewport.exposure;
}

bool App::LoadLayer(int layerIndex)
{
    if (m_activeTab < 0 || layerIndex < 0 || layerIndex >= static_cast<int>(m_layerInfo.layers.size()))
        return false;

    std::string errorMsg;
    ImageData newImage;
    const std::wstring& path = m_openTabs[m_activeTab].path;

    if (ImageLoader::LoadEXRLayer(path, m_layerInfo.layers[layerIndex], newImage, errorMsg))
    {
        // When switching between mip levels of the same layer, scale zoom
        // so the image occupies the same screen space
        const auto& oldLayer = m_layerInfo.layers[m_activeLayer];
        const auto& newLayer = m_layerInfo.layers[layerIndex];
        bool isMipSwitch = oldLayer.numMipLevels > 1 && newLayer.numMipLevels > 1 && oldLayer.name == newLayer.name &&
                           oldLayer.partIndex == newLayer.partIndex;

        float oldWidth = m_viewport.imageWidth;

        // Save the outgoing layer's exposure before switching.
        if (m_layerExposure.size() != m_layerInfo.layers.size())
            m_layerExposure.resize(m_layerInfo.layers.size(), std::numeric_limits<float>::quiet_NaN());
        if (m_activeLayer >= 0 && m_activeLayer < static_cast<int>(m_layerExposure.size()))
            m_layerExposure[m_activeLayer] = m_viewport.exposure;

        m_image = std::move(newImage);
        m_renderer.UploadImage(m_image);
        m_histogram = HistogramComputer::Compute(m_image);
        m_activeLayer = layerIndex;

        // Auto-select "RGB (ignore alpha)" when alpha is all zeros
        m_displayMode = AutoSelectDisplayMode(m_displayMode, m_image.alphaAllZero);
        m_window.UpdateMenuChecks(m_showGrid, m_displayMode);

        if (m_compareMode != CompareMode::Off)
        {
            // In compare mode, keep viewport locked — don't change
            // imageWidth/Height or zoom. Just snapshot and re-upload.
            SnapshotFocusedSource();
            UploadCompareTextures();
            SyncComparePanel();
        }
        else
        {
            m_viewport.imageWidth = static_cast<float>(m_image.width);
            m_viewport.imageHeight = static_cast<float>(m_image.height);

            if (isMipSwitch && oldWidth > 0.0f)
                m_viewport.zoom =
                    (std::min)(m_viewport.zoom * oldWidth / m_viewport.imageWidth, m_viewport.MaxZoom());

            // Restore this layer's exposure, auto-computing it the first time it's
            // viewed (depth vs RGB need very different exposures).
            if (m_activeLayer < static_cast<int>(m_layerExposure.size()))
            {
                if (std::isnan(m_layerExposure[m_activeLayer]))
                    m_layerExposure[m_activeLayer] = m_histogram.isValid ? m_histogram.autoExposure : m_viewport.exposure;
                m_viewport.exposure = m_layerExposure[m_activeLayer];
            }
        }

        SyncSidebar();
        UpdateImageStatusText();
        // Re-point the prefetch cache at this layer so it warms in the background
        // (no-op for non-sequence tabs).
        EnsureSequenceCache();
        m_needsRedraw = true;
        return true;
    }
    else if (!m_smokeTest)
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, errorMsg.c_str(), -1, nullptr, 0);
        std::wstring wideError(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, errorMsg.c_str(), -1, wideError.data(), len);
        MessageBoxW(m_window.GetHwnd(), wideError.c_str(), L"EXRay - Error", MB_ICONERROR);
    }
    return false;
}

static std::wstring FormatSequenceTabTitle(const std::wstring& path, const SequenceInfo& seq)
{
    const wchar_t* fn = ExtractFilename(path);
    if (seq.frames.empty())
        return fn;

    // "render [042 / 240]" — extract base name from prefix
    const wchar_t* prefixFn = ExtractFilename(seq.prefix);
    std::wstring baseName = prefixFn;
    // Remove trailing separator (. or _)
    if (!baseName.empty() && (baseName.back() == L'.' || baseName.back() == L'_'))
        baseName.pop_back();
    if (baseName.empty())
        baseName = fn;

    wchar_t buf[MAX_PATH];
    swprintf_s(buf, L"%s [%03d / %d]", baseName.c_str(), seq.CurrentFrameNumber(), seq.FrameCount());
    return buf;
}

void App::OpenFile(const std::wstring& rawPath)
{
    // Trim stray surrounding whitespace/quotes. recent.txt entries and
    // second-instance forwarding can carry a trailing space, which would corrupt
    // the sequence suffix in DetectSequence (".exr " ≠ ".exr") and silently open
    // the file as a single frame.
    std::wstring path = rawPath;
    auto trim = [](std::wstring& s)
    {
        size_t a = s.find_first_not_of(L" \t");
        size_t b = s.find_last_not_of(L" \t");
        s = (a == std::wstring::npos) ? L"" : s.substr(a, b - a + 1);
    };
    trim(path);
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
    {
        path = path.substr(1, path.size() - 2);
        trim(path);
    }

    // Detect if this file belongs to a sequence
    SequenceInfo seq = DetectSequence(path);

    // Check if file (or its sequence) is already open
    for (int i = 0; i < static_cast<int>(m_openTabs.size()); ++i)
    {
        if (!seq.frames.empty() && !m_openTabs[i].sequence.frames.empty())
        {
            // Same sequence? Check prefix + suffix match
            if (_wcsicmp(m_openTabs[i].sequence.prefix.c_str(), seq.prefix.c_str()) == 0 &&
                _wcsicmp(m_openTabs[i].sequence.suffix.c_str(), seq.suffix.c_str()) == 0)
            {
                // Switch to this tab and navigate to the requested frame
                SwitchToTab(i);
                JumpToFrame(seq.CurrentFrameNumber());
                return;
            }
        }
        else if (_wcsicmp(m_openTabs[i].path.c_str(), path.c_str()) == 0)
        {
            SwitchToTab(i);
            return;
        }
    }

    SaveTabState();

    if (!LoadFile(path))
        return;

    OpenTab newTab = {path,
                      m_viewport.exposure,
                      m_viewport.zoom,
                      m_viewport.panX,
                      m_viewport.panY,
                      m_viewport.gamma,
                      {},
                      {},
                      m_layerInfo,
                      m_activeLayer,
                      std::move(seq)};

    std::wstring tabTitle = FormatSequenceTabTitle(path, newTab.sequence);
    m_openTabs.push_back(std::move(newTab));
    int newIndex = static_cast<int>(m_openTabs.size()) - 1;
    m_window.AddTab(newIndex, tabTitle.c_str());
    m_activeTab = newIndex;
    m_window.SetActiveTab(m_activeTab);
    // New tab starts with default playback settings (and stops any prior playback).
    m_window.GetSidebar().ResetPlaybackForTab(m_openTabs[newIndex].playbackFps, m_openTabs[newIndex].loopMode);
    AddToRecentFiles(path);

    // If compare mode is active, snapshot the new file as the focused source
    if (m_compareMode != CompareMode::Off)
    {
        SnapshotFocusedSource();
        UploadCompareTextures();
        SyncComparePanel();
    }

    SyncSequencePanel();
    EnsureSequenceCache(); // start prefetching if this is a sequence
}

void App::SwitchToTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_openTabs.size()))
        return;
    if (index == m_activeTab)
        return;

    SaveTabState();
    m_activeTab = index;
    m_window.SetActiveTab(index);

    m_image = std::move(m_openTabs[index].image);
    m_histogram = std::move(m_openTabs[index].histogram);
    m_layerInfo = m_openTabs[index].layerInfo;
    m_activeLayer = m_openTabs[index].activeLayer;
    m_layerExposure = m_openTabs[index].layerExposure;
    m_renderer.UploadImage(m_image);

    // Restore this tab's playback settings (stops any playback from the old tab).
    m_window.GetSidebar().ResetPlaybackForTab(m_openTabs[index].playbackFps, m_openTabs[index].loopMode);

    // In compare mode, keep viewport locked to the entry dimensions
    if (m_compareMode == CompareMode::Off)
    {
        m_viewport.imageWidth = static_cast<float>(m_image.width);
        m_viewport.imageHeight = static_cast<float>(m_image.height);
        m_viewport.exposure = m_openTabs[index].exposure;
        m_viewport.zoom = m_openTabs[index].zoom;
        m_viewport.panX = m_openTabs[index].panX;
        m_viewport.panY = m_openTabs[index].panY;
        m_viewport.gamma = m_openTabs[index].gamma;
    }

    wchar_t title[MAX_PATH + 16];
    swprintf_s(title, L"EXRay - %s", m_openTabs[index].path.c_str());
    m_window.SetTitle(title);

    // If compare mode is active, switching tabs updates the focused source.
    // Snapshot directly from m_image (which was just set up above) rather than
    // going through SnapshotFocusedSource() which calls SaveTabState() and
    // would move m_image back into the tab, leaving it empty.
    if (m_compareMode != CompareMode::Off)
    {
        CompareSource& target = (m_compareFocused == 1) ? m_compareBlend : m_compareBase;
        target.tabIndex = m_activeTab;
        target.layerIndex = m_activeLayer;
        target.label = MakeSourceLabel();
        if (m_image.IsLoaded())
            target.image = m_image;
        UploadCompareTextures();
    }

    SyncSidebar();
    SyncComparePanel();
    SyncSequencePanel();
    UpdateImageStatusText();
    EnsureSequenceCache(); // prefetch the new tab's sequence (or release the cache)
    m_needsRedraw = true;
}

void App::CloseCurrentTab()
{
    if (m_activeTab < 0 || m_openTabs.empty())
    {
        DestroyWindow(m_window.GetHwnd());
        return;
    }

    int closingIndex = m_activeTab;
    AdjustCompareAfterRemove(closingIndex);
    if (!m_openTabs[closingIndex].sequence.frames.empty())
        m_seqCache.Remove(SequenceCacheId(m_openTabs[closingIndex].sequence)); // free its cached frames
    m_openTabs.erase(m_openTabs.begin() + closingIndex);
    m_window.RemoveTab(closingIndex);

    if (m_openTabs.empty())
    {
        // Exit compare before clearing state — snapshots reference tab data
        if (m_compareMode != CompareMode::Off)
            ExitCompare();

        m_activeTab = -1;
        m_image = ImageData{};
        m_renderer.UploadImage(m_image);
        m_histogram = HistogramData{};
        m_layerInfo = ExrFileInfo{};
        m_activeLayer = 0;
        m_viewport.exposure = 0.0f;
        m_window.SetTitle(L"EXRay");
        m_window.SetStatusText(0, L"");
        m_window.SetStatusText(1, L"");
        m_window.SetStatusText(2, L"");
        SyncSidebar();
        EnsureSequenceCache(); // no active tab → release the prefetch cache
        m_needsRedraw = true;
    }
    else
    {
        int newActive = closingIndex;
        if (newActive >= static_cast<int>(m_openTabs.size()))
            newActive = static_cast<int>(m_openTabs.size()) - 1;

        // Set m_activeTab to -1 so SwitchToTab doesn't try to save state
        // for the now-deleted tab
        m_activeTab = -1;
        SwitchToTab(newActive);
    }
}

void App::CloseTabAtIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_openTabs.size()))
        return;

    // If closing the active tab, reuse existing logic
    if (index == m_activeTab)
    {
        CloseCurrentTab();
        return;
    }

    AdjustCompareAfterRemove(index);
    if (!m_openTabs[index].sequence.frames.empty())
        m_seqCache.Remove(SequenceCacheId(m_openTabs[index].sequence)); // free its cached frames
    m_openTabs.erase(m_openTabs.begin() + index);
    m_window.RemoveTab(index);

    // Adjust active tab index if it was after the removed tab
    if (m_activeTab > index)
        m_activeTab--;

    m_window.SetActiveTab(m_activeTab);
}

void App::SaveTabState()
{
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_openTabs.size()))
    {
        m_openTabs[m_activeTab].exposure = m_viewport.exposure;
        m_openTabs[m_activeTab].zoom = m_viewport.zoom;
        m_openTabs[m_activeTab].panX = m_viewport.panX;
        m_openTabs[m_activeTab].panY = m_viewport.panY;
        m_openTabs[m_activeTab].gamma = m_viewport.gamma;
        m_openTabs[m_activeTab].layerInfo = m_layerInfo;
        m_openTabs[m_activeTab].activeLayer = m_activeLayer;
        // Capture the active layer's current exposure, then stow the per-layer set.
        if (m_activeLayer >= 0 && m_activeLayer < static_cast<int>(m_layerExposure.size()))
            m_layerExposure[m_activeLayer] = m_viewport.exposure;
        m_openTabs[m_activeTab].layerExposure = m_layerExposure;
        // Per-tab playback settings
        Sidebar& sb = m_window.GetSidebar();
        m_openTabs[m_activeTab].playbackFps = sb.GetFps();
        m_openTabs[m_activeTab].loopMode = sb.GetLoopMode();
        if (m_image.IsLoaded())
        {
            // Flush any histogram deferred by sequence stepping before stowing the tab.
            if (m_histogramDirty)
            {
                m_histogram = HistogramComputer::Compute(m_image);
                m_histogramDirty = false;
            }
            m_openTabs[m_activeTab].image = std::move(m_image);
            m_openTabs[m_activeTab].histogram = std::move(m_histogram);
        }
    }
}

void App::SetCompareMode(CompareMode mode)
{
    if (mode == CompareMode::Off)
    {
        ExitCompare();
        return;
    }

    bool wasOff = (m_compareMode == CompareMode::Off);
    m_compareMode = mode;

    if (wasOff)
    {
        // Entering compare — snapshot current image as both base and blend
        m_compareFocused = 0;
        SnapshotFocusedSource();
        m_compareFocused = 1;
        SnapshotFocusedSource();
        m_compareFocused = 0; // default focus on base

        // Lock viewport dimensions to the current image so zoom/pan stays
        // stable regardless of source changes or swaps
        m_savedImageWidth = m_viewport.imageWidth;
        m_savedImageHeight = m_viewport.imageHeight;
    }

    UploadCompareTextures();
    // Keep viewport locked to entry dimensions
    m_viewport.imageWidth = m_savedImageWidth;
    m_viewport.imageHeight = m_savedImageHeight;
    SyncComparePanel();
    UpdateImageStatusText();
    m_needsRedraw = true;
}

void App::ExitCompare()
{
    m_compareMode = CompareMode::Off;
    m_compareBase = {};
    m_compareBlend = {};
    m_renderer.ClearCompareImage();

    // Restore viewport to the live image's actual dimensions
    if (m_image.IsLoaded())
    {
        m_viewport.imageWidth = static_cast<float>(m_image.width);
        m_viewport.imageHeight = static_cast<float>(m_image.height);
        m_renderer.UploadImage(m_image);
    }

    SyncComparePanel();
    UpdateImageStatusText();
    m_needsRedraw = true;
}

void App::SwapCompareSources()
{
    if (m_compareMode == CompareMode::Off)
        return;
    std::swap(m_compareBase, m_compareBlend);

    // Swap GPU textures directly (no re-upload needed)
    m_renderer.SwapCompareImages();

    // Update viewport to match new base dimensions.
    // Scale zoom to preserve visual size, adjust pan to keep center fixed.
    if (m_compareBase.image.IsLoaded())
    {
        float oldW = m_savedImageWidth;
        float oldH = m_savedImageHeight;
        float newW = static_cast<float>(m_compareBase.image.width);
        float newH = static_cast<float>(m_compareBase.image.height);

        float scale = oldW / newW;
        float oldZoom = m_viewport.zoom;
        m_viewport.zoom *= scale;

        float cx = m_viewport.panX + oldW * oldZoom * 0.5f;
        float cy = m_viewport.panY + oldH * oldZoom * 0.5f;
        m_viewport.panX = cx - newW * m_viewport.zoom * 0.5f;
        m_viewport.panY = cy - newH * m_viewport.zoom * 0.5f;

        m_savedImageWidth = newW;
        m_savedImageHeight = newH;
        m_viewport.imageWidth = m_savedImageWidth;
        m_viewport.imageHeight = m_savedImageHeight;
        m_renderer.SetQuadSize(m_savedImageWidth, m_savedImageHeight);
    }

    SyncComparePanel();
    UpdateImageStatusText();
    m_needsRedraw = true;
}

void App::SetCompareFocus(int source)
{
    m_compareFocused = source;

    // Switch to the focused source's tab so the layer list, active file,
    // and tab bar are all in sync.
    const CompareSource& src = (source == 1) ? m_compareBlend : m_compareBase;
    if (src.tabIndex >= 0 && src.tabIndex < static_cast<int>(m_openTabs.size()) &&
        src.tabIndex != m_activeTab)
    {
        // Temporarily exit compare to prevent SwitchToTab from auto-snapshotting
        // (we're just navigating, not changing the source)
        CompareMode savedMode = m_compareMode;
        m_compareMode = CompareMode::Off;
        SwitchToTab(src.tabIndex);
        m_compareMode = savedMode;
        UploadCompareTextures();
    }

    // Update the layer selection to match the source's snapshotted layer
    if (src.tabIndex == m_activeTab)
        m_activeLayer = src.layerIndex;

    SyncSidebar();
    SyncComparePanel();
}

void App::SnapshotFocusedSource()
{
    CompareSource& target = (m_compareFocused == 1) ? m_compareBlend : m_compareBase;
    target.tabIndex = m_activeTab;
    target.layerIndex = m_activeLayer;
    target.label = MakeSourceLabel();
    // Copy directly from m_image (the live working image) when available.
    // Avoid SaveTabState() which moves m_image into the tab, creating
    // confusing ownership state.
    if (m_image.IsLoaded())
        target.image = m_image;
}

void App::AdjustCompareAfterRemove(int removedIndex)
{
    if (m_compareMode == CompareMode::Off)
        return;

    auto adjust = [](int src, int removed) -> int
    {
        if (src == removed)
            return -1;
        if (src > removed)
            return src - 1;
        return src;
    };

    m_compareBase.tabIndex = adjust(m_compareBase.tabIndex, removedIndex);
    m_compareBlend.tabIndex = adjust(m_compareBlend.tabIndex, removedIndex);

    // Images are snapshotted, so they survive tab closure.
    // Only update the tab indices for focus navigation.
}

void App::UploadCompareTextures()
{
    // Upload base as primary image
    if (m_compareBase.image.IsLoaded())
        m_renderer.UploadImage(m_compareBase.image);

    // Upload blend as compare image
    if (m_compareBlend.image.IsLoaded())
        m_renderer.UploadCompareImage(m_compareBlend.image);

    // Viewport dimensions always track the base (left) image.
    // When base dimensions change: scale zoom to preserve visual size,
    // then adjust pan to keep the screen center fixed.
    if (m_compareBase.image.IsLoaded())
    {
        float newW = static_cast<float>(m_compareBase.image.width);
        float newH = static_cast<float>(m_compareBase.image.height);
        if (newW != m_savedImageWidth || newH != m_savedImageHeight)
        {
            // Scale zoom so the image occupies the same screen area
            float scale = m_savedImageWidth / newW;
            float oldZoom = m_viewport.zoom;
            m_viewport.zoom *= scale;

            // Adjust pan to keep center fixed at the new zoom
            float cx = m_viewport.panX + m_savedImageWidth * oldZoom * 0.5f;
            float cy = m_viewport.panY + m_savedImageHeight * oldZoom * 0.5f;
            m_viewport.panX = cx - newW * m_viewport.zoom * 0.5f;
            m_viewport.panY = cy - newH * m_viewport.zoom * 0.5f;

            m_savedImageWidth = newW;
            m_savedImageHeight = newH;
        }
    }
    m_viewport.imageWidth = m_savedImageWidth;
    m_viewport.imageHeight = m_savedImageHeight;

    // Clamp zoom to the valid range for the new dimensions so the first
    // scroll interaction doesn't jump due to a deferred clamp.
    m_viewport.zoom = (std::max)(m_viewport.MinZoom(),
                                 (std::min)(m_viewport.zoom, m_viewport.MaxZoom()));
    m_renderer.SetQuadSize(m_savedImageWidth, m_savedImageHeight);
}

size_t App::SequenceCacheBudget()
{
    // Manual override (MB) for testing/tuning, e.g. EXRAY_CACHE_MB=1024.
    if (const char* env = std::getenv("EXRAY_CACHE_MB"))
    {
        long mb = std::atol(env);
        if (mb > 0)
            return static_cast<size_t>(mb) * 1024 * 1024;
    }

    // Cap the prefetch cache at a fraction of physical RAM, with a hard ceiling.
    // A 1080p half-float frame is ~16 MB, so 6 GB holds ~370 frames.
    constexpr size_t kHardCeiling = size_t(6) * 1024 * 1024 * 1024;
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem))
    {
        size_t half = static_cast<size_t>(mem.ullTotalPhys / 2);
        return (std::min)(half, kHardCeiling);
    }
    return size_t(2) * 1024 * 1024 * 1024;
}

std::wstring App::SequenceCacheId(const SequenceInfo& seq)
{
    return seq.prefix + L"\x1f" + seq.suffix;
}

void App::EnsureSequenceCache()
{
    if (!IsSequenceTab())
    {
        // Stop prefetching but keep resident frames (LRU evicts under pressure),
        // so returning to a sequence tab finds it still warm.
        m_seqCache.Deactivate();
        m_activeCacheId.clear();
        return;
    }

    const SequenceInfo& seq = m_openTabs[m_activeTab].sequence;
    std::wstring id = SequenceCacheId(seq);

    // A non-default layer gets its own cache entry, prefetched as that layer.
    const ExrLayer* layerPtr = nullptr;
    if (m_activeLayer > 0 && m_activeLayer < static_cast<int>(m_layerInfo.layers.size()))
    {
        id += L"|L" + std::to_wstring(m_activeLayer);
        layerPtr = &m_layerInfo.layers[m_activeLayer];
    }

    if (m_activeCacheId != id)
    {
        m_seqCache.SetBudget(SequenceCacheBudget());
        std::vector<std::wstring> paths;
        paths.reserve(seq.frames.size());
        for (const SequenceFrame& f : seq.frames)
            paths.push_back(f.path);
        m_seqCache.Activate(id, std::move(paths), layerPtr); // keeps this seq+layer's warm frames if cached
        m_activeCacheId = id;
    }
    m_seqCache.SetFocus(seq.currentIndex);
}

void App::NavigateSequence(int delta)
{
    if (!IsSequenceTab())
        return;

    auto& seq = m_openTabs[m_activeTab].sequence;
    int newIndex = seq.currentIndex + delta;
    newIndex = (std::max)(0, (std::min)(newIndex, seq.FrameCount() - 1));
    if (newIndex == seq.currentIndex)
        return;

    seq.currentIndex = newIndex;
    const std::wstring framePath = seq.CurrentPath();

    // Update the tab's path to the new frame
    m_openTabs[m_activeTab].path = framePath;

    const int64_t tStart = StartupTiming::Now();
    m_lastNavTick = tStart; // defer the expensive histogram until navigation settles

    // Drive the prefetch worker toward the new position and try the cache first.
    EnsureSequenceCache();
    m_seqCache.SetFocus(newIndex);

    // Load the new frame. The cache is configured for the active layer (default
    // RGBA or e.g. Z depth) by EnsureSequenceCache, so a hit is just a GPU upload
    // for either; a miss decodes the right thing synchronously and seeds the cache.
    bool loaded;
    bool cacheHit = false;
    const bool useLayer = (m_activeLayer > 0 && m_activeLayer < static_cast<int>(m_layerInfo.layers.size()));

    ImageData cached;
    if (m_seqCache.TryGet(newIndex, cached))
    {
        ApplyLoadedImage(std::move(cached), framePath, /*isSequenceStep=*/true);
        loaded = true;
        cacheHit = true;
    }
    else if (useLayer)
    {
        std::string err;
        ImageData img;
        if (ImageLoader::LoadEXRLayer(framePath, m_layerInfo.layers[m_activeLayer], img, err))
        {
            ApplyLoadedImage(std::move(img), framePath, /*isSequenceStep=*/true);
            loaded = true;
            m_seqCache.Put(newIndex, m_image);
        }
        else
        {
            // Layer missing in this frame → fall back to default RGBA (no Put;
            // the active cache is the layer cache).
            m_activeLayer = 0;
            loaded = LoadFile(framePath, /*isSequenceStep=*/true);
        }
    }
    else
    {
        loaded = LoadFile(framePath, /*isSequenceStep=*/true);
        if (loaded)
            m_seqCache.Put(newIndex, m_image);
    }
    const int64_t tApplied = StartupTiming::Now();

    if (loaded)
    {
        // Update tab title in place (no remove/add to avoid layout flicker)
        std::wstring title = FormatSequenceTabTitle(framePath, seq);
        m_window.UpdateTabTitle(m_activeTab, title.c_str());

        // Update window title
        wchar_t windowTitle[MAX_PATH + 16];
        swprintf_s(windowTitle, L"EXRay - %s", framePath.c_str());
        m_window.SetTitle(windowTitle);

        // In compare mode, update the focused source with the new frame
        if (m_compareMode != CompareMode::Off)
        {
            SnapshotFocusedSource();
            UploadCompareTextures();
            SyncComparePanel();
        }

        // Render immediately (within this input handler) so the frame appears now,
        // rather than after a message-loop round trip. Then record click→present
        // latency telemetry.
        Render();
        m_needsRedraw = false;

        const int64_t tEnd = StartupTiming::Now();
        m_lastAdvanceMs = m_timing->MsBetween(tStart, tEnd);

        wchar_t dbg[160];
        swprintf_s(dbg, L"[EXRay] advance %.1f ms (%s: load+upload %.1f, draw %.1f)\n", m_lastAdvanceMs,
                   cacheHit ? L"cache hit" : L"cache MISS", m_timing->MsBetween(tStart, tApplied),
                   m_timing->MsBetween(tApplied, tEnd));
        OutputDebugStringW(dbg);

        UpdateImageStatusText(); // refresh status bar to show the new latency

        if (m_playBench)
            RecordPlayBenchSample(m_lastAdvanceMs, cacheHit, tEnd);
    }

    SyncSequencePanel();
}

void App::RecomputeHistogramIfDirty()
{
    if (!m_histogramDirty)
        return;
    m_histogramDirty = false;
    if (m_image.IsLoaded())
    {
        m_histogram = HistogramComputer::Compute(m_image);
        SyncSidebar();
        m_needsRedraw = true;
    }
}

void App::JumpToFrame(int frameNumber)
{
    if (!IsSequenceTab())
        return;

    auto& seq = m_openTabs[m_activeTab].sequence;
    int newIndex = seq.FindNearest(frameNumber);
    if (newIndex < 0 || newIndex == seq.currentIndex)
        return;

    int delta = newIndex - seq.currentIndex;
    NavigateSequence(delta);
}

void App::SyncSequencePanel()
{
    // TODO: wire to sidebar once sequence panel is built
    Sidebar& sb = m_window.GetSidebar();
    if (IsSequenceTab())
    {
        auto& seq = m_openTabs[m_activeTab].sequence;
        sb.SetSequenceState(seq.currentIndex, seq.CurrentFrameNumber(), seq.FrameCount());
    }
    else
    {
        sb.ClearSequenceState();
    }
}

std::wstring App::MakeSourceLabel() const
{
    if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_openTabs.size()))
        return L"(none)";

    const auto& tab = m_openTabs[m_activeTab];
    std::wstring label = ExtractFilename(tab.path);

    // Append layer label (same format as the sidebar layer list)
    int layer = m_activeLayer;
    if (layer >= 0 && layer < static_cast<int>(m_layerInfo.layers.size()) &&
        m_layerInfo.layers.size() > 1)
    {
        label += L" - ";
        label += FormatLayerLabel(m_layerInfo.layers[layer]);
    }
    return label;
}

void App::SyncComparePanel()
{
    Sidebar& sb = m_window.GetSidebar();
    if (m_compareMode == CompareMode::Off)
    {
        sb.ClearCompareState();
        return;
    }

    sb.SetCompareState(static_cast<int>(m_compareMode),
                       m_compareBase.label.c_str(), m_compareBlend.label.c_str(),
                       m_compareFocused);
}

void App::UpdateImageStatusText()
{
    // Compare mode: sidebar handles detailed state, status bar just shows mode
    if (m_compareMode != CompareMode::Off)
    {
        static const wchar_t* kModeNames[] = {L"", L"Split", L"Difference", L"Add", L"Over"};
        int idx = static_cast<int>(m_compareMode);
        if (idx >= 1 && idx <= 4)
            m_window.SetStatusText(2, (std::wstring(L" Compare: ") + kModeNames[idx]).c_str());
        return;
    }

    if (!m_image.IsLoaded())
        return;

    wchar_t infoBuf[256];
    if (m_viewport.isHDR)
    {
        swprintf_s(infoBuf, L" %d x %d | EV %+.2f | HDR @ %.0f nits", m_image.width, m_image.height,
                   m_viewport.exposure, m_viewport.displayMaxNits);
    }
    else
    {
        float displayGamma = 1.0f / m_viewport.gamma;
        swprintf_s(infoBuf, L" %d x %d | EV %+.2f | SDR | Gamma %.1f", m_image.width, m_image.height,
                   m_viewport.exposure, displayGamma);
    }

    // Append color space name when source primaries differ from Rec. 709
    if (!m_image.colorSpace.empty())
    {
        wchar_t csName[64];
        MultiByteToWideChar(CP_UTF8, 0, m_image.colorSpace.c_str(), -1, csName, 64);
        wcscat_s(infoBuf, L" | ");
        wcscat_s(infoBuf, csName);
    }

    // Frame-advance latency telemetry (sequences only)
    if (IsSequenceTab() && m_lastAdvanceMs >= 0.0)
    {
        wchar_t advBuf[32];
        swprintf_s(advBuf, L" | adv %.1f ms", m_lastAdvanceMs);
        wcscat_s(infoBuf, advBuf);
    }

    m_window.SetStatusText(2, infoBuf);
}

void App::SyncSidebar()
{
    bool hasImage = m_renderer.HasImage();
    Sidebar& sb = m_window.GetSidebar();
    sb.SetEnabled(hasImage);
    m_window.EnableImageMenuItems(hasImage);
    sb.SetHistogramData(m_histogram, m_histogramChannel);
    sb.SetExposureGamma(m_viewport.exposure, m_viewport.gamma, m_viewport.isHDR);
    sb.SetLayers(m_layerInfo, m_activeLayer);
}

void App::OpenFileDialog()
{
    wchar_t filePath[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_window.GetHwnd();
    ofn.lpstrFilter = L"EXR Files (*.exr)\0*.exr\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"exr";

    if (GetOpenFileNameW(&ofn))
    {
        OpenFile(filePath);
    }
}

std::wstring App::GetRecentFilesPath()
{
    wchar_t* appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)))
        return {};
    std::wstring dir = std::wstring(appData) + L"\\EXRay";
    CoTaskMemFree(appData);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\recent.txt";
}

void App::LoadRecentFiles()
{
    std::wstring path = GetRecentFilesPath();
    if (path.empty())
        return;

    FILE* f = _wfopen(path.c_str(), L"r, ccs=UTF-8");
    if (!f)
        return;

    wchar_t line[MAX_PATH + 2];
    bool firstLine = true;
    while (fgetws(line, _countof(line), f))
    {
        wchar_t* start = line;
        // Skip BOM character if CRT didn't consume it
        if (firstLine && start[0] == L'\xFEFF')
            start++;
        firstLine = false;

        // Strip trailing newline/carriage return and stray whitespace
        size_t len = wcslen(start);
        while (len > 0 &&
               (start[len - 1] == L'\n' || start[len - 1] == L'\r' || start[len - 1] == L' ' || start[len - 1] == L'\t'))
            start[--len] = L'\0';
        if (len > 0)
            m_recentFiles.push_back(start);
    }
    fclose(f);

    m_window.UpdateRecentMenu(m_recentFiles);
}

void App::SaveRecentFiles()
{
    std::wstring path = GetRecentFilesPath();
    if (path.empty())
        return;

    // Write to a temp file first, then atomically rename.
    // This avoids data loss if the target file is briefly locked
    // by Windows Search Indexer or antivirus during the write.
    std::wstring tmpPath = path + L".tmp";

    FILE* f = _wfopen(tmpPath.c_str(), L"w, ccs=UTF-8");
    if (!f)
        return;

    for (const auto& entry : m_recentFiles)
        fwprintf(f, L"%s\n", entry.c_str());
    fclose(f);

    // Retry rename in case target is briefly locked
    for (int i = 0; i < 3; i++)
    {
        if (MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            return;
        Sleep(50);
    }
    DeleteFileW(tmpPath.c_str());
}

std::wstring App::GetPreferencesPath()
{
    wchar_t* appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)))
        return {};
    std::wstring dir = std::wstring(appData) + L"\\EXRay";
    CoTaskMemFree(appData);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\prefs.txt";
}

void App::LoadPreferences()
{
    std::wstring path = GetPreferencesPath();
    if (path.empty())
        return;

    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f)
        return;

    char line[128];
    while (fgets(line, sizeof(line), f))
    {
        int val;
        if (sscanf(line, "histogramChannel=%d", &val) == 1)
            m_histogramChannel = (std::max)(0, (std::min)(val, 4));
        else if (sscanf(line, "showGrid=%d", &val) == 1)
            m_showGrid = (val != 0);
        // showSidebar is no longer a preference (sidebar always visible)
    }
    fclose(f);
}

void App::SavePreferences()
{
    std::wstring path = GetPreferencesPath();
    if (path.empty())
        return;

    FILE* f = _wfopen(path.c_str(), L"w");
    if (!f)
        return;

    fprintf(f, "histogramChannel=%d\n", m_histogramChannel);
    fprintf(f, "showGrid=%d\n", m_showGrid ? 1 : 0);
    fclose(f);
}

void App::AddToRecentFiles(std::wstring path)
{
    // Remove if already present (case-insensitive, since Windows paths are case-insensitive)
    // Note: path is taken by value because callers may pass m_recentFiles[i],
    // and remove_if would corrupt a reference into the vector being modified.
    m_recentFiles.erase(std::remove_if(m_recentFiles.begin(), m_recentFiles.end(), [&path](const std::wstring& entry)
                                       { return _wcsicmp(entry.c_str(), path.c_str()) == 0; }),
                        m_recentFiles.end());

    m_recentFiles.insert(m_recentFiles.begin(), path);

    if (static_cast<int>(m_recentFiles.size()) > kMaxRecentFiles)
        m_recentFiles.resize(kMaxRecentFiles);

    m_window.UpdateRecentMenu(m_recentFiles);
    SaveRecentFiles();
}
