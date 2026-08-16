#pragma once

#include "openwow/game/actions/held_cursor/held_cursor.h"

namespace openwow::audio {
class SoundRuntime;
}

namespace openwow::ui::game {
class ScriptEventDispatch;

class HeldCursorPresentation final
    : public openwow::game::actions::held_cursor::PresentationPort {
 public:
  HeldCursorPresentation(ScriptEventDispatch& events,
                         openwow::audio::SoundRuntime& sound)
      : events_(events), sound_(sound) {}

  void ClearHeldOverlay() override;
  void PresentHeldOverlay(
      std::string texture_path,
      openwow::game::actions::held_cursor::TextureMode mode) override;
  void PlaySound(
      openwow::game::actions::held_cursor::Sound sound) override;
  void ShowGrid(openwow::game::actions::held_cursor::Grid grid) override;
  void HideGrid(openwow::game::actions::held_cursor::Grid grid) override;
  void PublishCursorUpdate() override;

 private:
  ScriptEventDispatch& events_;
  openwow::audio::SoundRuntime& sound_;
};

}
