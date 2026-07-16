#pragma once

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

    LockConfig health{ false, 9999.0 };
    LockConfig maxHealth{ false, 9999.0 };
    LockConfig moveSpeed{ false, 15.0 };
    LockConfig maxFallSpeed{ false, -10.0 };
    LockConfig jumpHeight{ false, 20.0 };
    LockConfig fireRate{ false, 5.0 };
    LockConfig skill{ false, 9999.0 };
    LockConfig lives{ false, 99.0 };
    LockConfig acidRain{ false, 0.0 };

    double guiAlpha = 0.92;

private:
    Config() = default;

    bool m_dirty = false;
    const char* m_path = "D:\\c++-trainer\\trainer_config.ini";
};
