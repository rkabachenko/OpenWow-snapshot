#pragma once

#include "openwow/data/model/m2_model.h"
#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/api/math/render_math_types.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openwow::render::m2 {

class M2Animator {
public:
  M2Animator() = default;
  explicit M2Animator(const openwow::data::model::M2Model *model) : model_(model) {}

  void BindModel(const openwow::data::model::M2Model *model) {
    model_ = model;
  }

  [[nodiscard]] std::optional<std::vector<float>>
  ComputeBoneMatrices(int animation_index, std::uint32_t time_ms,
                      const std::optional<RenderMatrix4x4View> &camera_inverse_view =
                          std::nullopt) const;
  [[nodiscard]] std::optional<std::vector<float>>
  ComputeBlendedBoneMatrices(int animation_index, std::uint32_t time_ms,
                             int blend_source_animation_index,
                             std::uint32_t blend_source_time_ms, float blend_factor,
                             const std::optional<RenderMatrix4x4View> &camera_inverse_view =
                                 std::nullopt) const;

  [[nodiscard]] std::optional<std::vector<float>> ComputeLayeredBoneMatrices(
      int animation_index, std::uint32_t time_ms,
      std::span<const M2AnimationSlotState> animation_slots,
      std::optional<int> blend_source_animation_index = std::nullopt,
      std::uint32_t blend_source_time_ms = 0u, float blend_factor = 1.0f,
      const std::optional<RenderMatrix4x4View> &camera_inverse_view = std::nullopt,
      std::span<const M2BoneBasisOverride> bone_basis_overrides = {}) const;

  [[nodiscard]] bool ComputeLayeredBoneMatricesInto(
      std::vector<float> *out, int animation_index, std::uint32_t time_ms,
      std::span<const M2AnimationSlotState> animation_slots,
      std::optional<int> blend_source_animation_index = std::nullopt,
      std::uint32_t blend_source_time_ms = 0u, float blend_factor = 1.0f,
      const std::optional<RenderMatrix4x4View> &camera_inverse_view = std::nullopt,
      std::span<const M2BoneBasisOverride> bone_basis_overrides = {}) const;

  [[nodiscard]] std::optional<RenderMatrix4x4> ComputeSingleBoneMatrix(
      std::size_t bone_index, int animation_index, std::uint32_t time_ms,
      std::span<const M2AnimationSlotState> animation_slots,
      std::optional<int> blend_source_animation_index = std::nullopt,
      std::uint32_t blend_source_time_ms = 0u, float blend_factor = 1.0f,
      const std::optional<RenderMatrix4x4View> &camera_inverse_view = std::nullopt,
      std::span<const M2BoneBasisOverride> bone_basis_overrides = {}) const;

  std::optional<M2CameraPose> SampleCamera(int camera_index, int animation_index,
                                           std::uint32_t time_ms) const;
  std::optional<M2CameraPose> SampleBlendedCamera(
      int camera_index, int animation_index, std::uint32_t time_ms,
      int blend_source_animation_index, std::uint32_t blend_source_time_ms,
      float blend_factor) const;

  std::optional<M2UvTransform> SampleUvTransform(int uv_animation_index, int animation_index,
                                                 std::uint32_t time_ms) const;

  float SampleAlpha(int transparency_index, int animation_index, std::uint32_t time_ms) const;

  M2ColorSample SampleColor(int color_index, int animation_index, std::uint32_t time_ms) const;

  M2LightSample SampleLight(int light_index, int animation_index, std::uint32_t time_ms,
                            std::span<const float> bone_matrices) const;

  std::optional<M2RibbonEmitterSample>
  SampleRibbonEmitter(std::size_t ribbon_index, int animation_index, std::uint32_t time_ms) const;

  std::optional<M2ParticleEmitterSample> SampleParticleEmitter(std::size_t emitter_index,
                                                               int animation_index,
                                                               std::uint32_t time_ms) const;

  std::vector<M2TriggeredEvent> CollectTriggeredEvents(
      int animation_index, std::uint32_t previous_time_ms, std::uint32_t current_time_ms,
      std::span<const float> bone_matrices = {},
      const std::optional<RenderMatrix4x4> &model_matrix = std::nullopt) const;

private:
  const openwow::data::model::M2Model *model_{nullptr};
};

}
