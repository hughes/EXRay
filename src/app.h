// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include "display_mode.h"
#include "histogram.h"
#include "image.h"
#include "renderer.h"
#include "sequence.h"
#include "timing.h"
#include "update_checker.h"
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
    bool Initialize(HINSTANCE hInstance, int nCmdShow, LPWSTR cmdLine, StartupTiming& timing, bool smokeTest = false);
    int Run();

  private:
    void OnCommand(int commandId);
    void OnResize(int width, int height);
    void OpenFileDialog();
    void OpenFile(const std::wstring& path);
    bool LoadFile(const std::wstring& path);
    bool LoadLayer(int layerIndex);
    void SwitchToTab(int index);
    void CloseCurrentTab();
    void CloseTabAtIndex(int index);
    void SaveTabState();
    void StartUpdateCheck();
    void Render();
    void UpdateImageStatusText();
    void SyncSidebar();

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

    // Histogram state (data for sidebar display)
    HistogramData m_histogram;
    int m_histogramChannel = 4; // 0=Lum, 1=R, 2=G, 3=B, 4=All

    // Layer info for current file
    ExrFileInfo m_layerInfo;
    int m_activeLayer = 0;

    // Grid state
    bool m_showGrid = true;

    // Display mode: 0=RGB, 1=R, 2=G, 3=B, 4=A, 5=RGB(ignore alpha)
    int m_displayMode = kDisplayModeRGB;

    // Compare mode
    enum class CompareMode
    {
        Off = 0,
        Split = 1,
        Difference = 2,
        Add = 3,
        Over = 4,
    };

    // A compare source snapshots the image at Set time
    struct CompareSource
    {
        int tabIndex = -1;
        int layerIndex = 0;
        ImageData image;       // snapshotted pixel data (copied at Set time)
        std::wstring label;    // human-readable label, e.g. "Blobbies.exr (diffuse)"
    };

    CompareMode m_compareMode = CompareMode::Off;
    CompareSource m_compareBase;
    CompareSource m_compareBlend;
    int m_compareFocused = 0;    // 0=base/left, 1=blend/right
    float m_splitX = 0.5f;      // normalized [0,1] split position
    float m_compareGain = 1.0f;  // gain for difference mode

    // Saved viewport state — restored when exiting compare
    float m_savedImageWidth = 0;
    float m_savedImageHeight = 0;

    void SetCompareMode(CompareMode mode);
    void ExitCompare();
    void SwapCompareSources();
    void SetCompareFocus(int source); // 0=base, 1=blend
    void SnapshotFocusedSource();
    void UploadCompareTextures();
    void SyncComparePanel();
    void AdjustCompareAfterRemove(int removedIndex);
    std::wstring MakeSourceLabel() const;

    // Open file tabs
    struct OpenTab
    {
        std::wstring path;
        float exposure = 0.0f;
        float zoom = 1.0f;
        float panX = 0.0f;
        float panY = 0.0f;
        float gamma = 1.0f / 2.2f;
        ImageData image;
        HistogramData histogram;
        ExrFileInfo layerInfo;
        int activeLayer = 0;
        SequenceInfo sequence; // empty if not a sequence
    };

    bool IsSequenceTab() const
    {
        return m_activeTab >= 0 && !m_openTabs[m_activeTab].sequence.frames.empty();
    }
    void NavigateSequence(int delta); // +1 next, -1 prev
    void JumpToFrame(int frameNumber);
    void SyncSequencePanel();
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

    // Update check state
    std::thread m_updateThread;
    UpdateCheckResult m_updateResult;
    std::atomic<bool> m_updateCheckComplete{false};
    bool m_updateAvailable = false;
    std::string m_updateVersion;

    // Smoke test mode — force WARP, suppress dialogs, exit after first frame
    bool m_smokeTest = false;
};
