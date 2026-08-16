#include "openwow/render/world/environment/weather_renderer.h"

#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace openwow::render {
namespace {

constexpr std::size_t kMaximumPrimaryParticles = 6144;

constexpr float kPrimaryBillboardHalfSize = 1.0f / 12.0f;

struct PrimaryHalfExtent {
  float x;
  float y;
  float z;
};
constexpr PrimaryHalfExtent kPrimaryHalfExtentRain{65.0f, 65.0f, 37.5f};
constexpr PrimaryHalfExtent kPrimaryHalfExtentSnow{45.0f, 45.0f, 30.0f};
constexpr PrimaryHalfExtent kPrimaryHalfExtentSand{20.0f, 20.0f, 12.5f};

const PrimaryHalfExtent& HalfExtentFor(const world::WeatherKind kind) {
  switch (kind) {
    case world::WeatherKind::kRain:
      return kPrimaryHalfExtentRain;
    case world::WeatherKind::kSnow:
      return kPrimaryHalfExtentSnow;
    case world::WeatherKind::kSandstorm:
    case world::WeatherKind::kNone:
    default:
      return kPrimaryHalfExtentSand;
  }
}

constexpr float kPrimaryFallSpeedRain = -25.0f;
constexpr float kPrimaryFallSpeedSnow = -3.0f;
constexpr float kPrimaryLifetimeRain = 3.0f;
constexpr float kPrimaryLifetimeSnow = 10.0f;
constexpr float kPrimaryLifetimeSand = 4.0f;

constexpr float kMistHalfExtentXY = 44.0f;
constexpr float kMistHalfExtentZ = 25.0f;
constexpr float kMistBillboardSize = 12.0f;
constexpr float kMistBillboardHalfSize = kMistBillboardSize * 0.5f;
constexpr std::size_t kMistCapacity = 128u;

constexpr float kMistFadeDuration = 0.4f;

float ClampedRatio(const float numerator, const float denominator) {
  if (!(denominator > 0.0f)) {
    return 1.0f;
  }
  return std::clamp(numerator / denominator, 0.0f, 1.0f);
}

float SpawnRate(const world::WeatherState& weather,
                const float particle_density_scale,
                const bool use_weather_shaders) {
  if (weather.indoors || weather.kind == world::WeatherKind::kNone) {
    return 0.0f;
  }
  const float intensity =
      std::max(0.0f, (weather.density - 0.25f) / 0.75f);
  const float maximum =
      weather.kind == world::WeatherKind::kRain
          ? (use_weather_shaders ? 35000.0f : 6500.0f)
          : weather.kind == world::WeatherKind::kSnow
                ? (use_weather_shaders ? 14000.0f : 1300.0f)
                : (use_weather_shaders ? 32000.0f : 6000.0f);
  return maximum * std::max(0.0f, particle_density_scale) * intensity;
}

constexpr float kNearFadeBias = 1.5f;
constexpr float kNearFadeInverseRange = 1.0f / 12.0f;

constexpr float kGroundProfileStepLimitFirst = 0.5f;
constexpr float kGroundProfileStepLimitSecond = 0.75f;
constexpr float kGroundProfileStepLimitThird = 1.0f;

constexpr float kGroundProfileSampleSpacing = 533.33333f / 16.0f / 8.0f;

constexpr float kNoGroundSample = -std::numeric_limits<float>::max();

constexpr float kGroundContactAccelerationStep = 1.6666666f;
constexpr float kGroundContactHalfSprite = kMistBillboardHalfSize;
constexpr float kGroundContactMaximumLift = kMistBillboardHalfSize * 0.5f;

struct WeatherVertex {
  RenderVec3 position{};
  std::uint32_t color{};
  std::array<float, 2> uv{};
};

float NearFadeAlpha(const RenderVec3& corner, const RenderVec3& camera) {
  const float dx = corner[0] - camera[0];
  const float dy = corner[1] - camera[1];
  const float dz = corner[2] - camera[2];
  const float value =
      kNearFadeBias - std::sqrt(dx * dx + dy * dy + dz * dz) * kNearFadeInverseRange;
  if (value < 0.0f) {
    return 1.0f;
  }
  return value < 1.0f ? 1.0f - value : 0.0f;
}

void AppendBillboard(const RenderVec3& center, const RenderVec3& right,
                     const RenderVec3& up, const RenderVec3& camera,
                     const float half_size, const std::uint32_t color,
                     const bool apply_near_fade,
                     std::vector<WeatherVertex>& vertices) {
  const auto point = [&](const float x, const float y) {
    return RenderVec3{center[0] + right[0] * x + up[0] * y,
                      center[1] + right[1] * x + up[1] * y,
                      center[2] + right[2] * x + up[2] * y};
  };
  const float base_alpha = static_cast<float>((color >> 24u) & 0xffu);
  const auto corner = [&](const float x, const float y,
                          const std::array<float, 2>& uv) {
    const auto position = point(x, y);
    const float near_fade = apply_near_fade ? NearFadeAlpha(position, camera) : 1.0f;
    const auto alpha = static_cast<std::uint32_t>(
        std::clamp(base_alpha * near_fade, 0.0f, 255.0f));
    return WeatherVertex{position, (color & 0x00ffffffu) | (alpha << 24u), uv};
  };
  const std::array<WeatherVertex, 4> corners{{
      corner(-half_size, -half_size, {0.0f, 0.0f}),
      corner(half_size, -half_size, {1.0f, 0.0f}),
      corner(half_size, half_size, {1.0f, 1.0f}),
      corner(-half_size, half_size, {0.0f, 1.0f}),
  }};
  for (const std::uint8_t index : {0, 1, 2, 2, 3, 0}) {
    vertices.push_back(corners[index]);
  }
}

}

WeatherRenderer::WeatherRenderer(TextureManager& texture_manager)
    : texture_manager_(texture_manager) {}

WeatherRenderer::~WeatherRenderer() {
  Shutdown();
}

void WeatherRenderer::SetGroundHeightSampler(GroundHeightSampler sampler) {
  ground_height_sampler_ = std::move(sampler);
}

bool WeatherRenderer::Initialize() {
  if (initialized_) {
    return true;
  }
  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();
  program_ =
      CreateEmbeddedProgram(ShaderProgramId::Weather, bgfx::getRendererType());
  sampler_ = bgfx::createUniform("s_weatherTex", bgfx::UniformType::Sampler);

  primary_vertices_ = bgfx::createDynamicVertexBuffer(
      static_cast<std::uint32_t>(kMaximumPrimaryParticles * 6u), layout_,
      BGFX_BUFFER_ALLOW_RESIZE);

  mist_vertices_ = bgfx::createDynamicVertexBuffer(
      static_cast<std::uint32_t>(kMistCapacity * 6u), layout_,
      BGFX_BUFFER_ALLOW_RESIZE);
  initialized_ = bgfx::isValid(program_) && bgfx::isValid(sampler_) &&
                 bgfx::isValid(primary_vertices_) && bgfx::isValid(mist_vertices_);
  if (!initialized_) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "Weather renderer initialization failed");
    Shutdown();
    return false;
  }

  primary_texture_ = texture_manager_.GetWhiteTexture();
  mist_texture_ = texture_manager_.GetWhiteTexture();
  return true;
}

void WeatherRenderer::RefreshPrimaryTexture(const world::WeatherState& weather) {
  if (weather.texture == primary_bound_texture_path_) {
    return;
  }
  primary_bound_texture_path_ = weather.texture;
  if (primary_bound_texture_path_.empty()) {

    primary_texture_lease_ = {};
    primary_texture_ = texture_manager_.GetWhiteTexture();
    return;
  }
  primary_texture_lease_ = texture_manager_.AcquireTextureAsync(
      primary_bound_texture_path_, TextureLoadFailurePolicy::kCheckerPlaceholder,
      TextureLoadPriority::kDemand);
  primary_texture_ = primary_texture_lease_
                         ? BgfxTextureLeaseAccess::Get(primary_texture_lease_)
                         : texture_manager_.GetWhiteTexture();
}

void WeatherRenderer::RefreshMistTexture(const world::WeatherKind kind) {
  if (kind == mist_bound_kind_) {
    return;
  }
  mist_bound_kind_ = kind;
  if (kind == world::WeatherKind::kNone) {
    mist_texture_lease_ = {};
    mist_texture_ = texture_manager_.GetWhiteTexture();
    return;
  }

  const char* path = kind == world::WeatherKind::kSandstorm
                          ? "textures\\Weather\\WeatherMistGrainy01.blp"
                          : "textures\\Weather\\SnowMist01.blp";
  mist_texture_lease_ = texture_manager_.AcquireTextureAsync(
      path, TextureLoadFailurePolicy::kCheckerPlaceholder,
      TextureLoadPriority::kDemand);
  mist_texture_ = mist_texture_lease_ ? BgfxTextureLeaseAccess::Get(mist_texture_lease_)
                                     : texture_manager_.GetWhiteTexture();
}

void WeatherRenderer::SpawnPrimary(const float dt, const RenderVec3& camera,
                                   const world::WeatherState& weather,
                                   const float particle_density_scale,
                                   const bool use_weather_shaders) {
  primary_spawn_credit_ += SpawnRate(weather, particle_density_scale,
                                     use_weather_shaders) * dt;
  const auto count = std::min<std::size_t>(
      static_cast<std::size_t>(primary_spawn_credit_),
      kMaximumPrimaryParticles - std::min(kMaximumPrimaryParticles, primary_particles_.size()));
  primary_spawn_credit_ -= static_cast<float>(count);
  if (count == 0u) {
    return;
  }

  const auto& half_extent = HalfExtentFor(weather.kind);
  std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
  primary_particles_.reserve(
      std::min(kMaximumPrimaryParticles, primary_particles_.size() + count));
  for (std::size_t index = 0; index < count; ++index) {
    PrimaryParticle particle{};
    particle.position = {camera[0] + unit(random_) * half_extent.x,
                         camera[1] + unit(random_) * half_extent.y,
                         camera[2] + unit(random_) * half_extent.z};
    if (weather.kind == world::WeatherKind::kRain) {
      particle.velocity = {weather.velocity[0], weather.velocity[1], kPrimaryFallSpeedRain};
      particle.lifetime = kPrimaryLifetimeRain;
    } else if (weather.kind == world::WeatherKind::kSnow) {
      particle.velocity = {weather.velocity[0] * 0.1f + unit(random_),
                           weather.velocity[1] * 0.1f + unit(random_), kPrimaryFallSpeedSnow};
      particle.lifetime = kPrimaryLifetimeSnow;
    } else {
      particle.velocity = {weather.velocity[0] + 12.0f,
                           weather.velocity[1] + unit(random_) * 2.0f,
                           unit(random_)};
      particle.lifetime = kPrimaryLifetimeSand;
    }
    primary_particles_.push_back(particle);
  }
}

void WeatherRenderer::SpawnMist(const float dt, const RenderVec3& camera,
                                const world::WeatherState& weather,
                                const float particle_density_scale,
                                const bool use_weather_shaders) {

  constexpr float kMistSpawnRateScale = 1.0f / 200.0f;
  mist_spawn_credit_ += SpawnRate(weather, particle_density_scale, use_weather_shaders) *
                       kMistSpawnRateScale * dt;
  const auto count = std::min<std::size_t>(
      static_cast<std::size_t>(mist_spawn_credit_),
      kMistCapacity - std::min(kMistCapacity, mist_particles_.size()));
  mist_spawn_credit_ -= static_cast<float>(count);
  if (count == 0u) {
    return;
  }

  std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
  mist_particles_.reserve(std::min(kMistCapacity, mist_particles_.size() + count));
  for (std::size_t index = 0; index < count; ++index) {
    MistParticle particle{};
    particle.position = {camera[0] + unit(random_) * kMistHalfExtentXY,
                         camera[1] + unit(random_) * kMistHalfExtentXY,
                         camera[2] - kMistHalfExtentZ + unit(random_) * (kMistHalfExtentZ * 0.25f)};

    particle.velocity = {weather.velocity[0] * 0.3f, weather.velocity[1] * 0.3f, -0.2f};

    particle.lifetime = kMistFadeDuration * 12.0f;
    if (!BuildGroundProfile(particle)) {
      continue;
    }
    mist_particles_.push_back(particle);
  }
}

bool WeatherRenderer::BuildGroundProfile(MistParticle& particle) const {
  particle.ground_profile_samples = 0u;
  if (!ground_height_sampler_) {

    return true;
  }

  const float horizontal_speed = std::sqrt(particle.velocity[0] * particle.velocity[0] +
                                           particle.velocity[1] * particle.velocity[1]);
  if (!(horizontal_speed > 0.0f) || !(particle.lifetime > 0.0f)) {

    return true;
  }
  const float step_x = (particle.velocity[0] / horizontal_speed) * kGroundProfileSampleSpacing;
  const float step_y = (particle.velocity[1] / horizontal_speed) * kGroundProfileSampleSpacing;
  const float step_length = std::sqrt(step_x * step_x + step_y * step_y);
  if (!(step_length > 0.0f)) {
    return true;
  }
  const auto sample_count = static_cast<std::size_t>(
      std::clamp((horizontal_speed * particle.lifetime) / step_length, 0.0f,
                 static_cast<float>(kMaximumGroundProfileSamples)));
  if (sample_count == 0u) {
    return true;
  }

  float cursor_x = particle.position[0];
  float cursor_y = particle.position[1];
  for (std::size_t index = 0; index < sample_count; ++index) {
    const auto height =
        ground_height_sampler_(cursor_x, cursor_y, particle.position[2]);
    particle.ground_profile[index] = height.value_or(kNoGroundSample);
    cursor_x += step_x;
    cursor_y += step_y;
  }

  particle.ground_profile_samples = static_cast<std::uint8_t>(sample_count);

  std::size_t blocking_index = 0;
  bool blocked = false;
  while (blocking_index + 3u < sample_count) {
    const float base = particle.ground_profile[blocking_index];
    if (particle.ground_profile[blocking_index + 3u] - base > kGroundProfileStepLimitThird ||
        particle.ground_profile[blocking_index + 2u] - base > kGroundProfileStepLimitSecond ||
        particle.ground_profile[blocking_index + 1u] - base > kGroundProfileStepLimitFirst) {
      blocked = true;
      break;
    }
    ++blocking_index;
  }
  if (!blocked) {
    return true;
  }

  particle.lifetime *= static_cast<float>(blocking_index + 1u) /
                       static_cast<float>(sample_count);
  particle.ground_profile_samples = static_cast<std::uint8_t>(blocking_index);

  return blocking_index != 0u;
}

float WeatherRenderer::SampleGroundProfile(const MistParticle& particle) const {
  const std::size_t count = particle.ground_profile_samples;
  if (count == 0u || !(particle.lifetime > 0.0f)) {
    return kNoGroundSample;
  }
  const float scaled =
      static_cast<float>(count) * std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
  const auto low = static_cast<std::size_t>(scaled);
  if (low >= count) {
    return particle.ground_profile[count - 1u];
  }
  const std::size_t high = std::min(low + 1u, count - 1u);

  if (particle.ground_profile[low] == kNoGroundSample ||
      particle.ground_profile[high] == kNoGroundSample) {
    return kNoGroundSample;
  }
  const float fraction = scaled - static_cast<float>(low);
  return particle.ground_profile[low] +
         (particle.ground_profile[high] - particle.ground_profile[low]) * fraction;
}

void WeatherRenderer::Update(const float dt, const RenderVec3& camera,
                             const world::WeatherState& weather,
                             const float particle_density_scale,
                             const bool use_weather_shaders) {
  if (weather.kind != kind_) {
    Reset();
    kind_ = weather.kind;
  }
  RefreshPrimaryTexture(weather);
  RefreshMistTexture(weather.kind);

  const float step = std::max(0.0f, dt);

  for (auto& particle : primary_particles_) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      particle.position[axis] += particle.velocity[axis] * step;
    }
    particle.age += step;
  }
  std::erase_if(primary_particles_, [](const PrimaryParticle& particle) {
    return particle.age >= particle.lifetime;
  });
  SpawnPrimary(step, camera, weather, particle_density_scale, use_weather_shaders);

  const float half_step_squared = 0.5f * step * step;
  for (auto& particle : mist_particles_) {

    const float acceleration_term = particle.acceleration * half_step_squared;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      particle.position[axis] += particle.velocity[axis] * step + acceleration_term;
    }
    particle.age += step;

    const float ground = SampleGroundProfile(particle) + kGroundContactHalfSprite;
    if (ground > particle.position[2]) {
      particle.acceleration += kGroundContactAccelerationStep;
      particle.position[2] +=
          std::min(ground - particle.position[2], kGroundContactMaximumLift);
    }
  }
  std::erase_if(mist_particles_, [](const MistParticle& particle) {
    return particle.age >= particle.lifetime;
  });
  SpawnMist(step, camera, weather, particle_density_scale, use_weather_shaders);
}

void WeatherRenderer::Render(const std::uint8_t view_id,
                             const RenderMatrix4x4& view,
                             const RenderMatrix4x4& projection,
                             const RenderVec3& camera,
                             const world::WeatherState& weather,
                             const RenderVec4& fog_color,
                             bgfx::Encoder* const encoder) {
  if (!initialized_ || weather.kind != kind_) {
    return;
  }
  const RenderVec3 right{view[0], view[4], view[8]};
  const RenderVec3 up{view[1], view[5], view[9]};

  const DrawEncoder draw{encoder};

  bgfx::setViewTransform(view_id, view.data(), projection.data());

  if (!primary_particles_.empty()) {
    std::vector<WeatherVertex> output;
    output.reserve(primary_particles_.size() * 6u);

    for (const auto& particle : primary_particles_) {
      const std::uint32_t color = weather.color_abgr;
      AppendBillboard(particle.position, right, up, camera, kPrimaryBillboardHalfSize,
                      color, false, output);
    }
    bgfx::update(primary_vertices_, 0,
                bgfx::copy(output.data(), static_cast<std::uint32_t>(
                                              output.size() * sizeof(WeatherVertex))));

    draw.setVertexBuffer(0, primary_vertices_, 0,
                         static_cast<std::uint32_t>(output.size()));
    draw.setTexture(0, sampler_, primary_texture_);
    draw.setState(
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_DEPTH_TEST_LESS |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                              BGFX_STATE_BLEND_INV_SRC_ALPHA) |
        BGFX_STATE_MSAA);
    draw.submit(view_id, program_);
  }

  if (!mist_particles_.empty()) {
    std::vector<WeatherVertex> output;
    output.reserve(mist_particles_.size() * 6u);

    const std::uint32_t fog_rgb =
        (static_cast<std::uint32_t>(std::clamp(fog_color[2], 0.0f, 1.0f) * 255.0f) << 16u) |
        (static_cast<std::uint32_t>(std::clamp(fog_color[1], 0.0f, 1.0f) * 255.0f) << 8u) |
        static_cast<std::uint32_t>(std::clamp(fog_color[0], 0.0f, 1.0f) * 255.0f);
    for (const auto& particle : mist_particles_) {
      const float remaining = particle.lifetime - particle.age;
      const float birth_fade = ClampedRatio(particle.age, kMistFadeDuration);
      const float death_fade = ClampedRatio(remaining, kMistFadeDuration);
      const auto alpha = static_cast<std::uint32_t>(
          std::clamp(birth_fade * death_fade, 0.0f, 1.0f) * 255.0f);
      const std::uint32_t color = fog_rgb | (alpha << 24u);
      AppendBillboard(particle.position, right, up, camera, kMistBillboardHalfSize,
                      color, true, output);
    }
    bgfx::update(mist_vertices_, 0,
                bgfx::copy(output.data(), static_cast<std::uint32_t>(
                                              output.size() * sizeof(WeatherVertex))));

    draw.setVertexBuffer(0, mist_vertices_, 0,
                         static_cast<std::uint32_t>(output.size()));
    draw.setTexture(0, sampler_, mist_texture_);
    draw.setState(
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_DEPTH_TEST_LESS |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                              BGFX_STATE_BLEND_INV_SRC_ALPHA) |
        BGFX_STATE_MSAA);
    draw.submit(view_id, program_);
  }
}

void WeatherRenderer::Reset() {
  kind_ = world::WeatherKind::kNone;
  primary_spawn_credit_ = 0.0f;
  primary_particles_.clear();
  mist_spawn_credit_ = 0.0f;
  mist_particles_.clear();
}

void WeatherRenderer::Shutdown() {
  Reset();
  primary_texture_lease_ = {};
  primary_texture_ = BGFX_INVALID_HANDLE;
  primary_bound_texture_path_.clear();
  mist_texture_lease_ = {};
  mist_texture_ = BGFX_INVALID_HANDLE;
  mist_bound_kind_ = world::WeatherKind::kNone;
  if (bgfx::isValid(primary_vertices_)) {
    bgfx::destroy(primary_vertices_);
  }
  if (bgfx::isValid(mist_vertices_)) {
    bgfx::destroy(mist_vertices_);
  }
  if (bgfx::isValid(sampler_)) {
    bgfx::destroy(sampler_);
  }
  if (bgfx::isValid(program_)) {
    bgfx::destroy(program_);
  }
  primary_vertices_ = BGFX_INVALID_HANDLE;
  mist_vertices_ = BGFX_INVALID_HANDLE;
  sampler_ = BGFX_INVALID_HANDLE;
  program_ = BGFX_INVALID_HANDLE;
  initialized_ = false;
}

}
