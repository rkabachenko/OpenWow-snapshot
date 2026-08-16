#include "openwow/render/world/water/water_particulate_system.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"

#include "openwow/foundation/hashing/retail_adler_seed.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/api/draw_encoder.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace openwow::render {

namespace {

constexpr std::size_t kParticleCount = 4000;
constexpr float kDefaultSizeBase = 1.0f / 36.0f;
constexpr float kWrapDistance = 30.0f;
constexpr float kTextureCellExtent = 51.0f / 256.0f;
constexpr float kFixedMotionDownPerSecond = -0.02f;
constexpr float kFixedMotionUpPerSecond = 0.02f;
constexpr float kMotionResetPhaseThreshold = 0.5f;
constexpr float kPi = 3.1415927f;
constexpr float kTwoPi = 6.2831855f;
constexpr std::uint32_t kLiquidSupportsParticulatesMask = 0x8u;

constexpr std::uint32_t kLiquidParticulatesFoggedMask = 0x10u;
constexpr char kWaterParticulateTexturePath[] = "Textures\\WaterPoop02.blp";

constexpr std::array<std::array<std::uint8_t, 8>, 5> kTextureFrameGroups = {{
    {0, 1, 2, 3, 4, 5, 6, 7},
    {8, 9, 10, 11, 8, 9, 10, 11},
    {12, 12, 12, 12, 12, 12, 12, 12},
    {13, 14, 15, 16, 13, 14, 15, 16},
    {17, 18, 19, 20, 21, 22, 23, 24},
}};

std::array<float, 2> ResolveTextureFrameOrigin(const std::uint8_t frame_index) {
  switch (frame_index) {
    case 0:
      return {0.0f, 0.0f};
    case 1:
      return {1.0f * kTextureCellExtent, 0.0f};
    case 2:
      return {2.0f * kTextureCellExtent, 0.0f};
    case 3:
      return {3.0f * kTextureCellExtent, 0.0f};
    case 4:
      return {0.0f, 1.0f * kTextureCellExtent};
    case 5:
      return {1.0f * kTextureCellExtent, 1.0f * kTextureCellExtent};
    case 6:
      return {2.0f * kTextureCellExtent, 1.0f * kTextureCellExtent};
    case 7:
      return {3.0f * kTextureCellExtent, 1.0f * kTextureCellExtent};
    case 8:
      return {0.0f, 2.0f * kTextureCellExtent};
    case 9:
      return {1.0f * kTextureCellExtent, 2.0f * kTextureCellExtent};
    case 10:
      return {2.0f * kTextureCellExtent, 2.0f * kTextureCellExtent};
    case 11:
      return {3.0f * kTextureCellExtent, 2.0f * kTextureCellExtent};
    case 12:
      return {4.0f * kTextureCellExtent, 0.0f};
    case 13:
      return {4.0f * kTextureCellExtent, 1.0f * kTextureCellExtent};
    case 14:
      return {4.0f * kTextureCellExtent, 2.0f * kTextureCellExtent};
    case 15:
      return {4.0f * kTextureCellExtent, 3.0f * kTextureCellExtent};
    case 16:
      return {4.0f * kTextureCellExtent, 4.0f * kTextureCellExtent};
    case 17:
      return {0.0f, 3.0f * kTextureCellExtent};
    case 18:
      return {1.0f * kTextureCellExtent, 3.0f * kTextureCellExtent};
    case 19:
      return {2.0f * kTextureCellExtent, 3.0f * kTextureCellExtent};
    case 20:
      return {3.0f * kTextureCellExtent, 3.0f * kTextureCellExtent};
    case 21:
      return {0.0f, 4.0f * kTextureCellExtent};
    case 22:
      return {1.0f * kTextureCellExtent, 4.0f * kTextureCellExtent};
    case 23:
      return {2.0f * kTextureCellExtent, 4.0f * kTextureCellExtent};
    case 24:
    default:
      return {3.0f * kTextureCellExtent, 4.0f * kTextureCellExtent};
  }
}

float WrapOffset(const float value, const float half_wrap_distance) {
  if (value > half_wrap_distance) {
    return value - 2.0f * half_wrap_distance;
  }
  if (value < -half_wrap_distance) {
    return value + 2.0f * half_wrap_distance;
  }
  return value;
}

float VectorLength(const std::array<float, 3>& value) {
  return std::sqrt(value[0] * value[0] + value[1] * value[1] +
                   value[2] * value[2]);
}

void NormalizeVector(std::array<float, 3>& value) {
  const float length = VectorLength(value);
  if (length <= 0.0f) {
    return;
  }

  const float inverse_length = 1.0f / length;
  value[0] *= inverse_length;
  value[1] *= inverse_length;
  value[2] *= inverse_length;
}

}

WaterParticulateSystem::WaterParticulateSystem(TextureManager& texture_manager)
    : texture_manager_(texture_manager) {}

WaterParticulateSystem::~WaterParticulateSystem() {
  Shutdown();
}

bool WaterParticulateSystem::Initialize() {
  if (initialized_) {
    return true;
  }

  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();

  const auto renderer_type = bgfx::getRendererType();
  program_ = openwow::render::CreateEmbeddedProgram(
      openwow::render::ShaderProgramId::Particle, renderer_type);
  u_particle_flags_ =
      bgfx::createUniform("u_particleFlags", bgfx::UniformType::Vec4);

  s_tex_color_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

  u_fog_params_ = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
  u_fog_color_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);

  if (!bgfx::isValid(program_) || !bgfx::isValid(u_particle_flags_) ||
      !bgfx::isValid(s_tex_color_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WaterParticulateSystem: shader setup failed");
    Shutdown();
    return false;
  }

  texture_lease_ =
      texture_manager_.AcquireTextureStrict(kWaterParticulateTexturePath);
  texture_ = BgfxTextureLeaseAccess::Get(texture_lease_);
  Reset();
  initialized_ = true;
  return true;
}

void WaterParticulateSystem::Shutdown() {
  particles_.clear();

  if (bgfx::isValid(s_tex_color_)) {
    bgfx::destroy(s_tex_color_);
  }
  if (bgfx::isValid(u_particle_flags_)) {
    bgfx::destroy(u_particle_flags_);
  }
  if (bgfx::isValid(u_fog_params_)) {
    bgfx::destroy(u_fog_params_);
  }
  if (bgfx::isValid(u_fog_color_)) {
    bgfx::destroy(u_fog_color_);
  }
  if (bgfx::isValid(program_)) {
    bgfx::destroy(program_);
  }

  program_ = BGFX_INVALID_HANDLE;
  u_particle_flags_ = BGFX_INVALID_HANDLE;
  s_tex_color_ = BGFX_INVALID_HANDLE;
  u_fog_params_ = BGFX_INVALID_HANDLE;
  u_fog_color_ = BGFX_INVALID_HANDLE;
  texture_lease_ = {};
  texture_ = BGFX_INVALID_HANDLE;
  initialized_ = false;
}

void WaterParticulateSystem::Reset() {
  particles_.assign(kParticleCount, Particle{});
  liquid_state_ = RuntimeLiquidState{
      .liquid_type_id = 2,
      .flags = 0u,
      .particle_movement = 0,
      .particle_tex_slots = 0,
      .size_base = kDefaultSizeBase,
      .render_enabled = false,
  };
  camera_position_.fill(0.0f);
  previous_camera_position_.fill(0.0f);
  motion_direction_.fill(0.0f);
  motion_frequency_ = 0.0f;
  motion_elapsed_ = 0.0f;
  motion_amplitude_ = 0.0f;
  observed_liquid_type_id_ = 0;
  render_flag_enabled_ = true;

  ResetParticles(liquid_state_.liquid_type_id);
  ResetMotion();
  liquid_state_.render_enabled = false;
}

void WaterParticulateSystem::BindDbc(
    const openwow::data::dbc::DbcLoader* dbc) {
  dbc_ = dbc;
}

void WaterParticulateSystem::Update(
    const float dt, const float camera_x, const float camera_y,
    const float camera_z, const std::uint32_t observed_liquid_type_id,
    const bool water_particulates_enabled) {
  camera_position_ = {camera_x, camera_y, camera_z};
  render_flag_enabled_ = water_particulates_enabled;

  if (observed_liquid_type_id != observed_liquid_type_id_) {
    ApplyObservedLiquidTransition(observed_liquid_type_id,
                                  water_particulates_enabled);
    observed_liquid_type_id_ = observed_liquid_type_id;
  }

  if (!water_particulates_enabled || observed_liquid_type_id_ == 0u ||
      !liquid_state_.render_enabled || particles_.empty()) {
    previous_camera_position_ = camera_position_;
    return;
  }

  EnsureTextureLoaded();

  std::array<float, 3> camera_delta = {
      previous_camera_position_[0] - camera_position_[0],
      previous_camera_position_[1] - camera_position_[1],
      previous_camera_position_[2] - camera_position_[2],
  };
  previous_camera_position_ = camera_position_;

  if (VectorLength(camera_delta) > kWrapDistance) {
    ResetParticles(liquid_state_.liquid_type_id);
    return;
  }

  const auto motion_delta = ComputeMotionDelta(dt);
  const float half_wrap_distance = 0.5f * kWrapDistance;
  for (auto& particle : particles_) {
    particle.offset[0] = WrapOffset(
        particle.offset[0] + camera_delta[0] + motion_delta[0],
        half_wrap_distance);
    particle.offset[1] = WrapOffset(
        particle.offset[1] + camera_delta[1] + motion_delta[1],
        half_wrap_distance);
    particle.offset[2] = WrapOffset(
        particle.offset[2] + camera_delta[2] + motion_delta[2],
        half_wrap_distance);
  }
}

void WaterParticulateSystem::Render(const std::uint8_t view_id,
                                    const float* view_mtx,
                                    const float* proj_mtx,
                                    const std::array<float, 4>& fog_params,
                                    const std::array<float, 4>& fog_color,
                                    bgfx::Encoder* const encoder) const {
  if (!initialized_ || !render_flag_enabled_ || observed_liquid_type_id_ == 0u ||
      !liquid_state_.render_enabled ||
      liquid_state_.liquid_type_id != observed_liquid_type_id_ ||
      particles_.empty() ||
      !bgfx::isValid(texture_)) {
    return;
  }

  const std::uint32_t vertex_count =
      static_cast<std::uint32_t>(particles_.size() * 4u);
  const std::uint32_t index_count =
      static_cast<std::uint32_t>(particles_.size() * 6u);
  if (bgfx::getAvailTransientVertexBuffer(vertex_count, layout_) < vertex_count ||
      bgfx::getAvailTransientIndexBuffer(index_count) < index_count) {
    return;
  }
  const DrawEncoder draw{encoder};

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  bgfx::allocTransientVertexBuffer(&tvb, vertex_count, layout_);
  bgfx::allocTransientIndexBuffer(&tib, index_count);

  auto* vertices = reinterpret_cast<ParticleVertex*>(tvb.data);
  auto* indices = reinterpret_cast<std::uint16_t*>(tib.data);

  const std::array<float, 3> right = {view_mtx[0], view_mtx[4], view_mtx[8]};
  const std::array<float, 3> up = {view_mtx[1], view_mtx[5], view_mtx[9]};
  const std::size_t frame_group_index =
      std::min<std::size_t>(liquid_state_.particle_tex_slots,
                            kTextureFrameGroups.size() - 1u);

  for (std::size_t i = 0; i < particles_.size(); ++i) {
    const auto& particle = particles_[i];
    const std::uint16_t base_vertex =
        static_cast<std::uint16_t>(i * 4u);
    const std::uint32_t base_index =
        static_cast<std::uint32_t>(i * 6u);
    const float center_x = camera_position_[0] + particle.offset[0];
    const float center_y = camera_position_[1] + particle.offset[1];
    const float center_z = camera_position_[2] + particle.offset[2];
    const float dx_r = right[0] * particle.size;
    const float dy_r = right[1] * particle.size;
    const float dz_r = right[2] * particle.size;
    const float dx_u = up[0] * particle.size;
    const float dy_u = up[1] * particle.size;
    const float dz_u = up[2] * particle.size;

    const auto frame_index =
        kTextureFrameGroups[frame_group_index][i & 7u];
    const auto frame_origin = ResolveTextureFrameOrigin(frame_index);
    const float u0 = frame_origin[0];
    const float v0 = frame_origin[1];
    const float u1 = u0 + kTextureCellExtent;
    const float v1 = v0 + kTextureCellExtent;

    vertices[base_vertex + 0] = {
        {center_x - dx_r - dx_u, center_y - dy_r - dy_u,
         center_z - dz_r - dz_u},
        0xFFFFFFFFu,
        {u0, v0},
    };
    vertices[base_vertex + 1] = {
        {center_x - dx_r + dx_u, center_y - dy_r + dy_u,
         center_z - dz_r + dz_u},
        0xFFFFFFFFu,
        {u0, v1},
    };
    vertices[base_vertex + 2] = {
        {center_x + dx_r - dx_u, center_y + dy_r - dy_u,
         center_z + dz_r - dz_u},
        0xFFFFFFFFu,
        {u1, v0},
    };
    vertices[base_vertex + 3] = {
        {center_x + dx_r + dx_u, center_y + dy_r + dy_u,
         center_z + dz_r + dz_u},
        0xFFFFFFFFu,
        {u1, v1},
    };

    indices[base_index + 0] = base_vertex + 0;
    indices[base_index + 1] = base_vertex + 1;
    indices[base_index + 2] = base_vertex + 2;
    indices[base_index + 3] = base_vertex + 3;
    indices[base_index + 4] = base_vertex + 2;
    indices[base_index + 5] = base_vertex + 1;
  }

  const float particle_flags[4] = {
      1.0f,
      (liquid_state_.flags & kLiquidParticulatesFoggedMask) != 0u ? 0.0f : 1.0f,
      0.0f, 1.0f};
  const std::uint64_t state =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
      BGFX_STATE_DEPTH_TEST_LESS |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                            BGFX_STATE_BLEND_INV_SRC_ALPHA) |
      BGFX_STATE_MSAA;

  draw.setVertexBuffer(0, &tvb);
  draw.setIndexBuffer(&tib);
  draw.setUniform(u_particle_flags_, particle_flags);

  draw.setUniform(u_fog_params_, fog_params.data());
  draw.setUniform(u_fog_color_, fog_color.data());
  draw.setTexture(0, s_tex_color_, texture_);
  draw.setState(state);
  draw.submit(view_id, program_);
}

void WaterParticulateSystem::EnsureTextureLoaded() {
  if (bgfx::isValid(texture_)) {
    return;
  }

  texture_lease_ =
      texture_manager_.AcquireTextureAsync(
          kWaterParticulateTexturePath,
          openwow::render::TextureLoadFailurePolicy::kStrict,
          openwow::render::TextureLoadPriority::kDemand);
  texture_ = BgfxTextureLeaseAccess::Get(texture_lease_);
}

void WaterParticulateSystem::EnsureRandomStateSeeded() {
  if (random_state_seeded_) {
    return;
  }

  std::uint32_t processor_count = std::thread::hardware_concurrency();
  if (processor_count == 0u) {
    processor_count = 1u;
  }

  random_state_ =
      openwow::foundation::hashing::MakeAdlerSeedState(processor_count);
  random_state_seeded_ = true;
}

void WaterParticulateSystem::ApplyObservedLiquidTransition(
    const std::uint32_t observed_liquid_type_id,
    const bool water_particulates_enabled) {
  if (!water_particulates_enabled || dbc_ == nullptr) {
    return;
  }

  if (observed_liquid_type_id == 0u) {
    liquid_state_.render_enabled = false;
    return;
  }

  const auto* const liquid_type =
      dbc_->liquid_type().LookupEntry(observed_liquid_type_id);
  if (liquid_type == nullptr) {
    liquid_state_.render_enabled = false;
    return;
  }

  liquid_state_.liquid_type_id = observed_liquid_type_id;
  liquid_state_.flags = liquid_type->flags;
  liquid_state_.particle_movement = liquid_type->particle_movement;
  liquid_state_.particle_tex_slots = liquid_type->particle_tex_slots;
  liquid_state_.size_base = liquid_type->particle_scale * kDefaultSizeBase;
  liquid_state_.render_enabled =
      (liquid_type->flags & kLiquidSupportsParticulatesMask) != 0u;

  if (!liquid_state_.render_enabled) {
    return;
  }

  EnsureTextureLoaded();
  ResetParticles(observed_liquid_type_id);
  ResetMotion();
}

void WaterParticulateSystem::ResetParticles(
    const std::uint32_t liquid_type_id) {
  EnsureRandomStateSeeded();

  const float half_wrap_distance = 0.5f * kWrapDistance;
  const float minimum_size = liquid_state_.size_base * 0.5f;
  const float size_range = liquid_state_.size_base;
  for (auto& particle : particles_) {
    particle.offset[0] = NextUnitFloat() * kWrapDistance - half_wrap_distance;
    particle.offset[1] = NextUnitFloat() * kWrapDistance - half_wrap_distance;
    particle.offset[2] = NextUnitFloat() * kWrapDistance - half_wrap_distance;
    particle.size = NextUnitFloat() * size_range + minimum_size;
  }

  liquid_state_.liquid_type_id = liquid_type_id;
}

void WaterParticulateSystem::ResetMotion() {
  EnsureRandomStateSeeded();

  const float polar_angle =
      openwow::foundation::hashing::AdlerSeedNextSignedUnitFloat(
          random_state_) *
      kPi;
  const float azimuth =
      openwow::foundation::hashing::AdlerSeedNextSignedUnitFloat(
          random_state_) *
      kPi;
  const float polar_sine = std::sin(polar_angle);

  motion_direction_[0] = polar_sine * std::cos(azimuth);
  motion_direction_[1] = std::sin(azimuth) * polar_sine;
  motion_direction_[2] = std::fabs(std::cos(polar_angle) * 0.25f);
  NormalizeVector(motion_direction_);

  motion_elapsed_ = 0.0f;
  motion_frequency_ =
      (openwow::foundation::hashing::AdlerSeedNextUnitFloat(random_state_) +
       1.0f) *
      0.0125f;
  motion_amplitude_ =
      (openwow::foundation::hashing::AdlerSeedNextUnitFloat(random_state_) +
       1.0f) *
      0.005f;
}

std::array<float, 3> WaterParticulateSystem::ComputeMotionDelta(
    const float dt) {
  switch (liquid_state_.particle_movement) {
    case 1:
      return {0.0f, 0.0f, kFixedMotionDownPerSecond * dt};
    case 2:
      return {0.0f, 0.0f, kFixedMotionUpPerSecond * dt};
    default:
      break;
  }

  motion_elapsed_ += dt;
  float phase = motion_elapsed_ * motion_frequency_;
  if (phase >= kMotionResetPhaseThreshold) {
    ResetMotion();
    phase = 0.0f;
  }

  const float motion_scale = std::sin(phase * kTwoPi) * motion_amplitude_;
  return {
      motion_direction_[0] * motion_scale,
      motion_direction_[1] * motion_scale,
      motion_direction_[2] * motion_scale,
  };
}

float WaterParticulateSystem::NextUnitFloat() {
  EnsureRandomStateSeeded();
  return openwow::foundation::hashing::AdlerSeedNextUnitFloat(random_state_);
}

}
