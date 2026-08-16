#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace openwow::game {

struct TaxiSliceNode {
  std::uint32_t id = 0;
  std::uint32_t map_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  std::string name;
  std::array<std::uint32_t, 2> mount_creature_id{};
};

struct TaxiSliceRouteEntry {
  std::array<std::int32_t, 5> weighted_costs{};
  std::vector<std::uint32_t> path_nodes;
  std::uint32_t path_count = 999999;
  bool requires_multi_hop = false;
  float accumulated_path_distance = 0.0f;
  bool has_route = false;
};

struct TaxiSliceState {
  std::vector<TaxiSliceNode> nodes;
  std::vector<TaxiSliceRouteEntry> routes;
  std::vector<std::uint64_t> known_mask_words;
  std::unordered_set<std::uint32_t> known_node_ids;
  std::uint32_t current_node_id = 0;
  std::uint32_t current_map_id = 0;
  float taxi_min_x = 0.0f;
  float taxi_min_y = 0.0f;
  float taxi_max_x = 0.0f;
  float taxi_max_y = 0.0f;
  std::string texture_path;
  bool uses_runtime_mask = false;
};

}
