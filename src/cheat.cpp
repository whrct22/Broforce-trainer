#include "cheat.h"
#include "memory.h"

Cheat& Cheat::Instance() {
    static Cheat instance;
    return instance;
}

bool Cheat::Initialize() {
    // UserData 基址钩子在 dllmain.cpp 中通过 Dojo NTR.CT 的 AOB 扫描安装
    return true;
}

uintptr_t Cheat::GetCoinAddress() const {
    if (!IsUserDataValid()) {
        return 0;
    }

    uintptr_t coinBase = Memory::Read<uintptr_t>(m_userDataBase + Offsets::CoinPointer);
    return coinBase ? coinBase + Offsets::CoinValue : 0;
}

void Cheat::SetCoin(int value) {
    uintptr_t address = GetCoinAddress();
    if (address) {
        Memory::Write<int>(address, value);
    }
}

void Cheat::SetFishCoin(int value) {
    if (IsUserDataValid()) {
        Memory::Write<int>(m_userDataBase + Offsets::FishCoin, value);
    }
}

void Cheat::SetEnergy(int value) {
    if (IsUserDataValid()) {
        Memory::Write<int>(m_userDataBase + Offsets::Energy, value);
    }
}

void Cheat::SetDay(int value) {
    if (IsUserDataValid()) {
        Memory::Write<int>(m_userDataBase + Offsets::Day, value);
    }
}

void Cheat::SetHour(int value) {
    if (IsUserDataValid()) {
        Memory::Write<int>(m_userDataBase + Offsets::Hour, value);
    }
}

void Cheat::SetMinute(int value) {
    if (IsUserDataValid()) {
        Memory::Write<int>(m_userDataBase + Offsets::Minute, value);
    }
}

int Cheat::GetCoin() const {
    uintptr_t address = GetCoinAddress();
    return address ? Memory::Read<int>(address) : 0;
}

int Cheat::GetFishCoin() const {
    if (IsUserDataValid()) {
        return Memory::Read<int>(m_userDataBase + Offsets::FishCoin);
    }
    return 0;
}

int Cheat::GetEnergy() const {
    if (IsUserDataValid()) {
        return Memory::Read<int>(m_userDataBase + Offsets::Energy);
    }
    return 0;
}

int Cheat::GetDay() const {
    if (IsUserDataValid()) {
        return Memory::Read<int>(m_userDataBase + Offsets::Day);
    }
    return 0;
}

int Cheat::GetHour() const {
    if (IsUserDataValid()) {
        return Memory::Read<int>(m_userDataBase + Offsets::Hour);
    }
    return 0;
}

int Cheat::GetMinute() const {
    if (IsUserDataValid()) {
        return Memory::Read<int>(m_userDataBase + Offsets::Minute);
    }
    return 0;
}
