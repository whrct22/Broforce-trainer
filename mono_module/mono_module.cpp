#define MONO_MODULE_EXPORTS
#include "mono_module.h"

#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct MonoDomain;
struct MonoAssembly;
struct MonoImage;
struct MonoClass;
struct MonoClassField;
struct MonoMethod;
struct MonoType;
struct MonoTableInfo;
struct MonoMethodSignature;
struct MonoObject;

typedef MonoDomain* (__cdecl* mono_get_root_domain_fn)();
typedef MonoDomain* (__cdecl* mono_domain_get_fn)();
typedef void (__cdecl* mono_thread_attach_fn)(MonoDomain* domain);
typedef MonoAssembly* (__cdecl* mono_domain_assembly_open_fn)(MonoDomain* domain, const char* name);
typedef MonoImage* (__cdecl* mono_assembly_get_image_fn)(MonoAssembly* assembly);
typedef const MonoTableInfo* (__cdecl* mono_image_get_table_info_fn)(MonoImage* image, int table_id);
typedef int (__cdecl* mono_table_info_get_rows_fn)(const MonoTableInfo* table);
typedef void (__cdecl* mono_metadata_decode_row_fn)(const MonoTableInfo* table, int idx, unsigned int* res, int res_size);
typedef const char* (__cdecl* mono_metadata_string_heap_fn)(MonoImage* image, unsigned int index);
typedef MonoClass* (__cdecl* mono_class_get_fn)(MonoImage* image, unsigned int token_index);
typedef const char* (__cdecl* mono_class_get_name_fn)(MonoClass* klass);
typedef const char* (__cdecl* mono_class_get_namespace_fn)(MonoClass* klass);
typedef MonoClass* (__cdecl* mono_class_from_name_fn)(MonoImage* image, const char* name_space, const char* name);
typedef MonoClassField* (__cdecl* mono_class_get_field_from_name_fn)(MonoClass* klass, const char* name);
typedef MonoClassField* (__cdecl* mono_class_get_fields_fn)(MonoClass* klass, void** iter);
typedef const char* (__cdecl* mono_field_get_name_fn)(MonoClassField* field);
typedef MonoType* (__cdecl* mono_field_get_type_fn)(MonoClassField* field);
typedef const char* (__cdecl* mono_type_get_name_fn)(MonoType* type);
typedef int (__cdecl* mono_field_get_flags_fn)(MonoClassField* field);
typedef int (__cdecl* mono_field_get_offset_fn)(MonoClassField* field);
typedef void (__cdecl* mono_field_static_get_value_fn)(void* vtable, MonoClassField* field, void* value);
typedef void* (__cdecl* mono_class_vtable_fn)(MonoDomain* domain, MonoClass* klass);
typedef MonoMethod* (__cdecl* mono_class_get_method_from_name_fn)(MonoClass* klass, const char* name, int param_count);
typedef MonoMethod* (__cdecl* mono_class_get_methods_fn)(MonoClass* klass, void** iter);
typedef const char* (__cdecl* mono_method_get_name_fn)(MonoMethod* method);
typedef MonoMethodSignature* (__cdecl* mono_method_signature_fn)(MonoMethod* method);
typedef unsigned int (__cdecl* mono_signature_get_param_count_fn)(MonoMethodSignature* sig);
typedef void* (__cdecl* mono_compile_method_fn)(MonoMethod* method);
typedef MonoObject* (__cdecl* mono_runtime_invoke_fn)(MonoMethod* method, void* obj, void** params, MonoObject** exc);
typedef void* (__cdecl* mono_object_unbox_fn)(MonoObject* obj);
typedef void* (__cdecl* mono_field_get_value_object_fn)(MonoDomain* domain, MonoClassField* field, void* obj);

struct MonoApi {
    HMODULE handle = nullptr;
    mono_get_root_domain_fn getRootDomain = nullptr;
    mono_domain_get_fn domainGet = nullptr;
    mono_thread_attach_fn threadAttach = nullptr;
    mono_domain_assembly_open_fn domainAssemblyOpen = nullptr;
    mono_assembly_get_image_fn assemblyGetImage = nullptr;
    mono_image_get_table_info_fn imageGetTableInfo = nullptr;
    mono_table_info_get_rows_fn tableInfoGetRows = nullptr;
    mono_metadata_decode_row_fn metadataDecodeRow = nullptr;
    mono_metadata_string_heap_fn metadataStringHeap = nullptr;
    mono_class_get_fn classGet = nullptr;
    mono_class_get_name_fn classGetName = nullptr;
    mono_class_get_namespace_fn classGetNamespace = nullptr;
    mono_class_from_name_fn classFromName = nullptr;
    mono_class_get_field_from_name_fn classGetFieldFromName = nullptr;
    mono_class_get_fields_fn classGetFields = nullptr;
    mono_field_get_name_fn fieldGetName = nullptr;
    mono_field_get_type_fn fieldGetType = nullptr;
    mono_type_get_name_fn typeGetName = nullptr;
    mono_field_get_flags_fn fieldGetFlags = nullptr;
    mono_field_get_offset_fn fieldGetOffset = nullptr;
    mono_field_static_get_value_fn fieldStaticGetValue = nullptr;
    mono_class_vtable_fn classVTable = nullptr;
    mono_class_get_method_from_name_fn classGetMethodFromName = nullptr;
    mono_class_get_methods_fn classGetMethods = nullptr;
    mono_method_get_name_fn methodGetName = nullptr;
    mono_method_signature_fn methodSignature = nullptr;
    mono_signature_get_param_count_fn signatureGetParamCount = nullptr;
    mono_compile_method_fn compileMethod = nullptr;
    mono_runtime_invoke_fn runtimeInvoke = nullptr;
    mono_object_unbox_fn objectUnbox = nullptr;
    mono_field_get_value_object_fn fieldGetValueObject = nullptr;
};

struct State {
    bool initialized = false;
    bool configuredUnity = false;
    bool unityDetected = false;
    bool monoAvailable = false;
    std::string configPath;
    std::string managedDir;
    std::string lastError;
    MonoApi mono;
};

State g_state;

void SetError(const char* message) {
    g_state.lastError = message ? message : "";
}

void SetErrorf(const char* format, const char* detail) {
    char buffer[512] = {};
    snprintf(buffer, sizeof(buffer), format, detail ? detail : "");
    g_state.lastError = buffer;
}

std::string DirectoryOf(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return std::string();
    }
    return path.substr(0, pos + 1);
}

std::string GetModuleDirectory() {
    char modulePath[MAX_PATH] = {};
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetModuleDirectory),
        &module
    );

    DWORD length = GetModuleFileNameA(module, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::string();
    }
    return DirectoryOf(modulePath);
}

std::string GetProcessDirectory() {
    char processPath[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(nullptr, processPath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::string();
    }
    return DirectoryOf(processPath);
}

bool FileExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::string NormalizeConfigPath(const char* configPath) {
    if (configPath && *configPath) {
        return configPath;
    }
    return GetModuleDirectory() + "trainer_config.ini";
}

bool IsConfiguredUnity(const std::string& configPath) {
    return GetPrivateProfileIntA("unity", "enabled", 0, configPath.c_str()) != 0 ||
           GetPrivateProfileIntA("u3d", "enabled", 0, configPath.c_str()) != 0 ||
           GetPrivateProfileIntA("mono", "enabled", 0, configPath.c_str()) != 0;
}

std::string ReadConfiguredManagedDir(const std::string& configPath) {
    char buffer[MAX_PATH] = {};
    GetPrivateProfileStringA("unity", "managed_dir", "", buffer, sizeof(buffer), configPath.c_str());
    if (!buffer[0]) {
        GetPrivateProfileStringA("u3d", "managed_dir", "", buffer, sizeof(buffer), configPath.c_str());
    }
    if (!buffer[0]) {
        GetPrivateProfileStringA("mono", "managed_dir", "", buffer, sizeof(buffer), configPath.c_str());
    }
    return buffer;
}

bool IsUnityDetected(std::string& managedDir) {
    HMODULE unityPlayer = GetModuleHandleA("UnityPlayer.dll");
    HMODULE gameAssembly = GetModuleHandleA("GameAssembly.dll");
    HMODULE mono = GetModuleHandleA("mono.dll");
    HMODULE monoBleeding = GetModuleHandleA("mono-2.0-bdwgc.dll");

    std::string processDir = GetProcessDirectory();
    std::vector<std::string> candidates;
    if (!managedDir.empty()) {
        candidates.push_back(managedDir);
    }
    if (!processDir.empty()) {
        candidates.push_back(processDir + "*_Data\\Managed\\");
        candidates.push_back(processDir + "Managed\\");
    }

    bool hasManagedDir = false;
    if (!processDir.empty()) {
        WIN32_FIND_DATAA data = {};
        HANDLE find = FindFirstFileA((processDir + "*_Data").c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    std::string candidate = processDir + data.cFileName + "\\Managed\\";
                    if (DirectoryExists(candidate)) {
                        managedDir = candidate;
                        hasManagedDir = true;
                        break;
                    }
                }
            } while (FindNextFileA(find, &data));
            FindClose(find);
        }
    }

    if (!hasManagedDir) {
        for (const std::string& candidate : candidates) {
            if (candidate.find('*') != std::string::npos) {
                continue;
            }
            if (DirectoryExists(candidate)) {
                managedDir = candidate;
                hasManagedDir = true;
                break;
            }
        }
    }

    // UnityPlayer/GameAssembly detects Unity generally. A loaded mono runtime or
    // Managed folder means the Mono API path can probably resolve assemblies.
    return unityPlayer || gameAssembly || mono || monoBleeding || hasManagedDir;
}

template <typename T>
bool LoadExport(HMODULE module, const char* name, T& out, bool required = true) {
    out = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!out && required) {
        SetErrorf("missing mono export: %s", name);
        return false;
    }
    return true;
}

HMODULE FindMonoHandle() {
    HMODULE handle = GetModuleHandleA("mono-2.0-bdwgc.dll");
    if (handle) {
        return handle;
    }
    handle = GetModuleHandleA("mono.dll");
    if (handle) {
        return handle;
    }
    handle = GetModuleHandleA("mono-2.0-sgen.dll");
    if (handle) {
        return handle;
    }
    return nullptr;
}

bool LoadMonoApi() {
    MonoApi api;
    api.handle = FindMonoHandle();
    if (!api.handle) {
        SetError("mono runtime module is not loaded");
        return false;
    }

    if (!LoadExport(api.handle, "mono_get_root_domain", api.getRootDomain) ||
        !LoadExport(api.handle, "mono_domain_get", api.domainGet, false) ||
        !LoadExport(api.handle, "mono_thread_attach", api.threadAttach) ||
        !LoadExport(api.handle, "mono_domain_assembly_open", api.domainAssemblyOpen) ||
        !LoadExport(api.handle, "mono_assembly_get_image", api.assemblyGetImage) ||
        !LoadExport(api.handle, "mono_image_get_table_info", api.imageGetTableInfo, false) ||
        !LoadExport(api.handle, "mono_table_info_get_rows", api.tableInfoGetRows, false) ||
        !LoadExport(api.handle, "mono_metadata_decode_row", api.metadataDecodeRow, false) ||
        !LoadExport(api.handle, "mono_metadata_string_heap", api.metadataStringHeap, false) ||
        !LoadExport(api.handle, "mono_class_get", api.classGet, false) ||
        !LoadExport(api.handle, "mono_class_get_name", api.classGetName, false) ||
        !LoadExport(api.handle, "mono_class_get_namespace", api.classGetNamespace, false) ||
        !LoadExport(api.handle, "mono_class_from_name", api.classFromName) ||
        !LoadExport(api.handle, "mono_class_get_field_from_name", api.classGetFieldFromName) ||
        !LoadExport(api.handle, "mono_class_get_fields", api.classGetFields, false) ||
        !LoadExport(api.handle, "mono_field_get_name", api.fieldGetName, false) ||
        !LoadExport(api.handle, "mono_field_get_type", api.fieldGetType, false) ||
        !LoadExport(api.handle, "mono_type_get_name", api.typeGetName, false) ||
        !LoadExport(api.handle, "mono_field_get_flags", api.fieldGetFlags, false) ||
        !LoadExport(api.handle, "mono_field_get_offset", api.fieldGetOffset) ||
        !LoadExport(api.handle, "mono_field_static_get_value", api.fieldStaticGetValue) ||
        !LoadExport(api.handle, "mono_class_vtable", api.classVTable) ||
        !LoadExport(api.handle, "mono_class_get_method_from_name", api.classGetMethodFromName) ||
        !LoadExport(api.handle, "mono_class_get_methods", api.classGetMethods, false) ||
        !LoadExport(api.handle, "mono_method_get_name", api.methodGetName, false) ||
        !LoadExport(api.handle, "mono_method_signature", api.methodSignature, false) ||
        !LoadExport(api.handle, "mono_signature_get_param_count", api.signatureGetParamCount, false) ||
        !LoadExport(api.handle, "mono_compile_method", api.compileMethod) ||
        !LoadExport(api.handle, "mono_runtime_invoke", api.runtimeInvoke) ||
        !LoadExport(api.handle, "mono_object_unbox", api.objectUnbox) ||
        !LoadExport(api.handle, "mono_field_get_value_object", api.fieldGetValueObject, false)) {
        return false;
    }

    MonoDomain* domain = api.getRootDomain();
    if (!domain) {
        SetError("mono root domain is not ready");
        return false;
    }
    api.threadAttach(domain);

    g_state.mono = api;
    SetError("");
    return true;
}

MonoDomain* CurrentDomain() {
    if (!g_state.mono.handle) {
        return nullptr;
    }
    MonoDomain* domain = nullptr;
    if (g_state.mono.domainGet) {
        domain = g_state.mono.domainGet();
    }
    if (!domain && g_state.mono.getRootDomain) {
        domain = g_state.mono.getRootDomain();
    }
    if (domain && g_state.mono.threadAttach) {
        g_state.mono.threadAttach(domain);
    }
    return domain;
}

std::string ResolveAssemblyPath(const char* assemblyPathOrName) {
    if (!assemblyPathOrName || !*assemblyPathOrName) {
        return std::string();
    }

    std::string input = assemblyPathOrName;
    if (FileExists(input)) {
        return input;
    }

    if (!g_state.managedDir.empty()) {
        std::string fromManaged = g_state.managedDir;
        char last = fromManaged.empty() ? '\0' : fromManaged.back();
        if (last != '\\' && last != '/') {
            fromManaged += "\\";
        }
        fromManaged += input;
        if (FileExists(fromManaged)) {
            return fromManaged;
        }
    }

    std::string processDir = GetProcessDirectory();
    if (!processDir.empty()) {
        std::string fromProcess = processDir + input;
        if (FileExists(fromProcess)) {
            return fromProcess;
        }
    }

    // mono_domain_assembly_open can also resolve by display/name in some runtimes.
    return input;
}

MonoAssembly* OpenAssemblyInternal(const char* assemblyPathOrName) {
    if (!g_state.monoAvailable) {
        SetError("mono api is not available");
        return nullptr;
    }

    MonoDomain* domain = CurrentDomain();
    if (!domain) {
        SetError("mono domain is not ready");
        return nullptr;
    }

    std::string path = ResolveAssemblyPath(assemblyPathOrName);
    MonoAssembly* assembly = g_state.mono.domainAssemblyOpen(domain, path.c_str());
    if (!assembly) {
        SetErrorf("failed to open assembly: %s", path.c_str());
    }
    return assembly;
}

MonoClass* FindClassInternal(const char* assemblyPathOrName, const char* nameSpace, const char* className) {
    MonoAssembly* assembly = OpenAssemblyInternal(assemblyPathOrName);
    if (!assembly) {
        return nullptr;
    }

    MonoImage* image = g_state.mono.assemblyGetImage(assembly);
    if (!image) {
        SetError("failed to get mono image");
        return nullptr;
    }

    MonoClass* klass = g_state.mono.classFromName(image, nameSpace ? nameSpace : "", className ? className : "");
    if (!klass) {
        SetErrorf("failed to find class: %s", className ? className : "");
    }
    return klass;
}

} // namespace

MONO_MODULE_API int MonoModule_Initialize(const char* configPath) {
    g_state = State{};
    g_state.configPath = NormalizeConfigPath(configPath);
    g_state.configuredUnity = IsConfiguredUnity(g_state.configPath);
    g_state.managedDir = ReadConfiguredManagedDir(g_state.configPath);
    g_state.unityDetected = IsUnityDetected(g_state.managedDir);
    g_state.monoAvailable = LoadMonoApi();
    g_state.initialized = true;

    if (!g_state.configuredUnity) {
        SetError("unity/u3d/mono mode is not enabled in config; keep AOB path");
    } else if (!g_state.unityDetected) {
        SetError("unity mode is enabled, but no Unity runtime layout was detected");
    } else if (!g_state.monoAvailable) {
        // LoadMonoApi already set the detailed error.
    } else {
        SetError("");
    }

    return MonoModule_ShouldUseMono();
}

MONO_MODULE_API void MonoModule_Shutdown() {
    g_state = State{};
}

MONO_MODULE_API const char* MonoModule_GetLastError() {
    return g_state.lastError.c_str();
}

MONO_MODULE_API const char* MonoModule_GetManagedDirectory() {
    return g_state.managedDir.c_str();
}

MONO_MODULE_API int MonoModule_IsConfiguredUnity() {
    return g_state.configuredUnity ? 1 : 0;
}

MONO_MODULE_API int MonoModule_IsUnityDetected() {
    return g_state.unityDetected ? 1 : 0;
}

MONO_MODULE_API int MonoModule_IsMonoAvailable() {
    return g_state.monoAvailable ? 1 : 0;
}

MONO_MODULE_API int MonoModule_ShouldUseMono() {
    return (g_state.configuredUnity && g_state.unityDetected && g_state.monoAvailable) ? 1 : 0;
}

MONO_MODULE_API uintptr_t MonoModule_GetMonoHandle() {
    return reinterpret_cast<uintptr_t>(g_state.mono.handle);
}

MONO_MODULE_API uintptr_t MonoModule_GetRootDomain() {
    if (!g_state.monoAvailable || !g_state.mono.getRootDomain) {
        return 0;
    }
    return reinterpret_cast<uintptr_t>(g_state.mono.getRootDomain());
}

MONO_MODULE_API uintptr_t MonoModule_GetCurrentDomain() {
    return reinterpret_cast<uintptr_t>(CurrentDomain());
}

MONO_MODULE_API uintptr_t MonoModule_OpenAssembly(const char* assemblyPathOrName) {
    return reinterpret_cast<uintptr_t>(OpenAssemblyInternal(assemblyPathOrName));
}

MONO_MODULE_API uintptr_t MonoModule_FindClass(const char* assemblyPathOrName, const char* nameSpace, const char* className) {
    return reinterpret_cast<uintptr_t>(FindClassInternal(assemblyPathOrName, nameSpace, className));
}

MONO_MODULE_API uintptr_t MonoModule_FindField(uintptr_t klass, const char* fieldName) {
    if (!g_state.monoAvailable || !klass || !fieldName) {
        SetError("invalid FindField arguments");
        return 0;
    }
    MonoClassField* field = g_state.mono.classGetFieldFromName(reinterpret_cast<MonoClass*>(klass), fieldName);
    if (!field) {
        SetErrorf("failed to find field: %s", fieldName);
    }
    return reinterpret_cast<uintptr_t>(field);
}

MONO_MODULE_API uintptr_t MonoModule_FindMethod(uintptr_t klass, const char* methodName, int paramCount) {
    if (!g_state.monoAvailable || !klass || !methodName) {
        SetError("invalid FindMethod arguments");
        return 0;
    }
    MonoMethod* method = g_state.mono.classGetMethodFromName(reinterpret_cast<MonoClass*>(klass), methodName, paramCount);
    if (!method) {
        SetErrorf("failed to find method: %s", methodName);
    }
    return reinterpret_cast<uintptr_t>(method);
}

MONO_MODULE_API uintptr_t MonoModule_CompileMethod(uintptr_t method) {
    if (!g_state.monoAvailable || !method) {
        SetError("invalid CompileMethod arguments");
        return 0;
    }

    void* address = g_state.mono.compileMethod(reinterpret_cast<MonoMethod*>(method));
    if (!address) {
        SetError("mono_compile_method returned null");
    }
    return reinterpret_cast<uintptr_t>(address);
}

MONO_MODULE_API uintptr_t MonoModule_GetMethodAddress(uintptr_t klass, const char* methodName, int paramCount) {
    uintptr_t method = MonoModule_FindMethod(klass, methodName, paramCount);
    if (!method) {
        return 0;
    }
    return MonoModule_CompileMethod(method);
}

MONO_MODULE_API uintptr_t MonoModule_GetMethodAddressByName(const char* assemblyPathOrName, const char* nameSpace, const char* className, const char* methodName, int paramCount) {
    MonoClass* klass = FindClassInternal(assemblyPathOrName, nameSpace, className);
    if (!klass) {
        return 0;
    }
    return MonoModule_GetMethodAddress(reinterpret_cast<uintptr_t>(klass), methodName, paramCount);
}

MONO_MODULE_API int MonoModule_GetFieldOffset(uintptr_t field) {
    if (!g_state.monoAvailable || !field) {
        SetError("invalid field offset arguments");
        return -1;
    }
    return g_state.mono.fieldGetOffset(reinterpret_cast<MonoClassField*>(field));
}

MONO_MODULE_API int MonoModule_GetFieldOffsetByName(const char* assemblyPathOrName, const char* nameSpace, const char* className, const char* fieldName) {
    MonoClass* klass = FindClassInternal(assemblyPathOrName, nameSpace, className);
    if (!klass) {
        return -1;
    }
    uintptr_t field = MonoModule_FindField(reinterpret_cast<uintptr_t>(klass), fieldName);
    if (!field) {
        return -1;
    }
    return MonoModule_GetFieldOffset(field);
}

MONO_MODULE_API uintptr_t MonoModule_InvokeStatic(uintptr_t klass, const char* methodName, int paramCount) {
    if (!g_state.monoAvailable || !klass || !methodName || paramCount != 0) {
        SetError("InvokeStatic currently supports static methods with zero parameters only");
        return 0;
    }

    MonoMethod* method = g_state.mono.classGetMethodFromName(reinterpret_cast<MonoClass*>(klass), methodName, paramCount);
    if (!method) {
        SetErrorf("failed to find method: %s", methodName);
        return 0;
    }

    MonoObject* exc = nullptr;
    MonoObject* result = g_state.mono.runtimeInvoke(method, nullptr, nullptr, &exc);
    if (exc) {
        SetError("mono method threw an exception");
        return 0;
    }
    return reinterpret_cast<uintptr_t>(result);
}

MONO_MODULE_API uintptr_t MonoModule_InvokeStaticByName(const char* assemblyPathOrName, const char* nameSpace, const char* className, const char* methodName, int paramCount) {
    MonoClass* klass = FindClassInternal(assemblyPathOrName, nameSpace, className);
    if (!klass) {
        return 0;
    }
    return MonoModule_InvokeStatic(reinterpret_cast<uintptr_t>(klass), methodName, paramCount);
}

MONO_MODULE_API uintptr_t MonoModule_GetStaticObjectField(uintptr_t klass, const char* fieldName) {
    if (!g_state.monoAvailable || !klass || !fieldName) {
        SetError("invalid static field arguments");
        return 0;
    }

    MonoClassField* field = g_state.mono.classGetFieldFromName(reinterpret_cast<MonoClass*>(klass), fieldName);
    if (!field) {
        SetErrorf("failed to find static field: %s", fieldName);
        return 0;
    }

    MonoDomain* domain = CurrentDomain();
    if (!domain) {
        SetError("mono domain is not ready");
        return 0;
    }

    void* vtable = g_state.mono.classVTable(domain, reinterpret_cast<MonoClass*>(klass));
    if (!vtable) {
        SetError("failed to get mono class vtable");
        return 0;
    }

    uintptr_t value = 0;
    g_state.mono.fieldStaticGetValue(vtable, field, &value);
    return value;
}

MONO_MODULE_API int MonoModule_GetStaticInt32Field(uintptr_t klass, const char* fieldName, int* outValue) {
    if (!outValue) {
        SetError("null output pointer");
        return 0;
    }
    *outValue = 0;

    uintptr_t value = MonoModule_GetStaticObjectField(klass, fieldName);
    if (!value) {
        return 0;
    }
    *outValue = static_cast<int>(value);
    return 1;
}

MONO_MODULE_API int MonoModule_ReadInt32Field(uintptr_t objectPtr, int fieldOffset, int* outValue) {
    if (!objectPtr || fieldOffset < 0 || !outValue) {
        SetError("invalid int field read arguments");
        return 0;
    }
    *outValue = *reinterpret_cast<int*>(objectPtr + static_cast<uintptr_t>(fieldOffset));
    return 1;
}

MONO_MODULE_API uintptr_t MonoModule_ReadPointerField(uintptr_t objectPtr, int fieldOffset) {
    if (!objectPtr || fieldOffset < 0) {
        SetError("invalid pointer field read arguments");
        return 0;
    }
    return *reinterpret_cast<uintptr_t*>(objectPtr + static_cast<uintptr_t>(fieldOffset));
}

MONO_MODULE_API uintptr_t MonoModule_GetObjectField(uintptr_t objectPtr, uintptr_t klass, const char* fieldName) {
    if (!g_state.monoAvailable || !objectPtr || !klass || !fieldName) {
        SetError("invalid object field arguments");
        return 0;
    }

    MonoClassField* field = g_state.mono.classGetFieldFromName(reinterpret_cast<MonoClass*>(klass), fieldName);
    if (!field) {
        SetErrorf("failed to find object field: %s", fieldName);
        return 0;
    }

    if (g_state.mono.fieldGetValueObject) {
        return reinterpret_cast<uintptr_t>(g_state.mono.fieldGetValueObject(CurrentDomain(), field, reinterpret_cast<void*>(objectPtr)));
    }

    int offset = g_state.mono.fieldGetOffset(field);
    if (offset < 0) {
        SetError("failed to resolve object field offset");
        return 0;
    }
    return MonoModule_ReadPointerField(objectPtr, offset);
}

MONO_MODULE_API int MonoModule_UnboxInt32(uintptr_t boxedObject, int* outValue) {
    if (!g_state.monoAvailable || !boxedObject || !outValue) {
        SetError("invalid unbox int arguments");
        return 0;
    }
    void* ptr = g_state.mono.objectUnbox(reinterpret_cast<MonoObject*>(boxedObject));
    if (!ptr) {
        SetError("mono_object_unbox returned null");
        return 0;
    }
    *outValue = *reinterpret_cast<int*>(ptr);
    return 1;
}

MONO_MODULE_API uintptr_t MonoModule_UnboxPointer(uintptr_t boxedObject) {
    if (!g_state.monoAvailable || !boxedObject) {
        SetError("invalid unbox pointer arguments");
        return 0;
    }
    return reinterpret_cast<uintptr_t>(g_state.mono.objectUnbox(reinterpret_cast<MonoObject*>(boxedObject)));
}

void AppendText(char* outBuffer, int outBufferSize, int& used, const char* format, ...) {
    if (!outBuffer || outBufferSize <= 0 || used >= outBufferSize - 1) {
        return;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(outBuffer + used, outBufferSize - used, format, args);
    va_end(args);

    if (written < 0) {
        return;
    }
    if (written >= outBufferSize - used) {
        used = outBufferSize - 1;
        outBuffer[used] = '\0';
        return;
    }
    used += written;
}

void DumpClassInternal(MonoClass* klass, char* outBuffer, int outBufferSize, int& used, int maxMembers, int compileMethods) {
    if (!klass) {
        return;
    }
    if (maxMembers <= 0) {
        maxMembers = 128;
    }

    const char* ns = g_state.mono.classGetNamespace ? g_state.mono.classGetNamespace(klass) : "";
    const char* name = g_state.mono.classGetName ? g_state.mono.classGetName(klass) : "<unknown>";
    AppendText(outBuffer, outBufferSize, used, "\n[类] 0x%p %s%s%s\n", klass, ns && *ns ? ns : "", ns && *ns ? "." : "", name ? name : "<未知>");

    AppendText(outBuffer, outBufferSize, used, "  字段列表:\n");
    if (g_state.mono.classGetFields && g_state.mono.fieldGetName) {
        void* iter = nullptr;
        int count = 0;
        while (count < maxMembers) {
            MonoClassField* field = g_state.mono.classGetFields(klass, &iter);
            if (!field) {
                break;
            }
            const char* fieldName = g_state.mono.fieldGetName(field);
            const char* typeName = "";
            if (g_state.mono.fieldGetType && g_state.mono.typeGetName) {
                MonoType* type = g_state.mono.fieldGetType(field);
                typeName = type ? g_state.mono.typeGetName(type) : "";
            }
            int offset = g_state.mono.fieldGetOffset ? g_state.mono.fieldGetOffset(field) : -1;
            int flags = g_state.mono.fieldGetFlags ? g_state.mono.fieldGetFlags(field) : 0;
            AppendText(outBuffer, outBufferSize, used, "    字段地址=0x%p  偏移=0x%X  标志=0x%X  类型=%s  名称=%s\n", field, offset, flags, typeName ? typeName : "", fieldName ? fieldName : "<未知>");
            ++count;
        }
    } else {
        AppendText(outBuffer, outBufferSize, used, "    <当前 Mono 导出不支持枚举字段>\n");
    }

    AppendText(outBuffer, outBufferSize, used, "  方法列表:\n");
    if (g_state.mono.classGetMethods && g_state.mono.methodGetName) {
        void* iter = nullptr;
        int count = 0;
        while (count < maxMembers) {
            MonoMethod* method = g_state.mono.classGetMethods(klass, &iter);
            if (!method) {
                break;
            }
            const char* methodName = g_state.mono.methodGetName(method);
            unsigned int paramCount = 0;
            if (g_state.mono.methodSignature && g_state.mono.signatureGetParamCount) {
                MonoMethodSignature* sig = g_state.mono.methodSignature(method);
                paramCount = sig ? g_state.mono.signatureGetParamCount(sig) : 0;
            }
            uintptr_t nativeAddress = 0;
            if (compileMethods && g_state.mono.compileMethod) {
                nativeAddress = reinterpret_cast<uintptr_t>(g_state.mono.compileMethod(method));
            }
            AppendText(outBuffer, outBufferSize, used, "    方法地址=0x%p  JIT地址=0x%p  参数=%u  名称=%s\n", method, (void*)nativeAddress, paramCount, methodName ? methodName : "<未知>");
            ++count;
        }
    } else {
        AppendText(outBuffer, outBufferSize, used, "    <当前 Mono 导出不支持枚举方法>\n");
    }
}

MONO_MODULE_API int MonoModule_DumpClass(const char* assemblyPathOrName, const char* nameSpace, const char* className, char* outBuffer, int outBufferSize, int maxMembers, int compileMethods) {
    if (!outBuffer || outBufferSize <= 0) {
        SetError("invalid dump buffer");
        return 0;
    }
    outBuffer[0] = '\0';
    int used = 0;

    MonoClass* klass = FindClassInternal(assemblyPathOrName, nameSpace, className);
    if (!klass) {
        AppendText(outBuffer, outBufferSize, used, "读取类详情失败: %s\n", g_state.lastError.c_str());
        return used;
    }

    DumpClassInternal(klass, outBuffer, outBufferSize, used, maxMembers, compileMethods);
    return used;
}

MONO_MODULE_API int MonoModule_ListClasses(const char* assemblyPathOrName, char* outBuffer, int outBufferSize, int maxClasses) {
    if (!outBuffer || outBufferSize <= 0) {
        SetError("invalid list buffer");
        return 0;
    }
    outBuffer[0] = '\0';
    int used = 0;
    if (maxClasses <= 0) {
        maxClasses = 2048;
    }

    MonoAssembly* assembly = OpenAssemblyInternal(assemblyPathOrName);
    if (!assembly) {
        AppendText(outBuffer, outBufferSize, used, "# 错误: %s\n", g_state.lastError.c_str());
        return used;
    }
    MonoImage* image = g_state.mono.assemblyGetImage(assembly);
    if (!image) {
        AppendText(outBuffer, outBufferSize, used, "# 错误: image 为空\n");
        return used;
    }
    if (!g_state.mono.imageGetTableInfo || !g_state.mono.tableInfoGetRows || !g_state.mono.classGet) {
        AppendText(outBuffer, outBufferSize, used, "# error: mono class enumeration exports unavailable\n");
        return used;
    }

    constexpr int MONO_TABLE_TYPEDEF = 0x02;
    const MonoTableInfo* table = g_state.mono.imageGetTableInfo(image, MONO_TABLE_TYPEDEF);
    int rows = table ? g_state.mono.tableInfoGetRows(table) : 0;

    AppendText(outBuffer, outBufferSize, used, "# Assembly-CSharp class list rows=%d\n", rows);
    int shown = 0;
    for (int i = 1; i <= rows && shown < maxClasses && used < outBufferSize - 128; ++i) {
        MonoClass* klass = g_state.mono.classGet(image, 0x02000000u | (unsigned int)i);
        if (!klass) {
            continue;
        }
        const char* ns = g_state.mono.classGetNamespace ? g_state.mono.classGetNamespace(klass) : "";
        const char* name = g_state.mono.classGetName ? g_state.mono.classGetName(klass) : "<unknown>";
        AppendText(outBuffer, outBufferSize, used, "%s|%s|0x%p\n", ns ? ns : "", name ? name : "<unknown>", klass);
        ++shown;
    }
    if (rows > shown) {
        AppendText(outBuffer, outBufferSize, used, "# truncated: shown=%d rows=%d\n", shown, rows);
    }
    return used;
}

MONO_MODULE_API int MonoModule_DumpAssemblyClasses(const char* assemblyPathOrName, char* outBuffer, int outBufferSize, int maxClasses, int maxMembersPerClass, int compileMethods) {
    if (!outBuffer || outBufferSize <= 0) {
        SetError("invalid dump buffer");
        return 0;
    }
    outBuffer[0] = '\0';
    int used = 0;
    if (maxClasses <= 0) {
        maxClasses = 512;
    }
    if (maxMembersPerClass <= 0) {
        maxMembersPerClass = 64;
    }

    MonoAssembly* assembly = OpenAssemblyInternal(assemblyPathOrName);
    if (!assembly) {
        AppendText(outBuffer, outBufferSize, used, "DumpAssembly failed: %s\n", g_state.lastError.c_str());
        return used;
    }
    MonoImage* image = g_state.mono.assemblyGetImage(assembly);
    if (!image) {
        AppendText(outBuffer, outBufferSize, used, "DumpAssembly failed: image is null\n");
        return used;
    }

    AppendText(outBuffer, outBufferSize, used, "Assembly dump: %s image=0x%p\n", assemblyPathOrName ? assemblyPathOrName : "", image);

    constexpr int MONO_TABLE_TYPEDEF = 0x02;
    const MonoTableInfo* table = g_state.mono.imageGetTableInfo ? g_state.mono.imageGetTableInfo(image, MONO_TABLE_TYPEDEF) : nullptr;
    int rows = (table && g_state.mono.tableInfoGetRows) ? g_state.mono.tableInfoGetRows(table) : 0;
    AppendText(outBuffer, outBufferSize, used, "TypeDef rows: %d, showing up to %d\n", rows, maxClasses);

    if (!g_state.mono.classGet) {
        AppendText(outBuffer, outBufferSize, used, "mono_class_get unavailable; cannot enumerate all classes\n");
        return used;
    }

    int shown = 0;
    for (int i = 1; i <= rows && shown < maxClasses && used < outBufferSize - 256; ++i) {
        MonoClass* klass = g_state.mono.classGet(image, 0x02000000u | (unsigned int)i);
        if (!klass) {
            continue;
        }
        DumpClassInternal(klass, outBuffer, outBufferSize, used, maxMembersPerClass, compileMethods);
        ++shown;
    }

    if (rows > shown) {
        AppendText(outBuffer, outBufferSize, used, "\n... truncated: shown %d of %d classes ...\n", shown, rows);
    }
    return used;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        MonoModule_Shutdown();
    }
    return TRUE;
}
