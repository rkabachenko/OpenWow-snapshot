
#include "openwow/render/models/animation/model_render_callback_pipeline.h"

#include <algorithm>
#include <utility>

namespace openwow::render {

RenderMatrix4x4 ModelCallbackTransform::Matrix4x4() const noexcept {
  RenderMatrix4x4 matrix{};
  std::copy_n(values.begin(), kMatrixFloatCount, matrix.begin());
  return matrix;
}

ModelCallbackTransform ModelCallbackTransform::FromMatrix4x4(
    const RenderMatrix4x4& matrix) {
  ModelCallbackTransform out;
  std::copy(matrix.begin(), matrix.end(), out.values.begin());
  return out;
}

void ModelRenderCallbackContext::ResetTransformSubmissions() {
  submitted_.clear();
}

void ModelRenderCallbackContext::SubmitTransform(const ModelCallbackTransform& transform) {
  submitted_.push_back(transform);
}

void ModelRenderCallbackContext::SetBaseTransformMatrix4x4(
    const RenderMatrix4x4& matrix) {
  base_transform_ = ModelCallbackTransform::FromMatrix4x4(matrix);
  have_base_transform_ = true;
}

void ModelRenderCallbackContext::SubmitBaseTransform() {
  if (!have_base_transform_) return;
  submitted_.push_back(base_transform_);
}

void ModelRenderCallbackContext::ClearLightingState() {
  lighting_state_.reset();
}

void ModelRenderCallbackContext::SetLightingState(ModelRenderCallbackLightingState state) {
  lighting_state_ = std::move(state);
}

ModelRenderCallbackPipelineOutput RunModelRenderCallbackPipeline(
    void* model_instance,
    const ModelRenderCallbackSlot& slot,
    const RenderMatrix4x4& base_transform_matrix,
    const bool selected_character_is_ghost,
    const ModelRenderCallbackGhostLightSample* ghost_light_sample,
    const ModelRenderCallbackLightingState* scene_lighting_state) {
  ModelRenderCallbackContext ctx;
  ctx.SetBaseTransformMatrix4x4(base_transform_matrix);
  ctx.SetSelectedCharacterGhost(selected_character_is_ghost);

  if (scene_lighting_state != nullptr) {
    ctx.SetLightingState(*scene_lighting_state);
  }
  if (ghost_light_sample != nullptr) {
    ctx.SetGhostLightSample(*ghost_light_sample);
  }

  if (slot.fn) {
    slot.fn(model_instance, ctx, slot.ctx);
  }

  if (ctx.submitted_count() == 0) {
    ctx.SubmitBaseTransform();
  }

  ModelRenderCallbackPipelineOutput out;
  out.transforms = ctx.submitted();
  if (const auto* lighting_state = ctx.lighting_state(); lighting_state != nullptr) {
    out.lighting_state = *lighting_state;
  }
  return out;
}

}
