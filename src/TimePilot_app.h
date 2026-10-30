// TimePilot - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <rex/logging/macros.h>
#include <rex/rex_app.h>
#include <rex/input/input_system.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/ui/keybinds.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "achievements_dialog.h"
#include "online_unavailable_dialog.h"

// default.xex 中 XamShowAchievementsUI 内核导入桩的 guest 地址。
// （来源：generated/default/TimePilot_register.cpp 中的
//   registrar->SetFunction(0x82459D64, __imp__XamShowAchievementsUI) ）
constexpr uint32_t kXamShowAchievementsUIAddress = 0x82459D64;

class TimepilotApp;

// App 单实例指针。定义在 src/hooks.cpp（生成的 _recomp 目标引用的
// midasm 钩子也定义在那里，二者需要共享同一个实例）。
extern TimepilotApp* g_timepilot_app;

// [[midasm_hook]] 宿主钩子（见 TimePilot_manifest.toml 末尾），定义在 src/hooks.cpp。
// 注入点：sub_820E6198 中 `b 0x82459D64` 指令处，return = true 短路原调用。
void TimePilot_AchievementsUIHook(PPCRegister& r3);

// 在线功能拦截钩子（定义在 src/hooks.cpp）：
// 1) 上游状态机切换钩子：拦下进入在线状态的切换，主菜单不退场；
// 2) 安全网：直接短路在线状态 enter 本体。
void TimePilot_OnlineTransitionHook(PPCRegister& r3, PPCRegister& r4);
void TimePilot_OnlineFeatureHook(PPCRegister& r3, PPCRegister& r4,
                                 PPCRegister& r12);

// 当前 exe 所在目录（用于把资源路径做成"随 exe 走"的可移植布局）。
inline std::filesystem::path ExeDirectory() {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH * 4];
  const DWORD length = ::GetModuleFileNameW(
      nullptr, buffer, DWORD(sizeof(buffer) / sizeof(buffer[0])));
  if (length > 0 && length < sizeof(buffer) / sizeof(buffer[0])) {
    return std::filesystem::path(std::wstring(buffer, length)).parent_path();
  }
#endif
  std::error_code ec;
  const auto cwd = std::filesystem::current_path(ec);
  return ec ? std::filesystem::path{} : cwd;
}

// 宽字符 → UTF-8（日志统一按 UTF-8 输出；路径里带中文用户名也能正确打印）。
inline std::string WideToUtf8(const std::wstring& wide) {
#ifdef _WIN32
  if (wide.empty()) {
    return {};
  }
  const int length = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                           int(wide.size()), nullptr, 0, nullptr,
                                           nullptr);
  if (length <= 0) {
    return {};
  }
  std::string utf8(size_t(length), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), int(wide.size()), utf8.data(),
                        length, nullptr, nullptr);
  return utf8;
#else
  return std::string(wide.begin(), wide.end());
#endif
}

// path → UTF-8 字符串。
inline std::string PathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
  return WideToUtf8(path.wstring());
#else
  return path.string();
#endif
}

class TimepilotApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    auto app = std::unique_ptr<TimepilotApp>(new TimepilotApp(ctx, "TimePilot",
        PPCImageConfig));
    g_timepilot_app = app.get();
    return app;
  }

  // 游戏内容根目录：按"可移植"顺序解析，不写死绝对路径。
  //   1) <exe目录>/game|TimePilot|data|assets/default.xex  → 约定目录名
  //   2) <exe目录>/default.xex                            → 资源平铺在 exe 旁
  //   3) <exe目录>/<任意子目录>/default.xex               → 解压出来的文件夹随便叫什么
  //   4) Debug 构建才有：老开发机布局（D:/ReverseTools/...），方便开发时不用拷资源
  // 命令行/环境变量 --game_data_root 优先级最高（此时本字段已非空，直接返回）。
  // Release 构建找不到数据目录时不再静默兜底，改为提示 + 退出（见 OnFinalizePaths）。
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) {
      resolved_note_ = "game_data_root provided externally: " +
                       PathToUtf8(paths.game_data_root);
      return;
    }
    const std::filesystem::path exe_dir = ExeDirectory();
    if (!exe_dir.empty()) {
      std::error_code ec;
      for (const wchar_t* name : {L"game", L"TimePilot", L"data", L"assets"}) {
        const std::filesystem::path candidate = exe_dir / name;
        if (std::filesystem::exists(candidate / "default.xex", ec)) {
          paths.game_data_root = candidate;
          resolved_note_ = "game data found at " + PathToUtf8(candidate);
          return;
        }
      }
      if (std::filesystem::exists(exe_dir / "default.xex", ec)) {
        paths.game_data_root = exe_dir;
        resolved_note_ = "game data found at " + PathToUtf8(exe_dir);
        return;
      }
      // 兜底：扫一层子目录，含 default.xex 的即视为游戏数据根目录。
      for (std::filesystem::directory_iterator it(exe_dir, ec), end; !ec && it != end;
           it.increment(ec)) {
        std::error_code file_ec;
        if (!it->is_directory(file_ec)) {
          continue;
        }
        if (std::filesystem::exists(it->path() / "default.xex", file_ec)) {
          paths.game_data_root = it->path();
          resolved_note_ = "game data found at " + PathToUtf8(it->path());
          return;
        }
      }
    }
#if defined(_DEBUG)
    // 仅调试构建保留开发机绝对路径，免得每次编译完都要拷 26 MB 资源。
    {
      std::error_code ec;
      const std::filesystem::path dev_path(L"D:/ReverseTools/ReXGlueSDK/TimePilot");
      if (std::filesystem::exists(dev_path / "default.xex", ec)) {
        paths.game_data_root = dev_path;
        resolved_note_ = "(debug build) falling back to dev path " +
                         PathToUtf8(dev_path);
        return;
      }
    }
#endif
    // 没找到：不猜、不兜底，把可读的说明留给 OnFinalizePaths 弹给用户。
    // （此处日志系统还没初始化——SetupEnvironment 里 paths 早于 logging——
    //  所以文字先存着，真正落盘在 OnFinalizePaths。）
    data_root_error_ = BuildMissingDataMessage(exe_dir);
  }

  // 路径解析失败时的收尾：弹出明确提示（说明该把 default.xex 放到哪里）。
  // 用 Win32 原生对话框而非 ImGui 浮层——返回 nullopt 后初始化会立即中止、
  // 应用马上退出，ImGui 浮层来不及绘制；原生框是阻塞的，用户点掉才继续。
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
    (void)resume;
    if (data_root_error_.empty()) {
      if (!resolved_note_.empty()) {
        REXLOG_INFO("TimePilot: {}", resolved_note_);
      }
      return defaults;
    }
    // 日志系统在路径解析之后才初始化，所以这里才是错误文本第一次真正落盘的位置。
    REXLOG_ERROR("TimePilot: {}", WideToUtf8(data_root_error_));
#ifdef _WIN32
    ::MessageBoxW(nullptr, data_root_error_.c_str(), L"TimePilot",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TASKMODAL);
#endif
    return std::nullopt;  // 不 resume → 初始化中止，进程随即退出
  }

  // 供 PPC 钩子调用：切到 UI 线程弹"在线功能不可用"提示框。
  // 对话框走 ImGuiDialog 的 self-deleting 惯例（点 OK / 手柄 A/B/Start
  // 关闭后由 drawer 删除），App 无需跟踪指针。
  void ShowOnlineUnavailableDialog() {
    app_context().CallInUIThreadDeferred([this]() {
      new OnlineUnavailableDialog(
          imgui_drawer(),
          static_cast<rex::input::InputSystem*>(runtime()->input_system()));
    });
  }

  // 供 PPC 钩子调用：切到 UI 线程再动 ImGui 对象（ImGui 非线程安全）。
  // F7 与游戏菜单共用本函数 → 全程只有浮层的单一实例。
  void ToggleAchievementsOverlay() {
    app_context().CallInUIThreadDeferred([this]() {
      if (achievements_dialog_) {
        achievements_dialog_->CloseFromApp();  // 下一帧 Draw 末尾自删
      } else {
        OpenAchievementsOverlay();
      }
    });
  }

 protected:
  // SetupOverlays 末尾调用（内置键位已注册）。把内置 F7 绑定替换成
  // 我们的 Toggle——否则内置绑定会创建第二份浮层实例，互相"看起来失灵"。
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    rex::ui::UnregisterBind("bind_achievements");
    rex::ui::RegisterBind("bind_achievements", "F7",
                          "Toggle achievements overlay",
                          [this] { ToggleAchievementsOverlay(); });
  }

  // 模块启动完成后函数表已注册完毕，此时重定向内核导入才不会被覆盖回去。
  // 注意：这只拦截"经函数表的间接调用"；主菜单的调用被 codegen 编译成
  // 对桩符号的直接 C++ 调用，由 manifest 里的 [[midasm_hook]] 在生成期拦截。
  // 两路都通到 ToggleAchievementsOverlay，互为兜底。
  void OnPostLaunchModule(rex::system::XThread* thread) override {
    (void)thread;
    if (runtime() && runtime()->function_dispatcher()) {
      runtime()->function_dispatcher()->SetFunction(
          kXamShowAchievementsUIAddress, &XamShowAchievementsUIHook);
    }
  }

  // 关机时浮层可能还开着：先手动删（此时 drawer/input system 仍存活），
  // 避免其析构回调在 App 部分析构后触碰 runtime()。
  void OnDestroy() override {
    if (achievements_dialog_) {
      auto* dialog = achievements_dialog_;
      achievements_dialog_ = nullptr;  // 防止析构回调重入
      delete dialog;
    }
  }

 private:
  void OpenAchievementsOverlay() {
    // 浮层打开期间手柄输入不进游戏（guest 读到空手柄），关浮层即恢复。
    auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system());
    if (input) {
      input->AddUIInputBlocker();
    }
    achievements_dialog_ = new TimePilotAchievementsDialog(
        imgui_drawer(), immediate_drawer(), runtime(), &achievements(),
        [this] {
          achievements_dialog_ = nullptr;
          auto* in =
              static_cast<rex::input::InputSystem*>(runtime()->input_system());
          if (in) {
            in->RemoveUIInputBlocker();
          }
        });
  }

  // 替代空桩的实现：返回成功并打开本地成就浮层。
  static void XamShowAchievementsUIHook(PPCContext& __restrict ctx,
                                        uint8_t* base) {
    (void)base;
    ctx.r3.u64 = 0;  // X_ERROR_SUCCESS
    if (g_timepilot_app) {
      g_timepilot_app->ToggleAchievementsOverlay();
    }
  }

  // 带手柄导航的成就浮层。ImGuiDialog 遵循 self-deleting 惯例
  // （Close 后下一帧 Draw 末尾 delete this，析构回调清指针），
  // 所以这里必须是裸指针而非 unique_ptr。
  TimePilotAchievementsDialog* achievements_dialog_ = nullptr;

  // 路径解析失败时的说明文本（空 = 正常）。在 OnConfigurePaths 里生成，
  // OnFinalizePaths 里弹框。用宽字符：Windows 原生 MessageBoxW 直接显示，
  // 不受 ImGui 默认字体缺少 CJK 字形的限制。
  static std::wstring BuildMissingDataMessage(const std::filesystem::path& exe_dir) {
    std::wstring message;
    message += L"TimePilot 找不到游戏数据（default.xex）。\n\n";
    message += L"请把游戏资源解压到下面这个位置：\n  ";
    if (!exe_dir.empty()) {
      message += (exe_dir / L"game" / L"default.xex").wstring();
    } else {
      message += L"game\\default.xex   （与 TimePilot.exe 同级）";
    }
    message += L"\n\n";
    message += L"以下布局同样能识别（都放在 exe 同目录）：\n";
    message += L"  game\\ | TimePilot\\ | data\\ | assets\\  里面有 default.xex\n";
    message += L"  default.xex 直接放在 exe 旁边\n";
    message += L"  任意单个子文件夹里有 default.xex（文件夹名字随意）\n";
    message += L"  注意 default.xex 必须在那个文件夹的根上，不能再多套一层\n\n";
    message += L"调试时可用命令行直接指定：--game_data_root=<数据目录>\n";
    message += L"logs 目录下的日志会记录本次尝试过的路径。";
    return message;
  }

  std::wstring data_root_error_;
  // 解析成功时的说明（UTF-8），同样挪到 OnFinalizePaths 再落日志。
  std::string resolved_note_;
};
