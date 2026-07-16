#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <cstdint>

// 玩家数据结构偏移 (从CT文件提取)
namespace Offsets {
    constexpr uintptr_t Health = 0xD4;           // 血量 (4字节)
    constexpr uintptr_t MaxHealth = 0xD8;        // 最大血量 (4字节)
    constexpr uintptr_t PosX = 0xDC;             // X坐标 (Float)
    constexpr uintptr_t PosY = 0xE0;             // Y坐标 (Float)
    constexpr uintptr_t IsHero = 0x104;          // 是否为玩家 (2字节)
    constexpr uintptr_t CanBeCoveredByAcidRain = 0x4D5;  // 能否被酸雨覆盖 (Byte)
    constexpr uintptr_t MoveSpeed = 0x52C;       // 移动速度 (Float)
    constexpr uintptr_t MaxFallSpeed = 0x53C;    // 最大坠落速度 (Float)
    constexpr uintptr_t FireRate = 0x5CC;        // 射速 (Float)
    constexpr uintptr_t Skill = 0x5DC;           // 技能 (4字节)
    constexpr uintptr_t JumpHeight = 0x620;      // 跳跃高度 (Float)
    constexpr uintptr_t Lives = 0x120;           // 生命条数 (需要多级指针: 0x218 -> 0x120)
}

class Cheat {
public:
    static Cheat& Instance();

    // 初始化
    bool Initialize();

    // 玩家基址
    uintptr_t GetPlayerBase() const { return m_playerBase; }
    void SetPlayerBase(uintptr_t base) { m_playerBase = base; }
    bool IsPlayerValid() const { return m_playerBase != 0; }

    // 血量修改
    void SetGodMode(bool enable);
    void SetHealth(int value);
    void SetMaxHealth(int value);

    // 移动相关
    void SetSpeedHack(bool enable);
    void SetFlyHack(bool enable);
    void SetSuperJump(bool enable);

    // 武器相关
    void SetRapidFire(bool enable);
    void SetInfiniteSkill(bool enable);

    // 其他
    void SetAcidRainImmunity(bool enable);

    // 读取当前值
    int GetHealth() const;
    int GetMaxHealth() const;
    float GetMoveSpeed() const;
    float GetFireRate() const;

private:
    Cheat() = default;
    ~Cheat() = default;

    uintptr_t m_playerBase = 0;
    uintptr_t m_originalCode = 0;

    // 安装玩家基址钩子
    void InstallPlayerBaseHook(uintptr_t hookAddress);

    // 状态
    bool m_godMode = false;
    bool m_speedHack = false;
    bool m_flyHack = false;
    bool m_superJump = false;
    bool m_rapidFire = false;
    bool m_infiniteSkill = false;
    bool m_acidRainImmunity = false;

    // 保存原始值
    float m_originalSpeed = 0.0f;
    float m_originalFireRate = 0.0f;
    float m_originalJumpHeight = 0.0f;
};
