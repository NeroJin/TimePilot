// TimePilot - ReXGlue Recompiled Project
//
// "Xbox Live 功能不可用"提示框。
//
// 原理：游戏主菜单的 Xbox Live Game / Leaderboards 走进在线流程后会在
// "等待网络异步结果"处死等（运行时伪装登录 + 会话/统计服务空转，
// 游戏永远等不到完成通知 → 冻结）。我们在流程入口（sub_820CBFD8，
// 见 TimePilot_manifest.toml 的 midasm_hook）短路整个在线动作，
// 弹出本提示框。UnleashedRecomp 等项目对在线菜单采用同样的拦截思路。
//
// 手柄支持：轮询 InputSystem::GetStateForUI，A/B/Start 任一按下即关闭
// （与成就浮层同款做法；浮层存续期间持有 UIInputBlocker 屏蔽游戏输入）。

#pragma once

#include <imgui.h>

#include <rex/input/input_system.h>
#include <rex/system/xtypes.h>
#include <rex/ui/imgui_dialog.h>

using rex::X_STATUS;

class OnlineUnavailableDialog : public rex::ui::ImGuiDialog {
 public:
  OnlineUnavailableDialog(rex::ui::ImGuiDrawer* imgui_drawer,
                          rex::input::InputSystem* input_system)
      : rex::ui::ImGuiDialog(imgui_drawer), input_(input_system) {
    if (input_) {
      input_->AddUIInputBlocker();  // 提示框打开期间手柄不进游戏
    }
  }

  ~OnlineUnavailableDialog() override {
    if (input_) {
      input_->RemoveUIInputBlocker();
    }
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    // 手柄 A/B/Start 关闭（边沿检测）。
    if (input_) {
      rex::input::X_INPUT_STATE state{};
      if (input_->GetStateForUI(0, &state) == X_STATUS_SUCCESS) {
        const uint16_t pressed =
            uint16_t(uint16_t(state.gamepad.buttons) & ~prev_buttons_);
        prev_buttons_ = state.gamepad.buttons;
        constexpr uint16_t kCloseMask =
            rex::input::X_INPUT_GAMEPAD_A | rex::input::X_INPUT_GAMEPAD_B |
            rex::input::X_INPUT_GAMEPAD_START;
        if (pressed & kCloseMask) {
          Close();
        }
      }
    }

    // 居中模态框。
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Xbox Live Unavailable##online", nullptr, flags)) {
      ImGui::TextWrapped(
          "Online features are not available in this recompiled port.");
      ImGui::Spacing();
      ImGui::TextDisabled("Press A / B / Start on the controller, or click OK.");
      ImGui::Spacing();
      if (ImGui::Button("OK", ImVec2(-1.0f, 0.0f))) {
        Close();
      }
    }
    ImGui::End();
  }

 private:
  rex::input::InputSystem* input_ = nullptr;
  uint16_t prev_buttons_ = 0;
};
