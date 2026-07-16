#include "config.h"
#include <Windows.h>
#include <cstdio>

void Log(const char* msg);

namespace {
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

bool Config::Load() {
    LoadLock(m_path, "health", health);
    LoadLock(m_path, "max_health", maxHealth);
    LoadLock(m_path, "move_speed", moveSpeed);
    LoadLock(m_path, "max_fall_speed", maxFallSpeed);
    LoadLock(m_path, "jump_height", jumpHeight);
    LoadLock(m_path, "fire_rate", fireRate);
    LoadLock(m_path, "skill", skill);
    LoadLock(m_path, "lives", lives);
    LoadLock(m_path, "acid_rain", acidRain);
    guiAlpha = ReadDouble(m_path, "gui", "alpha", guiAlpha);
    if (guiAlpha < 0.20) guiAlpha = 0.20;
    if (guiAlpha > 1.00) guiAlpha = 1.00;

    Save();
    Log("[配置] 已读取配置文件，缺失时自动写入默认配置");
    return true;
}

bool Config::Save() const {
    SaveLock(m_path, "health", health);
    SaveLock(m_path, "max_health", maxHealth);
    SaveLock(m_path, "move_speed", moveSpeed);
    SaveLock(m_path, "max_fall_speed", maxFallSpeed);
    SaveLock(m_path, "jump_height", jumpHeight);
    SaveLock(m_path, "fire_rate", fireRate);
    SaveLock(m_path, "skill", skill);
    SaveLock(m_path, "lives", lives);
    SaveLock(m_path, "acid_rain", acidRain);
    WriteDouble(m_path, "gui", "alpha", guiAlpha);
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
