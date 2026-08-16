#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game {

enum class CinematicDisplayState : std::uint8_t {
  Idle = 0,
  Playing = 1,
  Paused = 2,
  Finished = 3,
};

struct CinematicInfo {
  std::uint32_t cinematicId = 0;
  std::string name;
  float duration = 0.0f;
  float currentTime = 0.0f;
  CinematicDisplayState state = CinematicDisplayState::Idle;
  bool isSkippable = true;
};

class CinematicController {
 public:

  void Play(std::uint32_t cinematicId, const std::string& name, float duration,
            bool skippable = true);

  void Stop();

  void Pause();

  void Resume();

  bool Skip();

  [[nodiscard]] CinematicDisplayState GetState() const;

  [[nodiscard]] float GetCurrentTime() const;

  [[nodiscard]] float GetDuration() const;

  [[nodiscard]] float GetProgress() const;

  [[nodiscard]] std::string GetCinematicName() const;

  [[nodiscard]] bool IsPlaying() const;

  [[nodiscard]] bool IsSkippable() const;

  [[nodiscard]] std::optional<CinematicInfo> GetInfo() const;

  void Update(float dt);

  void Reset();

 private:
  CinematicDisplayState state_ = CinematicDisplayState::Idle;
  std::uint32_t cinematicId_ = 0;
  std::string name_;
  float duration_ = 0.0f;
  float currentTime_ = 0.0f;
  bool skippable_ = true;
};

}
