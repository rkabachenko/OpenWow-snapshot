#include "openwow/ui/glue/glue_background_controller.h"

#include "openwow/ui/glue/cgluemgr.h"
#include "openwow/ui/glue/glue_charselect_scene.h"

#include <algorithm>

namespace openwow::ui::glue {

void GlueBackgroundController::BindModelFrame(std::weak_ptr<GlueModelFFXWidget>& target_frame,
                                              std::string& target_frame_name,
                                              GlueWidgetRuntime& widgets,
                                              const std::string& frame_name) {

  if (frame_name.empty()) return;
  auto w = widgets.ResolveModelFFXWidget(frame_name);
  if (!w) return;
  target_frame = w;
  target_frame_name = frame_name;
}

void GlueBackgroundController::SetBackgroundForFrame(std::weak_ptr<GlueModelFFXWidget>& target_frame,
                                                     std::string& target_background,
                                                     GlueWidgetRuntime& widgets,
                                                     const std::string& filename,
                                                     bool set_customize_flag) {
  if (filename.empty()) return;

  auto frame = target_frame.lock();
  if (!frame) return;

  if (auto* model = frame->model_instance(); model != nullptr) {

    if (openwow::text::EqualsIgnoreCaseAscii(filename, model->RequestedPath())) {
      return;
    }
  }

  const std::string normalized = frame->SetModelFile(filename);
  frame->set_dirty(true);
  widgets.SetModel(frame->name(), filename);

  if (set_customize_flag) {
    frame->set_ghost_branch_gate_enabled(true);
  }

  target_background = normalized;
}

void GlueBackgroundController::StartTransition(float base) {
  base_ = std::clamp(base, 0.0f, 1.0f);
  progress_ = base_;
  transition_factor_ = (1.0f - base_ > 0.0f) ? 0.0f : 1.0f;
  state_ = TransitionState::kStage1;
  stage2_elapsed_ms_ = 0.0f;
  pending_start_ = false;
}

void GlueBackgroundController::ReleaseTransitionOverlay() {
  state_ = TransitionState::kInactive;
  base_ = 0.0f;
  progress_ = 1.0f;
  transition_factor_ = 1.0f;
  stage2_elapsed_ms_ = 0.0f;
  pending_start_ = false;
}

void GlueBackgroundController::BeginCharacterScreenTransition() {

  pending_start_ = true;
  state_ = TransitionState::kInactive;
  stage2_elapsed_ms_ = 0.0f;
}

void GlueBackgroundController::OnCharacterPreviewRebuilt() {
  BeginCharacterScreenTransition();
}

void GlueBackgroundController::OnScreenTransition(const std::string& old_screen,
                                                  const std::string& new_screen) {

  const bool leaving_char_screen = UsesCharacterScreenHandler(old_screen);
  const bool entering_char_screen = UsesCharacterScreenHandler(new_screen);

  if (leaving_char_screen) {
    ReleaseTransitionOverlay();
  }

  if (entering_char_screen) {
    BeginCharacterScreenTransition();
  } else if (!leaving_char_screen) {

    ReleaseTransitionOverlay();
  }
}

void GlueBackgroundController::TickTransition(float dt_seconds,
                                              const GlueStreamingCounters& streaming) {
  if (pending_start_) {
    if (streaming.Complete()) {
      ReleaseTransitionOverlay();
      return;
    }

    StartTransition(std::clamp(streaming.Ratio(), 0.0f, 1.0f));
  }

  if (state_ == TransitionState::kInactive) {
    transition_factor_ = 1.0f;
    return;
  }

  const float dt = std::max(0.0f, dt_seconds);
  const float ratio = std::clamp(streaming.Ratio(), 0.0f, 1.0f);
  bool entered_stage2 = false;

  if (state_ == TransitionState::kStage1) {
    progress_ = std::min(1.0f, progress_ + dt * 0.25f);
    if (progress_ > ratio) {
      progress_ = ratio;
    }
    if (streaming.Complete()) {
      state_ = TransitionState::kStage2;
      stage2_elapsed_ms_ = 0.0f;
      entered_stage2 = true;
    }
  }

  if (state_ == TransitionState::kStage2 && !entered_stage2) {
    progress_ = std::min(1.0f, progress_ + dt * 4.0f);
    stage2_elapsed_ms_ += dt * 1000.0f;
    if (stage2_elapsed_ms_ > 250.0f) {
      ReleaseTransitionOverlay();
      return;
    }
  }

  const float denom = std::max(1e-6f, 1.0f - base_);
  transition_factor_ = std::clamp((progress_ - base_) / denom, 0.0f, 1.0f);
}

void GlueBackgroundController::Update(float dt_seconds,
                                      GlueGameState& gs,
                                      GlueWidgetRuntime& widgets,
                                      const GlueStreamingCounters& streaming,
                                      const bool sync_character_scenes) {
  TickTransition(dt_seconds, streaming);
  widgets.SetGlobalTransitionFactor(transition_factor_);
  widgets.SetGlobalTransitionOverlayVisible(state_ != TransitionState::kInactive);

  if (sync_character_scenes) {
    SyncActiveCharacterScene(gs);
  }
}

void GlueBackgroundController::SyncActiveCharacterScene(GlueGameState& gs) {

  if (IsGlueScreenName(gs.current_screen, "charcreate")) {
    if (gs.char_customize_scene != nullptr) {
      gs.char_customize_scene->SyncCreateFromGameState(gs);
      gs.char_customize_scene->ApplyCreateFacing(gs.create_facing);
    }
    return;
  }

  if ((gs.current_screen.empty() || IsGlueScreenName(gs.current_screen, "charselect")) &&
      gs.char_select_scene != nullptr) {
    gs.char_select_scene->SyncFromGameState(gs);
    gs.char_select_scene->ApplySelectFacing(gs.select_facing);
  }
}

void GlueBackgroundController::SetCharSelectModelFrame(GlueGameState& gs,
                                                       GlueWidgetRuntime& widgets,
                                                       const std::string& frame_name) {
  BindModelFrame(gs.char_select_model_frame, gs.char_select_model_frame_name, widgets, frame_name);
}

void GlueBackgroundController::SetCharCustomizeModelFrame(GlueGameState& gs,
                                                          GlueWidgetRuntime& widgets,
                                                          const std::string& frame_name) {
  BindModelFrame(gs.char_customize_model_frame, gs.char_customize_frame_name, widgets, frame_name);
}

void GlueBackgroundController::SetCharSelectBackground(GlueGameState& gs,
                                                       GlueWidgetRuntime& widgets,
                                                       const std::string& filename) {
  SetBackgroundForFrame(gs.char_select_model_frame,
                        gs.char_select_background,
                        widgets,
                        filename,
                        false);
}

void GlueBackgroundController::SetCharCustomizeBackground(GlueGameState& gs,
                                                          GlueWidgetRuntime& widgets,
                                                          const std::string& filename) {
  SetBackgroundForFrame(gs.char_customize_model_frame,
                        gs.char_customize_background,
                        widgets,
                        filename,
                        true);
}

void GlueBackgroundController::ApplyCharacterSelectFacing(const GlueGameState& gs,
                                                            GlueWidgetRuntime& widgets) {
  (void)widgets;
  if (gs.char_select_scene != nullptr) {
    gs.char_select_scene->ApplySelectFacing(gs.select_facing);
  }
}

}
