#include "openwow/game/loading_screen_world_entry_gate.h"

namespace openwow::game {

bool IsLoadingScreenTransportRenderAssetReady(
    const LoadingScreenTransportRenderAssetState& state) {
  switch (state.kind) {
    case LoadingScreenTransportRenderAssetKind::kAreaScene:
      return state.is_ready;
    case LoadingScreenTransportRenderAssetKind::kM2:
      return state.is_ready;
    case LoadingScreenTransportRenderAssetKind::kNone:
    default:
      return false;
  }
}

bool ShouldKeepLoadingScreenVisibleForWorldEntry(
    const LoadingScreenWorldEntryGateState& state) {
  if (!state.has_active_player) {
    return true;
  }

  if (!state.active_player_render_assets_ready) {
    return true;
  }

  if (!state.critical_visible_world_surface_ready) {
    return true;
  }

  if (!state.requires_transport_assets) {
    return false;
  }

  if (!state.has_transport_guid) {
    return false;
  }

  if (!state.has_transport_object) {
    return false;
  }

  return !state.transport_assets_ready;
}

}
