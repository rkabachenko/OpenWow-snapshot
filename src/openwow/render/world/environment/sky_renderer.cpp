#include "openwow/render/world/environment/sky_renderer.h"

#include "openwow/world/environment/day_night.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/world/environment/sky_settings.h"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace openwow::render {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinutesPerDay = 1440.0f;
constexpr float kMillisecondsPerDay = 86400000.0f;
constexpr std::uint32_t kRetailSkyboxTimeOfDaySyncFlag = 0x1u;
constexpr float kRetailSkyDomeScale = 6.6666665f;
constexpr float kRetailWrappedPhaseScale = 0.15915494f;
constexpr float kRetailSkyDomeStep = 1.0f / 24.0f;
constexpr float kRetailSkyDomePhaseOffset = 0.25f;
constexpr float kRetailSkyDomeAxisThreshold = 0.0001f;
constexpr float kRetailSkyTintInnerBlend = 0.69999999f;
constexpr std::size_t kRetailSkyDomeLongitudeSegmentCount = 24u;
constexpr std::size_t kRetailSkyDomeAzimuthTintRingCount = 4u;
constexpr std::size_t kRetailSkyDomeVertexCount = 122u;
constexpr std::array<std::uint16_t, 7> kRetailSkyDomeRingOffsets = {
    0u, 1u, 25u, 49u, 73u, 97u, 121u};

struct WrappedFloatCurvePoint {
  float sample = 0.0f;
  float value = 0.0f;
};

constexpr WrappedFloatCurvePoint kRetailSkyHighlightCurve[] = {
    {0.125f, 0.0f},
    {0.27083334f, 1.0f},
    {0.29166669f, 0.0f},
    {0.85416663f, 0.0f},
    {0.89583331f, 1.0f},
    {0.99930555f, 0.0f},
};

constexpr WrappedFloatCurvePoint kRetailSkyBandBlendCurve[] = {
    {0.125f, 1.0f},
    {0.375f, 0.0f},
    {0.5f, -0.5f},
    {0.625f, -0.69999999f},
    {0.75f, -0.5f},
    {0.875f, 0.0f},
};

void UnpackArgbToRgba(uint32_t argb, uint8_t out[4]) {
  out[0] = static_cast<uint8_t>((argb >> 16) & 0xFF);
  out[1] = static_cast<uint8_t>((argb >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((argb >> 0) & 0xFF);
  out[3] = static_cast<uint8_t>((argb >> 24) & 0xFF);
}

float WrapUnitPhase(float phase) {
  while (phase < 0.0f) {
    phase += 1.0f;
  }
  while (phase > 1.0f) {
    phase -= 1.0f;
  }
  return phase;
}

float EvaluateWrappedFloatCurve(const WrappedFloatCurvePoint* points,
                                const std::size_t point_count, float sample) {
  if (point_count == 0u) {
    return 0.0f;
  }

  sample = WrapUnitPhase(sample);
  if (sample < points[0].sample) {
    sample += 1.0f;
  }

  WrappedFloatCurvePoint lower = points[point_count - 1u];
  WrappedFloatCurvePoint upper = points[0];
  upper.sample += 1.0f;

  for (std::size_t index = 0; index + 1u < point_count; ++index) {
    if (sample < points[index + 1u].sample) {
      lower = points[index];
      upper = points[index + 1u];
      break;
    }
  }

  const float span = upper.sample - lower.sample;
  if (std::fabs(span) < 0.001f) {
    return lower.value;
  }

  const float factor = (sample - lower.sample) / span;
  return lower.value + factor * (upper.value - lower.value);
}

std::uint8_t FloorLerpChannel(const std::uint8_t from, const std::uint8_t to,
                              const float factor) {
  const float blended = static_cast<float>(from) +
                        factor * static_cast<float>(static_cast<int>(to) - static_cast<int>(from));
  return static_cast<std::uint8_t>(std::floor(blended));
}

std::uint32_t BlendOpaqueArgbRgb(const std::uint32_t from_argb, const std::uint32_t to_argb,
                                 const float factor) {
  return 0xFF000000u |
         (static_cast<std::uint32_t>(
              FloorLerpChannel(static_cast<std::uint8_t>((from_argb >> 16) & 0xFFu),
                               static_cast<std::uint8_t>((to_argb >> 16) & 0xFFu), factor))
          << 16) |
         (static_cast<std::uint32_t>(
              FloorLerpChannel(static_cast<std::uint8_t>((from_argb >> 8) & 0xFFu),
                               static_cast<std::uint8_t>((to_argb >> 8) & 0xFFu), factor))
          << 8) |
         static_cast<std::uint32_t>(
             FloorLerpChannel(static_cast<std::uint8_t>(from_argb & 0xFFu),
                              static_cast<std::uint8_t>(to_argb & 0xFFu), factor));
}

std::uint32_t BlendPackedArgbRgb(const std::uint32_t destination_argb,
                                 const std::uint8_t blend_factor,
                                 const std::uint32_t source_argb) {
  if (blend_factor == 0u) {
    return destination_argb;
  }

  const std::uint32_t destination_alpha = destination_argb & 0xFF000000u;
  const std::uint32_t source_rgb = source_argb & 0x00FFFFFFu;
  if (blend_factor == 0xFFu) {
    return destination_alpha | source_rgb;
  }

  const auto blend_channel = [&](const int shift) -> std::uint32_t {
    const auto destination =
        static_cast<std::uint8_t>((destination_argb >> shift) & 0xFFu);
    const auto source = static_cast<std::uint8_t>((source_argb >> shift) & 0xFFu);
    const auto delta =
        static_cast<std::int32_t>(source) - static_cast<std::int32_t>(destination);
    const auto blended = static_cast<std::uint8_t>(
        destination + ((blend_factor * delta) >> 8));
    return static_cast<std::uint32_t>(blended) << shift;
  };

  return destination_alpha | blend_channel(16) | blend_channel(8) | blend_channel(0);
}

std::uint8_t RoundBlendByte(const float factor) {
  return static_cast<std::uint8_t>(
      static_cast<std::int32_t>(std::nearbyint(factor * 255.0f)));
}

float ComputeRetailSkyDomePhase(const std::array<float, 3>& camera_forward) {
  const float x = camera_forward[0];
  const float y = camera_forward[1];
  const float z = camera_forward[2];
  float phase = 0.0f;
  if (x * x + y * y <= kRetailSkyDomeAxisThreshold) {
    phase = std::atan2(z, x);
  } else {
    phase = std::atan2(y, x);
  }
  if (phase < 0.0f) {
    phase += 2.0f * kPi;
  }
  return phase;
}

std::vector<std::array<float, 3>> BuildRetailDomePositions() {

  openwow::game::DayNight_BuildSkyDome(kRetailSkyDomeScale);
  const auto& retail_mesh = openwow::game::DayNight_GetSkyDomeMesh();
  std::vector<std::array<float, 3>> positions;
  positions.reserve(retail_mesh.positions.size());
  for (const auto& position : retail_mesh.positions) {
    positions.push_back({position.x, position.y, position.z});
  }
  return positions;
}

std::vector<std::uint16_t> BuildRetailDomeIndices() {
  std::vector<std::uint16_t> indices;
  indices.reserve(6u * kRetailSkyDomeLongitudeSegmentCount * 6u);

  const auto ring_vertex = [&](const int ring, const int segment) -> std::uint16_t {
    if (ring == 0) {
      return kRetailSkyDomeRingOffsets[0];
    }
    if (ring == 6) {
      return kRetailSkyDomeRingOffsets[6];
    }
    return static_cast<std::uint16_t>(
        kRetailSkyDomeRingOffsets[ring] +
        (segment % static_cast<int>(kRetailSkyDomeLongitudeSegmentCount)));
  };

  for (int ring = 0; ring < 6; ++ring) {
    for (int segment = 0;
         segment < static_cast<int>(kRetailSkyDomeLongitudeSegmentCount); ++segment) {
      const std::uint16_t prev0 = ring_vertex(ring, segment);
      const std::uint16_t prev1 = ring_vertex(ring, segment + 1);
      const std::uint16_t curr0 = ring_vertex(ring + 1, segment);
      const std::uint16_t curr1 = ring_vertex(ring + 1, segment + 1);

      indices.push_back(prev0);
      indices.push_back(curr0);
      indices.push_back(prev1);

      indices.push_back(prev1);
      indices.push_back(curr0);
      indices.push_back(curr1);
    }
  }

  return indices;
}

std::vector<std::uint32_t> BuildRetailDomeColors(const world::SkyColors& colors, const float hour,
                                                 const SkyDomeRuntimeState& state) {

  std::array<std::uint32_t, 6> band_colors = {
      colors.colors[static_cast<std::size_t>(world::SkyColorSlot::kSkyTop)],
      colors.colors[static_cast<std::size_t>(world::SkyColorSlot::kSkyMiddle)],
      colors.colors[static_cast<std::size_t>(world::SkyColorSlot::kSkyBand1)],
      colors.colors[static_cast<std::size_t>(world::SkyColorSlot::kSkyBand2)],
      colors.colors[static_cast<std::size_t>(world::SkyColorSlot::kSkySmog)],
      colors.colors[static_cast<std::size_t>(world::SkyColorSlot::kSkyFog)],
  };

  if ((state.sky_update_mask & 1u) != 0u) {
    const std::uint8_t blend_factor = RoundBlendByte(1.0f - state.fog_band_fade_factor);
    for (std::uint32_t& color : band_colors) {
      color = BlendPackedArgbRgb(color, blend_factor, state.fog_band_fade_color_argb);
    }
  }

  const float normalized_time = hour / 24.0f;
  const float highlight_factor =
      EvaluateWrappedFloatCurve(kRetailSkyHighlightCurve,
                                sizeof(kRetailSkyHighlightCurve) /
                                    sizeof(kRetailSkyHighlightCurve[0]),
                                normalized_time) *
      colors.highlight_sky;

  std::array<std::uint32_t, 5> preblended_bands{};
  for (std::size_t index = 0; index < preblended_bands.size(); ++index) {
    preblended_bands[index] =
        BlendOpaqueArgbRgb(band_colors[1 + index], band_colors[1], highlight_factor);
  }

  std::vector<std::uint32_t> color_stream;
  color_stream.reserve(kRetailSkyDomeVertexCount);
  color_stream.push_back(band_colors[0] | 0xFF000000u);

  float phase =
      ComputeRetailSkyDomePhase(state.camera_forward) * kRetailWrappedPhaseScale +
      kRetailSkyDomePhaseOffset;
  if (phase > 1.0f) {
    phase -= 1.0f;
  }

  for (std::size_t ring = 0; ring < kRetailSkyDomeAzimuthTintRingCount; ++ring) {
    float segment_phase = phase;
    for (std::size_t segment = 0; segment < kRetailSkyDomeLongitudeSegmentCount; ++segment) {
      const float wrapped_phase =
          (segment_phase < 0.0f) ? (segment_phase + 1.0f) : segment_phase;
      const float curve_value =
          EvaluateWrappedFloatCurve(kRetailSkyBandBlendCurve,
                                    sizeof(kRetailSkyBandBlendCurve) /
                                        sizeof(kRetailSkyBandBlendCurve[0]),
                                    wrapped_phase);

      std::uint32_t color = 0;
      if (curve_value >= 0.0f) {
        color = BlendOpaqueArgbRgb(preblended_bands[ring], band_colors[ring + 1u],
                                   (1.0f - curve_value) * highlight_factor);
      } else {
        const std::uint32_t inner_mix =
            BlendOpaqueArgbRgb(preblended_bands[ring], band_colors[0],
                               highlight_factor * kRetailSkyTintInnerBlend);
        color = BlendOpaqueArgbRgb(preblended_bands[ring], inner_mix,
                                   -curve_value * highlight_factor);
      }

      if (state.spell_visual_tint_blend != 0u) {
        color = BlendPackedArgbRgb(color, state.spell_visual_tint_blend,
                                   state.spell_visual_tint_argb);
      }

      color_stream.push_back(color);
      segment_phase -= kRetailSkyDomeStep;
    }
  }

  std::uint32_t horizon_color = band_colors[5];
  if (state.spell_visual_tint_blend != 0u) {
    horizon_color = BlendPackedArgbRgb(
        horizon_color, state.spell_visual_tint_blend, state.spell_visual_tint_argb);
  }
  for (std::size_t segment = 0; segment < kRetailSkyDomeLongitudeSegmentCount; ++segment) {
    color_stream.push_back(horizon_color);
  }
  color_stream.push_back(horizon_color);

  return color_stream;
}

std::string NormalizeModelPath(const std::string& raw) {
  std::string path = raw;
  for (char& ch : path) {
    if (ch == '\\') {
      ch = '/';
    }
  }

  while (!path.empty() && (path.back() == ' ' || path.back() == '\0')) {
    path.pop_back();
  }
  return path;
}

}

struct SkyVertex {
  float position[3];
  uint32_t abgr;
};

struct SkyRenderer::ZoneSkyboxResource {
  std::string path;
  std::uint32_t m2_model_id{0};
  std::uint32_t m2_instance_id{0};
  std::uint32_t sequence_duration_ms{0};
  bool loaded{false};
  bool terminal_load_failure{false};
};

struct SkyRenderer::DayNightSkyModelResource {
  std::string path;
  std::uint32_t daynight_resource_id{0};
  std::uint32_t m2_model_id{0};
  std::uint32_t m2_instance_id{0};
  std::uint32_t sequence_duration_ms{0};
  ZoneSkyboxAnimationState animation_state{};
  bool loaded{false};
  bool terminal_load_failure{false};
};

struct SkyRenderer::StarsModelResource {
  std::uint32_t m2_instance_id{0};
  float pending_animation_seconds{0.0f};
  bool loaded{false};
  bool terminal_load_failure{false};
};

SkyRenderer::SkyRenderer(TextureManager& texture_manager,
                         m2::M2System& m2_system,
                         SkySettingsProvider sky_settings)
    : sky_settings_(std::move(sky_settings)),
      m2_system_(m2_system),
      celestial_renderer_(texture_manager) {}

SkyRenderer::~SkyRenderer() { Shutdown(); }

bool SkyRenderer::Initialize() {
  if (initialized_) return true;

  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  const auto type = bgfx::getRendererType();
  program_ = CreateEmbeddedProgram(ShaderProgramId::Sky, type);

  if (!bgfx::isValid(program_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                        "SkyRenderer: shader program creation failed");
    return false;
  }

  for (auto& c : current_colors_.colors) c = 0xFF4488CC;
  current_colors_.colors[static_cast<int>(world::SkyColorSlot::kSkyTop)] = 0xFF1E3A6E;
  current_colors_.colors[static_cast<int>(world::SkyColorSlot::kSkyMiddle)] = 0xFF3060A8;
  current_colors_.colors[static_cast<int>(world::SkyColorSlot::kSkyBand1)] = 0xFF5080C0;
  current_colors_.colors[static_cast<int>(world::SkyColorSlot::kSkyBand2)] = 0xFF78A0D0;
  current_colors_.colors[static_cast<int>(world::SkyColorSlot::kSkySmog)] = 0xFF90B0D8;
  current_colors_.colors[static_cast<int>(world::SkyColorSlot::kSkyFog)] = 0xFFA0C0E0;

  BuildDomeMesh();

  if (!celestial_renderer_.Initialize()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                        "SkyRenderer: celestial renderer initialization failed");

  }

  initialized_ = true;
  return true;
}

void SkyRenderer::SetFileLoader(LoadFileCallback callback) {
  load_file_ = std::move(callback);
  m2_system_.SetFileLoader(load_file_);
}

void SkyRenderer::Update(const float dt_seconds, const float hour_of_day) {
  const SkyRenderSettings sky_settings =
      sky_settings_ ? sky_settings_() : SkyRenderSettings{};

  celestial_renderer_.SetGlareEnabled(sky_settings.sun_glare_enabled);

  (void)openwow::game::DayNight_ApplySkyCloudLod(sky_settings.cloud_lod, false);

  SetTimeOfDay(hour_of_day);
  UpdateLoading();
  UpdateZoneSkyboxAnimation(dt_seconds);
  UpdateDayNightSkyModelAnimations(dt_seconds);
  AccumulateStarsModelAnimation(dt_seconds);
  celestial_renderer_.Update();
}

void SkyRenderer::SetZoneSkybox(std::optional<world::ZoneSkyboxEntry> entry) {
  if (zone_skybox_ == entry) {
    return;
  }

  zone_skybox_ = std::move(entry);
  UnloadZoneSkybox();
}

void SkyRenderer::UpdateLoading() {
  if (!initialized_) {
    return;
  }

  LoadZoneSkyboxIfNeeded();
  static_cast<void>(LoadDayNightSkyModelSlotsIfNeeded());
  LoadStarsModelIfNeeded();
}

void SkyRenderer::SetColors(const world::SkyColors& colors) {
  current_colors_ = colors;
  dome_colors_dirty_ = true;
}

void SkyRenderer::SetDomeRuntimeState(const SkyDomeRuntimeState& state) {
  dome_runtime_state_ = state;
  dome_colors_dirty_ = true;
}

void SkyRenderer::BuildDomeMesh() {
  dome_positions_ = BuildRetailDomePositions();
  dome_colors_.assign(dome_positions_.size(), 0xFF000000u);
  dome_indices_ = BuildRetailDomeIndices();
  index_count_ = static_cast<uint32_t>(dome_indices_.size());

  vb_ = bgfx::createDynamicVertexBuffer(
      static_cast<std::uint32_t>(dome_positions_.size()), layout_);
  const bgfx::Memory* ib_mem = bgfx::copy(
      dome_indices_.data(), static_cast<uint32_t>(dome_indices_.size() * sizeof(std::uint16_t)));
  ib_ = bgfx::createIndexBuffer(ib_mem);
  dome_colors_dirty_ = true;

  if (!bgfx::isValid(vb_) || !bgfx::isValid(ib_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                        "SkyRenderer: dome buffer creation failed");
  }
}

void SkyRenderer::UpdateRetailDomeVertexColors() {
  if (!dome_colors_dirty_ || dome_positions_.empty() || !bgfx::isValid(vb_)) {
    return;
  }

  dome_colors_ = BuildRetailDomeColors(active_colors(), time_of_day_, dome_runtime_state_);

  std::vector<SkyVertex> vertices(dome_positions_.size());
  for (std::size_t index = 0; index < dome_positions_.size(); ++index) {
    vertices[index].position[0] = dome_positions_[index][0];
    vertices[index].position[1] = dome_positions_[index][1];
    vertices[index].position[2] = dome_positions_[index][2];

    std::uint8_t rgba[4];
    UnpackArgbToRgba(dome_colors_[index], rgba);
    vertices[index].abgr = (static_cast<std::uint32_t>(rgba[3]) << 24) |
                           (static_cast<std::uint32_t>(rgba[2]) << 16) |
                           (static_cast<std::uint32_t>(rgba[1]) << 8) |
                           (static_cast<std::uint32_t>(rgba[0]) << 0);
  }

  const bgfx::Memory* vb_mem = bgfx::copy(
      vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(SkyVertex)));
  bgfx::update(vb_, 0, vb_mem);
  dome_colors_dirty_ = false;
}

void SkyRenderer::Render(uint8_t view_id, const float* view_mtx,
                          const float* proj_mtx, float camera_x,
                          float camera_y, float camera_z,
                          bgfx::Encoder* const encoder) {
  if (!initialized_) return;
  if (!bgfx::isValid(vb_) || !bgfx::isValid(ib_)) return;
  const DayNightSkyModelLoadSummary sky_model_summary =
      LoadDayNightSkyModelSlotsIfNeeded();
  const SkyModelRenderPlan sky_model_plan =
      BuildSkyModelRenderPlan(sky_model_summary, zone_skybox_.has_value());
  if (!sky_model_plan.render_procedural_dome) {
    return;
  }

  UpdateRetailDomeVertexColors();

  const DrawEncoder draw{encoder};

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

  float model[16];
  std::memset(model, 0, sizeof(model));
  model[0] = 1.0f;
  model[5] = 1.0f;
  model[10] = 1.0f;
  model[15] = 1.0f;
  model[12] = camera_x;
  model[13] = camera_y;
  model[14] = camera_z;

  const uint64_t state =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;

  draw.setTransform(model);
  draw.setVertexBuffer(0, vb_);
  draw.setIndexBuffer(ib_, 0, index_count_);
  draw.setState(state);
  draw.submit(view_id, program_);

  RenderStarsModel(view_id, view_mtx);

  celestial_renderer_.Render(view_id, view_mtx, proj_mtx,
                              camera_x, camera_y, camera_z, encoder);
}

void SkyRenderer::RenderZoneSkybox(uint8_t view_id, const float* view_mtx,
                                   const float* proj_mtx, float camera_x,
                                   float camera_y, float camera_z) {
  if (!initialized_) {
    return;
  }

  const DayNightSkyModelLoadSummary sky_model_summary =
      LoadDayNightSkyModelSlotsIfNeeded();
  const SkyModelRenderPlan sky_model_plan =
      BuildSkyModelRenderPlan(sky_model_summary, zone_skybox_.has_value());
  if (sky_model_plan.render_zone_skybox) {
    if (!zone_skybox_.has_value()) {
      return;
    }

    LoadZoneSkyboxIfNeeded();
    if (!zone_skybox_resource_ || !zone_skybox_resource_->loaded) {
      return;
    }

    bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

    auto& resource = *zone_skybox_resource_;
    const int total_minutes = static_cast<int>(std::floor(time_of_day_ * 60.0f));
    const ZoneSkyboxAnimationSyncResult sync = SyncZoneSkyboxAnimationForSample(
        zone_skybox_animation_state_, resource.sequence_duration_ms, zone_skybox_->flags,
        total_minutes);

    const RenderMatrix4x4 model_matrix = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        camera_x, camera_y, camera_z, 1.0f,
    };
    auto& system = m2_system_;
    m2::M2ResultStatus setup_status = m2::M2ResultStatus::kReady;
    const auto merge_setup_status = [&setup_status](const m2::M2ResultStatus status) {
      setup_status = m2::MergeM2ResultStatus(setup_status, status);
    };
    merge_setup_status(system.SetWorldTransformMatrix(resource.m2_instance_id, model_matrix));
    if (sync.should_set_sample) {
      merge_setup_status(system.SetAnimationSample(resource.m2_instance_id, 0u,
                                                   sync.sample_time_ms,
                                                   sync.playback_speed));
    }
    merge_setup_status(system.ClearBatchUniforms(resource.m2_instance_id));
    merge_setup_status(system.ClearVisibleSubmeshIndices(resource.m2_instance_id));
    merge_setup_status(system.SetVisible(resource.m2_instance_id, true));
    merge_setup_status(system.SetAlpha(resource.m2_instance_id, 1.0f));
    if (m2::IsTerminalM2ResultStatus(setup_status)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         std::string("SkyRenderer: terminal M2 skybox setup failure: ") +
                             m2::M2ResultStatusName(setup_status));
      UnloadZoneSkybox();
      return;
    }
    if (setup_status != m2::M2ResultStatus::kReady) {
      return;
    }

    const auto render_result =
        system.RenderInstance(view_id, resource.m2_instance_id,
                              RenderMatrix4x4View{view_mtx, 16u});
    if (m2::IsTerminalM2ResultStatus(render_result.status)) {
      UnloadZoneSkybox();
    }
    return;
  }

  if (!sky_model_plan.render_daynight_sky_models) {
    return;
  }

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);
  const openwow::game::DayNightSkyModelSlots slots = openwow::game::DayNight_GetSkyModelSlots();
  for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
    const auto& slot = slots[slot_index];
    const auto& resource = daynight_sky_model_resources_[slot_index];
    if (!slot.active || slot.alpha <= 0.0f || !resource || !resource->loaded ||
        resource->path != NormalizeModelPath(slot.path)) {
      continue;
    }

    RenderLoadedSkyModel(view_id, view_mtx, proj_mtx, camera_x, camera_y, camera_z,
                         *resource, slot.alpha, slot.flags);
  }
}

void SkyRenderer::Shutdown() {
  if (!initialized_) return;
  UnloadZoneSkybox();
  UnloadDayNightSkyModelResources();
  UnloadStarsModel();
  if (bgfx::isValid(vb_)) bgfx::destroy(vb_);
  if (bgfx::isValid(ib_)) bgfx::destroy(ib_);
  if (bgfx::isValid(program_)) bgfx::destroy(program_);
  vb_ = BGFX_INVALID_HANDLE;
  ib_ = BGFX_INVALID_HANDLE;
  program_ = BGFX_INVALID_HANDLE;
  dome_positions_.clear();
  dome_colors_.clear();
  dome_indices_.clear();
  celestial_renderer_.Shutdown();
  initialized_ = false;
}

void SkyRenderer::SetTimeOfDay(float hour) {
  while (hour < 0.0f) hour += 24.0f;
  while (hour >= 24.0f) hour -= 24.0f;
  if (std::fabs(hour - time_of_day_) <= 0.0001f) {
    return;
  }
  time_of_day_ = hour;
  dome_colors_dirty_ = true;

}

void SkyRenderer::SyncZoneSkyboxAnimation(ZoneSkyboxAnimationState& state,
                                          const std::uint32_t sequence_duration_ms,
                                          const std::uint32_t skybox_flags,
                                          const int total_minutes) {
  static_cast<void>(SyncZoneSkyboxAnimationForSample(state, sequence_duration_ms,
                                                     skybox_flags, total_minutes));
}

ZoneSkyboxAnimationSyncResult SkyRenderer::SyncZoneSkyboxAnimationForSample(
    ZoneSkyboxAnimationState& state, const std::uint32_t sequence_duration_ms,
    const std::uint32_t skybox_flags, const int total_minutes) {
  ZoneSkyboxAnimationSyncResult result{
      .sample_time_ms = state.current_animation_time_ms,
      .playback_speed = state.animation_speed,
  };

  if (state.sequence_duration_ms == 0u) {
    state.sequence_duration_ms = sequence_duration_ms;
  }

  if (state.sequence_duration_ms == 0u) {
    state.last_total_minutes = total_minutes;
    return result;
  }

  const auto bind_sample = [&](const std::uint32_t sample_time_ms, const float playback_speed) {
    state.current_animation_time_ms = sample_time_ms;
    state.animation_speed = playback_speed;
    state.sample_bound = true;
    result.should_set_sample = true;
    result.sample_time_ms = sample_time_ms;
    result.playback_speed = playback_speed;
  };

  const bool sync_to_time_of_day = (skybox_flags & kRetailSkyboxTimeOfDaySyncFlag) != 0u;
  const int minute_delta = total_minutes - state.last_total_minutes;
  const bool time_jump = minute_delta < -1 || minute_delta > 1;

  if (!state.sample_bound) {
    if (sync_to_time_of_day) {
      const double duration_ms = static_cast<double>(state.sequence_duration_ms);
      bind_sample(static_cast<std::uint32_t>(
                      duration_ms * (static_cast<double>(total_minutes) / kMinutesPerDay)),
                  static_cast<float>(duration_ms / kMillisecondsPerDay));
    } else {
      bind_sample(state.current_animation_time_ms, state.animation_speed);
    }
  } else if (sync_to_time_of_day && time_jump) {
    const double duration_ms = static_cast<double>(state.sequence_duration_ms);
    bind_sample(static_cast<std::uint32_t>(
                    duration_ms * (static_cast<double>(total_minutes) / kMinutesPerDay)),
                static_cast<float>(duration_ms / kMillisecondsPerDay));
  }

  state.last_total_minutes = total_minutes;
  return result;
}

void SkyRenderer::ComputeSunDirection(float hour, float out[3]) {

  constexpr float kTwoPi = 2.0f * 3.14159265358979323846f;
  const float angle = ((hour - 6.0f) / 24.0f) * kTwoPi;
  out[0] = -std::cos(angle);
  out[1] = 0.0f;
  out[2] = std::sin(angle);

}

void SkyRenderer::SetIndoorColors(const world::SkyColors& colors) {
  indoor_colors_ = colors;
  is_indoor_ = true;
  dome_colors_dirty_ = true;
}

void SkyRenderer::ClearIndoorColors() {
  is_indoor_ = false;
  indoor_colors_ = {};
  dome_colors_dirty_ = true;
}

void SkyRenderer::Reset() {
  current_colors_ = {};
  indoor_colors_ = {};
  is_indoor_ = false;
  time_of_day_ = 12.0f;
  zone_skybox_animation_state_ = {};
  dome_runtime_state_ = {};
  dome_colors_dirty_ = true;
  UnloadDayNightSkyModelResources();

  celestial_renderer_.Reset();
}

StarsModelRenderPlan SkyRenderer::BuildStarsModelRenderPlan(
    const openwow::game::DayNightStarsModelState& state) {
  return {
      .render = openwow::game::DayNight_ShouldRenderStarsModel(state.alpha),
      .alpha = openwow::game::DayNight_NormalizeStarsModelAlpha(state.alpha),
      .position = {state.position.x, state.position.y, state.position.z},
  };
}

SkyModelRenderPlan SkyRenderer::BuildSkyModelRenderPlan(
    const DayNightSkyModelLoadSummary& summary, const bool has_zone_skybox) {
  SkyModelRenderPlan plan{};

  plan.render_procedural_dome = true;
  plan.render_daynight_sky_models = summary.loaded_slot_count != 0u;
  plan.render_zone_skybox = has_zone_skybox && !plan.render_daynight_sky_models;
  return plan;
}

void SkyRenderer::UpdateZoneSkyboxAnimation(const float dt_seconds) {
  if (!initialized_ || !zone_skybox_resource_ || !zone_skybox_resource_->loaded ||
      zone_skybox_resource_->m2_instance_id == 0u || dt_seconds <= 0.0f ||
      !zone_skybox_animation_state_.sample_bound) {
    return;
  }

  const auto status =
      m2_system_.UpdateAnimation(
          zone_skybox_resource_->m2_instance_id, dt_seconds);
  if (m2::IsTerminalM2ResultStatus(status)) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
              std::string("SkyRenderer: terminal M2 skybox animation failure: ") +
                  m2::M2ResultStatusName(status));
    UnloadZoneSkybox();
  }
}

void SkyRenderer::UpdateDayNightSkyModelAnimations(const float dt_seconds) {
  if (!initialized_ || dt_seconds <= 0.0f) {
    return;
  }

  const openwow::game::DayNightSkyModelSlots slots = openwow::game::DayNight_GetSkyModelSlots();
  for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
    auto& resource = daynight_sky_model_resources_[slot_index];
    if (!slots[slot_index].active || !resource || !resource->loaded ||
        resource->m2_instance_id == 0u || !resource->animation_state.sample_bound) {
      continue;
    }

    const auto status =
        m2_system_.UpdateAnimation(resource->m2_instance_id, dt_seconds);
    if (m2::IsTerminalM2ResultStatus(status)) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                std::string("SkyRenderer: terminal DayNight sky model animation failure: ") +
                    m2::M2ResultStatusName(status));
      UnloadDayNightSkyModelResource(*resource);
      resource.reset();
    }
  }
}

void SkyRenderer::AccumulateStarsModelAnimation(const float dt_seconds) {
  if (!initialized_ || dt_seconds <= 0.0f || !stars_model_resource_ ||
      stars_model_resource_->terminal_load_failure) {
    return;
  }

  stars_model_resource_->pending_animation_seconds += dt_seconds;
}

void SkyRenderer::UnloadZoneSkybox() {
  if (zone_skybox_resource_ &&
      zone_skybox_resource_->m2_instance_id != 0u) {
    const auto destroy_status =
        m2_system_.DestroyInstance(zone_skybox_resource_->m2_instance_id);
    if (destroy_status != m2::M2ResultStatus::kReady) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                std::string("SkyRenderer: M2 instance destroy ") +
                    m2::M2ResultStatusName(destroy_status));
    }
  }

  zone_skybox_resource_.reset();
  zone_skybox_animation_state_ = {};
}

void SkyRenderer::UnloadDayNightSkyModelResource(DayNightSkyModelResource& resource) {
  if (resource.m2_instance_id != 0u) {
    const auto destroy_status =
        m2_system_.DestroyInstance(resource.m2_instance_id);
    if (destroy_status != m2::M2ResultStatus::kReady) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                std::string("SkyRenderer: DayNight sky M2 instance destroy ") +
                    m2::M2ResultStatusName(destroy_status));
    }
  }
  resource = {};
}

void SkyRenderer::UnloadDayNightSkyModelResources() {
  for (auto& resource : daynight_sky_model_resources_) {
    if (resource) {
      UnloadDayNightSkyModelResource(*resource);
      resource.reset();
    }
  }
}

void SkyRenderer::UnloadStarsModel() {
  if (stars_model_resource_ && stars_model_resource_->m2_instance_id != 0u) {
    const auto destroy_status =
        m2_system_.DestroyInstance(stars_model_resource_->m2_instance_id);
    if (destroy_status != m2::M2ResultStatus::kReady) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                std::string("SkyRenderer: stars M2 instance destroy ") +
                    m2::M2ResultStatusName(destroy_status));
    }
  }
  stars_model_resource_.reset();
}

void SkyRenderer::LoadZoneSkyboxIfNeeded() {
  if (!zone_skybox_.has_value() || !load_file_) {
    return;
  }

  const std::string normalized_path = NormalizeModelPath(zone_skybox_->model_path);
  if (normalized_path.empty()) {
    return;
  }

  if (zone_skybox_resource_ && zone_skybox_resource_->path == normalized_path) {
    return;
  }

  if (!zone_skybox_resource_) {
    zone_skybox_resource_ = std::make_unique<ZoneSkyboxResource>();
  } else if (zone_skybox_resource_->path != normalized_path) {
    UnloadZoneSkybox();
    zone_skybox_resource_ = std::make_unique<ZoneSkyboxResource>();
  }

  auto& resource = *zone_skybox_resource_;
  if (resource.terminal_load_failure || resource.loaded) {
    return;
  }
  resource.path = normalized_path;

  auto& system = m2_system_;
  const auto instance_result = system.LoadModelInstance(normalized_path);
  if (m2::IsTerminalM2ResultStatus(instance_result.status)) {
    resource.terminal_load_failure = true;
    return;
  }
  if (instance_result.status != m2::M2ResultStatus::kReady ||
      instance_result.model_id == 0u || instance_result.instance_id == 0u) {
    return;
  }

  const std::uint32_t model_id = instance_result.model_id;
  resource.m2_model_id = instance_result.model_id;
  resource.m2_instance_id = instance_result.instance_id;
  const auto duration = system.QueryFirstAnimationDuration(model_id);
  resource.sequence_duration_ms =
      duration.status == m2::M2ResultStatus::kReady ? duration.duration_ms : 0u;
  resource.loaded = true;
}

void SkyRenderer::LoadStarsModelIfNeeded() {
  if (!load_file_) {
    return;
  }

  const std::string normalized_path =
      NormalizeModelPath(openwow::game::kDayNightStarsModelPath);
  if (!stars_model_resource_) {
    stars_model_resource_ = std::make_unique<StarsModelResource>();
  }

  auto& resource = *stars_model_resource_;
  if (resource.loaded || resource.terminal_load_failure) {
    return;
  }

  const auto instance_result = m2_system_.LoadModelInstance(normalized_path);
  if (m2::IsTerminalM2ResultStatus(instance_result.status)) {
    resource.terminal_load_failure = true;
    return;
  }
  if (instance_result.status != m2::M2ResultStatus::kReady ||
      instance_result.model_id == 0u || instance_result.instance_id == 0u) {
    return;
  }

  resource.m2_instance_id = instance_result.instance_id;
  resource.loaded = true;
}

DayNightSkyModelLoadSummary SkyRenderer::LoadDayNightSkyModelSlotsIfNeeded() {
  const openwow::game::DayNightSkyModelSlots slots = openwow::game::DayNight_GetSkyModelSlots();
  DayNightSkyModelLoadSummary summary{};

  for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
    const auto& slot = slots[slot_index];
    const std::string normalized_path = NormalizeModelPath(slot.path);
    auto& resource = daynight_sky_model_resources_[slot_index];
    if (!slot.active || slot.alpha <= 0.0f || normalized_path.empty()) {
      if (resource) {
        UnloadDayNightSkyModelResource(*resource);
        resource.reset();
      }
      continue;
    }

    ++summary.active_slot_count;
    if (resource && (resource->path != normalized_path ||
                     resource->daynight_resource_id != slot.resourceId)) {
      UnloadDayNightSkyModelResource(*resource);
      resource.reset();
    }

    if (!resource && load_file_) {
      resource = std::make_unique<DayNightSkyModelResource>();
      resource->path = normalized_path;
      resource->daynight_resource_id = slot.resourceId;
    }

    if (resource && !resource->loaded && !resource->terminal_load_failure) {
      auto& system = m2_system_;
      const auto instance_result = system.LoadModelInstance(normalized_path);
      if (instance_result.status == m2::M2ResultStatus::kReady &&
          instance_result.model_id != 0u && instance_result.instance_id != 0u) {
        resource->m2_model_id = instance_result.model_id;
        resource->m2_instance_id = instance_result.instance_id;
        const auto duration = system.QueryFirstAnimationDuration(instance_result.model_id);
        resource->sequence_duration_ms =
            duration.status == m2::M2ResultStatus::kReady ? duration.duration_ms : 0u;
        resource->loaded = true;
      } else if (m2::IsTerminalM2ResultStatus(instance_result.status)) {
        resource->terminal_load_failure = true;
      }
    }

    if (resource && resource->loaded && resource->path == normalized_path) {
      ++summary.loaded_slot_count;
    }
  }

  return summary;
}

void SkyRenderer::RenderLoadedSkyModel(uint8_t view_id, const float* view_mtx,
                                       const float* proj_mtx, float camera_x,
                                       float camera_y, float camera_z,
                                       DayNightSkyModelResource& resource,
                                       const float alpha, const std::uint32_t flags) {

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

  const int total_minutes = static_cast<int>(std::floor(time_of_day_ * 60.0f));
  const ZoneSkyboxAnimationSyncResult sync = SyncZoneSkyboxAnimationForSample(
      resource.animation_state, resource.sequence_duration_ms, flags, total_minutes);

  const RenderMatrix4x4 model_matrix = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      camera_x, camera_y, camera_z, 1.0f,
  };

  auto& system = m2_system_;
  m2::M2ResultStatus setup_status = m2::M2ResultStatus::kReady;
  const auto merge_setup_status = [&setup_status](const m2::M2ResultStatus status) {
    setup_status = m2::MergeM2ResultStatus(setup_status, status);
  };

  merge_setup_status(system.SetWorldTransformMatrix(resource.m2_instance_id, model_matrix));
  if (sync.should_set_sample) {
    merge_setup_status(system.SetAnimationSample(resource.m2_instance_id, 0u,
                                                 sync.sample_time_ms,
                                                 sync.playback_speed));
  }
  merge_setup_status(system.ClearBatchUniforms(resource.m2_instance_id));
  merge_setup_status(system.ClearVisibleSubmeshIndices(resource.m2_instance_id));
  merge_setup_status(system.SetVisible(resource.m2_instance_id, true));
  merge_setup_status(system.SetAlpha(resource.m2_instance_id, std::clamp(alpha, 0.0f, 1.0f)));

  if (m2::IsTerminalM2ResultStatus(setup_status)) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
              std::string("SkyRenderer: terminal DayNight sky model setup failure: ") +
                  m2::M2ResultStatusName(setup_status));
    UnloadDayNightSkyModelResource(resource);
    return;
  }
  if (setup_status != m2::M2ResultStatus::kReady) {
    return;
  }

  const auto render_result =
      system.RenderInstance(view_id, resource.m2_instance_id,
                            RenderMatrix4x4View{view_mtx, 16u});
  if (m2::IsTerminalM2ResultStatus(render_result.status)) {
    UnloadDayNightSkyModelResource(resource);
  }
}

void SkyRenderer::RenderStarsModel(const uint8_t view_id, const float* view_mtx) {
  if (!stars_model_resource_ || !stars_model_resource_->loaded ||
      stars_model_resource_->m2_instance_id == 0u) {
    return;
  }

  const StarsModelRenderPlan plan =
      BuildStarsModelRenderPlan(openwow::game::DayNight_GetStarsModelState());
  if (!plan.render) {
    return;
  }

  auto& resource = *stars_model_resource_;
  const RenderMatrix4x4 model_matrix = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      plan.position[0], plan.position[1], plan.position[2], 1.0f,
  };

  auto& system = m2_system_;
  m2::M2ResultStatus setup_status = m2::M2ResultStatus::kReady;
  const auto merge_setup_status = [&setup_status](const m2::M2ResultStatus status) {
    setup_status = m2::MergeM2ResultStatus(setup_status, status);
  };

  merge_setup_status(system.SetWorldTransformMatrix(resource.m2_instance_id, model_matrix));
  merge_setup_status(system.SetVisible(resource.m2_instance_id, true));
  merge_setup_status(system.SetAlpha(resource.m2_instance_id, plan.alpha));
  if (resource.pending_animation_seconds > 0.0f) {
    const auto animation_status = system.UpdateAnimation(
        resource.m2_instance_id, resource.pending_animation_seconds);
    merge_setup_status(animation_status);
    if (animation_status == m2::M2ResultStatus::kReady) {
      resource.pending_animation_seconds = 0.0f;
    }
  }
  merge_setup_status(system.ClearBatchUniforms(resource.m2_instance_id));
  merge_setup_status(system.ClearVisibleSubmeshIndices(resource.m2_instance_id));

  if (m2::IsTerminalM2ResultStatus(setup_status)) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
              std::string("SkyRenderer: terminal stars M2 setup failure: ") +
                  m2::M2ResultStatusName(setup_status));
    UnloadStarsModel();
    return;
  }
  if (setup_status != m2::M2ResultStatus::kReady) {
    return;
  }

  const auto render_result =
      system.RenderInstance(view_id, resource.m2_instance_id,
                            RenderMatrix4x4View{view_mtx, 16u});
  if (m2::IsTerminalM2ResultStatus(render_result.status)) {
    UnloadStarsModel();
  }
}

}
