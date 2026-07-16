#include "cheat.h"
#include "memory.h"
#include <thread>
#include <chrono>

Cheat& Cheat::Instance() {
    static Cheat instance;
    return instance;
}

bool Cheat::Initialize() {
    // 玩家基址钩子在dllmain.cpp中通过AOB扫描安装
    return true;
}

void Cheat::SetGodMode(bool enable) {
    m_godMode = enable;
    if (enable && IsPlayerValid()) {
        SetHealth(9999);
        SetMaxHealth(9999);
    }
}

void Cheat::SetHealth(int value) {
    if (IsPlayerValid()) {
        Memory::Write<int>(m_playerBase + Offsets::Health, value);
    }
}

void Cheat::SetMaxHealth(int value) {
    if (IsPlayerValid()) {
        Memory::Write<int>(m_playerBase + Offsets::MaxHealth, value);
    }
}

void Cheat::SetSpeedHack(bool enable) {
    m_speedHack = enable;
    if (IsPlayerValid()) {
        if (enable) {
            if (m_originalSpeed == 0.0f) {
                m_originalSpeed = Memory::Read<float>(m_playerBase + Offsets::MoveSpeed);
            }
            Memory::Write<float>(m_playerBase + Offsets::MoveSpeed, m_originalSpeed * 3.0f);
        } else if (m_originalSpeed != 0.0f) {
            Memory::Write<float>(m_playerBase + Offsets::MoveSpeed, m_originalSpeed);
            m_originalSpeed = 0.0f;
        }
    }
}

void Cheat::SetFlyHack(bool enable) {
    m_flyHack = enable;
    if (enable && IsPlayerValid()) {
        Memory::Write<float>(m_playerBase + Offsets::MaxFallSpeed, -10.0f);
    } else if (!enable && IsPlayerValid()) {
        Memory::Write<float>(m_playerBase + Offsets::MaxFallSpeed, 15.0f);
    }
}

void Cheat::SetSuperJump(bool enable) {
    m_superJump = enable;
    if (IsPlayerValid()) {
        if (enable) {
            if (m_originalJumpHeight == 0.0f) {
                m_originalJumpHeight = Memory::Read<float>(m_playerBase + Offsets::JumpHeight);
            }
            Memory::Write<float>(m_playerBase + Offsets::JumpHeight, m_originalJumpHeight * 5.0f);
        } else if (m_originalJumpHeight != 0.0f) {
            Memory::Write<float>(m_playerBase + Offsets::JumpHeight, m_originalJumpHeight);
            m_originalJumpHeight = 0.0f;
        }
    }
}

void Cheat::SetRapidFire(bool enable) {
    m_rapidFire = enable;
    if (IsPlayerValid()) {
        if (enable) {
            if (m_originalFireRate == 0.0f) {
                m_originalFireRate = Memory::Read<float>(m_playerBase + Offsets::FireRate);
            }
            Memory::Write<float>(m_playerBase + Offsets::FireRate, m_originalFireRate * 5.0f);
        } else if (m_originalFireRate != 0.0f) {
            Memory::Write<float>(m_playerBase + Offsets::FireRate, m_originalFireRate);
            m_originalFireRate = 0.0f;
        }
    }
}

void Cheat::SetInfiniteSkill(bool enable) {
    m_infiniteSkill = enable;
    if (enable && IsPlayerValid()) {
        std::thread([this]() {
            while (m_infiniteSkill && IsPlayerValid()) {
                Memory::Write<int>(m_playerBase + Offsets::Skill, 9999);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }).detach();
    }
}

void Cheat::SetAcidRainImmunity(bool enable) {
    m_acidRainImmunity = enable;
    if (IsPlayerValid()) {
        Memory::Write<BYTE>(m_playerBase + Offsets::CanBeCoveredByAcidRain, enable ? 0 : 1);
    }
}

int Cheat::GetHealth() const {
    if (IsPlayerValid()) {
        return Memory::Read<int>(m_playerBase + Offsets::Health);
    }
    return 0;
}

int Cheat::GetMaxHealth() const {
    if (IsPlayerValid()) {
        return Memory::Read<int>(m_playerBase + Offsets::MaxHealth);
    }
    return 0;
}

float Cheat::GetMoveSpeed() const {
    if (IsPlayerValid()) {
        return Memory::Read<float>(m_playerBase + Offsets::MoveSpeed);
    }
    return 0.0f;
}

float Cheat::GetFireRate() const {
    if (IsPlayerValid()) {
        return Memory::Read<float>(m_playerBase + Offsets::FireRate);
    }
    return 0.0f;
}
