// dsound.dll proxy: forwards every export to the real system dsound.dll
// and loads Oni3Patch.dll (from the same folder as this proxy) on attach.

#include <windows.h>
#include <unknwn.h>

namespace
{
    HMODULE g_realDSound = nullptr;

    bool LoadReal()
    {
        if (g_realDSound)
            return true;

        wchar_t path[MAX_PATH];
        UINT len = GetSystemDirectoryW(path, MAX_PATH);
        if (len == 0 || len + 12 >= MAX_PATH)
            return false;
        lstrcatW(path, L"\\dsound.dll");

        g_realDSound = LoadLibraryW(path);
        return g_realDSound != nullptr;
    }

    template <typename Fn>
    Fn Resolve(const char* name)
    {
        if (!LoadReal())
            return nullptr;
        return reinterpret_cast<Fn>(GetProcAddress(g_realDSound, name));
    }

    void LoadPatch(HMODULE self)
    {
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return;

        // strip file name, keep directory
        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash)
            return;
        *(slash + 1) = L'\0';

        if (lstrlenW(path) + lstrlenW(L"Oni3Patch.dll") >= MAX_PATH)
            return;
        lstrcatW(path, L"Oni3Patch.dll");

        LoadLibraryW(path);
    }
}

// Forwarding helper: resolve once, call through, fail gracefully.
#define FORWARD(ret, name, params, args, fail)                         \
    extern "C" ret WINAPI Proxy_##name params                          \
    {                                                                  \
        using Fn = ret (WINAPI*) params;                               \
        static Fn fn = Resolve<Fn>(#name);                             \
        return fn ? fn args : (fail);                                  \
    }

#define E_DS_FAIL ((HRESULT)0x80004005L) /* E_FAIL */

FORWARD(HRESULT, DirectSoundCreate,
        (const GUID* dev, void** ppDS, IUnknown* outer),
        (dev, ppDS, outer), E_DS_FAIL)

FORWARD(HRESULT, DirectSoundEnumerateA,
        (void* callback, void* ctx), (callback, ctx), E_DS_FAIL)

FORWARD(HRESULT, DirectSoundEnumerateW,
        (void* callback, void* ctx), (callback, ctx), E_DS_FAIL)

FORWARD(HRESULT, DllCanUnloadNow,
        (void), (), S_FALSE)

FORWARD(HRESULT, DllGetClassObject,
        (const GUID* clsid, const GUID* iid, void** ppv),
        (clsid, iid, ppv), E_DS_FAIL)

FORWARD(HRESULT, DirectSoundCaptureCreate,
        (const GUID* dev, void** ppDSC, IUnknown* outer),
        (dev, ppDSC, outer), E_DS_FAIL)

FORWARD(HRESULT, DirectSoundCaptureEnumerateA,
        (void* callback, void* ctx), (callback, ctx), E_DS_FAIL)

FORWARD(HRESULT, DirectSoundCaptureEnumerateW,
        (void* callback, void* ctx), (callback, ctx), E_DS_FAIL)

FORWARD(HRESULT, GetDeviceID,
        (const GUID* src, GUID* dst), (src, dst), E_DS_FAIL)

FORWARD(HRESULT, DirectSoundFullDuplexCreate,
        (const GUID* capDev, const GUID* renDev, const void* capDesc,
         const void* bufDesc, HWND hwnd, DWORD level, void** ppFD,
         void** ppCapBuf, void** ppBuf, IUnknown* outer),
        (capDev, renDev, capDesc, bufDesc, hwnd, level, ppFD, ppCapBuf, ppBuf, outer),
        E_DS_FAIL)

FORWARD(HRESULT, DirectSoundCreate8,
        (const GUID* dev, void** ppDS8, IUnknown* outer),
        (dev, ppDS8, outer), E_DS_FAIL)

FORWARD(HRESULT, DirectSoundCaptureCreate8,
        (const GUID* dev, void** ppDSC8, IUnknown* outer),
        (dev, ppDSC8, outer), E_DS_FAIL)

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        LoadReal();
        LoadPatch(module);
        break;

    case DLL_PROCESS_DETACH:
        if (g_realDSound)
        {
            FreeLibrary(g_realDSound);
            g_realDSound = nullptr;
        }
        break;
    }
    return TRUE;
}
