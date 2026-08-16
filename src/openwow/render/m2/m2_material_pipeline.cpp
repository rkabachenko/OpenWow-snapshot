#include "openwow/render/m2/m2_material_pipeline.h"

#include "openwow/render/m2/m2_animator.h"
#include "openwow/render/m2/m2_shaders.h"

#include <algorithm>

namespace openwow::render::m2 {

namespace {

constexpr std::uint16_t kM2TextureUnitModeNoTexture = 0u;

}

bool TextureUnitMatchesDrawBatchScope(const data::model::SkinTextureUnit &texture_unit,
                                      const M2DrawBatchScope scope) noexcept {
  switch (scope) {
  case M2DrawBatchScope::kAllTextureUnits:
    return true;
  case M2DrawBatchScope::kWorldPrimaryTextureUnits:
    return texture_unit.tex_unit_number == 0u &&
           (texture_unit.flags & kM2TextureUnitFlagSecondaryWorldPass) == 0u;
  }

  return false;
}

bool M2ModelNeedsMaterialAnimator(const data::model::M2Model &model) noexcept {
  return !model.colors.empty() || !model.transparencies.empty() ||
         !model.uv_animations.empty();
}

M2TransparentSortMode
ResolveM2TransparentSortMode(const std::uint32_t model_global_flags) noexcept {

  return (model_global_flags & kM2GlobalFlagSpecialTextureUnitSort) != 0u
             ? M2TransparentSortMode::kSpecialPassPriority
             : M2TransparentSortMode::kStandard;
}

M2TextureUnitMaterialSlots ResolveM2TextureUnitMaterialSlots(
    const data::model::M2Model &model,
    const data::model::SkinTextureUnit &texture_unit) noexcept {
  M2TextureUnitMaterialSlots slots;

  if (texture_unit.mode != kM2TextureUnitModeNoTexture) {
    std::size_t resolved_transparency = static_cast<std::size_t>(texture_unit.transparency_index);
    if (resolved_transparency < model.transparency_lookup.size()) {
      resolved_transparency =
          static_cast<std::size_t>(model.transparency_lookup[resolved_transparency]);
    }
    if (resolved_transparency < model.transparencies.size()) {
      slots.transparency = resolved_transparency;
    }
  }
  if (static_cast<std::size_t>(texture_unit.color_index) < model.colors.size()) {
    slots.color = static_cast<std::size_t>(texture_unit.color_index);
  }
  return slots;
}

M2TextureUnitMaterialSample SampleM2MaterialSlots(
    const M2TextureUnitMaterialSlots slots, const M2Animator *const animator,
    const int animation_index, const std::uint32_t animation_time_ms, const float model_alpha,
    const std::optional<RenderVec4> &tint_color) {
  M2TextureUnitMaterialSample sample{};
  sample.color[3] = model_alpha;

  if (animator != nullptr) {
    if (slots.transparency != kNoM2MaterialSlot) {
      sample.transparency_alpha = animator->SampleAlpha(static_cast<int>(slots.transparency),
                                                        animation_index, animation_time_ms);
    }

    if (slots.color != kNoM2MaterialSlot) {
      const auto color =
          animator->SampleColor(static_cast<int>(slots.color), animation_index, animation_time_ms);
      sample.color[0] = color.r;
      sample.color[1] = color.g;
      sample.color[2] = color.b;
      sample.color_alpha = color.a;
    }
  }

  if (tint_color.has_value()) {
    sample.color[0] *= (*tint_color)[0];
    sample.color[1] *= (*tint_color)[1];
    sample.color[2] *= (*tint_color)[2];
    sample.color[3] *= (*tint_color)[3];
  }

  sample.color[3] *= sample.transparency_alpha * sample.color_alpha;
  return sample;
}

M2TextureUnitMaterialSample SampleM2TextureUnitMaterial(
    const data::model::M2Model &model, const data::model::SkinTextureUnit &texture_unit,
    const M2Animator *animator, const int animation_index, const std::uint32_t animation_time_ms,
    const float model_alpha, const std::optional<RenderVec4> &tint_color) {

  return SampleM2MaterialSlots(ResolveM2TextureUnitMaterialSlots(model, texture_unit), animator,
                               animation_index, animation_time_ms, model_alpha, tint_color);
}

void M2SubmissionMaterialCache::BeginSubmission(
    const data::model::M2Model &model, const M2Animator *const animator,
    const int animation_index, const std::uint32_t animation_time_ms, const float model_alpha,
    const std::optional<RenderVec4> &tint_color) {
  model_ = &model;
  animator_ = animator;
  animation_index_ = animation_index;
  animation_time_ms_ = animation_time_ms;
  model_alpha_ = model_alpha;
  tint_color_ = tint_color;
  used_ = 0;
}

M2TextureUnitMaterialSample M2SubmissionMaterialCache::Sample(
    const data::model::SkinTextureUnit &texture_unit) {
  const M2TextureUnitMaterialSlots slots =
      ResolveM2TextureUnitMaterialSlots(*model_, texture_unit);
  for (std::size_t slot = 0; slot < used_; ++slot) {
    if (entries_[slot].slots == slots) {
      return entries_[slot].sample;
    }
  }
  const M2TextureUnitMaterialSample sample =
      SampleM2MaterialSlots(slots, animator_, animation_index_, animation_time_ms_, model_alpha_,
                            tint_color_);
  if (used_ == entries_.size()) {
    entries_.push_back(Entry{});
  }
  entries_[used_] = Entry{.slots = slots, .sample = sample};
  ++used_;
  return sample;
}

std::optional<std::uint32_t>
ResolveM2TextureUnitPassGroupForBlendMode(const std::uint32_t blend_mode) noexcept {
  if (blend_mode < kM2BlendModeToPassGroupCount) {
    return kM2BlendModeToPassGroup[blend_mode];
  }
  return std::nullopt;
}

bool BuildM2TransparentSortKeysInto(
    const std::span<const M2TextureUnitSortInput> inputs,
    const M2TransparentSortMode mode,
    std::vector<M2TextureUnitSortKey> *const out_keys) {
  std::vector<M2TextureUnitSortKey> &keys = *out_keys;

  keys.clear();
  keys.reserve(inputs.size());

  for (std::size_t input_index = 0; input_index < inputs.size(); ++input_index) {
    const auto &input = inputs[input_index];
    M2TextureUnitSortKey key{};
    static_cast<M2TextureUnitSortInput &>(key) = input;
    key.input_index = input_index;
    const auto pass_group = ResolveM2TextureUnitPassGroupForBlendMode(input.blend_mode);
    if (!pass_group.has_value()) {
      return false;
    }
    key.pass_group = *pass_group;

    keys.push_back(key);
  }

  if (mode == M2TransparentSortMode::kSpecialPassPriority) {

    std::stable_sort(keys.begin(), keys.end(), CompareM2TransparentSortKeys);

    std::uint32_t pass_priority = 0u;
    bool prev_was_transparent = false;
    for (auto &key : keys) {
      const bool is_transparent = key.pass_group == kM2TransparentPassGroupA ||
                                  key.pass_group == kM2TransparentPassGroupB;
      if (!is_transparent || !prev_was_transparent) {
        ++pass_priority;
      }
      key.pass_priority = pass_priority;
      prev_was_transparent = is_transparent;
    }
  }

  return true;
}

std::optional<std::vector<M2TextureUnitSortKey>>
BuildM2TransparentSortKeys(const std::vector<M2TextureUnitSortInput> &inputs,
                           const M2TransparentSortMode mode) {
  std::vector<M2TextureUnitSortKey> keys;
  if (!BuildM2TransparentSortKeysInto(inputs, mode, &keys)) {
    return std::nullopt;
  }
  return keys;
}

bool CompareM2TransparentSortKeys(const M2TextureUnitSortKey &lhs,
                                  const M2TextureUnitSortKey &rhs) noexcept {
  if (lhs.pass_priority != rhs.pass_priority) {
    return lhs.pass_priority < rhs.pass_priority;
  }
  if (lhs.priority_plane != rhs.priority_plane) {
    return lhs.priority_plane < rhs.priority_plane;
  }
  if ((lhs.texture_unit_order_key & 1u) != (rhs.texture_unit_order_key & 1u)) {
    return (lhs.texture_unit_order_key & 1u) < (rhs.texture_unit_order_key & 1u);
  }
  if (lhs.geoset_id != rhs.geoset_id) {
    return lhs.geoset_id < rhs.geoset_id;
  }
  if (lhs.sort_distance != rhs.sort_distance) {
    return lhs.sort_distance > rhs.sort_distance;
  }
  if (lhs.render_pass_order_key != rhs.render_pass_order_key) {
    return lhs.render_pass_order_key < rhs.render_pass_order_key;
  }
  if (lhs.type != rhs.type) {
    return lhs.type < rhs.type;
  }
  if (lhs.texture_unit_order_key != rhs.texture_unit_order_key) {
    return lhs.texture_unit_order_key < rhs.texture_unit_order_key;
  }
  if (lhs.texture_unit_index != rhs.texture_unit_index) {
    return lhs.texture_unit_index < rhs.texture_unit_index;
  }
  return lhs.input_index < rhs.input_index;
}

float ComputeM2TextureUnitSortDistance(const data::model::SkinSubmesh &submesh,
                                       const RenderMatrix4x4 &model_mtx,
                                       const std::optional<RenderMatrix4x4View> view_mtx) noexcept {
  float x = submesh.sort_pos[0];
  float y = submesh.sort_pos[1];
  float z = submesh.sort_pos[2];

  const float world_x = model_mtx[0] * x + model_mtx[4] * y + model_mtx[8] * z + model_mtx[12];
  const float world_y = model_mtx[1] * x + model_mtx[5] * y + model_mtx[9] * z + model_mtx[13];
  const float world_z = model_mtx[2] * x + model_mtx[6] * y + model_mtx[10] * z + model_mtx[14];
  x = world_x;
  y = world_y;
  z = world_z;

  if (view_mtx.has_value()) {
    const RenderMatrix4x4View view = *view_mtx;
    return view[2] * x + view[6] * y + view[10] * z + view[14];
  }

  return 0.0f;
}

}
