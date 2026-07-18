#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <string>

#include "memory.h"
#include "cheat.h"
#include "gui.h"
#include "hook.h"
#include "config.h"

typedef HRESULT(__stdcall* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(__stdcall* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PresentFn g_originalPresent = nullptr;
ResizeBuffersFn g_originalResizeBuffers = nullptr;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_deviceContext = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTargetView = nullptr;
HWND g_gameHwnd = nullptr;
bool g_initialized = false;

uintptr_t g_capturedUserDataBase = 0;
void* g_userDataHookTarget = nullptr;
void* g_userDataHookCode = nullptr;
BYTE g_userDataHookOriginal[10] = {};
bool g_userDataHookInstalled = false;
bool g_consoleAllocated = false;
bool g_loggedUserDataBase = false;
bool g_aobScanActive = false;
bool g_aobScanFinished = true;
int g_aobScanAttempts = 0;
bool g_unloadRequested = false;
bool g_cleanupDone = false;
HMODULE g_module = nullptr;
HMODULE g_monoModuleHandle = nullptr;
int g_monoModuleLoadCount = 0;

bool g_monoProbeFinished = false;
bool g_monoProbeSucceeded = false;
uintptr_t g_monoSetSpecialAmmoMethod = 0;
uintptr_t g_monoSetSpecialAmmoInjectPoint = 0;
uintptr_t g_monoCapturedPlayerBase = 0;
void* g_monoPlayerHookTarget = nullptr;
void* g_monoPlayerHookCode = nullptr;
BYTE g_monoPlayerHookOriginal[6] = {};
bool g_monoPlayerHookInstalled = false;
bool g_monoProbeActive = false;
bool g_loggedMonoPlayerBase = false;
char g_monoProbeSummary[2048] = "MonoModule 尚未探测";
size_t g_userDataHookOriginalSize = 0;
bool g_logInitialized = false;
HANDLE g_unloadThreadHandle = nullptr;
DWORD g_mainThreadId = 0;

bool g_hasOriginalCoin = false;
int g_originalCoin = 0;
uintptr_t g_originalCoinAddress = 0;
bool g_hasOriginalFishCoin = false;
int g_originalFishCoin = 0;
uintptr_t g_originalFishCoinAddress = 0;
bool g_hasOriginalEnergy = false;
int g_originalEnergy = 0;
uintptr_t g_originalEnergyAddress = 0;
bool g_hasOriginalDay = false;
int g_originalDay = 0;
uintptr_t g_originalDayAddress = 0;
bool g_hasOriginalHour = false;
int g_originalHour = 0;
uintptr_t g_originalHourAddress = 0;
bool g_hasOriginalMinute = false;
int g_originalMinute = 0;
uintptr_t g_originalMinuteAddress = 0;

bool InstallUserDataHook(uintptr_t injectPoint);
bool InstallBroforceMonoPlayerHook(uintptr_t injectPoint);
void RemoveBroforceMonoPlayerHook();
HMODULE LoadMonoModuleManaged();
void ReleaseMonoModuleManaged();
void RemoveUserDataHook();
void RestoreConfiguredLocks();
void CleanupTrainer();
DWORD WINAPI UnloadThread(LPVOID);
void Log(const char* msg);
void Logf(const char* format, ...);

void RequestUnload() {
    if (g_unloadRequested) {
        return;
    }

    Log("[卸载] 已请求从游戏中卸载 DLL，准备恢复修改并释放文件占用");
    g_unloadRequested = true;
    g_aobScanActive = false;
    HANDLE thread = CreateThread(nullptr, 0, UnloadThread, nullptr, 0, nullptr);
    if (thread) {
        g_unloadThreadHandle = thread;
    } else {
        Logf("[卸载] 创建卸载线程失败，错误码: %lu", GetLastError());
    }
}

void CleanupTrainer() {
    if (g_cleanupDone) {
        return;
    }

    g_cleanupDone = true;
    g_aobScanActive = false;
    Log("[卸载] 开始恢复锁定数值、窗口、D3D Hook 和 AOB Hook");

    RestoreConfiguredLocks();

    if (GUI::s_originalWndProc && g_gameHwnd) {
        SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)GUI::s_originalWndProc);
        GUI::s_originalWndProc = nullptr;
        Log("[卸载] WndProc 已恢复");
    }

    RemoveUserDataHook();
    RemoveBroforceMonoPlayerHook();

    if (g_originalPresent) {
        D3D11Hook::RestoreFunction(8, (void*)g_originalPresent);
        Log("[卸载] Present Hook 已恢复");
        g_originalPresent = nullptr;
    }

    if (g_originalResizeBuffers) {
        D3D11Hook::RestoreFunction(13, (void*)g_originalResizeBuffers);
        Log("[卸载] ResizeBuffers Hook 已恢复");
        g_originalResizeBuffers = nullptr;
    }

    if (g_initialized) {
        GUI::Instance().Shutdown();
        g_initialized = false;
    }

    if (g_renderTargetView) {
        g_renderTargetView->Release();
        g_renderTargetView = nullptr;
    }

    if (g_deviceContext) {
        g_deviceContext->Release();
        g_deviceContext = nullptr;
    }

    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }

    ReleaseMonoModuleManaged();

    ShowCursor(TRUE);
    Log("[卸载] 清理完成，DLL 即将从游戏进程脱出");
}

void RestoreIntLock(bool& hasOriginal, uintptr_t& address, int& originalValue) {
    if (hasOriginal && address) {
        Memory::Write<int>(address, originalValue);
    }
    hasOriginal = false;
    address = 0;
}

void RestoreConfiguredLocks() {
    RestoreIntLock(g_hasOriginalCoin, g_originalCoinAddress, g_originalCoin);
    RestoreIntLock(g_hasOriginalFishCoin, g_originalFishCoinAddress, g_originalFishCoin);
    RestoreIntLock(g_hasOriginalEnergy, g_originalEnergyAddress, g_originalEnergy);
    RestoreIntLock(g_hasOriginalDay, g_originalDayAddress, g_originalDay);
    RestoreIntLock(g_hasOriginalHour, g_originalHourAddress, g_originalHour);
    RestoreIntLock(g_hasOriginalMinute, g_originalMinuteAddress, g_originalMinute);
    Log("[卸载] 已恢复锁定项的原始数值");
}

void ApplyIntLock(uintptr_t address, Config::LockConfig& config, bool& hasOriginal, uintptr_t& originalAddress, int& originalValue) {
    if (!address) {
        return;
    }

    if (!config.enabled) {
        if (hasOriginal) {
            RestoreIntLock(hasOriginal, originalAddress, originalValue);
        }
        return;
    }

    if (!hasOriginal || originalAddress != address) {
        if (hasOriginal) {
            RestoreIntLock(hasOriginal, originalAddress, originalValue);
        }
        originalValue = Memory::Read<int>(address);
        originalAddress = address;
        hasOriginal = true;
    }

    Memory::Write<int>(address, (int)config.value);
}

void ApplyConfiguredLocks(uintptr_t userDataBase) {
    if (!userDataBase) {
        return;
    }

    auto& config = Config::Instance();
    uintptr_t coinAddress = Cheat::Instance().GetCoinAddress();

    ApplyIntLock(coinAddress, config.coin, g_hasOriginalCoin, g_originalCoinAddress, g_originalCoin);
    ApplyIntLock(userDataBase + Offsets::FishCoin, config.fishCoin, g_hasOriginalFishCoin, g_originalFishCoinAddress, g_originalFishCoin);
    ApplyIntLock(userDataBase + Offsets::Energy, config.energy, g_hasOriginalEnergy, g_originalEnergyAddress, g_originalEnergy);
    ApplyIntLock(userDataBase + Offsets::Day, config.day, g_hasOriginalDay, g_originalDayAddress, g_originalDay);
    ApplyIntLock(userDataBase + Offsets::Hour, config.hour, g_hasOriginalHour, g_originalHourAddress, g_originalHour);
    ApplyIntLock(userDataBase + Offsets::Minute, config.minute, g_hasOriginalMinute, g_originalMinuteAddress, g_originalMinute);
}

bool IsRel32Reachable(uintptr_t from, uintptr_t to) {
    int64_t delta = (int64_t)to - (int64_t)from - 5;
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

void* AllocateNear(uintptr_t target, size_t size) {
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);

    uintptr_t granularity = si.dwAllocationGranularity;
    uintptr_t base = target & ~(granularity - 1);
    uintptr_t maxDistance = 0x70000000;

    for (uintptr_t distance = granularity; distance < maxDistance; distance += granularity) {
        uintptr_t low = base > distance ? base - distance : 0;
        if (low && IsRel32Reachable(target, low)) {
            void* memory = VirtualAlloc((void*)low, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (memory) {
                return memory;
            }
        }

        uintptr_t high = base + distance;
        if (high > base && IsRel32Reachable(target, high)) {
            void* memory = VirtualAlloc((void*)high, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (memory) {
                return memory;
            }
        }
    }

    return nullptr;
}

const char* GetLogPath() {
    static std::string path;
    if (!path.empty()) {
        return path.c_str();
    }

    char modulePath[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(g_module, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        path = "trainer_log.txt";
        return path.c_str();
    }

    char* slash = strrchr(modulePath, '\\');
    if (!slash) {
        path = "trainer_log.txt";
        return path.c_str();
    }

    *(slash + 1) = '\0';
    path = std::string(modulePath) + "trainer_log.txt";
    return path.c_str();
}

void Log(const char* msg) {
    const char* logPath = GetLogPath();
    if (!g_logInitialized) {
        FILE* init = fopen(logPath, "wb");
        if (init) {
            const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            fwrite(bom, 1, sizeof(bom), init);
            fclose(init);
        }
        g_logInitialized = true;
    }

    FILE* f = fopen(logPath, "ab");
    if (f) {
        fprintf(f, "%s\r\n", msg);
        fclose(f);
    }
}

void Logf(const char* format, ...) {
    char buffer[512] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Log(buffer);
}

void InitializeConsole() {
    g_consoleAllocated = false;
    Log("[日志] 调试控制台已禁用，仅写入 trainer_log.txt");
}

std::string GetSiblingDllPath(const char* dllName) {
    char modulePath[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(g_module, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return dllName;
    }

    char* slash = strrchr(modulePath, '\\');
    if (!slash) {
        return dllName;
    }

    *(slash + 1) = '\0';
    return std::string(modulePath) + dllName;
}

bool BytesMatch(uintptr_t address, const BYTE* pattern, const char* mask, size_t length) {
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi))) {
        return false;
    }
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }

    for (size_t i = 0; i < length; ++i) {
        if (mask[i] == 'x' && *(BYTE*)(address + i) != pattern[i]) {
            return false;
        }
    }
    return true;
}

uintptr_t ScanNear(uintptr_t start, size_t size, const BYTE* pattern, const char* mask) {
    size_t maskLen = strlen(mask);
    if (!start || size < maskLen) {
        return 0;
    }

    for (size_t offset = 0; offset + maskLen <= size; ++offset) {
        uintptr_t current = start + offset;
        if (BytesMatch(current, pattern, mask, maskLen)) {
            return current;
        }
    }
    return 0;
}

HMODULE LoadMonoModuleManaged() {
    if (g_monoModuleHandle) {
        return g_monoModuleHandle;
    }

    std::string monoPath = GetSiblingDllPath("MonoModule.dll");
    HMODULE monoModule = LoadLibraryA(monoPath.c_str());
    if (!monoModule) {
        monoModule = LoadLibraryA("MonoModule.dll");
    }
    if (monoModule) {
        g_monoModuleHandle = monoModule;
        g_monoModuleLoadCount = 1;
    }
    return monoModule;
}

void ReleaseMonoModuleManaged() {
    if (!g_monoModuleHandle) {
        return;
    }

    HMODULE monoModule = g_monoModuleHandle;
    g_monoModuleHandle = nullptr;
    int releases = g_monoModuleLoadCount > 0 ? g_monoModuleLoadCount : 1;
    g_monoModuleLoadCount = 0;

    for (int i = 0; i < releases; ++i) {
        FreeLibrary(monoModule);
    }
    Logf("[卸载] MonoModule.dll 已随 Trainer 一并释放，释放引用次数=%d", releases);
}

bool InstallBroforceMonoPlayerHook(uintptr_t injectPoint) {
    if (!injectPoint || g_monoPlayerHookInstalled) {
        return g_monoPlayerHookInstalled;
    }

    constexpr size_t originalSize = sizeof(g_monoPlayerHookOriginal);
    constexpr size_t codeSize = 64;

    Logf("[MonoModule] 准备在 Broforce set_SpecialAmmo+31 安装玩家基址捕获 Hook: 0x%p", (void*)injectPoint);

    g_monoPlayerHookCode = AllocateNear(injectPoint, codeSize);
    if (!g_monoPlayerHookCode) {
        Log("[MonoModule] 无法在 Mono 方法附近分配 newmem，玩家 Hook 未安装");
        return false;
    }

    memcpy(g_monoPlayerHookOriginal, (void*)injectPoint, originalSize);

    BYTE* code = (BYTE*)g_monoPlayerHookCode;
    size_t offset = 0;

    // Broforce.CT: cmp [rdi+104],1; only capture the local player.
    BYTE cmpHero[] = { 0x66, 0x83, 0xBF, 0x04, 0x01, 0x00, 0x00, 0x01 };
    memcpy(code + offset, cmpHero, sizeof(cmpHero));
    offset += sizeof(cmpHero);

    // jne +15: skip capture block but still execute original mov [rdi+5DC],eax.
    code[offset++] = 0x75;
    code[offset++] = 0x0F;

    // Preserve RAX/EAX because the original instruction writes eax.
    code[offset++] = 0x50;

    // mov rax, &g_monoCapturedPlayerBase
    code[offset++] = 0x48;
    code[offset++] = 0xB8;
    *(uintptr_t*)(code + offset) = (uintptr_t)&g_monoCapturedPlayerBase;
    offset += sizeof(uintptr_t);

    // mov [rax], rdi
    BYTE movPlayerBase[] = { 0x48, 0x89, 0x38 };
    memcpy(code + offset, movPlayerBase, sizeof(movPlayerBase));
    offset += sizeof(movPlayerBase);

    code[offset++] = 0x58;

    // Original: mov [rdi+000005DC],eax
    memcpy(code + offset, g_monoPlayerHookOriginal, originalSize);
    offset += originalSize;

    // jmp qword ptr [rip+0]; dq injectPoint + 6
    BYTE jumpBack[] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    memcpy(code + offset, jumpBack, sizeof(jumpBack));
    offset += sizeof(jumpBack);
    *(uintptr_t*)(code + offset) = injectPoint + originalSize;
    offset += sizeof(uintptr_t);

    DWORD oldProtect;
    if (!VirtualProtect((void*)injectPoint, originalSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(g_monoPlayerHookCode, 0, MEM_RELEASE);
        g_monoPlayerHookCode = nullptr;
        Logf("[MonoModule] 修改 Mono 注入点内存保护失败，错误码: %lu", GetLastError());
        return false;
    }

    if (!IsRel32Reachable(injectPoint, (uintptr_t)g_monoPlayerHookCode)) {
        VirtualProtect((void*)injectPoint, originalSize, oldProtect, &oldProtect);
        VirtualFree(g_monoPlayerHookCode, 0, MEM_RELEASE);
        g_monoPlayerHookCode = nullptr;
        Log("[MonoModule] Mono newmem 超出 ±2GB，Hook 未安装");
        return false;
    }

    BYTE patch[originalSize] = { 0xE9, 0, 0, 0, 0, 0x90 };
    *(int32_t*)(patch + 1) = (int32_t)((uintptr_t)g_monoPlayerHookCode - injectPoint - 5);
    memcpy((void*)injectPoint, patch, sizeof(patch));
    VirtualProtect((void*)injectPoint, originalSize, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), (void*)injectPoint, originalSize);

    g_monoPlayerHookTarget = (void*)injectPoint;
    g_monoPlayerHookInstalled = true;
    Log("[MonoModule] Broforce 玩家基址 Hook 安装成功；使用一次特殊弹药后应捕获 player base");
    return true;
}

void RemoveBroforceMonoPlayerHook() {
    if (!g_monoPlayerHookInstalled || !g_monoPlayerHookTarget) {
        return;
    }

    DWORD oldProtect;
    if (VirtualProtect(g_monoPlayerHookTarget, sizeof(g_monoPlayerHookOriginal), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(g_monoPlayerHookTarget, g_monoPlayerHookOriginal, sizeof(g_monoPlayerHookOriginal));
        VirtualProtect(g_monoPlayerHookTarget, sizeof(g_monoPlayerHookOriginal), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), g_monoPlayerHookTarget, sizeof(g_monoPlayerHookOriginal));
    }

    if (g_monoPlayerHookCode) {
        VirtualFree(g_monoPlayerHookCode, 0, MEM_RELEASE);
        g_monoPlayerHookCode = nullptr;
    }

    g_monoPlayerHookTarget = nullptr;
    g_monoPlayerHookInstalled = false;
}

void ProbeBroforceMonoMethod() {
    g_monoProbeActive = true;
    Log("[MonoModule] 开始通过 Mono 查询 Broforce TestVanDammeAnim:set_SpecialAmmo");

    HMODULE monoModule = LoadMonoModuleManaged();
    if (!monoModule) {
        snprintf(g_monoProbeSummary, sizeof(g_monoProbeSummary),
            "MonoModule.dll 加载失败，错误码=%lu", GetLastError());
        Log(g_monoProbeSummary);
        g_monoProbeFinished = true;
        g_monoProbeActive = false;
        return;
    }

    using InitializeFn = int(__cdecl*)(const char*);
    using GetLastErrorFn = const char*(__cdecl*)();
    using GetManagedDirectoryFn = const char*(__cdecl*)();
    using FindClassFn = uintptr_t(__cdecl*)(const char*, const char*, const char*);
    using FindMethodFn = uintptr_t(__cdecl*)(uintptr_t, const char*, int);
    using GetMethodAddressByNameFn = uintptr_t(__cdecl*)(const char*, const char*, const char*, const char*, int);

    auto monoInitialize = (InitializeFn)GetProcAddress(monoModule, "MonoModule_Initialize");
    auto monoGetLastError = (GetLastErrorFn)GetProcAddress(monoModule, "MonoModule_GetLastError");
    auto monoGetManagedDirectory = (GetManagedDirectoryFn)GetProcAddress(monoModule, "MonoModule_GetManagedDirectory");
    auto monoFindClass = (FindClassFn)GetProcAddress(monoModule, "MonoModule_FindClass");
    auto monoFindMethod = (FindMethodFn)GetProcAddress(monoModule, "MonoModule_FindMethod");
    auto monoGetMethodAddressByName = (GetMethodAddressByNameFn)GetProcAddress(monoModule, "MonoModule_GetMethodAddressByName");

    if (!monoInitialize || !monoGetLastError || !monoGetManagedDirectory || !monoFindClass || !monoFindMethod || !monoGetMethodAddressByName) {
        snprintf(g_monoProbeSummary, sizeof(g_monoProbeSummary),
            "MonoModule.dll 导出不完整，请先重新编译 MonoModule.dll");
        Log(g_monoProbeSummary);
        g_monoProbeFinished = true;
        g_monoProbeActive = false;
        return;
    }

    monoInitialize(Config::Instance().Path());
    const char* managedDir = monoGetManagedDirectory();
    Logf("[MonoModule] Managed目录: %s", managedDir && *managedDir ? managedDir : "未检测到/未配置");

    const char* assemblies[] = { "Assembly-CSharp.dll", "Assembly-CSharp" };
    uintptr_t klass = 0;
    uintptr_t method = 0;
    uintptr_t methodAddress = 0;
    const char* usedAssembly = nullptr;

    for (const char* assembly : assemblies) {
        klass = monoFindClass(assembly, "", "TestVanDammeAnim");
        if (!klass) {
            Logf("[MonoModule] %s 里没有找到 TestVanDammeAnim: %s", assembly, monoGetLastError());
            continue;
        }

        method = monoFindMethod(klass, "set_SpecialAmmo", 1);
        if (!method) {
            Logf("[MonoModule] TestVanDammeAnim:set_SpecialAmmo 没找到: %s", monoGetLastError());
            continue;
        }

        methodAddress = monoGetMethodAddressByName(assembly, "", "TestVanDammeAnim", "set_SpecialAmmo", 1);
        if (methodAddress) {
            usedAssembly = assembly;
            break;
        }

        Logf("[MonoModule] mono_compile_method 失败: %s", monoGetLastError());
    }

    if (!methodAddress) {
        snprintf(g_monoProbeSummary, sizeof(g_monoProbeSummary),
            "Mono 找不到 TestVanDammeAnim:set_SpecialAmmo。最后错误: %s", monoGetLastError());
        Log(g_monoProbeSummary);
        g_monoProbeFinished = true;
        g_monoProbeActive = false;
        return;
    }

    const BYTE pattern[] = { 0x89, 0x87, 0xDC, 0x05, 0x00, 0x00 };
    const char* mask = "xxxxxx";
    uintptr_t injectPoint = ScanNear(methodAddress, 0x200, pattern, mask);

    g_monoSetSpecialAmmoMethod = methodAddress;
    g_monoSetSpecialAmmoInjectPoint = injectPoint ? injectPoint : methodAddress + 0x31;
    g_monoProbeSucceeded = injectPoint != 0;
    if (g_monoProbeSucceeded) {
        g_aobScanActive = false;
        InstallBroforceMonoPlayerHook(g_monoSetSpecialAmmoInjectPoint);
    }
    g_monoProbeFinished = true;
    g_monoProbeActive = false;

    snprintf(g_monoProbeSummary, sizeof(g_monoProbeSummary),
        "Mono已拿到 %s!TestVanDammeAnim:set_SpecialAmmo 方法=0x%p，CE +31 位置=0x%p，特征%s",
        usedAssembly ? usedAssembly : "Assembly-CSharp",
        (void*)g_monoSetSpecialAmmoMethod,
        (void*)g_monoSetSpecialAmmoInjectPoint,
        injectPoint ? "匹配 89 87 DC 05 00 00" : "未在前0x200字节内匹配，暂按 method+0x31 显示");
    Log(g_monoProbeSummary);
}

bool InstallUserDataHook(uintptr_t injectPoint) {
    constexpr size_t originalSize = sizeof(g_userDataHookOriginal);
    constexpr size_t codeSize = 96;

    Logf("[AOB] 找到 Dojo NTR.CT 注入点: 0x%p", (void*)injectPoint);
    Logf("[AOB] 原始 10 字节: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
        *(BYTE*)injectPoint, *(BYTE*)(injectPoint + 1), *(BYTE*)(injectPoint + 2), *(BYTE*)(injectPoint + 3), *(BYTE*)(injectPoint + 4),
        *(BYTE*)(injectPoint + 5), *(BYTE*)(injectPoint + 6), *(BYTE*)(injectPoint + 7), *(BYTE*)(injectPoint + 8), *(BYTE*)(injectPoint + 9));

    g_userDataHookCode = AllocateNear(injectPoint, codeSize);
    if (!g_userDataHookCode) {
        Log("[AOB] 无法在注入点 ±2GB 内分配 newmem，5 字节 jmp 跳不到，Hook 未安装");
        return false;
    }

    Logf("[AOB] newmem 分配成功: 0x%p", g_userDataHookCode);
    memcpy(g_userDataHookOriginal, (void*)injectPoint, originalSize);

    BYTE* code = (BYTE*)g_userDataHookCode;
    size_t offset = 0;

    // push rdx
    code[offset++] = 0x52;

    // mov rdx, &g_capturedUserDataBase
    code[offset++] = 0x48;
    code[offset++] = 0xBA;
    *(uintptr_t*)(code + offset) = (uintptr_t)&g_capturedUserDataBase;
    offset += sizeof(uintptr_t);

    // mov [rdx], rax
    BYTE movCapturedBase[] = { 0x48, 0x89, 0x02 };
    memcpy(code + offset, movCapturedBase, sizeof(movCapturedBase));
    offset += sizeof(movCapturedBase);

    // pop rdx
    code[offset++] = 0x5A;

    // Original Dojo NTR.CT instructions:
    // mov [r8],rax
    BYTE movUserData[] = { 0x49, 0x89, 0x00 };
    memcpy(code + offset, movUserData, sizeof(movUserData));
    offset += sizeof(movUserData);

    // The second original instruction is RIP-relative:
    // mov rcx,[GameAssembly.dll+3284F48]
    // Do not copy its raw bytes into newmem, otherwise RIP will be relative to newmem and crash.
    int32_t rcxDisplacement = *(int32_t*)(g_userDataHookOriginal + 6);
    uintptr_t rcxSourceAddress = injectPoint + originalSize + rcxDisplacement;
    Logf("[AOB] 修正 RIP 相对寻址: rcx source=0x%p", (void*)rcxSourceAddress);

    // push r11; mov r11, rcxSourceAddress; mov rcx,[r11]; pop r11
    BYTE pushR11[] = { 0x41, 0x53 };
    memcpy(code + offset, pushR11, sizeof(pushR11));
    offset += sizeof(pushR11);
    code[offset++] = 0x49;
    code[offset++] = 0xBB;
    *(uintptr_t*)(code + offset) = rcxSourceAddress;
    offset += sizeof(uintptr_t);
    BYTE movRcxFromR11[] = { 0x49, 0x8B, 0x0B };
    memcpy(code + offset, movRcxFromR11, sizeof(movRcxFromR11));
    offset += sizeof(movRcxFromR11);
    BYTE popR11[] = { 0x41, 0x5B };
    memcpy(code + offset, popR11, sizeof(popR11));
    offset += sizeof(popR11);

    // jmp qword ptr [rip+0]; dq injectPoint + 10
    // This returns without clobbering any general-purpose register.
    BYTE jumpBack[] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    memcpy(code + offset, jumpBack, sizeof(jumpBack));
    offset += sizeof(jumpBack);
    *(uintptr_t*)(code + offset) = injectPoint + originalSize;
    offset += sizeof(uintptr_t);

    DWORD oldProtect;
    if (!VirtualProtect((void*)injectPoint, originalSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(g_userDataHookCode, 0, MEM_RELEASE);
        g_userDataHookCode = nullptr;
        Logf("[AOB] 修改注入点内存保护失败，错误码: %lu", GetLastError());
        return false;
    }

    BYTE patch[originalSize] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90 };
    uintptr_t relativeDetour = (uintptr_t)g_userDataHookCode - injectPoint - 5;
    if (!IsRel32Reachable(injectPoint, (uintptr_t)g_userDataHookCode)) {
        VirtualProtect((void*)injectPoint, originalSize, oldProtect, &oldProtect);
        VirtualFree(g_userDataHookCode, 0, MEM_RELEASE);
        g_userDataHookCode = nullptr;
        Log("[AOB] newmem 距离注入点超过 ±2GB，5 字节 jmp 无效，Hook 未安装");
        return false;
    }
    *(int32_t*)(patch + 1) = (int32_t)relativeDetour;
    memcpy((void*)injectPoint, patch, sizeof(patch));
    VirtualProtect((void*)injectPoint, originalSize, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), (void*)injectPoint, originalSize);

    g_userDataHookTarget = (void*)injectPoint;
    g_userDataHookInstalled = true;
    Log("[AOB] UserData 基址 Hook 安装成功：已写入 jmp newmem + nop");
    Log("[AOB] 现在进入游戏/读档，等待 SaveData.InitUser 附近代码执行并捕获 UserData 基址");
    return true;
}

void RemoveUserDataHook() {
    if (!g_userDataHookInstalled || !g_userDataHookTarget) {
        return;
    }

    DWORD oldProtect;
    if (VirtualProtect(g_userDataHookTarget, sizeof(g_userDataHookOriginal), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(g_userDataHookTarget, g_userDataHookOriginal, sizeof(g_userDataHookOriginal));
        VirtualProtect(g_userDataHookTarget, sizeof(g_userDataHookOriginal), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), g_userDataHookTarget, sizeof(g_userDataHookOriginal));
    }

    if (g_userDataHookCode) {
        VirtualFree(g_userDataHookCode, 0, MEM_RELEASE);
        g_userDataHookCode = nullptr;
    }

    g_userDataHookTarget = nullptr;
    g_userDataHookInstalled = false;
}

void AOBScanThread() {
    const BYTE pattern[] = { 0x49, 0x89, 0x00, 0x48, 0x8B, 0x0D, 0x5E, 0x0D, 0xE1, 0x02 };
    const char* mask = "xxxxxxxxxx";

    g_aobScanActive = true;
    g_aobScanFinished = false;
    Log("[AOB] 开始持续扫描 GameAssembly.dll：49 89 00 48 8B 0D 5E 0D E1 02");
    Log("[AOB] 说明：该特征码来自 Dojo NTR.CT 的 SaveData.InitUser 附近，用于捕获 UserData 基址");

    while (g_aobScanActive && !g_userDataHookInstalled) {
        ++g_aobScanAttempts;

        uintptr_t moduleBase = Memory::GetModuleBase("GameAssembly.dll");
        size_t moduleSize = Memory::GetModuleSize("GameAssembly.dll");
        if (!moduleBase || !moduleSize) {
            if (g_aobScanAttempts == 1 || g_aobScanAttempts % 5 == 0) {
                Logf("[AOB] 第 %d 次扫描：GameAssembly.dll 尚未加载", g_aobScanAttempts);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        uintptr_t injectPoint = Memory::ScanPattern(moduleBase, moduleSize, pattern, mask);
        if (injectPoint) {
            Logf("[AOB] 第 %d 次扫描找到 CT 特征码，地址=0x%p", g_aobScanAttempts, (void*)injectPoint);
            if (!InstallUserDataHook(injectPoint)) {
                Log("[AOB] 找到了特征码但 Hook 安装失败，停止持续扫描，避免反复修改内存");
            }
            break;
        }

        if (g_aobScanAttempts == 1 || g_aobScanAttempts % 5 == 0) {
            Logf("[AOB] 第 %d 次扫描仍未找到；请进入游戏/读档触发 UserData 初始化", g_aobScanAttempts);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    g_aobScanActive = false;
    g_aobScanFinished = true;
}

HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT syncInterval, UINT flags) {
    PresentFn originalPresent = g_originalPresent;
    if (g_unloadRequested && originalPresent) {
        return originalPresent(pSwapChain, syncInterval, flags);
    }

    if (!g_initialized) {
        Log("[DX11] Present 首次执行，开始初始化 ImGui");

        pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_device);
        if (!g_device) {
            Log("[DX11] Present 中获取 ID3D11Device 失败");
            return originalPresent(pSwapChain, syncInterval, flags);
        }
        g_device->GetImmediateContext(&g_deviceContext);

        DXGI_SWAP_CHAIN_DESC desc;
        pSwapChain->GetDesc(&desc);
        g_gameHwnd = desc.OutputWindow;
        Logf("[DX11] 游戏窗口句柄=0x%p", g_gameHwnd);

        ID3D11Texture2D* backBuffer = nullptr;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (backBuffer) {
            g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTargetView);
            backBuffer->Release();
            Logf("[DX11] RenderTargetView 创建成功=0x%p", g_renderTargetView);
        } else {
            Log("[DX11] 获取 backBuffer 失败");
        }

        GUI::Instance().Initialize(g_gameHwnd, g_device, g_deviceContext);
        Cheat::Instance().Initialize();

        GUI::s_originalWndProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)GUI::WndProc);
        Logf("[窗口] WndProc Hook 完成，原窗口过程=0x%p", GUI::s_originalWndProc);

        g_initialized = true;
        Log("[DX11] ImGui 初始化完成");
    }

    if (g_monoCapturedPlayerBase) {
        Cheat::Instance().SetUserDataBase(g_monoCapturedPlayerBase);
        if (!g_loggedMonoPlayerBase) {
            Logf("[MonoModule] Broforce player base 捕获成功: 0x%p；已停止等待 Dojo UserData/AOB", (void*)g_monoCapturedPlayerBase);
            g_loggedMonoPlayerBase = true;
        }
    }

    if (!g_monoProbeSucceeded && g_capturedUserDataBase) {
        Cheat::Instance().SetUserDataBase(g_capturedUserDataBase);
        ApplyConfiguredLocks(g_capturedUserDataBase);
        if (!g_loggedUserDataBase) {
            Logf("[UserData] 捕获成功: 0x%p，GUI 已停止等待", (void*)g_capturedUserDataBase);
            g_loggedUserDataBase = true;
        }
    }

    GUI::Instance().SetRenderTargetView(g_renderTargetView);
    GUI::Instance().Render();

    return originalPresent(pSwapChain, syncInterval, flags);
}

HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT bufferCount,
    UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    if (g_renderTargetView) {
        g_renderTargetView->Release();
        g_renderTargetView = nullptr;
    }

    HRESULT hr = g_originalResizeBuffers(pSwapChain, bufferCount, width, height, format, flags);

    if (g_device) {
        ID3D11Texture2D* backBuffer = nullptr;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (backBuffer) {
            g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTargetView);
            backBuffer->Release();
        }
    }

    return hr;
}

DWORD WINAPI UnloadThread(LPVOID) {
    Log("[卸载] 卸载线程已启动，正在停止后台任务");
    g_aobScanActive = false;

    for (int i = 0; i < 80 && !g_aobScanFinished; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!g_aobScanFinished) {
        Log("[卸载] AOB 扫描线程未及时结束，继续执行清理");
    }

    for (int i = 0; i < 80 && g_monoProbeActive; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (g_monoProbeActive) {
        Log("[卸载] Mono 探测线程仍在运行，继续执行清理；请稍后确认模块是否释放");
    }

    // Let the current Present frame return before unpatching vtables and unloading this DLL.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    CleanupTrainer();

    HMODULE module = g_module;
    if (g_consoleAllocated) {
        Log("[卸载] 控制台即将关闭");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        FreeConsole();
        g_consoleAllocated = false;
    }

    Log("[卸载] 调用 FreeLibraryAndExitThread，模块应该从进程中释放");
    if (module) {
        FreeLibraryAndExitThread(module, 0);
    }
    return 0;
}

DWORD WINAPI MainThread(LPVOID) {
    InitializeConsole();
    Config::Instance().Load();
    Log("[启动] DLL_PROCESS_ATTACH 已完成，主线程启动");
    std::thread(ProbeBroforceMonoMethod).detach();

    Log("[DX11] 等待 d3d11.dll 加载...");
    while (!GetModuleHandleA("d3d11.dll")) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    Log("[DX11] d3d11.dll 已加载，开始创建临时交换链定位 vtable");

    if (!D3D11Hook::Initialize()) {
        Log("[DX11] D3D11Hook::Initialize 失败：无法创建临时交换链");
        return 0;
    }
    Log("[DX11] vtable 定位成功，准备 Hook Present/ResizeBuffers");

    if (!D3D11Hook::HookFunction(8, (void*)HookedPresent, (void**)&g_originalPresent) ||
        !D3D11Hook::HookFunction(13, (void*)HookedResizeBuffers, (void**)&g_originalResizeBuffers)) {
        Log("[DX11] Hook Present/ResizeBuffers 失败");
        return 0;
    }

    Logf("[DX11] Hook 安装成功：Present 原函数=0x%p，ResizeBuffers 原函数=0x%p",
        (void*)g_originalPresent, (void*)g_originalResizeBuffers);

    for (int i = 0; i < 50 && !g_monoProbeFinished; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (g_monoProbeSucceeded) {
        Log("[AOB] Mono 已经定位 Broforce set_SpecialAmmo+31，跳过 GameAssembly/Dojo AOB 扫描");
    } else {
        uintptr_t moduleBase = Memory::GetModuleBase("GameAssembly.dll");
        size_t moduleSize = Memory::GetModuleSize("GameAssembly.dll");
        Logf("[AOB] GameAssembly.dll 基址=0x%p，模块大小=0x%zX", (void*)moduleBase, moduleSize);
        Log("[AOB] Mono 未定位到目标方法，才回退后台扫描 GameAssembly.dll");
        std::thread(AOBScanThread).detach();
    }

    Log("[启动] 初始化结束：按 INSERT 打开菜单，日志会显示 Mono/AOB 状态");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        g_module = hModule;
        DisableThreadLibraryCalls(hModule);
        Log("DLL_PROCESS_ATTACH");
        HANDLE thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (thread) {
            g_mainThreadId = GetThreadId(thread);
            CloseHandle(thread);
        }
        break;
    }

    case DLL_PROCESS_DETACH:
        if (!g_unloadRequested) {
            CleanupTrainer();
        }
        break;
    }
    return TRUE;
}
