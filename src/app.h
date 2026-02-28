#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include "histogram.h"
#include "image.h"
#include "renderer.h"
#include "timing.h"
#include "viewport.h"
#include "window.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

class App
{
  public:
    bool Initialize(HINSTANCE hInstance, int nCmdShow, LPWSTR cmdLine, StartupTiming& timing);
    int Run();

  private:
    void OnCommand(int commandId);
    void OnResize(int width, int height);
    void OpenFileDialog();
    void OpenFile(const std::wstring& path);
    bool LoadFile(const std::wstring& path);
    void SwitchToTab(int index);
    void CloseCurrentTab();
    void SaveTabState();
    void StartPreload();
    void FinishPreload();
    void EvictDistantTabs();
    void Render();
    void UpdateImageStatusText();
    HistogramCB BuildHistogramCB() const;

    // Recent files (MRU)
    void AddToRecentFiles(std::wstring path);
    void LoadRecentFiles();
    void SaveRecentFiles();
    std::wstring GetRecentFilesPath();

    // Preferences
    void LoadPreferences();
    void SavePreferences();
    std::wstring GetPreferencesPath();

    Window m_window;
    Renderer m_renderer;
    ImageData m_image;
    ViewportState m_viewport;
    HINSTANCE m_hInstance = nullptr;
    StartupTiming* m_timing = nullptr;
    bool m_needsRedraw = true;

    // Histogram state
    HistogramData m_histogram;
    bool m_showHistogram = true;
    int m_histogramChannel = 4; // 0=Lum, 1=R, 2=G, 3=B, 4=All

    // Grid state
    bool m_showGrid = true;

    // Open file tabs
    struct OpenTab
    {
        std::wstring path;
        float exposure = 0.0f;
        ImageData image;
        HistogramData histogram;
    };
    std::vector<OpenTab> m_openTabs;
    int m_activeTab = -1;

    // Recent files
    std::vector<std::wstring> m_recentFiles;
    static constexpr int kMaxRecentFiles = 10;

    // Startup parallel loading state
    std::thread m_loadThread;
    ImageData m_pendingImage;
    std::string m_loadError;
    std::atomic<bool> m_loadComplete{false};

    // Adjacent-tab preload state
    std::thread m_preloadThread;
    int m_preloadIndex = -1;
    ImageData m_preloadImage;
    HistogramData m_preloadHistogram;
    std::atomic<bool> m_preloadComplete{false};
};
