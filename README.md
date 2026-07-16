# C++ Trainer

![截图]([./path/to/your-image.png](https://filestation.mlyl.io/preview?key=%E5%85%B6%E4%BB%96%E9%A1%B9%E7%9B%AE%2F%E6%89%B9%E6%B3%A8+2026-07-17+010031.png))

> 一个用于学习 Windows DLL 注入、DirectX 11 Hook、ImGui 覆盖层和游戏内存读写的 Broforce 修改器示例项目。请仅在本机、学习和自用场景下使用。

## 项目功能

本项目会生成两个主要程序：

- `BroforceTrainer.dll`：注入到 Broforce 游戏进程中的修改器 DLL。
- `BroforceInjector.exe`：负责查找 Broforce 进程，并把 `BroforceTrainer.dll` 注入进去的注入器。

注入成功后，在游戏中按 `INSERT` 可以打开或关闭 ImGui 菜单。菜单中可以查看调试状态、玩家基址、当前血量、移动速度、坐标等信息，并可以锁定或修改部分玩家数值。

目前包含的主要功能：

- 锁定血量
- 锁定最大血量
- 锁定移动速度
- 锁定最大坠落速度
- 锁定跳跃高度
- 锁定射速
- 锁定技能 / 特殊弹药
- 锁定生命条数
- 锁定酸雨覆盖状态
- 显示玩家坐标
- 显示玩家当前站立物体信息
- 调整修改器窗口透明度
- 从菜单中恢复修改并卸载 DLL，释放 DLL 文件占用，方便重新编译

配置会自动保存到：

```txt
D:\c++-trainer\trainer_config.ini
```

日志会写入：

```txt
D:\c++-trainer\trainer_log.txt
```

## 使用方法

### 1. 准备环境

项目面向 Windows 环境，使用 C++17 编写。

需要准备：

- Windows
- Broforce 游戏进程
- MinGW / TDM-GCC 或支持 CMake 的 C++ 编译环境
- DirectX 11 相关系统库
- ImGui 源码，已放在 `vendor/imgui/`
- MinHook 预编译库目录 `MinHook_134_lib/`

> 注意：当前仓库的构建文件引用了 `MinHook_134_lib`，如果本地没有该目录，需要自行放入对应的 MinHook 头文件和库文件，或者调整构建脚本。

### 2. 编译

#### 方式一：使用 `build.bat`

直接运行：

```bat
build.bat
```

脚本会增量编译 ImGui、修改器 DLL 和注入器，成功后会生成：

```txt
BroforceTrainer.dll
BroforceInjector.exe
```

`build.bat` 中默认把 `D:\TDM-GCC\bin` 加入 `PATH`，如果你的编译器不在这个位置，需要修改脚本中的路径。

#### 方式二：使用 Makefile

如果已经配置好 MinGW：

```bash
mingw32-make
```

或：

```bash
make
```

清理构建产物：

```bash
mingw32-make clean
```

#### 方式三：使用 CMake

项目根目录包含 `CMakeLists.txt`，注入器也有单独的 `injector/CMakeLists.txt`。可按常规 CMake 流程构建：

```bash
cmake -S . -B build
cmake --build build
```

生成产物通常位于 `build/bin/` 下。

### 3. 运行游戏

先启动 Broforce，并进入游戏。

注入器会尝试查找以下进程名：

```txt
Broforce.exe
Broforce.bin.x86_64.exe
```

### 4. 注入 DLL

在生成文件所在目录运行：

```bat
BroforceInjector.exe BroforceTrainer.dll
```

注入器会：

1. 查找 Broforce 进程 ID。
2. 打开目标进程。
3. 在目标进程中分配内存。
4. 写入 DLL 路径。
5. 通过远程线程调用 `LoadLibraryW` 加载 `BroforceTrainer.dll`。
6. 注入成功后自动退出。

成功后会看到提示：

```txt
[SUCCESS] Injection completed!
[INFO] Press INSERT key to open cheat menu
```

### 5. 打开菜单

回到游戏窗口，按：

```txt
INSERT
```

即可打开或关闭修改器菜单。

如果菜单显示“正在等待玩家基址”，需要进入关卡，并让角色使用一次技能 / 特殊弹药，等待 Mono JIT 代码出现并被特征码扫描捕获。

### 6. 卸载 DLL

菜单中提供按钮：

```txt
恢复修改并从游戏中脱出 DLL
```

点击后会尝试：

- 恢复锁定项的原始数值
- 恢复 AOB Hook
- 恢复 D3D11 Present / ResizeBuffers Hook
- 恢复窗口过程 WndProc
- 关闭 ImGui
- 从游戏进程中卸载 DLL

这样可以释放 `BroforceTrainer.dll` 的文件占用，便于重新编译后再次注入。

## 运作原理

整体流程如下：

```txt
BroforceInjector.exe
        │
        │ 查找 Broforce 进程
        │ OpenProcess / VirtualAllocEx / WriteProcessMemory
        │ CreateRemoteThread + LoadLibraryW
        ▼
BroforceTrainer.dll 被加载进游戏进程
        │
        │ DllMain 创建主线程
        ▼
等待 d3d11.dll 加载
        │
        │ 创建临时 DX11 SwapChain 获取 vtable
        ▼
Hook IDXGISwapChain::Present 和 ResizeBuffers
        │
        │ 在 Present 中初始化 ImGui
        ▼
游戏画面上绘制修改器菜单
        │
        │ 后台扫描 Mono/JIT 可执行内存
        ▼
通过 AOB 特征码定位玩家相关代码
        │
        │ 安装跳转 Hook，捕获玩家基址
        ▼
根据配置持续写入 / 锁定玩家数据
```

### DLL 注入

`injector/injector.cpp` 负责 DLL 注入。

核心步骤是：

- 使用 `CreateToolhelp32Snapshot` 枚举进程。
- 找到 Broforce 的进程 ID。
- 使用 `OpenProcess` 打开目标进程。
- 使用 `VirtualAllocEx` 在目标进程分配内存。
- 使用 `WriteProcessMemory` 写入 DLL 完整路径。
- 使用 `CreateRemoteThread` 调用 `LoadLibraryW`。

这是一种常见的 Windows DLL 注入方式。

### DirectX 11 Hook

`src/hook.cpp` 中的 `D3D11Hook` 会创建一个临时 DX11 设备和 SwapChain，用来获取 `IDXGISwapChain` 的虚函数表。

随后在 `src/dllmain.cpp` 中 Hook：

- vtable index `8`：`Present`
- vtable index `13`：`ResizeBuffers`

`Present` 每帧都会被游戏调用，因此修改器会在 `HookedPresent` 中：

- 首次执行时初始化 ImGui
- 获取游戏窗口句柄
- 创建 RenderTargetView
- 接管窗口过程 WndProc
- 每帧渲染菜单
- 每帧根据配置写入锁定值

`ResizeBuffers` 用于处理窗口尺寸变化时的 RenderTargetView 重建。

### ImGui 菜单

`src/gui.cpp` 和 `src/gui.h` 负责图形界面。

主要职责：

- 初始化 ImGui Win32 + DX11 后端
- 加载中文字体
- 绘制菜单窗口
- 显示调试状态
- 显示玩家信息
- 提供数值锁定输入框
- 保存配置
- 处理 `INSERT` 热键显示 / 隐藏菜单

窗口过程通过 `SetWindowLongPtrW` 接管，优先交给 ImGui 处理输入，然后再转发给游戏原始窗口过程。

### 玩家基址捕获

Broforce 使用 Mono / JIT，某些游戏逻辑代码不会一开始就存在于主模块中。因此项目没有只扫描 `Broforce.exe` 主模块，而是在后台持续扫描整个进程的可执行内存。

`src/dllmain.cpp` 中的 `AOBScanThread` 会扫描特征码：

```txt
89 87 DC 05 00 00
```

找到后会安装一段跳转 Hook，把目标代码跳转到自定义的 `newmem` 区域。

自定义代码会：

1. 判断当前对象是否是玩家。
2. 如果是玩家，则保存玩家基址到 `g_capturedPlayerBase`。
3. 执行被覆盖的原始指令。
4. 跳回原游戏代码继续执行。

捕获到玩家基址后，GUI 和修改器逻辑就可以通过固定偏移读写玩家数据。

### 内存读写

`src/memory.h` 和 `src/memory.cpp` 封装了常用内存操作：

- 读取当前进程内存
- 写入当前进程内存
- 读取指针链
- 获取模块基址
- 获取模块大小
- NOP 填充
- 写入跳转
- AOB 特征码扫描

因为 DLL 已经注入到游戏进程内部，所以读写时使用的是 `GetCurrentProcess()`。

### 配置保存

`src/config.cpp` 和 `src/config.h` 负责配置读写。

配置文件使用 ini 格式，例如：

```ini
[health]
enabled=1
value=9999.000000

[gui]
alpha=1.000000
```

当用户在菜单中修改锁定项或窗口透明度时，配置会被标记为 dirty，并自动保存。

下次 DLL 加载时会读取同一个配置文件，并恢复上次的设置。

## 架构分布

```txt
.
├── CMakeLists.txt              # 主 CMake 构建文件，构建 DLL 并引入 injector 子项目
├── Makefile                    # MinGW Makefile 构建脚本
├── build.bat                   # Windows 批处理增量构建脚本
├── trainer_config.ini          # 修改器配置文件
├── Broforce.CT                 # Cheat Engine 表，作为偏移和注入点参考
├── injector/
│   ├── CMakeLists.txt          # 注入器 CMake 构建文件
│   └── injector.cpp            # DLL 注入器入口
├── src/
│   ├── dllmain.cpp             # DLL 入口、DX11 Hook、AOB 扫描、玩家基址捕获、卸载清理
│   ├── gui.h / gui.cpp         # ImGui 菜单、窗口输入处理、界面渲染
│   ├── cheat.h / cheat.cpp     # 修改器功能封装和玩家偏移定义
│   ├── memory.h / memory.cpp   # 内存读写、指针链、模块信息、AOB 扫描工具
│   ├── hook.h / hook.cpp       # 通用 Hook 和 D3D11 vtable Hook 工具
│   └── config.h / config.cpp   # ini 配置读取、保存和自动持久化
└── vendor/
    └── imgui/                  # Dear ImGui 源码及 Win32 / DX11 后端
```

### 模块职责简表

| 模块 | 职责 |
| --- | --- |
| `injector` | 查找游戏进程并注入 DLL |
| `dllmain` | DLL 生命周期、Hook 安装、AOB 扫描、卸载清理 |
| `hook` | 创建 DX11 SwapChain，修改 vtable，恢复 Hook |
| `gui` | 绘制 ImGui 菜单，处理热键和用户输入 |
| `cheat` | 定义玩家偏移，提供修改器功能接口 |
| `memory` | 封装读写内存、扫描特征码等底层操作 |
| `config` | 读取和保存 ini 配置 |
| `vendor/imgui` | 图形界面库 |

## 常见问题

### 按 INSERT 没有菜单

可以检查：

- DLL 是否注入成功
- 游戏是否使用 DirectX 11
- `trainer_log.txt` 中是否出现 D3D11 Hook 成功日志
- 游戏窗口是否处于前台

### 菜单显示“正在等待玩家基址”

这通常表示 AOB Hook 还没有被触发。可以尝试：

- 进入实际关卡
- 切换角色
- 使用一次技能 / 特殊弹药
- 等待后台扫描日志更新

### 重新编译时 DLL 被占用

如果 DLL 已经注入到游戏进程，Windows 会占用该文件。可以在菜单中点击：

```txt
恢复修改并从游戏中脱出 DLL
```

卸载后再重新编译。

### 配置文件路径不对

当前配置和日志路径在代码中写死为：

```txt
D:\c++-trainer\trainer_config.ini
D:\c++-trainer\trainer_log.txt
```

如果项目移动到了其他路径，需要修改 `src/config.h` 和 `src/dllmain.cpp` 中对应路径。
