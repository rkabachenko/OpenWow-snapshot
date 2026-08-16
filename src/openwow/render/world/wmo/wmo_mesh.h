#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "openwow/data/wmo/wmo_file.h"
#include "openwow/world/presentation/world_presentation_commands.h"

namespace openwow::render {

enum class WmoDebugGeometryMode : std::uint8_t {
  Collision,

  OutdoorSurface,

  Primary,

  TwoPass,

};

enum class WmoFaceFilter : std::uint8_t {
  Renderable,
};

struct WmoVertex {
  float position[3];
  float normal[3];
  uint32_t color;
  float texcoord[2];
};
static_assert(sizeof(WmoVertex) == 0x24);

struct WmoCompositeVertex {
  float position[3];
  float normal[3];
  uint32_t color;
  uint32_t color2;
  float texcoord[2];
  float texcoord2[2];
};
static_assert(sizeof(WmoCompositeVertex) == 0x30);

struct WmoBatchMesh {
  enum class Region : std::uint8_t { Transition, Interior, Exterior };
  uint32_t material_index{0};
  uint32_t start_index{0};

  uint32_t index_count{0};

  Region region{Region::Exterior};
};

struct WmoGroupMesh {
  std::vector<WmoVertex> vertices;
  std::vector<WmoCompositeVertex> composite_vertices;
  std::vector<uint16_t> indices;
  std::vector<uint16_t> collision_indices;
  std::vector<uint16_t> outdoor_surface_indices;
  std::vector<uint16_t> primary_debug_indices;
  std::vector<uint16_t> renderable_indices;

  std::vector<WmoBatchMesh> batches;
  float bounds_min[3];
  float bounds_max[3];
  uint32_t flags;

  [[nodiscard]] bool uses_composite_vertices() const noexcept {
    return !composite_vertices.empty();
  }
};

bool IsRenderableFace(uint8_t mopy_flags);

world::WmoGroupPublicationStatus ClassifyWmoGroupPublication(
    const data::wmo::WmoGroup& group);

world::WmoGroupPublicationStatus ClassifyWmoGroupPublication(
    const data::wmo::WmoGroup& group, std::size_t material_count);

WmoGroupMesh GenerateWmoGroupMesh(const data::wmo::WmoGroup &group, uint32_t mohd_flags = 0);

WmoGroupMesh GenerateWmoGroupMesh(const data::wmo::WmoRoot& root,
                                  const data::wmo::WmoGroup& group);

}
