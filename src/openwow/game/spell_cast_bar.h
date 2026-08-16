#pragma once

#include <cstdint>
#include <string>

namespace openwow::game {

enum class CastBarState : std::uint8_t {
  Idle = 0,
  Casting,
  Channeling,
  Succeeded,
  Failed,
  Interrupted,
};

struct CastBarColor {
  float r = 1.0f;
  float g = 0.7f;
  float b = 0.0f;
};

struct CastBarDisplayInfo {
  std::string   spellName;
  std::string   spellIcon;
  std::uint32_t spellId          = 0;
  float         castTime         = 0.0f;
  float         elapsed          = 0.0f;
  CastBarState  state            = CastBarState::Idle;
  bool          isUninterruptible = false;
  CastBarColor  barColor;
  float         progress         = 0.0f;
  std::string   targetName;
};

class SpellCastBar {
 public:
  SpellCastBar() = default;

  void StartCast(std::uint32_t spellId, const std::string& name,
                 const std::string& icon, float castTime,
                 bool uninterruptible = false,
                 const std::string& target = "");

  void StartChannel(std::uint32_t spellId, const std::string& name,
                    const std::string& icon, float duration,
                    const std::string& target = "");

  void Interrupt(const std::string& reason = "");
  void Succeed();
  void Fail(const std::string& reason = "");

  [[nodiscard]] CastBarDisplayInfo GetInfo() const;
  [[nodiscard]] CastBarState      GetState() const;

  [[nodiscard]] float GetProgress() const;

  [[nodiscard]] float GetTimeRemaining() const;
  [[nodiscard]] bool  IsCasting() const;
  [[nodiscard]] bool  IsChanneling() const;

  [[nodiscard]] bool IsActive() const;

  [[nodiscard]] const std::string& GetSpellName() const;

  void SetBarColor(const CastBarColor& color);
  [[nodiscard]] CastBarColor GetBarColor() const;

  void Update(float dt);

  void Reset();

 private:
  CastBarState  state_            = CastBarState::Idle;
  std::uint32_t spell_id_         = 0;
  std::string   spell_name_;
  std::string   spell_icon_;
  float         cast_time_        = 0.0f;
  float         elapsed_          = 0.0f;
  bool          uninterruptible_  = false;
  CastBarColor  bar_color_;
  std::string   target_name_;
  std::string   fail_reason_;
};

}
