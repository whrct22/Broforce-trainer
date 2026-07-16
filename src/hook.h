#pragma once

#include <Windows.h>
#include <cstdint>

class Hook {
public:
    // Create inline hook with trampoline (returns callable original)
    static void* CreateHook(void* target, void* detour);

    // Remove hook (restore original bytes)
    static bool RemoveHook(void* target);

private:
    struct HookEntry {
        void* target;
        void* trampoline;
        BYTE originalBytes[32];
        size_t copySize;
    };

    static HookEntry s_entries[64];
    static int s_count;
};

// D3D11 VTable discovery / swap-chain hook helpers
class D3D11Hook {
public:
    static bool Initialize();
    static bool HookFunction(int index, void* detour, void** original);
    static bool RestoreFunction(int index, void* original);

    template<typename T>
    static T GetOriginal(int index) {
        if (index >= 0 && index < 205 && s_originalVTable[index]) {
            return reinterpret_cast<T>(s_originalVTable[index]);
        }
        return nullptr;
    }

private:
    static void** s_vTable;
    static void* s_originalVTable[205];
    static bool s_initialized;
};
