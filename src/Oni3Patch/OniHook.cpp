#include "pch.h"
#include "OniHook.h"
#include "Config.h"

//#pragma comment(linker, "/export:DetourFinishHelperProcess,@1,NONAME")

// Detours requires the patch DLL to export something at ordinal #1.
extern "C" void CALLBACK DetourOrdinal1(HWND, HINSTANCE, LPSTR, int) {}

#ifdef _WIN64
#pragma comment(linker, "/export:DetourOrdinal1,@1,NONAME")
#else
#pragma comment(linker, "/export:DetourOrdinal1=_DetourOrdinal1@16,@1,NONAME")
#endif

CreateWindowExWType OniHook::TrueCreateWindowExW = nullptr;
Direct3DCreate9Type OniHook::TrueDirect3DCreate9 = nullptr;
SetCursorType OniHook::TrueSetCursor = nullptr;
OniHook* OniHook::instance = nullptr;

HWND WINAPI OniHook::MyCreateWindowExW(
    DWORD dwExStyle,
    _In_opt_ LPCWSTR   lpClassName,
    _In_opt_ LPCWSTR   lpWindowName,
    _In_     DWORD     dwStyle,
    _In_     int       X,
    _In_     int       Y,
    _In_     int       nWidth,
    _In_     int       nHeight,
    _In_opt_ HWND      hWndParent,
    _In_opt_ HMENU     hMenu,
    _In_opt_ HINSTANCE hInstance,
    _In_opt_ LPVOID    lpParam
)
{
    TraceFunc();

    // A NULL window name is valid (other DLLs loaded in the process, such as SDL
    // in the gamepad fix, create unnamed helper windows). Pass those through.
    const bool isOniWindow = lpWindowName != NULL && !wcscmp(lpWindowName, L"oni3");

    if (lpWindowName != NULL) {
        TraceParam("Window name", lpWindowName);
    }

    if (isOniWindow) {
        TraceMsg("Oni window found");

        const OniConfig& cfg = GetConfig();
        dwExStyle = WS_EX_APPWINDOW;

        if (cfg.mode == DisplayMode::Windowed) {
            int cw = 0, ch = 0;
            GetBackBufferSize(cw, ch);   // already 4:3 if configured

            dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

            // Size of the title bar + borders
            RECT frame = { 0, 0, 0, 0 };
            AdjustWindowRectEx(&frame, dwStyle, FALSE, dwExStyle);
            const int frameW = frame.right - frame.left;
            const int frameH = frame.bottom - frame.top;

            // Usable desktop area (monitor minus taskbar)
            RECT work = {};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            const int availW = (work.right - work.left) - frameW;
            const int availH = (work.bottom - work.top) - frameH;

            // Shrink the client area to fit, keeping the aspect ratio.
            // The back buffer keeps its configured size and is scaled by D3D.
            double scale = 1.0;
            if (cw > availW) scale = (double)availW / cw;
            if (ch * scale > availH) scale = (double)availH / ch;
            cw = (int)(cw * scale);
            ch = (int)(ch * scale);

            nWidth = cw + frameW;
            nHeight = ch + frameH;
            X = work.left + ((work.right - work.left) - nWidth) / 2;
            Y = work.top + ((work.bottom - work.top) - nHeight) / 2;
        }
        else {
            // borderless and fullscreen: popup window covering the monitor.
            // Borderless: monitor-sized (or a centered 4:3 area). The window never
            // follows the configured resolution; only the back buffer uses Width/Height.
            const RECT mon = (cfg.mode == DisplayMode::Borderless) ? GetBorderlessRect(nullptr)
                : GetMonitorRectFor(nullptr);

            dwStyle = WS_POPUP | WS_SYSMENU;
            // Borderless is made topmost only while active (see PinWindowToMonitor).
            // Exclusive fullscreen is managed by D3D, so no forced topmost there.
            dwExStyle = WS_EX_APPWINDOW | (cfg.mode == DisplayMode::Borderless ? WS_EX_TOPMOST : 0);
            X = mon.left;
            Y = mon.top;
            nWidth = mon.right - mon.left;
            nHeight = mon.bottom - mon.top;
        }
    }

    HWND result = TrueCreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

    if (isOniWindow && result != NULL) {
        GameWindow() = result;

        // HideCursor=1: answer WM_SETCURSOR over the client area with "no cursor".
        HideCursorOnWindow(result);

        // Borderless only: keeps the window pinned over the monitor even if the
        // game or D3D tries to resize it. No-op for windowed / fullscreen.
        PinWindowToMonitor(result);
        if (GetConfig().mode == DisplayMode::Borderless) {
            SetForegroundWindow(result);
        }
    }

    if (result == NULL) {
        DWORD errorCode = GetLastError();
        std::cerr << "CreateWindowExW failed with error code " << errorCode << std::endl;
    }

    return result;
}

IDirect3D9* WINAPI OniHook::MyDirect3DCreate9(UINT SDKVersion)
{
    TraceFunc();
    IDirect3D9* d3d9 = TrueDirect3DCreate9(SDKVersion);
    if (d3d9 == nullptr) {
        return nullptr;
    }

    return new MyIDirect3D9(d3d9);
}

HCURSOR WINAPI OniHook::MySetCursor(HCURSOR hCursor)
{
    // Only attached when HideCursor=1: while the game has focus, whatever cursor it
    // asks for, show none. When it doesn't (Alt+Tab), leave the cursor alone.
    return TrueSetCursor(GameHasFocus() ? nullptr : hCursor);
}

void OniHook::LoadEntryPoints()
{
    TraceFunc();

    TrueCreateWindowExW = reinterpret_cast<CreateWindowExWType>(Utils::LoadProcAddress("user32", "CreateWindowExW"));
    TrueDirect3DCreate9 = reinterpret_cast<Direct3DCreate9Type>(Utils::LoadProcAddress("d3d9", "Direct3DCreate9"));
    TrueSetCursor = reinterpret_cast<SetCursorType>(Utils::LoadProcAddress("user32", "SetCursor"));
}

OniHook* OniHook::GetInstance()
{
    if (OniHook::instance == nullptr) {
        OniHook::instance = new OniHook();
    }

    return OniHook::instance;
}

void OniHook::Hook()
{
    TraceFunc();
    SetProcessDPIAware();
    LoadEntryPoints();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    DetourAttach((PVOID*)&TrueDirect3DCreate9, MyDirect3DCreate9);
    DetourAttach((PVOID*)&TrueCreateWindowExW, MyCreateWindowExW);
    if (GetConfig().hideCursor && TrueSetCursor != nullptr) {
        DetourAttach((PVOID*)&TrueSetCursor, MySetCursor);
    }

    if (DetourTransactionCommit() != NO_ERROR) {
        std::cerr << "Detour failed";
    }
}

void OniHook::UnHook()
{
}