#pragma once

#include <cstdint>

namespace openwow::render {

enum class RuntimeRenderAssetKind : std::uint8_t {
  kNone = 0,
  kM2 = 1,
  kAreaScene = 2,
};

struct RuntimeRenderAssetReadinessState {
  RuntimeRenderAssetKind kind = RuntimeRenderAssetKind::kNone;
  bool area_scene_primary_ready = false;
  bool area_scene_dependencies_ready = false;
  bool m2_payload_bound = false;
  bool m2_payload_ready = false;
};

[[nodiscard]] inline bool IsRuntimeRenderAssetAreaScene(
    const RuntimeRenderAssetReadinessState& state) {
  return state.kind == RuntimeRenderAssetKind::kAreaScene;
}

[[nodiscard]] inline bool IsRuntimeRenderAssetReady(
    const RuntimeRenderAssetReadinessState& state) {
  switch (state.kind) {
    case RuntimeRenderAssetKind::kAreaScene:
      return state.area_scene_primary_ready &&
             state.area_scene_dependencies_ready;
    case RuntimeRenderAssetKind::kM2:
      return !state.m2_payload_bound || state.m2_payload_ready;
    case RuntimeRenderAssetKind::kNone:
    default:
      return false;
  }
}

}
