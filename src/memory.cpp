#include "memory.h"
#include <algorithm>
#include <winnt.h>

uintptr_t Memory::ReadPointerChain(uintptr_t base, std::initializer_list<uintptr_t> offsets) {
    uintptr_t address = base;
    for (auto it = offsets.begin(); it != offsets.end(); ++it) {
        if (it != offsets.end() - 1) {
            address = Read<uintptr_t>(address + *it);
        } else {
            address = address + *it;
        }
    }
    return address;
}

uintptr_t Memory::GetModuleBase(const char* moduleName) {
    return (uintptr_t)GetModuleHandleA(moduleName);
}

size_t Memory::GetModuleSize(const char* moduleName) {
    uintptr_t moduleBase = GetModuleBase(moduleName);
    if (!moduleBase) {
        return 0;
    }

    auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }

    auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(moduleBase + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }

    return ntHeaders->OptionalHeader.SizeOfImage;
}

void Memory::Nop(uintptr_t address, size_t length) {
    DWORD oldProtect;
    VirtualProtect((LPVOID)address, length, PAGE_EXECUTE_READWRITE, &oldProtect);
    memset((LPVOID)address, 0x90, length);
    VirtualProtect((LPVOID)address, length, oldProtect, &oldProtect);
}

void Memory::WriteJump(uintptr_t address, uintptr_t destination) {
    DWORD oldProtect;
    VirtualProtect((LPVOID)address, 5, PAGE_EXECUTE_READWRITE, &oldProtect);

    uintptr_t relativeAddress = destination - address - 5;

    *(BYTE*)address = 0xE9;
    *(DWORD*)(address + 1) = (DWORD)relativeAddress;

    VirtualProtect((LPVOID)address, 5, oldProtect, &oldProtect);
}

uintptr_t Memory::ScanPattern(uintptr_t start, size_t size, const BYTE* pattern, const char* mask) {
    if (!start || !size || !pattern || !mask) {
        return 0;
    }

    size_t maskLen = strlen(mask);
    uintptr_t end = start + size;
    uintptr_t current = start;

    while (current < end) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery((LPCVOID)current, &mbi, sizeof(mbi))) {
            current += 0x1000;
            continue;
        }

        uintptr_t regionStart = std::max(current, (uintptr_t)mbi.BaseAddress);
        uintptr_t regionEnd = std::min(end, (uintptr_t)mbi.BaseAddress + mbi.RegionSize);
        bool readable = mbi.State == MEM_COMMIT &&
            !(mbi.Protect & PAGE_GUARD) &&
            !(mbi.Protect & PAGE_NOACCESS);

        if (readable && regionEnd > regionStart && regionEnd - regionStart >= maskLen) {
            for (uintptr_t address = regionStart; address + maskLen <= regionEnd; ++address) {
                bool found = true;
                for (size_t j = 0; j < maskLen; j++) {
                    if (mask[j] == 'x' && *(BYTE*)(address + j) != pattern[j]) {
                        found = false;
                        break;
                    }
                }
                if (found) {
                    return address;
                }
            }
        }

        current = regionEnd > current ? regionEnd : current + 0x1000;
    }

    return 0;
}
