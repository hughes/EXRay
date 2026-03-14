// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "window.h"

#include "resource.h"

#include <shellapi.h>
#include <windowsx.h>

static const wchar_t* const kRenderClassName = L"EXRay_RenderArea";

static HMENU CreateAppMenu()
{
    HMENU menuBar = CreateMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_RELOAD, L"&Reload\tCtrl+R");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_CLOSE, L"&Close\tCtrl+W");
    HMENU recentMenu = CreatePopupMenu();
    AppendMenuW(recentMenu, MF_STRING | MF_GRAYED, 0, L"(empty)");
    AppendMenuW(fileMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(recentMenu), L"Open Recen&t");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"E&xit\tAlt+F4");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");

    HMENU viewMenu = CreatePopupMenu();
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_FIT, L"&Fit to Window\tCtrl+0");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_ACTUAL, L"&Actual Size\tCtrl+1");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_EXPOSURE_UP, L"Increase Exposure\t+");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_EXPOSURE_DOWN, L"Decrease Exposure\t\x2212");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_GAMMA_UP, L"Increase Gamma\t]");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_GAMMA_DOWN, L"Decrease Gamma\t[");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_GRID, L"Pixel &Grid\tG");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_CHANNEL_RGB, L"RGB\tShift+~");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_CHANNEL_R, L"Red Channel\tShift+1");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_CHANNEL_G, L"Green Channel\tShift+2");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_CHANNEL_B, L"Blue Channel\tShift+3");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_CHANNEL_A, L"Alpha Channel\tShift+4");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_HDR, L"&HDR Output");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_FULLSCREEN, L"&Fullscreen\tF11");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"&View");

    HMENU helpMenu = CreatePopupMenu();
    AppendMenuW(helpMenu, MF_STRING, IDM_HELP_ABOUT, L"&About EXRay");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"&Help");

    return menuBar;
}

static HACCEL CreateAppAccelerators()
{
    ACCEL accels[] = {
        {FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN},
        {FVIRTKEY | FCONTROL, 'R', IDM_FILE_RELOAD},
        {FVIRTKEY | FCONTROL, 'W', IDM_FILE_CLOSE},
        {FVIRTKEY | FCONTROL, VK_TAB, IDM_FILE_NEXT_TAB},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, IDM_FILE_PREV_TAB},
        {FVIRTKEY | FCONTROL, '0', IDM_VIEW_FIT},
        {FVIRTKEY | FCONTROL, '1', IDM_VIEW_ACTUAL},
    };
    return CreateAcceleratorTableW(accels, _countof(accels));
}

bool Window::Create(HINSTANCE hInstance, int nCmdShow, CommandHandler onCommand, ResizeHandler onResize)
{
    m_onCommand = std::move(onCommand);
    m_onResize = std::move(onResize);

    // Register main window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x2D, 0x2D, 0x2D));
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc))
        return false;

    // Register render area child window class
    WNDCLASSEXW rcWc = {};
    rcWc.cbSize = sizeof(rcWc);
    rcWc.style = CS_HREDRAW | CS_VREDRAW;
    rcWc.lpfnWndProc = RenderAreaProc;
    rcWc.hInstance = hInstance;
    rcWc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    rcWc.hbrBackground = nullptr;
    rcWc.lpszClassName = kRenderClassName;
    RegisterClassExW(&rcWc);

    HMENU menu = CreateAppMenu();

    m_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kWindowClass, L"EXRay",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
                             nullptr, menu, hInstance, this);

    if (!m_hwnd)
        return false;

    m_lastMonitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);

    // Create common controls (status bar + tab bar)
    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_BAR_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&icex);

    m_statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                  m_hwnd, nullptr, hInstance, nullptr);

    // 3 parts: pixel coords | RGBA values | image info (DPI-scaled)
    UpdateStatusBarParts();

    // Tab bar — always visible (empty strip when no tabs)
    m_tabBar = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
                               WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER, 0, 0, 0, 0, m_hwnd, nullptr,
                               hInstance, nullptr);
    // Match the tab bar font to the status bar so they look consistent across systems
    HFONT statusFont = reinterpret_cast<HFONT>(SendMessageW(m_statusBar, WM_GETFONT, 0, 0));
    SendMessageW(m_tabBar, WM_SETFONT, reinterpret_cast<WPARAM>(statusFont), FALSE);

    // Subclass the tab bar to intercept middle-click for tab close
    SetWindowSubclass(m_tabBar, TabBarProc, 0, reinterpret_cast<DWORD_PTR>(this));

    // Create render area child window — D3D11 swapchain targets this, not the main window
    m_renderArea = CreateWindowExW(0, kRenderClassName, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hwnd, nullptr,
                                   hInstance, this);

    // Create sidebar panel (always visible)
    m_sidebar.Create(m_hwnd, hInstance);
    m_sidebar.SetVisible(true);

    LayoutChildren();

    m_accel = CreateAppAccelerators();

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    SetFocus(m_renderArea);

    return true;
}

void Window::SetTitle(const wchar_t* title)
{
    if (m_hwnd)
        SetWindowTextW(m_hwnd, title);
}

void Window::GetClientSize(int& width, int& height) const
{
    RECT rc;
    GetClientRect(m_renderArea, &rc);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
}

void Window::LayoutChildren()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int totalW = rc.right - rc.left;
    int totalH = rc.bottom - rc.top;

    if (m_isFullscreen)
    {
        // Fullscreen: render area + sidebar fill the entire client rect
        int sidebarW = m_sidebar.GetWidth();
        int renderW = totalW - sidebarW;
        if (renderW < 0)
            renderW = 0;
        if (m_renderArea)
            MoveWindow(m_renderArea, 0, 0, renderW, totalH, TRUE);
        if (m_sidebar.GetHwnd())
            MoveWindow(m_sidebar.GetHwnd(), renderW, 0, sidebarW, totalH, TRUE);
        if (m_onResize && renderW > 0 && totalH > 0)
            m_onResize(renderW, totalH);
        return;
    }

    // Normal mode: fixed layout — tab bar, status bar, sidebar always present
    if (m_statusBar)
        SendMessageW(m_statusBar, WM_SIZE, 0, 0);

    int sbH = GetStatusBarHeight();
    int tabH = GetTabBarHeight();
    int sidebarW = m_sidebar.GetWidth();

    // Tab bar spans full width
    if (m_tabBar)
        MoveWindow(m_tabBar, 0, 0, totalW, tabH, TRUE);

    // Middle zone: render area + sidebar
    int middleH = totalH - tabH - sbH;
    if (middleH < 0)
        middleH = 0;
    int renderW = totalW - sidebarW;
    if (renderW < 0)
        renderW = 0;

    if (m_renderArea)
        MoveWindow(m_renderArea, 0, tabH, renderW, middleH, TRUE);

    if (m_sidebar.GetHwnd())
        MoveWindow(m_sidebar.GetHwnd(), renderW, tabH, sidebarW, middleH, TRUE);

    // Notify app of render area size change (e.g. for swap chain resize)
    if (m_onResize && renderW > 0 && middleH > 0)
        m_onResize(renderW, middleH);
}

void Window::UpdateStatusBarParts()
{
    int dpi = GetDpiForWindow(m_hwnd);
    int parts[] = {MulDiv(120, dpi, 96), MulDiv(420, dpi, 96), -1};
    SendMessageW(m_statusBar, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(parts));
}

int Window::GetTabBarHeight() const
{
    if (!m_tabBar)
        return 0;
    if (TabCtrl_GetItemCount(m_tabBar) == 0)
        return 0;
    RECT rc = {0, 0, 100, 0};
    TabCtrl_AdjustRect(m_tabBar, TRUE, &rc);
    return rc.bottom - rc.top;
}

int Window::GetStatusBarHeight() const
{
    if (!m_statusBar)
        return 0;
    RECT rc;
    GetWindowRect(m_statusBar, &rc);
    return rc.bottom - rc.top;
}

void Window::SetStatusText(int part, const wchar_t* text)
{
    if (m_statusBar)
        SendMessageW(m_statusBar, SB_SETTEXTW, part, reinterpret_cast<LPARAM>(text));
}

void Window::ToggleFullscreen()
{
    if (!m_hwnd)
        return;

    if (!m_isFullscreen)
    {
        // Save current placement and menu for restore
        m_savedPlacement.length = sizeof(m_savedPlacement);
        GetWindowPlacement(m_hwnd, &m_savedPlacement);
        m_savedMenu = GetMenu(m_hwnd);

        // Remove window chrome, menu bar, and cover the monitor
        LONG style = GetWindowLongW(m_hwnd, GWL_STYLE);
        SetWindowLongW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetMenu(m_hwnd, nullptr);

        HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(mon, &mi);

        // Set fullscreen first — LayoutChildren() uses this flag
        m_isFullscreen = true;

        // Hide tab bar and status bar (sidebar stays visible)
        if (m_statusBar)
            ShowWindow(m_statusBar, SW_HIDE);
        if (m_tabBar)
            ShowWindow(m_tabBar, SW_HIDE);

        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    else
    {
        // Clear fullscreen first — LayoutChildren() uses this flag
        m_isFullscreen = false;

        // Restore window chrome and menu bar
        LONG style = GetWindowLongW(m_hwnd, GWL_STYLE);
        SetWindowLongW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetMenu(m_hwnd, m_savedMenu);

        // Show tab bar and status bar (sidebar was never hidden)
        if (m_statusBar)
            ShowWindow(m_statusBar, SW_SHOW);
        if (m_tabBar)
            ShowWindow(m_tabBar, SW_SHOW);

        SetWindowPlacement(m_hwnd, &m_savedPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

void Window::UpdateRecentMenu(const std::vector<std::wstring>& paths)
{
    if (!m_hwnd)
        return;

    // Find the File menu (position 0), then the "Open Recent" submenu (position 3)
    HMENU menu = GetMenu(m_hwnd);
    if (!menu)
        menu = m_savedMenu;
    if (!menu)
        return;

    HMENU fileMenu = GetSubMenu(menu, 0);
    if (!fileMenu)
        return;

    HMENU recentMenu = GetSubMenu(fileMenu, 3); // "Open Recent" is at position 3
    if (!recentMenu)
        return;

    // Clear existing items
    while (GetMenuItemCount(recentMenu) > 0)
        DeleteMenu(recentMenu, 0, MF_BYPOSITION);

    if (paths.empty())
    {
        AppendMenuW(recentMenu, MF_STRING | MF_GRAYED, 0, L"(empty)");
    }
    else
    {
        for (size_t i = 0; i < paths.size(); ++i)
        {
            const wchar_t* filename = paths[i].c_str();
            // Find just the filename portion for display
            const wchar_t* slash = wcsrchr(filename, L'\\');
            if (slash)
                filename = slash + 1;

            wchar_t label[MAX_PATH + 16];
            swprintf_s(label, L"&%d  %s", static_cast<int>(i + 1), filename);
            AppendMenuW(recentMenu, MF_STRING, IDM_FILE_RECENT_BASE + static_cast<UINT>(i), label);
        }
    }
}

void Window::UpdateMenuChecks(bool showGrid, int displayMode)
{
    HMENU menu = GetMenu(m_hwnd);
    if (!menu)
        menu = m_savedMenu; // fullscreen: menu is detached
    if (!menu)
        return;

    CheckMenuItem(menu, IDM_VIEW_GRID, MF_BYCOMMAND | (showGrid ? MF_CHECKED : MF_UNCHECKED));

    // Radio-style check on channel display mode
    UINT channelIds[] = {IDM_VIEW_CHANNEL_RGB, IDM_VIEW_CHANNEL_R, IDM_VIEW_CHANNEL_G,
                         IDM_VIEW_CHANNEL_B, IDM_VIEW_CHANNEL_A};
    for (int i = 0; i < 5; i++)
        CheckMenuItem(menu, channelIds[i], MF_BYCOMMAND | (displayMode == i ? MF_CHECKED : MF_UNCHECKED));
}

void Window::UpdateHDRMenu(bool hdrCapable, bool hdrEnabled)
{
    HMENU menu = GetMenu(m_hwnd);
    if (!menu)
        menu = m_savedMenu;
    if (!menu)
        return;

    CheckMenuItem(menu, IDM_VIEW_HDR, MF_BYCOMMAND | (hdrEnabled ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(menu, IDM_VIEW_HDR, MF_BYCOMMAND | (hdrCapable ? MF_ENABLED : MF_GRAYED));

    // Gamma adjustment is only meaningful in SDR mode
    UINT gammaEnable = hdrEnabled ? MF_GRAYED : MF_ENABLED;
    EnableMenuItem(menu, IDM_VIEW_GAMMA_UP, MF_BYCOMMAND | gammaEnable);
    EnableMenuItem(menu, IDM_VIEW_GAMMA_DOWN, MF_BYCOMMAND | gammaEnable);
}

void Window::MarkHelpMenuUpdate(bool available)
{
    HMENU menu = GetMenu(m_hwnd);
    if (!menu)
        menu = m_savedMenu;
    if (!menu)
        return;

    // Help menu is at position 2 (File=0, View=1, Help=2)
    ModifyMenuW(menu, 2, MF_BYPOSITION | MF_POPUP,
                reinterpret_cast<UINT_PTR>(GetSubMenu(menu, 2)),
                available ? L"&Help *" : L"&Help");
    DrawMenuBar(m_hwnd);
}

void Window::AddTab(int index, const wchar_t* label)
{
    if (!m_tabBar)
        return;
    TCITEMW tci = {};
    tci.mask = TCIF_TEXT;
    tci.pszText = const_cast<wchar_t*>(label);
    TabCtrl_InsertItem(m_tabBar, index, &tci);
    LayoutChildren();
}

void Window::RemoveTab(int index)
{
    if (!m_tabBar)
        return;
    TabCtrl_DeleteItem(m_tabBar, index);
    LayoutChildren();
}

void Window::SetActiveTab(int index)
{
    if (m_tabBar)
        TabCtrl_SetCurSel(m_tabBar, index);
}

int Window::GetActiveTab() const
{
    if (!m_tabBar)
        return -1;
    return TabCtrl_GetCurSel(m_tabBar);
}

int Window::GetTabCount() const
{
    if (!m_tabBar)
        return 0;
    return TabCtrl_GetItemCount(m_tabBar);
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Window* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_COMMAND:
        if (self->m_onCommand)
            self->m_onCommand(LOWORD(wParam));
        return 0;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            self->LayoutChildren();
        return 0;

    case WM_NOTIFY:
    {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr->hwndFrom == self->m_tabBar && nmhdr->code == TCN_SELCHANGE)
        {
            int sel = TabCtrl_GetCurSel(self->m_tabBar);
            if (sel >= 0 && self->onTabChange)
                self->onTabChange(sel);
        }
        return 0;
    }

    case WM_DROPFILES:
    {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[MAX_PATH];
        DragQueryFileW(hDrop, 0, path, MAX_PATH);
        DragFinish(hDrop);
        if (self->onDrop)
            self->onDrop(path);
        return 0;
    }

    case WM_COPYDATA:
    {
        auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
        if (cds->dwData == kCopyDataOpenFile && cds->lpData && self->onDrop)
        {
            self->onDrop(static_cast<const wchar_t*>(cds->lpData));
            return TRUE;
        }
        return FALSE;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && self->m_renderArea)
            SetFocus(self->m_renderArea);
        return 0;

    case WM_DPICHANGED:
    {
        // Reposition to the rect Windows suggests for the new DPI
        RECT* r = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // Re-scale status bar parts and sync tab bar font for the new DPI
        if (self->m_statusBar)
            self->UpdateStatusBarParts();
        if (self->m_statusBar && self->m_tabBar)
        {
            HFONT f = reinterpret_cast<HFONT>(SendMessageW(self->m_statusBar, WM_GETFONT, 0, 0));
            SendMessageW(self->m_tabBar, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
        }
        return 0;
    }

    case WM_WINDOWPOSCHANGED:
    {
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (mon != self->m_lastMonitor)
        {
            self->m_lastMonitor = mon;
            if (self->onDisplayChange)
                self->onDisplayChange();
        }
        break; // let DefWindowProcW dispatch WM_SIZE / WM_MOVE
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 400;
        mmi->ptMinTrackSize.y = 300;
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK Window::RenderAreaProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Window* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;

    case WM_MOUSEWHEEL:
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        bool ctrl  = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
        bool shift = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0;
        if (self->onMouseWheel)
            self->onMouseWheel(pt.x, pt.y, delta, ctrl, shift);
        return 0;
    }

    case WM_MOUSEHWHEEL:
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (self->onMouseHWheel)
            self->onMouseHWheel(pt.x, pt.y, delta);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (self->m_middleDragging)
        {
            int dx = x - self->m_lastDragX;
            int dy = y - self->m_lastDragY;
            self->m_lastDragX = x;
            self->m_lastDragY = y;
            if (self->onMiddleButton)
                self->onMiddleButton(dx, dy, true);
        }

        if (self->onMouseMove)
            self->onMouseMove(x, y);
        return 0;
    }

    case WM_MBUTTONDOWN:
    {
        self->m_middleDragging = true;
        self->m_lastDragX = GET_X_LPARAM(lParam);
        self->m_lastDragY = GET_Y_LPARAM(lParam);
        SetCapture(hwnd);
        return 0;
    }

    case WM_MBUTTONUP:
    {
        self->m_middleDragging = false;
        ReleaseCapture();
        return 0;
    }

    case WM_KEYDOWN:
        if (self->onKeyDown)
            self->onKeyDown(static_cast<int>(wParam));
        return 0;

    case WM_CONTEXTMENU:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (x != -1 && y != -1 && self->onContextMenu)
            self->onContextMenu(x, y);
        return 0;
    }

    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    {
        UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
        POINTER_INFO pi  = {};
        if (!GetPointerInfo(pointerId, &pi) || pi.pointerType != PT_TOUCH)
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        POINT pt = pi.ptPixelLocation;
        ScreenToClient(hwnd, &pt);

        if (msg == WM_POINTERDOWN)
        {
            SetFocus(hwnd);
            // Add to tracking (max 2 points)
            bool found = false;
            for (int i = 0; i < self->m_touchCount; ++i)
            {
                if (self->m_touches[i].id == pointerId)
                {
                    self->m_touches[i] = {pointerId, pt.x, pt.y};
                    found = true;
                    break;
                }
            }
            if (!found && self->m_touchCount < 2)
                self->m_touches[self->m_touchCount++] = {pointerId, pt.x, pt.y};

            // Reset baseline so first POINTERUPDATE doesn't produce a stale delta
            int cx = 0, cy = 0;
            for (int i = 0; i < self->m_touchCount; ++i) { cx += self->m_touches[i].x; cy += self->m_touches[i].y; }
            self->m_prevTouchCX = cx / self->m_touchCount;
            self->m_prevTouchCY = cy / self->m_touchCount;
            if (self->m_touchCount == 2)
            {
                float dx = (float)(self->m_touches[0].x - self->m_touches[1].x);
                float dy = (float)(self->m_touches[0].y - self->m_touches[1].y);
                self->m_prevTouchDist = sqrtf(dx * dx + dy * dy);
            }
        }
        else if (msg == WM_POINTERUP)
        {
            for (int i = 0; i < self->m_touchCount; ++i)
            {
                if (self->m_touches[i].id == pointerId)
                {
                    self->m_touches[i] = self->m_touches[--self->m_touchCount];
                    break;
                }
            }
            // Reset baseline for remaining touch(es)
            if (self->m_touchCount > 0)
            {
                int cx = 0, cy = 0;
                for (int i = 0; i < self->m_touchCount; ++i) { cx += self->m_touches[i].x; cy += self->m_touches[i].y; }
                self->m_prevTouchCX = cx / self->m_touchCount;
                self->m_prevTouchCY = cy / self->m_touchCount;
                self->m_prevTouchDist = 0.0f;
            }
        }
        else // WM_POINTERUPDATE
        {
            // Update the stored position for this touch point
            for (int i = 0; i < self->m_touchCount; ++i)
            {
                if (self->m_touches[i].id == pointerId)
                {
                    self->m_touches[i].x = pt.x;
                    self->m_touches[i].y = pt.y;
                    break;
                }
            }

            // Coalesce: if more POINTERUPDATE messages are already queued, skip
            // firing callbacks now — the position is updated and a later message
            // (with everyone's latest position) will fire the callbacks once.
            // This prevents processing a large backlog of stale intermediate positions.
            MSG peek;
            if (PeekMessageW(&peek, hwnd, WM_POINTERUPDATE, WM_POINTERUPDATE, PM_NOREMOVE))
                return 0;

            if (self->m_touchCount > 0)
            {
                int cx = 0, cy = 0;
                for (int i = 0; i < self->m_touchCount; ++i) { cx += self->m_touches[i].x; cy += self->m_touches[i].y; }
                cx /= self->m_touchCount;
                cy /= self->m_touchCount;

                int dx = cx - self->m_prevTouchCX;
                int dy = cy - self->m_prevTouchCY;
                self->m_prevTouchCX = cx;
                self->m_prevTouchCY = cy;

                if ((dx || dy) && self->onMiddleButton)
                    self->onMiddleButton(dx, dy, true);

                if (self->m_touchCount == 2 && self->m_prevTouchDist > 0.0f)
                {
                    float fdx = (float)(self->m_touches[0].x - self->m_touches[1].x);
                    float fdy = (float)(self->m_touches[0].y - self->m_touches[1].y);
                    float dist = sqrtf(fdx * fdx + fdy * fdy);
                    if (dist > 0.0f)
                    {
                        float scale = dist / self->m_prevTouchDist;
                        if (self->onPinchZoom)
                            self->onPinchZoom(cx, cy, scale);
                    }
                    self->m_prevTouchDist = dist;
                }
            }
        }
        return 0; // Suppress mouse synthesis from touch
    }

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK Window::TabBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR /*subclassId*/, DWORD_PTR refData)
{
    if (msg == WM_MBUTTONDOWN)
    {
        auto* self = reinterpret_cast<Window*>(refData);
        TCHITTESTINFO hitInfo = {};
        hitInfo.pt.x = GET_X_LPARAM(lParam);
        hitInfo.pt.y = GET_Y_LPARAM(lParam);
        int tab = TabCtrl_HitTest(hwnd, &hitInfo);
        if (tab >= 0 && self->onTabClose)
            self->onTabClose(tab);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
