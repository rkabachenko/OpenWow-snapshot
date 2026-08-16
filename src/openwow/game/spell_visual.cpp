
#include "openwow/game/spell_visual.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

namespace openwow::game {

bool SpellVisualHandler::ParseVisualEvent(const std::uint8_t* data,
                                          std::size_t len, bool is_impact) {
  if (!data || len < 12) return false;

  PacketReader r(data, len);
  SpellVisualEvent ev;
  ev.is_impact = is_impact;
  if (!r.ReadU64(ev.target_guid)) return false;
  if (!r.ReadU32(ev.spell_visual_kit_id)) return false;

  visual_events_.push_back(ev);
  TrimEvents();
  return true;
}

void SpellVisualHandler::TrimEvents() {
  while (visual_events_.size() > max_events_) {
    visual_events_.erase(visual_events_.begin());
  }
}

bool SpellVisualHandler::HandlePlaySpellVisual(const std::uint8_t* data,
                                               std::size_t len) {
  return ParseVisualEvent(data, len, false);
}

bool SpellVisualHandler::HandlePlaySpellImpact(const std::uint8_t* data,
                                               std::size_t len) {
  return ParseVisualEvent(data, len, true);
}

bool SpellVisualHandler::HandleTriggerCinematic(const std::uint8_t* data,
                                                std::size_t len) {
  if (!data || len < 4) return false;

  PacketReader r(data, len);
  if (!r.ReadU32(last_cinematic_.cinematic_sequence_id)) return false;

  if (last_cinematic_.cinematic_sequence_id == 0) return false;

  return true;
}

bool SpellVisualHandler::HandleTriggerMovie(const std::uint8_t* data,
                                            std::size_t len) {
  if (!data || len < 4) return false;

  PacketReader r(data, len);
  if (!r.ReadU32(last_movie_.movie_id)) return false;

  if (last_movie_.movie_id == 0) return false;

  return true;
}

void SpellVisualHandler::RegisterKit(const SpellVisualKit& kit) {
  if (kit.kit_id == 0) return;
  kits_[kit.kit_id] = kit;
}

void SpellVisualHandler::MapSpellToKit(std::uint32_t spell_id,
                                       std::uint32_t kit_id) {
  if (spell_id == 0 || kit_id == 0) return;
  spell_to_kit_[spell_id] = kit_id;
}

const SpellVisualKit* SpellVisualHandler::ResolveKitForSpell(
    std::uint32_t spell_id) const {
  if (spell_id == 0) return nullptr;

  auto map_it = spell_to_kit_.find(spell_id);
  if (map_it == spell_to_kit_.end()) return nullptr;

  return GetKit(map_it->second);
}

const SpellVisualKit* SpellVisualHandler::GetKit(std::uint32_t kit_id) const {
  auto it = kits_.find(kit_id);
  if (it == kits_.end()) return nullptr;
  return &it->second;
}

std::vector<SpellVisualEvent> SpellVisualHandler::GetEventsForTarget(
    std::uint64_t target_guid) const {
  std::vector<SpellVisualEvent> result;
  result.reserve(8);
  for (const auto& ev : visual_events_) {
    if (ev.target_guid == target_guid) {
      result.push_back(ev);
    }
  }
  return result;
}

std::vector<SpellVisualEvent> SpellVisualHandler::GetRecentEvents(
    std::size_t max_count) const {
  if (max_count == 0) return {};

  std::size_t start = 0;
  if (visual_events_.size() > max_count) {
    start = visual_events_.size() - max_count;
  }

  return {visual_events_.begin() + static_cast<std::ptrdiff_t>(start),
          visual_events_.end()};
}

void SpellVisualHandler::RemoveEventsForTarget(std::uint64_t target_guid) {
  visual_events_.erase(
      std::remove_if(visual_events_.begin(), visual_events_.end(),
                     [target_guid](const SpellVisualEvent& ev) {
                       return ev.target_guid == target_guid;
                     }),
      visual_events_.end());
}

void SpellVisualHandler::SetMaxEvents(std::size_t max) {
  max_events_ = (max == 0) ? 1 : max;
  TrimEvents();
}

std::size_t SpellVisualHandler::GetEventCount() const {
  return visual_events_.size();
}

void SpellVisualHandler::BeginCinematic() {
  cinematic_active_ = true;
}

void SpellVisualHandler::StopCinematic() {
  cinematic_active_ = false;
}

void SpellVisualHandler::StopMovie() {
  movie_active_ = false;
}

std::string SpellVisualHandler::GetCategoryName(SpellVisualCategory cat) {
  switch (cat) {
    case SpellVisualCategory::kNone:     return "None";
    case SpellVisualCategory::kMelee:    return "Melee";
    case SpellVisualCategory::kRanged:   return "Ranged";
    case SpellVisualCategory::kHoly:     return "Holy";
    case SpellVisualCategory::kFire:     return "Fire";
    case SpellVisualCategory::kFrost:    return "Frost";
    case SpellVisualCategory::kNature:   return "Nature";
    case SpellVisualCategory::kShadow:   return "Shadow";
    case SpellVisualCategory::kArcane:   return "Arcane";
    case SpellVisualCategory::kPhysical: return "Physical";
  }
  return "Unknown";
}

void SpellVisualHandler::Clear() {
  visual_events_.clear();
  last_cinematic_ = {};
  last_movie_ = {};
  cinematic_active_ = false;
  movie_active_ = false;
  kits_.clear();
  spell_to_kit_.clear();
}

namespace {

constexpr const char* kFallbackMissileModel = "Spells\\ErrorCube.mdx";

constexpr std::uint32_t kAttachmentTypeLookup[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
};
constexpr std::size_t kAttachmentTypeLookupSize =
    sizeof(kAttachmentTypeLookup) / sizeof(kAttachmentTypeLookup[0]);

}

MissileVisualTimingParams MissileVisualTimingParams::FromRawDbc(
    std::int32_t raw_speed, std::int32_t raw_scale,
    std::int32_t raw_phase, std::uint32_t flags) {
  MissileVisualTimingParams result;

  const float speed = static_cast<float>(raw_speed) * 0.001f;
  result.min_speed = (speed > 0.25f) ? speed : 0.25f;

  const float scale = static_cast<float>(raw_scale) * 0.001f;
  result.speed_scale = (scale > 0.01f) ? scale : 0.01f;

  float phase = static_cast<float>(raw_phase) * 0.001f;
  if (phase < 0.0f) phase = 0.0f;
  if (phase >= 1.0f) phase = 1.0f;
  result.phase = phase;

  result.motion_flags = flags;
  return result;
}

MissileVisualCreationResult SpellVisualHandler::CreateSpellVisualEffectDetailed(
    const SpellVisualCreateParams& params) {
  MissileVisualCreationResult result;

  const SpellVisualKit* kit = nullptr;
  if (params.visual_kit_id != 0) {
    kit = GetKit(params.visual_kit_id);
  }
  if (!kit) {
    kit = ResolveKitForSpell(params.spell_id);
  }
  if (!kit) {
    return result;
  }

  const std::int32_t missile_model = params.missile_model;
  float resolved_scale = params.scale;
  std::string model_path;
  MissileModelSourceType source_type;

  if (missile_model > 0) {

    source_type = static_cast<MissileModelSourceType>(missile_model);
    model_path = kit->missile_model;
    if (model_path.empty()) {
      model_path = kFallbackMissileModel;
    }
  } else if (missile_model == 0) {

    source_type = MissileModelSourceType::kItemDisplayInfo;
    if (params.item_display_id != 0) {
      model_path = kit->missile_model;
    }
    if (model_path.empty()) {
      model_path = kFallbackMissileModel;
    }
  } else {

    source_type = static_cast<MissileModelSourceType>(missile_model);
    switch (missile_model) {
      case static_cast<std::int32_t>(MissileModelSourceType::kMainHandWeapon):
      case static_cast<std::int32_t>(MissileModelSourceType::kOffHandWeapon):
      case static_cast<std::int32_t>(MissileModelSourceType::kRangedWeapon):

        model_path = kFallbackMissileModel;
        break;
      case static_cast<std::int32_t>(MissileModelSourceType::kItemComponent1):
      case static_cast<std::int32_t>(MissileModelSourceType::kItemComponent2):
        model_path = kFallbackMissileModel;
        break;
      default:
        model_path = kFallbackMissileModel;
        break;
    }
  }

  result.model_source = source_type;
  result.resolved_model_path = model_path;
  result.model_scale = resolved_scale;

  result.timing = MissileVisualTimingParams::FromRawDbc(
      params.raw_speed, params.raw_speed_scale,
      params.raw_phase, params.raw_motion_flags);

  if (result.timing.motion_flags & 1u) {
    result.visual_flags |= 0x7800u;
  }

  if (params.spell_visual_flags & 0x200u) {
    result.attachment_type = params.attachment_type_raw;
  } else {
    const auto idx = params.attachment_type_raw;
    if (idx < kAttachmentTypeLookupSize) {
      result.attachment_type = kAttachmentTypeLookup[idx];
    } else {
      result.attachment_type = idx;
    }
  }

  result.salvo_count = 1;
  if (params.missile_motion_id != 0) {

  }

  result.area_trigger = (params.spell_visual_flags & 4u) != 0;

  SpellVisualEvent event;
  event.target_guid = params.target_guid;
  event.spell_visual_kit_id = kit->kit_id;
  event.is_impact = false;
  visual_events_.push_back(event);
  TrimEvents();

  result.success = true;
  result.instance_count = result.salvo_count;
  return result;
}

std::uint32_t SpellVisualHandler::CreateSpellVisualEffect(
    const SpellVisualCreateParams& params) {
  auto result = CreateSpellVisualEffectDetailed(params);
  return result.instance_count;
}

namespace {

constexpr float kHalf = 0.5f;
constexpr float kOne = 1.0f;
constexpr float kFrameSegmentRatio = 0.125f;
constexpr float kLegacyFrameIndexScale = 0.0059999996f;
constexpr float kLegacyFrameIndexPeriod = 166.66667f;
constexpr float kLegacyTau = -6.2831855f;

constexpr std::size_t kBoundsBottom = 100;
constexpr std::size_t kBoundsLeft = 104;
constexpr std::size_t kBoundsTop = 108;
constexpr std::size_t kBoundsRight = 112;
constexpr std::size_t kBubbleScale = 124;
constexpr std::size_t kPrimaryAlpha = 188;
constexpr std::size_t kSecondaryAlpha = 189;
constexpr std::size_t kAnimationPeriod = 672;
constexpr std::size_t kAnimationElapsed = 676;
constexpr std::size_t kStaticGeometryMode = 680;
constexpr std::size_t kFanLayoutMode = 684;
constexpr std::size_t kVertex0 = 692;
constexpr std::size_t kVertex1 = 704;
constexpr std::size_t kVertex2 = 716;
constexpr std::size_t kVertex3 = 728;
constexpr std::size_t kVertex4 = 740;
constexpr std::size_t kVertex5 = 752;
constexpr std::size_t kVertex6 = 764;
constexpr std::size_t kVertex7 = 776;
constexpr std::size_t kVertex8 = 788;
constexpr std::size_t kVertex9 = 800;
constexpr std::size_t kVertex0Uv = 812;
constexpr std::size_t kVertex8Uv = 876;
constexpr std::size_t kVertex9Uv = 884;
constexpr std::size_t kFillColor = 892;
constexpr std::size_t kArtworkQuadVertices = 896;
constexpr std::size_t kRotatingUv0 = 944;
constexpr std::size_t kRotatingUv1 = 952;
constexpr std::size_t kRotatingUv2 = 960;
constexpr std::size_t kRotatingUv3 = 968;
constexpr std::size_t kReadyFlashColor = 976;
constexpr std::size_t kMainTextureHandle = 688;
constexpr std::size_t kEdgeTextureHandle = 980;
constexpr std::size_t kRotatingAlphaByte = 979;
constexpr std::size_t kDrawEdgeFlag = 984;
constexpr std::size_t kEdgeQuadVertices = 988;
constexpr std::size_t kEdgeUv0 = 1036;
constexpr std::size_t kEdgeUv1 = 1044;
constexpr std::size_t kEdgeUv2 = 1052;
constexpr std::size_t kEdgeUv3 = 1060;
constexpr std::size_t kEdgeColor = 1068;

constexpr std::array<float, 7> kMinimapAngleSteps = {
    0.0f,
    -0.1303761f,
    -0.25918141f,
    -0.39269909f,
    -0.51836282f,
    -0.6518805f,
    -0.78539819f,
};

constexpr std::array<float, 7> kMinimapScaleSteps = {
    1.0f,
    0.69930071f,
    0.54054052f,
    0.62893081f,
    0.75187969f,
    0.625f,
    0.86956525f,
};

constexpr std::array<float, 7> kMinimapAlphaSteps = {
    0.0f,
    0.5f,
    1.0f,
    0.75f,
    0.5f,
    0.25f,
    0.0f,
};

class LegacyBubbleGeometryState {
 public:
  explicit LegacyBubbleGeometryState(void* self)
      : base_(static_cast<std::byte*>(self)) {}

  [[nodiscard]] float ReadFloat(std::size_t offset) const {
    float value = 0.0f;
    std::memcpy(&value, base_ + offset, sizeof(value));
    return value;
  }

  void WriteFloat(std::size_t offset, float value) {
    std::memcpy(base_ + offset, &value, sizeof(value));
  }

  [[nodiscard]] std::uint32_t ReadU32(std::size_t offset) const {
    std::uint32_t value = 0;
    std::memcpy(&value, base_ + offset, sizeof(value));
    return value;
  }

  void WriteU32(std::size_t offset, std::uint32_t value) {
    std::memcpy(base_ + offset, &value, sizeof(value));
  }

  [[nodiscard]] std::uint8_t ReadU8(std::size_t offset) const {
    return std::to_integer<std::uint8_t>(base_[offset]);
  }

  void WriteU8(std::size_t offset, std::uint8_t value) {
    base_[offset] = static_cast<std::byte>(value);
  }

  void CopyDword(std::size_t destination, std::size_t source) {
    WriteU32(destination, ReadU32(source));
  }

 private:
  std::byte* base_;
};

[[nodiscard]] float Lerp(float start, float end, float progress) {
  return start + (end - start) * progress;
}

void SetVertexXY(LegacyBubbleGeometryState& state,
                 std::size_t vertex_offset,
                 float x,
                 float y) {
  state.WriteFloat(vertex_offset, x);
  state.WriteFloat(vertex_offset + 4, y);
}

void CopyVertexTriplet(LegacyBubbleGeometryState& state,
                       std::size_t destination,
                       std::size_t source) {
  state.CopyDword(destination, source);
  state.CopyDword(destination + 4, source + 4);
  state.CopyDword(destination + 8, source + 8);
}

void WritePerimeterVertices(LegacyBubbleGeometryState& state,
                            float midpoint_x,
                            float midpoint_y,
                            float left,
                            float right,
                            float top,
                            float bottom) {
  SetVertexXY(state, kVertex1, right, top);
  SetVertexXY(state, kVertex2, right, midpoint_y);
  SetVertexXY(state, kVertex3, right, bottom);
  SetVertexXY(state, kVertex4, midpoint_x, bottom);
  SetVertexXY(state, kVertex5, left, bottom);
  SetVertexXY(state, kVertex6, left, midpoint_y);
  SetVertexXY(state, kVertex7, left, top);
}

void WriteEdgeUvs(LegacyBubbleGeometryState& state, float scale, float angle) {
  const float cosine = std::cos(angle);
  const float sine = std::sin(angle);
  const float negative_half_cosine = cosine * -0.5f;
  const float negative_half_sine = sine * -0.5f;
  const float positive_half_cosine = cosine * 0.5f;
  const float positive_half_sine = sine * 0.5f;

  state.WriteFloat(kEdgeUv0, (negative_half_cosine - negative_half_sine) * scale + 0.5f);
  state.WriteFloat(kEdgeUv0 + 4, (negative_half_sine + negative_half_cosine) * scale + 0.5f);
  state.WriteFloat(kEdgeUv1, (positive_half_cosine - negative_half_sine) * scale + 0.5f);
  state.WriteFloat(kEdgeUv1 + 4, (positive_half_sine + negative_half_cosine) * scale + 0.5f);
  state.WriteFloat(kEdgeUv2, (negative_half_cosine - positive_half_sine) * scale + 0.5f);
  state.WriteFloat(kEdgeUv2 + 4, (negative_half_sine + positive_half_cosine) * scale + 0.5f);
  state.WriteFloat(kEdgeUv3, (positive_half_cosine - positive_half_sine) * scale + 0.5f);
  state.WriteFloat(kEdgeUv3 + 4, scale * (positive_half_cosine + positive_half_sine) + 0.5f);
}

[[nodiscard]] std::uint8_t MultiplyAlphaBytes(
    std::uint8_t left, std::uint8_t right) {
  return static_cast<std::uint8_t>(
      (static_cast<std::uint32_t>(left) * static_cast<std::uint32_t>(right)) / 255u);
}

constexpr std::array<std::uint16_t, 12> kCooldownSweepIndices = {
    0u, 9u, 1u, 1u, 9u, 2u, 2u, 9u, 3u, 3u, 9u, 4u,
};

constexpr std::array<std::uint16_t, 6> kCooldownQuadIndices = {
    0u, 2u, 1u, 1u, 2u, 3u,
};

template <std::size_t VertexCount, std::size_t IndexCount>
void AppendCooldownRenderEntry(LegacyBubbleGeometryState& state,
                               std::vector<LegacyCooldownRenderEntry>& out_entries,
                               const std::uint32_t texture_handle,
                               const std::uint32_t render_state_key,
                               const std::uint32_t blend_state_key,
                               const std::size_t vertex_offset,
                               const std::size_t uv_offset,
                               const std::size_t color_offset,
                               const std::array<std::uint16_t, IndexCount>& indices,
                               const std::uint32_t pixel_shader_handle) {
  LegacyCooldownRenderEntry entry;
  entry.texture_handle = texture_handle;
  entry.render_state_key = render_state_key;
  entry.blend_state_key = blend_state_key;
  entry.packed_abgr = state.ReadU32(color_offset);
  entry.pixel_shader_handle = pixel_shader_handle;
  entry.vertex_count = static_cast<std::uint32_t>(VertexCount);
  entry.index_count = static_cast<std::uint32_t>(IndexCount);

  for (std::size_t index = 0; index < VertexCount; ++index) {
    const auto position = vertex_offset + index * 12u;
    const auto texcoord = uv_offset + index * 8u;
    entry.vertices[index] = {
        .x = state.ReadFloat(position),
        .y = state.ReadFloat(position + 4u),
        .z = state.ReadFloat(position + 8u),
        .u = state.ReadFloat(texcoord),
        .v = state.ReadFloat(texcoord + 4u),
    };
  }

  for (std::size_t index = 0; index < IndexCount; ++index) {
    entry.indices[index] = indices[index];
  }

  out_entries.push_back(entry);
}

}

void CGUnit_C_InterpolatePosition_Sub01(
    void* self, unsigned int quadrant, float progress) {
  LegacyBubbleGeometryState state(self);
  const float left = state.ReadFloat(kBoundsLeft);
  const float right = state.ReadFloat(kBoundsRight);
  const float top = state.ReadFloat(kBoundsTop);
  const float bottom = state.ReadFloat(kBoundsBottom);
  const float midpoint_x = (left + right) * kHalf;
  const float midpoint_y = (top + bottom) * kHalf;

  if (state.ReadU32(kFanLayoutMode) != 0u) {
    if (quadrant != 0u) {
      const float edge_y = Lerp(top, midpoint_y, progress);
      SetVertexXY(state, kVertex2, right, edge_y);
      SetVertexXY(state, kVertex1, right, top);
      SetVertexXY(state, kVertex3, right, top);
      SetVertexXY(state, kVertex4, right, top);
      SetVertexXY(state, kVertex5, right, top);
      SetVertexXY(state, kVertex6, right, top);
      SetVertexXY(state, kVertex7, right, top);
      SetVertexXY(state, kVertex8, right, top);
      return;
    }

    const float edge_x = Lerp(midpoint_x, right, progress);
    SetVertexXY(state, kVertex1, edge_x, top);
    SetVertexXY(state, kVertex2, edge_x, top);
    SetVertexXY(state, kVertex3, edge_x, top);
    SetVertexXY(state, kVertex4, edge_x, top);
    SetVertexXY(state, kVertex5, edge_x, top);
    SetVertexXY(state, kVertex6, edge_x, top);
    SetVertexXY(state, kVertex7, edge_x, top);
    SetVertexXY(state, kVertex8, edge_x, top);
    return;
  }

  if (quadrant == 0u) {
    SetVertexXY(state, kVertex0, Lerp(midpoint_x, right, progress), top);
    SetVertexXY(state, kVertex1, right, top);
  } else {
    SetVertexXY(state, kVertex1, right, Lerp(top, midpoint_y, progress));
    CopyVertexTriplet(state, kVertex0, kVertex1);
  }

  SetVertexXY(state, kVertex2, right, midpoint_y);
  SetVertexXY(state, kVertex3, right, bottom);
  SetVertexXY(state, kVertex4, midpoint_x, bottom);
  SetVertexXY(state, kVertex5, left, bottom);
  SetVertexXY(state, kVertex6, left, midpoint_y);
  SetVertexXY(state, kVertex7, left, top);
}

void CGUnit_C_InterpolatePosition_SubA(
    void* self, unsigned int quadrant, float progress) {
  LegacyBubbleGeometryState state(self);
  const float left = state.ReadFloat(kBoundsLeft);
  const float right = state.ReadFloat(kBoundsRight);
  const float top = state.ReadFloat(kBoundsTop);
  const float bottom = state.ReadFloat(kBoundsBottom);
  const float midpoint_x = (left + right) * kHalf;
  const float midpoint_y = (top + bottom) * kHalf;

  if (state.ReadU32(kFanLayoutMode) != 0u) {
    if (quadrant >= 3u) {
      const float edge_x = Lerp(right, midpoint_x, progress);
      SetVertexXY(state, kVertex4, edge_x, bottom);
      SetVertexXY(state, kVertex1, right, top);
      SetVertexXY(state, kVertex2, right, midpoint_y);
      SetVertexXY(state, kVertex3, right, bottom);
      SetVertexXY(state, kVertex5, edge_x, bottom);
      SetVertexXY(state, kVertex6, edge_x, bottom);
      SetVertexXY(state, kVertex7, edge_x, bottom);
      SetVertexXY(state, kVertex8, edge_x, bottom);
      return;
    }

    const float edge_y = Lerp(midpoint_y, bottom, progress);
    SetVertexXY(state, kVertex3, right, edge_y);
    SetVertexXY(state, kVertex1, right, top);
    SetVertexXY(state, kVertex2, right, midpoint_y);
    SetVertexXY(state, kVertex4, right, edge_y);
    SetVertexXY(state, kVertex5, right, edge_y);
    SetVertexXY(state, kVertex6, right, edge_y);
    SetVertexXY(state, kVertex7, right, edge_y);
    SetVertexXY(state, kVertex8, right, edge_y);
    return;
  }

  if (quadrant >= 3u) {
    SetVertexXY(state, kVertex3, Lerp(right, midpoint_x, progress), bottom);
    CopyVertexTriplet(state, kVertex0, kVertex3);
    CopyVertexTriplet(state, kVertex1, kVertex3);
    CopyVertexTriplet(state, kVertex2, kVertex3);
  } else {
    SetVertexXY(state, kVertex2, right, Lerp(midpoint_y, bottom, progress));
    CopyVertexTriplet(state, kVertex0, kVertex2);
    CopyVertexTriplet(state, kVertex1, kVertex2);
    SetVertexXY(state, kVertex3, right, bottom);
  }

  SetVertexXY(state, kVertex4, midpoint_x, bottom);
  SetVertexXY(state, kVertex5, left, bottom);
  SetVertexXY(state, kVertex6, left, midpoint_y);
  SetVertexXY(state, kVertex7, left, top);
}

void CGUnit_C_InterpolatePosition_SubB(
    void* self, unsigned int quadrant, float progress) {
  LegacyBubbleGeometryState state(self);
  const float left = state.ReadFloat(kBoundsLeft);
  const float right = state.ReadFloat(kBoundsRight);
  const float top = state.ReadFloat(kBoundsTop);
  const float bottom = state.ReadFloat(kBoundsBottom);
  const float midpoint_x = (left + right) * kHalf;
  const float midpoint_y = (top + bottom) * kHalf;

  if (state.ReadU32(kFanLayoutMode) != 0u) {
    if (quadrant >= 5u) {
      const float edge_y = Lerp(bottom, midpoint_y, progress);
      SetVertexXY(state, kVertex6, left, edge_y);
      SetVertexXY(state, kVertex1, right, top);
      SetVertexXY(state, kVertex2, right, midpoint_y);
      SetVertexXY(state, kVertex3, right, bottom);
      SetVertexXY(state, kVertex4, midpoint_x, bottom);
      SetVertexXY(state, kVertex5, left, bottom);
      SetVertexXY(state, kVertex7, left, edge_y);
      SetVertexXY(state, kVertex8, left, edge_y);
      return;
    }

    const float edge_x = Lerp(midpoint_x, left, progress);
    SetVertexXY(state, kVertex5, edge_x, bottom);
    SetVertexXY(state, kVertex1, right, top);
    SetVertexXY(state, kVertex2, right, midpoint_y);
    SetVertexXY(state, kVertex3, right, bottom);
    SetVertexXY(state, kVertex4, midpoint_x, bottom);
    SetVertexXY(state, kVertex6, edge_x, bottom);
    SetVertexXY(state, kVertex7, edge_x, bottom);
    SetVertexXY(state, kVertex8, edge_x, bottom);
    return;
  }

  if (quadrant >= 5u) {
    SetVertexXY(state, kVertex5, left, Lerp(bottom, midpoint_y, progress));
    CopyVertexTriplet(state, kVertex0, kVertex5);
    CopyVertexTriplet(state, kVertex1, kVertex5);
    CopyVertexTriplet(state, kVertex2, kVertex5);
    CopyVertexTriplet(state, kVertex3, kVertex5);
    CopyVertexTriplet(state, kVertex4, kVertex5);
  } else {
    SetVertexXY(state, kVertex4, Lerp(midpoint_x, left, progress), bottom);
    CopyVertexTriplet(state, kVertex0, kVertex4);
    CopyVertexTriplet(state, kVertex1, kVertex4);
    CopyVertexTriplet(state, kVertex2, kVertex4);
    CopyVertexTriplet(state, kVertex3, kVertex4);
    SetVertexXY(state, kVertex5, left, bottom);
  }

  SetVertexXY(state, kVertex6, left, midpoint_y);
  SetVertexXY(state, kVertex7, left, top);
}

void CGUnit_C_InterpolatePosition_SubC(
    void* self, unsigned int quadrant, float progress) {
  LegacyBubbleGeometryState state(self);
  const float left = state.ReadFloat(kBoundsLeft);
  const float right = state.ReadFloat(kBoundsRight);
  const float top = state.ReadFloat(kBoundsTop);
  const float bottom = state.ReadFloat(kBoundsBottom);
  const float midpoint_x = (left + right) * kHalf;
  const float midpoint_y = (top + bottom) * kHalf;

  if (state.ReadU32(kFanLayoutMode) != 0u) {
    if (quadrant < 7u) {
      SetVertexXY(state, kVertex7, left, Lerp(midpoint_y, top, progress));
      SetVertexXY(state, kVertex1, right, top);
      SetVertexXY(state, kVertex2, right, midpoint_y);
      SetVertexXY(state, kVertex3, right, bottom);
      SetVertexXY(state, kVertex4, midpoint_x, bottom);
      SetVertexXY(state, kVertex5, left, bottom);
      SetVertexXY(state, kVertex6, left, midpoint_y);
      SetVertexXY(state, kVertex8, left, state.ReadFloat(kVertex7 + 4));
      return;
    }

    SetVertexXY(state, kVertex8, Lerp(left, midpoint_x, progress), top);
    SetVertexXY(state, kVertex1, right, top);
    SetVertexXY(state, kVertex2, right, midpoint_y);
    SetVertexXY(state, kVertex3, right, bottom);
    SetVertexXY(state, kVertex4, midpoint_x, bottom);
    SetVertexXY(state, kVertex5, left, bottom);
    SetVertexXY(state, kVertex6, left, midpoint_y);
    SetVertexXY(state, kVertex7, left, top);
    return;
  }

  if (quadrant < 7u) {
    SetVertexXY(state, kVertex6, left, Lerp(midpoint_y, top, progress));
    CopyVertexTriplet(state, kVertex0, kVertex6);
    CopyVertexTriplet(state, kVertex1, kVertex6);
    CopyVertexTriplet(state, kVertex2, kVertex6);
    CopyVertexTriplet(state, kVertex3, kVertex6);
    CopyVertexTriplet(state, kVertex4, kVertex6);
    CopyVertexTriplet(state, kVertex5, kVertex6);
    SetVertexXY(state, kVertex7, left, top);
    return;
  }

  SetVertexXY(state, kVertex7, Lerp(left, midpoint_x, progress), top);
  CopyVertexTriplet(state, kVertex0, kVertex7);
  CopyVertexTriplet(state, kVertex1, kVertex7);
  CopyVertexTriplet(state, kVertex2, kVertex7);
  CopyVertexTriplet(state, kVertex3, kVertex7);
  CopyVertexTriplet(state, kVertex4, kVertex7);
  CopyVertexTriplet(state, kVertex5, kVertex7);
  CopyVertexTriplet(state, kVertex6, kVertex7);
}

void CGUnit_C_InterpolatePosition(void* self) {
  LegacyBubbleGeometryState state(self);
  const float left = state.ReadFloat(kBoundsLeft);
  const float right = state.ReadFloat(kBoundsRight);
  const float top = state.ReadFloat(kBoundsTop);
  const float bottom = state.ReadFloat(kBoundsBottom);
  const float midpoint_x = (left + right) * kHalf;
  const float midpoint_y = (top + bottom) * kHalf;

  if (state.ReadU32(kStaticGeometryMode) == 0u) {
    const auto animation_period = state.ReadU32(kAnimationPeriod);
    const auto animation_elapsed = state.ReadU32(kAnimationElapsed);
    if (animation_period == 0u) {
      return;
    }

    const float frame_duration = static_cast<float>(animation_period) * kFrameSegmentRatio;
    const unsigned int frame_index =
        static_cast<unsigned int>(static_cast<float>(animation_elapsed) / frame_duration);
    const float progress =
        (static_cast<float>(animation_elapsed) -
         frame_duration * static_cast<float>(frame_index)) /
        frame_duration;

    if (state.ReadU32(kFanLayoutMode) != 0u) {
      state.WriteFloat(kVertex0, midpoint_x);
      state.WriteFloat(kVertex0 + 4, top);
      state.WriteFloat(kVertex0 + 8, kOne);
      state.WriteFloat(kVertex0Uv, 0.5f);
      state.WriteFloat(kVertex0Uv + 4, 0.0f);
    } else {
      state.WriteFloat(kVertex8, midpoint_x);
      state.WriteFloat(kVertex8 + 4, top);
      state.WriteFloat(kVertex8 + 8, kOne);
      state.WriteFloat(kVertex8Uv, 0.5f);
      state.WriteFloat(kVertex8Uv + 4, 0.0f);
    }

    state.WriteFloat(kVertex9, midpoint_x);
    state.WriteFloat(kVertex9 + 4, midpoint_y);
    state.WriteFloat(kVertex9 + 8, kOne);
    state.WriteFloat(kVertex9Uv, 0.5f);
    state.WriteFloat(kVertex9Uv + 4, 0.5f);

    if (frame_index < 2u) {
      CGUnit_C_InterpolatePosition_Sub01(self, frame_index, progress);
    } else if (frame_index < 4u) {
      CGUnit_C_InterpolatePosition_SubA(self, frame_index, progress);
    } else if (frame_index < 6u) {
      CGUnit_C_InterpolatePosition_SubB(self, frame_index, progress);
    } else {
      CGUnit_C_InterpolatePosition_SubC(self, frame_index, progress);
    }

    if (state.ReadU32(kDrawEdgeFlag) != 0u) {
      const float angle =
          (static_cast<float>(animation_elapsed) / static_cast<float>(animation_period)) *
          kLegacyTau;
      WriteEdgeUvs(state, state.ReadFloat(kBubbleScale), angle);
    }
    return;
  }

  if (state.ReadU32(kFanLayoutMode) != 0u) {
    state.WriteFloat(kVertex0, midpoint_x);
    state.WriteFloat(kVertex0 + 4, top);
    state.WriteFloat(kVertex0 + 8, kOne);
    state.WriteFloat(kVertex0Uv, 0.5f);
    state.WriteFloat(kVertex0Uv + 4, 0.0f);

    WritePerimeterVertices(state, midpoint_x, midpoint_y, left, right, top, bottom);
    SetVertexXY(state, kVertex8, midpoint_x, top);
    state.WriteFloat(kVertex9, midpoint_x);
    state.WriteFloat(kVertex9 + 4, midpoint_y);
    state.WriteFloat(kVertex9 + 8, kOne);
    state.WriteFloat(kVertex9Uv, 0.5f);
    state.WriteFloat(kVertex9Uv + 4, 0.5f);
    return;
  }

  const auto animation_elapsed = state.ReadU32(kAnimationElapsed);
  const unsigned int frame_index =
      static_cast<unsigned int>(static_cast<float>(animation_elapsed) * kLegacyFrameIndexScale);
  const float frame_progress =
      (static_cast<float>(animation_elapsed) -
       static_cast<float>(frame_index) * kLegacyFrameIndexPeriod) *
      kLegacyFrameIndexScale;
  const float alpha_progress = Lerp(
      kMinimapAlphaSteps[frame_index], kMinimapAlphaSteps[frame_index + 1], frame_progress);
  const auto base_alpha = MultiplyAlphaBytes(
      state.ReadU8(kPrimaryAlpha), state.ReadU8(kSecondaryAlpha));
  state.WriteU8(
      kRotatingAlphaByte,
      static_cast<std::uint8_t>(std::trunc(static_cast<float>(base_alpha) * alpha_progress)));

  const float angle = Lerp(
      kMinimapAngleSteps[frame_index], kMinimapAngleSteps[frame_index + 1], frame_progress);
  const float cosine = std::cos(angle);
  const float sine = std::sin(angle);
  const float negative_half_cosine = cosine * -0.5f;
  const float negative_half_sine = sine * -0.5f;
  const float positive_half_cosine = cosine * 0.5f;
  const float positive_half_sine = sine * 0.5f;

  state.WriteFloat(kRotatingUv0, negative_half_cosine - negative_half_sine);
  state.WriteFloat(kRotatingUv0 + 4, negative_half_sine + negative_half_cosine);
  state.WriteFloat(kRotatingUv1, positive_half_cosine - negative_half_sine);
  state.WriteFloat(kRotatingUv1 + 4, positive_half_sine + negative_half_cosine);
  state.WriteFloat(kRotatingUv2, negative_half_cosine - positive_half_sine);
  state.WriteFloat(kRotatingUv2 + 4, negative_half_sine + positive_half_cosine);
  state.WriteFloat(kRotatingUv3, positive_half_cosine - positive_half_sine);
  state.WriteFloat(kRotatingUv3 + 4, positive_half_cosine + positive_half_sine);

  const float uv_scale = Lerp(
      kMinimapScaleSteps[frame_index], kMinimapScaleSteps[frame_index + 1], frame_progress);
  state.WriteFloat(kRotatingUv0, state.ReadFloat(kRotatingUv0) * uv_scale + 0.5f);
  state.WriteFloat(kRotatingUv0 + 4, state.ReadFloat(kRotatingUv0 + 4) * uv_scale + 0.5f);
  state.WriteFloat(kRotatingUv1, state.ReadFloat(kRotatingUv1) * uv_scale + 0.5f);
  state.WriteFloat(kRotatingUv1 + 4, state.ReadFloat(kRotatingUv1 + 4) * uv_scale + 0.5f);
  state.WriteFloat(kRotatingUv2, state.ReadFloat(kRotatingUv2) * uv_scale + 0.5f);
  state.WriteFloat(kRotatingUv2 + 4, state.ReadFloat(kRotatingUv2 + 4) * uv_scale + 0.5f);
  state.WriteFloat(kRotatingUv3, state.ReadFloat(kRotatingUv3) * uv_scale + 0.5f);
  state.WriteFloat(kRotatingUv3 + 4, state.ReadFloat(kRotatingUv3 + 4) * uv_scale + 0.5f);
}

void CSimpleCooldown_RegisterLayerRenderCallbacks(
    void* self, const int layer_index,
    const LegacyCooldownBaseRenderCallback& base_callback,
    const LegacyCooldownPixelShaderResolver& pixel_shader_resolver,
    std::vector<LegacyCooldownRenderEntry>& out_entries) {
  if (base_callback) {
    base_callback(self, layer_index);
  }

  const auto pixel_shader_handle =
      pixel_shader_resolver ? pixel_shader_resolver(0u) : 0u;
  LegacyBubbleGeometryState state(self);

  if (layer_index == 2) {
    const auto texture_handle = state.ReadU32(kMainTextureHandle);
    if (texture_handle == 0u) {
      return;
    }

    AppendCooldownRenderEntry<10>(
        state, out_entries, texture_handle, 2u, 10u, kVertex0, kVertex0Uv,
        kFillColor, kCooldownSweepIndices, pixel_shader_handle);
    AppendCooldownRenderEntry<4>(
        state, out_entries, texture_handle, 3u, 4u, kArtworkQuadVertices,
        kRotatingUv0, kReadyFlashColor, kCooldownQuadIndices,
        pixel_shader_handle);
    return;
  }

  if (layer_index != 3 || state.ReadU32(kDrawEdgeFlag) == 0u) {
    return;
  }

  const auto edge_texture_handle = state.ReadU32(kEdgeTextureHandle);
  if (edge_texture_handle == 0u) {
    return;
  }

  AppendCooldownRenderEntry<4>(
      state, out_entries, edge_texture_handle, 0u, 4u, kEdgeQuadVertices,
      kEdgeUv0, kEdgeColor, kCooldownQuadIndices, pixel_shader_handle);
}

ResolvedSpellVisualEffect ResolveSpellVisualEffectRecords(
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry& spell,
    const std::int32_t violence_level,
    const bool emit_diagnostics) {
  ResolvedSpellVisualEffect result;

  result.visual_id = spell.spell_visual[0];
  if (violence_level < 2 && spell.spell_visual[1] != 0) {
    result.visual_id = spell.spell_visual[1];
  }

  const auto warn_once = [emit_diagnostics](std::string message) {
    if (!emit_diagnostics) {
      return;
    }
    static std::mutex mutex;
    static std::unordered_set<std::string> reported;
    std::lock_guard lock(mutex);
    if (reported.insert(message).second) {
      diagnostics::Log(diagnostics::LogLevel::kWarn, message);
    }
  };

  if (result.visual_id == 0) {
    return result;
  }

  result.visual = dbc.spell_visual().LookupEntry(result.visual_id);
  if (result.visual == nullptr) {
    warn_once("SPELLVISUALIDNOTFOUND|" + std::to_string(result.visual_id) +
              "|" + std::to_string(spell.id));
    return result;
  }

  result.kit =
      dbc.spell_visual_kit().LookupEntry(result.visual->persistent_area_kit);
  if (result.kit == nullptr) {
    if (result.visual->persistent_area_kit != 0u) {
      warn_once("SPELLVISUALKITIDNOTFOUND|" +
                std::to_string(result.visual->persistent_area_kit));
    }
    return result;
  }

  result.effect =
      dbc.spell_visual_effect_name().LookupEntry(result.kit->world_effect);

  if (result.effect == nullptr) {
    result.effect =
        dbc.spell_visual_effect_name().LookupEntry(result.kit->base_effect);
    if (result.effect == nullptr &&
        (result.kit->world_effect != 0u || result.kit->base_effect != 0u)) {
      warn_once("SPELLEFFECTIDNOTFOUND|" +
                std::to_string(result.kit->world_effect) + "|" +
                std::to_string(result.kit->base_effect));
    }
  }

  return result;
}

const data::dbc::SpellVisualEffectNameEntry* ResolveSpellVisualEffect(
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry& spell,
    const std::int32_t violence_level) {
  return ResolveSpellVisualEffectRecords(dbc, spell, violence_level).effect;
}

}
