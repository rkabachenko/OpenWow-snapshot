#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "openwow/render/m2/m2_system.h"
#include "openwow/render/m2/m2_transparent_draw_order.h"
#include "openwow/render/effects/spell_visuals/spell_visual_effects.h"
#include "openwow/game/object_presentation_snapshot.h"

namespace openwow::data::dbc {
class DbcLoader;
struct SpellVisualEffectNameEntry;
struct SpellVisualKitEntry;
struct SpellVisualEntry;
struct SpellVisualKitModelAttachEntry;
}

namespace openwow::render {

enum class SpellVisualM2EventKind : std::uint8_t {
  kEffect,
  kMissile,
};

struct SpellVisualModelInstance {
  std::uint32_t effect_id = 0;
  std::uint32_t spell_id = 0;
  std::uint32_t spell_visual_id = 0;
  std::uint32_t spell_visual_kit_id = 0;
  std::uint32_t effect_name_id = 0;
  std::uint32_t raw_flags = 0;
  game::SpellVisualPresentationPhase presentation_phase{
      game::SpellVisualPresentationPhase::kEffect};
  std::uint32_t dispatch_type = 0;
  SpellVisualM2EventKind m2_event_kind{SpellVisualM2EventKind::kEffect};
  game::ObjectHandle m2_event_owner{};
  std::uint64_t required_m2_event_owner_guid{0};
  bool m2_events_require_owner_resolution{false};

  std::uint32_t model_id = 0;
  std::uint32_t instance_id = 0;

  std::string model_path;
  float scale = 1.0f;

  float position[3] = {0.0f, 0.0f, 0.0f};
  float offset[3] = {0.0f, 0.0f, 0.0f};
  float rotation[3] = {0.0f, 0.0f, 0.0f};
  std::int32_t attachment_id = -1;
  std::uint64_t parent_guid = 0;
  game::ObjectHandle parent_handle{};
  bool attached = false;
  bool attachment_resolved = false;

  float elapsed = 0.0f;
  float duration = 0.0f;
  float fade_out_duration = 0.3f;
  bool active = true;
  bool visible = true;
  bool fading = false;
};

struct PersistentAreaEffect {
  std::uint32_t effect_id = 0;
  std::uint32_t spell_visual_id = 0;
  std::uint32_t shard_model_id = 0;
  float position[3] = {0.0f, 0.0f, 0.0f};
  float radius = 0.0f;
  float elapsed = 0.0f;
  float duration = 0.0f;
  bool active = true;
  bool child_positions_owned_externally = false;

  std::vector<std::uint32_t> instance_ids;
};

struct ResolvedKitModel {
  std::string model_path;
  float scale = 1.0f;
  std::uint32_t attachment_id = 0;
  float offset[3] = {0.0f, 0.0f, 0.0f};
  float rotation[3] = {0.0f, 0.0f, 0.0f};
  std::uint32_t sound_kit_id = 0;

  bool has_model_attach = false;
};

enum class SpellSoundPlaybackMode : std::uint8_t {
  kUseSoundKit,
  kForceLoop,
  kForceOneShot,
};

struct SpellVisualM2PresentationEvent {
  std::uint32_t effect_id{0};
  std::uint32_t instance_id{0};
  game::ObjectHandle owner{};
  std::uint32_t raw_flags{0};
  SpellVisualM2EventKind kind{SpellVisualM2EventKind::kEffect};
  std::uint64_t required_owner_guid{0};
  bool requires_owner_resolution{false};
  m2::M2TriggeredEvent event;
};

struct SpellVisualDeferredImpactCommand {
  game::ObjectHandle caster{};
  game::ObjectHandle target{};
  std::uint64_t target_guid{0};
  std::uint32_t spell_id{0};
  std::uint32_t spell_visual_id{0};
  std::uint32_t kit_id{0};
  std::array<float, 3> world_position{};
};

class SpellVisualRenderer {
 public:
  SpellVisualRenderer() = default;
  ~SpellVisualRenderer() = default;

  SpellVisualRenderer(const SpellVisualRenderer&) = delete;
  SpellVisualRenderer& operator=(const SpellVisualRenderer&) = delete;

  bool Initialize(m2::M2System* m2_system,
                   const game::ObjectPresentationSnapshot* objects,
                   std::function<std::uint32_t(game::ObjectHandle)>
                       owner_m2_instance_resolver = {});

  void BindDbc(const data::dbc::DbcLoader* dbc);
  void BindSoundKitSink(
      std::function<void(std::uint32_t, const float*)> sink) {
    sound_kit_sink_ = std::move(sink);
  }
  void BindMissileSoundStartSink(
      std::function<std::uint32_t(std::uint32_t, const float*)> sink) {
    missile_sound_start_sink_ = std::move(sink);
  }
  void BindEffectSoundStartSink(
      std::function<std::uint32_t(std::uint32_t, const float*,
                                 SpellSoundPlaybackMode, std::uint64_t)> sink) {
    effect_sound_start_sink_ = std::move(sink);
  }
  void BindSoundPositionSink(
      std::function<void(std::uint32_t, const float*)> sink) {
    sound_position_sink_ = std::move(sink);
  }
  void BindSoundStopSink(std::function<void(std::uint32_t, float)> sink) {
    sound_stop_sink_ = std::move(sink);
  }
  void BindGroundHeightSink(
      std::function<std::optional<float>(float, float, float)> sink) {
    ground_height_sink_ = std::move(sink);
  }
  void BindCameraShakeSink(
      std::function<void(std::uint32_t, const std::array<float, 3>&)> sink) {
    camera_shake_sink_ = std::move(sink);
  }
  void BindM2EventSink(
      std::function<void(const SpellVisualM2PresentationEvent&)> sink) {
    m2_event_sink_ = std::move(sink);
  }
  void BindM2InstanceRetiredSink(
      std::function<void(std::uint32_t)> sink) {
    m2_instance_retired_sink_ = std::move(sink);
  }
  void BindDeferredImpactSink(
      std::function<void(const SpellVisualDeferredImpactCommand&)> sink) {
    deferred_impact_sink_ = std::move(sink);
  }
  void SetWorldBatchUniforms(const m2::M2BatchUniforms& uniforms) {
    world_batch_uniforms_ = uniforms;
  }

  void Shutdown();

  std::uint32_t CreateSpellVisualEffect(
      std::uint32_t spell_visual_id,
      std::uint32_t kit_id,
      std::uint64_t caster_guid,
      std::uint64_t target_guid,
      const float* position,
      VisualPhase phase,
      float duration_override = 0.0f);

  std::uint32_t CreateImpactEffect(
      std::uint32_t spell_visual_id,
      std::uint32_t kit_id,
      std::uint64_t caster_guid,
      std::uint64_t target_guid,
      const float* position);

  std::uint32_t CreatePersistentAreaEffect(
      std::uint32_t spell_visual_id,
      std::uint32_t kit_id,
      const float* position,
      float radius,
      float duration);

  std::uint32_t CreatePersistentAreaModel(
      std::string_view model_path,
      const float* position,
      float radius,
      float duration);

  std::uint32_t CreateAreaModelShard(std::uint32_t effect_id,
                                     std::string_view model_path,
                                     const float* position);
  void DestroyAreaModelShard(std::uint32_t effect_id,
                             std::uint32_t instance_id);
  bool SetAreaModelShardVisible(std::uint32_t instance_id, bool visible);
  bool SetAreaModelShardWorldTransform(std::uint32_t instance_id,
                                       const float* matrix);

  bool SetPersistentAreaEffectPosition(std::uint32_t effect_id,
                                       const float* position);

  std::uint32_t CreateMissileEffect(
      const game::SpellMissilePresentationData& missile,
      std::uint32_t spell_visual_id,
      std::uint32_t spell_id,
      std::uint64_t missile_caster_guid,
      std::uint8_t missile_cast_count,
      std::uint64_t caster_guid,
      std::uint64_t target_guid,
      const float* start_pos,
      const float* end_pos,
      float speed,
      std::uint32_t impact_kit_id = 0,
      std::uint8_t impact_result = 0,
      std::uint8_t reflect_result = 0,
      bool uses_timed_trajectory = false,
      float trajectory_pitch = 0.0f,
      float trajectory_speed = 0.0f,
      std::uint32_t trajectory_duration_ms = 0,
      game::SpellVisualDeferredImpactPolicy deferred_impact_policy =
          game::SpellVisualDeferredImpactPolicy::kGenericKit,
      std::uint32_t deferred_impact_raw_flags = 0,
      std::uint64_t deferred_impact_owner_guid = 0,
      game::ObjectHandle caster_handle = {},
      game::ObjectHandle target_handle = {});

  void DestroyEffect(std::uint32_t effect_id);

  void DestroyEffectsForObject(game::ObjectHandle owner);

  void ConsumePresentationEvent(
      const game::SpellVisualPresentationEvent& event);

  void ApplyMissilePositionCorrection(
      const game::SpellMissilePositionCorrection& correction);

  struct MissileFlight {
    std::uint32_t effect_id = 0;
    std::uint32_t instance_id = 0;
    std::uint64_t missile_caster_guid = 0;
    std::uint8_t missile_cast_count = 0;
    std::uint64_t caster_guid = 0;
    std::uint64_t target_guid = 0;
    game::ObjectHandle caster_handle{};
    game::ObjectHandle target_handle{};
    float start_position[3] = {0.0f, 0.0f, 0.0f};
    float end_position[3] = {0.0f, 0.0f, 0.0f};
    float current_position[3] = {0.0f, 0.0f, 0.0f};
    float speed = 0.0f;
    float base_speed = 0.0f;
    std::array<float, 3> timed_initial_velocity{};
    float timed_gravity = 0.0f;
    float timed_natural_duration = 0.0f;
    float timed_server_duration = 0.0f;
    float timed_time_scale = 1.0f;
    bool uses_timed_trajectory = false;
    float model_scale = 1.0f;
    float elapsed = 0.0f;
    float distance_traveled = 0.0f;
    float initial_distance = 0.0f;
    float follow_ground_height = 0.25f;
    float follow_ground_drop_speed = 0.01f;
    float follow_ground_approach = 0.0f;
    std::uint32_t follow_ground_flags = 0u;
    std::uint32_t missile_flags = 1u;
    std::int32_t target_attachment_id = -1;
    bool target_attachment_uses_raw_index = false;
    std::array<float, 3> target_attachment_offset{};
    std::array<float, 3> target_fallback_offset{};
    std::array<float, 3> random_motion{};
    std::array<float, 3> collision_position{};
    bool has_collision_position = false;
    std::uint8_t impact_result = 0;
    std::uint8_t reflect_result = 0;
    bool reflected = false;
    bool has_advanced = false;
    bool active = true;

    std::uint32_t spell_visual_id = 0;
    std::uint32_t spell_id = 0;
    std::uint32_t motion_id = 0;
    std::uint32_t salvo_index = 0;
    std::uint32_t salvo_count = 1;
    std::uint32_t impact_kit_id = 0;
    game::SpellVisualDeferredImpactPolicy deferred_impact_policy{
        game::SpellVisualDeferredImpactPolicy::kGenericKit};
    std::uint32_t deferred_impact_raw_flags = 0;
    std::uint64_t deferred_impact_owner_guid = 0;
    std::uint32_t sound_handle = 0;
  };

  std::uint32_t RegisterMissileFlight(std::uint32_t effect_id,
                                       std::uint32_t instance_id,
                                       std::uint64_t missile_caster_guid,
                                       std::uint8_t missile_cast_count,
                                       std::uint64_t caster_guid,
                                       std::uint64_t target_guid,
                                       const float* start_pos,
                                       const float* end_pos,
                                       float speed,
                                       std::uint32_t spell_visual_id = 0,
                                       std::uint32_t spell_id = 0,
                                       std::uint32_t motion_id = 0,
                                       std::uint32_t salvo_index = 0,
                                       std::uint32_t salvo_count = 1,
                                       std::uint32_t impact_kit_id = 0,
                                       std::uint8_t impact_result = 0,
                                       std::uint8_t reflect_result = 0,
                                       game::SpellVisualDeferredImpactPolicy
                                           deferred_impact_policy =
                                               game::SpellVisualDeferredImpactPolicy::kGenericKit,
                                       std::uint32_t deferred_impact_raw_flags = 0,
                                       std::uint64_t deferred_impact_owner_guid = 0,
                                       game::ObjectHandle caster_handle = {},
                                       game::ObjectHandle target_handle = {});

  void UpdateMissileFlights(float dt);

  void Update(float dt);

  [[nodiscard]] m2::M2RenderFrameResult Render(
      std::uint16_t view_id, const float* view_matrix,
      m2::M2RenderPassScope pass_scope = m2::M2RenderPassScope::kAll,
      m2::M2TransparentDrawOrder* transparent_draw_order = nullptr);

  void Clear();

  [[nodiscard]] bool ResolveKit(
      std::uint32_t kit_id,
      ResolvedKitModel& out) const;

  [[nodiscard]] bool ResolveEffectName(
      std::uint32_t effect_name_id,
      std::string& out_model_path,
      float& out_scale) const;

  [[nodiscard]] std::size_t active_effect_count() const { return model_instances_.size(); }
  [[nodiscard]] std::size_t active_area_effect_count() const { return area_effects_.size(); }

 private:

  [[nodiscard]] m2::M2ModelInstanceLoadResult CreateModelInstance(
      const ResolvedKitModel& kit_model,
      const float* position);

  [[nodiscard]] m2::M2ResultStatus BindDefaultModelSequence(
      std::uint32_t model_id, std::uint32_t instance_id);

  void DestroyM2Instance(std::uint32_t& instance_id);

  [[nodiscard]] bool BindM2EventCallback(std::uint32_t instance_id);

  void UpdateModelInstancePosition(SpellVisualModelInstance& inst);

  void FadeOutModelInstance(SpellVisualModelInstance& inst);

  void PlaySoundKit(std::uint32_t sound_kit_id, const float* position);
  void StartEffectSound(std::uint32_t effect_id,
                        const game::SpellVisualPresentationEvent& event);
  void StopEffectSound(std::uint32_t effect_id);
  void StopMissileSound(MissileFlight& flight);

  [[nodiscard]] std::uint32_t NextEffectId();

  [[nodiscard]] bool GetObjectPosition(std::uint64_t guid,
                                       float& x, float& y, float& z) const;
  [[nodiscard]] const game::ObjectPresentationRecord* FindObject(
      game::ObjectHandle handle) const;
  [[nodiscard]] game::ObjectHandle ResolveObjectHandle(
      std::uint64_t guid) const;
  [[nodiscard]] bool ResolveMissileTargetPosition(
      const MissileFlight& flight, float* output) const;

  void SpawnKitModels(
      std::uint32_t kit_id,
      std::uint64_t caster_guid,
      std::uint64_t target_guid,
      const float* position,
      VisualPhase phase,
      std::uint32_t effect_id,
      float duration);
  void SpawnPresentationModels(
      const game::SpellVisualPresentationEvent& event,
      std::uint32_t effect_id, float duration);
  void CreateDestLocAreaImpact(std::uint32_t spell_visual_id,
                               std::uint32_t spell_id,
                               std::uint32_t kit_id,
                               std::uint32_t initial_raw_flags,
                               std::uint64_t owner_guid,
                               const float* position);
  void DispatchGenericDeferredImpact(
      game::ObjectHandle caster_handle,
      game::ObjectHandle target_handle,
      std::uint64_t target_guid,
      std::uint32_t spell_id,
      std::uint32_t spell_visual_id,
      std::uint32_t kit_id,
      const float* position);
  [[nodiscard]] bool ApplyOwnerAttachmentTransform(
      SpellVisualModelInstance& inst);

  struct AuraVisualKey {
    game::ObjectHandle owner;
    std::uint8_t slot{0};
    [[nodiscard]] bool operator==(const AuraVisualKey&) const = default;
    struct Hash {
      [[nodiscard]] std::size_t operator()(const AuraVisualKey& value) const
          noexcept {
        return game::ObjectHandle::Hash{}(value.owner) ^
               (static_cast<std::size_t>(value.slot) << 1u);
      }
    };
  };

  struct SpellLifecycleVisualKey {
    game::ObjectHandle owner;
    std::uint32_t spell_id{0};
    std::uint32_t dispatch_type{0};
    std::uint32_t kit_id{0};
    [[nodiscard]] bool operator==(const SpellLifecycleVisualKey&) const = default;
    struct Hash {
      [[nodiscard]] std::size_t operator()(
          const SpellLifecycleVisualKey& value) const noexcept {
        return game::ObjectHandle::Hash{}(value.owner) ^
               (static_cast<std::size_t>(value.spell_id) << 1u) ^
               (static_cast<std::size_t>(value.dispatch_type) << 17u) ^
               (static_cast<std::size_t>(value.kit_id) << 25u);
      }
    };
  };

  struct EffectSound {
    std::uint32_t handle{0};
    game::ObjectHandle owner{};
  };

  m2::M2System* m2_system_ = nullptr;
  const game::ObjectPresentationSnapshot* objects_ = nullptr;
  const data::dbc::DbcLoader* dbc_ = nullptr;
  std::function<void(std::uint32_t, const float*)> sound_kit_sink_;
  std::function<std::uint32_t(std::uint32_t, const float*)>
      missile_sound_start_sink_;
  std::function<std::uint32_t(std::uint32_t, const float*,
                             SpellSoundPlaybackMode, std::uint64_t)>
      effect_sound_start_sink_;
  std::function<void(std::uint32_t, const float*)> sound_position_sink_;
  std::function<void(std::uint32_t, float)> sound_stop_sink_;
  std::function<std::optional<float>(float, float, float)>
      ground_height_sink_;
  std::function<void(std::uint32_t, const std::array<float, 3>&)>
      camera_shake_sink_;
  std::function<void(const SpellVisualM2PresentationEvent&)> m2_event_sink_;
  std::function<void(std::uint32_t)> m2_instance_retired_sink_;
  std::function<void(const SpellVisualDeferredImpactCommand&)>
      deferred_impact_sink_;
  m2::M2BatchUniforms world_batch_uniforms_{};
  std::function<std::uint32_t(game::ObjectHandle)> owner_m2_instance_resolver_;

  std::unordered_map<std::uint32_t, SpellVisualModelInstance> model_instances_;
  std::unordered_map<AuraVisualKey, std::uint32_t, AuraVisualKey::Hash>
      aura_effects_;
  std::unordered_map<SpellLifecycleVisualKey, std::uint32_t,
                     SpellLifecycleVisualKey::Hash>
      channel_effects_;
  std::unordered_map<SpellLifecycleVisualKey, std::uint32_t,
                     SpellLifecycleVisualKey::Hash>
      cast_effects_;
  std::unordered_map<std::uint32_t, PersistentAreaEffect> area_effects_;
  std::unordered_map<std::uint32_t, EffectSound> effect_sounds_;
  std::uint32_t next_effect_id_ = 1;

  std::vector<MissileFlight> missile_flights_;

  std::vector<std::uint32_t> render_batch_ids_scratch_;
  std::vector<std::uint32_t> render_batch_draw_ordinals_scratch_;
  std::vector<m2::M2RenderInstanceResult> render_batch_results_scratch_;

  static constexpr std::size_t kNotBatched = static_cast<std::size_t>(-1);
  struct RenderWalkRow {
    std::size_t batch_index{kNotBatched};
    m2::M2RenderInstanceResult setup_result{};
  };
  std::vector<RenderWalkRow> render_walk_rows_scratch_;

  bool initialized_ = false;
};

}
