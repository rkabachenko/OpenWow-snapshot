#pragma once

#include "openwow/game/taxi_slice_state.h"
#include "openwow/game/taxi_system.h"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace openwow::data::dbc {
class DbcLoader;
struct TaxiPathEntry;
struct TaxiPathNodeEntry;
}

namespace openwow::game {

class WorldSession;

struct TaxiRuntimeRouteContext {
  const data::dbc::DbcLoader& dbc;
  std::unordered_map<std::uint64_t, const data::dbc::TaxiPathEntry*> paths_by_edge;

  std::unordered_map<std::uint32_t,
                     std::vector<const data::dbc::TaxiPathNodeEntry*>>
      nodes_by_path_id;

  [[nodiscard]] const data::dbc::TaxiPathEntry* LookupPath(
      std::uint32_t from, std::uint32_t to) const;
  [[nodiscard]] bool HasDirectPath(std::uint32_t from,
                                   std::uint32_t to) const;
};

[[nodiscard]] TaxiRuntimeRouteContext BuildTaxiRuntimeRouteContext(
    const data::dbc::DbcLoader& dbc);

[[nodiscard]] float ComputeTaxiPathDistance3d(
    const TaxiRuntimeRouteContext& ctx,
    std::uint32_t from_node_id,
    std::uint32_t to_node_id);

[[nodiscard]] TaxiSliceState BuildTaxiRuntimeSliceState(
    const data::dbc::DbcLoader& dbc, const WorldSession& session);

[[nodiscard]] bool IsKnownTaxiSliceNode(const TaxiSliceState& state,
                                        std::uint32_t node_id);

[[nodiscard]] std::optional<TaxiPreviewSegment> BuildTaxiRuntimePreviewSegment(
    const TaxiRuntimeRouteContext& ctx,
    const TaxiSliceState& state,
    std::uint32_t from_node_id,
    std::uint32_t to_node_id);

}
