#pragma once

#include "openwow/render/api/render_scene.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::render::api {

enum class FrameGraphPassId : std::uint8_t {
  ShadowDepth,
  Reflection,
  Refraction,
  SceneOpaque,
  SceneAlpha,
  Water,
  Particles,
  PostProcess,
  WorldUi,
  DebugOverlay,
  Present,
};

enum class FrameGraphTarget : std::uint8_t {
  None,
  ShadowMap,
  ReflectionColor,
  RefractionColor,
  SceneColor,
  SceneDepth,
  Backbuffer,
};

struct FrameGraphPass {
  FrameGraphPassId id = FrameGraphPassId::Present;
  std::uint16_t view_id = 0;
  std::uint16_t view_count = 1;
  std::string name;
  RenderExtent extent{};
  FrameGraphTarget color_target = FrameGraphTarget::None;
  FrameGraphTarget depth_target = FrameGraphTarget::None;
  bool reads_scene_color = false;
  bool writes_scene_color = false;
};

[[nodiscard]] const char* FrameGraphPassName(FrameGraphPassId id) noexcept;

class FrameGraph {
 public:
  using ViewId = std::uint16_t;

  void Reset(ViewId first_view_id = 0);
  FrameGraphPass& AddPass(FrameGraphPassId id, RenderExtent extent);
  FrameGraphPass& AddPass(FrameGraphPassId id, RenderExtent extent,
                          std::uint16_t view_count);
  void BuildDefaultWorldFrame(RenderExtent extent, ViewId first_view_id = 0);

  [[nodiscard]] const std::vector<FrameGraphPass>& passes() const noexcept {
    return passes_;
  }

  [[nodiscard]] const FrameGraphPass* FindPass(FrameGraphPassId id) const noexcept;
  [[nodiscard]] bool IsBefore(FrameGraphPassId lhs, FrameGraphPassId rhs) const noexcept;

 private:
  ViewId AllocateViewRange(std::uint16_t view_count);

  std::vector<FrameGraphPass> passes_;
  ViewId next_view_id_ = 0;
};

}
