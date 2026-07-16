#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>

class Memory {
public:
    // 读取内存
    template<typename T>
    static T Read(uintptr_t address) {
        T value{};
        ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, &value, sizeof(T), nullptr);
        return value;
    }

    // 写入内存
    template<typename T>
    static void Write(uintptr_t address, T value) {
        DWORD oldProtect;
        VirtualProtect((LPVOID)address, sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect);
        WriteProcessMemory(GetCurrentProcess(), (LPVOID)address, &value, sizeof(T), nullptr);
        VirtualProtect((LPVOID)address, sizeof(T), oldProtect, &oldProtect);
    }

    // 读取指针链
    static uintptr_t ReadPointerChain(uintptr_t base, std::initializer_list<uintptr_t> offsets);

    // 获取模块基址
    static uintptr_t GetModuleBase(const char* moduleName);
    static size_t GetModuleSize(const char* moduleName);

    // NOP填充
    static void Nop(uintptr_t address, size_t length);

    // 写入跳转
    static void WriteJump(uintptr_t address, uintptr_t destination);

    // AOB模式扫描
    static uintptr_t ScanPattern(uintptr_t start, size_t size, const BYTE* pattern, const char* mask);
};
