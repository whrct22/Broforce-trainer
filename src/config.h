#pragma once

#include <string>

class Config {
public:
    struct LockConfig {
        bool enabled;
        double value;
    };

    static Config& Instance();

    bool Load();
    bool Save() const;
    void SaveIfDirty();
    void MarkDirty();
    const char* Path() const;

    LockConfig coin{ false, 999999.0 };
    LockConfig fishCoin{ false, 999999.0 };
    LockConfig energy{ false, 999.0 };
    LockConfig day{ false, 1.0 };
    LockConfig hour{ false, 8.0 };
    LockConfig minute{ false, 0.0 };

    double guiAlpha = 0.92;

private:
    Config() = default;

    bool m_dirty = false;
    std::string m_path;
};
