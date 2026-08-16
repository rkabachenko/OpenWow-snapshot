
#include "openwow/render/diagnostics/debug_draw_renderer.h"

#include "openwow/debug/inspection/render_debug.h"
#include "openwow/render/resources/shaders/shader_registry.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace openwow::render {
namespace {

std::array<std::uint8_t, 7> GlyphRows(unsigned char value) {
  value = static_cast<unsigned char>(std::toupper(value));
  switch (value) {
    case ' ': return {};
    case '!': return {4, 4, 4, 4, 4, 0, 4};
    case '"': return {10, 10, 10, 0, 0, 0, 0};
    case '#': return {10, 31, 10, 10, 31, 10, 0};
    case '$': return {4, 15, 20, 14, 5, 30, 4};
    case '%': return {24, 25, 2, 4, 8, 19, 3};
    case '&': return {12, 18, 20, 8, 21, 18, 13};
    case '\'': return {4, 4, 8, 0, 0, 0, 0};
    case '(': return {2, 4, 8, 8, 8, 4, 2};
    case ')': return {8, 4, 2, 2, 2, 4, 8};
    case '*': return {0, 21, 14, 31, 14, 21, 0};
    case '+': return {0, 4, 4, 31, 4, 4, 0};
    case ',': return {0, 0, 0, 0, 0, 4, 8};
    case '-': return {0, 0, 0, 31, 0, 0, 0};
    case '.': return {0, 0, 0, 0, 0, 0, 4};
    case '/': return {1, 1, 2, 4, 8, 16, 16};
    case '0': return {14, 17, 19, 21, 25, 17, 14};
    case '1': return {4, 12, 4, 4, 4, 4, 14};
    case '2': return {14, 17, 1, 2, 4, 8, 31};
    case '3': return {30, 1, 1, 14, 1, 1, 30};
    case '4': return {2, 6, 10, 18, 31, 2, 2};
    case '5': return {31, 16, 16, 30, 1, 1, 30};
    case '6': return {14, 16, 16, 30, 17, 17, 14};
    case '7': return {31, 1, 2, 4, 8, 8, 8};
    case '8': return {14, 17, 17, 14, 17, 17, 14};
    case '9': return {14, 17, 17, 15, 1, 1, 14};
    case ':': return {0, 4, 0, 0, 4, 0, 0};
    case ';': return {0, 4, 0, 0, 4, 4, 8};
    case '<': return {2, 4, 8, 16, 8, 4, 2};
    case '=': return {0, 0, 31, 0, 31, 0, 0};
    case '>': return {8, 4, 2, 1, 2, 4, 8};
    case '?': return {14, 17, 1, 2, 4, 0, 4};
    case '@': return {14, 17, 23, 21, 23, 16, 14};
    case 'A': return {14, 17, 17, 31, 17, 17, 17};
    case 'B': return {30, 17, 17, 30, 17, 17, 30};
    case 'C': return {14, 17, 16, 16, 16, 17, 14};
    case 'D': return {30, 17, 17, 17, 17, 17, 30};
    case 'E': return {31, 16, 16, 30, 16, 16, 31};
    case 'F': return {31, 16, 16, 30, 16, 16, 16};
    case 'G': return {14, 17, 16, 23, 17, 17, 15};
    case 'H': return {17, 17, 17, 31, 17, 17, 17};
    case 'I': return {14, 4, 4, 4, 4, 4, 14};
    case 'J': return {7, 2, 2, 2, 2, 18, 12};
    case 'K': return {17, 18, 20, 24, 20, 18, 17};
    case 'L': return {16, 16, 16, 16, 16, 16, 31};
    case 'M': return {17, 27, 21, 21, 17, 17, 17};
    case 'N': return {17, 25, 21, 19, 17, 17, 17};
    case 'O': return {14, 17, 17, 17, 17, 17, 14};
    case 'P': return {30, 17, 17, 30, 16, 16, 16};
    case 'Q': return {14, 17, 17, 17, 21, 18, 13};
    case 'R': return {30, 17, 17, 30, 20, 18, 17};
    case 'S': return {15, 16, 16, 14, 1, 1, 30};
    case 'T': return {31, 4, 4, 4, 4, 4, 4};
    case 'U': return {17, 17, 17, 17, 17, 17, 14};
    case 'V': return {17, 17, 17, 17, 17, 10, 4};
    case 'W': return {17, 17, 17, 21, 21, 21, 10};
    case 'X': return {17, 17, 10, 4, 10, 17, 17};
    case 'Y': return {17, 17, 10, 4, 4, 4, 4};
    case 'Z': return {31, 1, 2, 4, 8, 16, 31};
    case '[': return {14, 8, 8, 8, 8, 8, 14};
    case '\\': return {16, 16, 8, 4, 2, 1, 1};
    case ']': return {14, 2, 2, 2, 2, 2, 14};
    case '^': return {4, 10, 17, 0, 0, 0, 0};
    case '_': return {0, 0, 0, 0, 0, 0, 31};
    case '`': return {8, 4, 2, 0, 0, 0, 0};
    case '{': return {2, 4, 4, 8, 4, 4, 2};
    case '|': return {4, 4, 4, 4, 4, 4, 4};
    case '}': return {8, 4, 4, 2, 4, 4, 8};
    case '~': return {0, 0, 8, 21, 2, 0, 0};
    default: return {31, 17, 21, 17, 21, 17, 31};
  }
}

void TransformPoint(const float* matrix, const debug::Vec3& point,
                    float* result) {
  const float source[4]{point.x, point.y, point.z, 1.0f};
  bx::vec4MulMtx(result, source, matrix);
}

}

bgfx::VertexLayout DebugVertex::s_layout;

void DebugVertex::Init() {
  s_layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();
}

std::uint32_t DebugDrawRenderer::ColorToABGR(const debug::DebugColor& c) {
  const auto channel = [](const float value) {
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
  };
  const auto r = channel(c.r);
  const auto g = channel(c.g);
  const auto b = channel(c.b);
  const auto a = channel(c.a);
  return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r;
}

void DebugDrawRenderer::PushVertex(float x, float y, float z,
                                   std::uint32_t abgr) {
  vertices_.push_back({x, y, z, abgr});
}

void DebugDrawRenderer::PushTriangleVertex(float x, float y, float z,
                                           std::uint32_t abgr) {
  triangle_vertices_.push_back({x, y, z, abgr});
}

void DebugDrawRenderer::TessellateLine(const debug::Vec3& from,
                                       const debug::Vec3& to,
                                       std::uint32_t abgr) {
  PushVertex(from.x, from.y, from.z, abgr);
  PushVertex(to.x, to.y, to.z, abgr);
}

bool DebugDrawRenderer::TessellateThickLine(
    const debug::Vec3& from, const debug::Vec3& to, const float thickness,
    const std::uint32_t abgr, const float* view_mtx, const float* proj_mtx) {
  float view_proj[16];
  float inverse_view_proj[16];
  bx::mtxMul(view_proj, view_mtx, proj_mtx);
  bx::mtxInverse(inverse_view_proj, view_proj);

  float from_clip[4];
  float to_clip[4];
  TransformPoint(view_proj, from, from_clip);
  TransformPoint(view_proj, to, to_clip);
  const auto near_distance = [this](const float* point) {
    return homogeneous_depth_ ? point[2] + point[3] : point[2];
  };
  const float from_distance = near_distance(from_clip);
  const float to_distance = near_distance(to_clip);
  if (from_distance < 0.0f && to_distance < 0.0f) return false;
  if (from_distance < 0.0f || to_distance < 0.0f) {
    float* outside = from_distance < 0.0f ? from_clip : to_clip;
    const float* inside = from_distance < 0.0f ? to_clip : from_clip;
    const float outside_distance = from_distance < 0.0f
                                       ? from_distance
                                       : to_distance;
    const float inside_distance = from_distance < 0.0f
                                      ? to_distance
                                      : from_distance;
    const float interpolation =
        outside_distance / (outside_distance - inside_distance);
    for (std::size_t component = 0; component < 4; ++component) {
      outside[component] +=
          (inside[component] - outside[component]) * interpolation;
    }
  }
  if (from_clip[3] <= 1.0e-6f || to_clip[3] <= 1.0e-6f) return false;

  const float aspect = std::abs(proj_mtx[0]) > 1.0e-6f
                           ? std::abs(proj_mtx[5] / proj_mtx[0])
                           : 1.0f;
  const float virtual_width = kReferenceViewportHeight * aspect;
  const float from_x = from_clip[0] / from_clip[3];
  const float from_y = from_clip[1] / from_clip[3];
  const float to_x = to_clip[0] / to_clip[3];
  const float to_y = to_clip[1] / to_clip[3];
  const float delta_x = (to_x - from_x) * virtual_width;
  const float delta_y = (to_y - from_y) * kReferenceViewportHeight;
  const float length = std::hypot(delta_x, delta_y);
  if (length <= 1.0e-6f) return false;
  const float offset_x = -delta_y * thickness / (length * virtual_width);
  const float offset_y =
      delta_x * thickness / (length * kReferenceViewportHeight);
  const float from_z = from_clip[2] / from_clip[3];
  const float to_z = to_clip[2] / to_clip[3];
  const std::array<std::array<float, 3>, 4> ndc{{
      {from_x + offset_x, from_y + offset_y, from_z},
      {to_x + offset_x, to_y + offset_y, to_z},
      {to_x - offset_x, to_y - offset_y, to_z},
      {from_x - offset_x, from_y - offset_y, from_z},
  }};
  std::array<debug::Vec3, 4> world{};
  for (std::size_t index = 0; index < ndc.size(); ++index) {
    const float source[4]{ndc[index][0], ndc[index][1], ndc[index][2], 1.0f};
    float transformed[4];
    bx::vec4MulMtx(transformed, source, inverse_view_proj);
    if (std::abs(transformed[3]) <= 1.0e-6f) return false;
    const float inverse_w = 1.0f / transformed[3];
    world[index] = {transformed[0] * inverse_w, transformed[1] * inverse_w,
                    transformed[2] * inverse_w};
  }
  constexpr std::array<std::uint8_t, 6> indices{0, 1, 2, 0, 2, 3};
  for (const std::uint8_t index : indices) {
    PushTriangleVertex(world[index].x, world[index].y, world[index].z, abgr);
  }
  return true;
}

void DebugDrawRenderer::TessellateBBox(const debug::Vec3& mn,
                                       const debug::Vec3& mx,
                                       std::uint32_t abgr) {

  const float x0 = mn.x, y0 = mn.y, z0 = mn.z;
  const float x1 = mx.x, y1 = mx.y, z1 = mx.z;

  PushVertex(x0, y0, z0, abgr); PushVertex(x1, y0, z0, abgr);
  PushVertex(x1, y0, z0, abgr); PushVertex(x1, y0, z1, abgr);
  PushVertex(x1, y0, z1, abgr); PushVertex(x0, y0, z1, abgr);
  PushVertex(x0, y0, z1, abgr); PushVertex(x0, y0, z0, abgr);

  PushVertex(x0, y1, z0, abgr); PushVertex(x1, y1, z0, abgr);
  PushVertex(x1, y1, z0, abgr); PushVertex(x1, y1, z1, abgr);
  PushVertex(x1, y1, z1, abgr); PushVertex(x0, y1, z1, abgr);
  PushVertex(x0, y1, z1, abgr); PushVertex(x0, y1, z0, abgr);

  PushVertex(x0, y0, z0, abgr); PushVertex(x0, y1, z0, abgr);
  PushVertex(x1, y0, z0, abgr); PushVertex(x1, y1, z0, abgr);
  PushVertex(x1, y0, z1, abgr); PushVertex(x1, y1, z1, abgr);
  PushVertex(x0, y0, z1, abgr); PushVertex(x0, y1, z1, abgr);
}

void DebugDrawRenderer::TessellateSphere(const debug::Vec3& center,
                                         float radius, std::uint32_t abgr,
                                         int segments) {
  const float step = 2.0f * bx::kPi / static_cast<float>(segments);

  for (int i = 0; i < segments; ++i) {
    const float a0 = step * static_cast<float>(i);
    const float a1 = step * static_cast<float>(i + 1);
    PushVertex(center.x + radius * std::cos(a0),
               center.y + radius * std::sin(a0), center.z, abgr);
    PushVertex(center.x + radius * std::cos(a1),
               center.y + radius * std::sin(a1), center.z, abgr);
  }

  for (int i = 0; i < segments; ++i) {
    const float a0 = step * static_cast<float>(i);
    const float a1 = step * static_cast<float>(i + 1);
    PushVertex(center.x + radius * std::cos(a0), center.y,
               center.z + radius * std::sin(a0), abgr);
    PushVertex(center.x + radius * std::cos(a1), center.y,
               center.z + radius * std::sin(a1), abgr);
  }

  for (int i = 0; i < segments; ++i) {
    const float a0 = step * static_cast<float>(i);
    const float a1 = step * static_cast<float>(i + 1);
    PushVertex(center.x, center.y + radius * std::cos(a0),
               center.z + radius * std::sin(a0), abgr);
    PushVertex(center.x, center.y + radius * std::cos(a1),
               center.z + radius * std::sin(a1), abgr);
  }
}

void DebugDrawRenderer::TessellateCircle(const debug::Vec3& center,
                                         float radius,
                                         const debug::Vec3& normal,
                                         std::uint32_t abgr, int segments) {

  debug::Vec3 up = normal;

  const float len =
      std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
  if (len > 1e-6f) {
    up.x /= len;
    up.y /= len;
    up.z /= len;
  }

  debug::Vec3 ref = {0, 1, 0};
  if (std::abs(up.y) > 0.9f) ref = {1, 0, 0};

  debug::Vec3 t = {up.y * ref.z - up.z * ref.y,
                   up.z * ref.x - up.x * ref.z,
                   up.x * ref.y - up.y * ref.x};
  float tlen = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
  if (tlen > 1e-6f) {
    t.x /= tlen;
    t.y /= tlen;
    t.z /= tlen;
  }

  debug::Vec3 b = {up.y * t.z - up.z * t.y,
                   up.z * t.x - up.x * t.z,
                   up.x * t.y - up.y * t.x};

  const float step = 2.0f * bx::kPi / static_cast<float>(segments);
  for (int i = 0; i < segments; ++i) {
    const float a0 = step * static_cast<float>(i);
    const float a1 = step * static_cast<float>(i + 1);
    const float c0 = std::cos(a0), s0 = std::sin(a0);
    const float c1 = std::cos(a1), s1 = std::sin(a1);
    PushVertex(center.x + radius * (t.x * c0 + b.x * s0),
               center.y + radius * (t.y * c0 + b.y * s0),
               center.z + radius * (t.z * c0 + b.z * s0), abgr);
    PushVertex(center.x + radius * (t.x * c1 + b.x * s1),
               center.y + radius * (t.y * c1 + b.y * s1),
               center.z + radius * (t.z * c1 + b.z * s1), abgr);
  }
}

void DebugDrawRenderer::TessellateGrid(const debug::Vec3& origin, float size,
                                       float spacing, std::uint32_t abgr) {
  if (spacing <= 0.0f) return;
  const float half = size * 0.5f;
  const int lines = static_cast<int>(size / spacing) + 1;

  for (int i = 0; i < lines; ++i) {
    const float offset = -half + spacing * static_cast<float>(i);

    PushVertex(origin.x - half, origin.y, origin.z + offset, abgr);
    PushVertex(origin.x + half, origin.y, origin.z + offset, abgr);

    PushVertex(origin.x + offset, origin.y, origin.z - half, abgr);
    PushVertex(origin.x + offset, origin.y, origin.z + half, abgr);
  }
}

bool DebugDrawRenderer::TessellateFrustum(const debug::Mat4& vp,
                                          std::uint32_t abgr) {

  float inv[16];
  bx::mtxInverse(inv, vp.m);

  const float near_depth = homogeneous_depth_ ? -1.0f : 0.0f;
  const float ndc[8][3] = {
      {-1, -1, near_depth}, { 1, -1, near_depth},
      { 1,  1, near_depth}, {-1,  1, near_depth},
       {-1, -1, 1}, { 1, -1, 1}, { 1,  1, 1}, {-1,  1, 1},
  };

  float corners[8][3];
  for (int i = 0; i < 8; ++i) {
    const float source[4]{ndc[i][0], ndc[i][1], ndc[i][2], 1.0f};
    float tmp[4];
    bx::vec4MulMtx(tmp, source, inv);
    if (std::abs(tmp[3]) <= 1e-6f) return false;
    corners[i][0] = tmp[0] / tmp[3];
    corners[i][1] = tmp[1] / tmp[3];
    corners[i][2] = tmp[2] / tmp[3];
  }

  for (int i = 0; i < 4; ++i) {
    int j = (i + 1) % 4;
    PushVertex(corners[i][0], corners[i][1], corners[i][2], abgr);
    PushVertex(corners[j][0], corners[j][1], corners[j][2], abgr);
  }

  for (int i = 4; i < 8; ++i) {
    int j = 4 + ((i - 4 + 1) % 4);
    PushVertex(corners[i][0], corners[i][1], corners[i][2], abgr);
    PushVertex(corners[j][0], corners[j][1], corners[j][2], abgr);
  }

  for (int i = 0; i < 4; ++i) {
    PushVertex(corners[i][0], corners[i][1], corners[i][2], abgr);
    PushVertex(corners[i + 4][0], corners[i + 4][1], corners[i + 4][2], abgr);
  }
  return true;
}

bool DebugDrawRenderer::TessellateText(
    const debug::Vec3& position, const std::string& text, const float size,
    const std::uint32_t abgr, const float* view_mtx, const float* proj_mtx) {
  float view_position[4];
  float clip_position[4];
  TransformPoint(view_mtx, position, view_position);
  bx::vec4MulMtx(clip_position, view_position, proj_mtx);
  if (clip_position[3] <= 1.0e-6f || std::abs(proj_mtx[5]) <= 1.0e-6f) {
    return false;
  }

  float inverse_view[16];
  bx::mtxInverse(inverse_view, view_mtx);
  const debug::Vec3 right{inverse_view[0], inverse_view[1], inverse_view[2]};
  const debug::Vec3 up{inverse_view[4], inverse_view[5], inverse_view[6]};
  const float cell = 2.0f * clip_position[3] * size /
                     (kReferenceViewportHeight * std::abs(proj_mtx[5]) *
                      7.0f);
  float column_offset{};
  float row_offset{};
  bool produced{};
  for (const unsigned char value : text) {
    if (value == '\n') {
      column_offset = 0.0f;
      row_offset += 8.0f;
      continue;
    }
    if (value == '\t') {
      column_offset += 24.0f;
      continue;
    }
    if (value < 32 || value > 126) ++last_report_.replaced_text_byte_count;
    const auto rows = GlyphRows(value);
    for (std::size_t row = 0; row < rows.size(); ++row) {
      std::uint8_t bits = rows[row];
      std::size_t column{};
      while (column < 5) {
        while (column < 5 && (bits & (1u << (4u - column))) == 0) ++column;
        if (column == 5) break;
        const std::size_t begin = column;
        while (column < 5 && (bits & (1u << (4u - column))) != 0) ++column;
        const float y = -(row_offset + static_cast<float>(row) + 0.5f) * cell;
        const float x0 =
            (column_offset + static_cast<float>(begin) + 0.1f) * cell;
        const float x1 =
            (column_offset + static_cast<float>(column) - 0.1f) * cell;
        const debug::Vec3 from{position.x + right.x * x0 + up.x * y,
                               position.y + right.y * x0 + up.y * y,
                               position.z + right.z * x0 + up.z * y};
        const debug::Vec3 to{position.x + right.x * x1 + up.x * y,
                             position.y + right.y * x1 + up.y * y,
                             position.z + right.z * x1 + up.z * y};
        TessellateLine(from, to, abgr);
        produced = true;
      }
    }
    column_offset += 6.0f;
  }
  return produced;
}

bool DebugDrawRenderer::Initialize() {
  if (bgfx::isValid(program_)) {
    return true;
  }

  DebugVertex::Init();
  homogeneous_depth_ = bgfx::getCaps()->homogeneousDepth;
  const auto result = TryCreateEmbeddedProgram(ShaderProgramId::Debug,
                                               bgfx::getRendererType());
  program_ = result.handle;
  return static_cast<bool>(result);
}

void DebugDrawRenderer::Shutdown() {
  if (bgfx::isValid(program_)) {
    bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;
  }
  vertices_.clear();
  triangle_vertices_.clear();
  last_report_ = {};
}

bool DebugDrawRenderer::IsValid() const {
  return bgfx::isValid(program_);
}

std::uint32_t DebugDrawRenderer::Submit(
    const std::uint8_t view_id, const std::vector<DebugVertex>& vertices,
    const std::uint64_t primitive_state) {
  const std::size_t primitive_size =
      (primitive_state & BGFX_STATE_PT_LINES) != 0 ? 2u : 3u;
  std::size_t offset{};
  std::uint32_t draw_calls{};
  while (offset < vertices.size()) {
    const std::size_t remaining = vertices.size() - offset;
    const auto request = static_cast<std::uint32_t>(std::min<std::size_t>(
        remaining, std::numeric_limits<std::uint32_t>::max()));
    std::uint32_t count =
        bgfx::getAvailTransientVertexBuffer(request, DebugVertex::s_layout);
    count = static_cast<std::uint32_t>(count - count % primitive_size);
    if (count == 0) {
      last_report_.transient_vertex_shortfall += static_cast<std::uint32_t>(
          std::min<std::size_t>(remaining,
                                std::numeric_limits<std::uint32_t>::max()));
      break;
    }
    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer, count, DebugVertex::s_layout);
    std::memcpy(buffer.data, vertices.data() + offset,
                static_cast<std::size_t>(count) * sizeof(DebugVertex));

    bgfx::setVertexBuffer(0, &buffer);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ALPHA |
                   BGFX_STATE_MSAA | primitive_state);
    bgfx::submit(view_id, program_);
    offset += count;
    ++draw_calls;
  }
  return draw_calls;
}

void DebugDrawRenderer::Render(
    std::uint8_t view_id, const float* view_mtx, const float* proj_mtx,
    const std::vector<debug::DebugDrawCommand>& commands) {
  last_report_ = {};
  last_report_.command_count = static_cast<std::uint32_t>(
      std::min<std::size_t>(commands.size(),
                            std::numeric_limits<std::uint32_t>::max()));
  last_report_.program_valid = bgfx::isValid(program_);
  vertices_.clear();
  triangle_vertices_.clear();
  if (commands.empty() || !last_report_.program_valid || view_mtx == nullptr ||
      proj_mtx == nullptr) {
    return;
  }

  vertices_.reserve(std::max(vertices_.capacity(), commands.size() * 24u));
  triangle_vertices_.reserve(
      std::max(triangle_vertices_.capacity(), commands.size() * 6u));
  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

  for (const auto& cmd : commands) {
    if (cmd.type == debug::DebugDrawType::Line3D) {
      if (!vertices_.empty()) {
        last_report_.draw_call_count +=
            Submit(view_id, vertices_, BGFX_STATE_PT_LINES | BGFX_STATE_LINEAA);
        vertices_.clear();
      }
    } else if (!triangle_vertices_.empty()) {
      last_report_.draw_call_count += Submit(view_id, triangle_vertices_, 0);
      triangle_vertices_.clear();
    }
    const auto abgr = ColorToABGR(cmd.color);
    const std::size_t line_begin = vertices_.size();
    const std::size_t triangle_begin = triangle_vertices_.size();
    bool clipped{};
    switch (cmd.type) {
      case debug::DebugDrawType::Line3D:
        clipped = !TessellateThickLine(cmd.p0, cmd.p1, cmd.size, abgr,
                                       view_mtx, proj_mtx);
        break;
      case debug::DebugDrawType::BBox:
        TessellateBBox(cmd.p0, cmd.p1, abgr);
        break;
      case debug::DebugDrawType::Sphere:
        TessellateSphere(cmd.p0, cmd.radius, abgr, 32);
        break;
      case debug::DebugDrawType::Circle3D:
        TessellateCircle(cmd.p0, cmd.radius, cmd.normal, abgr, 32);
        break;
      case debug::DebugDrawType::Grid:
        TessellateGrid(cmd.p0, cmd.size, cmd.spacing, abgr);
        break;
      case debug::DebugDrawType::Frustum:
        clipped = !TessellateFrustum(cmd.view_proj, abgr);
        break;
      case debug::DebugDrawType::Text3D:
        clipped = !TessellateText(cmd.p0, cmd.text, cmd.size, abgr, view_mtx,
                                  proj_mtx);
        break;
    }
    if (vertices_.size() != line_begin ||
        triangle_vertices_.size() != triangle_begin) {
      ++last_report_.tessellated_command_count;
    } else if (clipped) {
      ++last_report_.unrendered_command_count;
    }
  }

  last_report_.draw_call_count += Submit(
      view_id, vertices_, BGFX_STATE_PT_LINES | BGFX_STATE_LINEAA);
  last_report_.draw_call_count += Submit(view_id, triangle_vertices_, 0);
}

}
