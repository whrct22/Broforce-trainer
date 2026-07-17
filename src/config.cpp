#include "config.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void Log(const char* msg);

namespace {
    std::string GetLocalConfigPath() {
        HMODULE module = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetLocalConfigPath),
            &module
        );

        char modulePath[MAX_PATH] = {};
        DWORD length = GetModuleFileNameA(module, modulePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return "trainer_config.ini";
        }

        char* slash = strrchr(modulePath, '\\');
        if (!slash) {
            return "trainer_config.ini";
        }

        *(slash + 1) = '\0';
        return std::string(modulePath) + "trainer_config.ini";
    }

    int ReadInt(const char* path, const char* section, const char* key, int defaultValue) {
        return GetPrivateProfileIntA(section, key, defaultValue, path);
    }

    double ReadDouble(const char* path, const char* section, const char* key, double defaultValue) {
        char defaultText[64] = {};
        char valueText[64] = {};
        snprintf(defaultText, sizeof(defaultText), "%.6f", defaultValue);
        GetPrivateProfileStringA(section, key, defaultText, valueText, sizeof(valueText), path);
        return atof(valueText);
    }

    void WriteBool(const char* path, const char* section, const char* key, bool value) {
        WritePrivateProfileStringA(section, key, value ? "1" : "0", path);
    }

    void WriteDouble(const char* path, const char* section, const char* key, double value) {
        char text[64] = {};
        snprintf(text, sizeof(text), "%.6f", value);
        WritePrivateProfileStringA(section, key, text, path);
    }

    void LoadLock(const char* path, const char* section, Config::LockConfig& config) {
        config.enabled = ReadInt(path, section, "enabled", config.enabled ? 1 : 0) != 0;
        config.value = ReadDouble(path, section, "value", config.value);
    }

    void SaveLock(const char* path, const char* section, const Config::LockConfig& config) {
        WriteBool(path, section, "enabled", config.enabled);
        WriteDouble(path, section, "value", config.value);
    }
}

Config& Config::Instance() {
    static Config instance;
    return instance;
}

const char* Config::Path() const {
    return m_path.c_str();
}

bool Config::Load() {
    if (m_path.empty()) {
        m_path = GetLocalConfigPath();
    }

    LoadLock(m_path.c_str(), "health", health);
    LoadLock(m_path.c_str(), "max_health", maxHealth);
    LoadLock(m_path.c_str(), "move_speed", moveSpeed);
    LoadLock(m_path.c_str(), "max_fall_speed", maxFallSpeed);
    LoadLock(m_path.c_str(), "jump_height", jumpHeight);
    LoadLock(m_path.c_str(), "fire_rate", fireRate);
    LoadLock(m_path.c_str(), "skill", skill);
    LoadLock(m_path.c_str(), "lives", lives);
    LoadLock(m_path.c_str(), "acid_rain", acidRain);
    guiAlpha = ReadDouble(m_path.c_str(), "gui", "alpha", guiAlpha);
    if (guiAlpha < 0.20) guiAlpha = 0.20;
    if (guiAlpha > 1.00) guiAlpha = 1.00;

    Save();
    Log("[配置] 已读取配置文件，缺失时自动写入默认配置");
    return true;
}

bool Config::Save() const {
    SaveLock(m_path.c_str(), "health", health);
    SaveLock(m_path.c_str(), "max_health", maxHealth);
    SaveLock(m_path.c_str(), "move_speed", moveSpeed);
    SaveLock(m_path.c_str(), "max_fall_speed", maxFallSpeed);
    SaveLock(m_path.c_str(), "jump_height", jumpHeight);
    SaveLock(m_path.c_str(), "fire_rate", fireRate);
    SaveLock(m_path.c_str(), "skill", skill);
    SaveLock(m_path.c_str(), "lives", lives);
    SaveLock(m_path.c_str(), "acid_rain", acidRain);
    WriteDouble(m_path.c_str(), "gui", "alpha", guiAlpha);
    return true;
}

void Config::SaveIfDirty() {
    if (!m_dirty) {
        return;
    }

    Save();
    m_dirty = false;
    Log("[配置] 配置已自动保存");
}

void Config::MarkDirty() {
    m_dirty = true;
}
