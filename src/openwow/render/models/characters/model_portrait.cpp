#include "openwow/render/models/characters/model_portrait.h"

#include "openwow/foundation/math/projection_aspect.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace openwow::render {
namespace {

[[nodiscard]] ModelPortraitResult MakeResult(
    const m2::M2ResultStatus status,
    const m2::M2ResultReason reason = m2::M2ResultReason::kNone,
    std::string detail = {}, const std::uint32_t submitted_draw_count = 0u) {
  return {
      .status = status,
      .reason = reason,
      .detail = std::move(detail),
      .submitted_draw_count = submitted_draw_count,
  };
}

void BuildSimpleModelOrthographicViewProjection(
    const SimpleModelOrthographicFrame& frame, float* const view_mtx,
    float* const proj_mtx) {

  constexpr float kMinimumRectExtent = 1.0e-6f;
  const float half_width =
      std::max(frame.rect_width, kMinimumRectExtent) * 0.5f;
  const float half_height =
      std::max(frame.rect_height, kMinimumRectExtent) * 0.5f;

  const float view[16] = {
      1.0f, 0.0f, 0.0f,  0.0f,
      0.0f, 1.0f, 0.0f,  0.0f,
      0.0f, 0.0f, -1.0f, 0.0f,
      -half_width, -half_height, 0.0f, 1.0f,
  };
  std::copy(std::begin(view), std::end(view), view_mtx);
  bx::mtxOrtho(proj_mtx, -half_width, half_width, -half_height, half_height,
               kSimpleModelOrthoNear, kSimpleModelOrthoFar, 0.0f,
               bgfx::getCaps()->homogeneousDepth, bx::Handedness::Left);
}

}

ModelPortrait::~ModelPortrait() {
  Shutdown();
}

bool ModelPortrait::Initialize(const std::uint16_t width,
                               const std::uint16_t height) {
  if (initialized_) {
    return true;
  }

  width_ = std::max(width, static_cast<std::uint16_t>(16));
  height_ = std::max(height, static_cast<std::uint16_t>(16));
  CreateFrameBuffer();
  if (!bgfx::isValid(fb_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                       "ModelPortrait: framebuffer creation failed");
    return false;
  }

  initialized_ = true;
  return true;
}

void ModelPortrait::Shutdown() {
  visual_clone_ = {};
  stable_camera_pose_.reset();
  stable_camera_missing_ = false;
  animation_applied_revision_ = 0u;
  DestroyOwnedSource();
  DestroyFrameBuffer();
  has_presented_content_ = false;
  initialized_ = false;
}

void ModelPortrait::SetSourceInstance(const std::uint32_t source_instance_id,
                                      const std::uint64_t visual_revision) {
  if (owned_instance_id_ != 0u && source_instance_id != owned_instance_id_) {
    DestroyOwnedSource();
    owned_model_path_.clear();
  }
  if (source_instance_id_ != source_instance_id ||
      source_visual_revision_ != visual_revision) {
    visual_clone_ = {};
    stable_camera_pose_.reset();
    stable_camera_missing_ = false;
    animation_applied_revision_ = 0u;
  }
  source_instance_id_ = source_instance_id;
  source_visual_revision_ = visual_revision;
}

void ModelPortrait::SetModelPath(std::string model_path) {
  SetModelPath(std::move(model_path), {}, std::nullopt);
}

void ModelPortrait::SetModelPath(
    std::string model_path,
    std::array<std::string, 3> display_texture_paths,
    std::optional<m2::M2ParticleColorRecord> display_particle_colors) {

  if (owned_model_path_ == model_path &&
      owned_display_texture_paths_ == display_texture_paths &&
      owned_display_particle_colors_ == display_particle_colors) {
    return;
  }
  visual_clone_ = {};
  stable_camera_pose_.reset();
  stable_camera_missing_ = false;
  animation_applied_revision_ = 0u;
  DestroyOwnedSource();
  source_instance_id_ = 0u;
  source_visual_revision_ = 0u;
  owned_model_path_ = std::move(model_path);
  owned_display_texture_paths_ = std::move(display_texture_paths);
  owned_display_particle_colors_ = std::move(display_particle_colors);
}

void ModelPortrait::DestroyOwnedSource() {
  const std::uint32_t destroyed_instance_id = owned_instance_id_;
  if (owned_instance_id_ != 0u) {
    (void)m2_system_.DestroyInstance(owned_instance_id_);
  }
  owned_instance_id_ = 0u;
  if (source_instance_id_ != 0u &&
      source_instance_id_ == destroyed_instance_id) {
    source_instance_id_ = 0u;
    source_visual_revision_ = 0u;
  }
}

ModelPortraitResult ModelPortrait::EnsureOwnedSource() {
  if (owned_model_path_.empty()) {
    return MakeResult(m2::M2ResultStatus::kFailed,
                      m2::M2ResultReason::kInvalidHandle,
                      "model path is empty");
  }
  if (owned_instance_id_ == 0u) {
    const auto loaded = m2_system_.LoadModelInstanceWithFallback(
        owned_model_path_);
    if (loaded.status != m2::M2ResultStatus::kReady ||
        loaded.instance_id == 0u) {
      return MakeResult(loaded.status, loaded.reason, loaded.detail);
    }
    owned_instance_id_ = loaded.instance_id;

    const bool has_display_overrides =
        owned_display_particle_colors_.has_value() ||
        std::any_of(owned_display_texture_paths_.begin(),
                    owned_display_texture_paths_.end(),
                    [](const std::string& path) { return !path.empty(); });
    if (has_display_overrides) {
      const auto override_status =
          m2_system_.ApplyCreatureDisplayRecordOverrides(
              owned_instance_id_, owned_display_texture_paths_,
              owned_display_particle_colors_);
      if (override_status != m2::M2ResultStatus::kReady) {
        DestroyOwnedSource();
        return MakeResult(override_status,
                          m2::M2ResultReason::kNone,
                          "creature display record overrides failed");
      }
    }
  }
  const auto revision = m2_system_.QueryVisualTreeRevision(
      owned_instance_id_);
  if (revision.status != m2::M2ResultStatus::kReady ||
      revision.revision == 0u) {
    return MakeResult(revision.status, revision.reason, revision.detail);
  }
  source_instance_id_ = owned_instance_id_;
  source_visual_revision_ = revision.revision;
  return MakeResult(m2::M2ResultStatus::kReady);
}

ModelPortraitResult ModelPortrait::SynchronizeVisualClone() {
  if (source_instance_id_ == 0u && !owned_model_path_.empty()) {
    const auto source = EnsureOwnedSource();
    if (source.status != m2::M2ResultStatus::kReady) {
      return source;
    }
  }
  if (source_instance_id_ == 0u || source_visual_revision_ == 0u) {
    return MakeResult(m2::M2ResultStatus::kFailed,
                      m2::M2ResultReason::kInvalidHandle,
                      "portrait source identity is incomplete");
  }
  if (visual_clone_.valid() &&
      visual_clone_.visual_revision() == source_visual_revision_) {
    return MakeResult(m2::M2ResultStatus::kReady);
  }

  auto clone = m2_system_.CreateVisualClone(
      source_instance_id_, source_visual_revision_);
  if (clone.status != m2::M2ResultStatus::kReady || !clone.lease.valid()) {
    return MakeResult(clone.status, clone.reason, std::move(clone.detail));
  }

  visual_clone_ = std::move(clone.lease);
  stable_camera_pose_.reset();
  stable_camera_missing_ = false;
  animation_applied_revision_ = 0u;
  return MakeResult(m2::M2ResultStatus::kReady);
}

void ModelPortrait::SetCamera(const float yaw, const float pitch,
                              const float distance) {
  cam_yaw_ = yaw;
  cam_pitch_ = pitch;
  cam_distance_ = std::max(distance, 0.1f);
}

void ModelPortrait::SetCameraIndex(const std::int32_t camera_index) {
  const std::int32_t clamped = std::max(0, camera_index);
  if (!select_camera_by_type_ && camera_index_ == clamped) {
    return;
  }
  camera_index_ = clamped;
  select_camera_by_type_ = false;
  stable_camera_pose_.reset();
  stable_camera_missing_ = false;
}

void ModelPortrait::SetCameraType(const std::uint32_t camera_type) {
  if (select_camera_by_type_ && camera_type_ == camera_type) {
    return;
  }
  camera_type_ = camera_type;
  select_camera_by_type_ = true;
  stable_camera_pose_.reset();
  stable_camera_missing_ = false;
}

void ModelPortrait::SetCameraTarget(const float x, const float y,
                                    const float z) {
  cam_target_[0] = x;
  cam_target_[1] = y;
  cam_target_[2] = z;
}

void ModelPortrait::SetModelTransform(const float scale, const float x,
                                      const float y, const float z) noexcept {
  model_scale_ = std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
  model_position_[0] = std::isfinite(x) ? x : 0.0f;
  model_position_[1] = std::isfinite(y) ? y : 0.0f;
  model_position_[2] = std::isfinite(z) ? z : 0.0f;
}

void ModelPortrait::SetAnimation(
    const std::uint32_t animation_id,
    const std::optional<std::uint32_t> time_ms) {
  if (has_animation_override_ && animation_id_ == animation_id &&
      animation_time_ms_ == time_ms) {
    return;
  }
  animation_id_ = animation_id;
  animation_time_ms_ = time_ms;
  has_animation_override_ = true;
  animation_applied_revision_ = 0u;
  stable_camera_pose_.reset();
  stable_camera_missing_ = false;
}

void ModelPortrait::ClearAnimationOverride() {
  if (!has_animation_override_) {
    return;
  }
  has_animation_override_ = false;
  animation_time_ms_.reset();
  animation_applied_revision_ = 0u;
  visual_clone_ = {};
  stable_camera_pose_.reset();
  stable_camera_missing_ = false;
}

ModelPortraitResult ModelPortrait::RenderToTexture(const bgfx::ViewId view_id) {
  if (!initialized_ || !bgfx::isValid(fb_)) {
    return MakeResult(m2::M2ResultStatus::kNotReady,
                      m2::M2ResultReason::kGpuGeometryNotReady,
                      "portrait framebuffer is not ready");
  }

  const ModelPortraitResult setup = SynchronizeVisualClone();
  if (setup.status != m2::M2ResultStatus::kReady) {
    return setup;
  }

  if (has_animation_override_ &&
      animation_applied_revision_ != visual_clone_.visual_revision()) {
    const auto animation_status = animation_time_ms_.has_value()
        ? m2_system_.SetVisualCloneAnimationSample(
              visual_clone_, animation_id_, *animation_time_ms_)
        : m2_system_.SetVisualCloneAnimation(
              visual_clone_, animation_id_);
    if (animation_status != m2::M2ResultStatus::kReady) {
      return MakeResult(animation_status,
                        m2::M2ResultReason::kInvalidQuery,
                        "portrait animation override was rejected");
    }
    animation_applied_revision_ = visual_clone_.visual_revision();
  }

  float view_mtx[16]{};
  float proj_mtx[16]{};
  const ModelPortraitResult camera =
      ComputeViewProjection(view_mtx, proj_mtx);
  if (camera.status != m2::M2ResultStatus::kReady) {
    return camera;
  }

  bgfx::setViewFrameBuffer(view_id, fb_);
  bgfx::setViewRect(view_id, 0, 0, width_, height_);
  bgfx::setViewClear(view_id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     0x00000000, 1.0f, 0);
  bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);
  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

  float matrix_scale = model_scale_;
  float matrix_position[3] = {model_position_[0], model_position_[1],
                              model_position_[2]};
  if (simple_model_orthographic_frame_.has_value() &&
      !stable_camera_pose_.has_value()) {
    const auto& frame = *simple_model_orthographic_frame_;
    matrix_scale = frame.frame_scale * model_scale_ *
                   (frame.ui_vertical_stretch / kSimpleModelGlobalScaleDivisor);
    for (float& component : matrix_position) {
      component *= frame.frame_scale;
    }
  }
  const float facing_cos = std::cos(model_rotation_);
  const float facing_sin = std::sin(model_rotation_);
  const RenderMatrix4x4 model_matrix{
      facing_cos * matrix_scale,  facing_sin * matrix_scale, 0.0f,         0.0f,
      -facing_sin * matrix_scale, facing_cos * matrix_scale, 0.0f,         0.0f,
      0.0f,                       0.0f,                      matrix_scale, 0.0f,
      matrix_position[0],         matrix_position[1],        matrix_position[2], 1.0f,
  };
  const auto render = m2_system_.RenderVisualClone(
      view_id, visual_clone_, model_matrix, RenderMatrix4x4View{view_mtx, 16u});
  bgfx::touch(view_id);
  if (render.status == m2::M2ResultStatus::kReady &&
      render.submitted_draw_count != 0u) {
    std::swap(fb_, presented_fb_);
    std::swap(color_tex_, presented_color_tex_);
    std::swap(depth_tex_, presented_depth_tex_);
    has_presented_content_ = true;
  }
  return MakeResult(render.status, render.reason, render.detail,
                    render.submitted_draw_count);
}

void ModelPortrait::Resize(std::uint16_t width, std::uint16_t height) {
  width = std::max(width, static_cast<std::uint16_t>(16));
  height = std::max(height, static_cast<std::uint16_t>(16));
  if (width == width_ && height == height_) {
    return;
  }

  width_ = width;
  height_ = height;
  if (initialized_) {
    DestroyFrameBuffer();
    CreateFrameBuffer();
    has_presented_content_ = false;
  }
}

void ModelPortrait::CreateFrameBuffer() {
  constexpr std::uint64_t kColorFlags =
      BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
  const auto create_target = [&](bgfx::FrameBufferHandle& framebuffer,
                                 bgfx::TextureHandle& color,
                                 bgfx::TextureHandle& depth) {
    color = bgfx::createTexture2D(width_, height_, false, 1,
                                  bgfx::TextureFormat::RGBA8, kColorFlags);
    depth = bgfx::createTexture2D(width_, height_, false, 1,
                                  bgfx::TextureFormat::D24S8,
                                  BGFX_TEXTURE_RT);
    if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
      return false;
    }
    bgfx::TextureHandle attachments[2] = {color, depth};
    framebuffer = bgfx::createFrameBuffer(2, attachments, false);
    return bgfx::isValid(framebuffer);
  };
  if (!create_target(fb_, color_tex_, depth_tex_) ||
      !create_target(presented_fb_, presented_color_tex_,
                     presented_depth_tex_)) {
    DestroyFrameBuffer();
  }
}

void ModelPortrait::DestroyFrameBuffer() {
  const auto destroy_target = [](bgfx::FrameBufferHandle& framebuffer,
                                 bgfx::TextureHandle& color,
                                 bgfx::TextureHandle& depth) {
    if (bgfx::isValid(framebuffer)) bgfx::destroy(framebuffer);
    if (bgfx::isValid(color)) bgfx::destroy(color);
    if (bgfx::isValid(depth)) bgfx::destroy(depth);
    framebuffer = BGFX_INVALID_HANDLE;
    color = BGFX_INVALID_HANDLE;
    depth = BGFX_INVALID_HANDLE;
  };
  destroy_target(fb_, color_tex_, depth_tex_);
  destroy_target(presented_fb_, presented_color_tex_,
                 presented_depth_tex_);
}

ModelPortraitResult ModelPortrait::ComputeViewProjection(
    float* const view_mtx, float* const proj_mtx) {
  if (view_mtx == nullptr || proj_mtx == nullptr || !visual_clone_.valid()) {
    return MakeResult(m2::M2ResultStatus::kFailed,
                      m2::M2ResultReason::kInvalidQuery,
                      "portrait camera has no visual clone");
  }

  auto& system = m2_system_;
  const float aspect = openwow::math::projection::ComputeAspectPx(
      static_cast<std::int32_t>(width_), static_cast<std::int32_t>(height_));

  if (!stable_camera_pose_.has_value() && !stable_camera_missing_) {
    const auto model_camera = select_camera_by_type_
                                  ? system.QueryVisualCloneCameraByType(
                                        visual_clone_, camera_type_)
                                  : system.QueryVisualCloneCamera(
                                        visual_clone_, camera_index_);
    if (model_camera.status == m2::M2ResultStatus::kReady) {
      stable_camera_pose_ = model_camera.pose;
    } else if (model_camera.status == m2::M2ResultStatus::kNotReady) {
      return MakeResult(model_camera.status, model_camera.reason,
                        model_camera.detail);
    } else if (model_camera.reason == m2::M2ResultReason::kMissingCamera) {
      stable_camera_missing_ = true;
    } else {
      return MakeResult(model_camera.status, model_camera.reason,
                        model_camera.detail);
    }
  }
  if (stable_camera_pose_.has_value()) {
    const RenderMatrix4x4 view =
        m2::M2System::BuildCameraViewMatrix(*stable_camera_pose_);
    std::copy(view.begin(), view.end(), view_mtx);
    const float vertical_fov = m2::M2System::BuildCameraVerticalFov(
        stable_camera_pose_->fov_rad, aspect);
    bx::mtxProj(proj_mtx, bx::toDeg(vertical_fov), aspect,
                std::max(0.01f, stable_camera_pose_->near_clip),
                std::max(1.0f, stable_camera_pose_->far_clip),
                bgfx::getCaps()->homogeneousDepth, bx::Handedness::Left);
    return MakeResult(m2::M2ResultStatus::kReady);
  }

  if (simple_model_orthographic_frame_.has_value()) {
    BuildSimpleModelOrthographicViewProjection(
        *simple_model_orthographic_frame_, view_mtx, proj_mtx);
    return MakeResult(m2::M2ResultStatus::kReady);
  }

  const auto spatial = system.QueryVisualCloneSpatialInfo(visual_clone_);
  if (spatial.status != m2::M2ResultStatus::kReady) {
    return MakeResult(spatial.status, spatial.reason, spatial.detail);
  }

  const auto& sphere = spatial.spatial.local_bounding_sphere;
  const float radius = std::isfinite(sphere[3]) && sphere[3] > 0.001f
                           ? sphere[3]
                           : 1.0f;
  const RenderVec3 center = {
      std::isfinite(sphere[0]) ? sphere[0] : 0.0f,
      std::isfinite(sphere[1]) ? sphere[1] : 0.0f,
      std::isfinite(sphere[2]) ? sphere[2] : 0.0f,
  };
  constexpr float kFallbackDiagonalFovRadians = 1.5700000524520874f;
  constexpr float kFallbackNearClip = 0.02777777798473835f;
  m2::M2CameraPose fallback_pose{
      .position = {center[0] + radius * 1.7f, center[1], center[2]},
      .target = {center[0], center[1], radius},
      .fov_rad = kFallbackDiagonalFovRadians,
      .near_clip = kFallbackNearClip,
      .far_clip = 1000.0f,
  };
  const RenderMatrix4x4 view =
      m2::M2System::BuildCameraViewMatrix(fallback_pose);
  std::copy(view.begin(), view.end(), view_mtx);
  const float vertical_fov =
      m2::M2System::BuildCameraVerticalFov(fallback_pose.fov_rad, aspect);
  bx::mtxProj(proj_mtx, bx::toDeg(vertical_fov), aspect,
              fallback_pose.near_clip, fallback_pose.far_clip,
              bgfx::getCaps()->homogeneousDepth, bx::Handedness::Left);
  return MakeResult(m2::M2ResultStatus::kReady);
}

}
