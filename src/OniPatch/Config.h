#pragma once

// Config.h - reads OniPatch.ini (next to OniPatch.dll) and applies the display
// mode to the D3D9 present parameters. Header-only: just drop it in src/OniPatch.

#include <windows.h>
#include <commctrl.h>
#include <d3d9.h>
#include <cstdarg>
#include <cstdio>
#include <string>

#pragma comment(lib, "comctl32.lib")

enum class DisplayMode { Windowed, Borderless, Fullscreen };
enum class AspectMode { Stretch, Ratio4x3 };

struct OniConfig {
    DisplayMode mode = DisplayMode::Borderless;
    AspectMode aspect = AspectMode::Ratio4x3;  // 4:3 keeps the game's native proportions
    int width = 0;        // 0 = desktop resolution (windowed: 1280). With 4:3 this is the bounding box.
    int height = 0;       // 0 = desktop resolution (windowed: 720)
    int refreshRate = 0;  // fullscreen only; 0 = desktop refresh rate
    bool log = false;     // [Debug] Log=1 writes OniPatch.log next to the DLL
    bool console = false; // [Debug] Console=1 opens a console window in the game process
    bool hideCursor = false; // [Display] HideCursor=1 hides the mouse cursor over the game
};

inline const OniConfig& GetConfig()
{
    static const OniConfig cfg = []() {
        OniConfig c;

        // Locate OniPatch.ini next to this DLL (not next to the exe's cwd).
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetConfig), &self);
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(self, path, MAX_PATH);
        std::wstring ini(path);
        size_t slash = ini.find_last_of(L"\\/");
        ini = (slash == std::wstring::npos ? std::wstring() : ini.substr(0, slash + 1)) + L"OniPatch.ini";

        wchar_t buf[32] = {};
        GetPrivateProfileStringW(L"Display", L"Mode", L"borderless", buf, 32, ini.c_str());
        if (!_wcsicmp(buf, L"windowed"))        c.mode = DisplayMode::Windowed;
        else if (!_wcsicmp(buf, L"fullscreen")) c.mode = DisplayMode::Fullscreen;
        else                                    c.mode = DisplayMode::Borderless;

        wchar_t abuf[32] = {};
        GetPrivateProfileStringW(L"Display", L"AspectRatio", L"4:3", abuf, 32, ini.c_str());
        if (!_wcsicmp(abuf, L"stretch") || !_wcsicmp(abuf, L"stretched") || !_wcsicmp(abuf, L"off"))
            c.aspect = AspectMode::Stretch;
        else
            c.aspect = AspectMode::Ratio4x3;

        c.width = GetPrivateProfileIntW(L"Display", L"Width", 0, ini.c_str());
        c.height = GetPrivateProfileIntW(L"Display", L"Height", 0, ini.c_str());
        c.refreshRate = GetPrivateProfileIntW(L"Display", L"RefreshRate", 0, ini.c_str());
        c.log = GetPrivateProfileIntW(L"Debug", L"Log", 0, ini.c_str()) != 0;
        c.console = GetPrivateProfileIntW(L"Debug", L"Console", 0, ini.c_str()) != 0;
        c.hideCursor = GetPrivateProfileIntW(L"Display", L"HideCursor", 0, ini.c_str()) != 0;
        return c;
        }();
    return cfg;
}

// ---------------------------------------------------------------------------
// Logging (only when [Debug] Log=1): OniPatch.log next to the DLL, rewritten each run.
// ---------------------------------------------------------------------------

inline void OniLog(const char* fmt, ...)
{
    if (!GetConfig().log)
        return;

    static FILE* f = []() -> FILE* {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetConfig), &self);
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(self, path, MAX_PATH);
        std::wstring p(path);
        size_t slash = p.find_last_of(L"\\/");
        p = (slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1)) + L"OniPatch.log";
        FILE* fp = nullptr;
        _wfopen_s(&fp, p.c_str(), L"w");
        return fp;
        }();
    if (!f)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fflush(f);
}

// ---------------------------------------------------------------------------
// Monitor / size helpers
// ---------------------------------------------------------------------------

// Full rectangle (taskbar included) of the monitor the window is on. With a
// null window, falls back to the primary monitor.
inline RECT GetMonitorRectFor(HWND hwnd)
{
    MONITORINFO mi = { sizeof(mi) };
    HMONITOR mon = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY)
        : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(mon, &mi)) {
        mi.rcMonitor = RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }
    return mi.rcMonitor;
}

// The resolution box from the INI (or the defaults), before any aspect fitting.
inline void GetBoundingSize(int& w, int& h)
{
    const OniConfig& c = GetConfig();
    const bool windowed = (c.mode == DisplayMode::Windowed);
    const RECT mon = GetMonitorRectFor(nullptr);
    w = c.width > 0 ? c.width : (windowed ? 1280 : mon.right - mon.left);
    h = c.height > 0 ? c.height : (windowed ? 720 : mon.bottom - mon.top);
}

// Back buffer / client size for the current config. With 4:3 this is the
// largest 4:3 size that fits inside the bounding box.
inline void GetBackBufferSize(int& w, int& h)
{
    GetBoundingSize(w, h);

    if (GetConfig().aspect == AspectMode::Ratio4x3) {
        int fitW = (int)((long long)h * 4 / 3);
        if (fitW > w) fitW = w;
        fitW &= ~1;                  // keep it even
        w = fitW;
        h = fitW * 3 / 4;
    }
}

// Where the borderless game window goes: the whole monitor for stretch, or the
// largest centered 4:3 rectangle for 4:3 (a black backdrop fills the rest).
inline RECT GetBorderlessRect(HWND hwnd)
{
    const RECT mon = GetMonitorRectFor(hwnd);
    if (GetConfig().aspect != AspectMode::Ratio4x3)
        return mon;

    const int mw = mon.right - mon.left;
    const int mh = mon.bottom - mon.top;
    int w = (int)((long long)mh * 4 / 3);
    int h = mh;
    if (w > mw) {
        w = mw;
        h = (int)((long long)mw * 3 / 4);
    }

    RECT r;
    r.left = mon.left + (mw - w) / 2;
    r.top = mon.top + (mh - h) / 2;
    r.right = r.left + w;
    r.bottom = r.top + h;
    return r;
}

// ---------------------------------------------------------------------------
// Hide cursor (HideCursor=1)
//
// Windows asks the window which cursor to show via WM_SETCURSOR on every mouse
// move; answering with "no cursor" over the client area hides it. The title bar
// and borders (windowed mode) keep their normal cursors.
// ---------------------------------------------------------------------------

// The game's own window (set when it is created).
inline HWND& GameWindow()
{
    static HWND h = nullptr;
    return h;
}

// True only while the game's own window is the foreground window. Other windows
// of the same process (the DirectInput mapper dialog, for example), the Alt+Tab
// switcher and other programs all get a normal cursor.
inline bool GameHasFocus()
{
    HWND game = GameWindow();
    return game != nullptr && GetForegroundWindow() == game;
}

inline LRESULT CALLBACK CursorSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR /*id*/, DWORD_PTR /*data*/)
{
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT && GameHasFocus()) {
        SetCursor(nullptr);
        return TRUE;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, CursorSubclassProc, 3);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

inline void HideCursorOnWindow(HWND hwnd)
{
    if (hwnd && GetConfig().hideCursor)
        SetWindowSubclass(hwnd, CursorSubclassProc, 3, 0);
}

// ---------------------------------------------------------------------------
// Borderless window pinning
//
// The borderless game window is pinned to GetBorderlessRect (whole monitor, or
// a centered 4:3 area). The subclass overrides any resize the game attempts,
// PinWindowToMonitor re-asserts the position after device creation / Reset, and
// EnforceBorderless is a periodic safety net.
//
// For 4:3, a separate black "backdrop" window covers the whole monitor
// (taskbar included) directly behind the game window, forming the black bars.
// D3D then simply stretches the back buffer to the 4:3 game window, which
// works with any swap effect / multisampling.
// ---------------------------------------------------------------------------

inline bool UsesBackdrop()
{
    const OniConfig& c = GetConfig();
    return c.mode == DisplayMode::Borderless && c.aspect == AspectMode::Ratio4x3;
}

// True while the game is the active app. The window and backdrop are only
// topmost while active, so Alt+Tab to other programs works.
inline bool& GameActive()
{
    static bool active = true;
    return active;
}

inline HWND& BackdropWindow()
{
    static HWND h = nullptr;
    return h;
}

inline LRESULT CALLBACK BackdropProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    if (msg == WM_SETCURSOR && GetConfig().hideCursor && GameHasFocus()) {
        SetCursor(nullptr);   // HideCursor=1 also applies over the black bars
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Creates the backdrop if needed and places it directly behind the game window
// (topmost while the game is active, normal z-order otherwise).
inline void SyncBackdrop(HWND game)
{
    if (!game || !UsesBackdrop())
        return;

    const RECT mon = GetMonitorRectFor(game);
    HWND& bd = BackdropWindow();

    if (!bd || !IsWindow(bd)) {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        WNDCLASSW wc = {};
        wc.lpfnWndProc = BackdropProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = L"OniPatchBackdrop";
        RegisterClassW(&wc);  // already-registered is fine

        bd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"OniPatchBackdrop", L"", WS_POPUP,
            mon.left, mon.top, mon.right - mon.left, mon.bottom - mon.top,
            nullptr, nullptr, inst, nullptr);
        OniLog("Backdrop created: %p", (void*)bd);
    }
    if (!bd)
        return;

    SetWindowPos(bd, game, mon.left, mon.top, mon.right - mon.left, mon.bottom - mon.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

inline LRESULT CALLBACK BorderlessSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR /*id*/, DWORD_PTR /*data*/)
{
    switch (msg) {
    case WM_WINDOWPOSCHANGING: {
        auto* wpos = reinterpret_cast<WINDOWPOS*>(lp);

        // Let minimize / hide through untouched (alt-tab).
        if (IsIconic(hwnd) || (wpos->flags & SWP_HIDEWINDOW) || wpos->x <= -32000)
            break;

#ifdef ONI_DEBUG_WINPOS
        char dbg[128];
        sprintf_s(dbg, "OniPatch: WINDOWPOSCHANGING x=%d y=%d cx=%d cy=%d flags=0x%X\n",
            wpos->x, wpos->y, wpos->cx, wpos->cy, wpos->flags);
        OutputDebugStringA(dbg);
#endif

        const RECT r = GetBorderlessRect(hwnd);
        wpos->x = r.left;
        wpos->y = r.top;
        wpos->cx = r.right - r.left;
        wpos->cy = r.bottom - r.top;
        wpos->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        break;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        const RECT r = GetBorderlessRect(hwnd);
        mmi->ptMaxSize.x = mmi->ptMaxTrackSize.x = r.right - r.left;
        mmi->ptMaxSize.y = mmi->ptMaxTrackSize.y = r.bottom - r.top;
        mmi->ptMaxPosition.x = r.left;
        mmi->ptMaxPosition.y = r.top;
        return 0;
    }
    case WM_ACTIVATEAPP: {
        const LRESULT res = DefSubclassProc(hwnd, msg, wp, lp);
        const bool active = (wp != FALSE);
        GameActive() = active;
        OniLog("WM_ACTIVATEAPP active=%d", (int)active);

        if (active) {
            const RECT r = GetBorderlessRect(hwnd);
            SetWindowPos(hwnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                SWP_SHOWWINDOW | SWP_NOACTIVATE);
            SyncBackdrop(hwnd);
        }
        else {
            // Step aside: drop topmost so other windows can come forward, but keep
            // the black backdrop directly behind the game so the bars stay visible.
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SyncBackdrop(hwnd);
        }
        return res;
    }
    case WM_SIZE:
        if (BackdropWindow() && IsWindow(BackdropWindow())) {
            if (wp == SIZE_MINIMIZED)
                ShowWindow(BackdropWindow(), SW_HIDE);
            else
                SyncBackdrop(hwnd);
        }
        break;
    case WM_NCDESTROY:
        if (BackdropWindow() && IsWindow(BackdropWindow()))
            DestroyWindow(BackdropWindow());
        BackdropWindow() = nullptr;
        RemoveWindowSubclass(hwnd, BorderlessSubclassProc, 1);
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// Subclasses the window (once) and snaps it into place. No-op unless the
// config is borderless.
inline void PinWindowToMonitor(HWND hwnd)
{
    if (!hwnd || GetConfig().mode != DisplayMode::Borderless)
        return;

    SetWindowSubclass(hwnd, BorderlessSubclassProc, 1, 0);

    const RECT r = GetBorderlessRect(hwnd);
    const bool active = GameActive();
    SetWindowPos(hwnd, active ? HWND_TOPMOST : HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
        SWP_FRAMECHANGED | (active ? SWP_SHOWWINDOW : SWP_NOACTIVATE));

    SyncBackdrop(hwnd);
}

// Cheap per-frame sanity check: if the window drifted (style, rect, topmost),
// put it back. Does not touch focus, so alt-tab keeps working.
inline void EnforceBorderless(HWND hwnd)
{
    if (!hwnd || GetConfig().mode != DisplayMode::Borderless || IsIconic(hwnd) || !GameActive())
        return;

    if (UsesBackdrop() && (!BackdropWindow() || !IsWindow(BackdropWindow())))
        SyncBackdrop(hwnd);

    const RECT target = GetBorderlessRect(hwnd);
    RECT win = {};
    GetWindowRect(hwnd, &win);

    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    const LONG frameBits = WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME;
    const bool badStyle = (style & frameBits) != 0;
    const bool badRect = !EqualRect(&target, &win);
    const bool notTop = !(GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST);

    if (!badStyle && !badRect && !notTop)
        return;

#ifdef ONI_DEBUG_WINPOS
    char dbg[192];
    sprintf_s(dbg, "OniPatch: drift win=(%ld,%ld,%ld,%ld) target=(%ld,%ld,%ld,%ld) style=0x%lX\n",
        win.left, win.top, win.right, win.bottom,
        target.left, target.top, target.right, target.bottom, (unsigned long)style);
    OutputDebugStringA(dbg);
#endif

    if (badStyle)
        SetWindowLongW(hwnd, GWL_STYLE, (style & ~frameBits) | WS_POPUP);

    SetWindowPos(hwnd, HWND_TOPMOST, target.left, target.top, target.right - target.left, target.bottom - target.top,
        SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    SyncBackdrop(hwnd);
}

// ---------------------------------------------------------------------------
// Present parameters
// ---------------------------------------------------------------------------

// Exclusive fullscreen can only use modes the adapter reports.
//  - stretch: use the requested size if it exists, else the desktop mode.
//  - 4:3:     use the largest real 4:3 mode inside the bounding box, else the
//             desktop mode (the GPU/monitor then decides how to scale).
inline void ChooseFullscreenMode(IDirect3D9* d3d, UINT adapter, const D3DDISPLAYMODE& desktop,
    int boundW, int boundH, bool ratio43,
    int& w, int& h, int& refresh)
{
    const UINT count = d3d->GetAdapterModeCount(adapter, desktop.Format);

    long long bestArea = 0;
    int bestW = 0, bestH = 0;

    for (UINT i = 0; i < count; i++) {
        D3DDISPLAYMODE m = {};
        if (FAILED(d3d->EnumAdapterModes(adapter, desktop.Format, i, &m)))
            continue;

        const bool fits = ratio43
            ? ((int)m.Width * 3 == (int)m.Height * 4 && (int)m.Width <= boundW && (int)m.Height <= boundH)
            : ((int)m.Width == boundW && (int)m.Height == boundH);
        if (!fits)
            continue;

        const long long area = (long long)m.Width * m.Height;
        if (area > bestArea) {
            bestArea = area;
            bestW = (int)m.Width;
            bestH = (int)m.Height;
        }
    }

    if (bestArea == 0) {
        w = (int)desktop.Width;
        h = (int)desktop.Height;
        refresh = (int)desktop.RefreshRate;
        return;
    }

    // Pick the refresh rate for the chosen size.
    int bestRefresh = 0;
    bool exactRefresh = false;
    for (UINT i = 0; i < count; i++) {
        D3DDISPLAYMODE m = {};
        if (FAILED(d3d->EnumAdapterModes(adapter, desktop.Format, i, &m)))
            continue;
        if ((int)m.Width != bestW || (int)m.Height != bestH)
            continue;
        if ((int)m.RefreshRate > bestRefresh) bestRefresh = (int)m.RefreshRate;
        if (refresh > 0 && (int)m.RefreshRate == refresh) exactRefresh = true;
    }

    w = bestW;
    h = bestH;
    if (!exactRefresh)
        refresh = bestRefresh;
}

// Rewrites the game's present parameters for the configured mode.
// Call from IDirect3D9::CreateDevice and IDirect3DDevice9::Reset.
inline void ApplyDisplayConfig(D3DPRESENT_PARAMETERS* p, IDirect3D9* d3d, UINT adapter)
{
    const OniConfig& c = GetConfig();

    D3DDISPLAYMODE desktop = {};
    if (FAILED(d3d->GetAdapterDisplayMode(adapter, &desktop)))
        return;

    OniLog("ApplyDisplayConfig IN : %ux%u fmt=%d windowed=%d swap=%d bbCount=%u msaa=%d hz=%u hDeviceWindow=%p",
        p->BackBufferWidth, p->BackBufferHeight, (int)p->BackBufferFormat, (int)p->Windowed,
        (int)p->SwapEffect, p->BackBufferCount, (int)p->MultiSampleType,
        p->FullScreen_RefreshRateInHz, (void*)p->hDeviceWindow);
    OniLog("  desktop mode: %ux%u @%u fmt=%d", desktop.Width, desktop.Height, desktop.RefreshRate, (int)desktop.Format);

    // The game creates throwaway 1x1 devices before the real one. Forcing
    // exclusive fullscreen on those would fight the real device, so leave them be.
    if (c.mode == DisplayMode::Fullscreen && p->BackBufferWidth == 1 && p->BackBufferHeight == 1) {
        OniLog("ApplyDisplayConfig: 1x1 probe device, leaving untouched");
        return;
    }

    int w = 0, h = 0;
    GetBackBufferSize(w, h);

    p->BackBufferFormat = desktop.Format;  // a 2005 game may ask for 16-bit

    if (c.mode == DisplayMode::Fullscreen) {
        int boundW = 0, boundH = 0;
        GetBoundingSize(boundW, boundH);

        int refresh = c.refreshRate > 0 ? c.refreshRate : (int)desktop.RefreshRate;
        ChooseFullscreenMode(d3d, adapter, desktop, boundW, boundH,
            c.aspect == AspectMode::Ratio4x3, w, h, refresh);

        p->Windowed = FALSE;
        p->FullScreen_RefreshRateInHz = (UINT)refresh;
    }
    else {
        // Windowed and borderless both render into a window; D3D stretches the
        // back buffer to the window if the sizes differ.
        p->Windowed = TRUE;
        p->FullScreen_RefreshRateInHz = 0;

        if (p->SwapEffect == D3DSWAPEFFECT_FLIP) {
            // The stretch only happens for DISCARD/COPY.
            p->SwapEffect = D3DSWAPEFFECT_DISCARD;
        }
    }

    p->BackBufferWidth = (UINT)w;
    p->BackBufferHeight = (UINT)h;

    OniLog("ApplyDisplayConfig OUT: %ux%u fmt=%d windowed=%d swap=%d bbCount=%u msaa=%d hz=%u",
        p->BackBufferWidth, p->BackBufferHeight, (int)p->BackBufferFormat, (int)p->Windowed,
        (int)p->SwapEffect, p->BackBufferCount, (int)p->MultiSampleType,
        p->FullScreen_RefreshRateInHz);
}

// Used when an exclusive-fullscreen CreateDevice/Reset is rejected: drop back to
// a windowed device (the window is already a monitor-sized popup) so the game
// keeps running instead of getting a null device.
inline void ApplyWindowedFallback(D3DPRESENT_PARAMETERS* p)
{
    int w = 0, h = 0;
    GetBackBufferSize(w, h);

    p->BackBufferWidth = (UINT)w;
    p->BackBufferHeight = (UINT)h;
    p->Windowed = TRUE;
    p->FullScreen_RefreshRateInHz = 0;
    if (p->SwapEffect == D3DSWAPEFFECT_FLIP)
        p->SwapEffect = D3DSWAPEFFECT_DISCARD;
}