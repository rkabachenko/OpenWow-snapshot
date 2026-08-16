#pragma once
namespace openwow::audio { class SoundRuntime; }

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class CGUnit_C;
class ObjectManager;
class WorldSession;

struct MountTransitionObjectHandle {
  std::uintptr_t value{0};

  [[nodiscard]] constexpr bool IsValid() const {
    return value != 0;
  }

  [[nodiscard]] constexpr explicit operator bool() const {
    return IsValid();
  }

  friend constexpr bool operator==(MountTransitionObjectHandle,
                                   MountTransitionObjectHandle) = default;
};

struct LightningObjectHandle {
  std::uintptr_t value{0};

  [[nodiscard]] constexpr bool IsValid() const {
    return value != 0;
  }

  [[nodiscard]] constexpr explicit operator bool() const {
    return IsValid();
  }

  friend constexpr bool operator==(LightningObjectHandle,
                                   LightningObjectHandle) = default;
};

struct TSGrowableArray {
  std::uint32_t capacity;
  std::uint32_t count;
  void*         data;
  std::uint32_t grow_step;

  [[nodiscard]] std::uint32_t ResolveAutoGrowQuantum(std::uint32_t needed);

  void SetCount(std::uint32_t new_count);

  void* Push(const void* element_ptr);
};

struct SpellVisualSphere {
  float x, y, z;
  float radius;
};

struct BoundingBox6 {
  float min_x, min_y, min_z;
  float max_x, max_y, max_z;
};

BoundingBox6 SpellVisual_ComputeBoundingBox(const SpellVisualSphere& sphere);

BoundingBox6 SpellVisual_ComputeMergedBoundingBox(
    const SpellVisualSphere* spheres, std::uint32_t count);

struct SpellCastTiming {
  std::uint32_t start;
  std::uint32_t now;
  std::uint32_t phase;
  std::uint32_t end;
  std::uint32_t post_end;
};

SpellCastTiming ComputeSpellCastTiming(const WorldSession& session,
                                       std::uint32_t start_time,
                                       float phase_fraction,
                                       std::uint32_t spell_rec);

inline constexpr std::uintptr_t kLightningFreeSlotTag = 1u;

struct CLightning {
  std::uint32_t  capacity;
  std::uint32_t  count;
  std::uintptr_t* slots;
  std::uint32_t  growth_step;

  [[nodiscard]] std::uintptr_t ResolveHandle(std::uint32_t handle) const;

  void SetCapacity(std::uint32_t new_capacity);
};

template <typename T>
struct TSCompactGrowableArray {
  std::uint32_t capacity = 0;
  std::uint32_t count    = 0;
  T*            data     = nullptr;

  void SetCapacity(std::uint32_t new_capacity) {
    T* old_data = data;
    capacity = new_capacity;

    auto* new_data = static_cast<T*>(
        std::realloc(old_data, sizeof(T) * new_capacity));

    if (new_data) {
      data = new_data;
      return;
    }

    new_data = static_cast<T*>(std::malloc(sizeof(T) * new_capacity));
    data = new_data;

    if (old_data && new_data) {
      const std::uint32_t copy_count = std::min(count, new_capacity);
      std::memcpy(new_data, old_data, sizeof(T) * copy_count);
      std::free(old_data);
    }
  }

  void SetCount(std::uint32_t new_count) {
    if (new_count == count) {
      return;
    }
    if (new_count == 0) {
      std::free(data);
      capacity = 0;
      count    = 0;
      data     = nullptr;
      return;
    }
    SetCapacity(new_count);
    if (new_count > count && data) {

      std::memset(data + count, 0, sizeof(T) * (new_count - count));
    }
    count = new_count;
  }

  void Release() {
    std::free(data);
    capacity = 0;
    count    = 0;
    data     = nullptr;
  }
};

struct LightningBoltKnot {
  float    position[3];
  std::int32_t segment_id;
  float    reserved_10[2];
  float    velocity[3];
  float    texture_phase;
};

static_assert(sizeof(LightningBoltKnot) == 40,
              "LightningBoltKnot must be exactly 40 bytes (10 DWORDs)");

struct LightningJoint {
  std::uint32_t  reserved_00[10];

  void*          sub_data;
  std::uint32_t  reserved_0C_1[2];

  void*          c3vec_data_a;
  std::uint32_t  reserved_0C_2[2];

  void*          c3vec_data_b;
  std::uint32_t  reserved_0C_3[2];

  void*          c2vec_data;
  std::uint32_t  reserved_0C_4[2];

  void*          imvec_data;
  std::uint32_t  reserved_0C_5[2];

  void*          g_data;
  std::uint32_t  reserved_68[7];

  void*          texture_ref;
};

static_assert(sizeof(void*) != 4 || sizeof(LightningJoint) == 136,
              "LightningJoint must be exactly 136 bytes (34 DWORDs) on 32-bit");
static_assert(sizeof(void*) != 8 || sizeof(LightningJoint) == 168,
              "LightningJoint must be exactly 168 bytes on 64-bit");

void LightningJoint_Destroy(LightningJoint* joint);

struct CLightningEmitter {
  CLightning     handles;
  std::uint32_t  field_10;
  std::uint32_t  field_14;
  void*          aux_data;
  std::uint32_t  field_1C;
};

static_assert(sizeof(void*) != 4 || sizeof(CLightningEmitter) == 32,
              "CLightningEmitter must be exactly 32 bytes (8 DWORDs) on 32-bit");
static_assert(sizeof(void*) != 8 || sizeof(CLightningEmitter) == 48,
              "CLightningEmitter must be exactly 48 bytes on 64-bit");

void CLightningEmitter_Init(CLightningEmitter* emitter);

void CLightningEmitter_Destroy(CLightningEmitter* emitter);

LightningObjectHandle LightningObject_Allocate(std::uint32_t extra_size);

enum class LightningObjectFlags : std::uint32_t {
  kLooping = 0x1u,

  kFixedWorldTarget = 0x2u,

  kUseRawAttachmentIndex = 0x4u,
};

struct LightningObjectStateSnapshot {
  std::uint32_t flags{0};
  std::uint32_t reference_count{0};
  std::uint32_t last_owner_release_tick{0};
  std::uint32_t chain_effect_id{0};
  std::uint32_t maximum_end_tick{0};
  std::uint32_t spell_id{0};
  std::int32_t source_attachment_id{-1};
  std::size_t bolt_count{0};
  bool          active{false};
  bool          cleaned_up{false};
};

struct LightningBoltStateSnapshot {
  std::uint16_t source_index{0};
  std::uint16_t target_index{0};
  std::uint64_t source_guid{0};
  std::uint64_t target_guid{0};
  std::uint32_t begin_tick{0};
  std::uint32_t end_tick{0};
  std::uint32_t renderer_handle{0};
  std::int32_t attachment_slot{-1};
  std::int32_t event_parameter{-1};
  std::array<float, 3> source_position{};
  std::array<float, 3> target_position{};
  bool source_resolved{false};
  bool target_resolved{false};
  bool visible{false};
};

struct SpellVisualChainAppearance {
  float average_segment_length{0.0f};
  float width{0.0f};
  float noise_scale{0.0f};
  float texture_coordinate_scale{0.0f};
  std::uint32_t flags{0};
  std::uint32_t joint_count{0};
  float joint_offset_radius{0.0f};
  std::uint32_t joints_per_minor_joint{0};
  std::uint32_t minor_joints_per_major_joint{0};
  float minor_joint_scale{0.0f};
  float major_joint_scale{0.0f};
  float joint_move_speed{0.0f};
  float joint_smoothness{0.0f};
  float minimum_joint_jump_duration{0.0f};
  float maximum_joint_jump_duration{0.0f};
  float wave_height{0.0f};
  float wave_frequency{0.0f};
  float wave_speed{0.0f};
  float minimum_wave_angle{0.0f};
  float maximum_wave_angle{0.0f};
  float minimum_wave_spin{0.0f};
  float maximum_wave_spin{0.0f};
  float arc_height{0.0f};
  float minimum_arc_angle{0.0f};
  float maximum_arc_angle{0.0f};
  float minimum_arc_spin{0.0f};
  float maximum_arc_spin{0.0f};
  float minimum_flicker_on_duration{0.0f};
  float maximum_flicker_on_duration{0.0f};
  float minimum_flicker_off_duration{0.0f};
  float maximum_flicker_off_duration{0.0f};
  float pulse_speed{0.0f};
  float pulse_on_length{0.0f};
  float pulse_fade_length{0.0f};
  std::uint8_t alpha{0xFFu};
  std::uint8_t red{0xFFu};
  std::uint8_t green{0xFFu};
  std::uint8_t blue{0xFFu};
  std::uint8_t blend_mode{0};
  std::uint32_t render_layer{0};
  float texture_length{0.0f};
  float wave_phase{0.0f};
};

struct SpellVisualChainRenderRequest {
  std::uint32_t chain_effect_id{0};
  std::string_view texture_path{};
  std::array<float, 3> source_position{};
  std::array<float, 3> target_position{};
  SpellVisualChainAppearance appearance{};
};

struct SpellVisualChainRenderCallbacks {
  std::function<std::uint32_t(const SpellVisualChainRenderRequest&)> create;
  std::function<bool(std::uint32_t, const float*, const float*, bool)> update;
  std::function<void(std::uint32_t)> destroy;
};

void SpellVisuals_SetChainRenderCallbacks(
    SpellVisualChainRenderCallbacks callbacks);
void SpellVisuals_ClearChainRenderCallbacks();

void LightningObject_AddReference(LightningObjectHandle obj);
void LightningObject_SetFlag(LightningObjectHandle obj,
                             LightningObjectFlags flag,
                             bool enabled);
[[nodiscard]] bool LightningObject_HasFlag(LightningObjectHandle obj,
                                           LightningObjectFlags flag);
[[nodiscard]] std::optional<LightningObjectStateSnapshot>
LightningObject_GetStateSnapshot(LightningObjectHandle obj);

[[nodiscard]] std::vector<LightningBoltStateSnapshot>
LightningObject_GetBoltStateSnapshots(LightningObjectHandle obj);

void LightningObject_ReleaseRetainedReference(LightningObjectHandle obj);

void LightningObject_InitBolts(LightningObjectHandle obj, std::uintptr_t source,
                                std::uintptr_t target_array,
                                std::uint32_t  bolt_count,
                                bool           single_source,
                                std::int32_t   first_bolt_event_parameter);

bool LightningObject_UpdateBolts(const ObjectManager& objects,
                                 LightningObjectHandle obj,
                                 std::uint32_t now);

void LightningObject_Cleanup(LightningObjectHandle obj);

void LightningObject_FreeAll();

void SpellVisual_CreateLightningEffect(
    std::uint32_t  chain_effect_id,
    std::uintptr_t caster,
    std::uintptr_t spell_c,
    std::uintptr_t target_array,
    std::uint32_t  target_count,
    std::uint32_t  spell_id,
    bool           loop,
    bool           single_source,
    std::uintptr_t out_handles,
    std::uint32_t  max_handles,
    const float*   position_offset,
    std::int32_t   first_bolt_event_parameter);

void SpellVisual_CreateLightningEffectForUnit(
    std::uintptr_t caster,
    std::uint32_t  chain_effect_id,
    std::uintptr_t spell_c,
    std::uintptr_t target_array,
    std::uint32_t  target_count,
    std::uint32_t  spell_id,
    bool           loop,
    bool           single_source,
    std::uintptr_t out_handles,
    std::uint32_t  max_handles,
    const float*   position_offset,
    std::int32_t   first_bolt_event_parameter);

void SpellVisual_GetAttachSourcePosition(float out[3],
                                          std::uintptr_t unit,
                                          std::uint32_t  attach_id,
                                          const float    offset[3],
                                          bool           use_raw_attachment_index);

MountTransitionObjectHandle MountTransitionObject_Allocate(
    std::uint32_t extra_size);

bool MountTransitionObject_InitAnimation(
    const ObjectManager& objects, MountTransitionObjectHandle handle);

void MountTransitionObject_GetTargetPosition(
    const ObjectManager& objects, MountTransitionObjectHandle handle,
    float out[3]);

bool MountTransitionObject_Update(MountTransitionObjectHandle handle);

MountTransitionObjectHandle MountTransitionObject_CreateForUnit(
    const CGUnit_C* unit);

void MountTransitionObject_MarkTransitionComplete(
    MountTransitionObjectHandle handle);

[[nodiscard]] float MountTransitionObject_GetBlendFactor(
    MountTransitionObjectHandle handle);

[[nodiscard]] float MountTransitionObject_GetScale(
    MountTransitionObjectHandle handle);

void MountTransitionObject_GetTransformData(MountTransitionObjectHandle handle,
                                            float out_position[3],
                                            float out_rotation[3],
                                            float *out_facing);

[[nodiscard]] bool MountTransitionObject_IsTransitionComplete(
    MountTransitionObjectHandle handle);

void MountTransitionObject_Release(MountTransitionObjectHandle handle);

void SpellVisualKit_AreaModel_SoundEventCallback(
    openwow::audio::SoundRuntime& sound_runtime,
    std::uint32_t& throttle_counter,
    std::uint32_t model, std::uint32_t bone,
    std::uint32_t fourcc, std::uint32_t data, const float* pos);

struct AreaModelKitObject {

  std::uint32_t link_prev;
  std::uint32_t link_next;

  static constexpr std::size_t kModelPathSize = 260;
  char model_path[kModelPathSize];

  std::uint32_t renderer_effect_id;

  float position[3];

  float radius;

  float accumulated_spawn;

  float rate;

  float reserved[5];

  std::uint32_t shard_list_head;
  std::uint32_t shard_list_next;

  float transform[16];

  std::uint32_t attach_flag;
};

static_assert(offsetof(AreaModelKitObject, model_path) == 0x008,
              "model_path offset must be 0x008");
static_assert(offsetof(AreaModelKitObject, renderer_effect_id) == 0x10C,
              "renderer_effect_id offset must be 0x10C");
static_assert(offsetof(AreaModelKitObject, position) == 0x110,
              "position offset must be 0x110");
static_assert(offsetof(AreaModelKitObject, radius) == 0x11C,
              "radius offset must be 0x11C");
static_assert(offsetof(AreaModelKitObject, accumulated_spawn) == 0x120,
              "accumulated_spawn offset must be 0x120");
static_assert(offsetof(AreaModelKitObject, rate) == 0x124,
              "rate offset must be 0x124");
static_assert(offsetof(AreaModelKitObject, transform) == 0x144,
              "transform offset must be 0x144");
static_assert(offsetof(AreaModelKitObject, attach_flag) == 0x184,
              "attach_flag offset must be 0x184");

void AreaModelKitObject_GetWorldPosition(AreaModelKitObject& obj,
                                          float out_xyz[3]);

void AreaModelKitObject_SetTransformMatrix(AreaModelKitObject& obj,
                                            const float* matrix);

void SpellVisualKit_CreateAreaModel(std::uintptr_t obj,
                                     const float    position[3],
                                     const char*    model_path,
                                     float          radius,
                                     float          rate);

void SpellVisualKit_RenderShards(std::uintptr_t kit, bool visible);

int SpellVisualKit_ShardCallback(std::uintptr_t kit, bool visible);

std::uintptr_t BlizzardObject_Create(const float position[3],
                                      float       radius,
                                      std::int32_t effect_id,
                                      float       rate,
                                      const data::dbc::DbcLoader* dbc);

void SpellVisualKit_AreaModel_Cleanup(std::uintptr_t obj);

[[nodiscard]] bool SpellVisualKit_AreaModel_SetTransformMatrix(
    std::uintptr_t obj, const float* matrix);

bool SpellVisualKit_AreaModel_Update(std::uintptr_t obj,
                                      float delta_seconds);

struct SpellVisualAreaModelRenderRequest {
  std::string_view model_path;
  const float* position{nullptr};
  float radius{0.0f};
  float duration_seconds{0.0f};
};

struct SpellVisualAreaShardRenderRequest {
  std::uint32_t area_effect_id{0};
  std::string_view model_path{};
  std::array<float, 3> world_position{};
};

struct SpellVisualAreaShardSnapshot {
  std::uint64_t shard_id{0};
  std::uint32_t renderer_model_id{0};
  std::array<float, 3> local_position{};
  std::array<float, 3> world_position{};
  std::uint32_t activation_tick{0};
  std::uint32_t retirement_tick{0};
  std::uint32_t collision_probe_count{0};
  std::uint32_t collision_hit_count{0};
  bool animation_started{false};
  bool visible{false};
  bool completed{false};
};

struct SpellVisualAreaModelStateSnapshot {
  float accumulated_spawn{0.0f};
  float rate{0.0f};
  bool visible{false};
  std::vector<SpellVisualAreaShardSnapshot> shards{};
};

struct SpellVisualAreaModelRenderCallbacks {

  std::function<std::uint32_t(const SpellVisualAreaModelRenderRequest&)> create;
  std::function<void(std::uint32_t)> destroy;
  std::function<bool(std::uint32_t, const float*)> set_position;
  std::function<std::uint32_t(const SpellVisualAreaShardRenderRequest&)>
      create_shard;
  std::function<void(std::uint32_t, std::uint32_t)> destroy_shard;
  std::function<bool(std::uint32_t, bool)> set_shard_visible;
  std::function<bool(std::uint32_t, const float*)> set_shard_transform;

  std::function<std::uint32_t(std::uint32_t, std::function<void()>)>
      start_shard_animation;
};

struct SpellVisualAreaModelRuntimeCallbacks {
  std::function<std::uint32_t()> now_ms;
  std::function<float()> random_unit_float;

  std::function<bool(const float*, const float*, std::uint32_t, float*, float*)>
      intersect_segment;
};

void SpellVisuals_SetAreaModelRenderCallbacks(
    SpellVisualAreaModelRenderCallbacks callbacks);
void SpellVisuals_ClearAreaModelRenderCallbacks();
void SpellVisuals_SetAreaModelRuntimeCallbacks(
    SpellVisualAreaModelRuntimeCallbacks callbacks);
void SpellVisuals_ClearAreaModelRuntimeCallbacks();

[[nodiscard]] std::optional<SpellVisualAreaModelStateSnapshot>
SpellVisualKit_GetAreaModelStateSnapshot(std::uintptr_t obj);

struct SpellVisualRibbonStripLayout {
    float current_pos[3];
    float head_pos[3];

    std::uint32_t color_packed;
    std::uint32_t definition_ptr;

    std::uint32_t field_020;

    std::uint32_t segment_count;
    std::uint32_t segment_data_ptr;

    std::uint32_t field_02C[5];

    std::uint32_t texcoord_base;
    std::uint32_t texcoord_count;
    std::uint32_t texcoord_data_ptr;
    std::uint32_t texcoord_field;

    std::uint32_t color_arr_base;
    std::uint32_t color_arr_count;
    std::uint32_t color_arr_data_ptr;
    std::uint32_t color_arr_field;

    std::uint32_t index_arr_base;
    std::uint32_t index_arr_count;

    std::uint32_t field_068[2];

    float interp_accumulator;
    std::uint32_t field_074[2];
    std::uint32_t field_07C;

    float texture_phase;

    std::uint32_t source_handle;
    std::uint32_t target_handle;
    std::uint32_t field_08C;

    std::uint32_t strip_flags;

    float above_width;

    float above_rate;

    float rotation_angle;

    float below_width;

    float below_rate;

    float segment_lifetime;

    float distance_accumulator;

    float random_factor;
};

static_assert(sizeof(SpellVisualRibbonStripLayout) == 180,
              "SpellVisualRibbonStripLayout must be exactly 180 bytes (0xB4)");
static_assert(offsetof(SpellVisualRibbonStripLayout, definition_ptr) == 0x1C,
              "definition_ptr must be at offset 0x1C");
static_assert(offsetof(SpellVisualRibbonStripLayout, strip_flags) == 0x90,
              "strip_flags must be at offset 0x90");
static_assert(offsetof(SpellVisualRibbonStripLayout, above_width) == 0x94,
              "above_width must be at offset 0x94");
static_assert(offsetof(SpellVisualRibbonStripLayout, rotation_angle) == 0x9C,
              "rotation_angle must be at offset 0x9C");
static_assert(offsetof(SpellVisualRibbonStripLayout, segment_lifetime) == 0xA8,
              "segment_lifetime must be at offset 0xA8");
static_assert(offsetof(SpellVisualRibbonStripLayout, distance_accumulator) == 0xAC,
              "distance_accumulator must be at offset 0xAC");
static_assert(offsetof(SpellVisualRibbonStripLayout, random_factor) == 0xB0,
              "random_factor must be at offset 0xB0");

void SpellVisualRibbonRandomizeProperties(SpellVisualRibbonStripLayout& strip);

float SpellVisualRibbonRandomFloatRange(float min_val, float max_val);

void SpellVisualRibbonRandomizeSegmentPositionAndVelocity(
    SpellVisualRibbonStripLayout& strip,
    LightningBoltKnot& knot,
    float distance);

void SpellVisualRibbonUpdateSegments(std::uintptr_t emitter, float dt);

void SpellVisualParticlePool_UpdateAll(TSGrowableArray& pool, float dt);

void SpellVisual_ResetVisibleHumanoidState();

void InitSpellVisuals();

void SpellVisuals_CleanAll();

struct SpellVisualLightingEnvelope {
  std::uint32_t packed_argb{0};
  std::uint32_t start_tick{0};
  std::uint32_t fade_in_end_tick{0};
  std::uint32_t fade_out_start_tick{0};
  std::uint32_t end_tick{0};

  [[nodiscard]] bool IsActive() const noexcept {
    return start_tick != end_tick;
  }
};

void SpellVisuals_BeginLightingEnvelope(const WorldSession& session,
                                        std::uint32_t packed_argb,
                                        float fade_in_fraction,
                                        std::uint32_t spell_id);
void SpellVisuals_ClearLightingEnvelope();
[[nodiscard]] SpellVisualLightingEnvelope
SpellVisuals_GetLightingEnvelopeSnapshot();

void SpellVisuals_UpdateAll(WorldSession& session, float delta_seconds);

void SpellVisuals_Shutdown();

}
