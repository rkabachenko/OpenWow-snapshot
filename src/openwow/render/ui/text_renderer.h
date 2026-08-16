#pragma once

#include "openwow/render/resources/fonts/font_face.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace openwow::render {

class BgfxGlyphAtlas;

namespace text {
struct TextLayout;
}

namespace ui {

struct TextExtent {
  float width{};
  float height{};
};

class TextRenderer {
 public:
  TextRenderer();
  ~TextRenderer();
  TextRenderer(const TextRenderer&) = delete;
  TextRenderer& operator=(const TextRenderer&) = delete;

  bool Init(int pixel_height = 14);
  bool InitFromFile(const std::string& font_path, int pixel_height = 14);
  bool InitFromVirtualPath(const std::string& font_path,
                           int pixel_height = 14);
  bool InitFromMemory(const std::string& font_path,
                      std::vector<std::uint8_t> font_data,
                      int pixel_height = 14);
  void Shutdown();

  bool WarmWorldDepthProgram() { return EnsureWorldDepthProgram(); }

  [[nodiscard]] bool is_ready() const noexcept { return ready_; }
  void BeginFrame(std::uint8_t view_id, float screen_width,
                  float screen_height);

  [[nodiscard]] bool BeginWorldDepthFrame(std::uint8_t view_id,
                                          float screen_width,
                                          float screen_height);

  void SetWorldDepth(float gl_ndc_depth);
  void EndWorldDepthFrame();

  bool DrawText(std::uint8_t view_id, float x, float y,
                const std::string& text, std::uint32_t color_argb,
                float scale = 1.0f);
  bool DrawRichText(std::uint8_t view_id, float x, float y,
                    const std::string& text, std::uint32_t color_argb,
                    float scale = 1.0f);
  bool DrawRichTextCentered(std::uint8_t view_id, float box_left,
                            float box_width, float y,
                            const std::string& text,
                            std::uint32_t color_argb, float scale = 1.0f);
  bool DrawTextAlpha(std::uint8_t view_id, float x, float y,
                     const std::string& text, std::uint32_t color_argb,
                     float alpha, float scale = 1.0f);

  bool DrawTextCentered(std::uint8_t view_id, float center_x, float y,
                        const std::string& text, std::uint32_t color_argb,
                        float alpha, float scale);

  bool DrawTextCentered(std::uint8_t view_id, float center_x, float y,
                        const openwow::render::text::TextLayout& layout,
                        std::uint32_t color_argb, float alpha, float scale);

  [[nodiscard]] std::shared_ptr<const openwow::render::text::TextLayout>
  LayoutPlainText(const std::string& text) const;

  [[nodiscard]] TextExtent Measure(const std::string& text,
                                   float scale = 1.0f) const;
  [[nodiscard]] TextExtent MeasureRichText(const std::string& text,
                                           float scale = 1.0f) const;
  [[nodiscard]] std::string WrapRichText(const std::string& text,
                                         float maximum_width,
                                         float scale = 1.0f) const;
  [[nodiscard]] float line_height() const noexcept;
  [[nodiscard]] float ascent() const noexcept;

  void DrawQuad(std::uint8_t view_id, float x, float y, float width,
                float height, std::uint32_t color_argb);
  void DrawTexturedQuad(
      std::uint8_t view_id, float x, float y, float width, float height,
      bgfx::TextureHandle texture,
      std::uint32_t color_argb = 0xFFFFFFFFu,
      std::uint64_t sampler_flags =
          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
      float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);

  void DrawTexturedQuadCorners(std::uint8_t view_id, float x, float y,
                               float width, float height,
                               bgfx::TextureHandle texture,
                               const std::array<float, 8>& corner_uvs,
                               std::uint32_t color_argb,
                               std::uint64_t sampler_flags);

  void SetQuadBatchingEnabled(bool enabled) noexcept;

  void FlushQuadBatch();

 private:
  struct Vertex {
    float x{};
    float y{};

    float z{};
    float u{};
    float v{};
    std::uint32_t abgr{};
  };

  bool InitializeGpuResources();
  [[nodiscard]] bool EnsureWorldDepthProgram();
  bool DrawLayout(std::uint8_t view_id, float box_left, float box_width,
                  float y, const std::string& text,
                  std::uint32_t color_argb, float scale,
                  bool formatting_enabled, bool center_lines);
  bool SubmitText(std::uint8_t view_id,
                  const std::vector<Vertex>& vertices,
                  const std::vector<std::uint16_t>& indices,
                  bgfx::TextureHandle texture);
  void SubmitQuad(std::uint8_t view_id, float x, float y, float width,
                  float height, float u0, float v0, float u1, float v1,
                  std::uint32_t color_argb, bgfx::TextureHandle texture,
                  std::uint64_t sampler_flags);
  void SubmitQuadCorners(std::uint8_t view_id, float x, float y, float width,
                         float height, const std::array<float, 8>& corner_uvs,
                         std::uint32_t color_argb, bgfx::TextureHandle texture,
                         std::uint64_t sampler_flags);

  struct QuadStateKey {
    std::uint64_t sampler_flags{0u};
    std::uint16_t texture{bgfx::kInvalidHandle};
    std::uint8_t view_id{0u};

    friend constexpr bool operator==(const QuadStateKey&,
                                     const QuadStateKey&) = default;
  };

  static constexpr std::uint32_t kMaxBatchedQuadVertices = 65536u;

  void EmitQuadRun(const QuadStateKey& key, std::span<const Vertex> vertices,
                   std::span<const std::uint16_t> indices);
  void SubmitQuadRun(const QuadStateKey& key, std::span<const Vertex> vertices,
                     std::span<const std::uint16_t> indices);

  static std::uint32_t ArgbToAbgr(std::uint32_t argb);

  struct PageBatch {
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
  };
  void AppendGlyphQuad(PageBatch& batch, float left, float top, float right,
                       float bottom, float u0, float v0, float u1, float v1,
                       std::uint32_t abgr);

  static void ClearPageBatches(std::map<std::size_t, PageBatch>& batches);

  std::shared_ptr<openwow::render::text::FontFace> face_;
  std::unique_ptr<openwow::render::BgfxGlyphAtlas> atlas_;

  bgfx::TextureHandle white_texture_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;

  bgfx::ProgramHandle world_depth_program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout vertex_layout_;
  bool ready_{};
  bool world_depth_active_{};
  float world_depth_z_{};

  std::map<std::size_t, PageBatch> world_batches_;

  std::map<std::size_t, PageBatch> screen_batches_;

  std::vector<std::uint32_t> layout_token_colors_;
  std::uint8_t world_batch_view_id_{};

  bool quad_batching_enabled_{};
  QuadStateKey quad_batch_key_{};
  std::vector<Vertex> quad_batch_vertices_;
  std::vector<std::uint16_t> quad_batch_indices_;
};

}
}
