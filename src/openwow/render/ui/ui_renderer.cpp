#include "openwow/render/ui/ui_renderer.h"

#include "openwow/render/backend/bgfx/renderer_context_services.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bgfx/embedded_shader.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>

namespace openwow::render::ui {

bgfx::VertexLayout UiRenderer::UiVertex::layout;

void UiRenderer::UiVertex::InitLayout() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;
  layout.begin()
      .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();
}

bool UiRenderer::Init() {
  Shutdown();
  UiVertex::InitLayout();

  if (!openwow::render::IsRendererContextActive()) {
    return false;
  }

  program_ = openwow::render::CreateEmbeddedProgram(
      openwow::render::ShaderProgramId::UiMaterial, bgfx::getRendererType());
  s_tex_ = bgfx::createUniform("s_uiTex", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(program_) || !bgfx::isValid(s_tex_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "UiRenderer: material shader program load failed");
    Shutdown();
    return false;
  }
  s_mask_ = bgfx::createUniform("s_uiMask", bgfx::UniformType::Sampler);
  u_material_ = bgfx::createUniform("u_uiMaterial", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(s_mask_) || !bgfx::isValid(u_material_)) {
    Shutdown();
    return false;
  }

  const std::uint32_t white = 0xFFFFFFFF;
  const bgfx::Memory* mem = bgfx::copy(&white, 4);
  white_pixel_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                        BGFX_TEXTURE_NONE, mem);
  if (!bgfx::isValid(white_pixel_)) {
    Shutdown();
    return false;
  }

  ok_ = true;
  return true;
}

void UiRenderer::Shutdown() {

  batch_vertices_.clear();
  batch_indices_.clear();
  if (openwow::render::IsRendererContextActive()) {
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(s_tex_)) bgfx::destroy(s_tex_);
    if (bgfx::isValid(s_mask_)) bgfx::destroy(s_mask_);
    if (bgfx::isValid(u_material_)) bgfx::destroy(u_material_);
  }
  if (openwow::render::IsRendererContextActive() &&
      bgfx::isValid(white_pixel_)) {
    bgfx::destroy(white_pixel_);
  }
  program_ = BGFX_INVALID_HANDLE;
  s_tex_ = BGFX_INVALID_HANDLE;
  s_mask_ = BGFX_INVALID_HANDLE;
  u_material_ = BGFX_INVALID_HANDLE;
  white_pixel_ = BGFX_INVALID_HANDLE;
  ok_ = false;
}

void UiRenderer::Begin(int view_id, int width, int height) {

  FlushSubmissionBatch();
  view_id_ = view_id;
  width_ = width;
  height_ = height;
  scissor_active_ = false;
  scissor_stack_depth_ = 0u;
  if (!ok_ || width_ <= 0 || height_ <= 0) {
    return;
  }

  const auto bgfx_view_id = static_cast<std::uint16_t>(view_id_);
  bgfx::setViewFrameBuffer(bgfx_view_id, BGFX_INVALID_HANDLE);
  bgfx::setViewClear(bgfx_view_id, BGFX_CLEAR_NONE);
  bgfx::setViewMode(bgfx_view_id, bgfx::ViewMode::Sequential);

  float view[16];
  float proj[16];
  bx::mtxIdentity(view);
  bx::mtxOrtho(proj,
               0.0f,
               static_cast<float>(width_),
               static_cast<float>(height_),
               0.0f,
               0.0f,
               1000.0f,
               0.0f,
               bgfx::getCaps()->homogeneousDepth);
  bgfx::setViewTransform(bgfx_view_id, view, proj);
  bgfx::setViewRect(bgfx_view_id, 0, 0, static_cast<uint16_t>(width_),
                    static_cast<uint16_t>(height_));
}

UiRenderer::DrawStateKey UiRenderer::BuildDrawStateKey(
    const std::uint64_t bgfx_state, const float alpha_reference,
    const bgfx::TextureHandle texture, const std::uint64_t sampler_flags,
    const bgfx::TextureHandle mask_texture, const bool desaturated,
    const bool flip_texture_y) const noexcept {

  const bool use_mask = bgfx::isValid(mask_texture);
  return DrawStateKey{
      .bgfx_state = bgfx_state,
      .sampler_flags = sampler_flags,
      .texture = texture.idx,
      .mask_texture = use_mask ? mask_texture.idx : white_pixel_.idx,
      .material = {alpha_reference, desaturated ? 1.0f : 0.0f,
                   use_mask ? 1.0f : 0.0f, flip_texture_y ? 1.0f : 0.0f},
      .scissor_active = scissor_active_,
      .scissor = {scissor_x_, scissor_y_, scissor_w_, scissor_h_},
  };
}

bool UiRenderer::SubmitRun(const DrawStateKey& key,
                           const std::span<const MeshVertex> vertices,
                           const std::span<const std::uint16_t> indices) {

  if (!ok_) {
    return false;
  }
  const auto vertex_count = static_cast<std::uint32_t>(vertices.size());
  const auto index_count = static_cast<std::uint32_t>(indices.size());
  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  if (!detail::HasTransientSubmissionCapacity(
          bgfx::getAvailTransientVertexBuffer(vertex_count, UiVertex::layout),
          bgfx::getAvailTransientIndexBuffer(index_count), vertex_count,
          index_count)) {
    return false;
  }
  bgfx::allocTransientVertexBuffer(&tvb, vertex_count, UiVertex::layout);
  bgfx::allocTransientIndexBuffer(&tib, index_count);

  auto* gpu_vertices = reinterpret_cast<UiVertex*>(tvb.data);
  for (std::uint32_t index = 0u; index < vertex_count; ++index) {
    const auto& source = vertices[index];
    gpu_vertices[index] =
        UiVertex{source.x, source.y, source.u, source.v, source.abgr};
  }
  std::copy(indices.begin(), indices.end(),
            reinterpret_cast<std::uint16_t*>(tib.data));

  bgfx::setState(key.bgfx_state);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::setTexture(0, s_tex_, bgfx::TextureHandle{key.texture},
                   key.sampler_flags);
  bgfx::setTexture(1, s_mask_, bgfx::TextureHandle{key.mask_texture},
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::setUniform(u_material_, key.material.data());
  if (key.scissor_active) {
    bgfx::setScissor(key.scissor.x, key.scissor.y, key.scissor.width,
                     key.scissor.height);
  }
  bgfx::submit(static_cast<uint16_t>(view_id_), program_);
  return true;
}

bool UiRenderer::EmitRun(const DrawStateKey& key,
                         const std::span<const MeshVertex> vertices,
                         const std::span<const std::uint16_t> indices,
                         const bool mergeable) {
  if (!batching_enabled_ || !mergeable) {
    FlushSubmissionBatch();
    return SubmitRun(key, vertices, indices);
  }

  if (!batch_vertices_.empty() && !(batch_key_ == key)) {
    FlushSubmissionBatch();
  }

  const auto vertex_count = static_cast<std::uint32_t>(vertices.size());
  const auto index_count = static_cast<std::uint32_t>(indices.size());
  const auto pending_vertices = static_cast<std::uint32_t>(batch_vertices_.size());

  if (pending_vertices + vertex_count > kMaxBatchedVertices ||
      !detail::HasTransientSubmissionCapacity(
          bgfx::getAvailTransientVertexBuffer(pending_vertices + vertex_count,
                                              UiVertex::layout),
          bgfx::getAvailTransientIndexBuffer(
              static_cast<std::uint32_t>(batch_indices_.size()) + index_count),
          pending_vertices + vertex_count,
          static_cast<std::uint32_t>(batch_indices_.size()) + index_count)) {
    FlushSubmissionBatch();
    if (!detail::HasTransientSubmissionCapacity(
            bgfx::getAvailTransientVertexBuffer(vertex_count, UiVertex::layout),
            bgfx::getAvailTransientIndexBuffer(index_count), vertex_count,
            index_count)) {
      return false;
    }
  }

  const auto base_vertex =
      static_cast<std::uint16_t>(batch_vertices_.size());
  if (batch_vertices_.empty()) {

    batch_key_ = key;
  }
  batch_vertices_.insert(batch_vertices_.end(), vertices.begin(),
                         vertices.end());

  for (const std::uint16_t index : indices) {
    batch_indices_.push_back(static_cast<std::uint16_t>(base_vertex + index));
  }
  return true;
}

void UiRenderer::SetSubmissionBatchingEnabled(const bool enabled) noexcept {
  batching_enabled_ = enabled;
}

void UiRenderer::FlushSubmissionBatch() {
  if (batch_vertices_.empty()) {
    return;
  }

  static_cast<void>(SubmitRun(batch_key_, batch_vertices_, batch_indices_));

  batch_vertices_.clear();
  batch_indices_.clear();
}

bool UiRenderer::Submit(const Quad& quad) {
  if (!ok_ || !bgfx::isValid(quad.texture) || width_ <= 0 || height_ <= 0) {
    return false;
  }

  std::array<MeshVertex, 4> quad_vertices{};
  auto* v = quad_vertices.data();
  const float x0 = quad.x;
  const float y0 = quad.y;
  const float x1 = quad.x + quad.w;
  const float y1 = quad.y + quad.h;
  const auto vertex_color = [&](std::size_t index) -> std::uint32_t {
    return quad.has_vertex_colors ? quad.vertex_abgr[index] : quad.abgr;
  };
  if (quad.has_custom_uv_quad) {
    v[0] = MeshVertex{x0, y0, quad.uv_quad[0].u, quad.uv_quad[0].v, vertex_color(0)};
    v[1] = MeshVertex{x1, y0, quad.uv_quad[1].u, quad.uv_quad[1].v, vertex_color(1)};
    v[2] = MeshVertex{x1, y1, quad.uv_quad[2].u, quad.uv_quad[2].v, vertex_color(2)};
    v[3] = MeshVertex{x0, y1, quad.uv_quad[3].u, quad.uv_quad[3].v, vertex_color(3)};
  } else if (!quad.rotate_90) {
    v[0] = MeshVertex{x0, y0, quad.u0, quad.v0, vertex_color(0)};
    v[1] = MeshVertex{x1, y0, quad.u1, quad.v0, vertex_color(1)};
    v[2] = MeshVertex{x1, y1, quad.u1, quad.v1, vertex_color(2)};
    v[3] = MeshVertex{x0, y1, quad.u0, quad.v1, vertex_color(3)};
  } else {

    v[0] = MeshVertex{x0, y0, quad.u0, quad.v1, vertex_color(0)};
    v[1] = MeshVertex{x1, y0, quad.u0, quad.v0, vertex_color(1)};
    v[2] = MeshVertex{x1, y1, quad.u1, quad.v0, vertex_color(2)};
    v[3] = MeshVertex{x0, y1, quad.u1, quad.v1, vertex_color(3)};
  }

  if (std::fabs(quad.rotation_radians) > 0.000001f) {
    const float origin_x = quad.has_rotation_origin ? quad.rotation_origin_x : (x0 + x1) * 0.5f;
    const float origin_y = quad.has_rotation_origin ? quad.rotation_origin_y : (y0 + y1) * 0.5f;
    const float c = std::cos(quad.rotation_radians);
    const float s = std::sin(quad.rotation_radians);
    for (std::size_t index = 0; index < 4; ++index) {
      const float dx = v[index].x - origin_x;
      const float dy = v[index].y - origin_y;
      v[index].x = origin_x + dx * c - dy * s;
      v[index].y = origin_y + dx * s + dy * c;
    }
  }

  static constexpr std::array<std::uint16_t, 6> kQuadIndices{0u, 1u, 2u,
                                                             0u, 2u, 3u};

  const MaterialState material = ResolveMaterialState(quad.blend);
  return EmitRun(BuildDrawStateKey(material.bgfx_state,
                                   material.alpha_reference, quad.texture,
                                   quad.sampler_flags, quad.mask_texture,
                                   quad.desaturated, quad.flip_texture_y),
                 quad_vertices, kQuadIndices, true);
}

bool UiRenderer::SubmitMesh(const Mesh& mesh) {
  if (!ok_ || !bgfx::isValid(mesh.texture) || width_ <= 0 || height_ <= 0 ||
      mesh.vertices.empty() || mesh.indices.empty()) {
    return false;
  }

  const MaterialState material = ResolveMaterialState(mesh.blend);
  const bool is_triangle_strip =
      mesh.topology == PrimitiveTopology::kTriangleStrip;
  std::uint64_t state = material.bgfx_state;
  if (is_triangle_strip) {
    state |= BGFX_STATE_PT_TRISTRIP;
  }

  return EmitRun(
      BuildDrawStateKey(state, material.alpha_reference, mesh.texture,
                        mesh.sampler_flags, BGFX_INVALID_HANDLE,
                        mesh.desaturated, false),
      mesh.vertices, mesh.indices, !is_triangle_strip);
}

bool UiRenderer::SubmitSolidMesh(const Mesh& mesh) {
  if (!ok_ || !bgfx::isValid(white_pixel_)) {
    return false;
  }
  Mesh solid = mesh;
  solid.texture = white_pixel_;
  return SubmitMesh(solid);
}

void UiRenderer::End() {

  FlushSubmissionBatch();
}

void UiRenderer::SetScissor(float x, float y, float w, float h) {

  auto rect = detail::ComputeClampedScissorRect(
      x, y, w, h, width_, height_);
  if (scissor_active_) {
    if (scissor_stack_depth_ < scissor_stack_.size()) {
      scissor_stack_[scissor_stack_depth_++] = {
          scissor_x_, scissor_y_, scissor_w_, scissor_h_};
    }
    rect = detail::IntersectScissorRects(
        {scissor_x_, scissor_y_, scissor_w_, scissor_h_}, rect);
  }
  scissor_active_ = true;
  scissor_x_ = rect.x;
  scissor_y_ = rect.y;
  scissor_w_ = rect.width;
  scissor_h_ = rect.height;
}

void UiRenderer::ClearScissor() {
  if (scissor_stack_depth_ == 0u) {
    scissor_active_ = false;
    return;
  }
  const auto rect = scissor_stack_[--scissor_stack_depth_];
  scissor_x_ = rect.x;
  scissor_y_ = rect.y;
  scissor_w_ = rect.width;
  scissor_h_ = rect.height;
  scissor_active_ = true;
}

bool UiRenderer::SubmitSolid(const Quad& quad) {
  if (!ok_ || width_ <= 0 || height_ <= 0 || !bgfx::isValid(white_pixel_)) {
    return false;
  }

  Quad solid = quad;
  solid.texture = white_pixel_;
  solid.u0 = 0.0f;
  solid.v0 = 0.0f;
  solid.u1 = 1.0f;
  solid.v1 = 1.0f;
  return Submit(solid);
}

}
