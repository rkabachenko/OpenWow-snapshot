#include "openwow/game/taxi_runtime_slice.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/taxi_handler.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_map_transform.h"
#include "openwow/game/world_session.h"
#include "openwow/net/client_services.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace openwow::game {

namespace {

using openwow::data::dbc::DbcLoader;
using openwow::data::dbc::TaxiNodesEntry;
using openwow::data::dbc::TaxiPathEntry;
using openwow::data::dbc::TaxiPathNodeEntry;
constexpr float kMissingTaxiPathDistance = 9.9999998e10f;
constexpr std::array<std::uint32_t, 4> kTaxiMountSetA{
    2224u, 3574u, 19917u, 34709u};
constexpr std::array<std::uint32_t, 5> kTaxiMountSetB{
    3837u, 541u, 28360u, 28361u, 31315u};

[[nodiscard]] bool ContainsMountId(std::span<const std::uint32_t> set,
                                   std::uint32_t value) {
  return std::find(set.begin(), set.end(), value) != set.end();
}

[[nodiscard]] bool PassesTaxiMountFilter(
    const std::array<std::uint32_t, 2>& mount_creature_id,
    std::uint32_t faction_group) {
  const bool has_set_a = ContainsMountId(kTaxiMountSetA, mount_creature_id[0]) ||
                         ContainsMountId(kTaxiMountSetA, mount_creature_id[1]);
  const bool has_set_b = ContainsMountId(kTaxiMountSetB, mount_creature_id[0]) ||
                         ContainsMountId(kTaxiMountSetB, mount_creature_id[1]);

  if (faction_group == 3 && has_set_a && !has_set_b) {
    return false;
  }
  if (faction_group == 5 && has_set_b && !has_set_a) {
    return false;
  }
  return true;
}

[[nodiscard]] std::uint32_t GetPlayerTaxiFactionGroup(
    const WorldSession& session, const DbcLoader& dbc) {
  const auto* player = session.objects().GetLocalPlayer();
  if (!player) {
    return 0;
  }

  const auto faction_template_id = player->GetUInt32(UNIT_FIELD_FACTIONTEMPLATE);
  if (const auto* entry =
          dbc.faction_template().LookupEntry(faction_template_id)) {
    return entry->faction_group;
  }
  return 0;
}

}

float ComputeTaxiPathDistance3d(const TaxiRuntimeRouteContext& ctx,
                                std::uint32_t from_node_id,
                                std::uint32_t to_node_id) {
  const TaxiPathEntry* path = ctx.LookupPath(from_node_id, to_node_id);
  if (!path) {
    return kMissingTaxiPathDistance;
  }

  const auto nodes_it = ctx.nodes_by_path_id.find(path->id);
  if (nodes_it == ctx.nodes_by_path_id.end()) {
    return kMissingTaxiPathDistance;
  }

  const auto& nodes = nodes_it->second;
  float total = 0.0f;
  for (std::size_t i = 1; i < nodes.size(); ++i) {
    const TaxiPathNodeEntry& prev = *nodes[i - 1];
    const TaxiPathNodeEntry& cur = *nodes[i];
    const auto prev_xy =
        ApplyWorldMapTransform(ctx.dbc.world_map_transforms(),
                               prev.map_id,
                               prev.x,
                               prev.y);
    const auto cur_xy =
        ApplyWorldMapTransform(ctx.dbc.world_map_transforms(),
                               cur.map_id,
                               cur.x,
                               cur.y);
    const float dx = cur_xy.x - prev_xy.x;
    const float dy = cur_xy.y - prev_xy.y;
    const float dz = cur.z - prev.z;
    total += std::sqrt(dx * dx + dy * dy + dz * dz);
  }
  return total;
}

namespace {

void BuildRuntimeTaxiPathCost(const TaxiRuntimeRouteContext& ctx,
                              TaxiSliceState& state,
                              std::size_t target_slot,
                              std::size_t source_slot,
                              std::uint32_t direct_cost,
                              std::uint32_t faction_group) {
  if (target_slot >= state.nodes.size() || source_slot >= state.nodes.size()) {
    return;
  }

  const TaxiSliceNode& target_node = state.nodes[target_slot];
  if (!PassesTaxiMountFilter(target_node.mount_creature_id, faction_group)) {
    return;
  }

  const TaxiSliceRouteEntry& source_route = state.routes[source_slot];
  const float new_distance =
      source_route.accumulated_path_distance +
      ComputeTaxiPathDistance3d(ctx, state.nodes[source_slot].id, target_node.id);

  TaxiSliceRouteEntry& target_route = state.routes[target_slot];
  if (target_route.has_route &&
      !(new_distance < target_route.accumulated_path_distance)) {
    return;
  }

  target_route.path_nodes = source_route.path_nodes;
  target_route.path_nodes.push_back(target_node.id);
  target_route.path_count =
      static_cast<std::uint32_t>(target_route.path_nodes.size());
  target_route.has_route = true;
  target_route.accumulated_path_distance = new_distance;

  float factor = 1.0f;
  for (std::size_t i = 0; i < target_route.weighted_costs.size(); ++i) {
    target_route.weighted_costs[i] =
        source_route.weighted_costs[i] +
        static_cast<std::int32_t>(
            std::lround(factor * static_cast<float>(direct_cost)));
    factor -= 0.05f;
  }

  for (std::size_t next_slot = 0; next_slot < state.nodes.size(); ++next_slot) {
    if (const TaxiPathEntry* next =
            ctx.LookupPath(target_node.id, state.nodes[next_slot].id)) {
      BuildRuntimeTaxiPathCost(ctx,
                               state,
                               next_slot,
                               target_slot,
                               next->cost,
                               faction_group);
    }
  }
}

}

const TaxiPathEntry* TaxiRuntimeRouteContext::LookupPath(std::uint32_t from,
                                                         std::uint32_t to) const {
  const auto key =
      (static_cast<std::uint64_t>(from) << 32) | static_cast<std::uint64_t>(to);
  const auto it = paths_by_edge.find(key);
  return (it != paths_by_edge.end()) ? it->second : nullptr;
}

bool TaxiRuntimeRouteContext::HasDirectPath(std::uint32_t from,
                                            std::uint32_t to) const {
  return LookupPath(from, to) != nullptr;
}

TaxiRuntimeRouteContext BuildTaxiRuntimeRouteContext(const DbcLoader& dbc) {
  TaxiRuntimeRouteContext ctx{.dbc = dbc, .paths_by_edge = {}, .nodes_by_path_id = {}};
  for (const TaxiPathEntry& path : dbc.taxi_path()) {
    const auto key = (static_cast<std::uint64_t>(path.from_node_id) << 32) |
                     static_cast<std::uint64_t>(path.to_node_id);

    ctx.paths_by_edge.emplace(key, &path);
  }
  for (const TaxiPathNodeEntry& node : dbc.taxi_path_node()) {
    ctx.nodes_by_path_id[node.path_id].push_back(&node);
  }
  for (auto& [path_id, nodes] : ctx.nodes_by_path_id) {
    std::sort(nodes.begin(), nodes.end(),
              [](const TaxiPathNodeEntry* lhs, const TaxiPathNodeEntry* rhs) {
                return lhs->node_index < rhs->node_index;
              });
  }
  return ctx;
}

TaxiSliceState BuildTaxiRuntimeSliceState(const DbcLoader& dbc,
                                          const WorldSession& session) {
  TaxiSliceState state;
  if (!session.taxi().IsTaxiMapOpen()) {
    return state;
  }

  state.current_node_id = session.taxi().GetCurrentNode();
  const TaxiNodesEntry* current_node =
      dbc.taxi_nodes().LookupEntry(state.current_node_id);
  if (!current_node) {
    return state;
  }

  const auto transformed_current =
      ApplyWorldMapTransform(dbc.world_map_transforms(),
                             current_node->map_id,
                             current_node->x,
                             current_node->y);
  state.current_map_id = transformed_current.map_id;

  bool found_bounds = false;
  for (const auto& continent : dbc.world_map_continent()) {
    if (continent.map_id != state.current_map_id) {
      continue;
    }
    state.taxi_min_x = continent.taxi_min_x;
    state.taxi_min_y = continent.taxi_min_y;
    state.taxi_max_x = continent.taxi_max_x;
    state.taxi_max_y = continent.taxi_max_y;
    found_bounds = state.taxi_max_x != state.taxi_min_x &&
                   state.taxi_max_y != state.taxi_min_y;
    break;
  }
  if (!found_bounds) {
    return state;
  }

  const auto account_expansion_level = static_cast<std::uint32_t>(
      openwow::net::ClientServices::Instance().GetExpansionLevel());
  const auto& mask = session.taxi().last_display().mask;
  state.known_mask_words.reserve((mask.size() + 1) / 2);
  for (std::size_t i = 0; i < mask.size(); i += 2) {
    const std::uint64_t lo = mask[i];
    const std::uint64_t hi = (i + 1 < mask.size())
                                 ? (static_cast<std::uint64_t>(mask[i + 1]) << 32)
                                 : 0;
    state.known_mask_words.push_back(lo | hi);
  }

  for (std::uint32_t node_id = 1; node_id < kMaxTaxiNodes; ++node_id) {
    const std::uint32_t bit_index = node_id - 1;
    const std::size_t word_index = bit_index / 32;
    if (word_index >= mask.size() ||
        (mask[word_index] & (1u << (bit_index % 32))) == 0) {
      continue;
    }

    const TaxiNodesEntry* entry = dbc.taxi_nodes().LookupEntry(node_id);
    if (!entry) {
      continue;
    }

    const auto transformed =
        ApplyWorldMapTransform(dbc.world_map_transforms(),
                               entry->map_id,
                               entry->x,
                               entry->y);
    if (transformed.map_id != state.current_map_id) {
      continue;
    }

    if (const auto* map_entry = dbc.map().LookupEntry(entry->map_id)) {
      if (map_entry->expansion_id > account_expansion_level) {
        continue;
      }
    }

    TaxiSliceNode node;
    node.id = entry->id;
    node.map_id = entry->map_id;
    node.x = (state.taxi_max_y - transformed.y) /
             (state.taxi_max_x - state.taxi_min_x);
    node.y = (transformed.x - state.taxi_min_x) /
             (state.taxi_max_y - state.taxi_min_y);
    node.z = entry->z;
    node.name = std::string(entry->name);
    node.mount_creature_id = entry->mount_creature_id;
    state.nodes.push_back(std::move(node));
  }

  std::sort(state.nodes.begin(), state.nodes.end(),
            [](const TaxiSliceNode& lhs, const TaxiSliceNode& rhs) {
              return lhs.id < rhs.id;
            });

  state.routes.resize(state.nodes.size());
  state.uses_runtime_mask = true;

  if (state.nodes.empty()) {
    return state;
  }

  state.texture_path = "Interface\\TaxiFrame\\TAXIMAP" +
                       std::to_string(state.current_map_id) + ".blp";

  const auto ctx = BuildTaxiRuntimeRouteContext(dbc);

  std::size_t current_slot = 0;
  for (std::size_t i = 0; i < state.nodes.size(); ++i) {
    state.routes[i].requires_multi_hop =
        !ctx.HasDirectPath(state.current_node_id, state.nodes[i].id);
    if (state.nodes[i].id == state.current_node_id) {
      current_slot = i;
    }
  }

  state.routes[current_slot].path_nodes.push_back(state.current_node_id);
  state.routes[current_slot].path_count = 1;
  state.routes[current_slot].accumulated_path_distance = 0.0f;
  state.routes[current_slot].has_route = true;

  const std::uint32_t faction_group = GetPlayerTaxiFactionGroup(session, dbc);
  for (std::size_t i = 0; i < state.nodes.size(); ++i) {
    if (const TaxiPathEntry* path =
            ctx.LookupPath(state.current_node_id, state.nodes[i].id)) {
      BuildRuntimeTaxiPathCost(
          ctx, state, i, current_slot, path->cost, faction_group);
    }
  }

  return state;
}

bool IsKnownTaxiSliceNode(const TaxiSliceState& state, std::uint32_t node_id) {
  if (node_id == 0) {
    return false;
  }
  if (state.uses_runtime_mask) {
    const std::uint32_t bit_index = node_id - 1;
    const std::size_t word_index = bit_index >> 6;
    if (word_index >= state.known_mask_words.size()) {
      return false;
    }
    return (state.known_mask_words[word_index] &
            (std::uint64_t{1} << (bit_index & 63))) != 0;
  }
  return state.known_node_ids.contains(node_id);
}

std::optional<TaxiPreviewSegment> BuildTaxiRuntimePreviewSegment(
    const TaxiRuntimeRouteContext& ctx,
    const TaxiSliceState& state,
    std::uint32_t from_node_id,
    std::uint32_t to_node_id) {
  const TaxiPathEntry* path = ctx.LookupPath(from_node_id, to_node_id);
  if (!path) {
    return std::nullopt;
  }

  const auto nodes_it = ctx.nodes_by_path_id.find(path->id);
  if (nodes_it == ctx.nodes_by_path_id.end() || nodes_it->second.size() < 2) {
    return std::nullopt;
  }

  const TaxiPathNodeEntry& src = *nodes_it->second.front();
  const TaxiPathNodeEntry& dst = *nodes_it->second.back();
  const auto src_xy =
      ApplyWorldMapTransform(ctx.dbc.world_map_transforms(), src.map_id, src.x, src.y);
  const auto dst_xy =
      ApplyWorldMapTransform(ctx.dbc.world_map_transforms(), dst.map_id, dst.x, dst.y);

  TaxiPreviewSegment segment{};
  segment.src_x =
      (state.taxi_max_y - src_xy.y) / (state.taxi_max_y - state.taxi_min_y);
  segment.src_y =
      (src_xy.x - state.taxi_min_x) / (state.taxi_max_x - state.taxi_min_x);
  segment.dst_x =
      (state.taxi_max_y - dst_xy.y) / (state.taxi_max_y - state.taxi_min_y);
  segment.dst_y =
      (dst_xy.x - state.taxi_min_x) / (state.taxi_max_x - state.taxi_min_x);
  return segment;
}

}
