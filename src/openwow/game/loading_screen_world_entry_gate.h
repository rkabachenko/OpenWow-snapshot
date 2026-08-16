#pragma once

#include <cstdint>

namespace openwow::game {

enum class LoadingScreenTransportRenderAssetKind : std::uint8_t {
  kNone = 0,
  kM2 = 1,
  kAreaScene = 2,
};

struct LoadingScreenTransportRenderAssetState {
  LoadingScreenTransportRenderAssetKind kind =
      LoadingScreenTransportRenderAssetKind::kNone;
  bool is_ready = false;
};

[[nodiscard]] bool IsLoadingScreenTransportRenderAssetReady(
    const LoadingScreenTransportRenderAssetState& state);

struct LoadingScreenWorldEntryGateState {
  bool has_active_player = false;
  bool active_player_render_assets_ready = false;
  bool critical_visible_world_surface_ready = false;
  bool requires_transport_assets = false;
  bool has_transport_guid = false;
  bool has_transport_object = false;
  bool transport_assets_ready = false;
};

[[nodiscard]] bool ShouldKeepLoadingScreenVisibleForWorldEntry(
    const LoadingScreenWorldEntryGateState& state);

}
