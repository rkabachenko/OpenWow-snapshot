#pragma once

#include "openwow/game/delayed_spell_visual_kit.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_presentation_snapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::data::dbc {
struct SpellVisualKitEntry;
}

namespace openwow::game {

enum class SpellVisualSpellBinding : std::uint8_t {
  kInferOwnerSpell,
  kUnbound,
};

class CGUnit_C;
class ChainChannelVisualNode;
class QueryCache;
class WorldSession;
struct SpellNode;
enum class HardcodedEffectId : std::uint32_t;

class UnitSpellVisualRuntime {
public:
  struct AttachedEffectNode {
    static constexpr std::uint32_t kFlagPendingDestroy = 0x20u;
    static constexpr std::uint32_t kFlagChannelVisual = 0x40u;
    static constexpr std::uint32_t kFlagPlayingAnimation = 0x1000u;
    static constexpr std::uint32_t kFlagStandalone = 0x2000u;
    static constexpr std::uint32_t kFlagPersistent = 0x20000u;
    static constexpr std::uint32_t kFlagAuraVisual = 0x40000u;
    static constexpr std::uint32_t kChannelTeardownMask = 0x3040u;

    std::uint32_t spell_id{0};
    std::uint32_t flags{0};
    std::uint32_t effect_record_id{0};
    std::uint32_t spell_record_id{0};
    std::int32_t attachment_point{0};
    std::uint32_t visual_kit_param{0};
    std::uintptr_t group_param{0};
  };

  struct DispatchEffect {
    std::uint32_t effect_name_id{0};
    std::string model_path{};
    float resource_scale{1.0f};
    std::int32_t attachment_id{0};
    std::uint32_t source_field_index{0};
    std::uintptr_t transform_key{0};
    bool world_space{false};
    bool uses_explicit_world_position{false};
    bool from_model_attach{false};
    std::array<float, 3> offset{};
    std::array<float, 3> rotation{};
  };

  enum class ProcOutcome : std::uint8_t {
    kIgnored,
    kSuppressed,
    kSkippedNoTarget,
    kSkippedUnresolvedSource,
    kApplied,
  };

  struct ProcExecution {
    std::uint32_t slot{0};
    std::uint32_t type{0};
    std::array<float, 4> parameters{};
    ProcOutcome outcome{ProcOutcome::kIgnored};
    ObjectGuid source_guid{};
    std::vector<ObjectGuid> target_guids{};
    std::optional<std::array<float, 3>> fixed_target_position{};
    std::uint32_t packed_value{0};
    std::uint32_t duration_ms{0};
    std::uint32_t delay_ms{0};
    std::uint32_t selector{0};
    std::uint32_t event_parameter{0};
    std::uint64_t logical_effect_id{0};
  };

  struct DispatchRecord {
    std::uint32_t kit_id{0};
    std::uint32_t spell_id{0};
    std::uint32_t dispatch_type{0};
    std::uint32_t raw_flags{0};
    std::uint32_t sound_kit_id{0};
    std::uint32_t shake_id{0};
    SpellVisualLifecycleAction lifecycle_action{
        SpellVisualLifecycleAction::kTransient};
    SpellVisualPresentationPhase phase{SpellVisualPresentationPhase::kEffect};
    std::uint8_t aura_slot{0};
    std::uint32_t spell_visual_id{0};
    bool harmful{false};
    std::optional<std::array<float, 3>> world_position{};
    std::vector<DispatchEffect> effects{};
    std::vector<ProcExecution> procs{};

    std::optional<SpellMissilePresentationData> missile;
    std::array<float, 3> missile_source_position{};
    std::uint64_t missile_caster_guid{0};
    std::uint8_t missile_cast_count{0};
    std::uint64_t missile_target_guid{0};
    std::array<float, 3> missile_target_position{};
    float missile_speed{0.0f};
    std::uint8_t missile_impact_result{0};
    std::uint8_t missile_reflect_result{0};

    std::uint32_t deferred_impact_kit_id{0};
  };

  struct AlphaFadeEffectNode {
    std::uint32_t spell_id{0};
    std::uint32_t kit_id{0};
    std::uint32_t kit_flags{0};
    float alpha_value{1.0f};
    std::uint32_t duration_ms{0};
  };

  struct BodyTintEffectNode {
    std::uint32_t spell_id{0};
    std::uint32_t packed_argb{0xFFFFFFFFu};
  };

  struct TransientModelColorProc {
    std::uint32_t packed_argb{0};
    std::uint32_t duration_ms{0};
  };

  explicit UnitSpellVisualRuntime(CGUnit_C& owner) noexcept;
  ~UnitSpellVisualRuntime();

  UnitSpellVisualRuntime(const UnitSpellVisualRuntime&) = delete;
  UnitSpellVisualRuntime& operator=(const UnitSpellVisualRuntime&) = delete;

  void SetCreatureInfoCallback(std::function<void(std::uint32_t, bool)> callback);
  void CreateFromCreatureInfo();
  void ClearCreatureInfo();
  [[nodiscard]] bool IsCreatureInfoActive() const noexcept {
    return creature_info_active_;
  }

  [[nodiscard]] bool ShouldApplyVisibleHumanoid(std::uint32_t filter_flags) const;
  [[nodiscard]] bool UpdateVisibleHumanoidDisplay(
      std::uint32_t creature_entry, std::uint32_t filter_flags,
      const QueryCache* query_cache = nullptr);
  [[nodiscard]] std::uint32_t VisibleHumanoidDisplayId() const noexcept {
    return visible_humanoid_display_id_;
  }

  void EnsureChainChannelNode();
  void ResetChainChannelNode();
  [[nodiscard]] bool HasChainChannelNode() const noexcept;

  void RefreshDescriptorChannelVisual(const WorldSession& session);

  void AddAttachedEffect(const WorldSession& session,
                         const AttachedEffectNode& node);
  [[nodiscard]] std::size_t AttachedEffectCount() const;
  void RemoveEffectsBySpellId(const WorldSession& session,
                              std::uint32_t spell_id,
                              bool force_remove_persistent);
  void RemoveMatchingEffects(const WorldSession& session,
                             std::uint32_t effect_record_id,
                             std::int32_t attachment_point,
                             std::uint32_t caster_id,
                             std::uint32_t spell_record_id,
                             std::uint32_t group_param);
  void ResetMatchingNodes(const WorldSession& session, std::uint32_t spell_id,
                          std::uint32_t visual_kit_param);
  void DestroyByKitType(const WorldSession& session, std::uint32_t spell_id,
                        std::uint32_t kit_type, std::uint32_t visual_param,
                        bool filter_spell, bool filter_param);
  void DestroyLootEffects(const WorldSession& session);
  void CleanupForLootEffect();
  void TeardownChannelEffectsIfAuraMissing(const WorldSession& session,
                                           std::uint32_t spell_id);

  void ReleaseAlphaFadeForSpell(std::uint32_t spell_id);

  void EndSpellVisualProcState(std::uint32_t spell_id);

  [[nodiscard]] bool HasLoopingAlphaEffect() const noexcept {
    return has_looping_alpha_effect_;
  }
  [[nodiscard]] const std::vector<AlphaFadeEffectNode>& AlphaFadeEffects() const {
    return alpha_fade_effect_nodes_;
  }
  [[nodiscard]] const std::vector<BodyTintEffectNode>& BodyTintEffects() const {
    return body_tint_effect_nodes_;
  }
  [[nodiscard]] const std::optional<TransientModelColorProc>&
  LastTransientModelColorProc() const {
    return last_transient_model_color_proc_;
  }

  void SetFixedTargetPosition(const std::array<float, 3>* position) noexcept;
  [[nodiscard]] bool HasFixedTargetPosition() const noexcept;
  [[nodiscard]] const std::array<float, 3>& FixedTargetPosition() const noexcept;

  void QueueMissileVisual(std::uint32_t spell_id,
                          std::uint32_t spell_visual_id,
                          SpellMissilePresentationData missile,
                          std::uint64_t missile_caster_guid,
                          std::uint8_t missile_cast_count,
                          std::uint64_t target_guid,
                          const std::array<float, 3>& source_position,
                          const std::array<float, 3>& target_position,
                          float speed,
                          std::uint32_t impact_kit_id = 0,
                          std::uint8_t impact_result = 0,
                          std::uint8_t reflect_result = 0);

  [[nodiscard]] bool CreateFromKit(
      const WorldSession& session, std::uint32_t kit_id,
      std::uint32_t dispatch_type,
      const std::array<float, 3>* world_position = nullptr,
      bool harmful = false, ObjectGuid explicit_chain_source = {},
      std::uint32_t spell_id = 0,
      std::uint32_t spell_visual_id = 0,
      SpellVisualPresentationPhase phase = SpellVisualPresentationPhase::kEffect,
      SpellVisualLifecycleAction action = SpellVisualLifecycleAction::kTransient,
      std::uint8_t aura_slot = 0,
      SpellVisualSpellBinding spell_binding =
          SpellVisualSpellBinding::kInferOwnerSpell);
  void QueueAuraVisualStop(std::uint8_t aura_slot, std::uint32_t spell_id,
                           std::uint32_t spell_visual_id,
                           std::uint32_t state_kit_id);
  void QueueChannelVisualStop(std::uint32_t spell_id);
  void QueueCastVisualStop(std::uint32_t spell_id);
  [[nodiscard]] const std::vector<DispatchRecord>& Dispatches() const;
  void ClearDispatches();

  DelayedSpellVisualKit& AddDelayedKit(
      std::uint32_t spell_id, std::uint32_t visual_record,
      std::uint32_t effect_index, const DelayedVisualKitPosition* position,
      std::uint32_t position_parameter_a, std::uint32_t position_parameter_b,
      bool flag_a, bool flag_b, std::uint32_t timestamp,
      std::uint32_t visual_type, std::uint32_t expire_time);
  [[nodiscard]] std::vector<DelayedSpellVisualKit>& DelayedKits();
  [[nodiscard]] const std::vector<DelayedSpellVisualKit>& DelayedKits() const;

  void RedirectSpellListForGuid(const std::uint8_t* match_data);
  void RecordAnimHitPosition(const float* position);
  void AdvanceFrame(std::uint32_t tick_count);

  void UpdateObjectEffect();
  void Cleanup();

  void SetPendingAuraVisualId(std::uint32_t id) noexcept {
    pending_aura_visual_id_ = id;
  }
  [[nodiscard]] std::uint32_t PendingAuraVisualId() const noexcept {
    return pending_aura_visual_id_;
  }

private:

  std::uint32_t RefreshOpacityFromAlphaFadeHead();

  void ClearVisibleHumanoidDisplay();
  [[nodiscard]] bool RefreshVisibleHumanoidDisplay(
      std::uint32_t creature_entry, const QueryCache* query_cache);
  void ProcessChainProcs(const WorldSession& session,
                         const data::dbc::SpellVisualKitEntry& kit,
                         std::uint32_t spell_id, std::uint32_t effect_flags,
                         ObjectGuid explicit_chain_source,
                         DispatchRecord& dispatch);

  CGUnit_C& owner_;
  std::function<void(std::uint32_t, bool)> creature_info_callback_{};
  std::uint32_t visible_humanoid_creature_entry_{0};
  std::uint32_t visible_humanoid_display_id_{0};
  bool creature_info_active_{false};
  std::uint32_t creature_info_effect_id_{0};
  SpellNode* spell_node_list_head_{nullptr};
  std::vector<DispatchRecord> dispatches_{};
  std::uint32_t next_chain_event_parameter_{0};
  std::array<float, 3> fixed_target_position_{};
  bool fixed_target_present_{false};
  std::vector<DelayedSpellVisualKit> delayed_kits_{};
  std::unique_ptr<ChainChannelVisualNode> chain_channel_node_{};
  std::vector<AlphaFadeEffectNode> alpha_fade_effect_nodes_{};
  std::vector<BodyTintEffectNode> body_tint_effect_nodes_{};
  std::optional<TransientModelColorProc> last_transient_model_color_proc_{};
  bool has_looping_alpha_effect_{false};
  std::array<float, 3> pending_anim_hit_position_{};
  std::uint32_t last_spell_update_time_{0};
  std::uint32_t pending_aura_visual_id_{0};
};

void AddHardcodedOneShotEffect(const WorldSession& session, CGUnit_C& unit,
                               HardcodedEffectId effect);

}
