# TimePilot（XBLA 试玩版）ReXGlue 静态重编译移植记录

> 记录日期：2026-10-29 ｜ SDK：ReXGlue 0.10.0.24-dev.gbd833a2 ｜ 工具链：VS 2026 自带 Clang 22 / CMake / Ninja
>
> 本文记录**重编译工程本身**（不含资源解包 loader）。目标：理解每一步在做什么、为什么这么做，
> 以便换游戏或从零重做时照着走。

---

## 0. 当前状态

| 项 | 状态 |
|---|---|
| codegen | 通过，0 错误（初始 9718 函数 → 最终 9741） |
| 编译 | 通过（Release，`build/Release/TimePilot.exe`） |
| 运行 | 通过：进游戏、菜单、游玩、成就浮层、手柄导航均正常 |
| 试玩限制 | 已解除（`license_mask`） |
| 在线功能 | Xbox Live Game / Leaderboards 点击后弹提示框，主菜单不受影响 |
| 可移植性 | 资源目录查找已改为相对 exe 的三级回退，整个文件夹拷走即可运行 |

---

## 1. 环境与版本

```text
Visual Studio 2026 (18)  → 自带 clang 22.1.3 / cmake / ninja
  cmake: D:/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe
ReXGlue SDK 0.10.0.24-dev  → D:/ReverseTools/ReXGlueSDK（工具链，不进仓库）
游戏资源                    → D:/ReverseTools/ReXGlueSDK/TimePilot（default.xex + media/ + classic/）
工程                        → D:/Projects/TimePilot（2026-10-30 从 SDK 目录迁出）
素材接入                    → 工程根下 game/ 是指向上述素材目录的 junction（codegen 与运行时都读它）
```

SDK 只提供二进制（无 `src/`），行为判断要靠头文件 + 生成代码 + 日志反推。

---

## 2. 工程结构

```text
TimePilot/
  TimePilot_manifest.toml      codegen 输入：xex 路径 + 函数标注 + 汇编钩子
  CMakeLists.txt               显式列出宿主源码（这一步很关键，见 §4.2）
  CMakePresets.json            SDK 模板带的预设（Ninja + clang）
  src/
    main.cpp                   入口，创建 App
    TimePilot_app.h            App 子类：路径解析、成就浮层、F7 重绑
    hooks.cpp                  midasm_hook 的宿主实现（必须链进 exe）
    achievements_dialog.h      自绘成就浮层（含手柄滚动）
    online_unavailable_dialog.h 在线不可用提示框
    fixes.cpp                  数学函数补丁 + XUsbcam 内核桩
  generated/                   codegen 产物（可随时删掉重生成）
    rexglue.cmake              SDK 模板注入（codegen 挂成 CMake target）
    default/                   TimePilot_recomp.NN.cpp、*_funcs.h、*_init.h …
  build/Release/               exe + toml + dll + logs/
  game -> D:/ReverseTools/ReXGlueSDK/TimePilot   （素材 junction，不入库）
  .gitignore                   构建产物/素材/运行配置全部排除；generated/ 必须入库
```

> 目录迁移后 `TimePilot_manifest.toml` 的 `game_root` / `file_path` 已从 `../timepilot`
> 改为 `game`（相对工程根）。换机器或换素材位置时只要让 `game/` 指到解包目录即可，
> manifest 本身不需要再动。

---

## 3. 七步流程

### ① 起骨架

用 SDK 模板生成工程（或复制一份已有工程，改 `[project].name` 与 `[entrypoint].file_path`）。
模板会生成 `CMakeLists.txt`、`CMakePresets.json`、`TimePilot_manifest.toml`、`generated/rexglue.cmake`。

### ② 写 manifest

```toml
[project]
name = "TimePilot"
game_root = "game"

[entrypoint]
file_path = "game/default.xex"
out_directory_path = "generated/default"
includes = []
```

`game_root` / `file_path` 都是**相对 manifest 所在目录**的路径。工程根下的 `game/`
一般做成指向解包素材的目录联接（junction），这样 manifest 里不出现任何绝对路径，
换机器或换素材位置只要重建这个链接即可。

### ③ 配置 + 生成

codegen 不单独手跑也行——`generated/rexglue.cmake` 已经把它挂成构建依赖：

```cmake
add_custom_command(
    OUTPUT  generated/default/codegen.build.stamp
    COMMAND $<TARGET_FILE:rex::rexglue> codegen ${CMAKE_CURRENT_SOURCE_DIR}/TimePilot_manifest.toml
    DEPFILE generated/default/codegen.d)
add_custom_target(TimePilot_codegen DEPENDS …)
```

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target TimePilot_codegen   # 只跑生成，快
```

### ④ 编译

```bash
cmake --build build --config Release          # 全量，首次 3–5 分钟
```

产物在 `build/Release/`：`TimePilot.exe` + `rexruntime.dll`（运行时）+ `rexgpu-xenos.dll`（GPU 插件）。

### ⑤ 运行前摆好文件

```text
build/Release/
  TimePilot.exe
  TimePilot.toml          ← gpu_plugin / license_mask / 分辨率
  rexruntime.dll          ← SDK 运行时（DLL，不是静态库）
  rexgpu-xenos.dll        ← 来自 GPU_PLUGINS xenos
  <资源目录>               ← default.xex + media/ + classic/
  logs/                   ← 运行时生成
```

### ⑥ 排障循环（本轮的主体工作）

```text
运行 → 崩/黑屏 → 读 build/Release/logs/最新日志 → 定位 → 改 manifest 或配置 → 重编
```

### ⑦ 功能层

GPU 插件 → 试玩解锁 → 成就 → 在线拦截 → 便携路径。见 §5、§6。

---

## 4. 五个必须知道的机制（踩过的坑）

### 4.1 TOML 作用域：`[entrypoint]` 内的表才生效

`manifest.cpp` 的 `LoadBinaryConfig` 只把 `[entrypoint]` 这一张表传给 `RecompilerConfig::LoadFromTable`。
所以：

```toml
[functions]                 # ❌ 顶层：被静默忽略，不报错
[[midasm_hook]]             # ❌ 同上

[entrypoint.functions]      # ✅
[[entrypoint.midasm_hook]]  # ✅
```

这是本次第一个坑：标注全对、codegen 也"成功"，运行时依旧 FATAL。**任何标注写了没效果，先查作用域。**

### 4.2 宿主源码必须显式列进 `TIMEPILOT_SOURCES`

`_recomp` 是独立编译的 object library，它引用的钩子符号需要有强定义被链进 exe。
写在 `src/hooks.cpp` 里、并且 `CMakeLists.txt` 的 `TIMEPILOT_SOURCES` 里列出来，缺一不可。
（inline 函数不行——`_recomp` 目标看不到 exe 内的未引用 inline。）

### 4.3 codegen 会把内联跳转编译成 C++ 直调，绕过函数表

`b 0x82459D64`（跳到 `XamShowAchievementsUI` 内核导入）在生成代码里变成对桩符号的直接调用。
后果：运行时 `FunctionDispatcher::SetFunction` 拦不住（它只管 guest 函数表），
exe 里定义同名符号也覆盖不了（运行时是 `rexruntime.dll`，符号已在 DLL 里）。
**唯一可行手段是生成期注入：`[[entrypoint.midasm_hook]]`。**

### 4.4 `size` 会截断跳转桩

```toml
0x82459488 = { size = 4 }     # ❌ 只标了 4 字节，桩体被切碎
0x82459488 = { name = "FUN_82459488" }   # ✅ 让分析器自动延伸
```

0x82459488 实际是完整的 `lis r11 / b 0x822DD738` 跳转桩。

### 4.5 日志在 exe 同目录，不在 SDK 目录

`build/Release/logs/TimePilot_NNN.log`。
闪退无日志时看 Windows 事件：`0xc0000409` = FATAL abort（即"未注册函数"那类错误）。

---

## 5. 静态分析盲区：四类形态与标注方法

codegen 是从入口开始递归发现函数的，**只发现不到的代码永远不会被编译**，运行时一旦跳进去就 FATAL。
本次共补了 32 条标注，归纳为四类：

| 形态 | 特征 | 标注方法 |
|---|---|---|
| 16 字节跳转桩表 | `lis r11 / li r4 / addi r3 / b target` 每 0x10 一槽 | 每个槽 `0x82xxxxxx = {}`，不写 size |
| 孤岛 | 上下都没有已识别函数覆盖 | 同上，靠种子级联逐步发现 |
| PPC 双入口 | 全局入口 +8 处是 TOC 设置指令 | 把 +8 地址也注册为函数 |
| 虚表/函数指针派发 | 静态完全无引用 | 静态无解，靠运行时 trace 抓返回地址反推 |

本次实际清单（见 manifest 注释）：

```text
0x824594A8–0x82459608  23 个桩槽（这段是 Xam* 内核导入的间接调用跳转表）
0x8227AB80             音频加载失败后的孤岛
0x82280598             PPC 双入口的局部入口
0x822E0BA8 / 0x822E0BB8 字体回退阶段的大孤岛（约 430KB 未发现代码）
0x82447978             渲染/着色器阶段的桩表缺口
0x822A4378             主菜单阶段（手柄接入后）缺口
```

**定位方法**：日志里 `Call to invalid or unregistered function at 0x82…` → 在 `generated/default/*.cpp`
里 `grep -io "<地址小写>"`（注意生成代码里是大写十六进制，直接 `grep 0x82ABC123` 会漏）→ 看它前后
最近的 `DEFINE_REX_FUNC` 判定是"单点"还是"整段桩表"。

---

## 6. 三个 `midasm_hook`

`midasm_hook` 语义：在指定**指令地址**插入一次宿主调用，`registers` 指定要暴露的寄存器，
`return = true` 表示执行完钩子就跳过原指令/原函数。

### 6.1 成就：短路尾跳转（`0x820E619C`）

```toml
[[entrypoint.midasm_hook]]
address = 0x820E619C          # sub_820E6198 里 `b 0x82459D64`
name = "TimePilot_AchievementsUIHook"
registers = ["r3"]
return = true
```

```cpp
void TimePilot_AchievementsUIHook(PPCRegister& r3) {
  r3.u64 = 0;                                  // X_ERROR_SUCCESS
  if (g_timepilot_app) g_timepilot_app->ToggleAchievementsOverlay();
}
```

原理：XamShowAchievementsUI 只有一个打日志的空壳桩，游戏调不到东西。这里直接返回成功并弹自己的浮层。

### 6.2 在线流程 enter：安全网（`0x820CBFDC`）

```toml
[[entrypoint.midasm_hook]]
address = 0x820CBFDC          # sub_820CBFD8 第 2 条指令（mflr r12 之后）
name = "TimePilot_OnlineFeatureHook"
registers = ["r3", "r4", "r12"]
return = true
```

`r12` 在 `mflr r12` 执行后正好是**调用方返回地址**，钩子里把它写日志——
这是定位"静态追不到的间接调用方"的通用手法，一次运行就能拿到地址。

### 6.3 状态机切换：真正生效的拦截（`0x820C6BC8`）

```toml
[[entrypoint.midasm_hook]]
address = 0x820C6BC8
name = "TimePilot_OnlineTransitionHook"
registers = ["r3", "r4"]
return = false                # 不短路，只改参数
```

```cpp
void TimePilot_OnlineTransitionHook(PPCRegister& r3, PPCRegister& r4) {
  constexpr uint32_t kOnlineEnterFn = 0x820CBFD8;
  int32_t target = r4.s32;
  if (target < 0) return;
  uint32_t manager = r3.u32;
  uint32_t count   = REX_LOAD_U32(manager + 8);          // states 计数
  if (uint32_t(target) >= count) return;
  uint32_t state   = REX_LOAD_U32(manager + 12 + target * 4);
  uint32_t vtable  = REX_LOAD_U32(state);
  if (REX_LOAD_U32(vtable + 60) == kOnlineEnterFn) {     // enter 槽
    r4.s64 = -1;                                         // 非法索引 → 空操作
    g_timepilot_app->ShowOnlineUnavailableDialog();
  }
}
```

**为什么必须拦在这一层**：`sub_820C6BC8` 是通用状态机 `Transition(this, target)`，执行顺序是
「对当前状态调 vtable+12（exit，主菜单退场）→ 写 target 索引 → 对目标状态调 vtable+60（enter）」。
只短路 enter 时，主菜单已经退场，关掉提示框后就僵在"无菜单、背景流动"的界面里。
把 target 改成 -1 后，切换函数自身的 `cmpwi r4,0; blt` 参数校验会让整个切换变成彻底的空操作。

---

## 7. 功能层改动

| 需求 | 做法 |
|---|---|
| 黑屏（有画面无渲染） | `gpu_plugin = "xenos"` + CMake `GPU_PLUGINS xenos`（值是插件名，不是路径；运行时找 `<exe>/rexgpu-xenos.dll`） |
| 试玩解锁 | `license_mask = 4294967295`（Xenia 同款 cvar，`XamContentGetLicenseMask` 直接返回；**只能十进制**，`from_chars` 不认 `0x`） |
| 资源目录便携化 | `OnConfigurePaths` 三级回退：`<exe>/game|TimePilot|data|assets` → `<exe>` 平铺 → 扫一层子目录；**仅 Debug 构建**保留老绝对路径，Release 找不到就弹原生提示框并退出（`OnFinalizePaths`） |
| 成就菜单 | `ToggleAchievementsOverlay()` 经 `CallInUIThreadDeferred` 切 UI 线程建浮层 |
| F7 与菜单项打架 | `OnCreateDialogs` 里 `UnregisterBind("bind_achievements")` 后重绑到单实例逻辑 |
| 浮层生命周期 | `ImGuiDialog::Close()` 是 self-deleting（Draw 末尾 `delete this`），所以用**裸指针** + 析构回调清指针，不能用 `unique_ptr` |
| 手柄操作浮层 | `imgui_drawer` 没有手柄路径，自建：每帧 `InputSystem::GetStateForUI(0)` → XInput 边沿映射到 `ImGuiKey_Gamepad*`，配 `NavEnableGamepad` |
| 列表不能滚动 | 内置浮层全是 Text/Dummy，无可聚焦控件 → 重写 `OnDraw`，左摇杆模拟量平滑滚动（死区 8000，满速 900px/s）、十字键 480px/s，`SetScrollY` 应用 |

---

## 8. 改动清单

| 文件 | 改了什么 |
|---|---|
| `TimePilot_manifest.toml` | 32 条函数标注 + 3 个 `midasm_hook`（全在 `[entrypoint]` 内） |
| `CMakeLists.txt` | `TIMEPILOT_SOURCES` 加 `src/hooks.cpp`；`rexglue_setup_target(TimePilot FORCE GPU_PLUGINS xenos)` |
| `build/Release/TimePilot.toml` | `gpu_plugin` / `license_mask` / 分辨率注释示例 |
| `src/TimePilot_app.h` | `OnConfigurePaths` 便携路径；`ToggleAchievementsOverlay`；`OnCreateDialogs` 重绑 F7；浮层裸指针管理；`extern TimepilotApp* g_timepilot_app` |
| `src/hooks.cpp` | 3 个钩子实现 + `g_timepilot_app` 定义（强符号，非 inline） |
| `src/achievements_dialog.h` | 重写 `OnDraw` + 手柄输入轮询 + 平滑滚动 |
| `src/online_unavailable_dialog.h` | 新建：手柄 A/B/Start 关闭、持有 `UIInputBlocker`、self-deleting |
| `src/fixes.cpp` | `roundevenf` + 7 个 XUsbcam 空桩 |

---

## 9. 从零手工复现

```bash
# 1) 工程骨架：以 SDK 模板为起点，放到 <任意目录>/<游戏名>Project
#    确保 generated/rexglue.cmake、CMakePresets.json 存在

# 2) 写 TimePilot_manifest.toml（§3② 的最小内容）

# 3) 配置（必须 ClangCL：SDK 头文件用了 __builtin_bswap*，MSVC 编不过）
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -T ClangCL -DCMAKE_GENERATOR_INSTANCE="D:/Microsoft Visual Studio/18/Community" -DCMAKE_PREFIX_PATH=D:/ReverseTools/ReXGlueSDK
cmake --build build --config Release --target TimePilot_codegen

# 4) 编译
cmake --build build --config Release

# 5) 摆文件（§3⑤），跑一次
build/Release/TimePilot.exe

# 6) 看日志，补标注，回到 3)
tail -50 build/Release/logs/TimePilot_*.log
```

排障原则：**一次只补一类**（单点 / 整段桩表），否则分不清是哪条生效；补完直接跑，不必全量重编。

### 三个环境坑（2026-10-30 目录迁移时踩到）

- **必须 `-T ClangCL`**：用默认的 MSVC 工具集会报 `__builtin_bswap32` 找不到标识符、
  `HostToGuestFunction` 不是 `rex::ppc` 的成员——SDK 头文件依赖 clang 内建函数。
- **必须指定 `-DCMAKE_GENERATOR_INSTANCE`**：机器上同时装了 BuildTools 与 Community，
  CMake 默认挑 BuildTools，而 ClangCL 工具集只装在 Community，会报 `MSB8020 找不到 ClangCL`。
- **代理环境变量的大小写重复会让配置假失败**：shell 里同时有 `HTTP_PROXY` 和 `http_proxy` 时，
  MSBuild 抛 `MSB6001 … 字典中的关键字"HTTPS_PROXY"`，表面症状却是 "No CMAKE_CXX_COMPILER
  could be found"。先 `unset` 掉小写副本再 configure。

---

## 10. 排障速查

| 现象 | 线索 | 处理 |
|---|---|---|
| 启动即 FATAL | `Call to invalid or unregistered function at 0x82…` | 补 `[entrypoint.functions]` 标注 |
| 标注没效果 | codegen 无变化 | 检查是否写在 `[entrypoint]` 顶层 |
| 能进游戏但黑屏 | 日志 `no gpu` / Vd* 被忽略 | 配 `gpu_plugin` + `GPU_PLUGINS` |
| 弹"找不到游戏数据" | 资源不在 exe 旁的三级布局内 | 放到 `<exe>/game/`，或 `--game_data_root=<目录>` |
| 闪退无日志 | 事件 `0xc0000409` | 日志其实在 exe 同目录 `logs/` |
| 点菜单项无响应 | 对应内核导入是空桩 | `midasm_hook` 短路 + 自实现 UI |
| 提示框关了仍僵死 | 状态机已切换 | 拦截要上移到 `Transition` 入口 |
| 链接报 undefined hook | `_recomp` 找不到符号 | 钩子写进 `hooks.cpp` 并加入 `TIMEPILOT_SOURCES` |

---

## 11. 已知限制 / 可继续做的事

- 分辨率只有 `TimePilot.toml` 手改（或 F4 设置面板），窗口尺寸需重启生效。
- `resolution` / `resolution_scale` 的关系：预设优先，自定义尺寸要删掉 `resolution`。
- 存档与成就不在游戏目录，在 `文档\TimePilot\58410815`（按用户走，换机器需单独迁移）。
- 在线功能只是"体面地拦住"，没有实现真实的 Xbox Live 替代（UnleashedRecomp 那类项目接的是自建服务）。
- 若以后 Leaderboards 出现独立的第二入口，把 `--log_verbose` 打开再跑一次，按 §6.2 的 trace 手法定位。
