# TimePilot (XBLA) — 静态重编译

用 ReXGlue SDK 把 Xbox 360 上的 **TimePilot**（XBLA）从
PowerPC 机器码静态重编译成 x64 原生程序，让原版代码直接在现代 Windows 上运行。

> 本仓库只包含**重编译产物与移植补丁**，不含任何游戏本体数据。
> 运行需要你自己合法持有的游戏文件（`default.xex` + `media/`），这里不提供下载。

## 目录结构

| 路径 | 说明 |
|---|---|
| `src/` | 手写代码：入口、GPU 钩子、移植补丁（`hooks.cpp` / `fixes.cpp` / `main.cpp`） |
| `generated/` | 重编译产出的 C++（约 150 个文件）。**不入库**——这是从游戏机器码反编译出的游戏专有代码（版权），且需你自备原始 `default.xex` 才能用 SDK 重新生成；克隆后执行 `cmake --build build --config Release --target TimePilot_codegen` 在本地生成 |
| `TimePilot_manifest.toml` | codegen 配置：入口点、函数表、素材根目录 |
| `PORTING_NOTES.md` | 移植全过程记录：函数补桩、图形与输入层的实测结论、构建环境坑 |
| `game/` | 指向解包素材的目录联接（不入库，见 `.gitignore`） |

## 构建

需要 ReXGlue SDK 0.10.0.24-dev 与 Visual Studio 2026。

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -T ClangCL ^
      -DCMAKE_GENERATOR_INSTANCE="<VS Community 安装路径>" ^
      -DCMAKE_PREFIX_PATH=<ReXGlueSDK 路径>
cmake --build build --config Release
```

- **必须 `-T ClangCL`**：SDK 头文件用了 `__builtin_bswap*`，MSVC 编不过。
- **必须指定 `CMAKE_GENERATOR_INSTANCE`**：机器上同时装了 BuildTools 与 Community 时，
  CMake 默认挑到没装 ClangCL 的那个，会报 `MSB8020`。

素材接法：工程根建一个目录联接指向解包目录，manifest 里只写相对路径 `game`，

```bat
mklink /J game <解包出来的素材目录>
```

换机器只要重建这个链接，manifest 不用改。

## 运行

`build/Release/` 下需要：`TimePilot.exe`、`rexruntime.dll`、`rexgpu-xenos.dll`、
`game/`（素材联接）、`TimePilot.toml`（首次运行自动生成）。
其中 `rexruntime.dll` / `rexgpu-xenos.dll` 是 ReXGlue SDK 运行时，**未随本仓库分发**，需自行从 SDK 取得（再分发许可请向 ReXGlueX 确认）。

画质与键位设置用配套启动器 [TimePilot.Loader](https://github.com/NeroJin/TimePilot.Loader)：
它按选项写 `TimePilot.toml` 再拉起游戏，也可以解包 STFS 包、录制键盘映射。

## 已实测的运行时行为

- `vsync` 必须开启——游戏速度依赖它，关掉会快得离谱。
- `present_effect` 只有四个值：`none` / `bilinear` / `fxaa` / `fxaa_extreme`，单选。
- 无 CAS / FSR / 整数缩放；放大固定双线性，锐利度靠 `resolution_scale` 超采样。
- `mnk_mode = true` 时键盘被模拟成 1P 手柄，与实体手柄并行（不是抢占）。

## 说明

仅供学习与研究。游戏资源版权归原权利人所有，请使用自己合法持有的游戏文件。
