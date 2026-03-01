#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <commctrl.h>

#include <functional>
#include <string>
#include <vector>

class Window
{
  public:
    using CommandHandler = std::function<void(int commandId)>;
    using ResizeHandler = std::function<void(int width, int height)>;
    using MouseWheelHandler = std::function<void(int x, int y, int delta, bool ctrlHeld)>;
    using MouseMoveHandler = std::function<void(int x, int y)>;
    using MouseButtonHandler = std::function<void(int x, int y, bool down)>;
    using KeyHandler = std::function<void(int vk)>;
    using DropHandler = std::function<void(const wchar_t* path)>;
    using TabChangeHandler = std::function<void(int newIndex)>;
    using ContextMenuHandler = std::function<void(int screenX, int screenY)>;

    bool Create(HINSTANCE hInstance, int nCmdShow, CommandHandler onCommand, ResizeHandler onResize);
    void SetTitle(const wchar_t* title);
    HWND GetHwnd() const { return m_hwnd; }
    HWND GetRenderHwnd() const { return m_renderArea; }
    HACCEL GetAccelTable() const { return m_accel; }

    // Returns render area size
    void GetClientSize(int& width, int& height) const;

    // Status bar updates
    void SetStatusText(int part, const wchar_t* text);

    // Fullscreen toggle
    void ToggleFullscreen();
    bool IsFullscreen() const { return m_isFullscreen; }

    // Recent files menu
    void UpdateRecentMenu(const std::vector<std::wstring>& paths);

    // Update View menu check marks / radio state
    void UpdateMenuChecks(bool showHistogram, int histogramChannel, bool showGrid);
    void UpdateHDRMenu(bool hdrCapable, bool hdrEnabled);

    // Tab bar management
    void AddTab(int index, const wchar_t* label);
    void RemoveTab(int index);
    void SetActiveTab(int index);
    int GetActiveTab() const;
    int GetTabCount() const;

    // Input callbacks
    MouseWheelHandler onMouseWheel;
    MouseMoveHandler onMouseMove;
    MouseButtonHandler onMiddleButton;
    KeyHandler onKeyDown;
    DropHandler onDrop;
    TabChangeHandler onTabChange;
    ContextMenuHandler onContextMenu;

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK RenderAreaProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void LayoutChildren();

    HWND m_hwnd = nullptr;
    HWND m_renderArea = nullptr;
    HWND m_tabBar = nullptr;
    HWND m_statusBar = nullptr;
    HACCEL m_accel = nullptr;
    CommandHandler m_onCommand;
    ResizeHandler m_onResize;

    // Drag state
    bool m_middleDragging = false;
    int m_lastDragX = 0;
    int m_lastDragY = 0;

    // Fullscreen state
    bool m_isFullscreen = false;
    WINDOWPLACEMENT m_savedPlacement = {};
    HMENU m_savedMenu = nullptr;

    int GetStatusBarHeight() const;
    int GetTabBarHeight() const;
};
