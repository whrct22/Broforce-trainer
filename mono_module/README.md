# MonoModule

独立的 Unity/Mono 查询模块，用于外部 C/C++ DLL 在注入到 Unity Mono 游戏后解析 Mono 元数据和对象字段地址。

## 编译

```bat
build_mono_module.bat
```

输出：

- `MonoModule.dll`
- `mono_module/mono_module.h`

也可以用 Makefile 的独立目标：

```bat
mingw32-make mono-module
```

## 配置开关

模块不会只因为检测到 Unity 文件/模块就自动启用 Mono 路径。配置缺失时，`MonoModule_ShouldUseMono()` 返回 `0`，调用方应继续走原来的 AOB 注入逻辑。

在传入的 ini 中显式写入任意一个开关即可启用：

```ini
[unity]
enabled=1
managed_dir=可选的_Data\Managed目录
```

兼容别名：

```ini
[u3d]
enabled=1
```

或：

```ini
[mono]
enabled=1
```

## 外部调用示例

```cpp
#include "mono_module/mono_module.h"

if (MonoModule_Initialize("trainer_config.ini") && MonoModule_ShouldUseMono()) {
    uintptr_t klass = MonoModule_FindClass("Assembly-CSharp.dll", "", "UserData");
    int coinOffset = MonoModule_GetFieldOffsetByName("Assembly-CSharp.dll", "", "UserData", "coin");
    uintptr_t coinObject = MonoModule_ReadPointerField(userDataObject, coinOffset);
} else {
    // 配置未启用、不是 Unity/Mono、或 Mono 尚未加载：继续使用 AOB 注入路径。
    const char* reason = MonoModule_GetLastError();
}
```

## 说明

- 该 DLL 只封装 Mono runtime 导出函数，不依赖 MelonLoader 托管部分。
- 支持检测 `UnityPlayer.dll`、`GameAssembly.dll`、`mono.dll`、`mono-2.0-bdwgc.dll` 和 `*_Data/Managed` 目录。
- IL2CPP 游戏通常只有 `GameAssembly.dll`，没有 Mono runtime 导出；这种情况下 `MonoModule_IsUnityDetected()` 可能为真，但 `MonoModule_IsMonoAvailable()` 为假，最终 `MonoModule_ShouldUseMono()` 仍返回 `0`。
- 调用时机应在游戏 Mono runtime 已启动之后；如果过早调用返回未就绪，可稍后重试 `MonoModule_Initialize`。
