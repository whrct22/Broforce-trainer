#include "gui.h"
#include "cheat.h"
#include "memory.h"
#include "config.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <cstdio>
#include <algorithm>
#include <string>
#include <cctype>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

WNDPROC GUI::s_originalWndProc = nullptr;

GUI& GUI::Instance() {
    static GUI instance;
    return instance;
}

void Log(const char* msg);
extern uintptr_t g_capturedPlayerBase;
extern bool g_playerHookInstalled;
extern void* g_playerHookTarget;
extern bool g_aobScanActive;
extern bool g_aobScanFinished;
extern int g_aobScanAttempts;
extern bool g_unloadRequested;
extern bool g_cleanupDone;
extern void* g_playerHookCode;
extern HWND g_gameHwnd;
extern bool g_initialized;
extern ID3D11Device* g_device;
extern ID3D11DeviceContext* g_deviceContext;
extern ID3D11RenderTargetView* g_renderTargetView;
extern HMODULE g_module;
void RequestUnload();

float g_guiScale = 1.0f;

float CalculateGuiScale(HWND hwnd) {
    RECT clientRect = {};
    if (!GetClientRect(hwnd, &clientRect)) {
        return 1.0f;
    }

    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) {
        return 1.0f;
    }

    float scale = std::min(width / 1600.0f, height / 900.0f);

    return std::clamp(scale, 0.85f, 1.25f);
}

bool GetWindowDebugInfo(HWND hwnd, int& width, int& height, bool& fullscreen) {
    width = 0;
    height = 0;
    fullscreen = false;

    RECT clientRect = {};
    if (!GetClientRect(hwnd, &clientRect)) {
        return false;
    }

    width = clientRect.right - clientRect.left;
    height = clientRect.bottom - clientRect.top;

    MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        POINT topLeft = { 0, 0 };
        ClientToScreen(hwnd, &topLeft);
        fullscreen = topLeft.x <= monitorInfo.rcMonitor.left + 2 &&
            topLeft.y <= monitorInfo.rcMonitor.top + 2 &&
            width >= (monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left) - 4 &&
            height >= (monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top) - 4;
    }

    return true;
}

uintptr_t ReadPointerChain(uintptr_t base, std::initializer_list<uintptr_t> offsets) {
    uintptr_t address = base;
    for (auto it = offsets.begin(); it != offsets.end(); ++it) {
        if (it != offsets.end() - 1) {
            address = Memory::Read<uintptr_t>(address + *it);
            if (!address) {
                return 0;
            }
        } else {
            address += *it;
        }
    }
    return address;
}

std::string ReadUtf16String(uintptr_t address, size_t maxChars) {
    if (!address) {
        return "未读取到";
    }

    std::string result;
    result.reserve(maxChars * 3);

    for (size_t i = 0; i < maxChars; ++i) {
        wchar_t ch = Memory::Read<wchar_t>(address + i * sizeof(wchar_t));
        if (ch == 0) {
            break;
        }

        char utf8[4] = {};
        int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8, sizeof(utf8), nullptr, nullptr);
        if (len > 0) {
            result.append(utf8, len);
        }
    }

    return result.empty() ? "空" : result;
}

std::string ToLowerAscii(std::string value) {
    for (char& ch : value) {
        ch = (char)std::tolower((unsigned char)ch);
    }
    return value;
}

ImVec4 GetMaterialColor(const std::string& material) {
    std::string lower = ToLowerAscii(material);
    if (lower.find("stone") != std::string::npos) return ImVec4(0.62f, 0.62f, 0.66f, 1.0f);
    if (lower.find("dirt") != std::string::npos) return ImVec4(0.58f, 0.36f, 0.18f, 1.0f);
    if (lower.find("metal") != std::string::npos) return ImVec4(0.70f, 0.78f, 0.86f, 1.0f);
    if (lower.find("grass") != std::string::npos) return ImVec4(0.25f, 0.85f, 0.25f, 1.0f);
    if (lower.find("wood") != std::string::npos) return ImVec4(0.76f, 0.48f, 0.22f, 1.0f);
    if (lower.find("slime") != std::string::npos) return ImVec4(0.35f, 1.00f, 0.55f, 1.0f);
    if (material.empty() || material == "空" || lower.find("empty") != std::string::npos || lower.find("none") != std::string::npos) {
        return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    }
    return ImVec4(1.00f, 0.90f, 0.35f, 1.0f);
}

bool LoadChineseFont(ImGuiIO& io) {
    const char* fontPaths[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",     // 微软雅黑
        "C:\\Windows\\Fonts\\simhei.ttf",   // 黑体
        "C:\\Windows\\Fonts\\simsun.ttc"    // 宋体
    };

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = true;

    for (const char* fontPath : fontPaths) {
        if (GetFileAttributesA(fontPath) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }

        ImFont* font = io.Fonts->AddFontFromFileTTF(
            fontPath,
            18.0f,
            &fontConfig,
            io.Fonts->GetGlyphRangesChineseFull()
        );

        if (font) {
            io.FontDefault = font;
            Log("[GUI] 中文字体加载成功");
            return true;
        }
    }

    Log("[GUI] 中文字体加载失败：未找到微软雅黑/黑体/宋体，中文可能显示为方块");
    return false;
}

bool GUI::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    Log("[GUI] ImGui 初始化开始");

    m_device = device;
    m_context = context;
    m_hwnd = hwnd;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    LoadChineseFont(io);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context);

    Log("[GUI] ImGui 初始化完成");
    return true;
}

void DrawLockInt(const char* label, uintptr_t address, Config::LockConfig& config, int& originalValue, bool& hasOriginal) {
    ImGui::PushID(label);

    bool previous = config.enabled;
    bool locked = config.enabled;
    int value = (int)config.value;

    if (ImGui::Checkbox("##lock", &locked)) {
        config.enabled = locked;
        Config::Instance().MarkDirty();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(220.0f * g_guiScale);
    ImGui::SetNextItemWidth(170.0f * g_guiScale);
    if (ImGui::InputInt("##value", &value)) {
        config.value = value;
        Config::Instance().MarkDirty();
    }

    if (config.enabled && !previous) {
        originalValue = Memory::Read<int>(address);
        hasOriginal = true;
        Log("[锁定] 已保存原始整数值并开始锁定");
        Config::Instance().SaveIfDirty();
    } else if (!config.enabled && previous && hasOriginal) {
        Memory::Write<int>(address, originalValue);
        Log("[锁定] 已取消锁定并恢复原始整数值");
        Config::Instance().SaveIfDirty();
    }

    if (config.enabled) {
        Memory::Write<int>(address, (int)config.value);
    }

    if (hasOriginal) {
        ImGui::SameLine();
        ImGui::TextDisabled("原始: %d", originalValue);
    }
    ImGui::PopID();
}

void DrawLockFloat(const char* label, uintptr_t address, Config::LockConfig& config, float& originalValue, bool& hasOriginal) {
    ImGui::PushID(label);

    bool previous = config.enabled;
    bool locked = config.enabled;
    float value = (float)config.value;

    if (ImGui::Checkbox("##lock", &locked)) {
        config.enabled = locked;
        Config::Instance().MarkDirty();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(220.0f * g_guiScale);
    ImGui::SetNextItemWidth(170.0f * g_guiScale);
    if (ImGui::InputFloat("##value", &value, 0.1f, 1.0f, "%.3f")) {
        config.value = value;
        Config::Instance().MarkDirty();
    }

    if (config.enabled && !previous) {
        originalValue = Memory::Read<float>(address);
        hasOriginal = true;
        Log("[锁定] 已保存原始浮点值并开始锁定");
        Config::Instance().SaveIfDirty();
    } else if (!config.enabled && previous && hasOriginal) {
        Memory::Write<float>(address, originalValue);
        Log("[锁定] 已取消锁定并恢复原始浮点值");
        Config::Instance().SaveIfDirty();
    }

    if (config.enabled) {
        Memory::Write<float>(address, (float)config.value);
    }

    if (hasOriginal) {
        ImGui::SameLine();
        ImGui::TextDisabled("原始: %.3f", originalValue);
    }
    ImGui::PopID();
}

void DrawLockByte(const char* label, uintptr_t address, Config::LockConfig& config, BYTE& originalValue, bool& hasOriginal) {
    ImGui::PushID(label);

    bool previous = config.enabled;
    bool locked = config.enabled;
    int value = std::clamp((int)config.value, 0, 255);

    if (ImGui::Checkbox("##lock", &locked)) {
        config.enabled = locked;
        Config::Instance().MarkDirty();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(220.0f * g_guiScale);
    ImGui::SetNextItemWidth(170.0f * g_guiScale);
    if (ImGui::InputInt("##value", &value)) {
        value = std::clamp(value, 0, 255);
        config.value = value;
        Config::Instance().MarkDirty();
    }

    if (config.enabled && !previous) {
        originalValue = Memory::Read<BYTE>(address);
        hasOriginal = true;
        Log("[锁定] 已保存原始字节值并开始锁定");
        Config::Instance().SaveIfDirty();
    } else if (!config.enabled && previous && hasOriginal) {
        Memory::Write<BYTE>(address, originalValue);
        Log("[锁定] 已取消锁定并恢复原始字节值");
        Config::Instance().SaveIfDirty();
    }

    if (config.enabled) {
        Memory::Write<BYTE>(address, (BYTE)std::clamp((int)config.value, 0, 255));
    }

    if (hasOriginal) {
        ImGui::SameLine();
        ImGui::TextDisabled("原始: %u", (unsigned)originalValue);
    }
    ImGui::PopID();
}

void GUI::Render() {
    if (!m_visible || !m_context || !m_renderTargetView) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = true;

    float guiScale = CalculateGuiScale(m_hwnd);
    g_guiScale = guiScale;
    ImGui::SetNextWindowSize(ImVec2(760.0f * guiScale, 600.0f * guiScale), ImGuiCond_FirstUseEver);

    auto& config = Config::Instance();
    ImGui::SetNextWindowBgAlpha((float)config.guiAlpha);
    ImGui::Begin("Broforce 修改器 [INSERT]");
    ImGui::SetWindowFontScale(guiScale);

    auto& cheat = Cheat::Instance();

    ImGui::Text("调试状态");
    int windowWidth = 0;
    int windowHeight = 0;
    bool isFullscreen = false;
    GetWindowDebugInfo(m_hwnd, windowWidth, windowHeight, isFullscreen);
    ImGui::Text("窗口: %dx%d，%s，缩放: %.2fx", windowWidth, windowHeight, isFullscreen ? "全屏" : "窗口化", guiScale);
    ImGui::Text("DLL模块: 0x%p，初始化: %s，清理: %s", g_module, g_initialized ? "是" : "否", g_cleanupDone ? "是" : "否");
    ImGui::Text("D3D: Device=0x%p Context=0x%p RTV=0x%p", g_device, g_deviceContext, g_renderTargetView);
    ImGui::Text("AOB Hook: %s，扫描: %s/%s，第 %d 次", g_playerHookInstalled ? "已安装" : "未安装", g_aobScanActive ? "进行中" : "停止", g_aobScanFinished ? "已结束" : "未结束", g_aobScanAttempts);
    ImGui::Text("注入点: 0x%p，newmem: 0x%p", g_playerHookTarget, g_playerHookCode);
    ImGui::Text("捕获到的玩家基址: 0x%p", (void*)g_capturedPlayerBase);
    ImGui::Text("配置文件: D:\\c++-trainer\\trainer_config.ini");
    ImGui::Text("日志文件: D:\\c++-trainer\\trainer_log.txt");
    float alpha = (float)config.guiAlpha;
    ImGui::SetNextItemWidth(180.0f * guiScale);
    if (ImGui::SliderFloat("窗口透明度", &alpha, 0.20f, 1.00f, "%.2f")) {
        config.guiAlpha = alpha;
        config.MarkDirty();
    }
    config.SaveIfDirty();
    ImGui::Separator();

    if (g_unloadRequested) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "正在脱出 DLL，请稍等...");
    } else if (ImGui::Button("恢复修改并从游戏中脱出 DLL")) {
        RequestUnload();
    }
    ImGui::TextWrapped("用途：恢复 AOB/D3D/窗口 Hook，释放 BroforceTrainer.dll 文件占用，方便下次重新编译。游戏会继续运行。");
    ImGui::Separator();

    if (!cheat.IsPlayerValid()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "正在等待玩家基址...");
        ImGui::TextWrapped("请确认已经进入关卡，并让角色使用一次技能/特殊弹药；如果控制台没有出现“捕获成功”，说明 CT 特征码 Hook 没有被游戏执行。");
        ImGui::Separator();
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "玩家基址: 0x%p", (void*)cheat.GetPlayerBase());
        ImGui::Separator();

        ImGui::Text("血量: %d / %d", cheat.GetHealth(), cheat.GetMaxHealth());
        ImGui::Text("移动速度: %.2f", cheat.GetMoveSpeed());
        ImGui::Text("射速: %.2f", cheat.GetFireRate());
        float posX = Memory::Read<float>(cheat.GetPlayerBase() + Offsets::PosX);
        float posY = Memory::Read<float>(cheat.GetPlayerBase() + Offsets::PosY);
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.00f, 1.0f), "玩家X坐标: %.3f", posX);
        ImGui::TextColored(ImVec4(1.00f, 0.55f, 0.35f, 1.0f), "玩家Y坐标: %.3f", posY);
        uintptr_t standingTextAddress = ReadPointerChain(cheat.GetPlayerBase(), { 0x330, 0x14 });
        std::string standingText = ReadUtf16String(standingTextAddress, 999);
        ImGui::TextColored(GetMaterialColor(standingText), "当前玩家站在什么物品上面: %s", standingText.c_str());
        uintptr_t livesBase = Memory::Read<uintptr_t>(cheat.GetPlayerBase() + 0x218);
        if (livesBase) {
            ImGui::Text("生命条数: %d", Memory::Read<int>(livesBase + Offsets::Lives));
        }
        ImGui::Text("界面缩放: %.2fx", guiScale);
        ImGui::Separator();

        uintptr_t base = cheat.GetPlayerBase();
        auto& config = Config::Instance();

        static int originalHealth = 0;
        static bool hasOriginalHealth = false;
        DrawLockInt("锁定血量", base + Offsets::Health, config.health, originalHealth, hasOriginalHealth);

        static int originalMaxHealth = 0;
        static bool hasOriginalMaxHealth = false;
        DrawLockInt("锁定最大血量", base + Offsets::MaxHealth, config.maxHealth, originalMaxHealth, hasOriginalMaxHealth);

        static float originalMoveSpeed = 0.0f;
        static bool hasOriginalMoveSpeed = false;
        DrawLockFloat("锁定移动速度", base + Offsets::MoveSpeed, config.moveSpeed, originalMoveSpeed, hasOriginalMoveSpeed);

        static float originalFallSpeed = 0.0f;
        static bool hasOriginalFallSpeed = false;
        DrawLockFloat("锁定最大坠落速度", base + Offsets::MaxFallSpeed, config.maxFallSpeed, originalFallSpeed, hasOriginalFallSpeed);

        static float originalJump = 0.0f;
        static bool hasOriginalJump = false;
        DrawLockFloat("锁定跳跃高度", base + Offsets::JumpHeight, config.jumpHeight, originalJump, hasOriginalJump);

        static float originalFireRate = 0.0f;
        static bool hasOriginalFireRate = false;
        DrawLockFloat("锁定射速", base + Offsets::FireRate, config.fireRate, originalFireRate, hasOriginalFireRate);

        static int originalSkill = 0;
        static bool hasOriginalSkill = false;
        DrawLockInt("锁定技能/特殊弹药", base + Offsets::Skill, config.skill, originalSkill, hasOriginalSkill);

        static int originalLives = 0;
        static bool hasOriginalLives = false;
        uintptr_t livesPointer = Memory::Read<uintptr_t>(base + 0x218);
        if (livesPointer) {
            DrawLockInt("锁定生命条数", livesPointer + Offsets::Lives, config.lives, originalLives, hasOriginalLives);
        } else {
            ImGui::TextDisabled("生命条数指针暂未就绪");
        }

        static BYTE originalAcid = 0;
        static bool hasOriginalAcid = false;
        DrawLockByte("锁定酸雨覆盖状态", base + Offsets::CanBeCoveredByAcidRain, config.acidRain, originalAcid, hasOriginalAcid);

        config.SaveIfDirty();
    }

    ImGui::End();

    ImGui::Render();

    if (m_renderTargetView) {
        m_context->OMSetRenderTargets(1, &m_renderTargetView, nullptr);
    }

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void GUI::Shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    m_renderTargetView = nullptr;
}

void GUI::Toggle() {
    m_visible = !m_visible;
    ImGui::GetIO().MouseDrawCursor = m_visible;
    ShowCursor(m_visible ? TRUE : FALSE);
    Log(m_visible ? "[菜单] 已显示，ImGui 软件鼠标已启用" : "[菜单] 已隐藏，ImGui 软件鼠标已关闭");
}

LRESULT GUI::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return TRUE;
    }

    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_INSERT) {
            Instance().Toggle();
            return 0;
        }
        break;
    }

    if (s_originalWndProc) {
        return CallWindowProcW(s_originalWndProc, hWnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
