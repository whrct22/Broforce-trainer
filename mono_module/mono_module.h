#pragma once

#include <cstdint>

#ifdef MONO_MODULE_EXPORTS
#define MONO_MODULE_API extern "C" __declspec(dllexport)
#else
#define MONO_MODULE_API extern "C" __declspec(dllimport)
#endif

// Standalone Unity/Mono helper DLL.
//
// The module intentionally does not enable Unity mode by auto-detection alone.
// Call MonoModule_Initialize with an ini file that contains:
//   [unity]
//   enabled=1
// If the key is missing or set to 0, MonoModule_ShouldUseMono returns 0 so the
// caller can keep its normal AOB path.
//
// Typical use from another injected DLL:
//   if (MonoModule_Initialize("trainer_config.ini") && MonoModule_ShouldUseMono()) {
//       uintptr_t cls = MonoModule_FindClass("Assembly-CSharp.dll", "", "SaveData");
//       uintptr_t obj = MonoModule_InvokeStatic(cls, "get_Instance", 0);
//       int offset = MonoModule_GetFieldOffsetByName("Assembly-CSharp.dll", "", "UserData", "coin");
//       uintptr_t coinObject = MonoModule_ReadPointerField(obj, offset);
//   }

MONO_MODULE_API int MonoModule_Initialize(const char* configPath);
MONO_MODULE_API void MonoModule_Shutdown();

MONO_MODULE_API const char* MonoModule_GetLastError();
MONO_MODULE_API const char* MonoModule_GetManagedDirectory();

MONO_MODULE_API int MonoModule_IsConfiguredUnity();
MONO_MODULE_API int MonoModule_IsUnityDetected();
MONO_MODULE_API int MonoModule_IsMonoAvailable();
MONO_MODULE_API int MonoModule_ShouldUseMono();

MONO_MODULE_API uintptr_t MonoModule_GetMonoHandle();
MONO_MODULE_API uintptr_t MonoModule_GetRootDomain();
MONO_MODULE_API uintptr_t MonoModule_GetCurrentDomain();

MONO_MODULE_API uintptr_t MonoModule_OpenAssembly(const char* assemblyPathOrName);
MONO_MODULE_API uintptr_t MonoModule_FindClass(const char* assemblyPathOrName, const char* nameSpace, const char* className);
MONO_MODULE_API uintptr_t MonoModule_FindField(uintptr_t klass, const char* fieldName);
MONO_MODULE_API uintptr_t MonoModule_FindMethod(uintptr_t klass, const char* methodName, int paramCount);
MONO_MODULE_API uintptr_t MonoModule_CompileMethod(uintptr_t method);
MONO_MODULE_API uintptr_t MonoModule_GetMethodAddress(uintptr_t klass, const char* methodName, int paramCount);
MONO_MODULE_API uintptr_t MonoModule_GetMethodAddressByName(const char* assemblyPathOrName, const char* nameSpace, const char* className, const char* methodName, int paramCount);

MONO_MODULE_API int MonoModule_GetFieldOffset(uintptr_t field);
MONO_MODULE_API int MonoModule_GetFieldOffsetByName(const char* assemblyPathOrName, const char* nameSpace, const char* className, const char* fieldName);

// InvokeStatic helpers currently support zero-parameter static methods.
MONO_MODULE_API uintptr_t MonoModule_InvokeStatic(uintptr_t klass, const char* methodName, int paramCount);
MONO_MODULE_API uintptr_t MonoModule_InvokeStaticByName(const char* assemblyPathOrName, const char* nameSpace, const char* className, const char* methodName, int paramCount);

MONO_MODULE_API uintptr_t MonoModule_GetStaticObjectField(uintptr_t klass, const char* fieldName);
MONO_MODULE_API int MonoModule_GetStaticInt32Field(uintptr_t klass, const char* fieldName, int* outValue);

MONO_MODULE_API int MonoModule_ReadInt32Field(uintptr_t objectPtr, int fieldOffset, int* outValue);
MONO_MODULE_API uintptr_t MonoModule_ReadPointerField(uintptr_t objectPtr, int fieldOffset);
MONO_MODULE_API uintptr_t MonoModule_GetObjectField(uintptr_t objectPtr, uintptr_t klass, const char* fieldName);

MONO_MODULE_API int MonoModule_UnboxInt32(uintptr_t boxedObject, int* outValue);
MONO_MODULE_API uintptr_t MonoModule_UnboxPointer(uintptr_t boxedObject);

// Writes a UTF-8 text snapshot into outBuffer and returns the number of bytes written.
// maxClasses / maxMembers <= 0 means use safe defaults. compileMethods=1 also prints JIT native addresses.
MONO_MODULE_API int MonoModule_ListClasses(const char* assemblyPathOrName, char* outBuffer, int outBufferSize, int maxClasses);
MONO_MODULE_API int MonoModule_DumpAssemblyClasses(const char* assemblyPathOrName, char* outBuffer, int outBufferSize, int maxClasses, int maxMembersPerClass, int compileMethods);
MONO_MODULE_API int MonoModule_DumpClass(const char* assemblyPathOrName, const char* nameSpace, const char* className, char* outBuffer, int outBufferSize, int maxMembers, int compileMethods);
