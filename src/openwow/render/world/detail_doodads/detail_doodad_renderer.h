#pragma once

#include "openwow/render/api/math/render_math_types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bgfx {
struct Encoder;
}

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::data::terrain {
struct AdtFile;
}

namespace openwow::world {
struct Frustum;
}

namespace openwow::render {

class TextureManager;
struct WorldM2SceneState;

class DetailDoodadRenderer {
 public:
  using LoadFileCallback =
      std::function<std::vector<std::uint8_t>(const std::string&)>;

  explicit DetailDoodadRenderer(TextureManager& texture_manager);
  ~DetailDoodadRenderer();

  DetailDoodadRenderer(const DetailDoodadRenderer&) = delete;
  DetailDoodadRenderer& operator=(const DetailDoodadRenderer&) = delete;

  bool Initialize();
  void Shutdown();

  void SetFileLoader(LoadFileCallback loader);
  void BindDbc(const data::dbc::DbcLoader* dbc);

  void LoadFromAdt(std::shared_ptr<const data::terrain::AdtFile> adt,
                   std::int32_t tile_x, std::int32_t tile_y);
  void UnloadTile(std::int32_t tile_x, std::int32_t tile_y);
  void Clear();

  void Update(const RenderVec3& camera_position);
  void Render(std::uint8_t view_id, const float* view_mtx,
              const float* projection_mtx, const world::Frustum* frustum,
              const RenderVec3& camera_position,
              const WorldM2SceneState& scene_state,
              bgfx::Encoder* encoder = nullptr,
              bool shadow_caster_pass = false);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
