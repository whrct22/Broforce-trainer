#include "hook.h"
#include <d3d11.h>
#include <dxgi.h>
#include <cstring>

Hook::HookEntry Hook::s_entries[64] = {};
int Hook::s_count = 0;

void* D3D11Hook::s_originalVTable[205] = {};
void** D3D11Hook::s_vTable = nullptr;
bool D3D11Hook::s_initialized = false;

bool D3D11Hook::Initialize() {
    if (s_initialized) return true;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, DefWindowProcW, 0, 0,
        GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"DX11Dummy", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &swapChain, &device, &featureLevel, &context
    );

    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    s_vTable = *(void***)swapChain;
    memcpy(s_originalVTable, s_vTable, sizeof(s_originalVTable));

    swapChain->Release();
    device->Release();
    context->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    s_initialized = true;
    return true;
}

bool D3D11Hook::HookFunction(int index, void* detour, void** original) {
    if (!s_initialized || !s_vTable || !detour || !original || index < 0 || index >= 205) {
        return false;
    }

    DWORD oldProtect;
    if (!VirtualProtect(&s_vTable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    *original = s_vTable[index];
    s_vTable[index] = detour;

    VirtualProtect(&s_vTable[index], sizeof(void*), oldProtect, &oldProtect);
    return true;
}

bool D3D11Hook::RestoreFunction(int index, void* original) {
    if (!s_initialized || !s_vTable || !original || index < 0 || index >= 205) {
        return false;
    }

    DWORD oldProtect;
    if (!VirtualProtect(&s_vTable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    s_vTable[index] = original;

    VirtualProtect(&s_vTable[index], sizeof(void*), oldProtect, &oldProtect);
    return true;
}

void* Hook::CreateHook(void* target, void* detour) {
    if (!target || !detour || s_count >= 64) return nullptr;

    // Allocate executable memory for trampoline
    void* trampoline = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) return nullptr;

    // Copy original bytes to trampoline (need at least 5 bytes for JMP)
    size_t copySize = 16; // Copy more bytes to be safe
    memcpy(trampoline, target, copySize);

    // Add JMP back to original function + 5
    uintptr_t jumpBackTarget = (uintptr_t)target + 5;
    uintptr_t relativeAddr = jumpBackTarget - ((uintptr_t)trampoline + copySize) - 5;

    BYTE* pTrampoline = (BYTE*)trampoline + copySize;
    pTrampoline[0] = 0xE9;
    *(DWORD*)(pTrampoline + 1) = (DWORD)relativeAddr;

    // Save entry
    HookEntry& entry = s_entries[s_count++];
    entry.target = target;
    entry.trampoline = trampoline;
    entry.copySize = copySize;
    memcpy(entry.originalBytes, target, copySize);

    // Patch original function with JMP to detour
    DWORD oldProtect;
    VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProtect);

    uintptr_t relativeDetour = (uintptr_t)detour - (uintptr_t)target - 5;
    BYTE* pTarget = (BYTE*)target;
    pTarget[0] = 0xE9;
    *(DWORD*)(pTarget + 1) = (DWORD)relativeDetour;

    VirtualProtect(target, 5, oldProtect, &oldProtect);

    return trampoline;
}

bool Hook::RemoveHook(void* target) {
    for (int i = 0; i < s_count; i++) {
        if (s_entries[i].target == target) {
            DWORD oldProtect;
            VirtualProtect(target, s_entries[i].copySize, PAGE_EXECUTE_READWRITE, &oldProtect);
            memcpy(target, s_entries[i].originalBytes, s_entries[i].copySize);
            VirtualProtect(target, s_entries[i].copySize, oldProtect, &oldProtect);

            VirtualFree(s_entries[i].trampoline, 0, MEM_RELEASE);
            return true;
        }
    }
    return false;
}
