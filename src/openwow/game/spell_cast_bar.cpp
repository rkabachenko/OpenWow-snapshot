
#include "openwow/game/spell_cast_bar.h"

#include <algorithm>

namespace openwow::game {

void SpellCastBar::StartCast(std::uint32_t spellId, const std::string& name,
                             const std::string& icon, float castTime,
                             bool uninterruptible,
                             const std::string& target) {
  state_           = CastBarState::Casting;
  spell_id_        = spellId;
  spell_name_      = name;
  spell_icon_      = icon;
  cast_time_       = castTime;
  elapsed_         = 0.0f;
  uninterruptible_ = uninterruptible;
  target_name_     = target;
  fail_reason_.clear();
}

void SpellCastBar::StartChannel(std::uint32_t spellId, const std::string& name,
                                const std::string& icon, float duration,
                                const std::string& target) {
  state_           = CastBarState::Channeling;
  spell_id_        = spellId;
  spell_name_      = name;
  spell_icon_      = icon;
  cast_time_       = duration;
  elapsed_         = 0.0f;
  uninterruptible_ = false;
  target_name_     = target;
  fail_reason_.clear();
}

void SpellCastBar::Interrupt(const std::string& reason) {
  if (state_ == CastBarState::Casting || state_ == CastBarState::Channeling) {
    state_       = CastBarState::Interrupted;
    fail_reason_ = reason;
  }
}

void SpellCastBar::Succeed() {
  if (state_ == CastBarState::Casting || state_ == CastBarState::Channeling) {
    state_ = CastBarState::Succeeded;
  }
}

void SpellCastBar::Fail(const std::string& reason) {
  if (state_ == CastBarState::Casting || state_ == CastBarState::Channeling) {
    state_       = CastBarState::Failed;
    fail_reason_ = reason;
  }
}

CastBarDisplayInfo SpellCastBar::GetInfo() const {
  CastBarDisplayInfo info;
  info.spellName        = spell_name_;
  info.spellIcon        = spell_icon_;
  info.spellId          = spell_id_;
  info.castTime         = cast_time_;
  info.elapsed          = elapsed_;
  info.state            = state_;
  info.isUninterruptible = uninterruptible_;
  info.barColor         = bar_color_;
  info.progress         = GetProgress();
  info.targetName       = target_name_;
  return info;
}

CastBarState SpellCastBar::GetState() const { return state_; }

float SpellCastBar::GetProgress() const {
  if (cast_time_ <= 0.0f) return 0.0f;
  float ratio = std::clamp(elapsed_ / cast_time_, 0.0f, 1.0f);

  return (state_ == CastBarState::Channeling) ? (1.0f - ratio) : ratio;
}

float SpellCastBar::GetTimeRemaining() const {
  return std::max(0.0f, cast_time_ - elapsed_);
}

bool SpellCastBar::IsCasting() const {
  return state_ == CastBarState::Casting;
}

bool SpellCastBar::IsChanneling() const {
  return state_ == CastBarState::Channeling;
}

bool SpellCastBar::IsActive() const {
  return IsCasting() || IsChanneling();
}

const std::string& SpellCastBar::GetSpellName() const {
  return spell_name_;
}

void SpellCastBar::SetBarColor(const CastBarColor& color) {
  bar_color_ = color;
}

CastBarColor SpellCastBar::GetBarColor() const {
  return bar_color_;
}

void SpellCastBar::Update(float dt) {
  if (!IsActive()) return;

  elapsed_ += dt;

  if (elapsed_ >= cast_time_) {
    elapsed_ = cast_time_;
    if (state_ == CastBarState::Casting) {
      state_ = CastBarState::Succeeded;
    } else if (state_ == CastBarState::Channeling) {
      state_ = CastBarState::Idle;
    }
  }
}

void SpellCastBar::Reset() {
  state_           = CastBarState::Idle;
  spell_id_        = 0;
  spell_name_.clear();
  spell_icon_.clear();
  cast_time_       = 0.0f;
  elapsed_         = 0.0f;
  uninterruptible_ = false;
  bar_color_       = CastBarColor{};
  target_name_.clear();
  fail_reason_.clear();
}

}
