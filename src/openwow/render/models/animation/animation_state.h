#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace openwow::render {

namespace AnimId {

inline constexpr std::uint16_t kStand              = 0;
inline constexpr std::uint16_t kDeath              = 1;
inline constexpr std::uint16_t kSpell              = 2;
inline constexpr std::uint16_t kStop               = 3;
inline constexpr std::uint16_t kWalk               = 4;
inline constexpr std::uint16_t kRun                = 5;
inline constexpr std::uint16_t kDead               = 6;
inline constexpr std::uint16_t kRise               = 7;
inline constexpr std::uint16_t kStandWound         = 8;
inline constexpr std::uint16_t kCombatWound        = 9;
inline constexpr std::uint16_t kCombatCritical     = 10;
inline constexpr std::uint16_t kShuffleLeft        = 11;
inline constexpr std::uint16_t kShuffleRight       = 12;
inline constexpr std::uint16_t kWalkBackwards      = 13;
inline constexpr std::uint16_t kStun               = 14;
inline constexpr std::uint16_t kHandsClosed        = 15;
inline constexpr std::uint16_t kAttackUnarmed      = 16;
inline constexpr std::uint16_t kAttack1H           = 17;
inline constexpr std::uint16_t kAttack2H           = 18;
inline constexpr std::uint16_t kAttack2HL          = 19;
inline constexpr std::uint16_t kParryUnarmed       = 20;
inline constexpr std::uint16_t kParry1H            = 21;
inline constexpr std::uint16_t kParry2H            = 22;
inline constexpr std::uint16_t kParry2HL           = 23;
inline constexpr std::uint16_t kShieldBlock        = 24;
inline constexpr std::uint16_t kReadyUnarmed       = 25;
inline constexpr std::uint16_t kReady1H            = 26;
inline constexpr std::uint16_t kReady2H            = 27;
inline constexpr std::uint16_t kReady2HL           = 28;
inline constexpr std::uint16_t kReadyBow           = 29;
inline constexpr std::uint16_t kDodge              = 30;
inline constexpr std::uint16_t kSpellPrecast       = 31;
inline constexpr std::uint16_t kSpellCast          = 32;
inline constexpr std::uint16_t kSpellCastArea      = 33;
inline constexpr std::uint16_t kNPCWelcome         = 34;
inline constexpr std::uint16_t kNPCGoodbye         = 35;
inline constexpr std::uint16_t kBlock              = 36;
inline constexpr std::uint16_t kJumpStart          = 37;
inline constexpr std::uint16_t kJump               = 38;
inline constexpr std::uint16_t kJumpEnd            = 39;
inline constexpr std::uint16_t kFall               = 40;
inline constexpr std::uint16_t kSwimIdle           = 41;
inline constexpr std::uint16_t kSwim               = 42;
inline constexpr std::uint16_t kSwimLeft           = 43;
inline constexpr std::uint16_t kSwimRight          = 44;
inline constexpr std::uint16_t kSwimBackwards      = 45;
inline constexpr std::uint16_t kAttackBow          = 46;
inline constexpr std::uint16_t kFireBow            = 47;
inline constexpr std::uint16_t kReadyRifle         = 48;
inline constexpr std::uint16_t kAttackRifle        = 49;
inline constexpr std::uint16_t kLoot               = 50;
inline constexpr std::uint16_t kReadySpellDirected = 51;
inline constexpr std::uint16_t kReadySpellOmni     = 52;
inline constexpr std::uint16_t kSpellCastDirected  = 53;
inline constexpr std::uint16_t kSpellCastOmni      = 54;
inline constexpr std::uint16_t kBattleRoar         = 55;
inline constexpr std::uint16_t kReadyAbility        = 56;
inline constexpr std::uint16_t kSpecial1H          = 57;
inline constexpr std::uint16_t kSpecial2H          = 58;
inline constexpr std::uint16_t kShieldBash         = 59;
inline constexpr std::uint16_t kEmoteTalk          = 60;
inline constexpr std::uint16_t kEmoteEat           = 61;
inline constexpr std::uint16_t kEmoteWork          = 62;
inline constexpr std::uint16_t kEmoteUseStanding   = 63;

inline constexpr std::uint16_t kSitGroundDown      = 96;
inline constexpr std::uint16_t kSitGround          = 97;
inline constexpr std::uint16_t kSitGroundUp        = 98;
inline constexpr std::uint16_t kSleepDown          = 99;
inline constexpr std::uint16_t kSleep              = 100;
inline constexpr std::uint16_t kSleepUp            = 101;
inline constexpr std::uint16_t kSitChairLow        = 102;
inline constexpr std::uint16_t kSitChairMed        = 103;
inline constexpr std::uint16_t kSitChairHigh       = 104;
inline constexpr std::uint16_t kKneelStart         = 114;
inline constexpr std::uint16_t kKneelLoop          = 115;
inline constexpr std::uint16_t kKneelEnd           = 116;

inline constexpr std::uint16_t kMount              = 91;

inline constexpr std::uint16_t kGOClose            = 146;
inline constexpr std::uint16_t kGOClosed           = 147;
inline constexpr std::uint16_t kGOOpen             = 148;
inline constexpr std::uint16_t kGOOpened           = 149;

inline constexpr std::uint16_t kCustom0            = 153;
inline constexpr std::uint16_t kCustom1            = 154;
inline constexpr std::uint16_t kCustom2            = 155;

inline constexpr std::uint16_t kStealthWalk        = 119;
inline constexpr std::uint16_t kFly                = 135;
inline constexpr std::uint16_t kSprint             = 143;
inline constexpr std::uint16_t kHover              = 193;

inline constexpr std::uint16_t kJumpLandRun        = 187;
inline constexpr std::uint16_t kStealthRun         = 223;

inline constexpr std::uint16_t kShipStart          = 162;
inline constexpr std::uint16_t kShipMoving         = 163;
inline constexpr std::uint16_t kShipStop           = 164;

inline constexpr std::uint16_t kStandHigh          = 190;

inline constexpr std::uint16_t kInvalid            = 0xFFFF;

}

class AnimationState {
 public:
  AnimationState() = default;

  void SetAnimation(std::uint16_t anim_id, bool looping = true);

  bool SetAnimationAtPhase(std::uint16_t anim_id, bool looping,
                           std::uint32_t start_time_ms);

  void PlayOneShot(std::uint16_t anim_id);

  void Restart(std::uint16_t anim_id, bool looping);

  void Update(float dt, std::uint32_t duration_ms);

  [[nodiscard]] std::uint16_t current_anim() const { return current_anim_; }

  [[nodiscard]] std::uint16_t previous_anim() const { return previous_anim_; }

  [[nodiscard]] float blend_factor() const { return blend_factor_; }

  [[nodiscard]] std::uint32_t current_time_ms() const;

  [[nodiscard]] std::uint32_t previous_time_ms() const;

  [[nodiscard]] bool is_looping() const { return is_looping_; }

  [[nodiscard]] bool is_blending() const { return blend_factor_ < 1.0f; }

  void Reset();

  static constexpr float kDefaultBlendDuration = 0.2f;

  void SetBlendDuration(float seconds) { blend_duration_ = seconds; }

  [[nodiscard]] float GetProgress(std::uint32_t duration_ms) const;

  [[nodiscard]] static std::string GetAnimationName(std::uint16_t anim_id);

  [[nodiscard]] bool is_one_shot() const { return is_one_shot_; }

  [[nodiscard]] std::uint16_t fallback_anim() const { return fallback_anim_; }

  using CompletionCallback = std::function<void(std::uint16_t )>;

  void SetCompletionCallback(CompletionCallback cb) {
    completion_callback_ = std::move(cb);
  }

  [[nodiscard]] bool DidAnimationComplete() const { return did_complete_; }

 private:
  std::uint16_t current_anim_{AnimId::kStand};
  std::uint16_t previous_anim_{AnimId::kStand};
  std::uint16_t fallback_anim_{AnimId::kStand};

  double current_time_ms_{0.0};
  double previous_time_ms_{0.0};

  float blend_factor_{1.0f};
  float blend_duration_{kDefaultBlendDuration};

  bool is_looping_{true};
  bool is_one_shot_{false};
  bool did_complete_{false};

  CompletionCallback completion_callback_;
};

}
