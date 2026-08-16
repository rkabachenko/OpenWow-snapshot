#include "openwow/render/world/wmo/wmo_portal_fill_renderer.h"

#include "openwow/render/resources/shaders/shader_registry.h"

#include <limits>

namespace openwow::render {

namespace {

struct PortalFillVertex {
  float x;
  float y;
  float z;
  std::uint32_t abgr;
};
static_assert(sizeof(PortalFillVertex) == 16u);

constexpr float kRetailPortalFillDepth = 1.0f;

constexpr std::uint64_t kRetailPortalFillState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
    BGFX_STATE_DEPTH_TEST_LEQUAL;

[[nodiscard]] std::uint32_t ArgbToAbgr(const std::uint32_t argb) {
  return (argb & 0xFF00FF00u) | ((argb & 0x00FF0000u) >> 16u) |
         ((argb & 0x000000FFu) << 16u);
}

}

bool WmoPortalFillRenderer::Initialize() {
  if (bgfx::isValid(program_)) {
    return true;
  }
  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();
  const auto result = TryCreateEmbeddedProgram(ShaderProgramId::WmoPortalFill,
                                               bgfx::getRendererType());
  program_ = result.handle;
  return static_cast<bool>(result);
}

void WmoPortalFillRenderer::Shutdown() {
  if (bgfx::isValid(program_)) {
    bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;
  }
}

bool WmoPortalFillRenderer::IsValid() const {
  return bgfx::isValid(program_);
}

void WmoPortalFillRenderer::Render(
    const std::uint8_t view_id,
    const world::WmoExteriorPortalFillBatch& fills,
    const std::uint32_t argb, const std::uint16_t viewport_width,
    const std::uint16_t viewport_height) {
  if (!bgfx::isValid(program_) || fills.fills.empty()) {
    return;
  }

  constexpr std::uint32_t kIndexLimit =
      std::numeric_limits<std::uint16_t>::max();
  std::uint32_t vertex_count = 0u;
  std::uint32_t index_count = 0u;
  std::size_t drawn_fills = 0u;
  for (const world::WmoExteriorPortalFill& fill : fills.fills) {
    if (fill.vertex_count < 3u ||
        vertex_count + fill.vertex_count > kIndexLimit) {
      break;
    }
    vertex_count += fill.vertex_count;
    index_count += (fill.vertex_count - 2u) * 3u;
    ++drawn_fills;
  }
  if (vertex_count == 0u || index_count == 0u) {
    return;
  }
  if (bgfx::getAvailTransientVertexBuffer(vertex_count, layout_) <
          vertex_count ||
      bgfx::getAvailTransientIndexBuffer(index_count) < index_count) {
    return;
  }

  bgfx::TransientVertexBuffer vertex_buffer;
  bgfx::TransientIndexBuffer index_buffer;
  bgfx::allocTransientVertexBuffer(&vertex_buffer, vertex_count, layout_);
  bgfx::allocTransientIndexBuffer(&index_buffer, index_count);
  auto* const vertices =
      reinterpret_cast<PortalFillVertex*>(vertex_buffer.data);
  auto* const indices = reinterpret_cast<std::uint16_t*>(index_buffer.data);

  const std::uint32_t abgr = ArgbToAbgr(argb);
  std::uint32_t vertex_cursor = 0u;
  std::uint32_t index_cursor = 0u;
  for (std::size_t f = 0u; f < drawn_fills; ++f) {
    const world::WmoExteriorPortalFill& fill = fills.fills[f];
    const std::uint32_t base = vertex_cursor;
    for (std::uint32_t i = 0u; i < fill.vertex_count; ++i) {
      const auto& ndc = fills.vertices[fill.first_vertex + i];
      vertices[vertex_cursor++] = PortalFillVertex{
          ndc[0], ndc[1], kRetailPortalFillDepth, abgr};
    }

    for (std::uint32_t i = 0u; i + 2u < fill.vertex_count; ++i) {
      indices[index_cursor++] = static_cast<std::uint16_t>(base);
      indices[index_cursor++] = static_cast<std::uint16_t>(base + i + 1u);
      indices[index_cursor++] = static_cast<std::uint16_t>(base + i + 2u);
    }
  }

  bgfx::setVertexBuffer(0, &vertex_buffer);
  bgfx::setIndexBuffer(&index_buffer);

  if (viewport_width != 0u && viewport_height != 0u) {
    bgfx::setScissor(0, 0, viewport_width, viewport_height);
  }
  bgfx::setState(kRetailPortalFillState);
  bgfx::submit(view_id, program_);
}

}
