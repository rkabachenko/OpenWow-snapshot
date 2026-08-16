#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

class CGUnit_C;
class SpellCastRuntime;
class WorldSession;
enum class ProcTriggerType : std::uint8_t;

struct CastInfo {
  std::uint32_t spell_id = 0;
  std::string spell_name;
  std::string spell_text;
  std::string texture;
  std::uint64_t start_time = 0;
  std::uint64_t end_time = 0;
  bool is_channel = false;
  bool not_interruptible = false;
  bool is_tradeskill = false;
  std::uint32_t cast_id = 0;
};

struct DelayedMissileTrajectoryState {
  std::uint32_t spell_id = 0;
  std::uint32_t cast_start_tick_ms = 0;
  std::uint32_t spell_go_tick_ms = 0;
  std::uint32_t server_delay_ms = 0;
  std::uint8_t active_missile_cast_count = 0;

  void Begin(std::uint32_t new_spell_id,
             std::uint32_t new_cast_start_tick_ms) noexcept;
  void Complete(std::uint32_t completed_spell_id,
                std::uint32_t new_spell_go_tick_ms,
                std::uint32_t new_server_delay_ms) noexcept;
  void Cancel(std::uint32_t cancelled_spell_id) noexcept;
  [[nodiscard]] bool HasPayload() const noexcept;
  [[nodiscard]] bool HasActiveMissile() const noexcept;
};

class UnitCastRuntime {
public:
  void OnPlayerSpellCompleted(CGUnit_C &owner);
  void SetCurrentCast(const CastInfo &cast);
  void ClearCurrentCast();
  void SetChannelCast(const CastInfo &cast);
  void ClearChannelCast();
  [[nodiscard]] const CastInfo &GetCurrentCast() const;
  [[nodiscard]] const CastInfo &GetChannelCast() const;
  [[nodiscard]] bool IsCasting() const;
  [[nodiscard]] bool IsChanneling() const;
  [[nodiscard]] bool IsCurrentCastUninterruptible() const;
  [[nodiscard]] std::int32_t GetCurrentCastPrecastStartAnimId(
      const CGUnit_C &owner) const;

  void OnSpellHitProc(const CGUnit_C &owner, std::uint32_t spell_id,
                      std::uint32_t school_mask, std::uint64_t target_guid,
                      bool is_crit);
  void OnSpellHealProc(const CGUnit_C &owner, std::uint32_t spell_id,
                       std::uint32_t school_mask, std::uint64_t target_guid);
  void OnDamageTakenProc(const CGUnit_C &owner, std::uint32_t spell_id,
                         std::uint32_t school_mask,
                         std::uint64_t attacker_guid);
  void OnDefenseProc(const CGUnit_C &owner, std::uint32_t spell_id,
                     ProcTriggerType defense_type,
                     std::uint64_t attacker_guid);
  void OnCastProc(const CGUnit_C &owner, std::uint32_t spell_id,
                  std::uint32_t school_mask, std::uint64_t target_guid);

  void ResetChannelTiming(std::uint32_t current_time);
  void ApplyChannelPushback(std::int32_t delay_ms,
                            std::uint32_t current_time);
  void ResetChannelBase(std::int32_t base_time, std::uint32_t current_time);
  void OnChannelingComplete(CGUnit_C &owner, const WorldSession &session,
                            std::uint32_t cast_id, bool resume_attack,
                            bool trigger_completion);

  void HandleSpellCast(CGUnit_C &owner, SpellCastRuntime &spell_cast_runtime,
                       std::uint32_t spell_id, bool show_learn_visual,
                       bool play_animation);
  void ForgetSpellCastTracking(CGUnit_C &owner, std::uint32_t spell_id);
  [[nodiscard]] bool HasTrackedItemRequirementSpell(
      std::uint32_t spell_id) const;
  [[nodiscard]] const std::vector<std::uint32_t> &
  GetTrackedItemRequirementSpellIds() const noexcept;
  void ForgetTrackedItemRequirementSpell(std::uint32_t spell_id);
  [[nodiscard]] std::optional<std::uint32_t>
  GetTrackedTradeSkillSpell(std::uint32_t skill_line_id) const;
  [[nodiscard]] std::optional<std::uint32_t>
  GetTrackedProficiencySpell(std::uint32_t proficiency_id) const;
  [[nodiscard]] bool CanEquipWeaponInOffHand() const noexcept;
  void ClearOffhandWeaponOverrideSpell() noexcept;
  void ClearSpellTracking();

  void ProcessMissileHitTargets(CGUnit_C &owner, const WorldSession &session,
                                std::span<const ObjectGuid> targets,
                                std::uint32_t spell_id);
  void FinalizeTrackedSpell(CGUnit_C &owner, const WorldSession &session,
                            std::uint32_t spell_id);
  [[nodiscard]] ObjectGuid GetTrackedSpellTarget() const noexcept;
  [[nodiscard]] std::uint32_t GetTrackedSpellTargetSpellId() const noexcept;
  [[nodiscard]] const std::vector<ObjectGuid> &
  GetMissileHitOtherTargets() const noexcept;
  void ClearMissileHitOtherTargets();

  void BeginDelayedMissileTrajectory(std::uint32_t spell_id,
                                     std::uint32_t cast_start_tick_ms);
  void CompleteDelayedMissileTrajectory(std::uint32_t spell_id,
                                        std::uint32_t spell_go_tick_ms,
                                        std::uint32_t server_delay_ms);
  void CancelDelayedMissileTrajectory(std::uint32_t spell_id);
  void SetDelayedMissileTrajectoryActiveCastCount(std::uint8_t cast_count);
  [[nodiscard]] bool HasPendingMissileTrajectory() const;
  [[nodiscard]] bool HasActiveMissileTrajectory() const;
  [[nodiscard]] const DelayedMissileTrajectoryState &
  GetDelayedMissileTrajectoryState() const noexcept;

  void SetComboSpellId(std::uint32_t spell_id);
  [[nodiscard]] std::uint32_t GetComboSpellId() const noexcept;
  void SetComboPointTarget(std::uint32_t target_id);
  [[nodiscard]] std::uint32_t GetComboPointTarget() const noexcept;
  void SetComboTarget(ObjectGuid target) noexcept { combo_target_ = target; }
  [[nodiscard]] ObjectGuid GetComboTarget() const noexcept {
    return combo_target_;
  }
  void SetSpellCooldownExpiry(std::uint32_t current_time);
  [[nodiscard]] std::uint32_t GetSpellCooldownExpiry() const noexcept;

  [[nodiscard]] std::uint32_t GetChannelSpellId(const CGUnit_C &owner) const;
  [[nodiscard]] ObjectGuid GetChannelObject(const CGUnit_C &owner) const;
  [[nodiscard]] std::uint32_t GetCreatedBySpell(const CGUnit_C &owner) const;
  [[nodiscard]] bool CheckCasterRequirements(
      const CGUnit_C &owner, std::int32_t error_context,
      const std::uint8_t *spell_data, std::int32_t item_data) const;

private:
  void BuildMissileHitTargetList(const CGUnit_C &owner,
                                 std::span<const ObjectGuid> targets);

  CastInfo current_cast_;
  CastInfo channel_cast_;
  std::vector<std::uint32_t> pending_spell_casts_;
  std::vector<std::uint32_t> tracked_item_requirement_spell_ids_;
  std::unordered_map<std::uint32_t, std::uint32_t> tracked_trade_skill_spells_;
  std::unordered_map<std::uint32_t, std::uint32_t> tracked_proficiency_spells_;
  std::uint32_t offhand_weapon_override_spell_id_{0};
  DelayedMissileTrajectoryState delayed_missile_trajectory_{};
  ObjectGuid tracked_spell_target_;
  std::uint32_t tracked_spell_target_spell_id_{0};
  std::vector<ObjectGuid> missile_hit_other_targets_;
  std::uint32_t combo_spell_id_{0};
  std::uint32_t combo_point_target_{0};
  ObjectGuid combo_target_;
  std::uint32_t spell_cooldown_expiry_{0};
  std::int32_t channel_spell_amount_{0};
  std::int32_t channel_start_time_{0};
  std::int32_t channel_end_time_{0};
  std::int32_t channel_delay_count_{0};
  std::int32_t channel_delay_start_{0};
  std::int32_t channel_delay_end_{0};
};

}
