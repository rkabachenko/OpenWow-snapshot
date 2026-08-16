#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game {

struct TargetCastBarInfo {
  ObjectGuid    casterGuid;
  std::string   casterName;
  std::uint32_t spellId          = 0;
  std::string   spellName;
  std::string   spellIcon;
  float         castTime         = 0.0f;
  float         startTime        = 0.0f;
  bool          isChanneling     = false;
  bool          isUninterruptible = false;
  float         currentTime      = 0.0f;
};

class TargetCastBar {
 public:
  TargetCastBar() = default;

  void SetCast(const TargetCastBarInfo& info);
  void ClearCast();

  [[nodiscard]] std::optional<TargetCastBarInfo> GetInfo() const;
  [[nodiscard]] bool IsActive() const;

  [[nodiscard]] float GetProgress(double currentTime) const;

  [[nodiscard]] float GetTimeRemaining(double currentTime) const;
  [[nodiscard]] bool  IsCasting() const;
  [[nodiscard]] bool  IsChanneling() const;

  [[nodiscard]] bool IsInterruptible() const;

  [[nodiscard]] const std::string& GetSpellName() const;

  void Update(float dt);

  void SetTarget(const ObjectGuid& guid);
  [[nodiscard]] ObjectGuid GetTarget() const;

  void Reset();

  void OnCastFailed();

  void OnCastPushback(float pushbackSeconds);

  [[nodiscard]] std::uint32_t GetBarColor() const;

  [[nodiscard]] static std::string FormatTimeRemaining(float seconds);

  [[nodiscard]] ObjectGuid GetCasterGuid() const;

  [[nodiscard]] const std::string& GetSpellIcon() const;

  [[nodiscard]] float GetCastTime() const;

  [[nodiscard]] bool HasFailed() const;

 private:
  std::optional<TargetCastBarInfo> info_;
  ObjectGuid target_guid_;
  float elapsed_ = 0.0f;
  bool failed_   = false;

  static const std::string kEmptyString;
};

}
