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

// The original particle manager stores weather particles in linked packets.
// 6144 is the capacity of one packet, not a global weather-system limit.
constexpr std::size_t kPrimaryPacketCapacity = 6144u;
constexpr std::size_t kMaximumActivePrimaryPackets = 32u;
constexpr std::size_t kMaximumPrimaryParticles =
    kPrimaryPacketCapacity * kMaximumActivePrimaryPackets;
constexpr std::size_t kMaximumActiveRainSplashPackets = 4u;
constexpr std::size_t kMaximumRainSplashes =
    kPrimaryPacketCapacity * kMaximumActiveRainSplashPackets;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kPointFallbackHalfSize = 1.0f / 12.0f;
constexpr float kRainDropHalfWidth = 0.05f;
constexpr float kRainDropTrailLength = 2.0f;
constexpr float kRainSplashLifetime = 0.25f;
constexpr float kSnowTerminalFade = 0.25f;
constexpr float kPlayerVelocitySpawnOffset = 1.75f;
constexpr float kMaximumParticleDistance = 200.0f;

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

constexpr float kMistExtentXY = 44.0f;
constexpr float kMistExtentZ = 25.0f;
constexpr float kMistBillboardSize = 12.0f;
constexpr float kMistBillboardHalfSize = kMistBillboardSize * 0.5f;
constexpr std::size_t kMistCapacity = 128u;

constexpr float kMistFadeDuration = 0.4f;
constexpr float kMistDirection = -1.57f;
constexpr float kMistDirectionRange = 0.34906584f;
constexpr float kMistVerticalSpeed = 1.0f / 3.0f;
constexpr float kMistVerticalSpeedRange = 1.0f / 30.0f;
constexpr float kMistSourceBackTime = 1.5f;
constexpr float kMistLifetime = 2.7f;
constexpr float kMistLifetimeRange = 0.3f;
constexpr float kMistAccelerationRange = 10.0f / 3.0f;

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

float MistSpawnRate(const world::WeatherState& weather,
                    const float particle_density_scale,
                    const bool use_weather_shaders) {
  if (weather.indoors || weather.kind == world::WeatherKind::kNone) {
    return 0.0f;
  }
  const float density =
      std::clamp((weather.density - 0.25f) / 0.75f, 0.0f, 1.0f);
  float rate = 0.0f;
  switch (weather.kind) {
    case world::WeatherKind::kRain:
      rate = (use_weather_shaders ? 38.0f : 18.0f) *
             std::max(0.0f, (density - 0.5f) * 2.0f);
      break;
    case world::WeatherKind::kSnow:
      rate = (use_weather_shaders ? 48.0f : 24.0f) *
             std::max(0.0f, (density - 0.5f) * 2.0f);
      break;
    case world::WeatherKind::kSandstorm:
      rate = (use_weather_shaders ? 64.0f : 32.0f) * density;
      break;
    case world::WeatherKind::kNone:
      break;
  }
  return rate * std::max(0.0f, particle_density_scale);
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

RenderVec3 Add(const RenderVec3& first, const RenderVec3& second) {
  return {first[0] + second[0], first[1] + second[1],
          first[2] + second[2]};
}

RenderVec3 Subtract(const RenderVec3& first, const RenderVec3& second) {
  return {first[0] - second[0], first[1] - second[1],
          first[2] - second[2]};
}

RenderVec3 Scale(const RenderVec3& value, const float scalar) {
  return {value[0] * scalar, value[1] * scalar, value[2] * scalar};
}

float Dot(const RenderVec3& first, const RenderVec3& second) {
  return first[0] * second[0] + first[1] * second[1] +
         first[2] * second[2];
}

RenderVec3 Cross(const RenderVec3& first, const RenderVec3& second) {
  return {first[1] * second[2] - first[2] * second[1],
          first[2] * second[0] - first[0] * second[2],
          first[0] * second[1] - first[1] * second[0]};
}

float Length(const RenderVec3& value) {
  return std::sqrt(Dot(value, value));
}

RenderVec3 NormalizeOr(const RenderVec3& value, const RenderVec3& fallback) {
  const float length = Length(value);
  return length > 1.0e-6f ? Scale(value, 1.0f / length) : fallback;
}

RenderVec3 RotateHorizontal(const RenderVec3& value, const float facing) {
  const float cosine = std::cos(facing);
  const float sine = std::sin(facing);
  return {value[0] * cosine - value[1] * sine,
          value[0] * sine + value[1] * cosine, value[2]};
}

std::uint32_t ScaleAlpha(const std::uint32_t color, const float scale) {
  const auto alpha = static_cast<std::uint32_t>(std::clamp(
      static_cast<float>((color >> 24u) & 0xffu) * scale, 0.0f, 255.0f));
  return (color & 0x00ffffffu) | (alpha << 24u);
}

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

void AppendRainDrop(const RenderVec3& center, const RenderVec3& velocity,
                    const RenderVec3& camera, const RenderVec3& fallback_right,
                    const std::uint32_t color,
                    std::vector<WeatherVertex>& vertices) {
  const RenderVec3 trail = Scale(
      NormalizeOr(velocity, {0.0f, 0.0f, -1.0f}), -1.0f);
  const RenderVec3 to_camera =
      NormalizeOr(Subtract(camera, center), {0.0f, 0.0f, 1.0f});
  const RenderVec3 side = NormalizeOr(Cross(to_camera, trail), fallback_right);
  vertices.push_back(
      {Add(center, Scale(side, -kRainDropHalfWidth)), color, {0.0f, 1.0f}});
  vertices.push_back(
      {Add(center, Scale(side, kRainDropHalfWidth)), color, {1.0f, 1.0f}});
  vertices.push_back(
      {Add(center, Scale(trail, kRainDropTrailLength)), color, {0.5f, 0.0f}});
}

void AppendRainSplash(const RenderVec3& center, const RenderVec3& right,
                      const RenderVec3& up,
                      const RenderVec3& camera, const float age,
                      const std::uint32_t color,
                      std::vector<WeatherVertex>& vertices) {
  const float frame = std::min(3.0f, std::floor(age * 4.0f * 3.99f));
  const float uv_u = frame * 0.25f;
  const RenderVec3 to_camera = Subtract(camera, center);
  const float distance = Length(to_camera);
  const float view_elevation =
      distance > 1.0e-6f ? std::max(0.0f, to_camera[2] / distance) : 1.0f;
  const float row = std::floor((1.0f - view_elevation) * 3.99f);
  const float uv_v = row * 0.25f;
  vertices.push_back({Add(center, Scale(right, -1.0f / 12.0f)), color,
                      {uv_u, uv_v + 0.25f}});
  vertices.push_back({Add(center, Scale(up, 1.0f / 6.0f)), color,
                      {uv_u + 0.125f, uv_v + 0.04296875f}});
  vertices.push_back({Add(center, Scale(right, 1.0f / 12.0f)), color,
                      {uv_u + 0.25f, uv_v + 0.25f}});
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

void WeatherRenderer::SetCollisionSampler(CollisionSampler sampler) {
  collision_sampler_ = std::move(sampler);
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
  weather_params_ =
      bgfx::createUniform("u_weatherParams", bgfx::UniformType::Vec4);

  primary_vertices_ = bgfx::createDynamicVertexBuffer(
      static_cast<std::uint32_t>(kPrimaryPacketCapacity * 6u), layout_,
      BGFX_BUFFER_ALLOW_RESIZE);

  rain_splash_vertices_ = bgfx::createDynamicVertexBuffer(
      static_cast<std::uint32_t>(kPrimaryPacketCapacity * 3u), layout_,
      BGFX_BUFFER_ALLOW_RESIZE);

  mist_vertices_ = bgfx::createDynamicVertexBuffer(
      static_cast<std::uint32_t>(kMistCapacity * 6u), layout_,
      BGFX_BUFFER_ALLOW_RESIZE);
  initialized_ = bgfx::isValid(program_) && bgfx::isValid(sampler_) &&
                 bgfx::isValid(weather_params_) &&
                 bgfx::isValid(primary_vertices_) &&
                 bgfx::isValid(rain_splash_vertices_) &&
                 bgfx::isValid(mist_vertices_);
  if (!initialized_) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "Weather renderer initialization failed");
    Shutdown();
    return false;
  }

  return true;
}

void WeatherRenderer::RefreshPrimaryTexture(const world::WeatherState& weather) {
  if (weather.texture != primary_bound_texture_path_) {
    primary_bound_texture_path_ = weather.texture;
    primary_texture_lease_ = {};
    if (!primary_bound_texture_path_.empty()) {
      static_cast<void>(texture_manager_.QueueTextureLoad(
          primary_bound_texture_path_, TextureLoadFailurePolicy::kStrict,
          TextureLoadPriority::kDemand));
    }
  }
  if (primary_bound_texture_path_.empty() || primary_texture_lease_.valid()) {
    return;
  }
  primary_texture_lease_ =
      texture_manager_.AcquireCachedTextureStrict(primary_bound_texture_path_);
}

void WeatherRenderer::RefreshMistTexture(const world::WeatherKind kind) {
  if (kind != mist_bound_kind_) {
    mist_bound_kind_ = kind;
    mist_texture_lease_ = {};
    if (kind != world::WeatherKind::kNone) {
      const char* path = kind == world::WeatherKind::kSandstorm
                             ? "textures\\Weather\\WeatherMistGrainy01.blp"
                             : "textures\\Weather\\SnowMist01.blp";
      static_cast<void>(texture_manager_.QueueTextureLoad(
          path, TextureLoadFailurePolicy::kStrict,
          TextureLoadPriority::kDemand));
    }
  }
  if (kind == world::WeatherKind::kNone || mist_texture_lease_.valid()) {
    return;
  }
  const char* path = kind == world::WeatherKind::kSandstorm
                         ? "textures\\Weather\\WeatherMistGrainy01.blp"
                         : "textures\\Weather\\SnowMist01.blp";
  mist_texture_lease_ = texture_manager_.AcquireCachedTextureStrict(path);
}

void WeatherRenderer::RefreshRainSplashTexture(
    const world::WeatherKind kind) {
  if (kind != world::WeatherKind::kRain) {
    rain_splash_texture_lease_ = {};
    return;
  }
  constexpr const char* kRainSplashTexture =
      "textures\\Weather\\RainDropSplash01.blp";
  if (!rain_splash_texture_lease_.valid()) {
    rain_splash_texture_lease_ =
        texture_manager_.AcquireCachedTextureStrict(kRainSplashTexture);
  }
  if (!rain_splash_texture_lease_.valid()) {
    static_cast<void>(texture_manager_.QueueTextureLoad(
        kRainSplashTexture, TextureLoadFailurePolicy::kStrict,
        TextureLoadPriority::kDemand));
  }
}

void WeatherRenderer::SpawnPrimary(const float dt, const RenderVec3& camera,
                                   const world::WeatherState& weather,
                                   const float particle_density_scale,
                                   const bool use_weather_shaders) {
  const float rate =
      SpawnRate(weather, particle_density_scale, use_weather_shaders);
  primary_spawn_credit_ =
      rate > 0.0f
          ? std::min(static_cast<float>(kMaximumPrimaryParticles),
                     primary_spawn_credit_ + rate * dt)
          : 0.0f;
  const auto count = std::min<std::size_t>(
      static_cast<std::size_t>(primary_spawn_credit_),
      kMaximumPrimaryParticles - std::min(kMaximumPrimaryParticles, primary_particles_.size()));
  primary_spawn_credit_ -= static_cast<float>(count);
  if (count == 0u) {
    return;
  }

  const auto& half_extent = HalfExtentFor(weather.kind);
  const float density =
      std::clamp((weather.density - 0.25f) / 0.75f, 0.0f, 1.0f);
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);
  primary_particles_.reserve(
      std::min(kMaximumPrimaryParticles, primary_particles_.size() + count));
  for (std::size_t index = 0; index < count; ++index) {
    PrimaryParticle particle{};
    RenderVec3 local{};
    float fallback_lifetime = 0.0f;
    if (weather.kind == world::WeatherKind::kRain) {
      local = {(unit(random_) - 0.5f) * half_extent.x * 2.0f,
               (unit(random_) - 0.5f) * half_extent.y * 2.0f,
               half_extent.z};
      const float angle =
          weather.facing + (unit(random_) - 0.5f) *
                               (density * 0.20943952f + 0.05235988f);
      const float horizontal_speed =
          density * 9.49f + 0.01f +
          (unit(random_) - 0.5f) * density * 2.0f;
      particle.velocity = {std::cos(angle) * horizontal_speed,
                           std::sin(angle) * horizontal_speed,
                           -28.0f - density * 4.0f -
                               unit(random_) * density * 2.0f};
      fallback_lifetime =
          (half_extent.z * 2.0f) / -particle.velocity[2];
    } else if (weather.kind == world::WeatherKind::kSnow) {
      local = {(unit(random_) - 0.5f) * half_extent.x * 2.0f,
               (unit(random_) - 0.5f) * half_extent.y * 2.0f,
               half_extent.z};
      const float angle =
          weather.facing + (unit(random_) - 0.5f) *
                               (2.0f * kPi - density * 5.9341197f);
      const float horizontal_speed =
          density * 5.985f + 0.015f +
          (unit(random_) - 0.5f) * density;
      particle.velocity = {std::cos(angle) * horizontal_speed,
                           std::sin(angle) * horizontal_speed,
                           -2.0f - density * 3.5f -
                               unit(random_) * density};
      fallback_lifetime =
          (half_extent.z * 2.0f) / -particle.velocity[2];
    } else {
      local = {half_extent.x * 2.0f * (0.85f + unit(random_) * 0.15f),
               (unit(random_) - 0.5f) * half_extent.y * 2.0f,
               (unit(random_) - 0.5f) * half_extent.z * 2.0f};
      const float angle =
          weather.facing + kPi +
          (unit(random_) - 0.5f) * 0.34906584f;
      const float horizontal_speed =
          18.666666f + (unit(random_) - 0.5f) * 0.6666667f;
      particle.velocity = {std::cos(angle) * horizontal_speed,
                           std::sin(angle) * horizontal_speed,
                           0.8333333f +
                               (unit(random_) - 0.5f) * 0.16666667f};
      fallback_lifetime = 3.2f + (unit(random_) - 0.5f) * 0.3f;
    }

    local = RotateHorizontal(local, weather.facing);
    const RenderVec3 motion_offset{
        weather.velocity[0] * kPlayerVelocitySpawnOffset,
        weather.velocity[1] * kPlayerVelocitySpawnOffset,
        weather.velocity[2] * kPlayerVelocitySpawnOffset};
    const RenderVec3 origin = Add(Add(camera, local), motion_offset);
    const float speed = Length(particle.velocity);
    particle.motion_lifetime = fallback_lifetime;
    if (collision_sampler_ && speed > 1.0e-6f) {
      const RenderVec3 direction = Scale(particle.velocity, 1.0f / speed);
      if (const auto collision = collision_sampler_(
              origin, direction, kMaximumParticleDistance)) {
        particle.motion_lifetime = collision->distance / speed;
        particle.collision_position = collision->position;
        particle.collision_normal = collision->normal;
        particle.has_collision = true;
      }
    }
    if (!particle.has_collision) {
      particle.collision_position =
          Add(origin, Scale(particle.velocity, particle.motion_lifetime));
    }
    const float pre_age = unit(random_) * dt;
    if (!(particle.motion_lifetime > pre_age)) {
      continue;
    }
    particle.age = pre_age;
    particle.lifetime =
        particle.motion_lifetime +
        (weather.kind == world::WeatherKind::kSnow ? kSnowTerminalFade : 0.0f);
    particle.position = Add(origin, Scale(particle.velocity, pre_age));
    primary_particles_.push_back(particle);
  }
}

void WeatherRenderer::SpawnMist(const float dt, const RenderVec3& camera,
                                const world::WeatherState& weather,
                                const float particle_density_scale,
                                const bool use_weather_shaders) {
  const float rate =
      MistSpawnRate(weather, particle_density_scale, use_weather_shaders);
  mist_spawn_credit_ =
      rate > 0.0f
          ? std::min(static_cast<float>(kMistCapacity),
                     mist_spawn_credit_ + rate * dt)
          : 0.0f;
  const auto count = std::min<std::size_t>(
      static_cast<std::size_t>(mist_spawn_credit_),
      kMistCapacity - std::min(kMistCapacity, mist_particles_.size()));
  mist_spawn_credit_ -= static_cast<float>(count);
  if (count == 0u) {
    return;
  }

  std::uniform_real_distribution<float> unit(0.0f, 1.0f);
  mist_particles_.reserve(std::min(kMistCapacity, mist_particles_.size() + count));
  for (std::size_t index = 0; index < count; ++index) {
    MistParticle particle{};
    float speed = 0.0f;
    float speed_range = 0.0f;
    switch (weather.kind) {
      case world::WeatherKind::kRain:
        speed = 5.0f;
        speed_range = 1.2f;
        break;
      case world::WeatherKind::kSnow:
        speed = 9.0f;
        speed_range = 3.0f;
        break;
      case world::WeatherKind::kSandstorm:
        speed = 15.0f;
        speed_range = 4.5f;
        break;
      case world::WeatherKind::kNone:
        continue;
    }
    const float angle = weather.facing + kMistDirection +
                        (unit(random_) - 0.5f) * kMistDirectionRange;
    speed += (unit(random_) - 0.5f) * speed_range;
    particle.velocity = {
        std::cos(angle) * speed, std::sin(angle) * speed,
        kMistVerticalSpeed +
            (unit(random_) - 0.5f) * kMistVerticalSpeedRange};

    RenderVec3 local =
        RotateHorizontal({(unit(random_) - 0.5f) * kMistExtentXY,
                          (unit(random_) - 0.5f) * kMistExtentXY,
                          (unit(random_) - 0.5f) * kMistExtentZ},
                         weather.facing);
    particle.position = Add(Add(camera, local),
                            Scale(particle.velocity, -kMistSourceBackTime));
    if (ground_height_sampler_) {
      const auto ground = ground_height_sampler_(
          particle.position[0], particle.position[1],
          particle.position[2] + 100.0f);
      if (!ground) {
        continue;
      }
      particle.position[2] =
          std::max(particle.position[2], *ground) + kMistBillboardHalfSize;
    } else {
      particle.position[2] += kMistBillboardHalfSize;
    }
    particle.acceleration =
        (unit(random_) - 0.5f) * kMistAccelerationRange;
    particle.lifetime =
        kMistLifetime + (unit(random_) - 0.5f) * kMistLifetimeRange;
    const float pre_age = unit(random_) * dt;
    if (!(particle.lifetime > pre_age)) {
      continue;
    }
    particle.age = pre_age;
    const float acceleration_term =
        0.5f * particle.acceleration * pre_age * pre_age;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      particle.position[axis] +=
          particle.velocity[axis] * pre_age + acceleration_term;
    }
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

void WeatherRenderer::AdvancePrimary(const float dt, const RenderVec3& camera) {
  std::size_t retained = 0u;
  for (PrimaryParticle particle : primary_particles_) {
    const float previous_age = particle.age;
    particle.age += dt;
    const float motion_step =
        std::max(0.0f, std::min(particle.age, particle.motion_lifetime) -
                           std::min(previous_age, particle.motion_lifetime));
    particle.position = Add(particle.position,
                            Scale(particle.velocity, motion_step));
    if (kind_ == world::WeatherKind::kRain &&
        previous_age < particle.motion_lifetime &&
        particle.age >= particle.motion_lifetime && particle.has_collision &&
        rain_splash_particles_.size() < kMaximumRainSplashes) {
      rain_splash_particles_.push_back({
          .position = Add(
              particle.collision_position,
              Scale(NormalizeOr(particle.collision_normal,
                                {0.0f, 0.0f, 1.0f}),
                    0.015f)),
          .age = 0.0f});
    }
    if (particle.age >= particle.lifetime ||
        Length(Subtract(particle.position, camera)) >
            kMaximumParticleDistance) {
      continue;
    }
    primary_particles_[retained++] = particle;
  }
  primary_particles_.resize(retained);

  for (auto& splash : rain_splash_particles_) {
    splash.age += dt;
  }
  std::erase_if(rain_splash_particles_, [&](const RainSplashParticle& splash) {
    return splash.age >= kRainSplashLifetime ||
           Length(Subtract(splash.position, camera)) >
               kMaximumParticleDistance;
  });
}

void WeatherRenderer::ActivateWeather(const world::WeatherState& weather) {
  Reset();
  kind_ = weather.kind;
  active_color_abgr_ = weather.color_abgr;
  RefreshPrimaryTexture(weather);
  RefreshMistTexture(weather.kind);
  RefreshRainSplashTexture(weather.kind);
}

void WeatherRenderer::Update(const float dt, const RenderVec3& camera,
                             const world::WeatherState& weather,
                             const float particle_density_scale,
                             const bool use_weather_shaders) {
  if (weather.kind != kind_) {
    if (!weather.smooth_transition || kind_ == world::WeatherKind::kNone ||
        (primary_particles_.empty() && rain_splash_particles_.empty() &&
         mist_particles_.empty())) {
      ActivateWeather(weather);
    } else {
      retiring_ = true;
      primary_spawn_credit_ = 0.0f;
      mist_spawn_credit_ = 0.0f;
    }
  } else {
    retiring_ = false;
    active_color_abgr_ = weather.color_abgr;
    RefreshPrimaryTexture(weather);
    RefreshMistTexture(weather.kind);
    RefreshRainSplashTexture(weather.kind);
  }

  const float step = std::max(0.0f, dt);
  AdvancePrimary(step, camera);

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

  if (retiring_ && primary_particles_.empty() &&
      rain_splash_particles_.empty() && mist_particles_.empty()) {
    ActivateWeather(weather);
  }
  if (!retiring_) {
    SpawnPrimary(step, camera, weather, particle_density_scale,
                 use_weather_shaders);
    SpawnMist(step, camera, weather, particle_density_scale,
              use_weather_shaders);
  }
}

void WeatherRenderer::Render(const std::uint8_t view_id,
                             const RenderMatrix4x4& view,
                             const RenderMatrix4x4& projection,
                             const RenderVec3& camera,
                             const RenderVec4& fog_color,
                             bgfx::Encoder* const encoder) {
  if (!initialized_ || kind_ == world::WeatherKind::kNone) {
    return;
  }
  const RenderVec3 right{view[0], view[4], view[8]};
  const RenderVec3 up{view[1], view[5], view[9]};

  const DrawEncoder draw{encoder};

  bgfx::setViewTransform(view_id, view.data(), projection.data());

  const bgfx::TextureHandle primary_texture =
      kind_ == world::WeatherKind::kSandstorm
          ? texture_manager_.GetWhiteTexture()
          : primary_texture_lease_.valid()
                ? BgfxTextureLeaseAccess::Get(primary_texture_lease_)
                : bgfx::TextureHandle{bgfx::kInvalidHandle};
  if (!primary_particles_.empty() && bgfx::isValid(primary_texture)) {
    std::vector<WeatherVertex> output;
    output.reserve(primary_particles_.size() * 6u);

    for (const auto& particle : primary_particles_) {
      if (kind_ == world::WeatherKind::kRain) {
        AppendRainDrop(particle.position, particle.velocity, camera, right,
                       ScaleAlpha(active_color_abgr_, 0.5f), output);
      } else {
        float opacity = 1.0f;
        if (kind_ == world::WeatherKind::kSnow) {
          opacity = std::clamp(particle.age, 0.0f, 1.0f) *
                    std::clamp((particle.lifetime - particle.age) * 4.0f,
                               0.0f, 1.0f);
        } else {
          opacity = std::clamp(particle.age * 5.0f, 0.0f, 1.0f) *
                    std::clamp((particle.lifetime - particle.age) * 5.0f,
                               0.0f, 1.0f);
        }
        AppendBillboard(particle.position, right, up, camera,
                        kPointFallbackHalfSize,
                        ScaleAlpha(active_color_abgr_, opacity), false,
                        output);
      }
    }
    bgfx::update(primary_vertices_, 0,
                bgfx::copy(output.data(), static_cast<std::uint32_t>(
                                              output.size() * sizeof(WeatherVertex))));

    draw.setVertexBuffer(0, primary_vertices_, 0,
                         static_cast<std::uint32_t>(output.size()));
    const std::array<float, 4> weather_params{
        kind_ == world::WeatherKind::kSandstorm ? 1.0f : 0.0f,
        0.0f, 0.0f, 0.0f};
    draw.setUniform(weather_params_, weather_params.data());
    draw.setTexture(0, sampler_, primary_texture);
    draw.setState(
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_DEPTH_TEST_LESS |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                              BGFX_STATE_BLEND_INV_SRC_ALPHA) |
        BGFX_STATE_MSAA);
    draw.submit(view_id, program_);
  }

  const bgfx::TextureHandle rain_splash_texture =
      rain_splash_texture_lease_.valid()
          ? BgfxTextureLeaseAccess::Get(rain_splash_texture_lease_)
          : bgfx::TextureHandle{bgfx::kInvalidHandle};
  if (!rain_splash_particles_.empty() &&
      bgfx::isValid(rain_splash_texture)) {
    std::vector<WeatherVertex> output;
    output.reserve(rain_splash_particles_.size() * 3u);
    for (const auto& splash : rain_splash_particles_) {
      AppendRainSplash(splash.position, right, up, camera, splash.age,
                       0x80808080u, output);
    }
    bgfx::update(rain_splash_vertices_, 0,
                 bgfx::copy(output.data(), static_cast<std::uint32_t>(
                                               output.size() * sizeof(WeatherVertex))));
    draw.setVertexBuffer(0, rain_splash_vertices_, 0,
                         static_cast<std::uint32_t>(output.size()));
    constexpr std::array<float, 4> kTexturedWeatherParams{};
    draw.setUniform(weather_params_, kTexturedWeatherParams.data());
    draw.setTexture(0, sampler_, rain_splash_texture);
    draw.setState(
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_DEPTH_TEST_LESS |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                              BGFX_STATE_BLEND_INV_SRC_ALPHA) |
        BGFX_STATE_MSAA);
    draw.submit(view_id, program_);
  }

  const bgfx::TextureHandle mist_texture =
      mist_texture_lease_.valid()
          ? BgfxTextureLeaseAccess::Get(mist_texture_lease_)
          : bgfx::TextureHandle{bgfx::kInvalidHandle};
  if (!mist_particles_.empty() && bgfx::isValid(mist_texture)) {
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
    constexpr std::array<float, 4> kTexturedWeatherParams{};
    draw.setUniform(weather_params_, kTexturedWeatherParams.data());
    draw.setTexture(0, sampler_, mist_texture);
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
  active_color_abgr_ = 0xffffffffu;
  retiring_ = false;
  primary_spawn_credit_ = 0.0f;
  primary_particles_.clear();
  rain_splash_particles_.clear();
  mist_spawn_credit_ = 0.0f;
  mist_particles_.clear();
}

void WeatherRenderer::Shutdown() {
  Reset();
  primary_texture_lease_ = {};
  primary_bound_texture_path_.clear();
  rain_splash_texture_lease_ = {};
  mist_texture_lease_ = {};
  mist_bound_kind_ = world::WeatherKind::kNone;
  if (bgfx::isValid(primary_vertices_)) {
    bgfx::destroy(primary_vertices_);
  }
  if (bgfx::isValid(rain_splash_vertices_)) {
    bgfx::destroy(rain_splash_vertices_);
  }
  if (bgfx::isValid(mist_vertices_)) {
    bgfx::destroy(mist_vertices_);
  }
  if (bgfx::isValid(sampler_)) {
    bgfx::destroy(sampler_);
  }
  if (bgfx::isValid(weather_params_)) {
    bgfx::destroy(weather_params_);
  }
  if (bgfx::isValid(program_)) {
    bgfx::destroy(program_);
  }
  primary_vertices_ = BGFX_INVALID_HANDLE;
  rain_splash_vertices_ = BGFX_INVALID_HANDLE;
  mist_vertices_ = BGFX_INVALID_HANDLE;
  sampler_ = BGFX_INVALID_HANDLE;
  weather_params_ = BGFX_INVALID_HANDLE;
  program_ = BGFX_INVALID_HANDLE;
  initialized_ = false;
}

}
