#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game {

enum class GameCastBarState : std::uint8_t {
  Idle = 0,
  Casting,
  Channeling,
  Failed,
  Interrupted,
  Succeeded,
};

struct TargetCastInfo {
  std::uint32_t spellId  = 0;
  std::string   spellName;
  float         progress = 0.0f;
  float         total    = 0.0f;
  bool          isChannel = false;
};

class CastBarData {
 public:
  CastBarData() = default;

  void StartCast(std::uint32_t spellId, const std::string& spellName,
                 float castTime, bool isChanneled);

  void StopCast(GameCastBarState reason);

  void Update(float dt);

  [[nodiscard]] GameCastBarState GetState() const;

  [[nodiscard]] bool IsCasting() const;

  [[nodiscard]] std::uint32_t GetSpellId() const;
  [[nodiscard]] const std::string& GetSpellName() const;

  [[nodiscard]] float GetProgress() const;

  [[nodiscard]] float GetElapsedTime() const;
  [[nodiscard]] float GetTotalTime() const;
  [[nodiscard]] float GetRemainingTime() const;
  [[nodiscard]] bool  IsChanneled() const;

  [[nodiscard]] float GetProgressDirection() const;

  void SetDelayAmount(float seconds);

  [[nodiscard]] std::uint32_t GetPushbackCount() const;

  [[nodiscard]] std::uint32_t GetMaxPushbacks() const;

  void ApplyPushback(float amount);

  void SetInterruptible(bool v);
  [[nodiscard]] bool IsInterruptible() const;

  void Reset();

  [[nodiscard]] std::optional<TargetCastInfo> GetTargetCast() const;

  void SetTargetCast(std::uint32_t spellId, const std::string& name,
                     float total, bool isChannel);

  void UpdateTargetCast(float dt);

  void ClearTargetCast();

 private:

  GameCastBarState state_  = GameCastBarState::Idle;
  std::uint32_t spell_id_  = 0;
  std::string   spell_name_;
  float         total_time_ = 0.0f;
  float         elapsed_    = 0.0f;
  bool          channeled_  = false;
  bool          interruptible_ = true;
  std::uint32_t pushback_count_ = 0;
  float         delay_amount_   = 0.0f;

  static constexpr std::uint32_t kMaxPushbacks = 2;

  std::optional<TargetCastInfo> target_cast_;
};

}
