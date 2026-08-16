#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::debug {
struct DebugDrawCommand;
struct DebugColor;
struct Vec3;
struct Mat4;
}

namespace openwow::render {

struct DebugVertex {
  float x, y, z;
  std::uint32_t abgr;

  static void Init();
  static bgfx::VertexLayout s_layout;
};

struct DebugDrawRenderReport {
  std::uint32_t command_count{};
  std::uint32_t tessellated_command_count{};
  std::uint32_t draw_call_count{};
  std::uint32_t unrendered_command_count{};
  std::uint32_t replaced_text_byte_count{};
  std::uint32_t transient_vertex_shortfall{};
  bool program_valid{};
};

class DebugDrawRenderer {
 public:
  static constexpr float kReferenceViewportHeight = 1080.0f;

  bool Initialize();

  void Shutdown();

  void Render(std::uint8_t view_id, const float* view_mtx,
              const float* proj_mtx,
              const std::vector<debug::DebugDrawCommand>& commands);

  [[nodiscard]] bool IsValid() const;

  [[nodiscard]] const DebugDrawRenderReport& LastReport() const {
    return last_report_;
  }

 private:
  [[nodiscard]] static std::uint32_t ColorToABGR(
      const debug::DebugColor& color);

  void TessellateLine(const debug::Vec3& from, const debug::Vec3& to,
                      std::uint32_t abgr);

  bool TessellateThickLine(const debug::Vec3& from, const debug::Vec3& to,
                           float thickness, std::uint32_t abgr,
                           const float* view_mtx, const float* proj_mtx);

  void TessellateBBox(const debug::Vec3& min, const debug::Vec3& max,
                      std::uint32_t abgr);

  void TessellateSphere(const debug::Vec3& center, float radius,
                        std::uint32_t abgr, int segments = 32);

  void TessellateCircle(const debug::Vec3& center, float radius,
                        const debug::Vec3& normal, std::uint32_t abgr,
                        int segments = 32);

  void TessellateGrid(const debug::Vec3& origin, float size, float spacing,
                      std::uint32_t abgr);

  bool TessellateFrustum(const debug::Mat4& view_proj, std::uint32_t abgr);

  bool TessellateText(const debug::Vec3& position, const std::string& text,
                      float size, std::uint32_t abgr, const float* view_mtx,
                      const float* proj_mtx);

  void PushVertex(float x, float y, float z, std::uint32_t abgr);

  void PushTriangleVertex(float x, float y, float z, std::uint32_t abgr);

  std::uint32_t Submit(std::uint8_t view_id,
                       const std::vector<DebugVertex>& vertices,
                       std::uint64_t primitive_state);

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  std::vector<DebugVertex> vertices_;
  std::vector<DebugVertex> triangle_vertices_;
  DebugDrawRenderReport last_report_;
  bool homogeneous_depth_{};
};

}
