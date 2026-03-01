#ifndef UNICODE
#define UNICODE
#endif

#include "window.h"

#include "resource.h"

#include <shellapi.h>
#include <windowsx.h>

static const wchar_t* const kClassName = L"EXRay_Window";
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
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_HISTOGRAM, L"&Histogram\tH");

    HMENU channelMenu = CreatePopupMenu();
    AppendMenuW(channelMenu, MF_STRING, IDM_VIEW_CHAN_ALL, L"&All");
    AppendMenuW(channelMenu, MF_STRING, IDM_VIEW_CHAN_LUM, L"&Luminance");
    AppendMenuW(channelMenu, MF_STRING, IDM_VIEW_CHAN_RED, L"&Red");
    AppendMenuW(channelMenu, MF_STRING, IDM_VIEW_CHAN_GREEN, L"&Green");
    AppendMenuW(channelMenu, MF_STRING, IDM_VIEW_CHAN_BLUE, L"&Blue");
    AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(channelMenu), L"Histogram &Channel\tC");

    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_GRID, L"Pixel &Grid\tG");
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
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;

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

    m_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kClassName, L"EXRay", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             1280, 720, nullptr, menu, hInstance, this);

    if (!m_hwnd)
        return false;

    // Create common controls (status bar + tab bar)
    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_BAR_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&icex);

    m_statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                  m_hwnd, nullptr, hInstance, nullptr);

    // 3 parts: pixel coords | RGBA values | image info
    int parts[] = {120, 420, -1};
    SendMessageW(m_statusBar, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(parts));

    // Create tab bar between menu and render area
    m_tabBar = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
                               WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER, 0, 0, 0, 0, m_hwnd, nullptr,
                               hInstance, nullptr);
    SendMessageW(m_tabBar, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);

    // Create render area child window — D3D11 swapchain targets this, not the main window
    m_renderArea = CreateWindowExW(0, kRenderClassName, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hwnd, nullptr,
                                   hInstance, this);

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

    if (m_statusBar)
        SendMessageW(m_statusBar, WM_SIZE, 0, 0);

    int sbH = GetStatusBarHeight();
    int tabH = GetTabBarHeight();

    if (m_tabBar)
        MoveWindow(m_tabBar, 0, 0, totalW, tabH, TRUE);

    int renderH = totalH - tabH - sbH;
    if (renderH < 0)
        renderH = 0;

    if (m_renderArea)
        MoveWindow(m_renderArea, 0, tabH, totalW, renderH, TRUE);
}

int Window::GetStatusBarHeight() const
{
    if (!m_statusBar)
        return 0;
    RECT rc;
    GetWindowRect(m_statusBar, &rc);
    return rc.bottom - rc.top;
}

int Window::GetTabBarHeight() const
{
    if (!m_tabBar || !IsWindowVisible(m_tabBar))
        return 0;
    if (TabCtrl_GetItemCount(m_tabBar) == 0)
        return 0;
    // Compute tab strip height from a zero-height display rect
    RECT rc = {0, 0, 100, 0};
    TabCtrl_AdjustRect(m_tabBar, TRUE, &rc);
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

        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        // Hide status bar and tab bar in fullscreen
        if (m_statusBar)
            ShowWindow(m_statusBar, SW_HIDE);
        if (m_tabBar)
            ShowWindow(m_tabBar, SW_HIDE);

        m_isFullscreen = true;
    }
    else
    {
        // Show status bar and tab bar before restoring placement,
        // because SetWindowPlacement triggers WM_SIZE -> LayoutChildren()
        // which needs these visible to compute correct layout.
        if (m_statusBar)
            ShowWindow(m_statusBar, SW_SHOW);
        if (m_tabBar)
            ShowWindow(m_tabBar, SW_SHOW);

        // Restore window chrome and menu bar
        m_isFullscreen = false;
        LONG style = GetWindowLongW(m_hwnd, GWL_STYLE);
        SetWindowLongW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetMenu(m_hwnd, m_savedMenu);
        SetWindowPlacement(m_hwnd, &m_savedPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

void Window::UpdateRecentMenu(const std::vector<std::wstring>& paths)
{
    if (!m_hwnd)
        return;

    // Find the File menu (position 0), then the "Open Recent" submenu (position 3)
    HMENU menuBar = GetMenu(m_hwnd);
    HMENU fileMenu = GetSubMenu(menuBar, 0);
    HMENU recentMenu = GetSubMenu(fileMenu, 3);

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
        for (size_t i = 0; i < paths.size() && i < 10; ++i)
        {
            // Show just the filename for readability
            const wchar_t* filename = wcsrchr(paths[i].c_str(), L'\\');
            if (!filename)
                filename = wcsrchr(paths[i].c_str(), L'/');
            filename = filename ? filename + 1 : paths[i].c_str();

            wchar_t label[MAX_PATH + 16];
            swprintf_s(label, L"&%d  %s", static_cast<int>(i + 1), filename);
            AppendMenuW(recentMenu, MF_STRING, IDM_FILE_RECENT_BASE + static_cast<UINT>(i), label);
        }
    }
}

void Window::UpdateMenuChecks(bool showHistogram, int histogramChannel, bool showGrid)
{
    HMENU menu = GetMenu(m_hwnd);
    if (!menu)
        menu = m_savedMenu; // fullscreen: menu is detached
    if (!menu)
        return;

    CheckMenuItem(menu, IDM_VIEW_HISTOGRAM, MF_BYCOMMAND | (showHistogram ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, IDM_VIEW_GRID, MF_BYCOMMAND | (showGrid ? MF_CHECKED : MF_UNCHECKED));

    // Radio check for histogram channel (0=Lum,1=R,2=G,3=B,4=All)
    static const UINT channelIds[] = {IDM_VIEW_CHAN_LUM, IDM_VIEW_CHAN_RED, IDM_VIEW_CHAN_GREEN, IDM_VIEW_CHAN_BLUE,
                                      IDM_VIEW_CHAN_ALL};
    UINT activeId = (histogramChannel >= 0 && histogramChannel <= 4) ? channelIds[histogramChannel] : IDM_VIEW_CHAN_ALL;
    CheckMenuRadioItem(menu, IDM_VIEW_CHAN_LUM, IDM_VIEW_CHAN_ALL, activeId, MF_BYCOMMAND);

    // Gray out channel items when histogram is off
    UINT enable = showHistogram ? MF_ENABLED : MF_GRAYED;
    for (UINT id : channelIds)
        EnableMenuItem(menu, id, MF_BYCOMMAND | enable);
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
        self->LayoutChildren();
        if (wParam != SIZE_MINIMIZED && self->m_onResize)
        {
            int w, h;
            self->GetClientSize(w, h);
            if (w > 0 && h > 0)
                self->m_onResize(w, h);
        }
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

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && self->m_renderArea)
            SetFocus(self->m_renderArea);
        return 0;

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
        bool ctrl = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
        if (self->onMouseWheel)
            self->onMouseWheel(pt.x, pt.y, delta, ctrl);
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

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
