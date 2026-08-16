
#pragma once

#include "openwow/render/api/math/render_math_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::render {

struct ModelCallbackTransform {
  static constexpr std::size_t kMatrixFloatCount = 16;
  static constexpr std::size_t kRecordFloatCount = 27;
  static constexpr std::size_t kRecordByteSize = kRecordFloatCount * sizeof(float);

  std::array<float, kRecordFloatCount> values{};

  [[nodiscard]] RenderMatrix4x4 Matrix4x4() const noexcept;

  static ModelCallbackTransform FromMatrix4x4(const RenderMatrix4x4& matrix);
};

struct ModelRenderCallbackSlot {
  using Fn = void (*)(void* model_instance, class ModelRenderCallbackContext& render_ctx,
                      const void* user_ctx);
  Fn fn = nullptr;
  const void* ctx = nullptr;
};

struct ModelRenderCallbackGhostLightSample {
  bool enabled{false};
  std::array<float, 3> ambient_rgb{};
  std::array<float, 3> diffuse_rgb{};
};

enum class ModelRenderCallbackPointLightAttenuation : std::uint8_t {

  kPolynomial,

  kRange,
};

struct ModelRenderCallbackPointLightState {
  std::array<float, 3> position{};
  std::array<float, 3> diffuse_rgb{};
  std::array<float, 3> attenuation{};
  ModelRenderCallbackPointLightAttenuation attenuation_model{
      ModelRenderCallbackPointLightAttenuation::kPolynomial};
};

enum class ModelRenderCallbackFogMode : std::uint8_t {
  kInherit,
  kDisabled,
  kOverride,
};

struct ModelRenderCallbackLightingState {
  bool has_directional_light{false};
  std::array<float, 3> ambient_rgb{};
  std::array<float, 3> diffuse_rgb{};
  std::array<float, 3> direction{};

  ModelRenderCallbackFogMode fog_mode{ModelRenderCallbackFogMode::kInherit};
  float fog_start{0.0f};
  float fog_end{0.0f};
  float fog_inverse_range{0.0f};
  float fog_blend_scale{0.0f};
  std::array<float, 3> fog_color_rgb{};

  std::vector<ModelRenderCallbackPointLightState> point_lights;
};

struct ModelRenderCallbackPipelineOutput {
  std::vector<ModelCallbackTransform> transforms;
  std::optional<ModelRenderCallbackLightingState> lighting_state;
};

class ModelRenderCallbackContext {
 public:
  void ResetTransformSubmissions();
  void SubmitTransform(const ModelCallbackTransform& transform);

  void SetBaseTransformMatrix4x4(const RenderMatrix4x4& matrix);
  void SubmitBaseTransform();

  void ClearLightingState();
  void SetLightingState(ModelRenderCallbackLightingState state);
  [[nodiscard]] const ModelRenderCallbackLightingState* lighting_state() const noexcept {
    return lighting_state_.has_value() ? &lighting_state_.value() : nullptr;
  }

  void SetSelectedCharacterGhost(const bool value) noexcept {
    selected_character_is_ghost_ = value;
  }
  [[nodiscard]] bool selected_character_is_ghost() const noexcept {
    return selected_character_is_ghost_;
  }

  void SetGhostLightSample(const ModelRenderCallbackGhostLightSample& sample) noexcept {
    ghost_light_sample_ = sample;
  }
  [[nodiscard]] const ModelRenderCallbackGhostLightSample& ghost_light_sample() const noexcept {
    return ghost_light_sample_;
  }

  [[nodiscard]] std::size_t submitted_count() const noexcept {
    return submitted_.size();
  }
  [[nodiscard]] const std::vector<ModelCallbackTransform>& submitted() const noexcept {
    return submitted_;
  }

 private:
  ModelCallbackTransform base_transform_{};
  bool have_base_transform_{false};
  std::vector<ModelCallbackTransform> submitted_;
  std::optional<ModelRenderCallbackLightingState> lighting_state_;
  ModelRenderCallbackGhostLightSample ghost_light_sample_{};
  bool selected_character_is_ghost_{false};
};

ModelRenderCallbackPipelineOutput RunModelRenderCallbackPipeline(
    void* model_instance,
    const ModelRenderCallbackSlot& slot,
    const RenderMatrix4x4& base_transform_matrix,
    bool selected_character_is_ghost = false,
    const ModelRenderCallbackGhostLightSample* ghost_light_sample = nullptr,
    const ModelRenderCallbackLightingState* scene_lighting_state = nullptr);

}
