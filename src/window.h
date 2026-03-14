// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <commctrl.h>

#include "sidebar.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

// Window class name — used by FindWindow for single-instance detection
static const wchar_t* const kWindowClass = L"EXRay_Window";

// WM_COPYDATA tag for file-open requests from a second instance ('EXR1' in ASCII)
static constexpr ULONG_PTR kCopyDataOpenFile = 0x45585231;

class Window
{
  public:
    using CommandHandler = std::function<void(int commandId)>;
    using ResizeHandler = std::function<void(int width, int height)>;
    using MouseWheelHandler  = std::function<void(int x, int y, int delta, bool ctrlHeld, bool shiftHeld)>;
    using MouseHWheelHandler = std::function<void(int x, int y, int delta)>;
    using PinchZoomHandler   = std::function<void(int cx, int cy, float scale)>;
    using MouseMoveHandler = std::function<void(int x, int y)>;
    using MouseButtonHandler = std::function<void(int x, int y, bool down)>;
    using KeyHandler = std::function<void(int vk)>;
    using DropHandler = std::function<void(const wchar_t* path)>;
    using TabChangeHandler = std::function<void(int newIndex)>;
    using ContextMenuHandler = std::function<void(int screenX, int screenY)>;
    using TabCloseHandler = std::function<void(int tabIndex)>;
    using DisplayChangeHandler = std::function<void()>;

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
    void UpdateMenuChecks(bool showGrid, int displayMode);
    void UpdateHDRMenu(bool hdrCapable, bool hdrEnabled);
    void EnableImageMenuItems(bool hasImage);
    void MarkHelpMenuUpdate(bool available);

    // Sidebar (always visible)
    Sidebar& GetSidebar() { return m_sidebar; }

    // Tab bar management
    void AddTab(int index, const wchar_t* label);
    void RemoveTab(int index);
    void SetActiveTab(int index);
    int GetActiveTab() const;
    int GetTabCount() const;

    // Input callbacks
    MouseWheelHandler  onMouseWheel;
    MouseHWheelHandler onMouseHWheel;
    MouseMoveHandler   onMouseMove;
    MouseButtonHandler onMiddleButton;
    PinchZoomHandler   onPinchZoom;
    KeyHandler         onKeyDown;
    DropHandler        onDrop;
    TabChangeHandler      onTabChange;
    ContextMenuHandler    onContextMenu;
    TabCloseHandler       onTabClose;
    DisplayChangeHandler  onDisplayChange;

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK RenderAreaProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void LayoutChildren();
    void UpdateStatusBarParts();

    HWND m_hwnd = nullptr;
    HWND m_renderArea = nullptr;
    HWND m_tabBar = nullptr;
    HWND m_statusBar = nullptr;
    Sidebar m_sidebar;
    HACCEL m_accel = nullptr;
    CommandHandler m_onCommand;
    ResizeHandler m_onResize;

    // Status bar text storage (SBT_OWNERDRAW needs persistent strings)
    std::wstring m_statusText[3];

    // Drag state
    bool m_middleDragging = false;
    int m_lastDragX = 0;
    int m_lastDragY = 0;

    // Touch tracking for WM_POINTER
    struct TouchPoint { UINT32 id; int x, y; };
    TouchPoint m_touches[2]  = {};
    int  m_touchCount        = 0;
    int  m_prevTouchCX       = 0;
    int  m_prevTouchCY       = 0;
    float m_prevTouchDist    = 0.0f;

    // Monitor tracking for display change detection
    HMONITOR m_lastMonitor = nullptr;

    // Fullscreen state
    bool m_isFullscreen = false;
    WINDOWPLACEMENT m_savedPlacement = {};
    HMENU m_savedMenu = nullptr;

    // DPI-scaled UI font (owned, destroyed on DPI change and shutdown)
    HFONT m_uiFont = nullptr;
    void RecreateFont();

    // Viewport focus tracking
    bool m_viewportFocused = false;

    // Tab close button hover state
    bool m_closeButtonHover = false;

    // Sidebar resizing
    bool m_sidebarDragging = false;
    int m_sidebarBaseWidth = 240; // DPI-independent base width (persisted)
    static constexpr int kMinSidebarWidth = 180;
    static constexpr int kMaxSidebarWidth = 400;
    int GetSidebarWidth() const;
    static LRESULT CALLBACK SplitterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    HWND m_splitter = nullptr;

    int GetTabBarHeight() const;
    int GetStatusBarHeight() const;

    // Tab bar subclass for middle-click close and custom painting
    static LRESULT CALLBACK TabBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                        UINT_PTR subclassId, DWORD_PTR refData);

    // Status bar subclass for borderless custom painting
    static LRESULT CALLBACK StatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR subclassId, DWORD_PTR refData);

    // Paint over the 1px system border below the menu bar
    void PaintMenuBarBorder();

    // Refresh all controls after system theme change
    void RefreshTheme();
};
