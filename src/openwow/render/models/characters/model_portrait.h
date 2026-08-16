#pragma once

#include "openwow/render/m2/m2_system.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::render {

struct ModelPortraitResult {
  m2::M2ResultStatus status = m2::M2ResultStatus::kNotReady;
  m2::M2ResultReason reason = m2::M2ResultReason::kNone;
  std::string detail;
  std::uint32_t submitted_draw_count = 0;

  [[nodiscard]] bool SubmittedCompleteVisual() const noexcept {
    return status == m2::M2ResultStatus::kReady &&
           submitted_draw_count != 0u;
  }

  [[nodiscard]] bool CanRetry() const noexcept {
    return status == m2::M2ResultStatus::kNotReady;
  }
};

struct SimpleModelOrthographicFrame {

  float rect_width{0.0f};
  float rect_height{0.0f};

  float frame_scale{1.0f};

  float ui_vertical_stretch{0.6f};
};

inline constexpr float kSimpleModelOrthoNear = -500.0f;
inline constexpr float kSimpleModelOrthoFar = 500.0f;

inline constexpr float kSimpleModelGlobalScaleDivisor = 0.6f;

class ModelPortrait {
 public:
  explicit ModelPortrait(m2::M2System& m2_system)
      : m2_system_(m2_system) {}
  ~ModelPortrait();

  ModelPortrait(const ModelPortrait&) = delete;
  ModelPortrait& operator=(const ModelPortrait&) = delete;

  [[nodiscard]] bool Initialize(std::uint16_t width = 64,
                                std::uint16_t height = 64);
  void Shutdown();

  void SetSourceInstance(std::uint32_t source_instance_id,
                         std::uint64_t visual_revision);

  void SetModelPath(std::string model_path);

  void SetModelPath(std::string model_path,
                    std::array<std::string, 3> display_texture_paths,
                    std::optional<m2::M2ParticleColorRecord>
                        display_particle_colors);

  [[nodiscard]] ModelPortraitResult SynchronizeVisualClone();

  void SetCamera(float yaw = 0.0f, float pitch = 0.15f,
                 float distance = 2.5f);
  void SetCameraIndex(std::int32_t camera_index);
  void SetCameraType(std::uint32_t camera_type);
  void SetCameraTarget(float x = 0.0f, float y = 1.2f, float z = 0.0f);
  void SetModelRotation(float radians) noexcept { model_rotation_ = radians; }
  void SetModelTransform(float scale, float x, float y, float z) noexcept;

  void SetSimpleModelOrthographicFrame(
      std::optional<SimpleModelOrthographicFrame> frame) noexcept {
    simple_model_orthographic_frame_ = frame;
  }
  void SetAnimation(std::uint32_t animation_id,
                    std::optional<std::uint32_t> time_ms = std::nullopt);
  void ClearAnimationOverride();

  [[nodiscard]] ModelPortraitResult RenderToTexture(bgfx::ViewId view_id);

  [[nodiscard]] bgfx::TextureHandle GetTexture() const noexcept {
    return presented_color_tex_;
  }
  [[nodiscard]] bgfx::FrameBufferHandle GetFrameBuffer() const noexcept {
    return presented_fb_;
  }
  [[nodiscard]] bool IsValid() const noexcept {
    return bgfx::isValid(fb_) && bgfx::isValid(presented_fb_);
  }
  [[nodiscard]] bool HasPresentedContent() const noexcept {
    return has_presented_content_;
  }

  void Resize(std::uint16_t width, std::uint16_t height);
  [[nodiscard]] std::uint16_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint16_t height() const noexcept { return height_; }

  [[nodiscard]] std::uint64_t SynchronizedVisualRevision() const noexcept {
    return visual_clone_.visual_revision();
  }

 private:
  m2::M2System& m2_system_;
  void CreateFrameBuffer();
  void DestroyFrameBuffer();
  void DestroyOwnedSource();
  [[nodiscard]] ModelPortraitResult EnsureOwnedSource();
  [[nodiscard]] ModelPortraitResult ComputeViewProjection(
      float* view_mtx, float* proj_mtx);

  std::uint16_t width_ = 64;
  std::uint16_t height_ = 64;
  bgfx::FrameBufferHandle fb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle color_tex_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle depth_tex_ = BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle presented_fb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle presented_color_tex_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle presented_depth_tex_ = BGFX_INVALID_HANDLE;
  bool has_presented_content_ = false;

  std::uint32_t source_instance_id_ = 0;
  std::uint64_t source_visual_revision_ = 0;
  std::string owned_model_path_;

  std::array<std::string, 3> owned_display_texture_paths_{};
  std::optional<m2::M2ParticleColorRecord> owned_display_particle_colors_;
  std::uint32_t owned_instance_id_ = 0u;
  m2::M2VisualCloneLease visual_clone_;

  std::optional<m2::M2CameraPose> stable_camera_pose_;
  bool stable_camera_missing_ = false;
  std::int32_t camera_index_ = 0;
  std::uint32_t camera_type_ = 0u;
  bool select_camera_by_type_ = false;
  std::uint32_t animation_id_ = 0u;
  std::optional<std::uint32_t> animation_time_ms_;
  std::uint64_t animation_applied_revision_ = 0u;
  bool has_animation_override_ = false;

  float cam_yaw_ = 0.0f;
  float cam_pitch_ = 0.15f;
  float cam_distance_ = 2.5f;
  float cam_target_[3] = {0.0f, 1.2f, 0.0f};
  float model_rotation_ = 0.0f;
  float model_scale_ = 1.0f;
  float model_position_[3] = {0.0f, 0.0f, 0.0f};
  std::optional<SimpleModelOrthographicFrame> simple_model_orthographic_frame_;

  bool initialized_ = false;
};

}
