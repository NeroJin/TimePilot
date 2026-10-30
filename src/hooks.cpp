// TimePilot - ReXGlue Recompiled Project
//
// 生成的 _recomp 目标引用的宿主钩子集中定义在这里。
// （钩子是普通函数而非 inline：_recomp 目标是独立编译的 object library，
//   它引用的符号必须有一个最终被链入 exe 的强定义。）

#include <cstdio>

#include <rex/runtime.h>

#include "generated/default/TimePilot_init.h"

#include "TimePilot_app.h"

// App 单实例指针（在 TimePilot_app.h 中声明为 extern）。
TimepilotApp* g_timepilot_app = nullptr;

// [[midasm_hook]] 宿主钩子（见 TimePilot_manifest.toml 末尾）。
// 注入点：sub_820E6198 中 `b 0x82459D64` 指令处，return = true 短路原调用。
// 真实签名 u32 XamShowAchievementsUI(...)：返回 X_ERROR_SUCCESS，
// 并唤起与 F7 相同的成就浮层。
void TimePilot_AchievementsUIHook(PPCRegister& r3) {
  r3.u64 = 0;  // X_ERROR_SUCCESS
  if (g_timepilot_app) {
    g_timepilot_app->ToggleAchievementsOverlay();
  }
}

// [[midasm_hook]] 宿主钩子：注入点 sub_820CBFD8+4（跳过首条 mflr r12），
// return = true 短路整个函数（该函数是 Xbox Live Game / Leaderboards 共用
// 的在线状态 enter：伪装登录 → 转场到永远等不到数据的联网界面 → 冻结）。
// 正常情况下状态机切换钩子（下方）会在更上游拦住，本钩子只是安全网。
// 同时把 r12（= 调用方返回地址，mflr r12 已执行）写入 trace 日志。
void TimePilot_OnlineFeatureHook(PPCRegister& r3, PPCRegister& r4,
                                 PPCRegister& r12) {
  if (FILE* trace = fopen("online_trace.log", "a")) {
    fprintf(trace,
            "OnlineFeatureHook: this=0x%08X mode=%u caller_lr=0x%08X\n",
            uint32_t(r3.u32), uint32_t(r4.u32), uint32_t(r12.u32));
    fclose(trace);
  }
  r3.u64 = 0;  // X_ERROR_SUCCESS
  if (g_timepilot_app) {
    g_timepilot_app->ShowOnlineUnavailableDialog();
  }
}

// [[midasm_hook]] 宿主钩子：注入点 sub_820C6BC8 入口（通用状态机切换函数
// Transition(this=r3, target_index=r4, ...)），return = false（不短路，
// 仅在检测到在线状态时改写参数）。
//
// 背景：之前只短路在线状态 enter（sub_820CBFD8）时，切换函数已经先对
// 主菜单状态调用了 exit 并把 current 索引改成在线状态，导致关闭提示框后
// 游戏停留在"无菜单、背景流动"的在线状态里僵死。
//
// 做法：读客体内存，取 states[r4]（数组基址 r3+12，元素 4 字节）的虚表
// +60（enter 槽位，与生成代码 lwz r11,60(r11) 对应），若等于在线状态
// enter 函数 0x820CBFD8，就把 r4 改成 -1——切换函数自身的参数校验
// （cmpwi r4,0; blt → 直接走收尾）会把它变成彻底的空操作：
// 主菜单不退场、在线状态不进入，只弹"在线功能不可用"提示框。
void TimePilot_OnlineTransitionHook(PPCRegister& r3, PPCRegister& r4) {
  constexpr uint32_t kOnlineEnterFn = 0x820CBFD8;
  rex::Runtime* runtime = rex::Runtime::instance();
  const int32_t target = r4.s32;
  if (!runtime || target < 0) {
    return;
  }
  uint8_t* base = runtime->virtual_membase();
  const uint32_t manager = r3.u32;
  if (!manager) {
    return;
  }
  // 计数在 manager+8；越界的目标让原函数自己走非法参数分支。
  const uint32_t count = REX_LOAD_U32(manager + 8);
  if (uint32_t(target) >= count) {
    return;
  }
  const uint32_t state_ptr = REX_LOAD_U32(manager + 12 + uint32_t(target) * 4);
  if (!state_ptr) {
    return;
  }
  const uint32_t vtable = REX_LOAD_U32(state_ptr);
  if (!vtable) {
    return;
  }
  const uint32_t enter_fn = REX_LOAD_U32(vtable + 60);
  if (enter_fn == kOnlineEnterFn) {
    if (FILE* trace = fopen("online_trace.log", "a")) {
      fprintf(trace,
              "OnlineTransitionHook: blocked manager=0x%08X state=0x%08X\n",
              manager, state_ptr);
      fclose(trace);
    }
    r4.s64 = -1;  // 原函数视为非法索引 → 整个切换变空操作
    if (g_timepilot_app) {
      g_timepilot_app->ShowOnlineUnavailableDialog();
    }
  }
}
