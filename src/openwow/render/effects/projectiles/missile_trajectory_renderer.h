#pragma once

#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/m2/m2_transparent_draw_order.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {
struct MissileArcRenderSnapshot;
struct SpellVisualChainRenderRequest;
}

namespace openwow::render {

class TextureManager;

class MissileTrajectoryRenderer final {
 public:
  explicit MissileTrajectoryRenderer(TextureManager& textures);
  ~MissileTrajectoryRenderer();

  MissileTrajectoryRenderer(const MissileTrajectoryRenderer&) = delete;
  MissileTrajectoryRenderer& operator=(const MissileTrajectoryRenderer&) = delete;

  bool Initialize();
  void Shutdown();

  void Render(std::uint8_t view_id, const float* view_mtx,
              const float* proj_mtx, const RenderFogState& fog,
              const game::MissileArcRenderSnapshot& snapshot,
              m2::M2TransparentDrawOrder& draw_order);
  std::uint32_t CreateSpellChain(
      const game::SpellVisualChainRenderRequest& request);
  bool UpdateSpellChain(std::uint32_t handle, const float* source,
                        const float* target, bool visible);
  void DestroySpellChain(std::uint32_t handle);

 private:
  struct GpuVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint32_t color_abgr = 0;
  };

  void SubmitRibbon(std::uint8_t view_id,
                    const game::MissileArcRenderSnapshot& snapshot,
                    const RenderFogState& fog,
                    m2::M2TransparentDrawOrder& draw_order);
  void SubmitEndpointProjection(
      std::uint8_t view_id,
      const game::MissileArcRenderSnapshot& snapshot,
      const RenderFogState& fog,
      m2::M2TransparentDrawOrder& draw_order);
  void SubmitSpellChains(std::uint8_t view_id, const float* view_mtx,
                         const RenderFogState& fog,
                         m2::M2TransparentDrawOrder& draw_order);
  void SetDrawUniforms(const RenderFogState& fog) const;

  struct SpellChainBeam {
    std::uint32_t chain_effect_id{0};
    std::string texture_path;
    std::array<float, 3> source{};
    std::array<float, 3> target{};
    float average_segment_length{1.0f};
    float width{0.1f};
    float noise_scale{0.0f};
    float texture_coordinate_scale{1.0f};
    float wave_height{0.0f};
    float wave_frequency{0.0f};
    float arc_height{0.0f};
    float texture_length{1.0f};
    float wave_phase{0.0f};
    std::uint32_t color_argb{0xFFFFFFFFu};
    std::uint8_t blend_mode{0};
    bool visible{false};
  };

  TextureManager& textures_;
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle texture_sampler_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle ribbon_color_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle fog_color_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle fog_params_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout_{};
  std::unordered_map<std::uint32_t, SpellChainBeam> spell_chains_;
  std::uint32_t next_spell_chain_handle_{1u};
  bool initialized_ = false;
};

}
