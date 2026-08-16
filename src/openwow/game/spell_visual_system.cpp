
#include "openwow/game/spell_visual_system.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/world_session.h"

#include "openwow/core/display_settings.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/foundation/hashing/retail_adler_seed.h"
#include "openwow/core/storm_ref_counted.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/async_file_read.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/ceffect_c.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::game {

namespace {

CLightningEmitter* g_lightning_emitter = nullptr;

constexpr std::uintptr_t kMountTransitionFlagsOffset = 120u;
constexpr std::uint32_t kMountTransitionInitializedFlag = 0x1u;
constexpr std::uint32_t kMountTransitionCompleteFlag = 0x2u;
constexpr std::size_t kMountTransitionObjectHeaderSize = 136u;
constexpr std::uint32_t kMountTransitionAnimationId = 0x7Fu;
constexpr std::uint32_t kMountTransitionStartEvent = 0x42545324u;
constexpr std::uint32_t kMountTransitionEndEvent = 0x45545324u;
constexpr std::uint32_t kMaxLightningObjectHandles = 12u;
constexpr const char* kSpellEffectLevelCVarName = "spellEffectLevel";
constexpr const char* kDefaultAreaModelPath = "Spells\\Blizzard_Impact_Base.mdx";
constexpr float kSpellEffectLevelMaxRetailValue = 9.0f;
constexpr float kSpellEffectLevelScaleRange = 0.9f;
constexpr float kSpellEffectLevelMinScale = 0.1f;
constexpr float kAreaModelUpdateDeltaClampSeconds = 1.0f / 15.0f;
constexpr float kAreaModelProbeTopOffset = 16.666666f;
constexpr float kAreaModelProbeCenterOffset = 1.0f / 6.0f;
constexpr float kAreaModelProbeDropDistance = 33.333332f;
constexpr float kAreaModelObstructionBackoff = 0.95f;
constexpr std::uint32_t kAreaModelCollisionMask = 0x00120111u;
constexpr std::uint32_t kFallbackShardAnimationLifetimeMs = 1000u;
constexpr float kDefaultAttachSourceHeightScale = 0.75f;
constexpr std::uint32_t kLightningTargetAttachmentIndex = 0x0Cu;
constexpr std::uint32_t kLightningCasterSourceEvent = 0x4C534324u;

SpellVisualAreaModelRenderCallbacks g_area_model_render_callbacks{};
SpellVisualAreaModelRuntimeCallbacks g_area_model_runtime_callbacks{};
SpellVisualChainRenderCallbacks g_chain_render_callbacks{};
openwow::foundation::hashing::AdlerSeedState g_area_model_prng_state{};
SpellVisualLightingEnvelope g_lighting_envelope{};

[[nodiscard]] float ScaleAreaModelRateBySpellEffectLevel(const float rate) {
  const auto snapshot =
      openwow::ui::game::CVarSystem::Instance().LookupCVarByName(
          kSpellEffectLevelCVarName);
  if (!snapshot.has_value()) {
    return rate;
  }

  const float level = static_cast<float>(snapshot->current_int_value);
  const float scale =
      (level / kSpellEffectLevelMaxRetailValue) * kSpellEffectLevelScaleRange +
      kSpellEffectLevelMinScale;
  return rate * scale;
}

[[nodiscard]] bool QueryUnitM2ModelWorldPointAndAttachment(
    const CGUnit_C& unit,
    const std::uint32_t attachment_lookup_index,
    float* const out_model_world_point,
    float* const out_attachment_position) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0u) {
    return false;
  }

  auto* const system = unit.m2_system();
  if (system == nullptr) {
    return false;
  }
  const auto model_query = system->QueryModelWorldPoint(instance_id);
  const auto attachment_query =
      system->QueryAttachmentPosition(instance_id, attachment_lookup_index);
  if (model_query.status != openwow::render::m2::M2ResultStatus::kReady ||
      attachment_query.status != openwow::render::m2::M2ResultStatus::kReady ||
      out_model_world_point == nullptr || out_attachment_position == nullptr) {
    return false;
  }

  for (std::size_t axis = 0; axis < 3u; ++axis) {
    out_model_world_point[axis] = model_query.position[axis];
    out_attachment_position[axis] = attachment_query.position[axis];
  }
  return true;
}

class MountTransitionObjectRegistry {
 public:
  static MountTransitionObjectRegistry& Get() {
    static MountTransitionObjectRegistry instance;
    return instance;
  }

  MountTransitionObjectHandle Allocate(const std::uint32_t extra_size) {
    const auto size = kMountTransitionObjectHeaderSize + extra_size;
    auto bytes = std::make_unique<std::byte[]>(size);
    std::memset(bytes.get(), 0, size);

    MountTransitionObjectHandle handle{
        reinterpret_cast<std::uintptr_t>(bytes.get())};
    records_.emplace(handle.value,
                     OwnedRecord{.bytes = std::move(bytes), .size = size});
    return handle;
  }

  void Release(const MountTransitionObjectHandle handle) {
    records_.erase(handle.value);
  }

  [[nodiscard]] bool Owns(const MountTransitionObjectHandle handle) const {
    return records_.find(handle.value) != records_.end();
  }

 private:
  struct OwnedRecord {
    std::unique_ptr<std::byte[]> bytes{};
    std::size_t size{0};
  };

  std::unordered_map<std::uintptr_t, OwnedRecord> records_{};
};

struct TimedLightningBolt {
  std::uint16_t source_index{0};
  std::uint16_t target_index{0};
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

struct LightningObjectRecord {
  std::uint32_t flags{0};
  std::uint32_t reference_count{1};
  std::uint32_t last_owner_release_tick{0};
  std::uint32_t chain_effect_id{0};
  std::uint32_t maximum_end_tick{0};
  std::uint32_t segment_duration_ms{300u};
  std::uint32_t segment_delay_ms{0u};
  std::uint32_t delay_between_effects_ms{0u};
  std::uint64_t source_guid{0};
  CEffect_C* retained_owner{nullptr};
  std::uint32_t spell_id{0};
  std::int32_t source_attachment_id{-1};
  SpellVisualChainAppearance appearance{};
  std::array<float, 3> source_offset{};
  std::array<float, 3> target_offset{};
  std::array<float, 3> fixed_target_position{};
  std::string texture_path{};
  std::vector<std::uint64_t> point_guids{};
  std::vector<TimedLightningBolt> bolts{};
  bool fixed_target{false};
  bool          active{true};
  bool          cleaned_up{false};
};

class LightningObjectRegistry {
 public:
  static LightningObjectRegistry& Get() {
    static LightningObjectRegistry instance;
    return instance;
  }

  LightningObjectHandle Allocate([[maybe_unused]] const std::uint32_t extra_size) {
    LightningObjectHandle handle{next_handle_++};
    records_.emplace(handle.value, LightningObjectRecord{});
    return handle;
  }

  LightningObjectRecord* Find(const LightningObjectHandle handle) {
    const auto it = records_.find(handle.value);
    if (it == records_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  const LightningObjectRecord* Find(const LightningObjectHandle handle) const {
    const auto it = records_.find(handle.value);
    if (it == records_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  [[nodiscard]] std::optional<LightningObjectStateSnapshot> FindRetired(
      const LightningObjectHandle handle) const {
    const auto it = retired_.find(handle.value);
    return it != retired_.end()
               ? std::optional<LightningObjectStateSnapshot>{it->second}
               : std::nullopt;
  }

  std::uint32_t NowMs() const {
    if (g_area_model_runtime_callbacks.now_ms) {
      return g_area_model_runtime_callbacks.now_ms();
    }
    return openwow::core::GameClock::GetTickCount32();
  }

  std::vector<LightningObjectHandle> SnapshotHandles() const {
    std::vector<LightningObjectHandle> result;
    result.reserve(records_.size());
    for (const auto& [handle, record] : records_) {
      if (!record.cleaned_up) {
        result.push_back(LightningObjectHandle{handle});
      }
    }
    std::sort(result.begin(), result.end(),
              [](const auto lhs, const auto rhs) {
                return lhs.value < rhs.value;
              });
    return result;
  }

  void Reset() {
    records_.clear();
    retired_.clear();
    retired_order_.clear();
    next_handle_ = 1u;
  }

  void Retire(const LightningObjectHandle handle) {
    const auto it = records_.find(handle.value);
    if (it == records_.end()) {
      return;
    }
    const auto& record = it->second;
    retired_[handle.value] = LightningObjectStateSnapshot{
        .flags = record.flags,
        .reference_count = record.reference_count,
        .last_owner_release_tick = record.last_owner_release_tick,
        .chain_effect_id = record.chain_effect_id,
        .maximum_end_tick = record.maximum_end_tick,
        .spell_id = record.spell_id,
        .source_attachment_id = record.source_attachment_id,
        .bolt_count = record.bolts.size(),
        .active = record.active,
        .cleaned_up = record.cleaned_up,
    };
    retired_order_.push_back(handle.value);
    records_.erase(it);

    constexpr std::size_t kMaximumDiagnosticTombstones = 128u;
    while (retired_order_.size() > kMaximumDiagnosticTombstones) {
      retired_.erase(retired_order_.front());
      retired_order_.pop_front();
    }
  }

 private:
  std::uintptr_t next_handle_{1};
  std::unordered_map<std::uintptr_t, LightningObjectRecord> records_{};
  std::unordered_map<std::uintptr_t, LightningObjectStateSnapshot> retired_{};
  std::deque<std::uintptr_t> retired_order_{};
};

constexpr std::uint32_t ToFlagMask(const LightningObjectFlags flag) {
  return static_cast<std::uint32_t>(flag);
}

class AreaModelKitObjectRegistry {
 public:
  struct ShardRecord {
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

  struct OwnedRecord {
    std::unique_ptr<AreaModelKitObject> object{};
    std::vector<ShardRecord> shards{};
    std::uint64_t next_shard_id{1};
    bool visible{true};
  };

  static AreaModelKitObjectRegistry& Get() {
    static AreaModelKitObjectRegistry instance;
    return instance;
  }

  std::uintptr_t Allocate() {
    auto object = std::make_unique<AreaModelKitObject>();
    std::memset(object.get(), 0, sizeof(AreaModelKitObject));
    const std::uintptr_t handle =
        reinterpret_cast<std::uintptr_t>(object.get());
    OwnedRecord record{};
    record.object = std::move(object);
    live_objects_.emplace(handle, std::move(record));
    return handle;
  }

  AreaModelKitObject* Find(const std::uintptr_t handle) const {
    const auto it = live_objects_.find(handle);
    return it != live_objects_.end() ? it->second.object.get() : nullptr;
  }

  OwnedRecord* FindOwned(const std::uintptr_t handle) {
    const auto it = live_objects_.find(handle);
    return it != live_objects_.end() ? &it->second : nullptr;
  }

  const OwnedRecord* FindOwned(const std::uintptr_t handle) const {
    const auto it = live_objects_.find(handle);
    return it != live_objects_.end() ? &it->second : nullptr;
  }

  void Release(const std::uintptr_t handle) {
    live_objects_.erase(handle);
  }

  std::vector<std::uintptr_t> SnapshotHandles() const {
    std::vector<std::uintptr_t> handles;
    handles.reserve(live_objects_.size());
    for (const auto& [handle, _] : live_objects_) {
      handles.push_back(handle);
    }
    std::sort(handles.begin(), handles.end());
    return handles;
  }

  void CleanAndReleaseAll() {
    const auto snapshot = SnapshotHandles();
    for (const auto handle : snapshot) {
      SpellVisualKit_AreaModel_Cleanup(handle);
    }
    live_objects_.clear();
  }

  [[nodiscard]] bool Empty() const { return live_objects_.empty(); }

 private:
  std::unordered_map<std::uintptr_t, OwnedRecord> live_objects_{};
};

[[nodiscard]] std::uint32_t SpellVisualNowMs() {
  if (g_area_model_runtime_callbacks.now_ms) {
    return g_area_model_runtime_callbacks.now_ms();
  }
  return openwow::core::GameClock::GetTickCount32();
}

[[nodiscard]] float SpellVisualRandomUnitFloat() {
  if (g_area_model_runtime_callbacks.random_unit_float) {
    const float sample = g_area_model_runtime_callbacks.random_unit_float();
    return std::isfinite(sample)
               ? std::clamp(sample, 0.0f, std::nextafter(1.0f, 0.0f))
               : 0.0f;
  }

  return openwow::foundation::hashing::AdlerSeedNextUnitFloat(
      g_area_model_prng_state);
}

[[nodiscard]] std::uint32_t RoundAreaModelDelayByte(const float sample) {

  const float scaled = sample * 255.0f;
  const float lower_float = std::floor(scaled);
  const auto lower = static_cast<std::uint32_t>(lower_float);
  const float fraction = scaled - lower_float;
  if (fraction > 0.5f || (fraction == 0.5f && (lower & 1u) != 0u)) {
    return lower + 1u;
  }
  return lower;
}

[[nodiscard]] std::uint32_t TruncateNonNegativeDelay(const float value) {

  if (value <= 0.0f) {
    return 0u;
  }

  if (std::isnan(value)) {
    return 0x80000000u;
  }
  constexpr double kUint32ExclusiveUpperBound = 4294967296.0;
  if (static_cast<double>(value) >= kUint32ExclusiveUpperBound) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(value);
}

template <typename Field>
[[nodiscard]] Field ReadRawSpellVisualField(
    const data::dbc::SpellVisualEntry& visual, const std::size_t field_index) {
  static_assert(sizeof(Field) == sizeof(std::uint32_t));
  static_assert(sizeof(data::dbc::SpellVisualEntry) ==
                32u * sizeof(std::uint32_t));
  Field result{};
  const auto* const bytes = reinterpret_cast<const std::byte*>(&visual);
  std::memcpy(&result, bytes + field_index * sizeof(std::uint32_t),
              sizeof(result));
  return result;
}

struct ResolvedLightningSpellVisual {
  std::int32_t source_attachment_id{-1};
  std::array<float, 3> source_offset{};
  std::array<float, 3> target_offset{};
  bool use_raw_attachment_index{false};
};

[[nodiscard]] ResolvedLightningSpellVisual ResolveLightningSpellVisual(
    const data::dbc::DbcLoader& dbc, const std::uint32_t spell_id) {
  ResolvedLightningSpellVisual result{};
  const auto* const spell = dbc.spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return result;
  }

  std::uint32_t visual_id = spell->spell_visual[0];
  const auto quality = static_cast<std::int32_t>(
      core::DisplaySettingsController::Instance().GetQualityLevel());
  if (quality < 2 && spell->spell_visual[1] != 0u) {
    visual_id = spell->spell_visual[1];
  }
  const auto* const visual = dbc.spell_visual().LookupEntry(visual_id);
  if (visual == nullptr) {
    return result;
  }

  result.source_attachment_id =
      ReadRawSpellVisualField<std::int32_t>(*visual, 16u);
  result.source_offset = {
      ReadRawSpellVisualField<float>(*visual, 26u),
      -ReadRawSpellVisualField<float>(*visual, 27u),
      ReadRawSpellVisualField<float>(*visual, 28u),
  };
  result.target_offset = {
      ReadRawSpellVisualField<float>(*visual, 29u),
      -ReadRawSpellVisualField<float>(*visual, 30u),
      ReadRawSpellVisualField<float>(*visual, 31u),
  };
  result.use_raw_attachment_index =
      (ReadRawSpellVisualField<std::uint32_t>(*visual, 13u) & 0x200u) != 0u;
  return result;
}

[[nodiscard]] bool TraceAreaModelSegment(
    const std::array<float, 3>& start, const std::array<float, 3>& end,
    std::array<float, 3>& hit_point, float& fraction) {
  if (g_area_model_runtime_callbacks.intersect_segment) {
    return g_area_model_runtime_callbacks.intersect_segment(
        start.data(), end.data(), kAreaModelCollisionMask, hit_point.data(),
        &fraction);
  }

  return false;
}

void BuildShardWorldTransform(const std::array<float, 3>& position,
                              float out_matrix[16]) {
  openwow::math::row_major_mat4x4::SetIdentity(out_matrix);
  out_matrix[12] = position[0];
  out_matrix[13] = position[1];
  out_matrix[14] = position[2];
}

void RefreshShardWorldPosition(
    const AreaModelKitObject& area,
    AreaModelKitObjectRegistry::ShardRecord& shard) {
  if (area.attach_flag == 0u) {
    shard.world_position = shard.local_position;
    return;
  }
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      shard.world_position.data(), shard.local_position.data(), area.transform);
}

void DestroyAreaModelShard(
    const AreaModelKitObject& area,
    AreaModelKitObjectRegistry::ShardRecord& shard);

void MarkAreaModelShardComplete(const std::uintptr_t area_handle,
                                const std::uint64_t shard_id) {
  auto* const owned =
      AreaModelKitObjectRegistry::Get().FindOwned(area_handle);
  if (owned == nullptr) {
    return;
  }
  const auto it = std::find_if(
      owned->shards.begin(), owned->shards.end(),
      [shard_id](const auto& shard) { return shard.shard_id == shard_id; });
  if (it != owned->shards.end()) {

    DestroyAreaModelShard(*owned->object, *it);
  }
}

void DestroyAreaModelShard(
    const AreaModelKitObject& area,
    AreaModelKitObjectRegistry::ShardRecord& shard) {
  if (shard.renderer_model_id != 0u &&
      g_area_model_render_callbacks.destroy_shard) {
    g_area_model_render_callbacks.destroy_shard(area.renderer_effect_id,
                                                 shard.renderer_model_id);
  }
  shard.renderer_model_id = 0u;
  shard.completed = true;
}

[[nodiscard]] std::uint32_t DecodeSpellChainComboId(
    const std::uint32_t encoded) {
  return (encoded & 0x00FEFEFEu) |
         (encoded & 0x02000000u) >> 18u |
         (encoded & 0x04000000u) >> 11u |
         (encoded & 0x08000000u) >> 4u |
         (encoded & 0x10000000u) >> 28u |
         (encoded & 0x20000000u) >> 21u |
         (encoded & 0x40000000u) >> 14u;
}

[[nodiscard]] std::uint32_t DecodeSpellChainComboAt(
    const std::string_view combo, const std::size_t slot) {
  constexpr std::size_t kEncodedWidth = sizeof(std::uint32_t);
  constexpr std::size_t kMaximumSlots = 12u;
  if (slot >= kMaximumSlots || slot * kEncodedWidth >= combo.size()) {
    return 0u;
  }
  const std::size_t offset = slot * kEncodedWidth;
  std::uint32_t encoded = 0u;
  for (std::size_t byte = 0;
       byte < kEncodedWidth && offset + byte < combo.size(); ++byte) {
    encoded |= static_cast<std::uint32_t>(
                   static_cast<std::uint8_t>(combo[offset + byte]))
               << (byte * 8u);
  }
  return DecodeSpellChainComboId(encoded);
}

[[nodiscard]] std::uint64_t SourceGuidFromObject(
    const std::uintptr_t source) {
  if (source == 0u) {
    return 0u;
  }
  return reinterpret_cast<const CGUnit_C*>(source)->GetGuid().GetRawValue();
}

[[nodiscard]] bool HasNonZeroOffset(const std::array<float, 3>& offset) {
  return offset[0] != 0.0f || offset[1] != 0.0f || offset[2] != 0.0f;
}

void RotateOffsetByUnitModelMatrix(const CGUnit_C& unit,
                                   const std::array<float, 3>& offset,
                                   std::array<float, 3>& out) {
  float model_to_world[16]{};
  unit.Presentation().ModelToWorldMatrix(model_to_world);
  std::array<float, 3> transformed_offset{};
  std::array<float, 3> transformed_origin{};
  constexpr std::array<float, 3> kOrigin{};
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      transformed_offset.data(), offset.data(), model_to_world);
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      transformed_origin.data(), kOrigin.data(), model_to_world);
  for (std::size_t axis = 0; axis < out.size(); ++axis) {
    out[axis] = transformed_offset[axis] - transformed_origin[axis];
  }
}

[[nodiscard]] bool ResolveCasterSourceEventPosition(
    const CGUnit_C& unit, const std::array<float, 3>& offset,
    std::array<float, 3>& out) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0u) {
    return false;
  }

  auto* const m2 = unit.m2_system();
  if (m2 == nullptr) {
    return false;
  }
  const auto animation = m2->QueryInstanceAnimationInfo(instance_id);
  if (animation.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }
  const auto event = m2->QueryInstanceEvent(
      instance_id, animation.info.resolved_animation_id,
      kLightningCasterSourceEvent);
  if (event.status != openwow::render::m2::M2ResultStatus::kReady ||
      !event.has_event) {
    return false;
  }

  out = event.event.world_position;
  if (HasNonZeroOffset(offset)) {
    std::array<float, 3> rotated_offset{};
    RotateOffsetByUnitModelMatrix(unit, offset, rotated_offset);
    for (std::size_t axis = 0; axis < out.size(); ++axis) {
      out[axis] += rotated_offset[axis];
    }
  }
  return true;
}

void ResolveLightningTargetUnitPosition(
    const CGUnit_C& unit, const std::array<float, 3>& target_offset,
    std::array<float, 3>& out) {

  if (unit.Presentation().HasModelAttachmentPoint(kLightningTargetAttachmentIndex, false)) {
    const openwow::render::RenderVec3 mapped_offset{
        target_offset[0], target_offset[1], target_offset[2]};
    if (unit.Presentation().GetMappedAttachmentPosition(
            out.data(), kLightningTargetAttachmentIndex, mapped_offset,
            false)) {
      return;
    }
  }

  const auto position = unit.GetPosition();
  out = {position.x, position.y,
         position.z +
             unit.Presentation().ModelHeight() * kDefaultAttachSourceHeightScale};
  if (HasNonZeroOffset(target_offset)) {
    std::array<float, 3> rotated_offset{};
    RotateOffsetByUnitModelMatrix(unit, target_offset, rotated_offset);
    for (std::size_t axis = 0; axis < out.size(); ++axis) {
      out[axis] += rotated_offset[axis];
    }
  }
}

[[nodiscard]] bool ResolveLightningEndpoint(
    const ObjectManager& objects, const LightningObjectRecord& record,
    const std::uint16_t point_index,
    const bool source_endpoint, const std::int32_t attachment_slot,
    std::array<float, 3>& out) {
  out = {};
  if (!source_endpoint && record.fixed_target && point_index != 0u) {
    out = record.fixed_target_position;
    return true;
  }
  if (point_index >= record.point_guids.size()) {
    return false;
  }
  const std::uint64_t guid = record.point_guids[point_index];
  const auto* const object = CGObject_HasFlags(objects, guid, kTypeMaskObject);
  if (object == nullptr) {
    return false;
  }

  const bool caster_endpoint = source_endpoint && point_index == 0u;
  const auto& offset =
      caster_endpoint ? record.source_offset : record.target_offset;
  if (const auto* const unit = dynamic_cast<const CGUnit_C*>(object);
      unit != nullptr) {
    if (!caster_endpoint) {
      ResolveLightningTargetUnitPosition(*unit, offset, out);
      return true;
    }
    const bool use_raw_attachment_index =
        (record.flags &
         ToFlagMask(LightningObjectFlags::kUseRawAttachmentIndex)) != 0u;
    SpellVisual_GetAttachSourcePosition(
        out.data(), reinterpret_cast<std::uintptr_t>(unit),
        caster_endpoint && attachment_slot >= 0
            ? static_cast<std::uint32_t>(attachment_slot)
            : std::numeric_limits<std::uint32_t>::max(),
        offset.data(), use_raw_attachment_index);
    return true;
  }

  float world_matrix[16]{};
  object->GetWorldMatrix(world_matrix);
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      out.data(), offset.data(), world_matrix);
  return true;
}

void SpawnAreaModelShard(
    AreaModelKitObjectRegistry::OwnedRecord& owned) {
  AreaModelKitObject& area = *owned.object;
  std::array<float, 3> center{};
  AreaModelKitObject_GetWorldPosition(area, center.data());

  const float radial_distance = SpellVisualRandomUnitFloat() * area.radius;
  const float angle =
      SpellVisualRandomUnitFloat() * 6.28318530717958647692f;
  const std::array<float, 3> probe_start{
      center[0], center[1], center[2] + kAreaModelProbeCenterOffset};
  const std::array<float, 3> candidate{
      center[0] + std::cos(angle) * radial_distance,
      center[1] + std::sin(angle) * radial_distance,
      center[2] + kAreaModelProbeTopOffset};

  std::array<float, 3> first_hit{};
  float first_fraction = 1.0f;
  const bool obstructed = TraceAreaModelSegment(
      probe_start, candidate, first_hit, first_fraction);
  std::array<float, 3> downward_start = candidate;
  std::uint32_t hit_count = 0u;
  if (obstructed) {
    ++hit_count;
    const float backed_off_fraction =
        first_fraction * kAreaModelObstructionBackoff;
    for (std::size_t axis = 0; axis < downward_start.size(); ++axis) {
      downward_start[axis] =
          probe_start[axis] +
          (candidate[axis] - probe_start[axis]) * backed_off_fraction;
    }
  }

  std::array<float, 3> downward_end = downward_start;
  downward_end[2] -= kAreaModelProbeDropDistance;
  std::array<float, 3> ground_hit{};
  float ground_fraction = 1.0f;
  const bool found_ground = TraceAreaModelSegment(
      downward_start, downward_end, ground_hit, ground_fraction);
  if (found_ground) {
    ++hit_count;
  }

  AreaModelKitObjectRegistry::ShardRecord shard{};
  shard.shard_id = owned.next_shard_id++;
  shard.collision_probe_count = 2u;
  shard.collision_hit_count = hit_count;
  if (!found_ground) {
    shard.local_position = {area.position[0], area.position[1],
                            area.position[2]};
  } else if (area.attach_flag == 0u) {
    shard.local_position = ground_hit;
  } else {
    float inverse[16]{};
    openwow::math::row_major_mat4x4::BuildInverseRigidTransform4x4(
        inverse, area.transform);
    openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
        shard.local_position.data(), ground_hit.data(), inverse);
  }
  RefreshShardWorldPosition(area, shard);

  if (g_area_model_render_callbacks.create_shard) {
    SpellVisualAreaShardRenderRequest request{};
    request.area_effect_id = area.renderer_effect_id;
    request.model_path = area.model_path;
    request.world_position = shard.world_position;
    shard.renderer_model_id =
        g_area_model_render_callbacks.create_shard(request);
  }

  const auto random_delay_byte =
      RoundAreaModelDelayByte(SpellVisualRandomUnitFloat()) & 0xFFu;
  shard.activation_tick =
      SpellVisualNowMs() + random_delay_byte * 2u;
  if (shard.activation_tick == 0u) {
    shard.activation_tick = 1u;
  }
  owned.shards.push_back(std::move(shard));
}

[[nodiscard]] bool IsTickInsideHalfOpenRange(const std::uint32_t now,
                                             const std::uint32_t begin,
                                             const std::uint32_t end) {
  return begin <= now && now < end;
}

[[nodiscard]] bool RetailSignedTickDeltaIsNonNegative(
    const std::uint32_t candidate, const std::uint32_t current) {

  return ((candidate - current) & 0x80000000u) == 0u;
}

}

std::uint32_t TSGrowableArray::ResolveAutoGrowQuantum(std::uint32_t needed) {
  if (needed >= 64) {
    grow_step = 64;
    return 64;
  }

  std::uint32_t result = needed;
  for (std::uint32_t i = needed & (needed - 1); i != 0; i &= (i - 1))
    result = i;

  return result != 0 ? result : 1;
}

void TSGrowableArray::SetCount(std::uint32_t new_count) {

  auto* old_data = static_cast<std::uint32_t*>(data);
  auto* new_data = static_cast<std::uint32_t*>(
      std::realloc(old_data, sizeof(std::uint32_t) * new_count));

  if (new_data) {
    data = new_data;
    capacity = new_count;
    return;
  }

  new_data = new std::uint32_t[new_count]();
  if (old_data) {
    std::uint32_t copy_count = std::min(new_count, count);
    std::memcpy(new_data, old_data, copy_count * sizeof(std::uint32_t));
    std::free(old_data);
  }
  data = new_data;
  capacity = new_count;
}

void* TSGrowableArray::Push(const void* element_ptr) {
  std::uint32_t needed = count + 1;

  if (needed > capacity) {
    std::uint32_t step = grow_step;
    if (step == 0) step = ResolveAutoGrowQuantum(needed);
    if (needed % step) needed += step - (needed % step);
    SetCount(needed);
  }

  auto* arr = static_cast<std::uint32_t*>(data);
  auto* slot = arr ? &arr[count] : nullptr;
  count += 1;

  if (!slot) return nullptr;

  *slot = *static_cast<const std::uint32_t*>(element_ptr);
  return slot;
}

BoundingBox6 SpellVisual_ComputeBoundingBox(const SpellVisualSphere& s) {
  return {
    s.x - s.radius, s.y - s.radius, s.z - s.radius,
    s.x + s.radius, s.y + s.radius, s.z + s.radius,
  };
}

BoundingBox6 SpellVisual_ComputeMergedBoundingBox(
    const SpellVisualSphere* spheres, std::uint32_t count) {
  if (count == 0) {
    return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  }

  BoundingBox6 result = SpellVisual_ComputeBoundingBox(spheres[0]);

  for (std::uint32_t i = 1; i < count; ++i) {
    const float cx = spheres[i].x;
    const float cy = spheres[i].y;
    const float cz = spheres[i].z;
    const float r  = spheres[i].radius;

    const float s_max_x = cx + r;
    const float s_max_y = cy + r;
    const float s_max_z = cz + r;
    const float s_min_x = cx - r;
    const float s_min_y = cy - r;
    const float s_min_z = cz - r;

    if (s_max_x > result.max_x) result.max_x = s_max_x;
    if (s_max_y > result.max_y) result.max_y = s_max_y;
    if (s_max_z > result.max_z) result.max_z = s_max_z;
    if (s_min_x < result.min_x) result.min_x = s_min_x;
    if (s_min_y < result.min_y) result.min_y = s_min_y;
    if (s_min_z < result.min_z) result.min_z = s_min_z;
  }

  return result;
}

void AreaModelKitObject_GetWorldPosition(AreaModelKitObject& obj,
                                          float out_xyz[3]) {
  if (obj.attach_flag) {
    openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4Unbuffered(
        out_xyz, obj.position, obj.transform);
  } else {
    out_xyz[0] = obj.position[0];
    out_xyz[1] = obj.position[1];
    out_xyz[2] = obj.position[2];
  }
}

void AreaModelKitObject_SetTransformMatrix(AreaModelKitObject& obj,
                                            const float* matrix) {
  obj.attach_flag = (matrix != nullptr) ? 1u : 0u;
  if (!matrix) {
    return;
  }

  openwow::math::row_major_mat4x4::Copy4x4(obj.transform, matrix);

  float local_matrix[16];
  openwow::math::row_major_mat4x4::SetIdentity(local_matrix);

  float world_pos[3];
  AreaModelKitObject_GetWorldPosition(obj, world_pos);

  local_matrix[12] = world_pos[0];
  local_matrix[13] = world_pos[1];
  local_matrix[14] = world_pos[2];

  SpellVisualSphere sphere{};
  sphere.x      = 0.0f;
  sphere.y      = 0.0f;
  sphere.z      = 0.0f;
  sphere.radius = obj.radius;

  [[maybe_unused]] BoundingBox6 bbox =
      SpellVisual_ComputeBoundingBox(sphere);

  if (obj.renderer_effect_id != 0u &&
      g_area_model_render_callbacks.set_position) {
    (void)g_area_model_render_callbacks.set_position(obj.renderer_effect_id,
                                                     world_pos);
  }

  const auto handle = reinterpret_cast<std::uintptr_t>(&obj);
  if (auto* const owned =
          AreaModelKitObjectRegistry::Get().FindOwned(handle);
      owned != nullptr) {
    for (auto& shard : owned->shards) {
      if (shard.completed) {
        continue;
      }
      RefreshShardWorldPosition(obj, shard);
      BuildShardWorldTransform(shard.world_position, local_matrix);
      if (shard.renderer_model_id != 0u &&
          g_area_model_render_callbacks.set_shard_transform) {
        (void)g_area_model_render_callbacks.set_shard_transform(
            shard.renderer_model_id, local_matrix);
      }
    }
  }
}

SpellCastTiming ComputeSpellCastTiming(const WorldSession& session,
                                       std::uint32_t start_time,
                                       float phase_fraction,
                                       std::uint32_t spell_rec) {
  SpellCastTiming t{};

  const std::int32_t duration =
      ComputeCastDuration(session, spell_rec, false,
                          false, false);

  t.start    = start_time;
  t.now      = core::GameClock::GetTickCount32();
  const auto udur = duration > 0
                        ? static_cast<std::uint32_t>(duration)
                        : 0u;
  const double phase_offset = static_cast<double>(udur) * phase_fraction;
  const auto phase_ms = !std::isfinite(phase_offset) || phase_offset <= 0.0
                            ? (phase_offset > 0.0
                                   ? std::numeric_limits<std::uint32_t>::max()
                                   : 0u)
                            : static_cast<std::uint32_t>(std::min(
                                  phase_offset,
                                  static_cast<double>(
                                      std::numeric_limits<std::uint32_t>::max())));
  t.end      = t.now + udur;
  t.phase    = t.now + phase_ms;
  t.post_end = t.end + 100;

  return t;
}

std::uintptr_t CLightning::ResolveHandle(const std::uint32_t handle) const {
  if (handle == 0xFFFF'FFFFu) {
    return 0;
  }
  if (handle >= count) {
    return 0;
  }
  const std::uintptr_t slot = slots[handle];
  if ((slot & kLightningFreeSlotTag) != 0u) {
    return 0;
  }
  return slot;
}

void CLightning::SetCapacity(const std::uint32_t new_capacity) {
  auto* old_slots = slots;
  const std::uint32_t old_capacity = capacity;
  capacity = new_capacity;

  auto* new_slots = static_cast<std::uintptr_t*>(
      std::realloc(old_slots, sizeof(std::uintptr_t) * new_capacity));

  if (new_slots || new_capacity == 0u) {
    slots = new_slots;
    return;
  }

  new_slots = static_cast<std::uintptr_t*>(
      std::malloc(sizeof(std::uintptr_t) * new_capacity));
  if (new_slots == nullptr) {
    capacity = old_capacity;
    slots = old_slots;
    return;
  }

  if (old_slots != nullptr) {
    const std::uint32_t copy_count = std::min(new_capacity, count);
    for (std::uint32_t i = 0; i < copy_count; ++i) {
      new_slots[i] = old_slots[i];
    }
    std::free(old_slots);
  }
  slots = new_slots;
}

void LightningJoint_Destroy(LightningJoint* joint) {
  if (joint->texture_ref) {
    openwow::core::StormRefCounted_Release(joint->texture_ref);
  }
  if (joint->g_data) {
    std::free(joint->g_data);
  }
  if (joint->imvec_data) {
    std::free(joint->imvec_data);
  }
  if (joint->c2vec_data) {
    std::free(joint->c2vec_data);
  }
  if (joint->c3vec_data_b) {
    std::free(joint->c3vec_data_b);
  }
  if (joint->c3vec_data_a) {
    std::free(joint->c3vec_data_a);
  }
  if (joint->sub_data) {
    std::free(joint->sub_data);
  }
}

void CLightningEmitter_Init(CLightningEmitter* emitter) {
  std::memset(emitter, 0, sizeof(CLightningEmitter));
}

void CLightningEmitter_Destroy(CLightningEmitter* emitter) {

  if (emitter->handles.slots != nullptr) {
    for (std::uint32_t i = emitter->handles.count; i > 0; --i) {
      const auto raw = emitter->handles.slots[i - 1];
      auto* joint = reinterpret_cast<LightningJoint*>(raw & ~kLightningFreeSlotTag);
      if (joint) {
        LightningJoint_Destroy(joint);
        std::free(joint);
      }
    }
  }

  if (emitter->aux_data) {
    std::free(emitter->aux_data);
  }

  if (emitter->handles.slots) {
    std::free(emitter->handles.slots);
  }
}

LightningObjectHandle LightningObject_Allocate(const std::uint32_t extra_size) {
  return LightningObjectRegistry::Get().Allocate(extra_size);
}

void LightningObject_AddReference(const LightningObjectHandle obj) {
  if (auto* const record = LightningObjectRegistry::Get().Find(obj);
      record != nullptr && record->active) {
    ++record->reference_count;
  }
}

void LightningObject_SetFlag(const LightningObjectHandle obj,
                             const LightningObjectFlags flag,
                             const bool enabled) {
  if (auto* const record = LightningObjectRegistry::Get().Find(obj);
      record != nullptr) {
    if (enabled) {
      record->flags |= ToFlagMask(flag);
    } else {
      record->flags &= ~ToFlagMask(flag);
    }
  }
}

bool LightningObject_HasFlag(const LightningObjectHandle obj,
                             const LightningObjectFlags flag) {
  const auto& registry = LightningObjectRegistry::Get();
  if (const auto* const record = registry.Find(obj);
      record != nullptr) {
    return (record->flags & ToFlagMask(flag)) != 0u;
  }
  const auto retired = registry.FindRetired(obj);
  return retired.has_value() &&
         (retired->flags & ToFlagMask(flag)) != 0u;
}

std::optional<LightningObjectStateSnapshot> LightningObject_GetStateSnapshot(
    const LightningObjectHandle obj) {
  const auto* const record = LightningObjectRegistry::Get().Find(obj);
  if (record == nullptr) {
    return LightningObjectRegistry::Get().FindRetired(obj);
  }

  LightningObjectStateSnapshot snapshot;
  snapshot.flags = record->flags;
  snapshot.reference_count = record->reference_count;
  snapshot.last_owner_release_tick = record->last_owner_release_tick;
  snapshot.chain_effect_id = record->chain_effect_id;
  snapshot.maximum_end_tick = record->maximum_end_tick;
  snapshot.spell_id = record->spell_id;
  snapshot.source_attachment_id = record->source_attachment_id;
  snapshot.bolt_count = record->bolts.size();
  snapshot.active = record->active;
  snapshot.cleaned_up = record->cleaned_up;
  return snapshot;
}

std::vector<LightningBoltStateSnapshot>
LightningObject_GetBoltStateSnapshots(const LightningObjectHandle obj) {
  const auto* const record = LightningObjectRegistry::Get().Find(obj);
  if (record == nullptr) {
    return {};
  }
  std::vector<LightningBoltStateSnapshot> result;
  result.reserve(record->bolts.size());
  for (const auto& bolt : record->bolts) {
    result.push_back(LightningBoltStateSnapshot{
        .source_index = bolt.source_index,
        .target_index = bolt.target_index,
        .source_guid =
            bolt.source_index < record->point_guids.size()
                ? record->point_guids[bolt.source_index]
                : 0u,
        .target_guid =
            bolt.target_index < record->point_guids.size()
                ? record->point_guids[bolt.target_index]
                : 0u,
        .begin_tick = bolt.begin_tick,
        .end_tick = bolt.end_tick,
        .renderer_handle = bolt.renderer_handle,
        .attachment_slot = bolt.attachment_slot,
        .event_parameter = bolt.event_parameter,
        .source_position = bolt.source_position,
        .target_position = bolt.target_position,
        .source_resolved = bolt.source_resolved,
        .target_resolved = bolt.target_resolved,
        .visible = bolt.visible,
    });
  }
  return result;
}

void LightningObject_ReleaseRetainedReference(const LightningObjectHandle obj) {
  auto& registry = LightningObjectRegistry::Get();
  auto* const record = registry.Find(obj);
  if (record == nullptr || !record->active) {
    return;
  }

  record->flags &= ~ToFlagMask(LightningObjectFlags::kLooping);
  record->last_owner_release_tick = registry.NowMs();

  if (record->reference_count == 0) {
    return;
  }

  --record->reference_count;
  if (record->reference_count == 0) {
    LightningObject_Cleanup(obj);
  }
}

void LightningObject_InitBolts(LightningObjectHandle obj,
                                std::uintptr_t source,
                                std::uintptr_t target_array,
                                std::uint32_t  bolt_count,
                                bool           single_source,
                                std::int32_t   first_bolt_event_parameter) {
  auto* record = LightningObjectRegistry::Get().Find(obj);
  if (record == nullptr || bolt_count == 0u || target_array == 0u) {
    return;
  }

  const auto* const targets =
      reinterpret_cast<const std::uint64_t*>(target_array);
  record->source_guid = SourceGuidFromObject(source);
  record->point_guids.assign(static_cast<std::size_t>(bolt_count) + 1u, 0u);
  record->point_guids[0] = record->source_guid;

  for (std::uint32_t index = 0; index < bolt_count; ++index) {
    record->point_guids[index + 1u] = targets[bolt_count - index - 1u];
  }

  const std::uint32_t now = SpellVisualNowMs();
  std::uint32_t begin = now + record->segment_delay_ms;
  record->maximum_end_tick = begin;
  for (auto& bolt : record->bolts) {
    if (bolt.renderer_handle != 0u && g_chain_render_callbacks.destroy) {
      g_chain_render_callbacks.destroy(bolt.renderer_handle);
    }
  }
  record->bolts.clear();
  record->bolts.reserve(bolt_count);
  for (std::uint32_t index = 0; index < bolt_count; ++index) {
    TimedLightningBolt bolt{};
    bolt.source_index = single_source ? 0u : static_cast<std::uint16_t>(index);
    bolt.target_index = static_cast<std::uint16_t>(index + 1u);
    bolt.begin_tick = begin;
    bolt.end_tick = begin + record->segment_duration_ms;
    bolt.attachment_slot = index == 0u ? record->source_attachment_id : -1;
    bolt.event_parameter =
        index == 0u ? first_bolt_event_parameter : -1;
    if (RetailSignedTickDeltaIsNonNegative(bolt.end_tick,
                                           record->maximum_end_tick)) {
      record->maximum_end_tick = bolt.end_tick;
    }
    record->bolts.push_back(bolt);
    begin += record->delay_between_effects_ms;
  }

  record->last_owner_release_tick = now;
  record->active = true;
  record->cleaned_up = false;
}

bool LightningObject_UpdateBolts(const ObjectManager& objects,
                                  LightningObjectHandle obj,
                                  std::uint32_t now) {
  auto* const record = LightningObjectRegistry::Get().Find(obj);
  if (record == nullptr || !record->active || record->cleaned_up) {
    return false;
  }

  const bool looping =
      (record->flags & ToFlagMask(LightningObjectFlags::kLooping)) != 0u;
  for (auto& bolt : record->bolts) {
    const bool inside_window =
        looping || IsTickInsideHalfOpenRange(now, bolt.begin_tick, bolt.end_tick);
    if (inside_window) {
      bolt.source_resolved = ResolveLightningEndpoint(
          objects, *record, bolt.source_index, true, bolt.attachment_slot,
          bolt.source_position);
      bolt.target_resolved = ResolveLightningEndpoint(
          objects, *record, bolt.target_index, false, -1, bolt.target_position);
      bolt.visible = bolt.source_resolved && bolt.target_resolved;

      if (bolt.renderer_handle == 0u && g_chain_render_callbacks.create) {
        SpellVisualChainRenderRequest request{};
        request.chain_effect_id = record->chain_effect_id;
        request.texture_path = record->texture_path;
        request.source_position = bolt.source_position;
        request.target_position = bolt.target_position;
        request.appearance = record->appearance;
        bolt.renderer_handle = g_chain_render_callbacks.create(request);
      }
      if (bolt.renderer_handle != 0u && g_chain_render_callbacks.update &&
          !g_chain_render_callbacks.update(
              bolt.renderer_handle, bolt.source_position.data(),
              bolt.target_position.data(), bolt.visible)) {

        bolt.renderer_handle = 0u;
      }
    }

    if (!looping && bolt.end_tick <= now && bolt.renderer_handle != 0u) {
      if (g_chain_render_callbacks.destroy) {
        g_chain_render_callbacks.destroy(bolt.renderer_handle);
      }
      bolt.renderer_handle = 0u;
      bolt.visible = false;
    }
  }

  if (looping || now < record->maximum_end_tick) {
    return true;
  }
  record->active = false;
  return false;
}

void LightningObject_Cleanup(const LightningObjectHandle obj) {
  auto& registry = LightningObjectRegistry::Get();
  if (auto* const record = registry.Find(obj);
      record != nullptr) {
    for (auto& bolt : record->bolts) {
      if (bolt.renderer_handle != 0u && g_chain_render_callbacks.destroy) {
        g_chain_render_callbacks.destroy(bolt.renderer_handle);
      }
      bolt.renderer_handle = 0u;
      bolt.visible = false;
    }
    record->bolts.clear();
    record->point_guids.clear();
    record->texture_path.clear();
    if (record->retained_owner != nullptr) {
      record->retained_owner->ReleaseOwnerReference();
      record->retained_owner = nullptr;
    }
    record->flags = 0u;
    record->reference_count = 0u;
    record->active = false;
    record->cleaned_up = true;
    registry.Retire(obj);
  }
}

void LightningObject_FreeAll() {
  auto& registry = LightningObjectRegistry::Get();
  for (const auto handle : registry.SnapshotHandles()) {
    LightningObject_Cleanup(handle);
  }
}

void SpellVisuals_SetChainRenderCallbacks(
    SpellVisualChainRenderCallbacks callbacks) {
  g_chain_render_callbacks = std::move(callbacks);
}

void SpellVisuals_ClearChainRenderCallbacks() {
  g_chain_render_callbacks = {};
}

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
    std::int32_t   first_bolt_event_parameter) {
  if (caster == 0u || spell_id == 0u ||
      (target_count == 0u && position_offset == nullptr) ||
      (loop && (out_handles == 0u || max_handles == 0u))) {
    return;
  }

  const auto* const caster_unit = reinterpret_cast<const CGUnit_C*>(caster);
  const auto* const dbc_loader =
      caster_unit != nullptr ? caster_unit->dbc_loader() : nullptr;
  if (dbc_loader == nullptr) return;
  const auto& dbc = *dbc_loader;

  const auto& definitions = dbc.spell_chain_effects();
  const data::dbc::SpellChainEffectsEntry* const primary_definition =
      definitions.LookupEntry(chain_effect_id);
  if (primary_definition == nullptr) {
    return;
  }
  const data::dbc::SpellChainEffectsEntry* definition = primary_definition;
  const ResolvedLightningSpellVisual spell_visual =
      ResolveLightningSpellVisual(dbc, spell_id);

  std::array<std::uint64_t, 1> fixed_target_guid{0u};
  const auto* targets = reinterpret_cast<const std::uint64_t*>(target_array);
  std::uint32_t effective_target_count = target_count;
  bool fixed_target = false;
  std::array<float, 3> fixed_position{};
  if (effective_target_count == 0u) {
    targets = fixed_target_guid.data();
    effective_target_count = 1u;
    fixed_target = true;
    auto* const source = reinterpret_cast<const CGUnit_C*>(caster);
    float source_matrix[16]{};
    source->Presentation().ModelToWorldMatrix(source_matrix);
    openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
        fixed_position.data(), position_offset, source_matrix);
  }

  auto* const output = reinterpret_cast<std::uintptr_t*>(out_handles);
  std::uint32_t output_count = 0u;
  std::size_t combo_slot = 0u;
  while (definition != nullptr && combo_slot < kMaxLightningObjectHandles) {
    const LightningObjectHandle handle = LightningObject_Allocate(0u);
    auto* const record = LightningObjectRegistry::Get().Find(handle);
    if (record == nullptr) {
      break;
    }
    record->chain_effect_id = definition->id;
    record->appearance = SpellVisualChainAppearance{
        .average_segment_length = definition->avg_seg_len,
        .width = definition->width,
        .noise_scale = definition->noise_scale,
        .texture_coordinate_scale = definition->tex_coord_scale,
        .flags = definition->flags,
        .joint_count = definition->joint_count,
        .joint_offset_radius = definition->joint_offset_radius,
        .joints_per_minor_joint = definition->joints_per_minor_joint,
        .minor_joints_per_major_joint =
            definition->minor_joints_per_major_joint,
        .minor_joint_scale = definition->minor_joint_scale,
        .major_joint_scale = definition->major_joint_scale,
        .joint_move_speed = definition->joint_move_speed,
        .joint_smoothness = definition->joint_smoothness,
        .minimum_joint_jump_duration =
            definition->min_duration_between_joint_jumps,
        .maximum_joint_jump_duration =
            definition->max_duration_between_joint_jumps,
        .wave_height = definition->wave_height,
        .wave_frequency = definition->wave_freq,
        .wave_speed = definition->wave_speed,
        .minimum_wave_angle = definition->min_wave_angle,
        .maximum_wave_angle = definition->max_wave_angle,
        .minimum_wave_spin = definition->min_wave_spin,
        .maximum_wave_spin = definition->max_wave_spin,
        .arc_height = definition->arc_height,
        .minimum_arc_angle = definition->min_arc_angle,
        .maximum_arc_angle = definition->max_arc_angle,
        .minimum_arc_spin = definition->min_arc_spin,
        .maximum_arc_spin = definition->max_arc_spin,
        .minimum_flicker_on_duration = definition->min_flicker_on_duration,
        .maximum_flicker_on_duration = definition->max_flicker_on_duration,
        .minimum_flicker_off_duration = definition->min_flicker_off_duration,
        .maximum_flicker_off_duration = definition->max_flicker_off_duration,
        .pulse_speed = definition->pulse_speed,
        .pulse_on_length = definition->pulse_on_length,
        .pulse_fade_length = definition->pulse_fade_length,
        .alpha = definition->alpha,
        .red = definition->red,
        .green = definition->green,
        .blue = definition->blue,
        .blend_mode = definition->blend_mode,
        .render_layer = definition->render_layer,
        .texture_length = definition->texture_length,
        .wave_phase = definition->wave_phase,
    };
    record->segment_duration_ms = definition->seg_duration;
    record->segment_delay_ms = definition->seg_delay;
    record->delay_between_effects_ms =
        TruncateNonNegativeDelay(definition->delay_between_effects);
    record->texture_path.assign(definition->texture);
    record->spell_id = spell_id;
    record->source_attachment_id = spell_visual.source_attachment_id;
    record->source_offset = spell_visual.source_offset;
    record->target_offset = spell_visual.target_offset;
    if (!loop && spell_c != 0u) {
      record->retained_owner = reinterpret_cast<CEffect_C*>(spell_c);
      record->retained_owner->IncrementOwnerRefCount();
    }
    record->fixed_target = fixed_target;
    record->fixed_target_position = fixed_position;
    if (loop) {
      record->flags |= ToFlagMask(LightningObjectFlags::kLooping);
    }
    if (fixed_target) {
      record->flags |=
          ToFlagMask(LightningObjectFlags::kFixedWorldTarget);
    }
    if (spell_visual.use_raw_attachment_index) {
      record->flags |=
          ToFlagMask(LightningObjectFlags::kUseRawAttachmentIndex);
    }

    LightningObject_InitBolts(
        handle, caster, reinterpret_cast<std::uintptr_t>(targets),
        effective_target_count, single_source, first_bolt_event_parameter);

    if (loop) {
      LightningObject_AddReference(handle);
      if (output != nullptr) {
        output[output_count++] = handle.value;
      }
    }
    if (max_handles <= output_count) {
      break;
    }

    const std::uint32_t next_id =
        DecodeSpellChainComboAt(primary_definition->combo, combo_slot++);
    definition = next_id != 0u ? definitions.LookupEntry(next_id) : nullptr;
  }
}

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
    std::int32_t   first_bolt_event_parameter) {
  if (!caster) return;
  SpellVisual_CreateLightningEffect(
      chain_effect_id, caster, spell_c, target_array, target_count,
      spell_id, loop, single_source, out_handles, max_handles, position_offset,
      first_bolt_event_parameter);
}

void SpellVisual_GetAttachSourcePosition(float          out[3],
                                          std::uintptr_t unit,
                                          std::uint32_t  attach_id,
                                          const float    offset[3],
                                          bool           use_raw_attachment_index) {
  if (out == nullptr) {
    return;
  }

  const auto* const unit_object = reinterpret_cast<const CGUnit_C*>(unit);
  if (unit_object == nullptr) {
    out[0] = offset ? offset[0] : 0.0f;
    out[1] = offset ? offset[1] : 0.0f;
    out[2] = offset ? offset[2] : 0.0f;
    return;
  }

  if (attach_id != std::numeric_limits<std::uint32_t>::max()) {
    const openwow::render::RenderVec3 mapped_offset{
        offset != nullptr ? offset[0] : 0.0f,
        offset != nullptr ? offset[1] : 0.0f,
        offset != nullptr ? offset[2] : 0.0f,
    };
    if (unit_object->Presentation().GetMappedAttachmentPosition(
            out, attach_id, mapped_offset, use_raw_attachment_index)) {
      return;
    }
  }

  const std::array<float, 3> source_offset{
      offset != nullptr ? offset[0] : 0.0f,
      offset != nullptr ? offset[1] : 0.0f,
      offset != nullptr ? offset[2] : 0.0f,
  };
  std::array<float, 3> event_position{};
  if (ResolveCasterSourceEventPosition(*unit_object, source_offset,
                                       event_position)) {
    std::copy(event_position.begin(), event_position.end(), out);
    return;
  }

  if (offset != nullptr &&
      (offset[0] != 0.0f || offset[1] != 0.0f || offset[2] != 0.0f)) {
    float world_matrix[16]{};
    unit_object->Presentation().ModelToWorldMatrix(world_matrix);
    openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4Unbuffered(
        out, offset, world_matrix);
    return;
  }

  const auto position = unit_object->GetPosition();
  out[0] = position.x;
  out[1] = position.y;
  out[2] = position.z +
           unit_object->Presentation().ModelHeight() * kDefaultAttachSourceHeightScale;
}

MountTransitionObjectHandle MountTransitionObject_Allocate(
    const std::uint32_t extra_size) {
  return MountTransitionObjectRegistry::Get().Allocate(extra_size);
}

bool MountTransitionObject_InitAnimation(
    const ObjectManager& objects, const MountTransitionObjectHandle handle) {
  if (!handle.IsValid()) {
    return false;
  }

  std::uint64_t owner_guid = 0;
  std::memcpy(&owner_guid, reinterpret_cast<const void*>(handle.value + 8u),
              sizeof(owner_guid));
  const auto* const owner_object = CGObject_HasFlags(objects, owner_guid, 8u);
  if (owner_object == nullptr) {
    return false;
  }

  const auto* const owner = static_cast<const CGUnit_C*>(owner_object);
  const std::uint32_t primary_instance = owner->GetPrimaryM2InstanceId();
  const std::uint32_t mount_instance = owner->Mount().OverlayM2InstanceId();
  if (primary_instance == 0u || mount_instance == 0u) {
    return false;
  }

  auto* const m2 = owner->m2_system();
  if (m2 == nullptr) {
    return false;
  }
  const auto primary_readiness = m2->QueryInstanceReadiness(primary_instance);
  const auto mount_readiness = m2->QueryInstanceReadiness(mount_instance);
  if (primary_readiness.status != openwow::render::m2::M2ResultStatus::kReady ||
      !primary_readiness.render_ready ||
      mount_readiness.status != openwow::render::m2::M2ResultStatus::kReady ||
      !mount_readiness.render_ready) {
    return false;
  }

  const auto primary_animation =
      m2->QueryInstanceAnimationInfo(primary_instance);
  const auto mount_animation = m2->QueryInstanceAnimationInfo(mount_instance);
  if (primary_animation.status !=
          openwow::render::m2::M2ResultStatus::kReady ||
      mount_animation.status !=
          openwow::render::m2::M2ResultStatus::kReady ||
      primary_animation.info.duration_ms == 0u) {
    return false;
  }

  const auto mount_origin = m2->QueryModelWorldPoint(mount_instance);
  const auto primary_origin = m2->QueryModelWorldPoint(primary_instance);
  if (mount_origin.status != openwow::render::m2::M2ResultStatus::kReady ||
      primary_origin.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }

  auto* const values = reinterpret_cast<float*>(handle.value);
  auto* const words = reinterpret_cast<std::uint32_t*>(handle.value);
  values[26] = mount_origin.position[0];
  values[27] = mount_origin.position[1];
  values[28] = mount_origin.position[2];

  const float inverse_duration =
      1.0f / static_cast<float>(primary_animation.info.duration_ms);
  values[25] = inverse_duration;

  const std::uint32_t primary_animation_id =
      owner->Animation().GetCurrentAnimationId().value_or(
          static_cast<std::uint16_t>(
              primary_animation.info.resolved_animation_id));
  const auto primary_start = m2->QueryInstanceEvent(
      primary_instance, primary_animation_id, kMountTransitionStartEvent);
  const auto primary_end = m2->QueryInstanceEvent(
      primary_instance, primary_animation_id, kMountTransitionEndEvent);
  if (primary_start.status != openwow::render::m2::M2ResultStatus::kReady ||
      primary_end.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }

  values[6] = static_cast<float>(
                  primary_start.has_event ? primary_start.event.time_ms : 0u) *
              inverse_duration;
  values[7] = static_cast<float>(
                  primary_end.has_event ? primary_end.event.time_ms : 0u) *
              inverse_duration;
  values[5] = 1.0f / (values[7] - values[6]);

  const auto mount_start = m2->QueryInstanceEvent(
      mount_instance, kMountTransitionAnimationId,
      kMountTransitionStartEvent);
  const auto mount_end = m2->QueryInstanceEvent(
      mount_instance, kMountTransitionAnimationId,
      kMountTransitionEndEvent);
  if (mount_start.status != openwow::render::m2::M2ResultStatus::kReady ||
      mount_end.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }

  if (mount_start.has_event) {
    values[8] = mount_start.event.world_position[0];
    values[9] = mount_start.event.world_position[1];
    values[10] = mount_start.event.world_position[2];
  }
  values[11] = primary_origin.position[0] - values[8];
  values[12] = primary_origin.position[1] - values[9];
  values[13] = primary_origin.position[2] - values[10];

  values[14] = static_cast<float>(
                   mount_start.has_event ? mount_start.event.time_ms : 0u) *
               inverse_duration;
  values[15] = static_cast<float>(
                   mount_end.has_event ? mount_end.event.time_ms : 0u) *
               inverse_duration;
  const float transition_window = values[15] - values[14];
  values[16] = transition_window;
  if (!(transition_window > 0.0f)) {
    return false;
  }

  values[16] = 1.0f / transition_window;
  words[30] |= kMountTransitionInitializedFlag;
  values[17] = 0.0f;
  values[29] = owner->GetFacing();
  return true;
}

void MountTransitionObject_GetTargetPosition(
    const ObjectManager& objects, const MountTransitionObjectHandle handle,
    float out[3]) {
  if (!handle.IsValid()) {
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    return;
  }

  const auto* const floats = reinterpret_cast<const float*>(handle.value);

  out[0] = floats[11];
  out[1] = floats[12];
  out[2] = floats[13];

  std::uint64_t guid_raw = 0;
  std::memcpy(&guid_raw, reinterpret_cast<const void*>(handle.value + 8),
              sizeof(guid_raw));

  const auto* const world_obj =
      CGObject_HasFlags(objects, guid_raw, 8);
  if (world_obj == nullptr) {
    return;
  }

  const auto* unit = static_cast<const CGUnit_C*>(world_obj);
  float bone_world_pos[3]{};
  float attach_pos[3]{};
  if (!QueryUnitM2ModelWorldPointAndAttachment(*unit, 0u, bone_world_pos,
                                               attach_pos)) {
    return;
  }

  out[0] = attach_pos[0] - bone_world_pos[0];
  out[1] = attach_pos[1] - bone_world_pos[1];
  out[2] = attach_pos[2] - bone_world_pos[2];

  const float scale = floats[4];
  out[0] *= scale;
  out[1] *= scale;
  out[2] *= scale;
}

bool MountTransitionObject_Update(const MountTransitionObjectHandle handle) {
  if (!handle.IsValid()) return false;
  if (!MountTransitionObjectRegistry::Get().Owns(handle)) return false;

  if (MountTransitionObject_IsTransitionComplete(handle)) {
    return false;
  }

  float* const floats = reinterpret_cast<float*>(handle.value);
  const float blend = MountTransitionObject_GetBlendFactor(handle);

  const float new_blend = std::min(blend + 0.05f, 1.0f);
  floats[31] = new_blend;

  if (new_blend >= 1.0f) {
    MountTransitionObject_MarkTransitionComplete(handle);
    return false;
  }

  return true;
}

MountTransitionObjectHandle MountTransitionObject_CreateForUnit(
    const CGUnit_C* const owner) {
  if (owner == nullptr) {
    return {};
  }

  const auto handle = MountTransitionObject_Allocate(0u);
  if (!handle.IsValid()) {
    return {};
  }

  const auto position = owner->GetPosition();
  auto* const floats = reinterpret_cast<float*>(handle.value);
  auto* const words = reinterpret_cast<std::uint32_t*>(handle.value);

  const std::uint64_t guid_raw = owner->GetGuid().GetRawValue();
  std::memcpy(reinterpret_cast<void*>(handle.value + 8), &guid_raw,
              sizeof(guid_raw));

  floats[21] = position.x;
  floats[22] = position.y;
  floats[23] = position.z;
  words[24] = openwow::core::GameClock::GetTickCount32();
  floats[8] = 0.0f;
  floats[9] = 0.0f;
  floats[10] = 0.0f;
  floats[30] = 0.0f;
  floats[31] = 0.0f;
  words[32] = openwow::core::GameClock::GetTickCount32();
  return handle;
}

void MountTransitionObject_MarkTransitionComplete(
    const MountTransitionObjectHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  auto *const flags =
      reinterpret_cast<std::uint32_t *>(handle.value +
                                        kMountTransitionFlagsOffset);
  *flags |= kMountTransitionCompleteFlag;
}

bool MountTransitionObject_IsTransitionComplete(
    const MountTransitionObjectHandle handle) {
  if (!handle.IsValid()) {
    return false;
  }

  const auto *const flags =
      reinterpret_cast<const std::uint32_t *>(handle.value +
                                             kMountTransitionFlagsOffset);
  return (*flags & kMountTransitionCompleteFlag) != 0u;
}

void MountTransitionObject_Release(const MountTransitionObjectHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  MountTransitionObjectRegistry::Get().Release(handle);
}

float MountTransitionObject_GetBlendFactor(
    const MountTransitionObjectHandle handle) {
  if (!handle.IsValid()) {
    return 0.0f;
  }
  return *reinterpret_cast<const float *>(handle.value + 16);
}

float MountTransitionObject_GetScale(
    const MountTransitionObjectHandle handle) {
  if (!handle.IsValid()) {
    return 1.0f;
  }
  return *reinterpret_cast<const float *>(handle.value + 124);
}

void MountTransitionObject_GetTransformData(
    const MountTransitionObjectHandle handle,
    float out_position[3],
    float out_rotation[3],
    float *out_facing) {
  if (!handle.IsValid()) {
    out_position[0] = out_position[1] = out_position[2] = 0.0f;
    out_rotation[0] = out_rotation[1] = out_rotation[2] = 0.0f;
    *out_facing = 0.0f;
    return;
  }
  const auto *const bytes =
      reinterpret_cast<const std::uint8_t *>(handle.value);
  std::memcpy(out_position, bytes + 72, 3 * sizeof(float));
  std::memcpy(out_rotation, bytes + 84, 3 * sizeof(float));
  std::memcpy(out_facing, bytes + 116, sizeof(float));
}

void SpellVisualKit_CreateAreaModel(std::uintptr_t obj,
                                     const float    position[3],
                                     const char*    model_path,
                                     float          radius,
                                     float          rate) {
  if (!obj || !position || !model_path) return;

  auto* kit = reinterpret_cast<AreaModelKitObject*>(obj);

  kit->position[0] = position[0];
  kit->position[1] = position[1];
  kit->position[2] = position[2];
  kit->radius = radius;
  kit->accumulated_spawn = 0.0f;
  kit->rate = ScaleAreaModelRateBySpellEffectLevel(rate);
  kit->attach_flag = 0u;

  std::strncpy(kit->model_path, model_path,
               AreaModelKitObject::kModelPathSize - 1);
  kit->model_path[AreaModelKitObject::kModelPathSize - 1] = '\0';

  float transform[16] = {};
  openwow::math::row_major_mat4x4::SetIdentity(transform);
  transform[12] = position[0];
  transform[13] = position[1];
  transform[14] = position[2];
  std::memcpy(kit->transform, transform, sizeof(kit->transform));

  kit->renderer_effect_id = 0u;
  if (!g_area_model_render_callbacks.create) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "SpellVisualKit_CreateAreaModel: no area model renderer");
  } else {
    SpellVisualAreaModelRenderRequest request{};
    request.model_path = kit->model_path;
    request.position = kit->position;
    request.radius = kit->radius;
    request.duration_seconds = 0.0f;
    kit->renderer_effect_id = g_area_model_render_callbacks.create(request);
  }

  SpellVisualSphere sphere{};
  sphere.x = 0.0f;
  sphere.y = 0.0f;
  sphere.z = 0.0f;
  sphere.radius = radius;
  [[maybe_unused]] BoundingBox6 bbox = SpellVisual_ComputeBoundingBox(sphere);
}

void SpellVisualKit_RenderShards(const std::uintptr_t kit,
                                 const bool visible) {
  auto* const owned = AreaModelKitObjectRegistry::Get().FindOwned(kit);
  if (owned == nullptr || owned->object == nullptr) {
    return;
  }

  owned->visible = visible;
  const std::uint32_t now = SpellVisualNowMs();
  for (auto& shard : owned->shards) {
    if (shard.completed || now < shard.activation_tick) {
      continue;
    }

    if (!shard.animation_started) {
      shard.animation_started = true;
      std::uint32_t duration_ms = 0u;
      if (shard.renderer_model_id != 0u &&
          g_area_model_render_callbacks.start_shard_animation) {
        const std::uint64_t shard_id = shard.shard_id;
        duration_ms =
            g_area_model_render_callbacks.start_shard_animation(
                shard.renderer_model_id,
                [kit, shard_id]() {
                  MarkAreaModelShardComplete(kit, shard_id);
                });
      } else {
        duration_ms = kFallbackShardAnimationLifetimeMs;
      }
      if (duration_ms != 0u) {
        shard.retirement_tick = now + duration_ms;
      }
    }

    shard.visible = visible;
    if (shard.renderer_model_id != 0u &&
        g_area_model_render_callbacks.set_shard_visible) {
      (void)g_area_model_render_callbacks.set_shard_visible(
          shard.renderer_model_id, visible);
    }

    RefreshShardWorldPosition(*owned->object, shard);
    float world_transform[16]{};
    BuildShardWorldTransform(shard.world_position, world_transform);
    if (shard.renderer_model_id != 0u &&
        g_area_model_render_callbacks.set_shard_transform) {
      (void)g_area_model_render_callbacks.set_shard_transform(
          shard.renderer_model_id, world_transform);
    }
  }
}

int SpellVisualKit_ShardCallback(std::uintptr_t kit, bool visible) {
  SpellVisualKit_RenderShards(kit, visible);
  return 1;
}

static constexpr std::uint32_t kFourCC_SND = 0x444E5324u;

void SpellVisualKit_AreaModel_SoundEventCallback(
    openwow::audio::SoundRuntime& sound_runtime,
    std::uint32_t& throttle_counter,
    std::uint32_t , std::uint32_t ,
    std::uint32_t fourcc, std::uint32_t data, const float* pos) {
  const std::uint32_t counter = ++throttle_counter;

  if ((counter & 3u) == 0 && fourcc == kFourCC_SND) {

    (void)sound_runtime.PlaySoundKit(data, pos, nullptr);
  }
}

std::uintptr_t BlizzardObject_Create(const float    position[3],
                                      float          radius,
                                      std::int32_t   effect_id,
                                      float          rate,
                                      const data::dbc::DbcLoader* dbc) {
  if (position == nullptr) {
    return 0u;
  }

  std::string model_path = kDefaultAreaModelPath;
  if (dbc != nullptr) {
    const auto& store = dbc->spell_visual_kit_area_model();
    const auto* entry = store.LookupEntryByRowIndex(effect_id);
    if (entry == nullptr && !store.empty()) {
      entry = store.LookupEntryByRowIndex(0);
    }
    if (entry != nullptr && !entry->model_path.empty()) {
      model_path.assign(entry->model_path);
    }
  }

  auto& registry = AreaModelKitObjectRegistry::Get();
  const std::uintptr_t handle = registry.Allocate();
  SpellVisualKit_CreateAreaModel(handle, position, model_path.c_str(), radius,
                                 rate);

  return handle;
}

void SpellVisualKit_AreaModel_Cleanup(std::uintptr_t obj) {
  if (obj == 0u) {
    return;
  }

  auto& registry = AreaModelKitObjectRegistry::Get();
  auto* const owned = registry.FindOwned(obj);
  const auto* const area = owned != nullptr ? owned->object.get() : nullptr;
  if (area != nullptr && owned != nullptr) {
    for (auto& shard : owned->shards) {
      DestroyAreaModelShard(*area, shard);
    }
    owned->shards.clear();
  }
  if (area != nullptr && area->renderer_effect_id != 0u &&
      g_area_model_render_callbacks.destroy) {
    g_area_model_render_callbacks.destroy(area->renderer_effect_id);
  }
  registry.Release(obj);
}

bool SpellVisualKit_AreaModel_SetTransformMatrix(const std::uintptr_t obj,
                                                  const float* const matrix) {
  auto* const area = AreaModelKitObjectRegistry::Get().Find(obj);
  if (area == nullptr) {
    return false;
  }
  AreaModelKitObject_SetTransformMatrix(*area, matrix);
  return true;
}

bool SpellVisualKit_AreaModel_Update(std::uintptr_t obj,
                                      const float delta_seconds) {
  if (obj == 0) return false;

  auto* const owned = AreaModelKitObjectRegistry::Get().FindOwned(obj);
  if (owned == nullptr || owned->object == nullptr) {
    return false;
  }
  auto& area = *owned->object;

  const std::uint32_t now = SpellVisualNowMs();
  for (auto& shard : owned->shards) {
    if (!shard.completed && shard.retirement_tick != 0u &&
        shard.retirement_tick <= now) {
      shard.completed = true;
    }
  }
  owned->shards.erase(
      std::remove_if(
          owned->shards.begin(), owned->shards.end(),
          [&area](auto& shard) {
            if (!shard.completed) {
              return false;
            }
            DestroyAreaModelShard(area, shard);
            return true;
          }),
      owned->shards.end());

  const float dt = delta_seconds <= kAreaModelUpdateDeltaClampSeconds
                       ? delta_seconds
                       : kAreaModelUpdateDeltaClampSeconds;
  area.accumulated_spawn += area.rate * dt;

  while (area.accumulated_spawn >= 1.0f) {
    SpawnAreaModelShard(*owned);
    area.accumulated_spawn -= 1.0f;
  }

  return area.rate != 0.0f || !owned->shards.empty();
}

void SpellVisuals_SetAreaModelRenderCallbacks(
    SpellVisualAreaModelRenderCallbacks callbacks) {
  g_area_model_render_callbacks = std::move(callbacks);
}

void SpellVisuals_ClearAreaModelRenderCallbacks() {
  g_area_model_render_callbacks = {};
}

void SpellVisuals_SetAreaModelRuntimeCallbacks(
    SpellVisualAreaModelRuntimeCallbacks callbacks) {
  g_area_model_runtime_callbacks = std::move(callbacks);
}

void SpellVisuals_ClearAreaModelRuntimeCallbacks() {
  g_area_model_runtime_callbacks = {};
}

std::optional<SpellVisualAreaModelStateSnapshot>
SpellVisualKit_GetAreaModelStateSnapshot(const std::uintptr_t obj) {
  const auto* const owned = AreaModelKitObjectRegistry::Get().FindOwned(obj);
  if (owned == nullptr || owned->object == nullptr) {
    return std::nullopt;
  }

  SpellVisualAreaModelStateSnapshot snapshot{};
  snapshot.accumulated_spawn = owned->object->accumulated_spawn;
  snapshot.rate = owned->object->rate;
  snapshot.visible = owned->visible;
  snapshot.shards.reserve(owned->shards.size());
  for (const auto& shard : owned->shards) {
    snapshot.shards.push_back(SpellVisualAreaShardSnapshot{
        .shard_id = shard.shard_id,
        .renderer_model_id = shard.renderer_model_id,
        .local_position = shard.local_position,
        .world_position = shard.world_position,
        .activation_tick = shard.activation_tick,
        .retirement_tick = shard.retirement_tick,
        .collision_probe_count = shard.collision_probe_count,
        .collision_hit_count = shard.collision_hit_count,
        .animation_started = shard.animation_started,
        .visible = shard.visible,
        .completed = shard.completed,
    });
  }
  return snapshot;
}

namespace {

openwow::foundation::hashing::AdlerSeedState g_ribbon_prng_state{};

}

float SpellVisualRibbonRandomFloatRange(float min_val, float max_val) {
  return openwow::foundation::hashing::AdlerSeedNextRangeFloat(
      min_val, max_val, g_ribbon_prng_state);
}

void SpellVisualRibbonRandomizeProperties(SpellVisualRibbonStripLayout& strip) {
  constexpr float kTwoPi = 6.2831855f;

  auto& rng = g_ribbon_prng_state;

  const auto* def = static_cast<const std::byte*>(
      openwow::data::AsyncFileRead_ResolvePointerToken(strip.definition_ptr));

  strip.strip_flags &= 0xFFFFFFFAu;

  auto read_def_f32 = [def](std::size_t offset) -> float {
    float v;
    std::memcpy(&v, def + offset, sizeof(v));
    return v;
  };

  auto read_def_u32 = [def](std::size_t offset) -> std::uint32_t {
    std::uint32_t v;
    std::memcpy(&v, def + offset, sizeof(v));
    return v;
  };

  const float above_min = read_def_f32(0x58);
  const float above_max = read_def_f32(0x5C);
  strip.above_width =
      openwow::foundation::hashing::AdlerSeedNextRangeFloat(
          above_min, above_max, rng);

  const float rate_min = read_def_f32(0x60);
  const float rate_max = read_def_f32(0x64);
  strip.above_rate =
      openwow::foundation::hashing::AdlerSeedNextRangeFloat(
          rate_min, rate_max, rng);

  const std::uint32_t def_flags = read_def_u32(0x20);
  if (def_flags & 0x400u) {
    strip.rotation_angle = read_def_f32(0xB0);
  } else {
    strip.rotation_angle =
        openwow::foundation::hashing::AdlerSeedNextUnitFloat(rng) * kTwoPi;
  }

  const float below_min = read_def_f32(0x6C);
  const float below_max = read_def_f32(0x70);
  strip.below_width =
      openwow::foundation::hashing::AdlerSeedNextRangeFloat(
          below_min, below_max, rng);

  const float brate_min = read_def_f32(0x74);
  const float brate_max = read_def_f32(0x78);
  strip.below_rate =
      openwow::foundation::hashing::AdlerSeedNextRangeFloat(
          brate_min, brate_max, rng);

  const float life_min = read_def_f32(0x80);
  const float life_max = read_def_f32(0x84);
  strip.segment_lifetime =
      openwow::foundation::hashing::AdlerSeedNextRangeFloat(
          life_min, life_max, rng);

  strip.distance_accumulator = 0.0f;

  const float gravity_direction = read_def_f32(0x90);
  if (gravity_direction < 0.0f) {
    strip.strip_flags |= 0x8u;
  }

  strip.random_factor =
      openwow::foundation::hashing::AdlerSeedNextUnitFloat(rng);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void SpellVisualRibbonUpdateSegments(
    std::uintptr_t emitter,
    float dt) {
  if (emitter == 0) return;

  auto* strip = reinterpret_cast<SpellVisualRibbonStripLayout*>(emitter);

  strip->texture_phase += dt;

  strip->above_width += strip->above_rate * dt;
  strip->below_width += strip->below_rate * dt;

  if (strip->definition_ptr != 0) {
    const auto* def = static_cast<const float*>(
        openwow::data::AsyncFileRead_ResolvePointerToken(strip->definition_ptr));
    strip->rotation_angle -= def[0x54 / 4] * dt;
  }

  strip->segment_lifetime -= dt;
  if (strip->segment_lifetime <= 0.0f) {

    SpellVisualRibbonRandomizeProperties(*strip);

    if (strip->segment_data_ptr != 0) {
      auto* knots = static_cast<LightningBoltKnot*>(
          openwow::data::AsyncFileRead_ResolvePointerToken(
              strip->segment_data_ptr));
      if (strip->segment_count > 0) {
        SpellVisualRibbonRandomizeSegmentPositionAndVelocity(
            *strip, knots[0], 1.0f);
      }
    }
    strip->segment_lifetime = std::max(strip->segment_lifetime, 0.1f);
  }

  strip->distance_accumulator += dt * 2.0f;

  constexpr std::uint32_t kMaxSegments = 64;
  if (strip->segment_data_ptr != 0 &&
      strip->distance_accumulator >= 1.0f &&
      strip->segment_count < kMaxSegments) {
    auto* knots = static_cast<LightningBoltKnot*>(
        openwow::data::AsyncFileRead_ResolvePointerToken(
            strip->segment_data_ptr));
    const std::uint32_t old_count = strip->segment_count;
    if (old_count > 0) {

      std::memmove(&knots[1], &knots[0], sizeof(LightningBoltKnot) * old_count);
    }

    SpellVisualRibbonRandomizeSegmentPositionAndVelocity(
        *strip, knots[0], 1.0f);
    knots[0].segment_id = static_cast<std::int32_t>(old_count);
    strip->segment_count = old_count + 1;
    strip->distance_accumulator -= 1.0f;
  }
}

void SpellVisualParticlePool_UpdateAll(TSGrowableArray& pool, float dt) {
  auto* slots = static_cast<std::uintptr_t*>(pool.data);

  for (std::uint32_t i = pool.count; i-- > 0;) {
    const std::uintptr_t raw = slots[i];
    if ((raw & 1u) == 0) {
      SpellVisualRibbonUpdateSegments(raw, dt);
    }
  }
}

void InitSpellVisuals() {

  SpellVisuals_CleanAll();
  LightningObjectRegistry::Get().Reset();
  g_area_model_prng_state =
      openwow::foundation::hashing::MakeAdlerSeedState(
          core::GameClock::GetTickCount32());

  if (g_lightning_emitter != nullptr) {
    CLightningEmitter_Destroy(g_lightning_emitter);
    std::free(g_lightning_emitter);
    g_lightning_emitter = nullptr;
  }
  auto* emitter = static_cast<CLightningEmitter*>(
      std::calloc(1, sizeof(CLightningEmitter)));
  if (emitter) {
    CLightningEmitter_Init(emitter);
  }
  g_lightning_emitter = emitter;

}

void SpellVisuals_CleanAll() {

  LightningObject_FreeAll();

  CEffect_C::Shutdown();

  SpellVisuals_ClearLightingEnvelope();

  auto& registry = AreaModelKitObjectRegistry::Get();
  registry.CleanAndReleaseAll();
}

void SpellVisuals_BeginLightingEnvelope(
    const WorldSession& session,
    const std::uint32_t packed_argb,
    const float fade_in_fraction,
    const std::uint32_t spell_id) {
  const auto duration = static_cast<std::uint32_t>(
      ComputeSpellDuration(
          session, spell_id, false, false, false, false));
  const std::uint32_t now = SpellVisualNowMs();
  const float fade_in_ms = static_cast<float>(duration) * fade_in_fraction;

  g_lighting_envelope = {
      .packed_argb = packed_argb,
      .start_tick = now,
      .fade_in_end_tick = now + TruncateNonNegativeDelay(fade_in_ms),
      .fade_out_start_tick = now + duration,
      .end_tick = now + duration + 100u,
  };
}

void SpellVisuals_ClearLightingEnvelope() {
  g_lighting_envelope = {};
  DayNight_ClearSpellVisualLightingTint();
}

SpellVisualLightingEnvelope SpellVisuals_GetLightingEnvelopeSnapshot() {
  return g_lighting_envelope;
}

namespace {

void UpdateSpellVisualLightingEnvelope(const std::uint32_t now) {
  auto& envelope = g_lighting_envelope;
  if (!envelope.IsActive()) {
    return;
  }

  float alpha = 0.0f;
  if (now < envelope.fade_in_end_tick) {
    const std::uint32_t denominator =
        envelope.fade_in_end_tick - envelope.start_tick;
    if (denominator != 0u) {
      alpha = static_cast<float>(now - envelope.start_tick) /
              static_cast<float>(denominator);
    }
  } else if (now < envelope.fade_out_start_tick) {
    alpha = 1.0f;
  } else if (now < envelope.end_tick) {
    const std::uint32_t denominator =
        envelope.end_tick - envelope.fade_out_start_tick;
    if (denominator != 0u) {
      alpha = 1.0f -
              static_cast<float>(now - envelope.fade_out_start_tick) /
                  static_cast<float>(denominator);
    }
  } else {
    envelope.start_tick = 0u;
    envelope.end_tick = 0u;
  }

  DayNight_SetSpellVisualLightingTint(envelope.packed_argb, alpha);
}

}

void SpellVisuals_UpdateAll(WorldSession& session, const float delta_seconds) {
  const std::uint32_t now = SpellVisualNowMs();
  UpdateSpellVisualLightingEnvelope(now);
  CEffect_C::UpdateAll(now);

  auto& lightning_registry = LightningObjectRegistry::Get();
  for (const auto handle : lightning_registry.SnapshotHandles()) {
    if (!LightningObject_UpdateBolts(session.objects(), handle, now)) {
      LightningObject_Cleanup(handle);
    }
  }

  auto& area_registry = AreaModelKitObjectRegistry::Get();
  for (const auto handle : area_registry.SnapshotHandles()) {
    if (!SpellVisualKit_AreaModel_Update(handle, delta_seconds)) {
      SpellVisualKit_AreaModel_Cleanup(handle);
    } else {
      SpellVisualKit_RenderShards(handle, true);
    }
  }
}

namespace {

std::uint32_t g_visible_humanoid_creature_entry = 0;
std::uint32_t g_visible_humanoid_filter_flags = 0;

}

void SpellVisual_ResetVisibleHumanoidState() {
  g_visible_humanoid_creature_entry = 0;
  g_visible_humanoid_filter_flags = 0;
}

void SpellVisuals_Shutdown() {

  SpellVisuals_CleanAll();

  if (g_lightning_emitter) {
    CLightningEmitter_Destroy(g_lightning_emitter);
    std::free(g_lightning_emitter);
    g_lightning_emitter = nullptr;
  }

  LightningObjectRegistry::Get().Reset();
  g_area_model_prng_state = {};
  g_area_model_runtime_callbacks = {};
  g_chain_render_callbacks = {};
  g_area_model_render_callbacks = {};
}

void SpellVisualRibbonRandomizeSegmentPositionAndVelocity(
    SpellVisualRibbonStripLayout& strip,
    LightningBoltKnot& knot,
    float distance) {
  const auto* def = static_cast<const std::byte*>(
      openwow::data::AsyncFileRead_ResolvePointerToken(strip.definition_ptr));

  auto read_def_f32 = [def](std::size_t offset) -> float {
    float value;
    std::memcpy(&value, def + offset, sizeof(value));
    return value;
  };

  auto& rng = g_ribbon_prng_state;
  knot.position[0] =
      openwow::foundation::hashing::AdlerSeedNextRangeFloat(
          -distance, distance, rng);
  knot.position[1] = 0.0f;
  knot.position[2] =
      openwow::foundation::hashing::AdlerSeedNextRangeFloat(
          -distance, distance, rng);

  const float velocity_scale = read_def_f32(0x3C);
  knot.velocity[0] =
      openwow::foundation::hashing::AdlerSeedNextRangeFloat(
          -velocity_scale, velocity_scale, rng);
  knot.velocity[1] = 0.0f;
  knot.velocity[2] =
      openwow::foundation::hashing::AdlerSeedNextRangeFloat(
          -velocity_scale, velocity_scale, rng);

  const float phase_max = read_def_f32(0x48);
  if ((strip.strip_flags & 0x1u) != 0u) {
    knot.texture_phase =
        openwow::foundation::hashing::AdlerSeedNextRangeFloat(
            0.0f, phase_max, rng);
    return;
  }

  const float phase_min = read_def_f32(0x44);
  knot.texture_phase =
      openwow::foundation::hashing::AdlerSeedNextRangeFloat(
          phase_min, phase_max, rng);
}

}
