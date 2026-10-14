#include "pch.h"
#include "MyIDirect3D9.h"
#include "Config.h"

MyIDirect3D9::MyIDirect3D9(IDirect3D9 *original)
{
    this->original = original;

    this->Version = this->original->Version;
}

MyIDirect3D9::~MyIDirect3D9() {}

/*** IUnknown methods ***/
HRESULT MyIDirect3D9::QueryInterface(THIS_ REFIID riid, void **ppvObj)
{
    return this->original->QueryInterface(riid, ppvObj);
}

ULONG MyIDirect3D9::AddRef(THIS)
{
    return this->original->AddRef();
}

ULONG MyIDirect3D9::Release(THIS)
{
    return this->original->Release();
}

/*** IDirect3D9 methods ***/
HRESULT MyIDirect3D9::RegisterSoftwareDevice(THIS_ void *pInitializeFunction)
{
    return this->original->RegisterSoftwareDevice(pInitializeFunction);
}

UINT MyIDirect3D9::GetAdapterCount(THIS)
{
    return this->original->GetAdapterCount();
}

HRESULT MyIDirect3D9::GetAdapterIdentifier(THIS_ UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier)
{
    return this->original->GetAdapterIdentifier(Adapter, Flags, pIdentifier);
}

UINT MyIDirect3D9::GetAdapterModeCount(THIS_ UINT Adapter, D3DFORMAT Format)
{
    TraceFunc();
    TraceParam("Adapter", Adapter);
    TraceParam("D3DFORMAT", Format);

    return this->original->GetAdapterModeCount(Adapter, Format);
}

HRESULT MyIDirect3D9::EnumAdapterModes(THIS_ UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE *pMode)
{
    HRESULT hResult = this->original->EnumAdapterModes(Adapter, Format, Mode, pMode);

    if (SUCCEEDED(hResult) && pMode != nullptr) {
        TraceFunc();
        TraceParam("Adapter", Adapter);
        TraceParam("D3DFORMAT", Format);
        TraceParam("Mode", Mode);
        TraceParam("d3dDisplayMode Width", pMode->Width);
        TraceParam("d3dDisplayMode Height", pMode->Height);
        TraceParam("d3dDisplayMode Refresh", pMode->RefreshRate);
    }

    return hResult;
}

HRESULT MyIDirect3D9::GetAdapterDisplayMode(THIS_ UINT Adapter, D3DDISPLAYMODE *pMode)
{
    return this->original->GetAdapterDisplayMode(Adapter, pMode);
}

HRESULT MyIDirect3D9::CheckDeviceType(THIS_ UINT iAdapter, D3DDEVTYPE DevType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed)
{
    return this->original->CheckDeviceType(iAdapter, DevType, DisplayFormat, BackBufferFormat, bWindowed);
}

HRESULT MyIDirect3D9::CheckDeviceFormat(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
{
    return this->original->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
}

HRESULT MyIDirect3D9::CheckDeviceMultiSampleType(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels)
{
    return this->original->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
}

HRESULT MyIDirect3D9::CheckDepthStencilMatch(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat)
{
    return this->original->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
}

HRESULT MyIDirect3D9::CheckDeviceFormatConversion(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat)
{
    return this->original->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat);
}

HRESULT MyIDirect3D9::GetDeviceCaps(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps)
{
    return this->original->GetDeviceCaps(Adapter, DeviceType, pCaps);
}

HMONITOR MyIDirect3D9::GetAdapterMonitor(THIS_ UINT Adapter)
{
    return this->original->GetAdapterMonitor(Adapter);
}

HRESULT MyIDirect3D9::CreateDevice(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DDevice9 **ppReturnedDeviceInterface)
{
    TraceFunc();

    if (pPresentationParameters == nullptr) {
        return this->original->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    }

    TraceParam("BackBufferWidth", pPresentationParameters->BackBufferWidth);
    TraceParam("BackBufferHeight", pPresentationParameters->BackBufferHeight);
    TraceParam("Windowed", pPresentationParameters->Windowed);
    TraceParam("MultiSampleType", pPresentationParameters->MultiSampleType);
    TraceParam("MultiSampleQuality", pPresentationParameters->MultiSampleQuality);
    TraceParam("SwapEffect", pPresentationParameters->SwapEffect);
    TraceParam("AutoDepthStencilFormat", pPresentationParameters->AutoDepthStencilFormat);
    TraceParam("PresentationInterval", pPresentationParameters->PresentationInterval);

    // Always apply the configured display mode (not just for the game's default
    // 800 wide request), so the back buffer and window can't get out of sync.
    TraceMsg("Applying display config");
    ApplyDisplayConfig(pPresentationParameters, this->original, Adapter);

    IDirect3DDevice9 *trueDevice = nullptr;

    const bool forcedExclusive = (GetConfig().mode == DisplayMode::Fullscreen) && !pPresentationParameters->Windowed;

    HRESULT hResult = this->original->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, &trueDevice);

    if (FAILED(hResult) && forcedExclusive) {
        OniLog("Exclusive CreateDevice failed hr=0x%08lX, retrying windowed", (unsigned long)hResult);
        ApplyWindowedFallback(pPresentationParameters);
        trueDevice = nullptr;
        hResult = this->original->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, &trueDevice);
    }

    OniLog("CreateDevice -> hr=0x%08lX behavior=0x%lX focusWnd=%p", (unsigned long)hResult, (unsigned long)BehaviorFlags, (void *)hFocusWindow);

    if (SUCCEEDED(hResult) && trueDevice != nullptr) {
        // The game (or D3D) may have resized the window while creating the device.
        HWND hwnd = pPresentationParameters->hDeviceWindow ? pPresentationParameters->hDeviceWindow : hFocusWindow;
        PinWindowToMonitor(hwnd);

        (*ppReturnedDeviceInterface) = new MyIDirect3DDevice9(trueDevice);
    }
    else {
        (*ppReturnedDeviceInterface) = nullptr;
    }

    return hResult;
}
