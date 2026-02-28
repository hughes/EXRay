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

bool App::Initialize(HINSTANCE hInstance, int nCmdShow, LPWSTR cmdLine, StartupTiming& timing)
{
    m_hInstance = hInstance;
    m_timing = &timing;

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
        MessageBoxW(nullptr, L"Failed to create window.", L"EXRay", MB_ICONERROR);
        return false;
    }

    // Wire up input callbacks
    m_window.onMouseWheel = [this](int x, int y, int delta, bool ctrl)
    {
        if (ctrl)
        {
            m_viewport.AdjustExposure(delta > 0 ? 0.25f : -0.25f);
            UpdateImageStatusText();
        }
        else
        {
            m_viewport.ZoomAt(static_cast<float>(x), static_cast<float>(y), static_cast<float>(delta));
        }
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
            UpdateImageStatusText();
            m_needsRedraw = true;
        }
        else if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT)
        {
            m_viewport.AdjustExposure(-0.25f);
            UpdateImageStatusText();
            m_needsRedraw = true;
        }
        else if ((vk == VK_OEM_6 || vk == VK_OEM_4) && !m_viewport.isHDR) // [/] — gamma (SDR only)
        {
            m_viewport.AdjustGamma(vk == VK_OEM_6 ? 0.05f : -0.05f);
            UpdateImageStatusText();
            m_needsRedraw = true;
        }
        else if (vk == 'H')
        {
            m_showHistogram = !m_showHistogram;
            SavePreferences();
            m_window.UpdateMenuChecks(m_showHistogram, m_histogramChannel, m_showGrid);
            m_needsRedraw = true;
        }
        else if (vk == 'C')
        {
            if (m_showHistogram)
            {
                m_histogramChannel = (m_histogramChannel + 1) % 5;
                SavePreferences();
                m_window.UpdateMenuChecks(m_showHistogram, m_histogramChannel, m_showGrid);
                m_needsRedraw = true;
            }
        }
        else if (vk == 'G')
        {
            m_showGrid = !m_showGrid;
            SavePreferences();
            m_window.UpdateMenuChecks(m_showHistogram, m_histogramChannel, m_showGrid);
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
    m_window.onTabChange = [this](int index) { SwitchToTab(index); };

    m_timing->windowVisible = StartupTiming::Now();

    if (!m_renderer.Initialize(m_window.GetRenderHwnd()))
    {
        MessageBoxW(nullptr, L"Failed to initialize D3D11.", L"EXRay", MB_ICONERROR);
        return false;
    }

    m_timing->d3dReady = StartupTiming::Now();

    LoadRecentFiles();
    LoadPreferences();
    m_window.UpdateMenuChecks(m_showHistogram, m_histogramChannel, m_showGrid);

    // Propagate HDR state to viewport
    if (m_renderer.IsHDREnabled())
    {
        m_viewport.isHDR = true;
        const auto& hdrInfo = m_renderer.GetHDRInfo();
        m_viewport.displayMaxNits = hdrInfo.maxLuminance;
    }

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
            m_renderer.UploadHistogram(m_histogram);
            m_timing->textureUploaded = StartupTiming::Now();

            m_viewport.imageWidth = static_cast<float>(m_image.width);
            m_viewport.imageHeight = static_cast<float>(m_image.height);
            m_viewport.FitToWindow();

            m_openTabs.push_back({cmdLinePath, 0.0f});
            m_activeTab = 0;
            m_window.AddTab(0, ExtractFilename(cmdLinePath));
            m_window.SetActiveTab(0);

            wchar_t title[MAX_PATH + 16];
            swprintf_s(title, L"EXRay - %s", cmdLinePath.c_str());
            m_window.SetTitle(title);

            UpdateImageStatusText();
            AddToRecentFiles(cmdLinePath);
        }
        else if (!m_loadError.empty())
        {
            int len = MultiByteToWideChar(CP_UTF8, 0, m_loadError.c_str(), -1, nullptr, 0);
            std::wstring wideError(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, m_loadError.c_str(), -1, wideError.data(), len);
            MessageBoxW(m_window.GetHwnd(), wideError.c_str(), L"EXRay - Error", MB_ICONERROR);
        }
    }

    return true;
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

    MSG msg = {};
    while (true)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return static_cast<int>(msg.wParam);
            if (!TranslateAcceleratorW(m_window.GetHwnd(), m_window.GetAccelTable(), &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
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
        m_renderer.RenderImage(vp);

        if (m_showHistogram && m_histogram.isValid)
        {
            m_renderer.RenderHistogram(BuildHistogramCB());
        }
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
            m_openTabs[m_activeTab].exposure = 0.0f;
            LoadFile(m_openTabs[m_activeTab].path);
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

    case IDM_VIEW_HISTOGRAM:
        m_showHistogram = !m_showHistogram;
        SavePreferences();
        m_window.UpdateMenuChecks(m_showHistogram, m_histogramChannel, m_showGrid);
        m_needsRedraw = true;
        break;

    case IDM_VIEW_GRID:
        m_showGrid = !m_showGrid;
        SavePreferences();
        m_window.UpdateMenuChecks(m_showHistogram, m_histogramChannel, m_showGrid);
        m_needsRedraw = true;
        break;

    case IDM_VIEW_CHAN_LUM:
    case IDM_VIEW_CHAN_RED:
    case IDM_VIEW_CHAN_GREEN:
    case IDM_VIEW_CHAN_BLUE:
    case IDM_VIEW_CHAN_ALL:
        m_histogramChannel = commandId - IDM_VIEW_CHAN_LUM;
        SavePreferences();
        m_window.UpdateMenuChecks(m_showHistogram, m_histogramChannel, m_showGrid);
        m_needsRedraw = true;
        break;

    case IDM_VIEW_FULLSCREEN:
        m_window.ToggleFullscreen();
        break;

    case IDM_HELP_ABOUT:
        MessageBoxW(m_window.GetHwnd(), L"EXRay - The EXR Viewer\nVersion 0.1.0", L"About EXRay",
                    MB_OK | MB_ICONINFORMATION);
        break;
    }
}

void App::OnResize(int width, int height)
{
    m_renderer.Resize(width, height);
    m_viewport.clientWidth = static_cast<float>(width);
    m_viewport.clientHeight = static_cast<float>(height);
    if (m_image.IsLoaded())
        m_viewport.FitToWindow();
    m_needsRedraw = true;
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
        m_renderer.UploadHistogram(m_histogram);

        m_viewport.imageWidth = static_cast<float>(m_image.width);
        m_viewport.imageHeight = static_cast<float>(m_image.height);
        m_viewport.exposure = 0.0f;
        m_viewport.FitToWindow();

        wchar_t title[MAX_PATH + 16];
        swprintf_s(title, L"EXRay - %s", path.c_str());
        m_window.SetTitle(title);

        UpdateImageStatusText();
        m_needsRedraw = true;
        return true;
    }
    else
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, errorMsg.c_str(), -1, nullptr, 0);
        std::wstring wideError(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, errorMsg.c_str(), -1, wideError.data(), len);
        MessageBoxW(m_window.GetHwnd(), wideError.c_str(), L"EXRay - Error", MB_ICONERROR);
        return false;
    }
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

    SaveTabState();

    if (!LoadFile(path))
        return;

    m_openTabs.push_back({path, 0.0f});
    int newIndex = static_cast<int>(m_openTabs.size()) - 1;
    m_window.AddTab(newIndex, ExtractFilename(path));
    m_activeTab = newIndex;
    m_window.SetActiveTab(m_activeTab);
    AddToRecentFiles(path);
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
    LoadFile(m_openTabs[index].path);
    m_viewport.exposure = m_openTabs[index].exposure;
    UpdateImageStatusText();
    m_needsRedraw = true;
}

void App::CloseCurrentTab()
{
    if (m_activeTab < 0 || m_openTabs.empty())
        return;

    int closingIndex = m_activeTab;
    m_openTabs.erase(m_openTabs.begin() + closingIndex);
    m_window.RemoveTab(closingIndex);

    if (m_openTabs.empty())
    {
        m_activeTab = -1;
        m_image = ImageData{};
        m_renderer.UploadImage(m_image);
        m_histogram = HistogramData{};
        m_window.SetTitle(L"EXRay");
        m_window.SetStatusText(0, L"");
        m_window.SetStatusText(1, L"");
        m_window.SetStatusText(2, L"");
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

void App::SaveTabState()
{
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_openTabs.size()))
        m_openTabs[m_activeTab].exposure = m_viewport.exposure;
}

HistogramCB App::BuildHistogramCB() const
{
    HistogramCB cb = {};
    // Panel in NDC: top-left corner, ~40% width, ~30% height
    cb.panelLeft = -0.95f;
    cb.panelTop = 0.95f;
    cb.panelWidth = 0.40f;
    cb.panelHeight = 0.30f;

    cb.channelMode = m_histogramChannel;
    cb.log2Min = m_histogram.log2Min;
    cb.log2Max = m_histogram.log2Max;
    cb.binCount = HistogramData::kBinCount;

    cb.tfExposure = m_viewport.exposure;
    cb.tfGamma = m_viewport.gamma;
    cb.tfIsHDR = m_viewport.isHDR ? 1 : 0;
    cb.sdrWhiteNits = m_viewport.sdrWhiteNits;
    cb.displayMaxNits = m_viewport.displayMaxNits;

    return cb;
}

void App::UpdateImageStatusText()
{
    if (!m_image.IsLoaded())
        return;

    wchar_t infoBuf[192];
    if (m_viewport.isHDR)
    {
        swprintf_s(infoBuf, L" %d x %d | EV %+.2f | HDR", m_image.width, m_image.height, m_viewport.exposure);
    }
    else
    {
        float displayGamma = 1.0f / m_viewport.gamma;
        swprintf_s(infoBuf, L" %d x %d | EV %+.2f | \u03B3 %.1f", m_image.width, m_image.height, m_viewport.exposure,
                   displayGamma);
    }
    m_window.SetStatusText(2, infoBuf);
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
        if (sscanf(line, "showHistogram=%d", &val) == 1)
            m_showHistogram = (val != 0);
        else if (sscanf(line, "histogramChannel=%d", &val) == 1)
            m_histogramChannel = (std::max)(0, (std::min)(val, 4));
        else if (sscanf(line, "showGrid=%d", &val) == 1)
            m_showGrid = (val != 0);
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

    fprintf(f, "showHistogram=%d\n", m_showHistogram ? 1 : 0);
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
