#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::render::ui {

namespace detail {

[[nodiscard]] constexpr bool HasTransientSubmissionCapacity(
    const std::uint32_t available_vertices,
    const std::uint32_t available_indices,
    const std::uint32_t required_vertices,
    const std::uint32_t required_indices) noexcept {
  return required_vertices > 0u && required_indices > 0u &&
         available_vertices >= required_vertices &&
         available_indices >= required_indices;
}

struct ClampedScissorRect {
  std::uint16_t x{0u};
  std::uint16_t y{0u};
  std::uint16_t width{0u};
  std::uint16_t height{0u};

  friend constexpr bool operator==(const ClampedScissorRect&,
                                   const ClampedScissorRect&) = default;
};

[[nodiscard]] constexpr ClampedScissorRect IntersectScissorRects(
    const ClampedScissorRect lhs, const ClampedScissorRect rhs) noexcept {
  const std::uint32_t left = std::max(lhs.x, rhs.x);
  const std::uint32_t top = std::max(lhs.y, rhs.y);
  const std::uint32_t right = std::min<std::uint32_t>(
      lhs.x + lhs.width, rhs.x + rhs.width);
  const std::uint32_t bottom = std::min<std::uint32_t>(
      lhs.y + lhs.height, rhs.y + rhs.height);
  return {static_cast<std::uint16_t>(left),
          static_cast<std::uint16_t>(top),
          static_cast<std::uint16_t>(right > left ? right - left : 0u),
          static_cast<std::uint16_t>(bottom > top ? bottom - top : 0u)};
}

[[nodiscard]] inline ClampedScissorRect ComputeClampedScissorRect(
    const float x, const float y, const float width, const float height,
    const int viewport_width, const int viewport_height) noexcept {
  const int raw_x0 = static_cast<int>(x + 0.5F);
  const int raw_y0 = static_cast<int>(y + 0.5F);
  const int raw_x1 = static_cast<int>(x + width + 0.5F);
  const int raw_y1 = static_cast<int>(y + height + 0.5F);
  const int x0 = std::clamp(raw_x0, 0, std::max(0, viewport_width));
  const int y0 = std::clamp(raw_y0, 0, std::max(0, viewport_height));
  const int x1 = std::clamp(raw_x1, 0, std::max(0, viewport_width));
  const int y1 = std::clamp(raw_y1, 0, std::max(0, viewport_height));
  return {
      static_cast<std::uint16_t>(x0),
      static_cast<std::uint16_t>(y0),
      static_cast<std::uint16_t>(std::max(0, x1 - x0)),
      static_cast<std::uint16_t>(std::max(0, y1 - y0)),
  };
}

}

enum class BlendMode : std::uint8_t {
  kOpaque = 0,
  kAlphaKey = 1,
  kAlpha = 2,
  kAdditive = 3,
  kModulate = 4,
  kCoveragePremultiplied = 5,
};

struct MaterialState {
  std::uint64_t bgfx_state{BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A};
  float alpha_reference{0.0f};
};

[[nodiscard]] constexpr BlendMode BlendModeFromClientValue(
    const std::uint32_t value) noexcept {
  switch (value) {
    case 0u: return BlendMode::kOpaque;
    case 1u: return BlendMode::kAlphaKey;
    case 2u: return BlendMode::kAlpha;
    case 3u: return BlendMode::kAdditive;
    case 4u: return BlendMode::kModulate;
    default: return BlendMode::kAlpha;
  }
}

[[nodiscard]] constexpr MaterialState ResolveMaterialState(
    const BlendMode blend) noexcept {
  constexpr std::uint64_t writes =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
  switch (blend) {
    case BlendMode::kOpaque:
      return {writes, 0.0f};
    case BlendMode::kAlphaKey:
      return {writes, 128.0f / 255.0f};
    case BlendMode::kAlpha:
      return {writes | BGFX_STATE_BLEND_FUNC(
                           BGFX_STATE_BLEND_SRC_ALPHA,
                           BGFX_STATE_BLEND_INV_SRC_ALPHA),
              0.0f};
    case BlendMode::kAdditive:
      return {writes | BGFX_STATE_BLEND_FUNC(
                           BGFX_STATE_BLEND_SRC_ALPHA,
                           BGFX_STATE_BLEND_ONE),
              0.0f};
    case BlendMode::kModulate:
      return {writes | BGFX_STATE_BLEND_FUNC(
                           BGFX_STATE_BLEND_DST_COLOR,
                           BGFX_STATE_BLEND_ZERO),
              0.0f};
    case BlendMode::kCoveragePremultiplied:
      return {writes | BGFX_STATE_BLEND_FUNC_SEPARATE(
                           BGFX_STATE_BLEND_ONE,
                           BGFX_STATE_BLEND_INV_SRC_ALPHA,
                           BGFX_STATE_BLEND_ONE,
                           BGFX_STATE_BLEND_INV_SRC_ALPHA),
              0.0f};
  }
  return {writes, 0.0f};
}

struct Quad {
  struct UvPoint {
    float u{0.0f};
    float v{0.0f};
  };

  bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
  float x{0.0f};
  float y{0.0f};
  float w{0.0f};
  float h{0.0f};
  float u0{0.0f};
  float v0{0.0f};
  float u1{1.0f};
  float v1{1.0f};

  std::uint32_t abgr{0xffffffffu};
  std::array<std::uint32_t, 4> vertex_abgr{
      0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};

  BlendMode blend{BlendMode::kAlpha};
  bgfx::TextureHandle mask_texture = BGFX_INVALID_HANDLE;
  std::uint64_t sampler_flags{BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP};
  std::array<UvPoint, 4> uv_quad{};
  bool has_custom_uv_quad{false};
  bool has_vertex_colors{false};
  bool rotate_90{false};
  float rotation_radians{0.0f};
  float rotation_origin_x{0.0f};
  float rotation_origin_y{0.0f};
  bool has_rotation_origin{false};
  bool desaturated{false};
  bool flip_texture_y{false};
};

enum class PrimitiveTopology {
  kTriangleList,
  kTriangleStrip,
};

struct MeshVertex {
  float x{0.0f};
  float y{0.0f};
  float u{0.0f};
  float v{0.0f};
  std::uint32_t abgr{0xffffffffu};
};

struct Mesh {
  bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
  std::span<const MeshVertex> vertices{};
  std::span<const std::uint16_t> indices{};
  BlendMode blend{BlendMode::kAlpha};
  PrimitiveTopology topology{PrimitiveTopology::kTriangleList};
  std::uint64_t sampler_flags{BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP};
  bool desaturated{false};
};

class UiRenderer {
 public:
  UiRenderer() = default;
  UiRenderer(const UiRenderer&) = delete;
  UiRenderer& operator=(const UiRenderer&) = delete;

  bool Init();
  void Shutdown();

  void Begin(int view_id, int width, int height);

  void SetSubmissionBatchingEnabled(bool enabled) noexcept;

  void FlushSubmissionBatch();

  bool Submit(const Quad& quad);
  bool SubmitMesh(const Mesh& mesh);

  bool SubmitSolidMesh(const Mesh& mesh);

  bool SubmitSolid(const Quad& quad);
  void End();

  void SetScissor(float x, float y, float w, float h);
  void ClearScissor();

 private:
  struct UiVertex {
    float x;
    float y;
    float u;
    float v;
    std::uint32_t abgr;
    static void InitLayout();
    static bgfx::VertexLayout layout;
  };

  struct DrawStateKey {
    std::uint64_t bgfx_state{0u};
    std::uint64_t sampler_flags{0u};
    std::uint16_t texture{bgfx::kInvalidHandle};
    std::uint16_t mask_texture{bgfx::kInvalidHandle};

    std::array<float, 4> material{0.0f, 0.0f, 0.0f, 0.0f};
    bool scissor_active{false};
    detail::ClampedScissorRect scissor{};

    friend bool operator==(const DrawStateKey&, const DrawStateKey&) = default;
  };

  static constexpr std::uint32_t kMaxBatchedVertices = 65536u;

  [[nodiscard]] DrawStateKey BuildDrawStateKey(
      std::uint64_t bgfx_state, float alpha_reference,
      bgfx::TextureHandle texture, std::uint64_t sampler_flags,
      bgfx::TextureHandle mask_texture, bool desaturated,
      bool flip_texture_y) const noexcept;

  bool EmitRun(const DrawStateKey& key, std::span<const MeshVertex> vertices,
               std::span<const std::uint16_t> indices, bool mergeable);
  bool SubmitRun(const DrawStateKey& key, std::span<const MeshVertex> vertices,
                 std::span<const std::uint16_t> indices);

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_tex_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_mask_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_material_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle white_pixel_ = BGFX_INVALID_HANDLE;
  int view_id_{0};
  int width_{0};
  int height_{0};
  bool ok_{false};

  bool scissor_active_{false};
  std::uint16_t scissor_x_{0};
  std::uint16_t scissor_y_{0};
  std::uint16_t scissor_w_{0};
  std::uint16_t scissor_h_{0};
  std::array<detail::ClampedScissorRect, 64> scissor_stack_{};
  std::uint8_t scissor_stack_depth_{0u};

  bool batching_enabled_{false};
  DrawStateKey batch_key_{};
  std::vector<MeshVertex> batch_vertices_;
  std::vector<std::uint16_t> batch_indices_;
};

}
