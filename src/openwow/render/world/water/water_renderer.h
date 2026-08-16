#pragma once

#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/resources/textures/texture_lease.h"
#include "openwow/render/world/environment/spatial_point_light.h"
#include "openwow/render/world/environment/world_environment.h"
#include "openwow/render/world/water/liquid_shader_constants.h"
#include "openwow/world/liquid/water_heightfield.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <bgfx/bgfx.h>

namespace openwow::data::dbc {
struct LiquidMaterialEntry;
struct LiquidTypeEntry;
}
namespace openwow::world {
struct SpawnWaterRippleCommand;
}

namespace openwow::render {

class TextureManager;

class WaterRenderer final {
 public:
  using SurfaceOwnerId = std::uint64_t;
  using LiquidTypeLookupFn =
      std::function<const data::dbc::LiquidTypeEntry*(std::uint32_t)>;
  using LiquidMaterialLookupFn =
      std::function<const data::dbc::LiquidMaterialEntry*(std::uint32_t)>;

  explicit WaterRenderer(TextureManager& texture_manager);
  ~WaterRenderer();

  WaterRenderer(const WaterRenderer&) = delete;
  WaterRenderer& operator=(const WaterRenderer&) = delete;

  bool Initialize();

  void ReplaceOwnedWaterHeightfields(
      SurfaceOwnerId owner, std::vector<world::WaterHeightfield> heightfields);
  void RemoveOwnedWaterHeightfields(SurfaceOwnerId owner);
  void AddTransientWaterHeightfield(const world::WaterHeightfield& heightfield);

  void SetTransientWaterHeightfields(
      std::span<const std::shared_ptr<const world::WaterHeightfield>> sources);
  void ClearSurfaces();
  void ClearTransientSurfaces();
  void SpawnWaterRipple(const world::SpawnWaterRippleCommand& command);

  void Update(float delta_seconds);
  void SetLiquidStores(LiquidTypeLookupFn liquid_types,
                       LiquidMaterialLookupFn liquid_materials);
  void SetSpecularEnabled(bool enabled) noexcept {
    specular_enabled_ = enabled;
  }

  void Render(std::uint8_t view_id, const RenderMatrix4x4& view_mtx,
              const RenderMatrix4x4& proj_mtx,
              const RenderVec3& camera_position,
              const WorldEnvironmentSnapshot& environment,
              bgfx::Encoder* encoder = nullptr);

  [[nodiscard]] std::size_t surface_count() const {
    return persistent_surface_count_;
  }
  [[nodiscard]] std::size_t surface_owner_count() const {
    return surface_batches_.size();
  }
  [[nodiscard]] std::size_t transient_surface_count() const {
    return transient_surfaces_.size();
  }

  void Shutdown();
  void Reset();

  static constexpr std::size_t kMaxLiquidDrawPointLights = 3u;

  static constexpr std::size_t kLiquidVsParamCount = 16u;
  static constexpr std::size_t kLiquidFsParamCount = 2u;
  static constexpr std::size_t kLiquidProcVsParamCount = 13u;
  static constexpr std::size_t kLiquidProcFsParamCount = 7u;

 private:
  enum class MaterialFamily : std::uint8_t {
    kWater = 1u,
    kMagma = 2u,
    kProceduralWater = 3u,
  };

  struct LiquidPassParams {
    std::array<RenderVec4, kLiquidVsParamCount> vs{};
    std::array<RenderVec4, kLiquidFsParamCount> fs{};
    std::array<RenderVec4, kLiquidProcVsParamCount> proc_vs{};
    std::array<RenderVec4, kLiquidProcFsParamCount> proc_fs{};
  };

  struct WaterVertex {
    RenderVec3 position{};
    RenderVec3 normal{0.0f, 0.0f, 1.0f};
    std::array<std::uint8_t, 4> color{255u, 255u, 255u, 255u};
    std::array<float, 2> texcoord0{};
    std::array<float, 2> texcoord1{};
  };

  struct DrawRange {
    std::uint32_t liquid_type_id{0u};
    std::uint32_t first_index{0u};
    std::uint32_t index_count{0u};
    MaterialFamily family{MaterialFamily::kWater};
    bool second_queue{false};

    RenderVec3 lighting_position{};
    float lighting_radius{0.0f};
  };

  struct SurfaceBatch {
    std::vector<world::WaterHeightfield> surfaces;
    bgfx::DynamicVertexBufferHandle vb = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
    std::vector<DrawRange> draw_ranges;
    std::uint32_t vertex_count{0u};
    std::uint32_t index_count{0u};
    bool mesh_dirty{true};
  };

  struct ResolvedMaterial {
    const data::dbc::LiquidTypeEntry* type{nullptr};
    const data::dbc::LiquidMaterialEntry* material{nullptr};
    MaterialFamily family{MaterialFamily::kWater};
    bool second_queue{false};
  };

  struct TextureSequence {
    std::vector<std::string> paths;
    std::vector<TextureLease> resident;
    bool queued{false};
  };

  struct MaterialTextureBinding {
    std::size_t texture_count{0u};
    std::array<bgfx::TextureHandle, 6> textures{{
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    RenderVec4 uv0{};
    RenderVec4 uv1{};
    std::array<RenderVec4, 2> material_parameters{};
    bool procedural{false};
    ProceduralLiquidTextureConstants texture_constants{};
  };

  struct ProceduralLookupState {
    std::array<std::uint32_t, 4> colors{};
    std::array<float, 4> alpha{};

    bool operator==(const ProceduralLookupState&) const = default;
  };

  struct RippleVertex {
    RenderVec3 position{};
    std::array<std::uint8_t, 4> color{255u, 255u, 255u, 0u};
    std::array<float, 2> texcoord{};
  };

  struct WaterRipple {
    std::array<float, 3> position{};
    std::vector<std::array<float, 3>> control_points;
    float rotation_radians{};
    float extent{};
    float extent_rate{};
    float opacity{};
    float max_opacity{};
    float opacity_ramp_up{};
    float opacity_ramp_down{};
    float remaining_lifetime{};
    std::uint8_t texture_bucket{};
    bool active{};
  };

  [[nodiscard]] std::optional<ResolvedMaterial> ResolveMaterial(
      std::uint32_t liquid_type_id) const;
  [[nodiscard]] bgfx::ProgramHandle ProgramFor(
      MaterialFamily family, std::size_t point_light_count) const;
  [[nodiscard]] bgfx::TextureHandle ResolveTexture(
      std::string_view texture_name, std::uint32_t period_ms,
      std::uint8_t sampler_index);
  [[nodiscard]] bgfx::TextureHandle ResolveProceduralTexture(
      std::string_view texture_name) const;

  [[nodiscard]] std::optional<MaterialTextureBinding> ResolveMaterialBinding(
      const ResolvedMaterial& material);

  void ApplyMaterialTextures(const MaterialTextureBinding& binding,
                             bgfx::Encoder* encoder) const;

  static void WriteMaterialParams(const MaterialTextureBinding& binding,
                                  MaterialFamily family,
                                  LiquidPassParams& params);
  void UpdateProceduralTextures(const WorldEnvironmentSnapshot& environment);

  void EnsureBatchGpuResources(SurfaceBatch& batch);
  void DestroyBatchGpuResources(SurfaceBatch& batch);
  void RebuildMesh(const std::vector<world::WaterHeightfield>& surfaces,
                   bgfx::DynamicVertexBufferHandle& vb,
                   bgfx::IndexBufferHandle& ib,
                   std::vector<DrawRange>& draw_ranges,
                   std::uint32_t& vertex_count,
                   std::uint32_t& index_count, bool& mesh_dirty);

  [[nodiscard]] LiquidPassParams BuildPassFrameConstants(
      const RenderVec3& camera_position,
      const WorldEnvironmentSnapshot& environment) const;
  void RenderDrawRanges(std::uint8_t view_id,
                        bgfx::DynamicVertexBufferHandle vb,
                        bgfx::IndexBufferHandle ib,
                        std::uint32_t vertex_count,
                        const std::vector<DrawRange>& draw_ranges,
                        const WorldEnvironmentSnapshot& environment,
                        LiquidPassParams& params,
                        bool second_queue,
                        bgfx::Encoder* encoder);

  void RenderWaterRipples(std::uint8_t view_id,
                          const WorldEnvironmentSnapshot& environment,
                          bgfx::Encoder* encoder);

  TextureManager& texture_manager_;
  std::array<bgfx::ProgramHandle, 4> water_programs_{{
      BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
      BGFX_INVALID_HANDLE}};
  std::array<bgfx::ProgramHandle, 4> water_no_spec_programs_{{
      BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
      BGFX_INVALID_HANDLE}};
  bgfx::ProgramHandle magma_program_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle procedural_water_program_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle water_ripple_program_ = BGFX_INVALID_HANDLE;
  std::array<bgfx::UniformHandle, 6> samplers_{{BGFX_INVALID_HANDLE,
                                               BGFX_INVALID_HANDLE,
                                               BGFX_INVALID_HANDLE,
                                               BGFX_INVALID_HANDLE,
                                               BGFX_INVALID_HANDLE,
                                               BGFX_INVALID_HANDLE}};

  bgfx::UniformHandle u_vs_params_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fs_params_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_proc_vs_params_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_proc_fs_params_ = BGFX_INVALID_HANDLE;

  bgfx::UniformHandle u_fog_params_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fog_color_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_water_ripple_flags_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_water_ripple_texture_ = BGFX_INVALID_HANDLE;

  bgfx::TextureHandle procedural_river_depth_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle procedural_ocean_depth_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle procedural_wmo_water_ = BGFX_INVALID_HANDLE;
  std::optional<ProceduralLookupState> procedural_lookup_state_;

  bgfx::VertexLayout layout_{};
  bgfx::VertexLayout water_ripple_layout_{};
  bgfx::DynamicVertexBufferHandle transient_vb_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle transient_ib_ = BGFX_INVALID_HANDLE;
  std::map<SurfaceOwnerId, SurfaceBatch> surface_batches_;
  std::size_t persistent_surface_count_{0u};
  std::vector<world::WaterHeightfield> transient_surfaces_;

  std::vector<std::shared_ptr<const world::WaterHeightfield>>
      transient_surface_sources_;
  std::vector<DrawRange> transient_draw_ranges_;
  std::unordered_map<std::string, TextureSequence> texture_sequences_;
  std::array<TextureLease, 2> water_ripple_textures_{};
  std::array<WaterRipple, 128> water_ripples_{};
  std::uint32_t next_ordinary_ripple_slot_{32u};
  std::uint32_t next_local_player_ripple_slot_{0u};
  ProceduralLiquidWaveState procedural_wave_state_{};
  double animation_milliseconds_{0.0};
  std::uint32_t transient_vertex_count_{0u};
  std::uint32_t transient_index_count_{0u};
  bool transient_mesh_dirty_{true};
  bool initialized_{false};
  bool specular_enabled_{true};
  LiquidTypeLookupFn liquid_type_lookup_;
  LiquidMaterialLookupFn liquid_material_lookup_;
};

}
