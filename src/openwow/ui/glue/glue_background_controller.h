
#pragma once

#include "openwow/ui/glue/glue_game_state.h"
#include "openwow/ui/glue/glue_streaming_counters.h"
#include "openwow/ui/glue/glue_widget_runtime.h"

#include <string>

namespace openwow::ui::glue {

class GlueBackgroundController {
 public:

  void OnScreenTransition(const std::string& old_screen, const std::string& new_screen);

  void OnCharacterPreviewRebuilt();

  void Update(float dt_seconds,
              GlueGameState& gs,
              GlueWidgetRuntime& widgets,
              const GlueStreamingCounters& streaming,
              bool sync_character_scenes = true);

  void SyncActiveCharacterScene(GlueGameState& gs);

  void SetCharSelectModelFrame(GlueGameState& gs, GlueWidgetRuntime& widgets, const std::string& frame_name);

  void SetCharCustomizeModelFrame(GlueGameState& gs, GlueWidgetRuntime& widgets, const std::string& frame_name);

  void SetCharSelectBackground(GlueGameState& gs, GlueWidgetRuntime& widgets, const std::string& filename);

  void SetCharCustomizeBackground(GlueGameState& gs, GlueWidgetRuntime& widgets, const std::string& filename);

  void ApplyCharacterSelectFacing(const GlueGameState& gs, GlueWidgetRuntime& widgets);

  float transition_factor() const { return transition_factor_; }

 private:

  void TickTransition(float dt_seconds, const GlueStreamingCounters& streaming);

  void BindModelFrame(std::weak_ptr<GlueModelFFXWidget>& target_frame,
                      std::string& target_frame_name,
                      GlueWidgetRuntime& widgets,
                      const std::string& frame_name);
  void SetBackgroundForFrame(std::weak_ptr<GlueModelFFXWidget>& target_frame,
                             std::string& target_background,
                             GlueWidgetRuntime& widgets,
                             const std::string& filename,
                             bool set_customize_flag);

  enum class TransitionState : int {
    kInactive = 3,
    kStage1 = 1,
    kStage2 = 2,
  };

  void StartTransition(float base);
  void ReleaseTransitionOverlay();
  void BeginCharacterScreenTransition();

  bool pending_start_{false};

  TransitionState state_{TransitionState::kInactive};
  float base_{0.0f};
  float progress_{1.0f};
  float transition_factor_{1.0f};
  float stage2_elapsed_ms_{0.0f};
};

}
