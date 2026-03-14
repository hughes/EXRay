// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "window.h"

#include "resource.h"
#include "themes.h"

#include <algorithm>
#include <shellapi.h>
#include <windowsx.h>

static const wchar_t* const kRenderClassName = L"EXRay_RenderArea";
static const wchar_t* const kSplitterClassName = L"EXRay_Splitter";

// Tab close button hit area (DPI-independent)
static constexpr int kTabCloseBtnSize = 14;
static constexpr int kTabCloseBtnMargin = 4;

// Make top-level menu bar items owner-drawn so we can paint them with the current theme.
// The popup menus are themed automatically by SetPreferredAppMode, but the menu bar strip
// is part of the non-client area and doesn't respond to dark mode APIs on all Windows builds.
static void MakeMenuBarOwnerDrawn(HMENU menuBar)
{
    MENUINFO mi = {sizeof(mi)};
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = Theme::GetBackgroundBrush();
    SetMenuInfo(menuBar, &mi);

    int count = GetMenuItemCount(menuBar);
    for (int i = 0; i < count; i++)
    {
        wchar_t text[64] = {};
        MENUITEMINFOW mii = {sizeof(mii)};
        mii.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_DATA;
        mii.dwTypeData = text;
        mii.cch = _countof(text);
        GetMenuItemInfoW(menuBar, i, TRUE, &mii);

        // Allocate persistent copy of the text (lives for process lifetime)
        wchar_t* copy = _wcsdup(text);
        mii.fMask = MIIM_FTYPE | MIIM_DATA;
        mii.fType |= MFT_OWNERDRAW;
        mii.dwItemData = reinterpret_cast<ULONG_PTR>(copy);
        SetMenuItemInfoW(menuBar, i, TRUE, &mii);
    }
}

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
    wc.hbrBackground = Theme::GetBackgroundBrush();
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

    // Register splitter class for sidebar resize
    WNDCLASSEXW spWc = {};
    spWc.cbSize = sizeof(spWc);
    spWc.lpfnWndProc = SplitterProc;
    spWc.hInstance = hInstance;
    spWc.hCursor = LoadCursorW(nullptr, IDC_SIZEWE);
    spWc.hbrBackground = Theme::GetBackgroundBrush();
    spWc.lpszClassName = kSplitterClassName;
    RegisterClassExW(&spWc);

    HMENU menu = CreateAppMenu();

    m_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kWindowClass, L"EXRay", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, menu, hInstance, this);

    if (!m_hwnd)
        return false;

    // Apply theme to title bar and non-client area
    Theme::EnableForWindow(m_hwnd);

    // Owner-draw the menu bar items for theme-aware rendering
    MakeMenuBarOwnerDrawn(menu);
    DrawMenuBar(m_hwnd);

    m_lastMonitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);

    // Create DPI-scaled font
    RecreateFont();

    // Create common controls (status bar + tab bar)
    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_BAR_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&icex);

    // Status bar — custom-painted via subclass (no SBARS_SIZEGRIP to avoid light grip)
    m_statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hwnd, nullptr,
                                  hInstance, nullptr);
    Theme::ApplyToControl(m_statusBar);
    SendMessageW(m_statusBar, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), FALSE);
    SetWindowSubclass(m_statusBar, StatusBarProc, 0, reinterpret_cast<DWORD_PTR>(this));

    // 3 parts: pixel coords | RGBA values | image info (DPI-scaled)
    UpdateStatusBarParts();

    // Tab bar — custom-painted via subclass (TCS_OWNERDRAWFIXED still needed for item rects)
    m_tabBar = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
                               WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER | TCS_OWNERDRAWFIXED, 0, 0, 0,
                               0, m_hwnd, nullptr, hInstance, nullptr);
    SendMessageW(m_tabBar, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), FALSE);
    Theme::ApplyToControl(m_tabBar);

    // Add extra horizontal padding for close button on active tab.
    // TCM_SETPADDING is per-side, and the close button needs btnSize + 2*margin on the right.
    // Since padding is symmetric, we use the full close-button width as per-side padding;
    // inactive tabs get a bit more space too, but that's fine.
    {
        int dpi = GetDpiForWindow(m_hwnd);
        int closeBtnSpace = MulDiv(kTabCloseBtnSize + kTabCloseBtnMargin * 2, dpi, 96);
        SendMessageW(m_tabBar, TCM_SETPADDING, 0, MAKELPARAM(closeBtnSpace, MulDiv(4, dpi, 96)));
    }

    // Subclass the tab bar for custom painting, middle-click close
    SetWindowSubclass(m_tabBar, TabBarProc, 0, reinterpret_cast<DWORD_PTR>(this));

    // Create render area child window — D3D11 swapchain targets this, not the main window
    m_renderArea = CreateWindowExW(0, kRenderClassName, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hwnd, nullptr,
                                   hInstance, this);

    // Splitter between render area and sidebar (4px wide)
    m_splitter = CreateWindowExW(0, kSplitterClassName, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hwnd, nullptr,
                                 hInstance, this);

    // Create sidebar panel (always visible)
    m_sidebar.Create(m_hwnd, hInstance);
    m_sidebar.SetVisible(true);
    m_sidebar.SetFont(m_uiFont);

    LayoutChildren();

    m_accel = CreateAppAccelerators();

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    SetFocus(m_renderArea);

    return true;
}

void Window::RecreateFont()
{
    if (m_uiFont)
        DeleteObject(m_uiFont);
    m_uiFont = Theme::CreateDpiFont(m_hwnd);
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

int Window::GetSidebarWidth() const
{
    if (!m_sidebar.IsVisible() || !m_sidebar.GetHwnd())
        return 0;
    int dpi = GetDpiForWindow(m_hwnd);
    return MulDiv(m_sidebarBaseWidth, dpi, 96);
}

static constexpr int kSplitterWidth = 4;

void Window::LayoutChildren()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int totalW = rc.right - rc.left;
    int totalH = rc.bottom - rc.top;

    if (m_isFullscreen)
    {
        // Fullscreen: render area + splitter + sidebar fill the entire client rect
        int sidebarW = GetSidebarWidth();
        int renderW = totalW - sidebarW - kSplitterWidth;
        if (renderW < 0)
            renderW = 0;
        if (m_renderArea)
            MoveWindow(m_renderArea, 0, 0, renderW, totalH, TRUE);
        if (m_splitter)
            MoveWindow(m_splitter, renderW, 0, kSplitterWidth, totalH, TRUE);
        if (m_sidebar.GetHwnd())
            MoveWindow(m_sidebar.GetHwnd(), renderW + kSplitterWidth, 0, sidebarW, totalH, TRUE);
        if (m_onResize && renderW > 0 && totalH > 0)
            m_onResize(renderW, totalH);
        return;
    }

    // Normal mode: fixed layout — tab bar, status bar, sidebar always present
    if (m_statusBar)
        SendMessageW(m_statusBar, WM_SIZE, 0, 0);

    int sbH = GetStatusBarHeight();
    int tabH = GetTabBarHeight();
    int sidebarW = GetSidebarWidth();

    // Tab bar spans full width
    if (m_tabBar)
        MoveWindow(m_tabBar, 0, 0, totalW, tabH, TRUE);

    // Middle zone: render area + splitter + sidebar
    int middleH = totalH - tabH - sbH;
    if (middleH < 0)
        middleH = 0;
    int renderW = totalW - sidebarW - kSplitterWidth;
    if (renderW < 0)
        renderW = 0;

    if (m_renderArea)
        MoveWindow(m_renderArea, 0, tabH, renderW, middleH, TRUE);

    if (m_splitter)
        MoveWindow(m_splitter, renderW, tabH, kSplitterWidth, middleH, TRUE);

    if (m_sidebar.GetHwnd())
        MoveWindow(m_sidebar.GetHwnd(), renderW + kSplitterWidth, tabH, sidebarW, middleH, TRUE);

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
    if (!m_statusBar || part < 0 || part > 2)
        return;
    m_statusText[part] = text ? text : L"";
    SendMessageW(m_statusBar, SB_SETTEXTW, part | SBT_OWNERDRAW, reinterpret_cast<LPARAM>(m_statusText[part].c_str()));
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
    UINT channelIds[] = {IDM_VIEW_CHANNEL_RGB, IDM_VIEW_CHANNEL_R, IDM_VIEW_CHANNEL_G, IDM_VIEW_CHANNEL_B,
                         IDM_VIEW_CHANNEL_A};
    for (int i = 0; i < 5; i++)
        CheckMenuItem(menu, channelIds[i], MF_BYCOMMAND | (displayMode == i ? MF_CHECKED : MF_UNCHECKED));
}

void Window::EnableImageMenuItems(bool hasImage)
{
    HMENU menu = GetMenu(m_hwnd);
    if (!menu)
        menu = m_savedMenu;
    if (!menu)
        return;

    UINT flag = hasImage ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(menu, IDM_FILE_RELOAD, MF_BYCOMMAND | flag);
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
    ModifyMenuW(menu, 2, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(GetSubMenu(menu, 2)),
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

    case WM_MEASUREITEM:
    {
        auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mis->CtlType == ODT_MENU)
        {
            const wchar_t* text = reinterpret_cast<const wchar_t*>(mis->itemData);
            if (text)
            {
                HDC hdc = GetDC(hwnd);
                HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, self->m_uiFont));
                SIZE sz;
                GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &sz);
                SelectObject(hdc, oldFont);
                ReleaseDC(hwnd, hdc);
                mis->itemWidth = sz.cx;
                mis->itemHeight = sz.cy + MulDiv(8, GetDpiForWindow(hwnd), 96);
            }
            return TRUE;
        }
        return 0;
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlType == ODT_MENU)
        {
            bool selected = (dis->itemState & ODS_SELECTED) != 0;
            bool hotlight = (dis->itemState & ODS_HOTLIGHT) != 0;

            COLORREF bgColor = (selected || hotlight) ? Colors::TabActive : Colors::Background;
            HBRUSH bg = CreateSolidBrush(bgColor);
            FillRect(dis->hDC, &dis->rcItem, bg);
            DeleteObject(bg);

            const wchar_t* text = reinterpret_cast<const wchar_t*>(dis->itemData);
            if (text)
            {
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, Colors::TextPrimary);
                HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, self->m_uiFont));
                DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                SelectObject(dis->hDC, oldFont);
            }
            return TRUE;
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
        // Re-scale status bar parts
        if (self->m_statusBar)
            self->UpdateStatusBarParts();
        // Recreate font at new DPI
        self->RecreateFont();
        if (self->m_statusBar)
            SendMessageW(self->m_statusBar, WM_SETFONT, reinterpret_cast<WPARAM>(self->m_uiFont), TRUE);
        if (self->m_tabBar)
            SendMessageW(self->m_tabBar, WM_SETFONT, reinterpret_cast<WPARAM>(self->m_uiFont), TRUE);
        self->m_sidebar.SetFont(self->m_uiFont);
        // Force layout update — status bar may have changed height at new DPI
        self->LayoutChildren();
        return 0;
    }

    case WM_SETTINGCHANGE:
    {
        if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0)
            self->RefreshTheme();
        break;
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

    // Background brush for all child static/edit controls (theme-aware)
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    {
        HDC childDC = reinterpret_cast<HDC>(wParam);
        SetBkColor(childDC, Colors::Background);
        SetTextColor(childDC, Colors::TextPrimary);
        return reinterpret_cast<LRESULT>(Theme::GetBackgroundBrush());
    }

    case WM_NCPAINT:
    {
        DefWindowProcW(hwnd, msg, wParam, lParam);
        self->PaintMenuBarBorder();
        return 0;
    }

    case WM_NCACTIVATE:
    {
        LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
        self->PaintMenuBarBorder();
        return result;
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
        if (self->m_uiFont)
        {
            DeleteObject(self->m_uiFont);
            self->m_uiFont = nullptr;
        }
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
        self->m_leftDragging = true;
        self->m_lastDragX = GET_X_LPARAM(lParam);
        self->m_lastDragY = GET_Y_LPARAM(lParam);
        SetCapture(hwnd);
        return 0;

    case WM_LBUTTONUP:
        if (self->m_leftDragging)
        {
            self->m_leftDragging = false;
            if (!self->m_middleDragging)
                ReleaseCapture();
        }
        return 0;

    case WM_SETFOCUS:
        self->m_viewportFocused = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_KILLFOCUS:
        self->m_viewportFocused = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_PAINT:
    {
        // Draw a focus border over the D3D content when the viewport has focus.
        // D3D11 renders via DirectComposition, so GDI paint sits underneath it —
        // but we can use WM_PAINT to draw a border frame that the compositor
        // won't overwrite at the very edges.
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self->m_viewportFocused)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int dpi = GetDpiForWindow(hwnd);
            int borderW = MulDiv(2, dpi, 96);
            HBRUSH focusBrush = CreateSolidBrush(Colors::FocusBorder);
            // Top
            RECT r = {rc.left, rc.top, rc.right, rc.top + borderW};
            FillRect(hdc, &r, focusBrush);
            // Bottom
            r = {rc.left, rc.bottom - borderW, rc.right, rc.bottom};
            FillRect(hdc, &r, focusBrush);
            // Left
            r = {rc.left, rc.top, rc.left + borderW, rc.bottom};
            FillRect(hdc, &r, focusBrush);
            // Right
            r = {rc.right - borderW, rc.top, rc.right, rc.bottom};
            FillRect(hdc, &r, focusBrush);
            DeleteObject(focusBrush);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        bool ctrl = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
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

        if (self->m_leftDragging || self->m_middleDragging)
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
        if (!self->m_leftDragging)
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
        POINTER_INFO pi = {};
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
            for (int i = 0; i < self->m_touchCount; ++i)
            {
                cx += self->m_touches[i].x;
                cy += self->m_touches[i].y;
            }
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
                for (int i = 0; i < self->m_touchCount; ++i)
                {
                    cx += self->m_touches[i].x;
                    cy += self->m_touches[i].y;
                }
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
                for (int i = 0; i < self->m_touchCount; ++i)
                {
                    cx += self->m_touches[i].x;
                    cy += self->m_touches[i].y;
                }
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

LRESULT CALLBACK Window::TabBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*subclassId*/,
                                    DWORD_PTR refData)
{
    auto* self = reinterpret_cast<Window*>(refData);

    switch (msg)
    {
    case WM_MBUTTONDOWN:
    {
        TCHITTESTINFO hitInfo = {};
        hitInfo.pt.x = GET_X_LPARAM(lParam);
        hitInfo.pt.y = GET_Y_LPARAM(lParam);
        int tab = TabCtrl_HitTest(hwnd, &hitInfo);
        if (tab >= 0 && self->onTabClose)
            self->onTabClose(tab);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        // Track whether mouse is over the active tab's close button
        int activeSel = TabCtrl_GetCurSel(hwnd);
        bool hovering = false;
        if (activeSel >= 0)
        {
            int dpi = GetDpiForWindow(hwnd);
            int btnSize = MulDiv(kTabCloseBtnSize, dpi, 96);
            int btnMargin = MulDiv(kTabCloseBtnMargin, dpi, 96);
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT tabRect;
            TabCtrl_GetItemRect(hwnd, activeSel, &tabRect);
            int accentH = MulDiv(3, dpi, 96);
            int btnX = tabRect.right - btnSize - btnMargin;
            int btnY = (tabRect.top + tabRect.bottom - accentH - btnSize) / 2 + 1;
            hovering = (mx >= btnX && mx < btnX + btnSize && my >= btnY && my < btnY + btnSize);
        }
        if (hovering != self->m_closeButtonHover)
        {
            self->m_closeButtonHover = hovering;
            InvalidateRect(hwnd, nullptr, FALSE);
            // Request WM_MOUSELEAVE so we can clear hover when mouse leaves the tab bar
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        if (self->m_closeButtonHover)
        {
            self->m_closeButtonHover = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        // Check if click is on the active tab's close button
        int activeSel = TabCtrl_GetCurSel(hwnd);
        if (activeSel >= 0)
        {
            int dpi = GetDpiForWindow(hwnd);
            int btnSize = MulDiv(kTabCloseBtnSize, dpi, 96);
            int btnMargin = MulDiv(kTabCloseBtnMargin, dpi, 96);
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            RECT tabRect;
            TabCtrl_GetItemRect(hwnd, activeSel, &tabRect);
            int accentH = MulDiv(3, dpi, 96);
            int btnX = tabRect.right - btnSize - btnMargin;
            int btnY = (tabRect.top + tabRect.bottom - accentH - btnSize) / 2 + 1;
            if (mx >= btnX && mx < btnX + btnSize && my >= btnY && my < btnY + btnSize)
            {
                if (self->onTabClose)
                    self->onTabClose(activeSel);
                return 0;
            }
        }
        break; // let default tab selection happen
    }

    case WM_PAINT:
    {
        // Full custom painting — bypasses the tab control's own drawing entirely,
        // eliminating 3D borders that can't be suppressed via owner-draw alone.
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, Theme::GetBackgroundBrush());

        int count = TabCtrl_GetItemCount(hwnd);
        int activeSel = TabCtrl_GetCurSel(hwnd);
        int dpi = GetDpiForWindow(hwnd);

        for (int i = 0; i < count; i++)
        {
            RECT tabRect;
            TabCtrl_GetItemRect(hwnd, i, &tabRect);
            bool isActive = (i == activeSel);

            // Tab background
            COLORREF bgColor = isActive ? Colors::TabActive : Colors::Background;
            HBRUSH bg = CreateSolidBrush(bgColor);
            FillRect(hdc, &tabRect, bg);
            DeleteObject(bg);

            // Bottom accent line on active tab
            if (isActive)
            {
                RECT accent = tabRect;
                accent.top = accent.bottom - MulDiv(2, dpi, 96);
                HBRUSH accentBr = CreateSolidBrush(Colors::Selection);
                FillRect(hdc, &accent, accentBr);
                DeleteObject(accentBr);
            }

            // Get tab text
            wchar_t text[128] = {};
            TCITEMW tci = {};
            tci.mask = TCIF_TEXT;
            tci.pszText = text;
            tci.cchTextMax = _countof(text);
            TabCtrl_GetItem(hwnd, i, &tci);

            // Draw text
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, isActive ? Colors::TextBright : Colors::TextDim);
            HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, self->m_uiFont));
            RECT textRect = tabRect;
            textRect.left += MulDiv(6, dpi, 96);
            int btnSize = MulDiv(kTabCloseBtnSize, dpi, 96);
            int btnMargin = MulDiv(kTabCloseBtnMargin, dpi, 96);
            if (isActive)
                textRect.right -= btnSize + btnMargin * 2;
            else
                textRect.right -= MulDiv(4, dpi, 96);
            DrawTextW(hdc, text, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(hdc, oldFont);

            // Close button on active tab — centered vertically above the accent line
            if (isActive)
            {
                int accentH = MulDiv(3, dpi, 96); // accent(2) + 1px clearance
                int btnX = tabRect.right - btnSize - btnMargin;
                int btnY = (tabRect.top + tabRect.bottom - accentH - btnSize) / 2 + 1;

                // Hover background: rounded rectangle behind the X
                if (self->m_closeButtonHover)
                {
                    int inset = MulDiv(1, dpi, 96);
                    RECT hoverRect = {btnX - inset, btnY - inset, btnX + btnSize + inset + 1,
                                      btnY + btnSize + inset + 1};
                    int radius = MulDiv(3, dpi, 96);
                    // Shift the tab background toward mid-gray for a visible hover
                    COLORREF tabBg = Colors::TabActive;
                    int r = GetRValue(tabBg), g = GetGValue(tabBg), b = GetBValue(tabBg);
                    if (Theme::IsDark())
                    {
                        r += 30;
                        g += 30;
                        b += 30;
                    }
                    else
                    {
                        r -= 30;
                        g -= 30;
                        b -= 30;
                    }
                    r = (std::max)(0, (std::min)(255, r));
                    g = (std::max)(0, (std::min)(255, g));
                    b = (std::max)(0, (std::min)(255, b));
                    HBRUSH hoverBr = CreateSolidBrush(RGB(r, g, b));
                    HPEN nullPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
                    HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, hoverBr));
                    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, nullPen));
                    RoundRect(hdc, hoverRect.left, hoverRect.top, hoverRect.right + 1, hoverRect.bottom + 1, radius,
                              radius);
                    SelectObject(hdc, oldBr);
                    SelectObject(hdc, oldPen);
                    DeleteObject(hoverBr);
                }

                // Draw X with brighter color on hover
                COLORREF xColor = self->m_closeButtonHover ? Colors::TextBright : Colors::TextDim;
                int pad = MulDiv(3, dpi, 96);
                int x0 = btnX + pad, y0 = btnY + pad;
                int x1 = btnX + btnSize - pad, y1 = btnY + btnSize - pad;
                HBRUSH xBrush = CreateSolidBrush(xColor);
                int steps = x1 - x0;
                if (steps > 0)
                {
                    int penW = MulDiv(1, dpi, 96);
                    for (int s = 0; s <= steps; s++)
                    {
                        int cx = x0 + s;
                        int cy0 = y0 + (y1 - y0) * s / steps;
                        int cy1 = y1 - (y1 - y0) * s / steps;
                        RECT r0 = {cx, cy0, cx + penW, cy0 + penW};
                        RECT r1 = {cx, cy1, cx + penW, cy1 + penW};
                        FillRect(hdc, &r0, xBrush);
                        FillRect(hdc, &r1, xBrush);
                    }
                }
                DeleteObject(xBrush);
            }

            // Vertical separator between tabs (skip if next tab is active or this is active)
            if (!isActive && i + 1 < count && i + 1 != activeSel)
            {
                RECT sep = {tabRect.right - 1, tabRect.top + MulDiv(4, dpi, 96), tabRect.right,
                            tabRect.bottom - MulDiv(4, dpi, 96)};
                HBRUSH sepBr = CreateSolidBrush(Colors::Separator);
                FillRect(hdc, &sep, sepBr);
                DeleteObject(sepBr);
            }
        }

        // Bottom border below entire tab strip
        {
            HBRUSH borderBr = CreateSolidBrush(Colors::Separator);
            RECT bottom = {rc.left, rc.bottom - 1, rc.right, rc.bottom};
            FillRect(hdc, &bottom, borderBr);
            DeleteObject(borderBr);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Splitter proc for resizable sidebar
LRESULT CALLBACK Window::SplitterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
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
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        // Draw separator line in the middle of the splitter
        HBRUSH sepBrush = CreateSolidBrush(Colors::Separator);
        RECT line = {rc.right / 2, rc.top, rc.right / 2 + 1, rc.bottom};
        FillRect(hdc, &line, sepBrush);
        DeleteObject(sepBrush);
        // Fill the rest with background
        RECT left = {rc.left, rc.top, rc.right / 2, rc.bottom};
        RECT right = {rc.right / 2 + 1, rc.top, rc.right, rc.bottom};
        FillRect(hdc, &left, Theme::GetBackgroundBrush());
        FillRect(hdc, &right, Theme::GetBackgroundBrush());
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        self->m_sidebarDragging = true;
        SetCapture(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        if (self->m_sidebarDragging)
        {
            // Convert mouse position to main window client coords
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            MapWindowPoints(hwnd, self->m_hwnd, &pt, 1);

            RECT rc;
            GetClientRect(self->m_hwnd, &rc);
            int totalW = rc.right - rc.left;

            // Sidebar width = total width - mouse X - splitter width
            int dpi = GetDpiForWindow(self->m_hwnd);
            int newSidebarPx = totalW - pt.x - kSplitterWidth;
            // Convert to DPI-independent base width
            int newBase = MulDiv(newSidebarPx, 96, dpi);
            newBase = (std::max)(kMinSidebarWidth, (std::min)(newBase, kMaxSidebarWidth));
            if (newBase != self->m_sidebarBaseWidth)
            {
                self->m_sidebarBaseWidth = newBase;
                self->LayoutChildren();
            }
        }
        return 0;

    case WM_LBUTTONUP:
        self->m_sidebarDragging = false;
        ReleaseCapture();
        return 0;

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Status bar subclass — custom painting eliminates 3D borders
LRESULT CALLBACK Window::StatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*subclassId*/,
                                       DWORD_PTR refData)
{
    auto* self = reinterpret_cast<Window*>(refData);

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        // Flat background
        FillRect(hdc, &rc, Theme::GetBackgroundBrush());

        // Top separator line
        HBRUSH sepBrush = CreateSolidBrush(Colors::Separator);
        RECT sep = {rc.left, rc.top, rc.right, rc.top + 1};
        FillRect(hdc, &sep, sepBrush);

        // Draw each part
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, Colors::TextPrimary);
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, self->m_uiFont));

        for (int i = 0; i < 3; i++)
        {
            RECT partRect;
            SendMessageW(hwnd, SB_GETRECT, i, reinterpret_cast<LPARAM>(&partRect));

            // Subtle separator between parts
            if (i > 0)
            {
                RECT pSep = {partRect.left, partRect.top + 3, partRect.left + 1, partRect.bottom - 3};
                FillRect(hdc, &pSep, sepBrush);
            }

            const wchar_t* text = self->m_statusText[i].c_str();
            if (text && text[0])
            {
                RECT textRect = partRect;
                textRect.left += 4;
                DrawTextW(hdc, text, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            }
        }

        SelectObject(hdc, oldFont);
        DeleteObject(sepBrush);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void Window::PaintMenuBarBorder()
{
    // Paint over the 1px system border at the bottom of the menu bar
    if (!GetMenu(m_hwnd))
        return;
    HDC hdc = GetWindowDC(m_hwnd);
    if (!hdc)
        return;
    RECT windowRect, clientRect;
    GetWindowRect(m_hwnd, &windowRect);
    GetClientRect(m_hwnd, &clientRect);
    MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&clientRect), 2);
    int menuBottom = clientRect.top - windowRect.top;
    RECT borderRect = {0, menuBottom - 1, windowRect.right - windowRect.left, menuBottom};
    HBRUSH brush = CreateSolidBrush(Colors::Background);
    FillRect(hdc, &borderRect, brush);
    DeleteObject(brush);
    ReleaseDC(m_hwnd, hdc);
}

void Window::RefreshTheme()
{
    // Update colors and brush from system preference
    Theme::Refresh();

    // Re-apply dark/light mode to window chrome
    Theme::EnableForWindow(m_hwnd);

    // Re-theme all common controls
    Theme::ApplyToControl(m_statusBar);
    Theme::ApplyToControl(m_tabBar);
    m_sidebar.RefreshTheme();

    // Update menu bar background brush and force repaint
    HMENU menu = GetMenu(m_hwnd);
    if (!menu)
        menu = m_savedMenu;
    if (menu)
    {
        MENUINFO mi = {sizeof(mi)};
        mi.fMask = MIM_BACKGROUND;
        mi.hbrBack = Theme::GetBackgroundBrush();
        SetMenuInfo(menu, &mi);
    }
    DrawMenuBar(m_hwnd);

    // Repaint everything
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}
