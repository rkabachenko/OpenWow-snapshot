#include "openwow/render/api/frame_graph.h"

namespace openwow::render::api {

const char* FrameGraphPassName(FrameGraphPassId id) noexcept {
  switch (id) {
    case FrameGraphPassId::ShadowDepth:
      return "ShadowDepth";
    case FrameGraphPassId::Reflection:
      return "Reflection";
    case FrameGraphPassId::Refraction:
      return "Refraction";
    case FrameGraphPassId::SceneOpaque:
      return "SceneOpaque";
    case FrameGraphPassId::SceneAlpha:
      return "SceneAlpha";
    case FrameGraphPassId::Water:
      return "Water";
    case FrameGraphPassId::Particles:
      return "Particles";
    case FrameGraphPassId::PostProcess:
      return "PostProcess";
    case FrameGraphPassId::WorldUi:
      return "WorldUi";
    case FrameGraphPassId::DebugOverlay:
      return "DebugOverlay";
    case FrameGraphPassId::Present:
      return "Present";
  }
  return "Unknown";
}

void FrameGraph::Reset(ViewId first_view_id) {
  passes_.clear();
  next_view_id_ = first_view_id;
}

FrameGraphPass& FrameGraph::AddPass(FrameGraphPassId id, RenderExtent extent) {
  return AddPass(id, extent, 1);
}

FrameGraphPass& FrameGraph::AddPass(FrameGraphPassId id, RenderExtent extent,
                                    std::uint16_t view_count) {
  FrameGraphPass pass;
  pass.id = id;
  pass.view_count = view_count == 0 ? 1 : view_count;
  pass.view_id = AllocateViewRange(pass.view_count);
  pass.name = FrameGraphPassName(id);
  pass.extent = extent;

  switch (id) {
    case FrameGraphPassId::ShadowDepth:
      pass.depth_target = FrameGraphTarget::ShadowMap;
      break;
    case FrameGraphPassId::Reflection:
      pass.color_target = FrameGraphTarget::ReflectionColor;
      pass.depth_target = FrameGraphTarget::SceneDepth;
      break;
    case FrameGraphPassId::Refraction:
      pass.color_target = FrameGraphTarget::RefractionColor;
      pass.depth_target = FrameGraphTarget::SceneDepth;
      break;
    case FrameGraphPassId::SceneOpaque:
    case FrameGraphPassId::SceneAlpha:
    case FrameGraphPassId::Water:
    case FrameGraphPassId::Particles:
      pass.color_target = FrameGraphTarget::SceneColor;
      pass.depth_target = FrameGraphTarget::SceneDepth;
      pass.writes_scene_color = true;
      break;
    case FrameGraphPassId::PostProcess:
      pass.color_target = FrameGraphTarget::Backbuffer;
      pass.reads_scene_color = true;
      break;
    case FrameGraphPassId::WorldUi:
    case FrameGraphPassId::DebugOverlay:
    case FrameGraphPassId::Present:
      pass.color_target = FrameGraphTarget::Backbuffer;
      break;
  }

  passes_.push_back(std::move(pass));
  return passes_.back();
}

void FrameGraph::BuildDefaultWorldFrame(RenderExtent extent, ViewId first_view_id) {
  Reset(first_view_id);
  AddPass(FrameGraphPassId::ShadowDepth, extent);
  AddPass(FrameGraphPassId::Reflection, extent);
  AddPass(FrameGraphPassId::Refraction, extent);
  AddPass(FrameGraphPassId::SceneOpaque, extent);
  AddPass(FrameGraphPassId::SceneAlpha, extent);
  AddPass(FrameGraphPassId::Water, extent);
  AddPass(FrameGraphPassId::Particles, extent);
  AddPass(FrameGraphPassId::PostProcess, extent);
  AddPass(FrameGraphPassId::WorldUi, extent);
  AddPass(FrameGraphPassId::DebugOverlay, extent);
  AddPass(FrameGraphPassId::Present, extent);
}

const FrameGraphPass* FrameGraph::FindPass(FrameGraphPassId id) const noexcept {
  for (const auto& pass : passes_) {
    if (pass.id == id) {
      return &pass;
    }
  }
  return nullptr;
}

bool FrameGraph::IsBefore(FrameGraphPassId lhs, FrameGraphPassId rhs) const noexcept {
  int lhs_index = -1;
  int rhs_index = -1;
  for (std::size_t i = 0; i < passes_.size(); ++i) {
    if (passes_[i].id == lhs) {
      lhs_index = static_cast<int>(i);
    }
    if (passes_[i].id == rhs) {
      rhs_index = static_cast<int>(i);
    }
  }
  return lhs_index >= 0 && rhs_index >= 0 && lhs_index < rhs_index;
}

FrameGraph::ViewId FrameGraph::AllocateViewRange(std::uint16_t view_count) {
  const ViewId first = next_view_id_;
  next_view_id_ = static_cast<ViewId>(next_view_id_ + (view_count == 0 ? 1 : view_count));
  return first;
}

}
