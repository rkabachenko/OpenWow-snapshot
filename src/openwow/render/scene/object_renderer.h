#pragma once

#include "openwow/foundation/compiler/inline_hint.h"
#include "openwow/game/objects/cgdynamicobject.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/m2/m2_resource_streamer.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/m2/m2_transparent_draw_order.h"
#include "openwow/render/models/animation/animation_state.h"
#include "openwow/render/models/characters/character_appearance_geosets.h"
#include "openwow/render/models/characters/character_appearance_texture_baker.h"
#include "openwow/render/models/characters/equipment_renderer.h"
#include "openwow/render/models/characters/mount_renderer.h"
#include "openwow/render/models/display_info_resolver.h"
#include "openwow/render/resources/render_asset_readiness.h"
#include "openwow/render/world/environment/world_model_lighting.h"

#include "openwow/runtime/scheduling/thread_pool_system.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::render {

class BowstringRenderer;
class TextureManager;

struct CreatureRenderStateKey {
  bool resolved{false};
  std::uint32_t model_id{0};
  std::int32_t extra_display_info_id{0};
  std::uint32_t npc_sound_id{0};
};

enum class RenderAssetKind : std::uint8_t {
  kUnknown = 0,
  kM2 = 1,
  kAreaScene = 2,
};

struct GameObjectM2AnimationRenderState {
  bool active{false};
  std::int8_t state_index{-1};
  std::int8_t previous_state_index{-1};

  std::uint16_t animation_id{0};
  bool uses_direct_animation_id{false};
  std::uint32_t direct_animation_id{0};
  bool looping{false};
  bool use_sequence_repeat_count{false};
  float playback_speed{1.0f};
  std::uint32_t sync_serial{0};
  std::uint32_t applied_sync_serial{0};

  std::uint32_t resolved_animation_id{0};
  float resolved_playback_speed{1.0f};

  bool suppress_completion_schedule{false};
};

inline constexpr std::uint32_t kUpperBodyPrimaryKeyBoneSlot = 4u;
inline constexpr std::uint32_t kUpperBodyFallbackKeyBoneSlot = 6u;
inline constexpr std::uint32_t kNoKeyBoneAnimationSlot = 0xFFFFFFFFu;

struct UnitAnimationPresentationRequest {

  std::uint16_t animation_id{AnimId::kStand};
  std::uint16_t resolved_animation_id{AnimId::kStand};
  bool looping{true};
  std::uint64_t serial{0};

  std::uint16_t resolved_base_animation_id{AnimId::kStand};
  bool base_looping{true};
  bool upper_body_only{false};

  bool zero_blend{false};
};

enum class ModelAttachmentRole : std::uint8_t {
  kMainHand,
  kOffHand,
  kRanged,
  kAmmoProjectile,
  kRightShoulder,
  kLeftShoulder,
  kHelm,
  kCape,
  kCorpseLootSparkle,
  kQuestOverlay,
};

[[nodiscard]] constexpr bool IsEquipmentAttachmentRole(
    const ModelAttachmentRole role) noexcept {
  return role <= ModelAttachmentRole::kCape;
}

struct ModelAttachmentSpec {
  ModelAttachmentRole role{ModelAttachmentRole::kMainHand};

  std::shared_ptr<const WeaponAttachmentVisual> visual;
  float scale{1.0f};
  std::uint16_t animation_id{0};
};

struct ItemVisualChildBinding {
  ItemVisualChild desired;
  std::string requested_model_path;
  std::string bound_model_path;
  m2::M2StreamTicket stream_ticket;
  std::uint32_t m2_model_id{0};
  std::uint32_t m2_instance_id{0};
  bool request_failed{false};

  std::uint32_t transparent_draw_ordinal{0};
};

struct ModelAttachmentBinding {
  ModelAttachmentSpec desired;
  std::string requested_model_path;
  std::string bound_model_path;
  std::string bound_texture_path;
  std::uint32_t bound_attachment_id{0};
  m2::M2StreamTicket stream_ticket;
  std::uint32_t m2_model_id{0};
  std::uint32_t m2_instance_id{0};
  bool request_failed{false};
  std::vector<ItemVisualChildBinding> item_visual_children;

  std::uint32_t transparent_draw_ordinal{0};
  std::uint32_t bowstring_draw_ordinal{0};
};

struct DestructibleAreaSceneState {
  std::uint32_t display_id{0};
  std::uint16_t destruction_or_init_doodad_set{0};
  std::uint16_t impact_effect_doodad_set{0};
  std::uint16_t ambient_doodad_set{0};
  std::array<std::uint16_t, 3> additional_doodad_sets{};
};

struct ObjectProjection {
  game::ObjectHandle handle;

  std::uint32_t presentation_slot{game::kNoPresentationSlot};
  game::TypeID type_id{game::TypeID::kObject};

  float position[3]{0.0f, 0.0f, 0.0f};
  float orientation{0.0f};
  float scale{1.0f};
  RenderMatrix4x4 world_transform{kRenderIdentityMatrix4x4};
  bool has_explicit_world_transform{false};

  std::uint32_t display_id{0};
  bool visible{true};
  bool needs_model_load{true};
  bool needs_display_resolve{true};
  float alpha{1.0f};
  RenderVec4 tint_color{1.0f, 1.0f, 1.0f, 1.0f};

  bool is_mounted{false};
  std::uint32_t movement_flags{0};
  float locomotion_speed{0.0f};
  UnitAnimationPresentationRequest unit_animation;

  std::uint16_t corpse_death_animation_id{0};

  GameObjectM2AnimationRenderState game_object_m2_animation;

  std::uint32_t game_object_collision_state{0};
  std::uint32_t art_kit_sync_serial{0};
  std::uint8_t art_kit{0};
  bool art_kit_visuals_initialized{false};
  std::array<std::string, 3> art_kit_texture_paths{};

  std::array<DestructibleAreaSceneState, 4> destructible_area_scene_states{};
  std::array<std::uint16_t, 3> area_scene_additional_doodad_sets{};
  std::uint32_t destructible_rebuild_effect_display_id{0};
  std::uint32_t destructible_rebuild_transition_mode{4};
  std::uint32_t destructible_rebuild_transition_speed{0};
  std::uint32_t destructible_visual_sync_serial{0};
  std::uint8_t destructible_area_scene_active_state{0};
  std::int8_t destructible_area_scene_previous_state{-1};
  bool has_destructible_area_scene_states{false};

  std::uint32_t corpse_visual_sync_serial{0};
  std::uint32_t corpse_render_flags{0};
  std::string corpse_model_path;

  bool corpse_creature_texture_replacement{false};

  game::DynamicObjectVisualState dynamic_object_visual{};
  game::DynamicObjectType dynamic_object_type{game::DynamicObjectType::AreaSpell};
  float dynamic_object_radius{0.0f};
  bool dynamic_object_static_model{false};

  std::shared_ptr<const CharacterAppearanceTextureSources>
      character_appearance_sources;

  std::shared_ptr<const std::string> character_appearance_key;
  CharacterAppearanceGeosetState character_appearance_geosets{};
  std::string character_prebaked_body_texture;
  bool character_appearance_declared{false};
  bool character_appearance_selection_initialized{false};

  std::uint64_t equipment_sync_serial{0};

  std::vector<ModelAttachmentSpec> model_attachments;

  [[nodiscard]] const std::string &CharacterAppearanceKey() const noexcept {
    static const std::string kNoCharacterAppearanceKey;
    return character_appearance_key != nullptr ? *character_appearance_key
                                               : kNoCharacterAppearanceKey;
  }
};

struct RenderInstance {

  struct DestructibleM2StateBinding {
    std::uint32_t display_id{0};
    std::string model_path;
    std::string requested_model_path;
    m2::M2StreamTicket stream_ticket;
    std::uint32_t m2_model_id{0};
    std::uint32_t m2_instance_id{0};
    bool request_failed{false};

    std::uint32_t transparent_draw_ordinal{0};
  };
  struct DestructibleM2StateRuntime {

    float vertical_offset_down{0.0f};
    bool visible{false};
  };

  static constexpr std::size_t kDestructibleRebuildEffectBindingIndex = 4u;
  game::ObjectHandle handle;
  game::ObjectGuid guid;

  std::uint32_t presentation_slot{game::kNoPresentationSlot};
  game::TypeID type_id{game::TypeID::kObject};
  RenderAssetKind render_asset_kind{RenderAssetKind::kUnknown};

  bool visible{true};
  bool needs_model_load{true};
  bool needs_display_resolve{true};
  bool has_explicit_world_transform{false};

  bool is_mounted{false};

  bool visible_submeshes_applied{true};

  std::uint32_t display_id{0};
  std::uint32_t m2_model_id{0};
  std::uint32_t m2_instance_id{0};
  float model_retry_seconds{0.0f};
  m2::M2StreamTicket m2_stream_ticket;

  float position[3]{0.0f, 0.0f, 0.0f};
  float orientation{0.0f};
  float scale{1.0f};
  float alpha{1.0f};
  RenderVec4 tint_color{1.0f, 1.0f, 1.0f, 1.0f};
  std::uint32_t movement_flags{0};

  float locomotion_speed{0.0f};

  float animation_playback_rate{1.0f};
  bool character_appearance_declared{false};
  bool character_appearance_applied{false};
  bool creature_display_overrides_applied{false};

  bool dynamic_object_visual_applied{false};

  bool game_object_m2_animation_callback_installed{false};
  bool has_destructible_area_scene_states{false};
  std::uint8_t destructible_area_scene_active_state{0};
  bool animation_sample_ready{false};

  RenderMatrix4x4 world_transform{kRenderIdentityMatrix4x4};

  AnimationState animation;
  UnitAnimationPresentationRequest unit_animation;
  GameObjectM2AnimationRenderState game_object_m2_animation;

  std::uint64_t animation_sample_frame{0};

  std::uint64_t submitted_draw_count{0};
  std::uint64_t emitted_unit_animation_completion_serial{0};

  std::uint32_t transparent_draw_ordinal{0};

  AnimationState upper_animation;

  std::uint32_t upper_body_animation_slot{kNoKeyBoneAnimationSlot};
  bool upper_body_slot_resolved{false};
  bool upper_body_slot_active{false};
  std::vector<ModelAttachmentBinding> model_attachments;

  std::shared_ptr<const std::string> character_appearance_settled_key;
  std::uint64_t character_appearance_settled_cache_generation{0};
  std::uint64_t character_appearance_settled_eviction_generation{0};

  std::shared_ptr<const std::string> character_appearance_key;

  std::uint64_t equipment_sync_serial{0};

  std::string model_path;
  std::string requested_model_path;

  std::array<std::uint16_t, 3> area_scene_additional_doodad_sets{};
  std::array<DestructibleAreaSceneState, 4> destructible_area_scene_states{};
  std::array<DestructibleM2StateBinding, 5> destructible_m2_state_bindings{};
  std::array<DestructibleM2StateRuntime, 5> destructible_m2_state_runtime{};
  std::int8_t destructible_area_scene_previous_state{-1};
  std::uint32_t destructible_rebuild_effect_display_id{0};
  std::uint32_t destructible_rebuild_transition_mode{4};
  std::uint32_t destructible_rebuild_transition_speed{0};
  std::uint32_t destructible_visual_sync_serial{0};

  CreatureRenderStateKey creature_render_state_key{};
  bool art_kit_visuals_initialized{false};
  std::uint8_t art_kit{0};
  std::uint32_t art_kit_sync_serial{0};
  std::array<std::string, 3> art_kit_texture_paths{};

  std::uint32_t corpse_visual_sync_serial{0};
  std::uint32_t corpse_render_flags{0};
  std::string corpse_model_path;

  bool corpse_creature_texture_replacement{false};

  game::DynamicObjectVisualState dynamic_object_visual{};
  game::DynamicObjectType dynamic_object_type{game::DynamicObjectType::AreaSpell};
  float dynamic_object_radius{0.0f};
  bool dynamic_object_static_model{false};

  std::shared_ptr<const CharacterAppearanceTextureSources>
      character_appearance_sources;

  CharacterAppearanceGeosetState character_appearance_geosets{};

  std::string character_prebaked_body_texture;
  bool character_appearance_selection_initialized{false};
  std::uint8_t applied_hand_pose_mask{0};
  std::uint32_t hand_pose_body_instance_id{0};

  std::uint32_t game_object_collision_state{0};

  [[nodiscard]] const std::string &CharacterAppearanceKey() const noexcept {
    static const std::string kNoCharacterAppearanceKey;
    return character_appearance_key != nullptr ? *character_appearance_key
                                               : kNoCharacterAppearanceKey;
  }
};

struct ObjectRenderPresentationSnapshot {
  std::uint64_t publication_generation{0};
  game::ObjectHandle local_player;
  std::vector<ObjectProjection> active;
  std::vector<game::ObjectHandle> retired;
};

struct ObjectRayIntersection {
  game::ObjectGuid guid;
  RenderVec3 point{0.0f, 0.0f, 0.0f};
  float distance{0.0f};
};

enum class DynamicObjectPresentationEventKind : std::uint8_t {
  kSound,
  kCameraShake,
};

struct DynamicObjectPresentationEvent {
  DynamicObjectPresentationEventKind kind{DynamicObjectPresentationEventKind::kSound};
  std::uint32_t visual_id{0};
  RenderVec3 position{0.0f, 0.0f, 0.0f};
};

enum class GameObjectM2PresentationEventSource : std::uint8_t {
  kPrimaryModel,
  kPerSequenceModel,
};

struct GameObjectM2PresentationEvent {
  game::ObjectHandle owner;
  m2::M2TriggeredEvent event;
  GameObjectM2PresentationEventSource source{
      GameObjectM2PresentationEventSource::kPrimaryModel};
};

struct ModelSpatialQueryResult {
  RenderMatrix4x4 world_transform{kRenderIdentityMatrix4x4};
  std::array<float, 6> local_bounds{};
  std::array<float, 4> local_bounding_sphere{};
  std::array<float, 6> world_bounds{};
  std::array<float, 4> world_bounding_sphere{};
};

struct DestructibleM2StateSpatialQueryResult {
  std::array<float, 6> local_bounds{};
};

struct AreaSceneReadinessState {
  bool runtime_primary_ready{false};
  bool runtime_dependencies_ready{false};
  bool loading_screen_ready{false};
};

struct AreaScenePresentationState {
  struct Model {
    std::string resource_path;
    std::uint16_t destruction_or_init_doodad_set{0};
    std::uint16_t impact_effect_doodad_set{0};
    std::uint16_t ambient_doodad_set{0};
    std::array<std::uint16_t, 3> additional_doodad_sets{};
    std::uint32_t display_id{0};
    std::uint8_t state_index{0};
    bool visible{true};
  };

  std::array<Model, 4> models{};
  std::size_t model_count{0};
  std::string resource_path;
  RenderMatrix4x4 world_transform{kRenderIdentityMatrix4x4};
  std::array<std::uint16_t, 3> additional_doodad_sets{};
  std::uint32_t display_id{0};
  bool visible{true};
  float uniform_scale{1.0f};
  std::uint8_t destructible_active_state{0};
  std::int8_t destructible_previous_state{-1};
  std::uint32_t destructible_rebuild_effect_display_id{0};
  std::string rebuild_effect_resource_path;
  std::uint32_t destructible_rebuild_transition_mode{4};
  std::uint32_t destructible_rebuild_transition_speed{0};
  std::uint32_t destructible_visual_sync_serial{0};
};

inline constexpr std::size_t kObjectRendererInstanceBucketFloor = 2048u;

class ObjectRenderer {
public:
  ObjectRenderer(TextureManager &texture_manager, m2::M2System &m2_system)
      : texture_manager_(texture_manager), m2_system_(m2_system), mount_renderer_(m2_system) {
    instances_.rehash(kObjectRendererInstanceBucketFloor);
    instances_by_guid_.rehash(kObjectRendererInstanceBucketFloor);
  }
  ~ObjectRenderer();

  ObjectRenderer(const ObjectRenderer &) = delete;
  ObjectRenderer &operator=(const ObjectRenderer &) = delete;

  bool Initialize();
  void Shutdown();

  void BindDbc(const openwow::data::dbc::DbcLoader *dbc);

  void BindBowstringRenderer(BowstringRenderer *renderer) {
    bowstring_renderer_ = renderer;
  }

  void
  BindDynamicObjectEventSink(std::function<void(const DynamicObjectPresentationEvent &)> sink) {
    dynamic_object_event_sink_ = std::move(sink);
  }
  void BindUnitAnimationCompletionSink(
      std::function<void(const game::UnitAnimationCompletionEvent &)> sink) {
    unit_animation_completion_sink_ = std::move(sink);
  }
  void BindGameObjectM2EventSink(
      std::function<void(const GameObjectM2PresentationEvent &)> sink) {
    game_object_m2_event_sink_ = std::move(sink);
  }

  void SetFileLoader(std::function<std::vector<std::uint8_t>(const std::string &)> loader);

  [[nodiscard]] EquipmentRenderer &equipment_renderer() {
    return equipment_renderer_;
  }

  [[nodiscard]] MountRenderer &mount_renderer() {
    return mount_renderer_;
  }

  void ConsumePresentation(ObjectRenderPresentationSnapshot &presentation,
                           const game::ObjectPresentationSnapshot &objects);

  void ClearPresentation();

  void Update(float dt);

  struct FrameVisibilityFilters {

    std::function<bool(const game::ObjectPresentationRecord &)> admit;

    std::function<bool(const std::array<float, 4> &world_bounding_sphere,
                       bool has_bounding_sphere)>
        admit_bounding_sphere;
  };

  void PrepareVisibleInstances(
      std::span<const game::ObjectPresentationRecord> active,
      const FrameVisibilityFilters &filters,
      std::vector<std::uint64_t> &out_entity_ids,
      std::vector<std::array<float, 4>> &out_bounding_spheres);

  void Render(std::uint8_t view_id, const float *view_mtx, const float *proj_mtx, float screen_w,
              float screen_h, std::span<const std::uint64_t> entity_ids);

  void RenderTransparent(std::uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                         float screen_w, float screen_h, std::span<const std::uint64_t> entity_ids,
                         m2::M2TransparentDrawOrder &transparent_draw_order);

  void SetCameraPosition(float x, float y, float z) {
    camera_x_ = x;
    camera_y_ = y;
    camera_z_ = z;
    mount_renderer_.SetCameraPosition(x, y, z);
  }

  void SetWorldM2SceneState(const WorldM2SceneState &scene_state) {
    world_m2_scene_state_ = scene_state;
    mount_renderer_.SetWorldM2SceneState(scene_state);
  }

  void RenderMounts(std::uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                    const game::ObjectPresentationSnapshot &objects,
                    m2::M2TransparentDrawOrder &transparent_draw_order);

  [[nodiscard]] DisplayInfoResolver &display_info() {
    return display_info_;
  }
  [[nodiscard]] const DisplayInfoResolver &display_info() const {
    return display_info_;
  }

  [[nodiscard]] std::size_t active_instance_count() const {
    return instances_.size();
  }

  [[nodiscard]] std::size_t loaded_model_count() const {
    return m2_system_.GetLoadedModelCount();
  }

  using AreaSceneReadinessResolver =
      std::function<std::optional<AreaSceneReadinessState>(const RenderInstance &)>;

  [[nodiscard]] bool IsModelReadyForAnimation(game::ObjectGuid guid) const;
  [[nodiscard]] bool IsRuntimeRenderAssetReady(game::ObjectGuid guid) const;

  [[nodiscard]] bool QueryAreaScenePresentationState(
      game::ObjectHandle handle, AreaScenePresentationState *out) const;
  [[nodiscard]] bool QueryDestructibleM2StateSpatial(
      game::ObjectHandle handle, std::uint8_t state_index,
      DestructibleM2StateSpatialQueryResult* out) const;

  void SetDestructibleM2StatePresentation(game::ObjectHandle handle,
                                          std::uint8_t state_index,
                                          bool visible,
                                          float vertical_offset_down);

  void SetDestructibleRebuildEffectPresentation(game::ObjectHandle handle,
                                                bool visible);

  [[nodiscard]] std::uint32_t QueryPrimaryM2InstanceId(game::ObjectHandle handle) const noexcept;

  [[nodiscard]] bool IsCharacterAppearancePrepared(game::ObjectGuid guid) const;

  [[nodiscard]] bool IsLoadingScreenPlayerRenderAssetReady(game::ObjectGuid guid) const;
  [[nodiscard]] bool HasSubmittedVisibleDraw(game::ObjectGuid guid) const;

  [[nodiscard]] bool IsOpaquePassOcclusionCullable(game::ObjectGuid guid);
  [[nodiscard]] bool IsLoadingScreenTransportRenderAssetReady(game::ObjectGuid guid) const;
  [[nodiscard]] bool ModelHasSubmeshId(game::ObjectGuid guid, std::uint16_t submesh_id) const;
  [[nodiscard]] bool ModelHasAttachment(game::ObjectGuid guid,
                                        std::uint32_t attachment_lookup_index) const;
  [[nodiscard]] std::optional<RenderVec3>
  QueryModelAttachmentPosition(game::ObjectGuid guid, std::uint32_t attachment_lookup_index,
                               const std::optional<RenderVec3> &local_offset = std::nullopt) const;
  [[nodiscard]] std::optional<RenderVec3>
  QueryModelAttachmentPosition(game::ObjectHandle handle, std::uint32_t attachment_lookup_index,
                               const std::optional<RenderVec3> &local_offset = std::nullopt) const;
  [[nodiscard]] bool QueryModelAttachmentTransform(game::ObjectGuid guid,
                                                   std::uint32_t attachment_lookup_index,
                                                   float *out_matrix) const;
  [[nodiscard]] bool QueryModelWorldPoint(game::ObjectGuid guid, float *out_position) const;
  [[nodiscard]] bool QueryModelRootBoneWorldMatrix(game::ObjectGuid guid, float *out_matrix) const;
  void SetAreaSceneReadinessResolver(AreaSceneReadinessResolver resolver) {
    area_scene_readiness_resolver_ = std::move(resolver);
  }

  [[nodiscard]] float GetAnimationProgress(game::ObjectGuid guid) const;

  [[nodiscard]] bool QueryModelSpatialState(game::ObjectGuid guid,
                                            ModelSpatialQueryResult *out) const;

  [[nodiscard]] std::optional<ObjectRayIntersection>
  FindClosestSegmentIntersectionForGuid(game::ObjectGuid guid, const RenderVec3 &segment_start,
                                        const RenderVec3 &segment_end) const;

  [[nodiscard]] bool HasAnySegmentIntersection(const RenderVec3 &segment_start,
                                               const RenderVec3 &segment_end) const;

  struct GameObjectCollisionTriangle {
    std::array<RenderVec3, 3> vertices{};
    game::ObjectGuid guid;
    std::uint64_t owner_id{0u};
    std::uint64_t facet_id{0u};
  };
  using GameObjectCollisionTriangleVisitor =
      std::function<void(const GameObjectCollisionTriangle &)>;
  void VisitGameObjectCollisionTriangles(
      const std::array<float, 6> &world_bounds,
      const GameObjectCollisionTriangleVisitor &visitor) const;

  [[nodiscard]] std::optional<std::uint32_t>
  QueryGameObjectAnimationDurationMs(game::ObjectGuid guid) const;

  [[nodiscard]] std::uint64_t GameObjectCollisionRevision() const noexcept {
    return game_object_collision_revision_;
  }

private:
  TextureManager &texture_manager_;

  void ApplyProjection(RenderInstance &instance, ObjectProjection &&projection);

  void InitializeInstance(RenderInstance &instance, ObjectProjection &&projection);
  void RemoveInstance(game::ObjectHandle handle);

  [[nodiscard]] OPENWOW_FORCE_INLINE RenderInstance *
  FindInstance(game::ObjectGuid guid) {
    return const_cast<RenderInstance *>(
        static_cast<const ObjectRenderer *>(this)->FindInstance(guid));
  }

  [[nodiscard]] OPENWOW_FORCE_INLINE const RenderInstance *
  FindInstance(game::ObjectGuid guid) const {
    const auto indexed = instances_by_guid_.find(guid);
    if (indexed != instances_by_guid_.end()) {
      return indexed->second;
    }
    return FindInjectedInstanceByScan(guid);
  }

  [[nodiscard]] const RenderInstance *
  FindInjectedInstanceByScan(game::ObjectGuid guid) const {

    if (instances_by_guid_.size() == instances_.size()) {
      return nullptr;
    }
    const auto scan =
        std::find_if(instances_.begin(), instances_.end(),
                     [guid](const auto &entry) { return entry.second.guid == guid; });
    return scan != instances_.end() ? &scan->second : nullptr;
  }

  enum class CharacterAppearancePhase : std::uint8_t {
    kPreparing,
    kCommit,
    kReady,
    kFailed,
  };

  struct CharacterAppearanceRecord {
    CharacterAppearancePhase phase{CharacterAppearancePhase::kPreparing};
    std::vector<PreparedTextureUpload> pending_uploads;
    std::size_t next_upload{0};
    std::unordered_map<std::uint32_t, std::string> replaceable_paths;
  };

  void ResolveDisplayId(RenderInstance &inst);

  struct GameObjectM2AnimationSelection {
    std::uint32_t animation_id{0};
    float speed{1.0f};

    bool substituted{false};
  };
  [[nodiscard]] GameObjectM2AnimationSelection
  ResolveGameObjectM2AnimationSubstitution(std::uint32_t model_id,
                                           std::uint32_t requested_animation_id,
                                           float requested_speed) const;
  void ApplyGameObjectM2AnimationRequestCallback(RenderInstance &inst);
  void ApplyGameObjectM2AnimationRequest(RenderInstance &inst);
  void ApplyGameObjectM2EventCallback(RenderInstance &inst);
  void ApplyCreatureDisplayOverrides(RenderInstance &inst);
  void ApplyVisibleSubmeshes(RenderInstance &inst);
  void QueueCharacterAppearance(RenderInstance &inst);

  [[nodiscard]] bool HasEvictedCharacterComposite(
      const CharacterAppearanceRecord &record, const std::string &key) const;
  void PumpCharacterAppearanceCompletions();
  void CommitCharacterAppearanceUploads();
  void ApplyCharacterAppearance(RenderInstance &inst);
  void QueueEquipmentTexture(const std::string &path);
  void PumpEquipmentTextureCompletions();
  void CommitEquipmentTextures();
  [[nodiscard]] bool IsEquipmentTextureReady(const std::string &path) const;
  void ApplyDynamicObjectVisualState(RenderInstance &inst);
  void ClearM2Binding(RenderInstance &inst);
  void ReleaseDestructibleM2StateBindings(RenderInstance &inst);
  void SynchronizeInactiveDestructibleM2StateBindings(RenderInstance &inst,
                                                       int &loads_this_frame);
  void StashPrimaryDestructibleM2StateBinding(RenderInstance &inst);
  void RestorePrimaryDestructibleM2StateBinding(RenderInstance &inst);
  [[nodiscard]] RenderMatrix4x4 BuildDestructibleM2StateModelMatrix(
      const RenderInstance& inst, std::uint8_t state_index) const;

  struct PassBatchUniforms {
    const m2::M2BatchUniforms* world_value = nullptr;
    m2::M2SharedBatchUniformsHandle world{};
    m2::M2SharedBatchUniformsHandle character{};
  };
  void SubmitDestructibleM2StateBindings(RenderInstance& inst,
                                         std::uint8_t view_id,
                                         const float* view_mtx,
                                         m2::M2RenderPassScope pass_scope,
                                         const PassBatchUniforms& pass_uniforms);
  void ReleaseModelAttachments(RenderInstance &inst);
  void ReconcileModelAttachments(RenderInstance &inst,
                                 std::vector<ModelAttachmentSpec> projected,
                                 bool rebuild_equipment);
  [[nodiscard]] bool RequestModelForInstance(RenderInstance &inst);
  void PublishStreamedModelForInstance(RenderInstance &inst);

  [[nodiscard]] std::size_t UpdateModelAttachments(RenderInstance &inst,
                                                   std::size_t request_budget);
  void ApplyEquipmentHandPose(RenderInstance &inst);
  void ApplyUpperBodyAnimationChannel(RenderInstance &inst);
  [[nodiscard]] static RenderAssetKind ClassifyRenderAssetPath(const std::string &path);
  [[nodiscard]] bool IsM2RenderReady(const RenderInstance &inst) const;

  [[nodiscard]] bool PrepareM2InstanceQuery(const RenderInstance &inst) const;

  [[nodiscard]] m2::M2InstanceFrameSpatialRequest BuildM2FrameSpatialRequest(
      const RenderInstance &inst, RenderMatrix4x4 *world_transform_storage,
      bool query_spatial) const;

  [[nodiscard]] static m2::M2InstanceFramePresentationRequest
  BuildM2FramePresentationRequest(const RenderInstance &inst);

  [[nodiscard]] bool PrepareM2InstanceSpatialQuery(
      const RenderInstance &inst,
      RenderMatrix4x4 *out_world_transform = nullptr,
      bool *out_pose_installed = nullptr) const;

  [[nodiscard]] bool ResolveFrameAnimationSample(
      m2::M2InstanceFramePrepareScope &prepare, RenderInstance &inst);

  void RenderPass(std::uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                  float screen_w, float screen_h, m2::M2RenderPassScope pass_scope,
                  std::span<const std::uint64_t> entity_ids,
                  m2::M2TransparentDrawOrder *transparent_draw_order);

  void ReserveTransparentDrawOrdinals(m2::M2TransparentDrawOrder &order);

  [[nodiscard]] bool PrepareInstanceBodyForSubmit(m2::M2InstanceFramePrepareScope &prepare,
                                                  RenderInstance &inst,
                                                  const PassBatchUniforms &pass_uniforms);

  [[nodiscard]] bool ApplyBodyRenderResult(RenderInstance &inst,
                                           const m2::M2RenderInstanceResult &result);

  void RenderInstanceAttachments(std::span<RenderInstance *const> owners,
                                 std::uint8_t view_id,
                                 const float *view_mtx, m2::M2RenderPassScope pass_scope,
                                 const PassBatchUniforms &pass_uniforms);

  struct PendingAttachmentPlacement {
    RenderInstance *owner{nullptr};
    ModelAttachmentBinding *binding{nullptr};
  };

  struct PendingAttachmentFrameState {
    RenderInstance *owner{nullptr};
    ModelAttachmentBinding *binding{nullptr};

    RenderMatrix4x4 child_world{kRenderIdentityMatrix4x4};
  };

  DisplayInfoResolver display_info_;
  EquipmentRenderer equipment_renderer_;
  m2::M2System &m2_system_;
  MountRenderer mount_renderer_;

  std::vector<RenderInstance *> batch_bodies_;
  std::vector<std::uint32_t> batch_body_ids_;

  std::vector<std::uint32_t> batch_body_draw_ordinals_;
  std::vector<m2::M2RenderInstanceResult> batch_body_results_;

  std::vector<RenderInstance *> batch_attachment_owners_;

  std::vector<std::size_t> attachment_batch_rows_scratch_;
  std::vector<std::uint32_t> attachment_batch_ids_scratch_;
  std::vector<std::uint32_t> attachment_batch_draw_ordinals_scratch_;
  std::vector<m2::M2RenderInstanceResult> attachment_batch_results_scratch_;

  std::vector<PendingAttachmentPlacement> attachment_placement_targets_scratch_;
  std::vector<m2::M2AttachmentPlacementRequest> attachment_placement_requests_scratch_;
  std::vector<m2::M2AttachmentPlacementQuery> attachment_placement_results_scratch_;

  std::vector<PendingAttachmentFrameState> attachment_frame_work_scratch_;
  std::vector<m2::M2AttachmentFrameRenderRequest> attachment_frame_requests_scratch_;
  std::vector<m2::M2ResultStatus> attachment_frame_statuses_scratch_;

  BowstringRenderer *bowstring_renderer_{nullptr};
  const openwow::data::dbc::DbcLoader *dbc_{nullptr};
  std::function<void(const DynamicObjectPresentationEvent &)> dynamic_object_event_sink_;
  std::function<void(const game::UnitAnimationCompletionEvent &)> unit_animation_completion_sink_;
  std::function<void(const GameObjectM2PresentationEvent &)> game_object_m2_event_sink_;
  bool initialized_{false};

  std::unordered_map<game::ObjectHandle, RenderInstance, game::ObjectHandle::Hash> instances_;

  std::vector<RenderInstance *> consume_instances_scratch_;

  struct InstanceSlot {
    game::ObjectHandle handle;
    RenderInstance *instance{nullptr};
  };
  std::vector<InstanceSlot> instances_by_slot_;
  std::vector<const game::ObjectPresentationRecord *> prepare_admitted_scratch_;
  std::vector<RenderInstance *> prepare_instances_scratch_;

  std::vector<std::uint32_t> attachment_animation_batch_;

  std::vector<RenderInstance *> render_pass_instances_scratch_;

  std::uint64_t animation_sample_frame_{0};

  std::uint64_t game_object_collision_revision_{0};

  std::unordered_map<game::ObjectGuid, RenderInstance *, game::ObjectGuid::Hash>
      instances_by_guid_;

  game::ObjectHandle priority_instance_{};

  AreaSceneReadinessResolver area_scene_readiness_resolver_;

  std::function<std::vector<std::uint8_t>(const std::string &)> file_loader_;

  struct CharacterAppearanceCompletion {
    std::string key;
    PreparedCharacterAppearanceTextures prepared;
  };

  struct CharacterAppearanceMailbox {
    std::mutex mutex;
    std::deque<CharacterAppearanceCompletion> completions;
  };

  struct EquipmentTextureRecord {
    CharacterAppearancePhase phase{CharacterAppearancePhase::kPreparing};
    PreparedTextureUpload prepared;
  };

  struct EquipmentTextureCompletion {
    std::string path;
    PreparedTextureUpload prepared;
  };

  struct EquipmentTextureMailbox {
    std::mutex mutex;
    std::deque<EquipmentTextureCompletion> completions;
  };

  std::unordered_map<std::string, CharacterAppearanceRecord> character_appearance_cache_;

  std::uint64_t character_appearance_cache_generation_{0};
  openwow::core::ThreadPoolSystem character_appearance_workers_;
  std::shared_ptr<CharacterAppearanceMailbox> character_appearance_mailbox_;
  std::unordered_map<std::string, EquipmentTextureRecord> equipment_texture_cache_;
  std::shared_ptr<EquipmentTextureMailbox> equipment_texture_mailbox_;
  static constexpr std::uint32_t kCharacterAppearanceWorkerCount = 2u;
  static constexpr std::uint32_t kMaxCharacterAppearanceCommitsPerFrame = 4u;
  static constexpr std::uint32_t kMaxEquipmentTextureCommitsPerFrame = 4u;

  float camera_x_{0.0f};
  float camera_y_{0.0f};
  float camera_z_{0.0f};
  WorldM2SceneState world_m2_scene_state_{};

  static constexpr int kMaxLoadsPerFrame = 4;
};

}
