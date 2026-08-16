#pragma once

#include "openwow/game/character_animation.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct ObjectHandle {
  ObjectGuid guid;
  std::uint64_t generation{0};

  [[nodiscard]] bool operator==(const ObjectHandle&) const = default;

  struct Hash {
    [[nodiscard]] std::size_t operator()(const ObjectHandle& value) const
        noexcept {
      const auto guid_hash = ObjectGuid::Hash{}(value.guid);
      const auto generation_hash = std::hash<std::uint64_t>{}(value.generation);
      return guid_hash ^
             (generation_hash + 0x9e3779b97f4a7c15ULL +
              (guid_hash << 6u) + (guid_hash >> 2u));
    }
  };
};

inline constexpr std::uint32_t kNoPresentationSlot = 0xFFFFFFFFu;

struct ObjectPresentationRecord {
  ObjectHandle handle;

  std::uint32_t presentation_slot{kNoPresentationSlot};
  TypeID type_id{TypeID::kObject};
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float facing{0.0f};
  float scale{1.0f};
  std::uint32_t display_id{0};
  std::uint32_t health{0};
  std::uint32_t max_health{0};
  std::uint32_t level{0};
  std::uint32_t movement_flags{0};
  std::uint32_t mount_display_id{0};
  std::uint32_t unit_flags{0};
  std::uint32_t unit_flags2{0};
  std::uint32_t creature_type{0};
  std::uint32_t model_relation_mask{0};
  std::uint32_t dynamic_flags{0};

  std::uint8_t stand_state{0};

  float render_opacity{1.0f};
  bool controlled_by_local_player{false};
  bool game_object_requirements_met{false};
  bool game_object_highlighted{false};
  bool game_object_spell_focus_eligible{false};
  bool corpse_spell_target_eligible{false};
  bool unit_click_deferred{false};
  bool mounted{false};

  CharacterLocomotionState locomotion{};
};

enum class SpellVisualLifecycleAction : std::uint8_t {
  kTransient,
  kAuraStart,
  kAuraStop,
  kCastStart,
  kCastStop,
  kChannelStart,
  kChannelStop,
};

enum class SpellVisualPresentationPhase : std::uint8_t {
  kEffect,
  kState,
  kStateDone,
};

struct SpellVisualPresentationEffect {
  std::uint32_t effect_name_id{0};
  std::string model_path;
  float resource_scale{1.0f};
  std::int32_t attachment_id{-1};
  std::uint32_t source_field_index{0};
  std::uintptr_t transform_key{0};
  bool world_space{false};
  bool uses_explicit_world_position{false};
  bool from_model_attach{false};
  std::array<float, 3> offset{};
  std::array<float, 3> rotation{};
};

enum class SpellVisualDeferredImpactPolicy : std::uint8_t {
  kGenericKit,
  kDestLocArea,
};

struct SpellMissilePresentationData {
  std::string model_path;
  std::string texture_path;
  std::uint32_t replaceable_texture_type{2u};
  float model_scale{1.0f};
  std::int32_t target_attachment_id{-1};
  bool target_attachment_uses_raw_index{false};
  std::array<float, 3> target_attachment_offset{};
  std::int32_t follow_ground_height_ms{0};
  std::int32_t follow_ground_drop_speed_ms{0};
  std::int32_t follow_ground_approach_ms{0};
  std::uint32_t follow_ground_flags{0u};
  std::uint32_t motion_id{0u};
  std::uint32_t sound_kit_id{0u};
};

struct SpellVisualPresentationEvent {
  ObjectHandle owner;
  std::uint64_t sequence{0};
  SpellVisualLifecycleAction action{SpellVisualLifecycleAction::kTransient};
  SpellVisualPresentationPhase phase{SpellVisualPresentationPhase::kEffect};
  std::uint32_t dispatch_type{0};

  std::uint32_t raw_flags{0};
  std::uint8_t aura_slot{0};
  std::uint32_t spell_id{0};
  std::uint32_t spell_visual_id{0};
  std::uint32_t kit_id{0};

  std::uint32_t sound_kit_id{0};
  std::uint32_t camera_shake_id{0};
  std::array<float, 3> owner_position{};
  std::optional<std::array<float, 3>> world_position;

  std::optional<SpellMissilePresentationData> missile;
  std::array<float, 3> missile_source_position{};
  std::uint64_t missile_caster_guid{0};
  std::uint8_t missile_cast_count{0};
  std::uint64_t missile_target_guid{0};
  std::array<float, 3> missile_target_position{};
  float missile_speed{0.0f};

  bool missile_uses_timed_trajectory{false};
  float missile_trajectory_pitch{0.0f};
  float missile_trajectory_speed{0.0f};
  std::uint32_t missile_trajectory_duration_ms{0};

  std::uint8_t missile_impact_result{0};
  std::uint8_t missile_reflect_result{0};

  std::uint32_t deferred_impact_kit_id{0};
  SpellVisualDeferredImpactPolicy deferred_impact_policy{
      SpellVisualDeferredImpactPolicy::kGenericKit};
  std::uint32_t deferred_impact_raw_flags{0};

  std::uint64_t deferred_impact_owner_guid{0};
  bool m2_events_require_owner_resolution{false};
  std::vector<SpellVisualPresentationEffect> effects;
};

struct SpellMissilePositionCorrection {
  std::uint64_t caster_guid{0};
  std::uint8_t cast_count{0};
  std::array<float, 3> world_position{};
};

struct UnitAnimationCompletionEvent {
  ObjectHandle owner;
  std::uint16_t animation_id{0};
  std::uint64_t request_serial{0};
};

struct ObjectPresentationSnapshot {
  std::uint64_t publication_generation{0};
  ObjectHandle local_player;
  ObjectGuid target;
  bool has_active_targeting_spell{false};
  bool active_spell_allows_flagged_units{false};
  bool active_spell_allows_tapped{false};
  std::vector<ObjectPresentationRecord> active;
  std::vector<ObjectPresentationRecord> retired;
  std::vector<SpellVisualPresentationEvent> spell_visual_events;
  std::vector<SpellMissilePositionCorrection> spell_missile_corrections;
};

}
