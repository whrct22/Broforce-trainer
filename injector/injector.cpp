#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace {
constexpr const wchar_t* kAllowedProcessNames[] = {
    L"Dojo NTR.exe",
    L"Broforce.exe",
    L"Broforce.bin.x86_64.exe",
};
constexpr const wchar_t* kTrainerDllName = L"DojoNTRTrainer.dll";

const wchar_t* BaseName(const wchar_t* path) {
    const wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t* forwardSlash = wcsrchr(path, L'/');
    const wchar_t* last = slash > forwardSlash ? slash : forwardSlash;
    return last ? last + 1 : path;
}

bool IsAllowedProcessName(const wchar_t* processName) {
    for (const wchar_t* allowedName : kAllowedProcessNames) {
        if (_wcsicmp(processName, allowedName) == 0) {
            return true;
        }
    }
    return false;
}

bool GetInjectorDirectory(wchar_t* directory, DWORD directoryCount) {
    wchar_t modulePath[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return false;
    }

    wchar_t* slash = wcsrchr(modulePath, L'\\');
    if (!slash) {
        return false;
    }

    *slash = L'\0';
    if (wcslen(modulePath) + 1 > directoryCount) {
        return false;
    }

    wcscpy(directory, modulePath);
    return true;
}

bool BuildLocalDllPath(wchar_t* dllPath, DWORD dllPathCount) {
    wchar_t injectorDirectory[MAX_PATH] = {};
    if (!GetInjectorDirectory(injectorDirectory, MAX_PATH)) {
        return false;
    }

    if (wcslen(injectorDirectory) + wcslen(kTrainerDllName) + 2 > dllPathCount) {
        return false;
    }

    swprintf(dllPath, dllPathCount, L"%ls\\%ls", injectorDirectory, kTrainerDllName);
    return true;
}

bool IsExpectedTrainerDll(const wchar_t* fullPath) {
    return _wcsicmp(BaseName(fullPath), kTrainerDllName) == 0;
}

bool GetProcessImagePath(DWORD processId, wchar_t* imagePath, DWORD imagePathCount) {
    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!processHandle) {
        return false;
    }

    DWORD size = imagePathCount;
    bool ok = QueryFullProcessImageNameW(processHandle, 0, imagePath, &size) != FALSE;
    CloseHandle(processHandle);
    return ok;
}
}

// 获取明确允许的 Dojo NTR 进程 ID，不匹配系统进程或其它任意进程。
DWORD FindDojoNTRProcessId(wchar_t* matchedName, DWORD matchedNameCount, wchar_t* imagePath, DWORD imagePathCount) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        printf("[ERROR] CreateToolhelp32Snapshot failed: %lu\n", GetLastError());
        return 0;
    }

    DWORD processId = 0;
    PROCESSENTRY32W processEntry = {};
    processEntry.dwSize = sizeof(processEntry);

    if (Process32FirstW(snapshot, &processEntry)) {
        do {
            if (!IsAllowedProcessName(processEntry.szExeFile)) {
                continue;
            }

            processId = processEntry.th32ProcessID;
            if (matchedName) {
                if (wcslen(processEntry.szExeFile) + 1 <= matchedNameCount) {
                    wcscpy(matchedName, processEntry.szExeFile);
                }
            }
            if (imagePath) {
                imagePath[0] = L'\0';
                GetProcessImagePath(processId, imagePath, imagePathCount);
            }
            break;
        } while (Process32NextW(snapshot, &processEntry));
    }

    CloseHandle(snapshot);
    return processId;
}

bool InjectDLL(DWORD processId, const wchar_t* dllPath) {
    printf("[INFO] Opening target process %lu with DLL-injection permissions only...\n", processId);

    DWORD desiredAccess = PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ;

    HANDLE processHandle = OpenProcess(desiredAccess, FALSE, processId);
    if (processHandle == nullptr) {
        printf("[ERROR] OpenProcess failed: %lu\n", GetLastError());
        return false;
    }

    // 在目标进程中分配内存，只写入明确显示给用户的本地 DLL 完整路径。
    size_t dllPathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    printf("[INFO] Allocating %zu bytes in target process for the DLL path...\n", dllPathSize);

    LPVOID remoteMemory = VirtualAllocEx(
        processHandle,
        nullptr,
        dllPathSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (remoteMemory == nullptr) {
        printf("[ERROR] VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(processHandle);
        return false;
    }

    printf("[INFO] Writing DLL path to target process...\n");
    if (!WriteProcessMemory(processHandle, remoteMemory, dllPath, dllPathSize, nullptr)) {
        printf("[ERROR] WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(processHandle);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLibrary = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr;

    if (loadLibrary == nullptr) {
        printf("[ERROR] GetProcAddress(LoadLibraryW) failed\n");
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(processHandle);
        return false;
    }

    printf("[INFO] Creating remote thread to call LoadLibraryW...\n");
    HANDLE remoteThread = CreateRemoteThread(
        processHandle,
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary),
        remoteMemory,
        0,
        nullptr
    );

    if (remoteThread == nullptr) {
        printf("[ERROR] CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(processHandle);
        return false;
    }

    printf("[INFO] Waiting up to 10 seconds for the DLL to load...\n");
    DWORD waitResult = WaitForSingleObject(remoteThread, 10000);

    if (waitResult == WAIT_TIMEOUT) {
        printf("[ERROR] Remote LoadLibraryW call timed out. The thread was left untouched; no forced termination was used.\n");
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(remoteThread);
        CloseHandle(processHandle);
        return false;
    }

    DWORD exitCode = 0;
    GetExitCodeThread(remoteThread, &exitCode);

    if (exitCode == 0) {
        printf("[ERROR] DLL LoadLibraryW returned NULL (load failed)\n");
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(remoteThread);
        CloseHandle(processHandle);
        return false;
    }

    printf("[INFO] DLL loaded successfully at 0x%08lX\n", exitCode);

    VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
    CloseHandle(remoteThread);
    CloseHandle(processHandle);

    return true;
}

int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("   Dojo NTR Trainer Injector\n");
    printf("========================================\n\n");
    printf("[INFO] This tool only searches for Dojo NTR.exe.\n");
    printf("[INFO] It loads the explicitly named DojoNTRTrainer.dll into that game process.\n\n");

    wchar_t dllPath[MAX_PATH] = {};
    if (argc >= 2) {
        if (MultiByteToWideChar(CP_ACP, 0, argv[1], -1, dllPath, MAX_PATH) == 0) {
            printf("[ERROR] Failed to read DLL path argument: %lu\n", GetLastError());
            system("pause");
            return 1;
        }
    } else if (!BuildLocalDllPath(dllPath, MAX_PATH)) {
        printf("[ERROR] Failed to build local DojoNTRTrainer.dll path\n");
        system("pause");
        return 1;
    }

    printf("[INFO] Requested DLL path: %ls\n", dllPath);

    DWORD attributes = GetFileAttributesW(dllPath);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("[ERROR] DLL file not found or path points to a directory.\n");
        system("pause");
        return 1;
    }

    wchar_t fullPath[MAX_PATH] = {};
    if (!GetFullPathNameW(dllPath, MAX_PATH, fullPath, nullptr)) {
        printf("[ERROR] Failed to get full DLL path: %lu\n", GetLastError());
        system("pause");
        return 1;
    }

    if (!IsExpectedTrainerDll(fullPath)) {
        printf("[ERROR] Refusing to inject an arbitrary DLL. Expected file name: %ls\n", kTrainerDllName);
        system("pause");
        return 1;
    }

    printf("[INFO] Full DLL path: %ls\n\n", fullPath);

    printf("[INFO] Searching for Dojo NTR process...\n");
    wchar_t matchedProcessName[MAX_PATH] = {};
    wchar_t processImagePath[MAX_PATH] = {};
    DWORD processId = FindDojoNTRProcessId(matchedProcessName, MAX_PATH, processImagePath, MAX_PATH);

    if (processId == 0) {
        printf("[ERROR] Dojo NTR process not found. Please start the game first.\n");
        system("pause");
        return 1;
    }

    printf("[INFO] Found target process: %ls (PID %lu)\n", matchedProcessName, processId);
    if (processImagePath[0] != L'\0') {
        printf("[INFO] Target process path: %ls\n", processImagePath);
    }
    printf("[INFO] No system process names or system process paths are used as a target.\n\n");

    printf("[INFO] Injecting DLL...\n");
    bool injected = false;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        if (attempt > 1) {
            printf("[INFO] Retrying injection (%d/5)...\n", attempt);
            Sleep(1000);
        }

        if (InjectDLL(processId, fullPath)) {
            injected = true;
            break;
        }
    }

    if (injected) {
        printf("\n");
        printf("[SUCCESS] Injection completed.\n");
        printf("[INFO] Press INSERT in the game to open the trainer menu.\n");
        printf("[INFO] Injector will now close automatically.\n");
    } else {
        printf("\n");
        printf("[FAILED] Injection failed.\n");
        system("pause");
        return 1;
    }

    return 0;
}
