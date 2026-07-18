#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <cstdint>

// UserData 数据结构偏移 (从 Dojo NTR.CT 提取)
namespace Offsets {
    constexpr uintptr_t Day = 0x20;        // 天数 (4字节)
    constexpr uintptr_t Hour = 0x24;       // 小时 (4字节)
    constexpr uintptr_t Minute = 0x28;     // 分钟 (4字节)
    constexpr uintptr_t Energy = 0x50;     // 能量 (4字节)
    constexpr uintptr_t FishCoin = 0x90;   // 渔币 (4字节)
    constexpr uintptr_t CoinPointer = 0x98; // 金币 List/封装对象指针
    constexpr uintptr_t CoinValue = 0x10;  // 金币对象内数值偏移 (4字节)
}

class Cheat {
public:
    static Cheat& Instance();

    // 初始化
    bool Initialize();

    // UserData 基址
    uintptr_t GetUserDataBase() const { return m_userDataBase; }
    void SetUserDataBase(uintptr_t base) { m_userDataBase = base; }
    bool IsUserDataValid() const { return m_userDataBase != 0; }

    // 兼容旧调用命名
    uintptr_t GetPlayerBase() const { return GetUserDataBase(); }
    void SetPlayerBase(uintptr_t base) { SetUserDataBase(base); }
    bool IsPlayerValid() const { return IsUserDataValid(); }

    // 写入数值
    void SetCoin(int value);
    void SetFishCoin(int value);
    void SetEnergy(int value);
    void SetDay(int value);
    void SetHour(int value);
    void SetMinute(int value);

    // 读取当前值
    int GetCoin() const;
    int GetFishCoin() const;
    int GetEnergy() const;
    int GetDay() const;
    int GetHour() const;
    int GetMinute() const;

    uintptr_t GetCoinAddress() const;

private:
    Cheat() = default;
    ~Cheat() = default;

    uintptr_t m_userDataBase = 0;
};
