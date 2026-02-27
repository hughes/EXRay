#ifndef UNICODE
#define UNICODE
#endif

#include "app.h"

#include "resource.h"

#include <algorithm>
#include <shlobj.h>

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
        MessageBoxW(nullptr, L"Failed to create window.", L"SeeEXR", MB_ICONERROR);
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
            m_needsRedraw = true;
        }
        else if (vk == 'C')
        {
            if (m_showHistogram)
            {
                m_histogramChannel = (m_histogramChannel + 1) % 5;
                m_needsRedraw = true;
            }
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

    m_window.onDrop = [this](const wchar_t* path) { LoadFile(path); };

    m_timing->windowVisible = StartupTiming::Now();

    if (!m_renderer.Initialize(m_window.GetRenderHwnd()))
    {
        MessageBoxW(nullptr, L"Failed to initialize D3D11.", L"SeeEXR", MB_ICONERROR);
        return false;
    }

    m_timing->d3dReady = StartupTiming::Now();

    LoadRecentFiles();

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

            wchar_t title[MAX_PATH + 16];
            swprintf_s(title, L"SeeEXR - %s", cmdLinePath.c_str());
            m_window.SetTitle(title);

            UpdateImageStatusText();
            AddToRecentFiles(cmdLinePath);
        }
        else if (!m_loadError.empty())
        {
            int len = MultiByteToWideChar(CP_UTF8, 0, m_loadError.c_str(), -1, nullptr, 0);
            std::wstring wideError(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, m_loadError.c_str(), -1, wideError.data(), len);
            MessageBoxW(m_window.GetHwnd(), wideError.c_str(), L"SeeEXR - Error", MB_ICONERROR);
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
        m_renderer.RenderImage(m_viewport.ToViewportCB());

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
            LoadFile(m_recentFiles[index]);
        return;
    }

    switch (commandId)
    {
    case IDM_FILE_OPEN:
        OpenFileDialog();
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
        m_needsRedraw = true;
        break;

    case IDM_VIEW_FULLSCREEN:
        m_window.ToggleFullscreen();
        break;

    case IDM_HELP_ABOUT:
        MessageBoxW(m_window.GetHwnd(), L"SeeEXR - The EXR Viewer\nVersion 0.1.0", L"About SeeEXR",
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

void App::LoadFile(const std::wstring& path)
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
        m_viewport.FitToWindow();

        wchar_t title[MAX_PATH + 16];
        swprintf_s(title, L"SeeEXR - %s", path.c_str());
        m_window.SetTitle(title);

        UpdateImageStatusText();
        AddToRecentFiles(path);

        m_needsRedraw = true;
    }
    else
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, errorMsg.c_str(), -1, nullptr, 0);
        std::wstring wideError(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, errorMsg.c_str(), -1, wideError.data(), len);
        MessageBoxW(m_window.GetHwnd(), wideError.c_str(), L"SeeEXR - Error", MB_ICONERROR);
    }
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
        LoadFile(filePath);
    }
}

std::wstring App::GetRecentFilesPath()
{
    wchar_t* appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)))
        return {};
    std::wstring dir = std::wstring(appData) + L"\\SeeEXR";
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
    while (fwscanf(f, L" %[^\n]", line) == 1)
    {
        if (line[0] != L'\0')
            m_recentFiles.push_back(line);
    }
    fclose(f);

    m_window.UpdateRecentMenu(m_recentFiles);
}

void App::SaveRecentFiles()
{
    std::wstring path = GetRecentFilesPath();
    if (path.empty())
        return;

    FILE* f = _wfopen(path.c_str(), L"w, ccs=UTF-8");
    if (!f)
        return;

    for (const auto& entry : m_recentFiles)
        fwprintf(f, L"%s\n", entry.c_str());
    fclose(f);
}

void App::AddToRecentFiles(const std::wstring& path)
{
    // Remove if already present (move to front)
    m_recentFiles.erase(
        std::remove(m_recentFiles.begin(), m_recentFiles.end(), path), m_recentFiles.end());

    m_recentFiles.insert(m_recentFiles.begin(), path);

    if (static_cast<int>(m_recentFiles.size()) > kMaxRecentFiles)
        m_recentFiles.resize(kMaxRecentFiles);

    m_window.UpdateRecentMenu(m_recentFiles);
    SaveRecentFiles();
}
