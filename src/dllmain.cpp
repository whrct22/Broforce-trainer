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

uintptr_t g_capturedPlayerBase = 0;
void* g_playerHookTarget = nullptr;
void* g_playerHookCode = nullptr;
BYTE g_playerHookOriginal[6] = {};
bool g_playerHookInstalled = false;
bool g_consoleAllocated = false;
bool g_loggedPlayerBase = false;
bool g_aobScanActive = false;
bool g_aobScanFinished = true;
int g_aobScanAttempts = 0;
bool g_unloadRequested = false;
bool g_cleanupDone = false;
HMODULE g_module = nullptr;
uintptr_t g_lockPlayerBase = 0;
bool g_hasOriginalHealth = false;
int g_originalHealth = 0;
bool g_hasOriginalMaxHealth = false;
int g_originalMaxHealth = 0;
bool g_hasOriginalMoveSpeed = false;
float g_originalMoveSpeed = 0.0f;
bool g_hasOriginalMaxFallSpeed = false;
float g_originalMaxFallSpeed = 0.0f;
bool g_hasOriginalJumpHeight = false;
float g_originalJumpHeight = 0.0f;
bool g_hasOriginalFireRate = false;
float g_originalFireRate = 0.0f;
bool g_hasOriginalSkill = false;
int g_originalSkill = 0;
bool g_hasOriginalLives = false;
int g_originalLives = 0;
bool g_hasOriginalAcidRain = false;
BYTE g_originalAcidRain = 0;

void RemovePlayerBaseHook();
void RestoreConfiguredLocks();
void CleanupTrainer();
DWORD WINAPI UnloadThread(LPVOID);
void Log(const char* msg);

void RequestUnload() {
    if (g_unloadRequested) {
        return;
    }

    Log("[卸载] 已请求从游戏中卸载 DLL，准备恢复修改并释放文件占用");
    g_unloadRequested = true;
    g_aobScanActive = false;
    HANDLE thread = CreateThread(nullptr, 0, UnloadThread, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
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

    RemovePlayerBaseHook();

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

    ShowCursor(TRUE);
    Log("[卸载] 清理完成，DLL 即将从游戏进程脱出");
}

uintptr_t GetLivesAddress(uintptr_t playerBase) {
    uintptr_t livesBase = Memory::Read<uintptr_t>(playerBase + 0x218);
    return livesBase ? livesBase + Offsets::Lives : 0;
}

void RestoreConfiguredLocks() {
    if (!g_lockPlayerBase) {
        return;
    }

    if (g_hasOriginalHealth) {
        Memory::Write<int>(g_lockPlayerBase + Offsets::Health, g_originalHealth);
        g_hasOriginalHealth = false;
    }
    if (g_hasOriginalMaxHealth) {
        Memory::Write<int>(g_lockPlayerBase + Offsets::MaxHealth, g_originalMaxHealth);
        g_hasOriginalMaxHealth = false;
    }
    if (g_hasOriginalMoveSpeed) {
        Memory::Write<float>(g_lockPlayerBase + Offsets::MoveSpeed, g_originalMoveSpeed);
        g_hasOriginalMoveSpeed = false;
    }
    if (g_hasOriginalMaxFallSpeed) {
        Memory::Write<float>(g_lockPlayerBase + Offsets::MaxFallSpeed, g_originalMaxFallSpeed);
        g_hasOriginalMaxFallSpeed = false;
    }
    if (g_hasOriginalJumpHeight) {
        Memory::Write<float>(g_lockPlayerBase + Offsets::JumpHeight, g_originalJumpHeight);
        g_hasOriginalJumpHeight = false;
    }
    if (g_hasOriginalFireRate) {
        Memory::Write<float>(g_lockPlayerBase + Offsets::FireRate, g_originalFireRate);
        g_hasOriginalFireRate = false;
    }
    if (g_hasOriginalSkill) {
        Memory::Write<int>(g_lockPlayerBase + Offsets::Skill, g_originalSkill);
        g_hasOriginalSkill = false;
    }
    if (g_hasOriginalLives) {
        uintptr_t livesAddress = GetLivesAddress(g_lockPlayerBase);
        if (livesAddress) {
            Memory::Write<int>(livesAddress, g_originalLives);
        }
        g_hasOriginalLives = false;
    }
    if (g_hasOriginalAcidRain) {
        Memory::Write<BYTE>(g_lockPlayerBase + Offsets::CanBeCoveredByAcidRain, g_originalAcidRain);
        g_hasOriginalAcidRain = false;
    }

    g_lockPlayerBase = 0;
    Log("[卸载] 已恢复锁定项的原始数值");
}

void ApplyConfiguredLocks(uintptr_t playerBase) {
    if (!playerBase) {
        return;
    }

    if (g_lockPlayerBase && g_lockPlayerBase != playerBase) {
        RestoreConfiguredLocks();
    }
    if (!g_lockPlayerBase) {
        g_lockPlayerBase = playerBase;
    }

    auto& config = Config::Instance();

    if (config.health.enabled) {
        if (!g_hasOriginalHealth) {
            g_originalHealth = Memory::Read<int>(playerBase + Offsets::Health);
            g_hasOriginalHealth = true;
        }
        Memory::Write<int>(playerBase + Offsets::Health, (int)config.health.value);
    }
    if (config.maxHealth.enabled) {
        if (!g_hasOriginalMaxHealth) {
            g_originalMaxHealth = Memory::Read<int>(playerBase + Offsets::MaxHealth);
            g_hasOriginalMaxHealth = true;
        }
        Memory::Write<int>(playerBase + Offsets::MaxHealth, (int)config.maxHealth.value);
    }
    if (config.moveSpeed.enabled) {
        if (!g_hasOriginalMoveSpeed) {
            g_originalMoveSpeed = Memory::Read<float>(playerBase + Offsets::MoveSpeed);
            g_hasOriginalMoveSpeed = true;
        }
        Memory::Write<float>(playerBase + Offsets::MoveSpeed, (float)config.moveSpeed.value);
    }
    if (config.maxFallSpeed.enabled) {
        if (!g_hasOriginalMaxFallSpeed) {
            g_originalMaxFallSpeed = Memory::Read<float>(playerBase + Offsets::MaxFallSpeed);
            g_hasOriginalMaxFallSpeed = true;
        }
        Memory::Write<float>(playerBase + Offsets::MaxFallSpeed, (float)config.maxFallSpeed.value);
    }
    if (config.jumpHeight.enabled) {
        if (!g_hasOriginalJumpHeight) {
            g_originalJumpHeight = Memory::Read<float>(playerBase + Offsets::JumpHeight);
            g_hasOriginalJumpHeight = true;
        }
        Memory::Write<float>(playerBase + Offsets::JumpHeight, (float)config.jumpHeight.value);
    }
    if (config.fireRate.enabled) {
        if (!g_hasOriginalFireRate) {
            g_originalFireRate = Memory::Read<float>(playerBase + Offsets::FireRate);
            g_hasOriginalFireRate = true;
        }
        Memory::Write<float>(playerBase + Offsets::FireRate, (float)config.fireRate.value);
    }
    if (config.skill.enabled) {
        if (!g_hasOriginalSkill) {
            g_originalSkill = Memory::Read<int>(playerBase + Offsets::Skill);
            g_hasOriginalSkill = true;
        }
        Memory::Write<int>(playerBase + Offsets::Skill, (int)config.skill.value);
    }
    if (config.lives.enabled) {
        uintptr_t livesAddress = GetLivesAddress(playerBase);
        if (livesAddress) {
            if (!g_hasOriginalLives) {
                g_originalLives = Memory::Read<int>(livesAddress);
                g_hasOriginalLives = true;
            }
            Memory::Write<int>(livesAddress, (int)config.lives.value);
        }
    }
    if (config.acidRain.enabled) {
        if (!g_hasOriginalAcidRain) {
            g_originalAcidRain = Memory::Read<BYTE>(playerBase + Offsets::CanBeCoveredByAcidRain);
            g_hasOriginalAcidRain = true;
        }
        int value = (int)config.acidRain.value;
        if (value < 0) value = 0;
        if (value > 255) value = 255;
        Memory::Write<BYTE>(playerBase + Offsets::CanBeCoveredByAcidRain, (BYTE)value);
    }
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

bool IsExecutableMemory(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOACCESS)) {
        return false;
    }

    DWORD readableExecutableFlags = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (protect & readableExecutableFlags) != 0;
}

uintptr_t ScanExecutableMemory(const BYTE* pattern, const char* mask) {
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);

    uintptr_t current = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t maxAddress = (uintptr_t)si.lpMaximumApplicationAddress;
    size_t maskLen = strlen(mask);

    while (current < maxAddress) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery((LPCVOID)current, &mbi, sizeof(mbi))) {
            current += 0x1000;
            continue;
        }

        uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
        uintptr_t regionEnd = regionStart + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && IsExecutableMemory(mbi.Protect) && regionEnd > regionStart && regionEnd - regionStart >= maskLen) {
            for (uintptr_t address = regionStart; address + maskLen <= regionEnd; ++address) {
                bool found = true;
                for (size_t i = 0; i < maskLen; ++i) {
                    if (mask[i] == 'x' && *(BYTE*)(address + i) != pattern[i]) {
                        found = false;
                        break;
                    }
                }

                if (found) {
                    return address;
                }
            }
        }

        current = regionEnd > current ? regionEnd : current + 0x1000;
    }

    return 0;
}

void AOBScanThread();

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
    FILE* f = fopen(GetLogPath(), "a");
    if (f) {
        fprintf(f, "%s\n", msg);
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

bool InstallPlayerBaseHook(uintptr_t injectPoint) {
    constexpr size_t originalSize = 6;
    constexpr size_t codeSize = 64;

    Logf("[AOB] 找到 CT 注入点: 0x%p", (void*)injectPoint);
    Logf("[AOB] 原始 6 字节: %02X %02X %02X %02X %02X %02X",
        *(BYTE*)injectPoint, *(BYTE*)(injectPoint + 1), *(BYTE*)(injectPoint + 2),
        *(BYTE*)(injectPoint + 3), *(BYTE*)(injectPoint + 4), *(BYTE*)(injectPoint + 5));

    g_playerHookCode = AllocateNear(injectPoint, codeSize);
    if (!g_playerHookCode) {
        Log("[AOB] 无法在注入点 ±2GB 内分配 newmem，5 字节 jmp 跳不到，Hook 未安装");
        return false;
    }

    Logf("[AOB] newmem 分配成功: 0x%p", g_playerHookCode);
    memcpy(g_playerHookOriginal, (void*)injectPoint, originalSize);

    BYTE* code = (BYTE*)g_playerHookCode;
    size_t offset = 0;

    // cmp word ptr [rdi+104h], 1
    BYTE cmpHero[] = { 0x66, 0x83, 0xBF, 0x04, 0x01, 0x00, 0x00, 0x01 };
    memcpy(code + offset, cmpHero, sizeof(cmpHero));
    offset += sizeof(cmpHero);

    // jne +15; non-player skips only the capture block, then still executes the original instruction
    code[offset++] = 0x75;
    code[offset++] = 0x0F;

    // Save RAX/EAX before using RAX as a scratch register.
    // The overwritten CT instruction is mov [rdi+000005DC],eax, so EAX must stay intact.
    code[offset++] = 0x50;

    // mov rax, &g_capturedPlayerBase
    code[offset++] = 0x48;
    code[offset++] = 0xB8;
    *(uintptr_t*)(code + offset) = (uintptr_t)&g_capturedPlayerBase;
    offset += sizeof(uintptr_t);

    // mov [rax], rdi
    BYTE movPlayerBase[] = { 0x48, 0x89, 0x38 };
    memcpy(code + offset, movPlayerBase, sizeof(movPlayerBase));
    offset += sizeof(movPlayerBase);

    // Restore RAX/EAX before executing the original instruction.
    code[offset++] = 0x58;

    // mov [rdi+000005DC], eax (original instruction from Broforce.CT)
    memcpy(code + offset, g_playerHookOriginal, originalSize);
    offset += originalSize;

    // mov rax, injectPoint + 6; jmp rax
    code[offset++] = 0x48;
    code[offset++] = 0xB8;
    *(uintptr_t*)(code + offset) = injectPoint + originalSize;
    offset += sizeof(uintptr_t);
    code[offset++] = 0xFF;
    code[offset++] = 0xE0;

    DWORD oldProtect;
    if (!VirtualProtect((void*)injectPoint, originalSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(g_playerHookCode, 0, MEM_RELEASE);
        g_playerHookCode = nullptr;
        Logf("[AOB] 修改注入点内存保护失败，错误码: %lu", GetLastError());
        return false;
    }

    BYTE patch[originalSize] = { 0xE9, 0, 0, 0, 0, 0x90 };
    uintptr_t relativeDetour = (uintptr_t)g_playerHookCode - injectPoint - 5;
    if (!IsRel32Reachable(injectPoint, (uintptr_t)g_playerHookCode)) {
        VirtualProtect((void*)injectPoint, originalSize, oldProtect, &oldProtect);
        VirtualFree(g_playerHookCode, 0, MEM_RELEASE);
        g_playerHookCode = nullptr;
        Log("[AOB] newmem 距离注入点超过 ±2GB，5 字节 jmp 无效，Hook 未安装");
        return false;
    }
    *(int32_t*)(patch + 1) = (int32_t)relativeDetour;
    memcpy((void*)injectPoint, patch, sizeof(patch));
    VirtualProtect((void*)injectPoint, originalSize, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), (void*)injectPoint, originalSize);

    g_playerHookTarget = (void*)injectPoint;
    g_playerHookInstalled = true;
    Log("[AOB] 玩家基址 Hook 安装成功：已写入 jmp newmem + nop");
    Log("[AOB] 现在进入关卡/使用技能或等待游戏调用 set_SpecialAmmo，控制台应打印玩家基址");
    return true;
}

void RemovePlayerBaseHook() {
    if (!g_playerHookInstalled || !g_playerHookTarget) {
        return;
    }

    DWORD oldProtect;
    if (VirtualProtect(g_playerHookTarget, sizeof(g_playerHookOriginal), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(g_playerHookTarget, g_playerHookOriginal, sizeof(g_playerHookOriginal));
        VirtualProtect(g_playerHookTarget, sizeof(g_playerHookOriginal), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), g_playerHookTarget, sizeof(g_playerHookOriginal));
    }

    if (g_playerHookCode) {
        VirtualFree(g_playerHookCode, 0, MEM_RELEASE);
        g_playerHookCode = nullptr;
    }

    g_playerHookTarget = nullptr;
    g_playerHookInstalled = false;
}

void AOBScanThread() {
    const BYTE pattern[] = { 0x89, 0x87, 0xDC, 0x05, 0x00, 0x00 };
    const char* mask = "xxxxxx";

    g_aobScanActive = true;
    g_aobScanFinished = false;
    Log("[AOB] 开始持续扫描 Mono/JIT 可执行内存：89 87 DC 05 00 00");
    Log("[AOB] 说明：CT 的注入点是 TestVanDammeAnim:set_SpecialAmmo，这是 Mono JIT 代码，通常不会在游戏 EXE 主模块里一开始就出现");

    while (g_aobScanActive && !g_playerHookInstalled) {
        ++g_aobScanAttempts;
        uintptr_t injectPoint = ScanExecutableMemory(pattern, mask);

        if (injectPoint) {
            Logf("[AOB] 第 %d 次扫描找到 CT 特征码，地址=0x%p", g_aobScanAttempts, (void*)injectPoint);
            if (!InstallPlayerBaseHook(injectPoint)) {
                Log("[AOB] 找到了特征码但 Hook 安装失败，停止持续扫描，避免反复修改内存");
            }
            break;
        }

        if (g_aobScanAttempts == 1 || g_aobScanAttempts % 5 == 0) {
            Logf("[AOB] 第 %d 次扫描仍未找到；请进入关卡/切换角色/使用一次特殊弹药，让 Mono 编译 set_SpecialAmmo", g_aobScanAttempts);
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

        // Use SetWindowLongPtrW for Unicode windows
        GUI::s_originalWndProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)GUI::WndProc);
        Logf("[窗口] WndProc Hook 完成，原窗口过程=0x%p", GUI::s_originalWndProc);

        g_initialized = true;
        Log("[DX11] ImGui 初始化完成");
    }

    if (g_capturedPlayerBase) {
        Cheat::Instance().SetPlayerBase(g_capturedPlayerBase);
        ApplyConfiguredLocks(g_capturedPlayerBase);
        if (!g_loggedPlayerBase) {
            Logf("[玩家基址] 捕获成功: 0x%p，GUI 已停止等待", (void*)g_capturedPlayerBase);
            g_loggedPlayerBase = true;
        }
    }

    // Pass render target view to GUI
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
    // Let the ImGui button frame return to the original Present before unpatching vtables.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    for (int i = 0; i < 30 && !g_aobScanFinished; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    CleanupTrainer();

    HMODULE module = g_module;
    if (g_consoleAllocated) {
        Log("[卸载] 控制台即将关闭");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        FreeConsole();
        g_consoleAllocated = false;
    }

    if (module) {
        FreeLibraryAndExitThread(module, 0);
    }
    return 0;
}

DWORD WINAPI MainThread(LPVOID) {
    InitializeConsole();
    Config::Instance().Load();
    Log("[启动] DLL_PROCESS_ATTACH 已完成，主线程启动");

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

    uintptr_t moduleBase = Memory::GetModuleBase(nullptr);
    size_t moduleSize = Memory::GetModuleSize(nullptr);
    Logf("[AOB] 游戏主模块基址=0x%p，模块大小=0x%zX", (void*)moduleBase, moduleSize);
    Log("[AOB] 主模块扫描跳过：CT 注入点属于 Mono/JIT 代码，不一定在 Broforce.exe 主模块里");
    Log("[AOB] 改为后台持续扫描整个进程的可执行内存，直到 JIT 代码出现");
    std::thread(AOBScanThread).detach();

    Log("[启动] 初始化结束：按 INSERT 打开菜单，控制台会显示扫描和玩家基址状态");
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
