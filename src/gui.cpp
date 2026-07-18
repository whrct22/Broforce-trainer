#include "gui.h"
#include "cheat.h"
#include "memory.h"
#include "config.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <cctype>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

WNDPROC GUI::s_originalWndProc = nullptr;

GUI& GUI::Instance() {
    static GUI instance;
    return instance;
}

void Log(const char* msg);
extern uintptr_t g_capturedUserDataBase;
extern bool g_userDataHookInstalled;
extern void* g_userDataHookTarget;
extern bool g_aobScanActive;
extern bool g_aobScanFinished;
extern int g_aobScanAttempts;
extern bool g_unloadRequested;
extern bool g_cleanupDone;
extern void* g_userDataHookCode;
extern HWND g_gameHwnd;
extern bool g_initialized;
extern ID3D11Device* g_device;
extern ID3D11DeviceContext* g_deviceContext;
extern ID3D11RenderTargetView* g_renderTargetView;
extern HMODULE g_module;
extern HMODULE g_monoModuleHandle;
extern int g_monoModuleLoadCount;
HMODULE LoadMonoModuleManaged();
extern bool g_monoProbeFinished;
extern bool g_monoProbeSucceeded;
extern uintptr_t g_monoSetSpecialAmmoMethod;
extern uintptr_t g_monoSetSpecialAmmoInjectPoint;
extern uintptr_t g_monoCapturedPlayerBase;
extern bool g_monoPlayerHookInstalled;
extern char g_monoProbeSummary[2048];
void RequestUnload();

float g_guiScale = 1.0f;

namespace {
    using MonoInitializeFn = int(__cdecl*)(const char*);
    using MonoListClassesFn = int(__cdecl*)(const char*, char*, int, int);
    using MonoDumpClassFn = int(__cdecl*)(const char*, const char*, const char*, char*, int, int, int);

    std::string& MonoBrowserText();
    const char*& MonoBrowserStatus();

    HMODULE LoadMonoModuleForGui() {
        return LoadMonoModuleManaged();
    }

    struct MonoClassEntry {
        std::string nameSpace;
        std::string name;
        std::string address;
        std::string label;
    };

    std::vector<MonoClassEntry>& MonoClassList() {
        static std::vector<MonoClassEntry> classes;
        return classes;
    }

    int& MonoSelectedClassIndex() {
        static int selected = -1;
        return selected;
    }

    bool ContainsTextInsensitive(const std::string& value, const char* filter) {
        if (!filter || !*filter) {
            return true;
        }

        std::string haystack = value;
        std::string needle = filter;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return haystack.find(needle) != std::string::npos;
    }

    void ParseMonoClassList(const std::string& text) {
        auto& classes = MonoClassList();
        classes.clear();
        MonoSelectedClassIndex() = -1;

        const char* begin = text.c_str();
        const char* end = begin + text.size();
        const char* line = begin;
        while (line < end) {
            const char* next = (const char*)memchr(line, '\n', (size_t)(end - line));
            const char* lineEnd = next ? next : end;
            if (line < lineEnd && *line != '#') {
                std::string row(line, lineEnd);
                size_t p1 = row.find('|');
                size_t p2 = p1 == std::string::npos ? std::string::npos : row.find('|', p1 + 1);
                if (p1 != std::string::npos && p2 != std::string::npos) {
                    MonoClassEntry entry;
                    entry.nameSpace = row.substr(0, p1);
                    entry.name = row.substr(p1 + 1, p2 - p1 - 1);
                    entry.address = row.substr(p2 + 1);
                    entry.label = entry.nameSpace.empty() ? entry.name : entry.nameSpace + "." + entry.name;
                    classes.push_back(entry);
                }
            }
            line = next ? next + 1 : end;
        }
    }

    void LoadMonoClassList() {
        HMODULE monoModule = LoadMonoModuleForGui();
        if (!monoModule) {
            MonoBrowserStatus() = "MonoModule.dll 加载失败";
            MonoBrowserText() = "MonoModule.dll 加载失败。请确认它和 trainer DLL 在同一目录。";
            return;
        }

        auto monoInitialize = (MonoInitializeFn)GetProcAddress(monoModule, "MonoModule_Initialize");
        auto listClasses = (MonoListClassesFn)GetProcAddress(monoModule, "MonoModule_ListClasses");
        if (!monoInitialize || !listClasses) {
            MonoBrowserStatus() = "当前 MonoModule.dll 没有 ListClasses 导出";
            MonoBrowserText() = "请先卸载/关闭游戏，重新编译并替换新版 MonoModule.dll。";
            return;
        }

        monoInitialize(Config::Instance().Path());
        std::string buffer;
        buffer.resize(512 * 1024);
        int written = listClasses("Assembly-CSharp.dll", buffer.data(), (int)buffer.size(), 4096);
        if (written < 0) written = 0;
        if (written >= (int)buffer.size()) written = (int)buffer.size() - 1;
        buffer.resize((size_t)written);
        ParseMonoClassList(buffer);

        char status[128] = {};
        snprintf(status, sizeof(status), "已加载类列表: %zu 个类", MonoClassList().size());
        static std::string statusText;
        statusText = status;
        MonoBrowserStatus() = statusText.c_str();
        MonoBrowserText() = MonoClassList().empty() ? buffer : "左侧选择类后，右侧会显示字段、方法和 JIT 地址。";
    }

    std::string& MonoBrowserText() {
        static std::string text = "点击刷新按钮读取 Mono 类信息。";
        return text;
    }

    const char*& MonoBrowserStatus() {
        static const char* status = "未刷新";
        return status;
    }

    void RefreshMonoClassDump(const char* nameSpace, const char* className) {
        HMODULE monoModule = LoadMonoModuleForGui();
        if (!monoModule) {
            MonoBrowserStatus() = "MonoModule.dll 加载失败";
            MonoBrowserText() = "MonoModule.dll 加载失败。请确认它和 trainer DLL 在同一目录。";
            return;
        }

        auto monoInitialize = (MonoInitializeFn)GetProcAddress(monoModule, "MonoModule_Initialize");
        auto dumpClass = (MonoDumpClassFn)GetProcAddress(monoModule, "MonoModule_DumpClass");
        if (!monoInitialize || !dumpClass) {
            MonoBrowserStatus() = "当前 MonoModule.dll 没有 DumpClass 导出";
            MonoBrowserText() = "请先卸载/关闭游戏，重新编译并替换新版 MonoModule.dll。";
            return;
        }

        monoInitialize(Config::Instance().Path());
        std::string buffer;
        buffer.resize(256 * 1024);
        int written = dumpClass("Assembly-CSharp.dll", nameSpace ? nameSpace : "", className ? className : "", buffer.data(), (int)buffer.size(), 260, 1);
        if (written < 0) written = 0;
        if (written >= (int)buffer.size()) written = (int)buffer.size() - 1;
        buffer.resize((size_t)written);
        MonoBrowserText() = buffer.empty() ? "DumpClass 返回空结果。" : buffer;

        static std::string statusText;
        statusText = "已加载类详情: ";
        if (nameSpace && *nameSpace) {
            statusText += nameSpace;
            statusText += ".";
        }
        statusText += className ? className : "";
        MonoBrowserStatus() = statusText.c_str();
    }

    void RefreshMonoClassDump(const char* className) {
        RefreshMonoClassDump("", className);
    }

    bool LineMatchesFilter(const char* lineStart, const char* lineEnd, const char* filter) {
        if (!filter || !*filter) {
            return true;
        }
        std::string line(lineStart, lineEnd);
        return line.find(filter) != std::string::npos;
    }

    std::string& MonoCopiedNotice() {
        static std::string notice;
        return notice;
    }

    void CopyTextToClipboard(const std::string& text, const char* notice) {
        ImGui::SetClipboardText(text.c_str());
        MonoCopiedNotice() = notice ? notice : "已复制";
    }

    std::string ExtractFirstHexAddress(const char* start, const char* end) {
        const char* p = start;
        while (p + 2 <= end) {
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                const char* q = p + 2;
                while (q < end && std::isxdigit((unsigned char)*q)) {
                    ++q;
                }
                if (q > p + 2) {
                    return std::string(p, q);
                }
            }
            ++p;
        }
        return std::string();
    }

    void DrawDumpLine(const char* line, const char* lineEnd, int lineIndex) {
        ImGui::PushID(lineIndex);
        std::string row(line, lineEnd);
        std::string address = ExtractFirstHexAddress(line, lineEnd);
        if (!address.empty()) {
            if (ImGui::SmallButton("复制地址")) {
                CopyTextToClipboard(address, "已复制地址");
            }
            ImGui::SameLine(0.0f, 10.0f * g_guiScale);
        }
        if (ImGui::SmallButton("复制行")) {
            CopyTextToClipboard(row, "已复制当前行");
        }
        ImGui::SameLine(0.0f, 10.0f * g_guiScale);
        ImGui::TextUnformatted(line, lineEnd);
        ImGui::PopID();
    }

    void DrawMonoDumpText(const std::string& text, const char* filter) {
        const char* begin = text.c_str();
        const char* end = begin + text.size();
        const char* line = begin;
        int lineIndex = 0;
        while (line < end) {
            const char* next = (const char*)memchr(line, '\n', (size_t)(end - line));
            const char* lineEnd = next ? next : end;
            if (LineMatchesFilter(line, lineEnd, filter)) {
                DrawDumpLine(line, lineEnd, lineIndex);
            }
            line = next ? next + 1 : end;
            ++lineIndex;
        }
    }
}

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
    ImGui::SameLine(180.0f * g_guiScale);
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
        hasOriginal = false;
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

void GUI::Render() {
    if (!m_visible || !m_context || !m_renderTargetView) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = true;

    float guiScale = CalculateGuiScale(m_hwnd);
    g_guiScale = guiScale;
    ImGui::SetNextWindowSize(ImVec2(700.0f * guiScale, 520.0f * guiScale), ImGuiCond_FirstUseEver);

    auto& config = Config::Instance();
    ImGui::SetNextWindowBgAlpha((float)config.guiAlpha);
    ImGui::Begin("Dojo NTR 修改器 [INSERT]");
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
    ImGui::Text("AOB Hook: %s，扫描: %s/%s，第 %d 次", g_userDataHookInstalled ? "已安装" : "未安装", g_aobScanActive ? "进行中" : "停止", g_aobScanFinished ? "已结束" : "未结束", g_aobScanAttempts);
    ImGui::Text("注入点: 0x%p，newmem: 0x%p", g_userDataHookTarget, g_userDataHookCode);
    ImGui::Text("捕获到的 UserData 基址: 0x%p", (void*)g_capturedUserDataBase);
    ImGui::Text("配置文件: %s", config.Path());
    ImGui::Text("日志文件: DojoNTRTrainer.dll 同目录下的 trainer_log.txt");
    ImGui::Separator();
    ImGui::Text("Broforce Mono 探测: %s / %s", g_monoProbeFinished ? "已完成" : "进行中", g_monoProbeSucceeded ? "成功" : "未确认");
    ImGui::Text("set_SpecialAmmo 方法地址: 0x%p", (void*)g_monoSetSpecialAmmoMethod);
    ImGui::Text("CE +31 / mov [rdi+5DC],eax 地址: 0x%p", (void*)g_monoSetSpecialAmmoInjectPoint);
    ImGui::Text("Mono 玩家 Hook: %s，捕获玩家基址: 0x%p", g_monoPlayerHookInstalled ? "已安装" : "未安装", (void*)g_monoCapturedPlayerBase);
    ImGui::Text("MonoModule: 句柄=0x%p，Trainer持有引用=%d（卸载 Trainer 时会一并释放）", g_monoModuleHandle, g_monoModuleLoadCount);
    ImGui::TextWrapped("%s", g_monoProbeSummary);

    if (ImGui::CollapsingHeader("Mono 类浏览器 / Assembly-CSharp", ImGuiTreeNodeFlags_DefaultOpen)) {
        static char className[128] = "TestVanDammeAnim";
        static char classFilter[128] = "Player";
        static char detailFilter[128] = "";
        static float browserFontScale = 1.25f;
        static float browserHeightSetting = 430.0f;
        static float classListWidthSetting = 340.0f;
        static bool listOnRight = false;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f * guiScale, 8.0f * guiScale));
        ImGui::Text("浏览器状态: %s", MonoBrowserStatus());
        ImGui::TextDisabled("左/右布局可调；点击类名后会在详情区显示字段偏移、字段类型、方法参数和 JIT 地址。") ;

        ImGui::SetNextItemWidth(180.0f * guiScale);
        ImGui::SliderFloat("浏览器字号", &browserFontScale, 0.90f, 1.80f, "%.2fx");
        ImGui::SameLine(0.0f, 18.0f * guiScale);
        ImGui::SetNextItemWidth(180.0f * guiScale);
        ImGui::SliderFloat("面板高度", &browserHeightSetting, 260.0f, 760.0f, "%.0f");
        ImGui::SameLine(0.0f, 18.0f * guiScale);
        ImGui::SetNextItemWidth(180.0f * guiScale);
        ImGui::SliderFloat("类列表宽度", &classListWidthSetting, 220.0f, 560.0f, "%.0f");
        ImGui::SameLine(0.0f, 18.0f * guiScale);
        ImGui::Checkbox("类列表放右边", &listOnRight);

        if (ImGui::Button("加载 / 刷新全部类")) {
            LoadMonoClassList();
        }
        ImGui::SameLine(0.0f, 18.0f * guiScale);
        ImGui::SetNextItemWidth(220.0f * guiScale);
        ImGui::InputText("指定类名", className, sizeof(className));
        ImGui::SameLine(0.0f, 18.0f * guiScale);
        if (ImGui::Button("打开指定类")) {
            RefreshMonoClassDump(className);
        }
        ImGui::SameLine(0.0f, 18.0f * guiScale);
        if (ImGui::Button("打开 TestVanDammeAnim")) {
            RefreshMonoClassDump("TestVanDammeAnim");
        }

        ImGui::SetNextItemWidth(260.0f * guiScale);
        ImGui::InputText("类名过滤", classFilter, sizeof(classFilter));
        ImGui::SameLine(0.0f, 18.0f * guiScale);
        ImGui::SetNextItemWidth(260.0f * guiScale);
        ImGui::InputText("详情过滤", detailFilter, sizeof(detailFilter));
        if (!MonoCopiedNotice().empty()) {
            ImGui::SameLine(0.0f, 18.0f * guiScale);
            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "%s", MonoCopiedNotice().c_str());
        }

        float browserHeight = browserHeightSetting * guiScale;
        float leftWidth = classListWidthSetting * guiScale;
        auto& classes = MonoClassList();

        auto drawClassList = [&]() {
            ImGui::BeginChild("MonoClassListPanel", ImVec2(leftWidth, browserHeight), true);
            ImGui::SetWindowFontScale(browserFontScale);
            ImGui::Text("类列表：%zu 个", classes.size());
            ImGui::TextDisabled("过滤：%s", classFilter[0] ? classFilter : "无");
            ImGui::Separator();
            int visibleCount = 0;
            for (int i = 0; i < (int)classes.size(); ++i) {
                const auto& entry = classes[(size_t)i];
                if (!ContainsTextInsensitive(entry.label, classFilter) && !ContainsTextInsensitive(entry.address, classFilter)) {
                    continue;
                }
                ++visibleCount;
                bool selected = MonoSelectedClassIndex() == i;
                std::string buttonLabel = entry.label + "##" + entry.address;
                if (ImGui::Selectable(buttonLabel.c_str(), selected)) {
                    MonoSelectedClassIndex() = i;
                    RefreshMonoClassDump(entry.nameSpace.c_str(), entry.name.c_str());
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("类地址：%s\n完整类名：%s\n右键可复制", entry.address.c_str(), entry.label.c_str());
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("复制完整类名")) {
                        CopyTextToClipboard(entry.label, "已复制类名");
                    }
                    if (ImGui::MenuItem("复制类地址")) {
                        CopyTextToClipboard(entry.address, "已复制类地址");
                    }
                    ImGui::EndPopup();
                }
            }
            if (visibleCount == 0) {
                ImGui::TextDisabled("没有匹配的类。先点“加载/刷新全部类”，或调整过滤。") ;
            }
            ImGui::SetWindowFontScale(1.0f);
            ImGui::EndChild();
        };

        auto drawClassDetail = [&](float panelWidth) {
            ImGui::BeginChild("MonoClassDetailPanel", ImVec2(panelWidth, browserHeight), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::SetWindowFontScale(browserFontScale);
            if (MonoSelectedClassIndex() >= 0 && MonoSelectedClassIndex() < (int)classes.size()) {
                const auto& entry = classes[(size_t)MonoSelectedClassIndex()];
                ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "当前选中：%s", entry.label.c_str());
                ImGui::Text("类地址 MonoClass*: %s", entry.address.c_str());
                if (ImGui::Button("复制当前类名")) {
                    CopyTextToClipboard(entry.label, "已复制当前类名");
                }
                ImGui::SameLine(0.0f, 12.0f * g_guiScale);
                if (ImGui::Button("复制当前类地址")) {
                    CopyTextToClipboard(entry.address, "已复制当前类地址");
                }
                ImGui::SameLine(0.0f, 12.0f * g_guiScale);
                if (ImGui::Button("复制当前详情全文")) {
                    CopyTextToClipboard(MonoBrowserText(), "已复制详情全文");
                }
                ImGui::Separator();
            } else {
                ImGui::TextDisabled("尚未选中类。请从类列表点击一个类，或者输入类名后点“打开指定类”。");
                ImGui::Separator();
            }
            DrawMonoDumpText(MonoBrowserText(), detailFilter);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::EndChild();
        };

        float availableWidth = ImGui::GetContentRegionAvail().x;
        float detailWidth = std::max(260.0f * guiScale, availableWidth - leftWidth - 16.0f * guiScale);
        if (listOnRight) {
            drawClassDetail(detailWidth);
            ImGui::SameLine(0.0f, 12.0f * guiScale);
            drawClassList();
        } else {
            drawClassList();
            ImGui::SameLine(0.0f, 12.0f * guiScale);
            drawClassDetail(0.0f);
        }
        ImGui::PopStyleVar();
    }
    ImGui::Separator();
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
    ImGui::TextWrapped("用途：恢复 AOB/D3D/窗口 Hook，释放 DojoNTRTrainer.dll 文件占用，方便下次重新编译。游戏会继续运行。");
    ImGui::Separator();

    if (g_monoProbeSucceeded) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Mono 已定位 Broforce set_SpecialAmmo，不再需要 Dojo UserData / GameAssembly AOB");
        ImGui::Text("方法起始地址: 0x%p", (void*)g_monoSetSpecialAmmoMethod);
        ImGui::Text("CE 注入点(+31): 0x%p", (void*)g_monoSetSpecialAmmoInjectPoint);

        if (!g_monoCapturedPlayerBase) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.0f, 1.0f), "正在等待 Broforce PlayerBase...");
            ImGui::TextWrapped("Mono 已经找到 CE 里的 TestVanDammeAnim:set_SpecialAmmo+31，并已准备/安装 Hook。请在关卡里使用一次特殊弹药/技能，让 set_SpecialAmmo 执行一次，就会从 RDI 捕获 player base。");
        } else {
            uintptr_t playerBase = g_monoCapturedPlayerBase;
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Broforce PlayerBase: 0x%p", (void*)playerBase);
            ImGui::Text("isHero [0x104]: %u", (unsigned)Memory::Read<unsigned short>(playerBase + 0x104));
            ImGui::Text("health [0xD4]: %d", Memory::Read<int>(playerBase + 0xD4));
            ImGui::Text("maxHealth [0xD8]: %d", Memory::Read<int>(playerBase + 0xD8));
            ImGui::Text("pos [0xDC/0xE0]: %.3f, %.3f", Memory::Read<float>(playerBase + 0xDC), Memory::Read<float>(playerBase + 0xE0));
            ImGui::Text("specialAmmo [0x5DC]: %d", Memory::Read<int>(playerBase + 0x5DC));
        }
        ImGui::Separator();
    } else if (!cheat.IsUserDataValid()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "正在等待 UserData 基址...");
        ImGui::TextWrapped("Mono 没有定位到 Broforce 方法时，才会回退到 Dojo NTR.CT 的 AOB/UserData 路径。");
        ImGui::Separator();
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "UserData 基址: 0x%p", (void*)cheat.GetUserDataBase());
        ImGui::Separator();

        ImGui::Text("金币: %d", cheat.GetCoin());
        ImGui::Text("渔币: %d", cheat.GetFishCoin());
        ImGui::Text("能量: %d", cheat.GetEnergy());
        ImGui::Text("时间: 第 %d 天 %02d:%02d", cheat.GetDay(), cheat.GetHour(), cheat.GetMinute());
        ImGui::Separator();

        static int originalCoin = 0;
        static bool hasOriginalCoin = false;
        uintptr_t coinAddress = cheat.GetCoinAddress();
        if (coinAddress) {
            DrawLockInt("锁定金币", coinAddress, config.coin, originalCoin, hasOriginalCoin);
        } else {
            ImGui::TextDisabled("金币指针暂未就绪");
        }

        uintptr_t base = cheat.GetUserDataBase();
        static int originalFishCoin = 0;
        static bool hasOriginalFishCoin = false;
        DrawLockInt("锁定渔币", base + Offsets::FishCoin, config.fishCoin, originalFishCoin, hasOriginalFishCoin);

        static int originalEnergy = 0;
        static bool hasOriginalEnergy = false;
        DrawLockInt("锁定能量", base + Offsets::Energy, config.energy, originalEnergy, hasOriginalEnergy);

        static int originalDay = 0;
        static bool hasOriginalDay = false;
        DrawLockInt("锁定天数", base + Offsets::Day, config.day, originalDay, hasOriginalDay);

        static int originalHour = 0;
        static bool hasOriginalHour = false;
        DrawLockInt("锁定小时", base + Offsets::Hour, config.hour, originalHour, hasOriginalHour);

        static int originalMinute = 0;
        static bool hasOriginalMinute = false;
        DrawLockInt("锁定分钟", base + Offsets::Minute, config.minute, originalMinute, hasOriginalMinute);

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
