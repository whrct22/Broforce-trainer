#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <cstring>

// 获取进程ID
DWORD FindProcessId(const wchar_t* processName) {
    DWORD processId = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W processEntry;
        processEntry.dwSize = sizeof(processEntry);

        if (Process32FirstW(snapshot, &processEntry)) {
            do {
                if (_wcsicmp(processEntry.szExeFile, processName) == 0) {
                    processId = processEntry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &processEntry));
        }
        CloseHandle(snapshot);
    }

    return processId;
}

bool InjectDLL(DWORD processId, const wchar_t* dllPath) {
    printf("[INFO] Opening process %lu...\n", processId);

    HANDLE processHandle = OpenProcess(
        PROCESS_ALL_ACCESS,
        FALSE,
        processId
    );

    if (processHandle == NULL) {
        printf("[ERROR] OpenProcess failed: %lu\n", GetLastError());
        return false;
    }

    // 在目标进程中分配内存
    size_t dllPathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    printf("[INFO] Allocating %zu bytes in target process...\n", dllPathSize);

    LPVOID remoteMemory = VirtualAllocEx(
        processHandle,
        NULL,
        dllPathSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (remoteMemory == NULL) {
        printf("[ERROR] VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(processHandle);
        return false;
    }

    // 写入DLL路径
    printf("[INFO] Writing DLL path to target process...\n");
    if (!WriteProcessMemory(processHandle, remoteMemory, dllPath, dllPathSize, NULL)) {
        printf("[ERROR] WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(processHandle);
        return false;
    }

    // 获取LoadLibraryW地址
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLibrary = GetProcAddress(kernel32, "LoadLibraryW");

    if (loadLibrary == NULL) {
        printf("[ERROR] GetProcAddress failed\n");
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(processHandle);
        return false;
    }

    // 创建远程线程
    printf("[INFO] Creating remote thread...\n");
    HANDLE remoteThread = CreateRemoteThread(
        processHandle,
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)loadLibrary,
        remoteMemory,
        0,
        NULL
    );

    if (remoteThread == NULL) {
        printf("[ERROR] CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(processHandle);
        return false;
    }

    // 等待线程执行完成
    printf("[INFO] Waiting for DLL to load...\n");
    DWORD waitResult = WaitForSingleObject(remoteThread, 10000);  // 10秒超时

    if (waitResult == WAIT_TIMEOUT) {
        printf("[ERROR] Remote thread timed out\n");
        TerminateThread(remoteThread, 0);
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(remoteThread);
        CloseHandle(processHandle);
        return false;
    }

    // 获取返回值
    DWORD exitCode;
    GetExitCodeThread(remoteThread, &exitCode);

    if (exitCode == 0) {
        printf("[ERROR] DLL LoadLibrary returned NULL (load failed)\n");
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(remoteThread);
        CloseHandle(processHandle);
        return false;
    }

    printf("[INFO] DLL loaded successfully at 0x%08X\n", exitCode);

    // 清理
    VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
    CloseHandle(remoteThread);
    CloseHandle(processHandle);

    return true;
}

int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("   Broforce Trainer Injector\n");
    printf("========================================\n\n");

    // 检查参数
    if (argc < 2) {
        printf("[ERROR] No DLL path provided!\n");
        printf("Usage: BroforceInjector.exe <DLL path>\n");
        printf("Example: BroforceInjector.exe BroforceTrainer.dll\n");
        system("pause");
        return 1;
    }

    // 转换DLL路径为宽字符
    wchar_t dllPath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, dllPath, MAX_PATH);

    printf("[INFO] DLL path: %ls\n", dllPath);

    // 检查DLL文件是否存在
    DWORD attributes = GetFileAttributesW(dllPath);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        printf("[ERROR] DLL file not found!\n");
        system("pause");
        return 1;
    }

    // 获取完整路径
    wchar_t fullPath[MAX_PATH];
    if (!GetFullPathNameW(dllPath, MAX_PATH, fullPath, NULL)) {
        printf("[ERROR] Failed to get full path\n");
        system("pause");
        return 1;
    }

    printf("[INFO] Full path: %ls\n\n", fullPath);

    // 查找Broforce进程
    printf("[INFO] Searching for Broforce process...\n");
    DWORD processId = FindProcessId(L"Broforce.exe");

    if (processId == 0) {
        processId = FindProcessId(L"Broforce.bin.x86_64.exe");
    }

    if (processId == 0) {
        printf("[ERROR] Broforce process not found!\n");
        printf("Please make sure the game is running.\n");
        system("pause");
        return 1;
    }

    printf("[INFO] Found process ID: %lu\n\n", processId);

    // 注入DLL；刚点“脱出 DLL”后 Windows 可能还在卸载模块，短时间内 LoadLibrary 会返回 NULL，自动重试几次。
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
        printf("[SUCCESS] Injection completed!\n");
        printf("[INFO] Press INSERT key to open cheat menu\n");
        printf("[INFO] Injector will now close automatically.\n");
    } else {
        printf("\n");
        printf("[FAILED] Injection failed!\n");
        system("pause");
        return 1;
    }

    return 0;
}
