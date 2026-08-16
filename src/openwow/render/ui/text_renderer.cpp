#include "openwow/render/ui/text_renderer.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/backend/bgfx/bgfx_glyph_atlas.h"
#include "openwow/render/resources/fonts/text_layout.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/ui/ui_shaders.h"
#include "openwow/vfs/sfile_core.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <span>
#include <utility>

namespace openwow::render::ui {
namespace {

std::string FindSystemFont() {
  constexpr std::array paths{
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/System/Library/Fonts/Helvetica.ttc",
      "/System/Library/Fonts/SFNS.ttf",
      "/Library/Fonts/Arial.ttf",
      "C:\\Windows\\Fonts\\arial.ttf",
      "C:\\Windows\\Fonts\\segoeui.ttf",
  };
  for (const char* path : paths) {
    if (std::filesystem::exists(path)) return path;
  }
  return {};
}

constexpr std::uint64_t kQuadBlendState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                          BGFX_STATE_BLEND_INV_SRC_ALPHA);

void ResolveTokenColors(const openwow::render::text::TextLayout& layout,
                        const std::uint32_t base_argb,
                        std::vector<std::uint32_t>& colors) {
  colors.assign(layout.tokens.size(), base_argb);
  std::uint32_t current = base_argb;
  for (std::size_t index = 0; index < layout.tokens.size(); ++index) {
    const auto& token = layout.tokens[index];
    if (token.kind ==
        openwow::render::text::FormattedTokenKind::Color) {
      const std::uint32_t alpha =
          (((base_argb >> 24u) & 0xffu) *
               ((token.color_argb >> 24u) & 0xffu) +
           127u) /
          255u;
      current = (alpha << 24u) | (token.color_argb & 0x00ffffffu);
    } else if (token.kind ==
               openwow::render::text::FormattedTokenKind::ResetColor) {
      current = base_argb;
    }
    colors[index] = current;
  }
}

}

TextRenderer::TextRenderer() = default;
TextRenderer::~TextRenderer() { Shutdown(); }

bool TextRenderer::Init(const int pixel_height) {
  const std::string path = FindSystemFont();
  return !path.empty() && InitFromFile(path, pixel_height);
}

bool TextRenderer::InitFromFile(const std::string& font_path,
                                const int pixel_height) {
  Shutdown();
  face_ = openwow::render::text::FontFace::LoadFile(font_path, pixel_height);
  if (!face_ || !InitializeGpuResources()) {
    Shutdown();
    return false;
  }
  ready_ = true;
  return true;
}

bool TextRenderer::InitFromVirtualPath(const std::string& font_path,
                                       const int pixel_height) {
  void* bytes{};
  std::size_t size{};
  if (!openwow::vfs::SFileOpenFileAndLoadData(
          nullptr, font_path.c_str(), &bytes, &size, 0, 3, 0) ||
      bytes == nullptr || size == 0) {
    return false;
  }
  std::vector<std::uint8_t> owned(
      static_cast<const std::uint8_t*>(bytes),
      static_cast<const std::uint8_t*>(bytes) + size);
  openwow::vfs::SFileFreeLoadedData(bytes);
  return InitFromMemory(font_path, std::move(owned), pixel_height);
}

bool TextRenderer::InitFromMemory(const std::string& font_path,
                                  std::vector<std::uint8_t> font_data,
                                  const int pixel_height) {
  Shutdown();
  face_ = openwow::render::text::FontFace::LoadMemory(
      font_path, std::move(font_data), pixel_height);
  if (!face_ || !InitializeGpuResources()) {
    Shutdown();
    return false;
  }
  ready_ = true;
  return true;
}

bool TextRenderer::InitializeGpuResources() {
  const auto handles = LoadUiProgram();
  if (!bgfx::isValid(handles.program) || !bgfx::isValid(handles.s_tex)) {
    return false;
  }
  program_ = handles.program;
  sampler_ = handles.s_tex;

  vertex_layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  atlas_ = std::make_unique<openwow::render::BgfxGlyphAtlas>(face_, 0);

  constexpr std::array<std::uint8_t, 4> white{255, 255, 255, 255};
  white_texture_ = bgfx::createTexture2D(
      1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
      bgfx::copy(white.data(), static_cast<std::uint32_t>(white.size())));
  return bgfx::isValid(white_texture_);
}

void TextRenderer::Shutdown() {

  quad_batch_vertices_.clear();
  quad_batch_indices_.clear();

  world_batches_.clear();
  screen_batches_.clear();
  atlas_.reset();
  if (bgfx::isValid(white_texture_)) bgfx::destroy(white_texture_);
  if (bgfx::isValid(world_depth_program_)) {
    bgfx::destroy(world_depth_program_);
  }
  DestroyUiProgram(program_, sampler_);
  white_texture_ = BGFX_INVALID_HANDLE;
  program_ = BGFX_INVALID_HANDLE;
  world_depth_program_ = BGFX_INVALID_HANDLE;
  sampler_ = BGFX_INVALID_HANDLE;
  world_depth_active_ = false;
  world_depth_z_ = 0.0f;
  face_.reset();
  ready_ = false;
}

bool TextRenderer::EnsureWorldDepthProgram() {
  if (bgfx::isValid(world_depth_program_)) {
    return true;
  }
  world_depth_program_ = openwow::render::CreateEmbeddedProgram(
      openwow::render::ShaderProgramId::WorldText, bgfx::getRendererType());
  if (!bgfx::isValid(world_depth_program_)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "TextRenderer: world-text program (vs_ui + fs_world_text) failed to "
        "create; overhead names would draw without their depth test");
    return false;
  }
  return true;
}

bool TextRenderer::BeginWorldDepthFrame(const std::uint8_t view_id,
                                        const float screen_width,
                                        const float screen_height) {

  FlushQuadBatch();
  if (!ready_ || !EnsureWorldDepthProgram()) return false;

  const bool homogeneous_depth = bgfx::getCaps()->homogeneousDepth;
  float projection[16];
  bx::mtxOrtho(projection, 0.0f, screen_width, screen_height, 0.0f,
               homogeneous_depth ? -1.0f : 0.0f, 1.0f, 0.0f,
               homogeneous_depth);
  bgfx::setViewTransform(view_id, nullptr, projection);
  world_depth_active_ = true;
  world_depth_z_ = homogeneous_depth ? -1.0f : 0.0f;
  return true;
}

void TextRenderer::SetWorldDepth(const float gl_ndc_depth) {

  world_depth_z_ = bgfx::getCaps()->homogeneousDepth
                       ? gl_ndc_depth
                       : (gl_ndc_depth + 1.0f) * 0.5f;
}

void TextRenderer::EndWorldDepthFrame() {

  FlushQuadBatch();

  for (auto& [page_index, batch] : world_batches_) {
    if (batch.vertices.empty()) continue;
    (void)SubmitText(world_batch_view_id_, batch.vertices, batch.indices,
                     atlas_->page(page_index).body);
  }
  ClearPageBatches(world_batches_);
  world_depth_active_ = false;
  world_depth_z_ = 0.0f;
}

void TextRenderer::BeginFrame(const std::uint8_t view_id,
                              const float screen_width,
                              const float screen_height) {

  FlushQuadBatch();
  if (!ready_) return;
  float projection[16];
  bx::mtxOrtho(projection, 0.0f, screen_width, screen_height, 0.0f, 0.0f,
               100.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
  bgfx::setViewTransform(view_id, nullptr, projection);
  bgfx::setViewRect(view_id, 0, 0,
                    static_cast<std::uint16_t>(std::max(1.0f, screen_width)),
                    static_cast<std::uint16_t>(std::max(1.0f, screen_height)));
}

bool TextRenderer::DrawText(const std::uint8_t view_id, const float x,
                            const float y, const std::string& text,
                            const std::uint32_t color_argb,
                            const float scale) {
  return DrawLayout(view_id, x, 0.0f, y, text, color_argb, scale, false,
                    false);
}

bool TextRenderer::DrawRichText(const std::uint8_t view_id, const float x,
                                const float y, const std::string& text,
                                const std::uint32_t color_argb,
                                const float scale) {
  return DrawLayout(view_id, x, 0.0f, y, text, color_argb, scale, true,
                    false);
}

bool TextRenderer::DrawRichTextCentered(
    const std::uint8_t view_id, const float box_left, const float box_width,
    const float y, const std::string& text, const std::uint32_t color_argb,
    const float scale) {
  return DrawLayout(view_id, box_left, box_width, y, text, color_argb, scale,
                    true, true);
}

bool TextRenderer::DrawTextAlpha(
    const std::uint8_t view_id, const float x, const float y,
    const std::string& text, const std::uint32_t color_argb,
    const float alpha, const float scale) {
  const auto base_alpha = static_cast<std::uint32_t>(color_argb >> 24u);
  const auto modulated = static_cast<std::uint32_t>(
      std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>(base_alpha));
  return DrawText(view_id, x, y,
                  text, (color_argb & 0x00ffffffu) | (modulated << 24u),
                  scale);
}

void TextRenderer::AppendGlyphQuad(PageBatch& batch, const float left,
                                   const float top, const float right,
                                   const float bottom, const float u0,
                                   const float v0, const float u1,
                                   const float v1, const std::uint32_t abgr) {
  if (batch.vertices.size() / 4u >= 16383u) return;
  const std::uint16_t base = static_cast<std::uint16_t>(batch.vertices.size());
  const float z = world_depth_z_;
  batch.vertices.insert(batch.vertices.end(),
                        {{left, top, z, u0, v0, abgr},
                         {right, top, z, u1, v0, abgr},
                         {right, bottom, z, u1, v1, abgr},
                         {left, bottom, z, u0, v1, abgr}});
  batch.indices.insert(batch.indices.end(),
                       {base, static_cast<std::uint16_t>(base + 1u),
                        static_cast<std::uint16_t>(base + 2u), base,
                        static_cast<std::uint16_t>(base + 2u),
                        static_cast<std::uint16_t>(base + 3u)});
}

std::shared_ptr<const openwow::render::text::TextLayout>
TextRenderer::LayoutPlainText(const std::string& text) const {
  if (!ready_ || !face_ || text.empty()) return nullptr;
  openwow::render::text::TextLayoutRequest request;
  request.formatting_enabled = false;
  return std::make_shared<const openwow::render::text::TextLayout>(
      openwow::render::text::LayoutText(*face_, text, request));
}

bool TextRenderer::DrawTextCentered(const std::uint8_t view_id,
                                    const float center_x, const float y,
                                    const std::string& text,
                                    const std::uint32_t color_argb,
                                    const float alpha, const float scale) {
  if (!ready_ || !face_ || text.empty() || scale <= 0.0f) return false;

  openwow::render::text::TextLayoutRequest request;
  request.formatting_enabled = false;
  const auto layout =
      openwow::render::text::LayoutText(*face_, text, request);
  return DrawTextCentered(view_id, center_x, y, layout, color_argb, alpha,
                          scale);
}

bool TextRenderer::DrawTextCentered(
    const std::uint8_t view_id, const float center_x, const float y,
    const openwow::render::text::TextLayout& layout,
    const std::uint32_t color_argb, const float alpha, const float scale) {

  FlushQuadBatch();
  if (!ready_ || !face_ || scale <= 0.0f) return false;

  const float clamped_alpha = std::clamp(alpha, 0.0f, 1.0f);
  const auto modulated_alpha = static_cast<std::uint32_t>(
      clamped_alpha * static_cast<float>(color_argb >> 24u));
  const std::uint32_t fill_abgr = ArgbToAbgr(
      (color_argb & 0x00ffffffu) | (modulated_alpha << 24u));

  const float left0 = center_x - layout.width * scale * 0.5f;

  if (world_depth_active_) {
    world_batch_view_id_ = view_id;
  } else {
    ClearPageBatches(screen_batches_);
  }
  auto& batches = world_depth_active_ ? world_batches_ : screen_batches_;

  PageBatch* current_batch = nullptr;
  std::size_t current_batch_page = 0u;
  bool has_current_batch = false;
  bool emitted = false;
  for (const auto& element : layout.elements) {
    const auto& token = layout.tokens[element.token_index];
    if (token.kind != openwow::render::text::FormattedTokenKind::Glyph) {
      continue;
    }
    const auto* glyph = atlas_->Get(token.codepoint);
    if (glyph == nullptr || glyph->width <= 0.0f || glyph->height <= 0.0f) {
      continue;
    }
    const float left = left0 + (element.x + glyph->bearing_x) * scale;
    const float top = y + element.y * scale +
                      (face_->ascent() - glyph->bearing_y) * scale;
    if (!has_current_batch || current_batch_page != glyph->page) {
      current_batch = &batches[glyph->page];
      current_batch_page = glyph->page;
      has_current_batch = true;
    }
    AppendGlyphQuad(*current_batch, left, top, left + glyph->width * scale,
                    top + glyph->height * scale, glyph->u0, glyph->v0,
                    glyph->u1, glyph->v1, fill_abgr);
    emitted = true;
  }
  if (!emitted) return false;

  if (world_depth_active_) {
    return true;
  }
  bool submitted{};
  for (const auto& [page_index, batch] : screen_batches_) {
    if (batch.vertices.empty()) continue;
    submitted |= SubmitText(view_id, batch.vertices, batch.indices,
                            atlas_->page(page_index).body);
  }
  return submitted;
}

void TextRenderer::ClearPageBatches(
    std::map<std::size_t, PageBatch>& batches) {
  for (auto& [page_index, batch] : batches) {
    batch.vertices.clear();
    batch.indices.clear();
  }
}

bool TextRenderer::DrawLayout(
    const std::uint8_t view_id, const float box_left, const float box_width,
    const float y, const std::string& text, const std::uint32_t color_argb,
    const float scale, const bool formatting_enabled,
    const bool center_lines) {

  FlushQuadBatch();
  if (!ready_ || !face_ || text.empty() || scale <= 0.0f) return false;

  openwow::render::text::TextLayoutRequest request;
  request.formatting_enabled = formatting_enabled;
  if (box_width > 0.0f) {
    request.maximum_width = box_width / scale;
    request.wrap =
        openwow::render::text::WrapMode::WordWithCharacterFallback;
  }
  const auto layout =
      openwow::render::text::LayoutText(*face_, text, request);
  ResolveTokenColors(layout, color_argb, layout_token_colors_);
  const auto& colors = layout_token_colors_;

  ClearPageBatches(screen_batches_);
  auto& batches = screen_batches_;

  PageBatch* current_batch = nullptr;
  std::size_t current_batch_page = 0u;
  bool has_current_batch = false;

  std::size_t line_index{};
  for (const auto& element : layout.elements) {
    const auto& token = layout.tokens[element.token_index];
    if (token.kind != openwow::render::text::FormattedTokenKind::Glyph) {
      continue;
    }
    while (line_index + 1u < layout.lines.size() &&
           token.begin >= layout.lines[line_index + 1u].begin) {
      ++line_index;
    }
    const auto* glyph = atlas_->Get(token.codepoint);
    if (glyph == nullptr || glyph->width <= 0.0f ||
        glyph->height <= 0.0f) {
      continue;
    }

    const float line_offset =
        center_lines && box_width > 0.0f
            ? (box_width - layout.lines[line_index].width * scale) * 0.5f
            : 0.0f;
    const float left =
        box_left + line_offset +
        (element.x + glyph->bearing_x) * scale;
    const float top =
        y + element.y * scale +
        (face_->ascent() - glyph->bearing_y) * scale;
    const float right = left + glyph->width * scale;
    const float bottom = top + glyph->height * scale;
    const std::uint32_t color = ArgbToAbgr(colors[element.token_index]);
    if (!has_current_batch || current_batch_page != glyph->page) {
      current_batch = &batches[glyph->page];
      current_batch_page = glyph->page;
      has_current_batch = true;
    }
    auto& batch = *current_batch;
    if (batch.vertices.size() / 4u >= 16383u) continue;
    const std::uint16_t base =
        static_cast<std::uint16_t>(batch.vertices.size());
    const float z = world_depth_z_;
    batch.vertices.insert(batch.vertices.end(),
                          {{left, top, z, glyph->u0, glyph->v0, color},
                           {right, top, z, glyph->u1, glyph->v0, color},
                           {right, bottom, z, glyph->u1, glyph->v1, color},
                           {left, bottom, z, glyph->u0, glyph->v1, color}});
    batch.indices.insert(batch.indices.end(),
                         {base, static_cast<std::uint16_t>(base + 1u),
                          static_cast<std::uint16_t>(base + 2u), base,
                          static_cast<std::uint16_t>(base + 2u),
                          static_cast<std::uint16_t>(base + 3u)});
  }

  bool submitted{};
  for (const auto& [page_index, batch] : batches) {
    if (batch.vertices.empty()) continue;
    submitted |= SubmitText(view_id, batch.vertices, batch.indices,
                            atlas_->page(page_index).body);
  }
  return submitted;
}

bool TextRenderer::SubmitText(
    const std::uint8_t view_id, const std::vector<Vertex>& vertices,
    const std::vector<std::uint16_t>& indices,
    const bgfx::TextureHandle texture) {

  FlushQuadBatch();
  if (vertices.empty() || !bgfx::isValid(texture) ||
      bgfx::getAvailTransientVertexBuffer(
          static_cast<std::uint32_t>(vertices.size()), vertex_layout_) <
          vertices.size() ||
      bgfx::getAvailTransientIndexBuffer(
          static_cast<std::uint32_t>(indices.size())) < indices.size()) {
    return false;
  }

  bgfx::TransientVertexBuffer vertex_buffer;
  bgfx::TransientIndexBuffer index_buffer;
  bgfx::allocTransientVertexBuffer(
      &vertex_buffer, static_cast<std::uint32_t>(vertices.size()),
      vertex_layout_);
  bgfx::allocTransientIndexBuffer(
      &index_buffer, static_cast<std::uint32_t>(indices.size()));
  std::copy(vertices.begin(), vertices.end(),
            reinterpret_cast<Vertex*>(vertex_buffer.data));
  std::copy(indices.begin(), indices.end(),
            reinterpret_cast<std::uint16_t*>(index_buffer.data));

  const std::uint64_t depth_state =
      world_depth_active_
          ? (BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL)
          : 0u;
  bgfx::setState(
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | depth_state |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                            BGFX_STATE_BLEND_INV_SRC_ALPHA));
  bgfx::setVertexBuffer(0, &vertex_buffer);
  bgfx::setIndexBuffer(&index_buffer);
  bgfx::setTexture(0, sampler_, texture,
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::submit(view_id,
               world_depth_active_ ? world_depth_program_ : program_);
  return true;
}

TextExtent TextRenderer::Measure(const std::string& text,
                                 const float scale) const {
  if (!ready_ || !face_ || text.empty() || scale <= 0.0f) return {};
  openwow::render::text::TextLayoutRequest request;
  request.formatting_enabled = false;
  const auto layout =
      openwow::render::text::LayoutText(*face_, text, request);
  return {.width = layout.width * scale, .height = layout.height * scale};
}

TextExtent TextRenderer::MeasureRichText(const std::string& text,
                                         const float scale) const {
  if (!ready_ || !face_ || text.empty() || scale <= 0.0f) return {};
  const auto layout = openwow::render::text::LayoutText(*face_, text, {});
  return {.width = layout.width * scale, .height = layout.height * scale};
}

std::string TextRenderer::WrapRichText(const std::string& text,
                                       const float maximum_width,
                                       const float scale) const {
  if (!ready_ || !face_ || text.empty() || maximum_width <= 0.0f ||
      scale <= 0.0f) {
    return text;
  }
  openwow::render::text::TextLayoutRequest request;
  request.maximum_width = maximum_width / scale;
  request.wrap =
      openwow::render::text::WrapMode::WordWithCharacterFallback;
  const auto layout =
      openwow::render::text::LayoutText(*face_, text, request);
  if (layout.lines.size() <= 1u) return text;

  std::string wrapped;
  wrapped.reserve(text.size() + layout.lines.size());
  for (std::size_t index = 0; index < layout.lines.size(); ++index) {
    const auto& line = layout.lines[index];
    wrapped.append(text, line.begin, line.end - line.begin);
    if (index + 1u < layout.lines.size()) wrapped.push_back('\n');
  }
  return wrapped;
}

float TextRenderer::line_height() const noexcept {
  return face_ ? face_->line_height() : 0.0f;
}

float TextRenderer::ascent() const noexcept {
  return face_ ? face_->ascent() : 0.0f;
}

void TextRenderer::SubmitQuad(
    const std::uint8_t view_id, const float x, const float y,
    const float width, const float height, const float u0, const float v0,
    const float u1, const float v1, const std::uint32_t color_argb,
    const bgfx::TextureHandle texture, const std::uint64_t sampler_flags) {

  SubmitQuadCorners(view_id, x, y, width, height,
                    {u0, v0, u0, v1, u1, v0, u1, v1}, color_argb, texture,
                    sampler_flags);
}

void TextRenderer::SubmitQuadCorners(
    const std::uint8_t view_id, const float x, const float y,
    const float width, const float height,
    const std::array<float, 8>& corner_uvs, const std::uint32_t color_argb,
    const bgfx::TextureHandle texture, const std::uint64_t sampler_flags) {
  if (!ready_ || width < 1.0f || height < 1.0f || !bgfx::isValid(texture)) {
    return;
  }
  const std::uint32_t color = ArgbToAbgr(color_argb);
  const float z = world_depth_z_;
  const std::array vertices{
      Vertex{x, y, z, corner_uvs[0], corner_uvs[1], color},
      Vertex{x + width, y, z, corner_uvs[4], corner_uvs[5], color},
      Vertex{x + width, y + height, z, corner_uvs[6], corner_uvs[7], color},
      Vertex{x, y + height, z, corner_uvs[2], corner_uvs[3], color},
  };
  constexpr std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
  EmitQuadRun(QuadStateKey{.sampler_flags = sampler_flags,
                           .texture = texture.idx,
                           .view_id = view_id},
              vertices, indices);
}

void TextRenderer::EmitQuadRun(
    const QuadStateKey& key, const std::span<const Vertex> vertices,
    const std::span<const std::uint16_t> indices) {
  if (!quad_batching_enabled_) {

    FlushQuadBatch();
    SubmitQuadRun(key, vertices, indices);
    return;
  }

  if (!quad_batch_vertices_.empty() && !(quad_batch_key_ == key)) {
    FlushQuadBatch();
  }

  const auto vertex_count = static_cast<std::uint32_t>(vertices.size());
  const auto index_count = static_cast<std::uint32_t>(indices.size());
  const auto pending_vertices =
      static_cast<std::uint32_t>(quad_batch_vertices_.size());
  const auto pending_indices =
      static_cast<std::uint32_t>(quad_batch_indices_.size());

  if (pending_vertices + vertex_count > kMaxBatchedQuadVertices ||
      bgfx::getAvailTransientVertexBuffer(pending_vertices + vertex_count,
                                          vertex_layout_) <
          pending_vertices + vertex_count ||
      bgfx::getAvailTransientIndexBuffer(pending_indices + index_count) <
          pending_indices + index_count) {
    FlushQuadBatch();
    if (bgfx::getAvailTransientVertexBuffer(vertex_count, vertex_layout_) <
            vertex_count ||
        bgfx::getAvailTransientIndexBuffer(index_count) < index_count) {
      return;
    }
  }

  const auto base_vertex =
      static_cast<std::uint16_t>(quad_batch_vertices_.size());
  if (quad_batch_vertices_.empty()) {

    quad_batch_key_ = key;
  }
  quad_batch_vertices_.insert(quad_batch_vertices_.end(), vertices.begin(),
                              vertices.end());

  for (const std::uint16_t index : indices) {
    quad_batch_indices_.push_back(
        static_cast<std::uint16_t>(base_vertex + index));
  }
}

void TextRenderer::SubmitQuadRun(
    const QuadStateKey& key, const std::span<const Vertex> vertices,
    const std::span<const std::uint16_t> indices) {

  if (!ready_ || vertices.empty() || indices.empty()) {
    return;
  }
  const auto vertex_count = static_cast<std::uint32_t>(vertices.size());
  const auto index_count = static_cast<std::uint32_t>(indices.size());

  if (bgfx::getAvailTransientVertexBuffer(vertex_count, vertex_layout_) <
          vertex_count ||
      bgfx::getAvailTransientIndexBuffer(index_count) < index_count) {
    return;
  }

  bgfx::TransientVertexBuffer vertex_buffer;
  bgfx::TransientIndexBuffer index_buffer;
  bgfx::allocTransientVertexBuffer(&vertex_buffer, vertex_count,
                                   vertex_layout_);
  bgfx::allocTransientIndexBuffer(&index_buffer, index_count);
  std::copy(vertices.begin(), vertices.end(),
            reinterpret_cast<Vertex*>(vertex_buffer.data));
  std::copy(indices.begin(), indices.end(),
            reinterpret_cast<std::uint16_t*>(index_buffer.data));
  bgfx::setState(kQuadBlendState);
  bgfx::setVertexBuffer(0, &vertex_buffer);
  bgfx::setIndexBuffer(&index_buffer);
  bgfx::setTexture(0, sampler_, bgfx::TextureHandle{key.texture},
                   key.sampler_flags);
  bgfx::submit(key.view_id, program_);
}

void TextRenderer::SetQuadBatchingEnabled(const bool enabled) noexcept {
  quad_batching_enabled_ = enabled;
}

void TextRenderer::FlushQuadBatch() {
  if (quad_batch_vertices_.empty()) {
    return;
  }
  SubmitQuadRun(quad_batch_key_, quad_batch_vertices_, quad_batch_indices_);

  quad_batch_vertices_.clear();
  quad_batch_indices_.clear();
}

void TextRenderer::DrawQuad(const std::uint8_t view_id, const float x,
                            const float y, const float width,
                            const float height,
                            const std::uint32_t color_argb) {
  SubmitQuad(view_id, x, y, width, height, 0.0f, 0.0f, 1.0f, 1.0f,
             color_argb, white_texture_, 0);
}

void TextRenderer::DrawTexturedQuad(
    const std::uint8_t view_id, const float x, const float y,
    const float width, const float height, const bgfx::TextureHandle texture,
    const std::uint32_t color_argb, const std::uint64_t sampler_flags,
    const float u0, const float v0, const float u1, const float v1) {
  SubmitQuad(view_id, x, y, width, height, u0, v0, u1, v1, color_argb,
             texture, sampler_flags);
}

void TextRenderer::DrawTexturedQuadCorners(
    const std::uint8_t view_id, const float x, const float y,
    const float width, const float height, const bgfx::TextureHandle texture,
    const std::array<float, 8>& corner_uvs, const std::uint32_t color_argb,
    const std::uint64_t sampler_flags) {
  SubmitQuadCorners(view_id, x, y, width, height, corner_uvs, color_argb,
                    texture, sampler_flags);
}

std::uint32_t TextRenderer::ArgbToAbgr(const std::uint32_t argb) {
  const std::uint32_t alpha = (argb >> 24u) & 0xffu;
  const std::uint32_t red = (argb >> 16u) & 0xffu;
  const std::uint32_t green = (argb >> 8u) & 0xffu;
  const std::uint32_t blue = argb & 0xffu;
  return (alpha << 24u) |
         (static_cast<std::uint32_t>(blue * alpha / 255u) << 16u) |
         (static_cast<std::uint32_t>(green * alpha / 255u) << 8u) |
         static_cast<std::uint32_t>(red * alpha / 255u);
}

}
