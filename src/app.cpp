// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "app.h"

#include "resource.h"

#include <algorithm>
#include <shlobj.h>

static const wchar_t* ExtractFilename(const std::wstring& path)
{
    const wchar_t* fn = wcsrchr(path.c_str(), L'\\');
    if (!fn)
        fn = wcsrchr(path.c_str(), L'/');
    return fn ? fn + 1 : path.c_str();
}

bool App::Initialize(HINSTANCE hInstance, int nCmdShow, LPWSTR cmdLine, StartupTiming& timing, bool smokeTest)
{
    m_hInstance = hInstance;
    m_timing = &timing;
    m_smokeTest = smokeTest;

    // If a file path was passed on the command line, start loading immediately
    // on a background thread — before window/D3D11 init.
    std::wstring cmdLinePath;
    if (cmdLine && cmdLine[0] != L'\0')
    {
        cmdLinePath = cmdLine;
        if (cmdLinePath.size() >= 2 && cmdLinePath.front() == L'"' && cmdLinePath.back() == L'"')
        {
            cmdLinePath = cmdLinePath.substr(1, cmdLinePath.size() - 2);
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
        if (ctrl)
            m_viewport.ZoomAt(static_cast<float>(x), static_cast<float>(y), static_cast<float>(delta));
        else if (shift)
            m_viewport.Pan(static_cast<float>(delta) * 0.8f, 0.0f);  // Shift+scroll → pan X
        else
            m_viewport.Pan(0.0f, static_cast<float>(delta) * 0.8f);   // scroll → pan Y
        m_needsRedraw = true;
    };

    m_window.onMouseHWheel = [this](int /*x*/, int /*y*/, int delta)
    {
        m_viewport.Pan(static_cast<float>(-delta) * 0.8f, 0.0f); // H-scroll → pan X
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
            const float* px = m_image.PixelAt(ix, iy);
            wchar_t coordBuf[32];
            swprintf_s(coordBuf, L" (%d, %d)", ix, iy);
            m_window.SetStatusText(0, coordBuf);

            wchar_t rgbaBuf[128];
            swprintf_s(rgbaBuf, L" R: %.4f  G: %.4f  B: %.4f  A: %.4f", px[0], px[1], px[2], px[3]);
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
        else if ((GetKeyState(VK_SHIFT) & 0x8000) &&
                 (vk == VK_OEM_3 || (vk >= '1' && vk <= '4')))
        {
            m_displayMode = (vk == VK_OEM_3) ? 0 : (vk - '0');
            m_window.UpdateMenuChecks(m_showGrid, m_displayMode);
            m_needsRedraw = true;
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
            if (m_window.IsFullscreen())
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
    sidebar.onLayerSelect = [this](int layerIndex)
    {
        LoadLayer(layerIndex);
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

        const float* px = m_image.PixelAt(ix, iy);

        HMENU popup = CreatePopupMenu();
        wchar_t label[256];
        swprintf_s(label, L"Copy Pixel Value  (%.4f, %.4f, %.4f, %.4f)", px[0], px[1], px[2], px[3]);
        AppendMenuW(popup, MF_STRING, 1, label);

        int cmd = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, m_window.GetHwnd(),
                                 nullptr);
        DestroyMenu(popup);

        if (cmd == 1)
        {
            wchar_t clipText[128];
            swprintf_s(clipText, L"R: %.4f  G: %.4f  B: %.4f  A: %.4f", px[0], px[1], px[2], px[3]);

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
    
            m_timing->textureUploaded = StartupTiming::Now();

            m_viewport.imageWidth = static_cast<float>(m_image.width);
            m_viewport.imageHeight = static_cast<float>(m_image.height);
            m_viewport.exposure = m_histogram.autoExposure;
            m_viewport.FitToWindow();

            // Scan layers for initial file
            std::string layerError;
            ImageLoader::ScanLayers(cmdLinePath, m_layerInfo, layerError);
            m_activeLayer = 0;

            m_openTabs.push_back({cmdLinePath, m_viewport.exposure, m_viewport.zoom, m_viewport.panX, m_viewport.panY, m_viewport.gamma, {}, {}, m_layerInfo, m_activeLayer});
            m_activeTab = 0;
            m_window.AddTab(0, ExtractFilename(cmdLinePath));
            m_window.SetActiveTab(0);

            m_viewport.FitToWindow();

            wchar_t title[MAX_PATH + 16];
            swprintf_s(title, L"EXRay - %s", cmdLinePath.c_str());
            m_window.SetTitle(title);

            SyncSidebar();
            UpdateImageStatusText();
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
            m_updateResult =
                UpdateChecker::Check(EXRAY_VERSION_MAJOR, EXRAY_VERSION_MINOR, EXRAY_VERSION_PATCH);
            m_updateCheckComplete = true;
            PostMessageW(hwnd, WM_APP + 1, 0, 0);
        });
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

    MSG msg = {};
    while (true)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                if (m_preloadThread.joinable())
                    m_preloadThread.join();
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

        // Collect finished preloads and start the next one
        if (m_preloadComplete)
        {
            FinishPreload();
            StartPreload();
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
        else
        {
            WaitMessage();
        }
    }
}

void App::Render()
{
    m_renderer.BeginFrame(0.18f, 0.18f, 0.18f);

    if (m_renderer.HasImage())
    {
        ViewportCB vp = m_viewport.ToViewportCB();
        vp.showGrid = m_showGrid ? 1 : 0;
        vp.displayMode = m_displayMode;
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
        m_displayMode = commandId - IDM_VIEW_CHANNEL_RGB;
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

    case IDM_HELP_ABOUT:
    {
        HICON bigIcon = static_cast<HICON>(
            LoadImageW(m_hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR));

        std::wstring content = L"Version " EXRAY_VERSION_WSTR L"\n"
                               EXRAY_COPYRIGHT_W L"\n\n"
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
            m_renderer.RenderImage(vp);
        }
        m_renderer.EndFrame(false);
        m_needsRedraw = false;
    }
}

bool App::LoadFile(const std::wstring& path)
{
    std::string errorMsg;
    ImageData newImage;

    if (ImageLoader::LoadEXR(path, newImage, errorMsg))
    {
        m_image = std::move(newImage);
        m_renderer.UploadImage(m_image);
        m_histogram = HistogramComputer::Compute(m_image);

        // Scan layers (metadata only)
        std::string layerError;
        ImageLoader::ScanLayers(path, m_layerInfo, layerError);
        m_activeLayer = 0;

        m_viewport.imageWidth = static_cast<float>(m_image.width);
        m_viewport.imageHeight = static_cast<float>(m_image.height);
        m_viewport.exposure = m_histogram.autoExposure;
        m_viewport.FitToWindow();

        wchar_t title[MAX_PATH + 16];
        swprintf_s(title, L"EXRay - %s", path.c_str());
        m_window.SetTitle(title);

        SyncSidebar();
        UpdateImageStatusText();
        m_needsRedraw = true;
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
        bool isMipSwitch = oldLayer.numMipLevels > 1 && newLayer.numMipLevels > 1
                           && oldLayer.name == newLayer.name
                           && oldLayer.partIndex == newLayer.partIndex;

        float oldWidth = m_viewport.imageWidth;

        m_image = std::move(newImage);
        m_renderer.UploadImage(m_image);
        m_histogram = HistogramComputer::Compute(m_image);
        m_activeLayer = layerIndex;

        m_viewport.imageWidth = static_cast<float>(m_image.width);
        m_viewport.imageHeight = static_cast<float>(m_image.height);

        if (isMipSwitch && oldWidth > 0.0f)
            m_viewport.zoom = (std::min)(m_viewport.zoom * oldWidth / m_viewport.imageWidth, m_viewport.MaxZoom());

        SyncSidebar();
        UpdateImageStatusText();
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

void App::OpenFile(const std::wstring& path)
{
    // Check if file is already open (case-insensitive)
    for (int i = 0; i < static_cast<int>(m_openTabs.size()); ++i)
    {
        if (_wcsicmp(m_openTabs[i].path.c_str(), path.c_str()) == 0)
        {
            SwitchToTab(i);
            return;
        }
    }

    // Collect any finished preload before modifying tabs
    if (m_preloadComplete)
        FinishPreload();

    SaveTabState();

    if (!LoadFile(path))
        return;

    m_openTabs.push_back({path, m_viewport.exposure, m_viewport.zoom, m_viewport.panX, m_viewport.panY, m_viewport.gamma, {}, {}, m_layerInfo, m_activeLayer});
    int newIndex = static_cast<int>(m_openTabs.size()) - 1;
    m_window.AddTab(newIndex, ExtractFilename(path));
    m_activeTab = newIndex;
    m_window.SetActiveTab(m_activeTab);
    AddToRecentFiles(path);
    EvictDistantTabs();
    StartPreload();
}

void App::SwitchToTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_openTabs.size()))
        return;
    if (index == m_activeTab)
        return;

    // Collect any finished preload before switching
    if (m_preloadComplete)
        FinishPreload();

    SaveTabState();
    m_activeTab = index;
    m_window.SetActiveTab(index);

    if (m_openTabs[index].image.IsLoaded())
    {
        // Cache hit — use cached data
        m_image = std::move(m_openTabs[index].image);
        m_histogram = std::move(m_openTabs[index].histogram);
        m_layerInfo = m_openTabs[index].layerInfo;
        m_activeLayer = m_openTabs[index].activeLayer;
        m_renderer.UploadImage(m_image);

        m_viewport.imageWidth = static_cast<float>(m_image.width);
        m_viewport.imageHeight = static_cast<float>(m_image.height);
        m_viewport.exposure = m_openTabs[index].exposure;
        m_viewport.zoom = m_openTabs[index].zoom;
        m_viewport.panX = m_openTabs[index].panX;
        m_viewport.panY = m_openTabs[index].panY;
        m_viewport.gamma = m_openTabs[index].gamma;

        wchar_t title[MAX_PATH + 16];
        swprintf_s(title, L"EXRay - %s", m_openTabs[index].path.c_str());
        m_window.SetTitle(title);
    }
    else
    {
        // Cache miss — read from disk
        LoadFile(m_openTabs[index].path);
        m_viewport.exposure = m_openTabs[index].exposure;
        m_viewport.zoom = m_openTabs[index].zoom;
        m_viewport.panX = m_openTabs[index].panX;
        m_viewport.panY = m_openTabs[index].panY;
        m_viewport.gamma = m_openTabs[index].gamma;
    }

    EvictDistantTabs();
    StartPreload();
    SyncSidebar();
    UpdateImageStatusText();
    m_needsRedraw = true;
}

void App::CloseCurrentTab()
{
    if (m_activeTab < 0 || m_openTabs.empty())
    {
        DestroyWindow(m_window.GetHwnd());
        return;
    }

    // Invalidate preload — tab indices are shifting
    m_preloadIndex = -1;

    int closingIndex = m_activeTab;
    m_openTabs.erase(m_openTabs.begin() + closingIndex);
    m_window.RemoveTab(closingIndex);

    if (m_openTabs.empty())
    {
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

    // Invalidate preload — tab indices are shifting
    m_preloadIndex = -1;

    m_openTabs.erase(m_openTabs.begin() + index);
    m_window.RemoveTab(index);

    // Adjust active tab index if it was after the removed tab
    if (m_activeTab > index)
        m_activeTab--;

    m_window.SetActiveTab(m_activeTab);
    StartPreload();
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
        if (m_image.IsLoaded())
        {
            m_openTabs[m_activeTab].image = std::move(m_image);
            m_openTabs[m_activeTab].histogram = std::move(m_histogram);
        }
    }
}

void App::StartPreload()
{
    // Don't start if already preloading
    if (m_preloadThread.joinable())
        return;
    if (m_openTabs.size() <= 1)
        return;

    int count = static_cast<int>(m_openTabs.size());
    int next = m_activeTab + 1;
    int prev = m_activeTab - 1;

    // Prefer preloading the next tab, then previous
    int target = -1;
    if (next < count && !m_openTabs[next].image.IsLoaded())
        target = next;
    else if (prev >= 0 && !m_openTabs[prev].image.IsLoaded())
        target = prev;

    if (target < 0)
        return;

    m_preloadIndex = target;
    m_preloadComplete = false;
    std::wstring path = m_openTabs[target].path;
    HWND hwnd = m_window.GetHwnd();

    m_preloadThread = std::thread(
        [this, path, hwnd]()
        {
            std::string error;
            ImageLoader::LoadEXR(path, m_preloadImage, error);
            if (m_preloadImage.IsLoaded())
                m_preloadHistogram = HistogramComputer::Compute(m_preloadImage);
            m_preloadComplete = true;
            PostMessageW(hwnd, WM_APP, 0, 0);
        });
}

void App::FinishPreload()
{
    if (!m_preloadThread.joinable())
        return;
    m_preloadThread.join();

    if (m_preloadIndex >= 0 && m_preloadIndex < static_cast<int>(m_openTabs.size()) && m_preloadImage.IsLoaded())
    {
        m_openTabs[m_preloadIndex].image = std::move(m_preloadImage);
        m_openTabs[m_preloadIndex].histogram = std::move(m_preloadHistogram);
    }

    m_preloadImage = ImageData{};
    m_preloadHistogram = HistogramData{};
    m_preloadIndex = -1;
    m_preloadComplete = false;
}

void App::EvictDistantTabs()
{
    for (int i = 0; i < static_cast<int>(m_openTabs.size()); i++)
    {
        if (i >= m_activeTab - 1 && i <= m_activeTab + 1)
            continue;
        if (i == m_preloadIndex)
            continue;
        m_openTabs[i].image = ImageData{};
        m_openTabs[i].histogram = HistogramData{};
    }
}

void App::UpdateImageStatusText()
{
    if (!m_image.IsLoaded())
        return;

    wchar_t infoBuf[192];
    if (m_viewport.isHDR)
    {
        swprintf_s(infoBuf, L" %d x %d | EV %+.2f | HDR @ %.0f nits", m_image.width, m_image.height, m_viewport.exposure, m_viewport.displayMaxNits);
    }
    else
    {
        float displayGamma = 1.0f / m_viewport.gamma;
        swprintf_s(infoBuf, L" %d x %d | EV %+.2f | SDR | Gamma %.1f", m_image.width, m_image.height,
                   m_viewport.exposure, displayGamma);
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

        // Strip trailing newline/carriage return
        size_t len = wcslen(start);
        while (len > 0 && (start[len - 1] == L'\n' || start[len - 1] == L'\r'))
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
