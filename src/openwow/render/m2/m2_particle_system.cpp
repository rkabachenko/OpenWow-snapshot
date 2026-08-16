#include "openwow/render/m2/m2_particle_system.h"

#include "openwow/render/m2/m2_animator.h"
#include "openwow/render/m2/m2_shaders.h"
#include "openwow/render/resources/shaders/shader_registry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace openwow::render::m2 {

bgfx::VertexLayout ParticleVertex::layout;

void ParticleVertex::InitLayout() {
  layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();
}

namespace {

constexpr float kMsToSec = 1.0f / 1000.0f;
constexpr float kSecondsToMs = 1000.0f;
constexpr float kVectorEpsilon = 1.0e-6f;
constexpr float kTau = 6.28318530717958647692f;
constexpr float kM2ParticleCapacityHeadroom = 1.15f;
constexpr float kM2ParticleTailScreenLengthSquaredMin = 0.00077160494f;
constexpr float kM2ParticleUpdateSubstepSeconds = 0.1f;
constexpr float kM2ParticleFollowSpeedEpsilon = 2.3841858e-07f;

constexpr float kM2ParticleTrailFixedTimestepSec = 0.033333335f;
constexpr float kM2ParticleSplineParameterChangeEpsilon = 2.3841858e-07f;

constexpr float kM2ParticleEmitterPlaneBasisEpsilon = 2.3841858e-07f;
constexpr float kM2ParticleTumbleAngularSpeedEpsilon = 1.0e-4f;
constexpr std::uint32_t kM2ParticleTwinkleTableSize = 128u;
constexpr std::uint32_t kM2ParticleTwinkleTableMask = kM2ParticleTwinkleTableSize - 1u;
constexpr std::uint32_t kM2ParticleTwinklePointerEntropyShift = 5u;
constexpr auto kM2ParticleU16TrackNormDenominator = static_cast<std::uint16_t>(32767u);
constexpr std::uint8_t kM2ParticleForcedAdditiveSortBlendIndex = 4u;
constexpr std::uint8_t kM2ParticleEmitterTypePlane = 1u;
constexpr std::uint8_t kM2ParticleEmitterTypeSphere = 2u;
constexpr std::uint8_t kM2ParticleEmitterTypeSpline = 3u;
constexpr std::uint32_t kM2ParticleRandomPackedByteMask = 0xffu;
constexpr std::uint32_t kM2ParticleRandomPackedByteBits = 8u;
constexpr std::uint32_t kM2ParticleRandomTableWordByteShift = 2u;
constexpr float kM2ParticleVelocityZeroEpsilon = 1.0e-8f;

constexpr float kM2ParticleDensityFalloffNearDistance = 50.0f;
constexpr float kM2ParticleDensityFalloffSlope = 0.02f;
constexpr float kM2ParticleDensityFalloffMin = 0.25f;

[[nodiscard]] float ResolveM2ParticleDistanceFalloff(const float distance) noexcept {
  const float linear = 1.0f - (distance - kM2ParticleDensityFalloffNearDistance) *
                                  kM2ParticleDensityFalloffSlope;
  return std::clamp(linear, kM2ParticleDensityFalloffMin, 1.0f);
}
constexpr bx::Vec3 kM2ParticleLocalUp{0.0f, 0.0f, 1.0f};

struct M2ParticleRandomLaneConfig {
  std::uint32_t seed_modulus;
  std::uint32_t seed_shift;
  std::uint32_t subtract_step;
  std::uint32_t wrap_add;
  int rotate_left_bits;
};

constexpr std::array<M2ParticleRandomLaneConfig, 4> kM2ParticleRandomLaneConfig{{
    {0x3du, 2u, 0x1cu, 0xd8u, 0},
    {0x3bu, 10u, 0x18u, 0xd4u, 3},
    {0x35u, 18u, 0x0cu, 0xc8u, 2},
    {0x2fu, 26u, 0x04u, 0xb8u, 1},
}};

constexpr std::array<std::uint32_t, 64> kM2ParticleRandomTable{
    0x9927148eu, 0x08c7aafdu, 0x1f3ee6d5u, 0xda55bbf6u, 0x6a4aa075u,
    0xff97bde8u, 0x9fbc9bdeu, 0x46a18a81u, 0x63e30b6eu, 0x5d6c7a76u,
    0xca69d388u, 0x25b947c3u, 0x3fa2ab83u, 0xba7c41a6u, 0x0195ace5u,
    0xc109cf7eu, 0x717062d9u, 0x0205db8du, 0x54ef8724u, 0x3037d4c6u,
    0x7bcb1bd0u, 0xecd8e4b8u, 0xdcadce49u, 0xc494a913u, 0x0dae398fu,
    0x0edd5218u, 0x85f5fa78u, 0x6dafd258u, 0x3b53b2a4u, 0xbe50a551u,
    0x11f42dfcu, 0xf1169848u, 0x663ddf86u, 0x2f2e445eu, 0x176b0736u,
    0xb64c298bu, 0xe75f89e2u, 0xe121a7cdu, 0xed65c94du, 0x239ceefeu,
    0x04b77d33u, 0x402a9a9eu, 0xf35b10b3u, 0x921c7782u, 0x571e4e20u,
    0x8c067222u, 0xfb732c67u, 0xbf0ac259u, 0x0cf95c79u, 0x68121a28u,
    0x42193474u, 0xf884c0b1u, 0x9d15f038u, 0x6f3af260u, 0x91eb90b4u,
    0x61357f1du, 0x5603325au, 0x932bc5a3u, 0x434b0f80u, 0x3ce0a8f7u,
    0x2664d196u, 0x4fcc45d7u, 0xb5e9b0c8u, 0xea31d600u,
};

float M2RandomMantissaToUnitFloat(const std::uint32_t value) noexcept {
  return std::bit_cast<float>((value & 0x007fffffu) | 0x3f800000u) - 1.0f;
}

float M2RandomMantissaToSignedUnitFloat(const std::uint32_t value) noexcept {
  const float sample = std::bit_cast<float>((value & 0x007fffffu) | 0x3f800000u);
  return (value & 0x80000000u) != 0u ? 2.0f - sample : sample - 2.0f;
}

std::uint32_t M2ParticleRandomTableWord(const std::uint32_t byte_offset) noexcept {
  return kM2ParticleRandomTable[byte_offset >> kM2ParticleRandomTableWordByteShift];
}

std::uint32_t AdvanceM2ParticleRandomLane(
    const std::uint32_t byte_offset,
    const M2ParticleRandomLaneConfig &config) noexcept {
  return byte_offset >= config.subtract_step ? byte_offset - config.subtract_step
                                             : byte_offset + config.wrap_add;
}

std::uint32_t PackM2ParticleABGR(const float r, const float g, const float b, const float a) {
  const auto round_byte = [](const float value) -> std::uint8_t {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
  };
  const std::uint8_t alpha = round_byte(std::clamp(a, 0.0f, 1.0f) * 255.0f);
  return (static_cast<std::uint32_t>(alpha) << 24u) |
         (static_cast<std::uint32_t>(round_byte(b)) << 16u) |
         (static_cast<std::uint32_t>(round_byte(g)) << 8u) |
         static_cast<std::uint32_t>(round_byte(r));
}

struct ParticleSortEntry {
  std::uint32_t index = 0;
  float depth = 0.0f;
};

void PushParticleQuad(std::vector<ParticleVertex> *vertices, const std::array<bx::Vec3, 4> &corners,
                      const std::uint32_t color, const float u0, const float v0, const float u1,
                      const float v1) {
  auto push = [&](const bx::Vec3 &pos, const float u, const float v) {
    vertices->push_back({pos.x, pos.y, pos.z, color, u, v});
  };

  push(corners[0], u0, v0);
  push(corners[1], u1, v0);
  push(corners[2], u1, v1);

  push(corners[0], u0, v0);
  push(corners[2], u1, v1);
  push(corners[3], u0, v1);
}

struct M2ParticleQuadCornerSign {
  float sx;
  float sy;
};
constexpr std::array<M2ParticleQuadCornerSign, 4> kM2ParticleQuadCornerSigns{{
    {-1.0f, +1.0f},
    {+1.0f, +1.0f},
    {+1.0f, -1.0f},
    {-1.0f, -1.0f},
}};

std::array<bx::Vec3, 4> BuildM2ParticleQuadCorners(const bx::Vec3 &center,
                                                   const bx::Vec3 &right_scaled,
                                                   const bx::Vec3 &up_scaled) noexcept {
  const auto corner = [&](const M2ParticleQuadCornerSign &sign) {
    return bx::add(center, bx::add(bx::mul(right_scaled, sign.sx), bx::mul(up_scaled, sign.sy)));
  };
  return {{
      corner(kM2ParticleQuadCornerSigns[0]),
      corner(kM2ParticleQuadCornerSigns[1]),
      corner(kM2ParticleQuadCornerSigns[2]),
      corner(kM2ParticleQuadCornerSigns[3]),
  }};
}

using M2ParticleAxisAngleMatrix = std::array<float, 9>;

M2ParticleAxisAngleMatrix BuildM2ParticleAxisAngleMatrix(const bx::Vec3 &unit_axis,
                                                         const float angle) noexcept {
  const float nx = unit_axis.x;
  const float ny = unit_axis.y;
  const float nz = unit_axis.z;
  const float s = std::sin(angle);
  const float c = std::cos(angle);
  const float one_minus_c = 1.0f - c;
  const float xz = nx * nz * one_minus_c;
  const float xy = nx * ny * one_minus_c;
  const float yz = nz * ny * one_minus_c;
  return {
      nx * nx * one_minus_c + c, s * nz + xy, xz - s * ny,
      xy - s * nz, ny * ny * one_minus_c + c, nx * s + yz,
      s * ny + xz, yz - nx * s, nz * nz * one_minus_c + c,
  };
}

bx::Vec3 ApplyM2ParticleAxisAngleMatrix(const M2ParticleAxisAngleMatrix &m,
                                        const bx::Vec3 &v) noexcept {
  return bx::Vec3{
      v.x * m[0] + v.y * m[1] + v.z * m[2],
      v.x * m[3] + v.y * m[4] + v.z * m[5],
      v.x * m[6] + v.y * m[7] + v.z * m[8],
  };
}

bool ShouldBatchWith(const M2ParticleDrawRange &range, const M2ParticleDrawInput &input,
                     const bool batch_particles, const std::uint8_t sort_blend_index,
                     const std::uint32_t flag_sort_key) {
  if (!batch_particles) {
    return false;
  }
  if (range.first_emitter + range.emitter_count != input.emitter_index) {
    return false;
  }
  if (range.first_vertex + range.vertex_count != input.first_vertex) {
    return false;
  }
  return range.texture_index == input.texture_index && range.blending_type == input.blending_type &&
         range.flag_sort_key == flag_sort_key && range.sort_blend_index == sort_blend_index;
}

[[nodiscard]] RenderMatrix4x4View ResolveM2ParticleBoneMatrix(
    const std::vector<float> &bone_mats, const std::size_t bone_count,
    const std::size_t bone_index) noexcept {
  constexpr std::size_t kM2ParticleBoneMatrixFloatCount = 16u;
  if (bone_index >= bone_count ||
      bone_index > std::numeric_limits<std::size_t>::max() / kM2ParticleBoneMatrixFloatCount) {
    return RenderMatrix4x4View{kRenderIdentityMatrix4x4};
  }

  const std::size_t offset = bone_index * kM2ParticleBoneMatrixFloatCount;
  if (offset > bone_mats.size() ||
      bone_mats.size() - offset < kM2ParticleBoneMatrixFloatCount) {
    return RenderMatrix4x4View{kRenderIdentityMatrix4x4};
  }

  return RenderMatrix4x4View{bone_mats.data() + offset, kM2ParticleBoneMatrixFloatCount};
}

bx::Vec3 TransformPoint(const RenderMatrix4x4View mtx, const bx::Vec3 &p) {
  return bx::Vec3{
      mtx[0] * p.x + mtx[4] * p.y + mtx[8] * p.z + mtx[12],
      mtx[1] * p.x + mtx[5] * p.y + mtx[9] * p.z + mtx[13],
      mtx[2] * p.x + mtx[6] * p.y + mtx[10] * p.z + mtx[14],
  };
}

bx::Vec3 TransformDir(const RenderMatrix4x4View mtx, const bx::Vec3 &d) {
  return bx::Vec3{
      mtx[0] * d.x + mtx[4] * d.y + mtx[8] * d.z,
      mtx[1] * d.x + mtx[5] * d.y + mtx[9] * d.z,
      mtx[2] * d.x + mtx[6] * d.y + mtx[10] * d.z,
  };
}

[[nodiscard]] RenderMatrix4x4 ComposeM2ParticleMatrix(
    const RenderMatrix4x4View outer, const RenderMatrix4x4View inner) noexcept {
  RenderMatrix4x4 result{};
  for (std::size_t column = 0; column < 4u; ++column) {
    for (std::size_t row = 0; row < 4u; ++row) {
      float sum = 0.0f;
      for (std::size_t k = 0; k < 4u; ++k) {
        sum += outer[k * 4u + row] * inner[column * 4u + k];
      }
      result[column * 4u + row] = sum;
    }
  }
  return result;
}

constexpr RenderMatrix4x4 kM2ParticleEmitterFrameRotation{
    0.0f,  1.0f, 0.0f, 0.0f,
    -1.0f, 0.0f, 0.0f, 0.0f,
    0.0f,  0.0f, 1.0f, 0.0f,
    0.0f,  0.0f, 0.0f, 1.0f,
};

bx::Vec3 NormalizeM2ParticleVector(const bx::Vec3 &value) noexcept {
  const float length = bx::length(value);
  if (!(length > kVectorEpsilon)) {
    return {0.0f, 0.0f, 0.0f};
  }
  return bx::mul(value, 1.0f / length);
}

openwow::render::C3Vector ToM2ParticleSplinePoint(
    const openwow::data::model::M2Vec3 &point) noexcept {
  return {point.x, point.y, point.z};
}

bx::Vec3 ToBxVec3(const openwow::render::C3Vector &point) noexcept {
  return {point.x, point.y, point.z};
}

openwow::data::model::M2Vec2 LerpM2ParticleLifetimeValue(
    const openwow::data::model::M2Vec2 &from, const openwow::data::model::M2Vec2 &to,
    const float t) noexcept {
  return {
      from.x + t * (to.x - from.x),
      from.y + t * (to.y - from.y),
  };
}

openwow::data::model::M2Vec3 LerpM2ParticleLifetimeValue(
    const openwow::data::model::M2Vec3 &from, const openwow::data::model::M2Vec3 &to,
    const float t) noexcept {
  return {
      from.x + t * (to.x - from.x),
      from.y + t * (to.y - from.y),
      from.z + t * (to.z - from.z),
  };
}

std::uint16_t LerpM2ParticleLifetimeValue(const std::uint16_t from, const std::uint16_t to,
                                          const float t) noexcept {
  const float value = static_cast<float>(from) + t * (static_cast<float>(to) - from);
  if (!(value > 0.0f)) {
    return 0u;
  }
  constexpr float kMaxU16 = static_cast<float>(std::numeric_limits<std::uint16_t>::max());
  if (value >= kMaxU16) {
    return std::numeric_limits<std::uint16_t>::max();
  }
  return static_cast<std::uint16_t>(std::lround(value));
}

template <typename T>
T SampleM2ParticleLifetimeTrackValue(
    const openwow::data::model::M2ParticleLifetimeTrack<T> &track, const float lifetime_fraction,
    const T &default_value) {
  if (track.values.empty()) {
    return default_value;
  }

  if (track.values.size() == 1u || track.times.empty()) {
    return track.values.front();
  }

  const std::size_t sample_count = std::min(track.times.size(), track.values.size());
  if (sample_count < 2u) {
    return track.values.front();
  }

  const float sample_time = std::clamp(lifetime_fraction, 0.0f, 1.0f);
  if (sample_time <= 0.0f) {
    return track.values.front();
  }
  if (sample_time >= 1.0f) {
    return track.values[sample_count - 1u];
  }

  if (sample_count == 2u) {
    return LerpM2ParticleLifetimeValue(track.values[0], track.values[1], sample_time);
  }
  if (sample_count == 3u) {
    constexpr float kTimestampDenominator = 32767.0f;
    const float middle = static_cast<float>(track.times[1]) / kTimestampDenominator;
    if (sample_time < middle) {
      const float factor = middle > 0.0f ? sample_time / middle : 0.0f;
      return LerpM2ParticleLifetimeValue(track.values[0], track.values[1], factor);
    }
    const float remaining = 1.0f - middle;
    const float factor = remaining > 0.0f ? (sample_time - middle) / remaining : 1.0f;
    return LerpM2ParticleLifetimeValue(track.values[1], track.values[2], factor);
  }

  constexpr float kTimestampDenominator = 32767.0f;
  for (std::size_t index = 0; index + 1u < sample_count; ++index) {
    const float t0 = static_cast<float>(track.times[index]) / kTimestampDenominator;
    const float t1 = static_cast<float>(track.times[index + 1u]) / kTimestampDenominator;
    if (sample_time >= t0 && sample_time < t1) {
      const float range = t1 - t0;
      if (range <= 0.0f) {
        return track.values[index];
      }
      return LerpM2ParticleLifetimeValue(track.values[index], track.values[index + 1u],
                                         (sample_time - t0) / range);
    }
  }

  return track.values[sample_count - 1u];
}

float ClampM2ParticleSplineParameter(const float value) noexcept {
  if (!(value >= 0.0f)) {
    return 0.0f;
  }
  return std::min(value, 1.0f);
}

float ResolveM2ParticleSplineParameter(const float sampled_start, const float sampled_end,
                                       M2EmitterState *state,
                                       M2ParticleRandomState *random) noexcept {
  const float start = ClampM2ParticleSplineParameter(sampled_start);
  const float end = ClampM2ParticleSplineParameter(sampled_end);
  if (state != nullptr) {
    if (!state->spline_path_end_initialized) {
      state->spline_path_end_initialized = true;
      state->spline_path_end = end;
    } else if (std::abs(end - state->spline_path_end) >=
               kM2ParticleSplineParameterChangeEpsilon) {
      state->spline_path_end = end;
      state->spline_path_end_dirty = true;
    }

    if (state->spline_path_end_dirty) {
      state->spline_path_end_dirty = false;
      return end;
    }
  }

  const float k = random != nullptr ? random->NextUnitFloat() : 0.0f;
  return start + (end - start) * k;
}

bx::Vec3 RotateM2ParticleVectorAroundAxis(const bx::Vec3 &value, const bx::Vec3 &axis,
                                          const float angle) noexcept {
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  return bx::add(
      bx::add(bx::mul(value, c), bx::mul(bx::cross(axis, value), s)),
      bx::mul(axis, bx::dot(axis, value) * (1.0f - c)));
}

bool BuildM2ParticleSplineSpawn(const openwow::render::CSpline &path, const float path_start,
                                const float path_end, const float z_source,
                                const float emission_speed, const float speed_variation,
                                const float vertical_range, const float horizontal_range,
                                M2EmitterState *state,
                                M2ParticleRandomState *random, bx::Vec3 *local_position,
                                bx::Vec3 *local_velocity) {
  if (local_position == nullptr || local_velocity == nullptr) {
    return false;
  }

  const float t = ResolveM2ParticleSplineParameter(path_start, path_end, state, random);
  openwow::render::C3Vector path_position{};
  if (!path.EvaluateInto(t, path_position, openwow::render::CSpline::kArcLengthParameterMode)) {
    return false;
  }

  bx::Vec3 position = ToBxVec3(path_position);
  const float actual_speed =
      emission_speed * (1.0f + (random != nullptr ? random->NextSignedRange(speed_variation)
                                                   : 0.0f));
  bx::Vec3 direction{0.0f, 0.0f, 0.0f};
  if (z_source != 0.0f) {
    direction = NormalizeM2ParticleVector({position.x, position.y, position.z - z_source});
    if (bx::length(direction) <= kVectorEpsilon) {
      return false;
    }
  } else if (vertical_range != 0.0f) {
    openwow::render::C3Vector path_tangent{};
    if (!path.EvaluateTangentInto(t, path_tangent,
                                  openwow::render::CSpline::kArcLengthParameterMode)) {
      return false;
    }
    const bx::Vec3 tangent = NormalizeM2ParticleVector(ToBxVec3(path_tangent));
    if (bx::length(tangent) <= kVectorEpsilon) {
      return false;
    }
    const float angle = random != nullptr ? random->NextSignedRange(vertical_range) : 0.0f;
    direction = NormalizeM2ParticleVector(
        RotateM2ParticleVectorAroundAxis(kM2ParticleLocalUp, tangent, angle));
    if (bx::length(direction) <= kVectorEpsilon) {
      return false;
    }
    if (horizontal_range != 0.0f) {
      const float offset = (random != nullptr ? random->NextUnitFloat() : 0.0f) * horizontal_range;
      position = bx::add(position, bx::mul(direction, offset));
    }
  } else {
    direction = kM2ParticleLocalUp;
  }

  *local_position = position;
  *local_velocity = bx::mul(direction, actual_speed);
  return true;
}

[[nodiscard]] float ClearNearZeroParticleVelocityComponent(const float value) noexcept {
  if (value != 0.0f && std::abs(value) < kM2ParticleVelocityZeroEpsilon) {
    return 0.0f;
  }
  return value;
}

void ClearNearZeroParticleVelocity(M2Particle *particle) noexcept {
  if (particle == nullptr) {
    return;
  }
  particle->velocity = {
      ClearNearZeroParticleVelocityComponent(particle->velocity.x),
      ClearNearZeroParticleVelocityComponent(particle->velocity.y),
      ClearNearZeroParticleVelocityComponent(particle->velocity.z),
  };
}

std::pair<float, float>
ResolveM2ParticleFollowScaleLine(const openwow::data::model::M2ParticleEmitter &def) noexcept {
  const float speed_delta = def.follow_speed2 - def.follow_speed1;
  if (std::abs(speed_delta) < kM2ParticleFollowSpeedEpsilon) {
    return {0.0f, 0.0f};
  }
  const float slope = (def.follow_scale2 - def.follow_scale1) / speed_delta;
  const float intercept = def.follow_scale1 - def.follow_speed1 * slope;
  return {slope, intercept};
}

float ClampM2ParticleFollowScale(const float scale) noexcept {
  if (!(scale > 0.0f)) {
    return 0.0f;
  }
  return std::min(scale, 1.0f);
}

void ShiftM2ParticleEmitterPositionHistory(const bx::Vec3 &current_position,
                                           M2EmitterState *state) noexcept {
  if (state == nullptr) {
    return;
  }
  state->trail_previous_position = state->trail_current_position;
  state->trail_current_position = current_position;
}

bx::Vec3 ComputeM2ParticleFollowDisplacement(const openwow::data::model::M2ParticleEmitter &def,
                                             const M2EmitterState &state,
                                             const float dt_sec) noexcept {
  if ((def.flags & kM2ParticleFlagFollowEmitterMotion) == 0u ||
      !(dt_sec >= kM2ParticleFollowSpeedEpsilon)) {
    return {0.0f, 0.0f, 0.0f};
  }

  const bx::Vec3 displacement =
      bx::sub(state.trail_current_position, state.trail_previous_position);
  const float speed = bx::length(displacement) / dt_sec;
  const auto [slope, intercept] = ResolveM2ParticleFollowScaleLine(def);
  const float scale = ClampM2ParticleFollowScale(speed * slope + intercept);
  return bx::mul(displacement, scale);
}

void UpdateM2ParticleTrailState(const openwow::data::model::M2ParticleEmitter &def,
                                M2EmitterState *state, const float dt_sec) noexcept {
  if (state == nullptr) {
    return;
  }

  if ((def.flags & kM2ParticleFlagPositionDeltaTrail) == 0u) {
    return;
  }

  state->trail_delta_accumulator_sec += dt_sec;
  if (state->trail_delta_accumulator_sec <= kM2ParticleTrailFixedTimestepSec) {
    return;
  }
  const float elapsed_sec = state->trail_delta_accumulator_sec;
  state->trail_delta_accumulator_sec = 0.0f;

  if (!state->trail_has_history) {
    state->trail_delta = {0.0f, 0.0f, 0.0f};
    state->trail_has_history = true;
    return;
  }

  const bx::Vec3 raw_delta =
      bx::sub(state->trail_current_position, state->trail_previous_position);
  const float normalize_scale =
      (kM2ParticleTrailFixedTimestepSec / elapsed_sec) * def.position_delta_multiplier;
  state->trail_delta = bx::mul(raw_delta, normalize_scale);
}

std::optional<float> ResolveM2ParticleTwinkleScale(
    const openwow::data::model::M2ParticleEmitter &def, const float age_seconds,
    const std::uintptr_t particle_address) noexcept {
  const float scale_range = def.twinkle_scale_max - def.twinkle_scale_min;
  std::uint32_t sample_index = 0u;
  if ((def.twinkle_percent <= 1.0f && def.twinkle_percent != 1.0f) || scale_range != 0.0f) {
    sample_index =
        ResolveM2ParticleTwinkleSampleIndex(age_seconds, def.twinkle_speed, particle_address);
  }
  const float sample = SampleM2ParticleTwinkleTable(sample_index);

  if (def.twinkle_percent < 1.0f && def.twinkle_percent < sample) {
    return std::nullopt;
  }

  return def.twinkle_scale_min + scale_range * sample;
}

bool HasM2ParticleTumble(const openwow::data::model::M2ParticleEmitter &def) noexcept {
  return def.tumble_min[0] != 0.0f || def.tumble_min[1] != 0.0f ||
         def.tumble_min[2] != 0.0f || def.tumble_max[0] != 0.0f ||
         def.tumble_max[1] != 0.0f || def.tumble_max[2] != 0.0f;
}

float SampleM2ParticleTumbleComponent(const float min_value, const float max_value,
                                      M2ParticleRandomState *random) noexcept {
  if (random == nullptr) {
    return min_value;
  }
  return min_value + random->NextUnitFloat() * (max_value - min_value);
}

void ApplyM2ParticleTumbleRandomSigns(const openwow::data::model::M2ParticleEmitter &def,
                                      M2Particle *particle,
                                      M2ParticleRandomState *random) noexcept {
  if (particle == nullptr || random == nullptr ||
      (def.flags & kM2ParticleFlagRandomTumbleAxesSign) == 0u) {
    return;
  }

  if ((random->NextU32() & 1u) == 0u) {
    particle->angular_velocity.x = -particle->angular_velocity.x;
  }
  if ((random->NextU32() & 1u) == 0u) {
    particle->angular_velocity.y = -particle->angular_velocity.y;
  }
  if ((random->NextU32() & 1u) == 0u) {
    particle->angular_velocity.z = -particle->angular_velocity.z;
  }
}

void InitM2ParticleTumble(const openwow::data::model::M2ParticleEmitter &def,
                          M2Particle *particle, M2ParticleRandomState *random) noexcept {
  if (particle == nullptr || !HasM2ParticleTumble(def)) {
    return;
  }

  particle->angular_velocity = {
      SampleM2ParticleTumbleComponent(def.tumble_min[0], def.tumble_max[0], random),
      SampleM2ParticleTumbleComponent(def.tumble_min[1], def.tumble_max[1], random),
      SampleM2ParticleTumbleComponent(def.tumble_min[2], def.tumble_max[2], random),
  };
  ApplyM2ParticleTumbleRandomSigns(def, particle, random);
}

bx::Quaternion MultiplyM2ParticleOrientation(const bx::Quaternion &lhs,
                                             const bx::Quaternion &rhs) noexcept {
  return {
      lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
      lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
      lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
      lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
  };
}

void AdvanceM2ParticleTumbleOrientation(M2Particle *particle, const float dt_sec) noexcept {
  if (particle == nullptr || !(dt_sec > 0.0f)) {
    return;
  }

  const bx::Vec3 &w = particle->angular_velocity;
  const float angular_speed = bx::length(w);
  if (!(angular_speed > kM2ParticleTumbleAngularSpeedEpsilon)) {
    return;
  }

  const float half_angle = angular_speed * dt_sec * 0.5f;
  const float axis_scale = std::sin(half_angle) / angular_speed;
  const bx::Quaternion delta{
      w.x * axis_scale,
      w.y * axis_scale,
      w.z * axis_scale,
      std::cos(half_angle),
  };
  particle->orientation = MultiplyM2ParticleOrientation(particle->orientation, delta);
}

bx::Vec3 RotateM2ParticleVector(const bx::Vec3 &v, const bx::Quaternion &q) noexcept {
  const bx::Vec3 qv{q.x, q.y, q.z};
  const bx::Vec3 t = bx::mul(bx::cross(qv, v), 2.0f);
  return bx::add(v, bx::add(bx::mul(t, q.w), bx::cross(qv, t)));
}

std::uint32_t ResolveM2ParticleSubstepCount(const float dt_sec, const float lifespan_seconds) {
  if (!(dt_sec > kM2ParticleUpdateSubstepSeconds)) {
    return 0u;
  }
  float count = std::floor(dt_sec / kM2ParticleUpdateSubstepSeconds);
  if (lifespan_seconds > 0.0f && std::isfinite(lifespan_seconds)) {
    count = std::min(count, std::floor(lifespan_seconds / kM2ParticleUpdateSubstepSeconds));
  }
  if (!(count > 0.0f) || !std::isfinite(count)) {
    return 0u;
  }
  if (count >= static_cast<float>(std::numeric_limits<std::uint32_t>::max())) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(count);
}

}

void M2ParticleRandomState::Seed(const std::uint32_t seed) noexcept {
  value_ = seed;
  packed_indices_ = 0u;
  for (const auto &config : kM2ParticleRandomLaneConfig) {
    packed_indices_ |= (seed % config.seed_modulus) << config.seed_shift;
  }
}

std::uint32_t M2ParticleRandomState::NextU32() noexcept {
  std::array<std::uint32_t, 4> next_offsets{};
  std::uint32_t mixed = 0u;
  for (std::size_t lane = 0; lane < kM2ParticleRandomLaneConfig.size(); ++lane) {
    const auto &config = kM2ParticleRandomLaneConfig[lane];
    const std::uint32_t offset =
        (packed_indices_ >> (lane * kM2ParticleRandomPackedByteBits)) &
        kM2ParticleRandomPackedByteMask;
    const std::uint32_t next_offset = AdvanceM2ParticleRandomLane(offset, config);
    std::uint32_t word = M2ParticleRandomTableWord(next_offset);
    if (config.rotate_left_bits != 0) {
      word = std::rotl(word, config.rotate_left_bits);
    }
    mixed ^= word;
    next_offsets[lane] = next_offset;
  }

  value_ += mixed;
  packed_indices_ = next_offsets[0] | (next_offsets[1] << kM2ParticleRandomPackedByteBits) |
                    (next_offsets[2] << (2u * kM2ParticleRandomPackedByteBits)) |
                    (next_offsets[3] << (3u * kM2ParticleRandomPackedByteBits));
  return value_;
}

float M2ParticleRandomState::NextUnitFloat() noexcept {
  return M2RandomMantissaToUnitFloat(NextU32());
}

float M2ParticleRandomState::NextSignedUnitFloat() noexcept {
  return M2RandomMantissaToSignedUnitFloat(NextU32());
}

float M2ParticleRandomState::NextRange(const float range) noexcept {
  return NextUnitFloat() * range;
}

float M2ParticleRandomState::NextSignedRange(const float range) noexcept {
  return NextSignedUnitFloat() * range;
}

namespace {

const std::array<float, kM2ParticleTwinkleTableSize>&
M2ParticleTwinkleTable() {
  static const auto table = [] {

    core::ClientCrtRandom client_random;
    const std::uint32_t high = client_random.Next();
    const std::uint32_t low = client_random.Next();
    M2ParticleRandomState random((high << 16u) | (low & 0xffffu));
    std::array<float, kM2ParticleTwinkleTableSize> result{};
    for (float &sample : result) {
      sample = random.NextUnitFloat();
    }
    return result;
  }();
  return table;
}

}

M2ParticleRandomVector M2ParticleRandomState::NextUnitVector() noexcept {
  const float z = NextSignedUnitFloat();
  const float angle = NextRange(kTau);
  const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
  return {
      radius * std::cos(angle),
      radius * std::sin(angle),
      z,
  };
}

std::uint16_t M2ParticleRandomState::NextU16() noexcept {
  return static_cast<std::uint16_t>(NextU32());
}

std::uint32_t M2ParticleRandomState::NextIndex(const std::uint32_t count) noexcept {
  if (count == 0u) {
    return 0u;
  }
  return NextU32() % count;
}

std::optional<std::uint8_t>
ResolveM2ParticleRenderTypeFromBlendMode(const std::uint8_t blending_type) {
  switch (blending_type) {
  case 0:
    return 0;
  case 1:
    return 1;
  case 2:
    return 2;
  case 3:
    return 10;
  case 4:
    return 3;
  case 5:
    return 4;
  case 6:
    return 5;
  default:
    return std::nullopt;
  }
}

std::uint8_t ResolveM2ParticleRetailSortBlendIndex(const std::uint8_t render_particle_type,
                                                   const bool force_additive_sort) {
  if (force_additive_sort) {
    return kM2ParticleForcedAdditiveSortBlendIndex;
  }

  switch (render_particle_type) {
  case 1:
    return 1;
  case 2:
    return 2;
  case 3:
    return 4;
  case 4:
    return 5;
  case 5:
    return 6;
  case 10:
    return 3;
  default:
    return 0;
  }
}

std::uint32_t ResolveM2ParticleRetailFlagSortKey(const std::uint32_t flags,
                                                 const std::uint8_t blending_type) {

  const std::uint32_t base_bit = flags & 1u;
  const bool raw_bit3_set = ((flags >> 3) & 1u) != 0u;
  const bool opaque_like = blending_type == 0u || blending_type == 1u;

  std::uint32_t key = 4u + base_bit;
  if (raw_bit3_set) {
    key |= 2u;
  }
  if (!opaque_like) {
    key |= 0x10u;
  }
  return key;
}

bool IsM2ParticleAtlasDimensionValid(const std::uint32_t value) noexcept {
  return value != 0u && (value & (value - 1u)) == 0u;
}

bool IsM2ParticleAtlasValid(const std::uint32_t rows, const std::uint32_t columns) noexcept {
  return IsM2ParticleAtlasDimensionValid(rows) && IsM2ParticleAtlasDimensionValid(columns);
}

std::uint32_t ResolveM2ParticleTwinkleSampleIndex(
    const float age_seconds, const float twinkle_speed,
    const std::uintptr_t particle_address) noexcept {
  const auto age_step = static_cast<std::int64_t>(std::lround(age_seconds * twinkle_speed));
  const auto pointer_step =
      static_cast<std::int64_t>(particle_address >> kM2ParticleTwinklePointerEntropyShift);
  return static_cast<std::uint32_t>(age_step + pointer_step) & kM2ParticleTwinkleTableMask;
}

float SampleM2ParticleTwinkleTable(const std::uint32_t index) noexcept {
  return M2ParticleTwinkleTable()[index & kM2ParticleTwinkleTableMask];
}

std::uint16_t SampleM2ParticleU16LifetimeTrack(
    const openwow::data::model::M2ParticleLifetimeTrack<std::uint16_t> &track,
    const float lifetime_fraction, const std::uint16_t default_value) {
  return SampleM2ParticleLifetimeTrackValue(track, lifetime_fraction, default_value);
}

std::uint32_t
ComputeM2ParticleEmitterCapacity(const float emission_rate, const float emission_rate_variation,
                                 const float lifespan_seconds,
                                 const float lifespan_variation_seconds) noexcept {
  const float max_lifespan = lifespan_seconds + lifespan_variation_seconds;
  const float max_rate = emission_rate + emission_rate_variation;
  const float capacity = max_rate * kM2ParticleCapacityHeadroom * max_lifespan;
  if (!(capacity > 0.0f) || !std::isfinite(capacity)) {
    return 0u;
  }
  if (capacity >= static_cast<float>(std::numeric_limits<std::uint32_t>::max())) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(capacity);
}

std::optional<std::vector<M2ParticleDrawRange>>
BuildM2ParticleDrawRanges(const std::vector<M2ParticleDrawInput> &inputs,
                          const bool batch_particles, const bool force_additive_sort) {
  std::vector<M2ParticleDrawRange> ranges;
  ranges.reserve(inputs.size());

  for (const auto &input : inputs) {
    if (input.vertex_count == 0u) {
      continue;
    }

    const auto render_particle_type =
        ResolveM2ParticleRenderTypeFromBlendMode(input.blending_type);
    if (!render_particle_type.has_value()) {
      return std::nullopt;
    }
    const std::uint8_t sort_blend_index =
        ResolveM2ParticleRetailSortBlendIndex(*render_particle_type, force_additive_sort);
    const std::uint32_t flag_sort_key =
        ResolveM2ParticleRetailFlagSortKey(input.flags, input.blending_type);

    if (!ranges.empty() &&
        ShouldBatchWith(ranges.back(), input, batch_particles, sort_blend_index, flag_sort_key)) {
      ranges.back().emitter_count += 1u;
      ranges.back().vertex_count += input.vertex_count;
      continue;
    }

    ranges.push_back({
        .first_emitter = input.emitter_index,
        .emitter_count = 1u,
        .first_vertex = input.first_vertex,
        .vertex_count = input.vertex_count,
        .texture_index = input.texture_index,
        .blending_type = input.blending_type,
        .render_particle_type = *render_particle_type,
        .sort_blend_index = sort_blend_index,
        .flag_sort_key = flag_sort_key,
        .flags = input.flags,
    });
  }

  std::stable_sort(ranges.begin(), ranges.end(),
                   [](const M2ParticleDrawRange &lhs, const M2ParticleDrawRange &rhs) {
                     if (lhs.sort_blend_index != rhs.sort_blend_index) {
                       return lhs.sort_blend_index < rhs.sort_blend_index;
                     }
                     return lhs.flag_sort_key < rhs.flag_sort_key;
                   });

  return ranges;
}

void M2ParticleSystem::Init(const openwow::data::model::M2Model *model) {

  ++state_generation_;
  vertices_valid_ = false;
  model_ = model;
  emitters_.clear();
  emitter_splines_.clear();
  emitter_render_vertex_counts_.clear();

  (void)M2ParticleTwinkleTable();
  const std::uint32_t high = client_random_.Next();
  const std::uint32_t low = client_random_.Next();
  random_.Seed((high << 16u) | (low & 0xffffu));
  if (model_ == nullptr)
    return;
  emitters_.resize(model_->particle_emitters.size());
  emitter_splines_.resize(model_->particle_emitters.size());
  emitter_render_vertex_counts_.assign(model_->particle_emitters.size(), 0u);
  for (std::size_t index = 0; index < emitters_.size(); ++index) {
    auto &es = emitters_[index];
    es.emission_accumulator = 0.0f;
    es.spline_path_end_initialized = false;
    es.spline_path_end_dirty = false;
    es.spline_path_end = 0.0f;
    es.particles.clear();
    es.trail_delta_accumulator_sec = 0.0f;
    es.trail_current_position = {0.0f, 0.0f, 0.0f};
    es.trail_previous_position = {0.0f, 0.0f, 0.0f};
    es.trail_has_history = false;
    es.trail_delta = {0.0f, 0.0f, 0.0f};

    const auto &def = model_->particle_emitters[index];
    if (def.emitter_type == kM2ParticleEmitterTypeSpline) {
      std::vector<openwow::render::C3Vector> points;
      points.reserve(def.spline_points.size());
      for (const auto &point : def.spline_points) {
        points.push_back(ToM2ParticleSplinePoint(point));
      }
      emitter_splines_[index].SetCurveType(openwow::render::CSpline::CurveType::kBezier);
      emitter_splines_[index].SetControlPoints(points);
    }
  }
}

openwow::data::model::M2Vec3 M2ParticleSystem::SampleParticleLifetimeVec3(
    const openwow::data::model::M2ParticleLifetimeTrack<openwow::data::model::M2Vec3> &track,
    float t) const {
  return SampleM2ParticleLifetimeTrackValue(track, t, {255.0f, 255.0f, 255.0f});
}

openwow::data::model::M2Vec2 M2ParticleSystem::SampleParticleLifetimeVec2(
    const openwow::data::model::M2ParticleLifetimeTrack<openwow::data::model::M2Vec2> &track,
    float t) const {
  return SampleM2ParticleLifetimeTrackValue(track, t, {1.0f, 1.0f});
}

float M2ParticleSystem::SampleParticleLifetimeU16Norm(
    const openwow::data::model::M2ParticleLifetimeTrack<std::uint16_t> &track, float t) const {
  return static_cast<float>(
             SampleM2ParticleU16LifetimeTrack(track, t, kM2ParticleU16TrackNormDenominator)) /
         static_cast<float>(kM2ParticleU16TrackNormDenominator);
}

void M2ParticleSystem::Update(float delta_seconds, int animation_index, std::uint32_t time_ms,
                              const std::vector<float> &bone_mats, std::size_t bone_count,
                              const M2Animator &animator, const float density_scale,
                              const RenderMatrix4x4 &model_mtx,
                              const std::optional<bx::Vec3> &camera_world_position) {
  if (model_ == nullptr || emitters_.empty())
    return;
  const float dt_sec = delta_seconds;
  if (dt_sec <= 0.0f)
    return;

  ++state_generation_;

  const auto &pe = model_->particle_emitters;
  for (std::size_t ei = 0; ei < pe.size() && ei < emitters_.size(); ++ei) {
    const auto &def = pe[ei];
    auto &state = emitters_[ei];

    const std::size_t bi = static_cast<std::size_t>(def.bone);
    const RenderMatrix4x4View bone_mtx = ResolveM2ParticleBoneMatrix(bone_mats, bone_count, bi);

    const bool emitter_local_space =
        (def.flags & kM2ParticleFlagEmitterLocalSpace) != 0u;
    const RenderMatrix4x4 bone_world_mtx =
        ComposeM2ParticleMatrix(RenderMatrix4x4View{model_mtx}, bone_mtx);
    const bx::Vec3 emitter_pos = TransformPoint(
        RenderMatrix4x4View{bone_world_mtx},
        bx::Vec3{def.position[0], def.position[1], def.position[2]});
    state.emitter_world_matrix = bone_world_mtx;
    state.emitter_world_matrix[12] = emitter_pos.x;
    state.emitter_world_matrix[13] = emitter_pos.y;
    state.emitter_world_matrix[14] = emitter_pos.z;

    state.emitter_world_matrix = ComposeM2ParticleMatrix(
        RenderMatrix4x4View{state.emitter_world_matrix},
        RenderMatrix4x4View{kM2ParticleEmitterFrameRotation});
    const RenderMatrix4x4View emitter_mtx{state.emitter_world_matrix};

    state.emitter_matrix_x_scale =
        bx::length(TransformDir(emitter_mtx, bx::Vec3{1.0f, 0.0f, 0.0f}));

    float density_falloff = 1.0f;
    if (camera_world_position.has_value()) {
      const float distance = bx::length(bx::sub(emitter_pos, *camera_world_position));
      density_falloff = ResolveM2ParticleDistanceFalloff(distance);
    }

    const auto sample = animator.SampleParticleEmitter(ei, animation_index, time_ms);
    if (!sample.has_value()) {
      continue;
    }
    const float emission_rate = sample->emission_rate;
    const float lifespan = sample->lifespan;
    const float speed = sample->emission_speed;
    const float speed_var = sample->speed_variation;
    const float vert_range = sample->vertical_range;
    const float horiz_range = sample->horizontal_range;
    const float gravity = sample->gravity;
    const float area_len = sample->emission_area_length;
    const float area_wid = sample->emission_area_width;

    ShiftM2ParticleEmitterPositionHistory(emitter_pos, &state);
    const bx::Vec3 follow_displacement =
        ComputeM2ParticleFollowDisplacement(def, state, dt_sec);
    UpdateM2ParticleTrailState(def, &state, dt_sec);
    const std::uint32_t substep_count = ResolveM2ParticleSubstepCount(dt_sec, lifespan);
    const float follow_step_divisor = static_cast<float>(substep_count + 1u);
    const bx::Vec3 follow_step =
        substep_count > 0u ? bx::mul(follow_displacement, 1.0f / follow_step_divisor)
                           : follow_displacement;

    const auto update_step = [&](const float step_dt_sec, const bx::Vec3 &step_follow) {
      if (!(step_dt_sec > 0.0f)) {
        return;
      }

      const auto erase_expired_particles = [&state]() {
        state.particles.erase(
            std::remove_if(state.particles.begin(), state.particles.end(),
                           [](const M2Particle &p) { return p.age_ms >= p.max_age_ms; }),
            state.particles.end());
      };
      erase_expired_particles();

      for (auto &p : state.particles) {
        p.age_ms += step_dt_sec * kSecondsToMs;

        const float age_sec = p.age_ms * kMsToSec;
        if (age_sec <= def.wind_time && age_sec != def.wind_time) {
          p.velocity.x += def.wind_vector[0] * step_dt_sec;
          p.velocity.y += def.wind_vector[1] * step_dt_sec;
          p.velocity.z += def.wind_vector[2] * step_dt_sec;
          ClearNearZeroParticleVelocity(&p);
        }

        if ((def.flags & kM2ParticleFlagFollowEmitterMotion) != 0u &&
            step_dt_sec + step_dt_sec < age_sec) {
          p.position = bx::add(p.position, step_follow);
        }

        p.position.x += p.velocity.x * step_dt_sec;
        p.position.y += p.velocity.y * step_dt_sec;
        p.position.z +=
            p.velocity.z * step_dt_sec - gravity * step_dt_sec * step_dt_sec * 0.5f;
        p.velocity.z -= gravity * step_dt_sec;

        if (def.drag != 0.0f) {
          const float drag_step = def.drag * step_dt_sec;
          const float clamped_drag = drag_step <= 1.0f ? drag_step : 1.0f;
          p.velocity.x -= clamped_drag * p.velocity.x;
          p.velocity.y -= clamped_drag * p.velocity.y;
          p.velocity.z -= clamped_drag * p.velocity.z;
          ClearNearZeroParticleVelocity(&p);
        }

        p.spin_rad += p.spin_speed * step_dt_sec;
        AdvanceM2ParticleTumbleOrientation(&p, step_dt_sec);
      }
      erase_expired_particles();

      if (!sample->enabled) {
        state.emission_accumulator = 0.0f;
        return;
      }

      const float rate = std::max(
          0.0f, emission_rate * std::clamp(density_scale, 0.0f, 1.0f) * density_falloff);
      state.emission_accumulator += rate * step_dt_sec;
      const float clamped_accumulator = std::min(
          state.emission_accumulator,
          static_cast<float>(std::numeric_limits<std::uint32_t>::max()));
      const auto to_spawn = static_cast<std::uint32_t>(std::max(0.0f, clamped_accumulator));
      state.emission_accumulator -= static_cast<float>(to_spawn);

      const std::uint32_t emitter_capacity = ComputeM2ParticleEmitterCapacity(
          emission_rate, def.emission_rate_vary, lifespan, def.lifespan_vary);
      const std::size_t available = emitter_capacity > state.particles.size()
                                        ? emitter_capacity - state.particles.size()
                                        : 0u;
      const std::size_t spawn_count = std::min<std::size_t>(to_spawn, available);

      constexpr float kZSourceEpsilon = 0.001f;
      const float z_source =
          std::abs(sample->z_source) < kZSourceEpsilon ? 0.0f : sample->z_source;

      for (std::size_t s = 0; s < spawn_count; ++s) {
        M2Particle np;

        bx::Vec3 offset{0.0f, 0.0f, 0.0f};
        bx::Vec3 local_velocity{0.0f, 0.0f, 0.0f};
        bool has_path_velocity = false;
        bool has_spawn_direction = false;
        bx::Vec3 spawn_direction{0.0f, 0.0f, 1.0f};
        if (def.emitter_type == kM2ParticleEmitterTypePlane) {

          offset.x = random_.NextSignedRange(area_len * 0.5f);
          offset.y = random_.NextSignedRange(area_wid * 0.5f);
          if (z_source != 0.0f) {
            spawn_direction = NormalizeM2ParticleVector(
                {offset.x, offset.y, offset.z - z_source});
          } else {
            const float polar = random_.NextSignedRange(vert_range);
            const float azimuth = random_.NextSignedRange(horiz_range);
            spawn_direction = {std::cos(azimuth) * std::sin(polar),
                               std::sin(azimuth) * std::sin(polar),
                               std::cos(polar)};
          }
          has_spawn_direction = true;
        } else if (def.emitter_type == kM2ParticleEmitterTypeSphere) {

          const float radius = area_len + random_.NextRange(area_wid - area_len);
          const float latitude = random_.NextSignedRange(vert_range);
          const float azimuth = random_.NextSignedRange(horiz_range);
          const float cos_latitude = std::cos(latitude);
          const bx::Vec3 radial{std::cos(azimuth) * cos_latitude,
                                std::sin(azimuth) * cos_latitude,
                                std::sin(latitude)};
          offset = bx::mul(radial, radius);
          if (z_source != 0.0f) {
            spawn_direction = NormalizeM2ParticleVector(
                {offset.x, offset.y, offset.z - z_source});
          } else if ((def.flags & kM2ParticleFlagSphereEmitStraightUp) != 0u) {
            spawn_direction = {0.0f, 0.0f, 1.0f};
          } else {
            spawn_direction = radial;
          }
          has_spawn_direction = true;
        } else if (def.emitter_type == kM2ParticleEmitterTypeSpline) {
          if (ei >= emitter_splines_.size() ||
              !BuildM2ParticleSplineSpawn(emitter_splines_[ei], area_len, area_wid, z_source,
                                          speed, speed_var, vert_range, horiz_range, &state, &random_,
                                          &offset, &local_velocity)) {
            continue;
          }
          has_path_velocity = true;
        }

        np.position = emitter_local_space ? offset : TransformPoint(emitter_mtx, offset);

        const float actual_speed =
            has_path_velocity ? 0.0f : speed * (1.0f + random_.NextSignedRange(speed_var));
        bx::Vec3 dir = has_path_velocity ? local_velocity : spawn_direction;

        if (!has_path_velocity && !has_spawn_direction) {
          dir = {0.0f, 0.0f, 1.0f};
        }

        if (!has_path_velocity) {
          dir = bx::mul(dir, actual_speed);
        }

        if (!emitter_local_space) {
          dir = TransformDir(emitter_mtx, dir);
        }
        if (!has_path_velocity) {
          np.velocity = dir;
        } else {
          np.velocity = {
              ClearNearZeroParticleVelocityComponent(dir.x),
              ClearNearZeroParticleVelocityComponent(dir.y),
              ClearNearZeroParticleVelocityComponent(dir.z),
          };
        }

        if ((def.flags & kM2ParticleFlagPositionDeltaTrail) != 0u) {
          const float burst_scale = 1.0f + random_.NextSignedRange(speed_var);
          np.velocity = bx::add(np.velocity,
                                bx::mul(state.trail_delta, burst_scale));
        }

        const float ls_var = def.lifespan_vary;
        np.max_age_ms = lifespan * kSecondsToMs * (1.0f + random_.NextSignedRange(ls_var));
        if (!(np.max_age_ms > 0.0f) || !std::isfinite(np.max_age_ms)) {
          continue;
        }
        np.age_ms = 0.0f;

        np.visual_random_seed = random_.NextU16();

        np.spin_speed = def.spin + random_.NextSignedRange(def.spin_vary);
        np.spin_rad = def.base_spin + random_.NextSignedRange(def.base_spin_vary);
        InitM2ParticleTumble(def, &np, &random_);

        const std::uint32_t total_tiles =
            static_cast<std::uint32_t>(def.texture_tile_rows) *
            static_cast<std::uint32_t>(def.texture_tile_cols);
        if (total_tiles > 1u && (def.flags & kM2ParticleFlagRandomTextureTile) != 0u) {
          np.tile_index = random_.NextIndex(total_tiles);
        }

        state.particles.push_back(np);
      }
    };

    for (std::uint32_t step = 0; step < substep_count; ++step) {
      update_step(kM2ParticleUpdateSubstepSeconds, follow_step);
    }
    const float remainder =
        dt_sec - static_cast<float>(substep_count) * kM2ParticleUpdateSubstepSeconds;
    update_step(remainder, follow_step);
  }
}

const std::vector<ParticleVertex> &M2ParticleSystem::BuildVertices(const bx::Vec3 &cam_right,
                                                                   const bx::Vec3 &cam_up) {

  const auto vec3_bits = [](const bx::Vec3 &value) {
    return std::array<std::uint32_t, 3>{std::bit_cast<std::uint32_t>(value.x),
                                        std::bit_cast<std::uint32_t>(value.y),
                                        std::bit_cast<std::uint32_t>(value.z)};
  };
  const auto cam_right_bits = vec3_bits(cam_right);
  const auto cam_up_bits = vec3_bits(cam_up);
  if (vertices_valid_ && vertices_state_generation_ == state_generation_ &&
      vertices_cam_right_bits_ == cam_right_bits &&
      vertices_cam_up_bits_ == cam_up_bits) {
    return vertices_;
  }
  vertices_valid_ = true;
  vertices_state_generation_ = state_generation_;
  vertices_cam_right_bits_ = cam_right_bits;
  vertices_cam_up_bits_ = cam_up_bits;

  vertices_.clear();
  emitter_render_vertex_counts_.assign(emitters_.size(), 0u);
  if (model_ == nullptr)
    return vertices_;

  const auto &pe = model_->particle_emitters;

  std::size_t total = 0;
  for (const auto &es : emitters_)
    total += es.particles.size();
  vertices_.reserve(total * 12u);

  std::vector<ParticleSortEntry> sorted_particles;

  for (std::size_t ei = 0; ei < pe.size() && ei < emitters_.size(); ++ei) {
    const auto &def = pe[ei];
    const auto &state = emitters_[ei];
    const std::size_t emitter_first_vertex = vertices_.size();
    if (state.particles.empty())
      continue;
    const bool emit_head = (def.flags & kM2ParticleFlagHeadGeometry) != 0u;
    const bool emit_tail = (def.flags & kM2ParticleFlagTailGeometry) != 0u;
    if (!emit_head && !emit_tail) {
      continue;
    }

    const bool emitter_local_space =
        (def.flags & kM2ParticleFlagEmitterLocalSpace) != 0u;
    const RenderMatrix4x4View particle_to_world{
        emitter_local_space ? state.emitter_world_matrix : kRenderIdentityMatrix4x4};

    const bx::Vec3 cam_fwd = bx::cross(cam_up, cam_right);
    const auto to_view = [&](const bx::Vec3 &world) {
      return bx::Vec3{bx::dot(world, cam_right), bx::dot(world, cam_up), bx::dot(world, cam_fwd)};
    };
    const auto from_view = [&](const bx::Vec3 &view) {
      return bx::add(bx::add(bx::mul(cam_right, view.x), bx::mul(cam_up, view.y)),
                     bx::mul(cam_fwd, view.z));
    };

    const bool align_to_emitter_plane =
        (def.flags & kM2ParticleFlagAlignToEmitterPlane) != 0u;
    bx::Vec3 plane_basis_x_view{1.0f, 0.0f, 0.0f};
    bx::Vec3 plane_basis_y_view{0.0f, 1.0f, 0.0f};
    bx::Vec3 plane_spin_axis_view{0.0f, 0.0f, 1.0f};
    if (align_to_emitter_plane) {
      bx::Vec3 axis_x = TransformDir(particle_to_world, bx::Vec3{1.0f, 0.0f, 0.0f});
      bx::Vec3 axis_y = TransformDir(particle_to_world, bx::Vec3{0.0f, 1.0f, 0.0f});
      bx::Vec3 axis_z = TransformDir(particle_to_world, bx::Vec3{0.0f, 0.0f, 1.0f});
      if (emitter_local_space &&
          std::abs(state.emitter_matrix_x_scale) >= kM2ParticleEmitterPlaneBasisEpsilon) {
        const float inverse_scale = 1.0f / state.emitter_matrix_x_scale;
        axis_x = bx::mul(axis_x, inverse_scale);
        axis_y = bx::mul(axis_y, inverse_scale);
        axis_z = bx::mul(axis_z, inverse_scale);
      }
      const float axis_z_length_squared = bx::dot(axis_z, axis_z);
      if (axis_z_length_squared > kM2ParticleEmitterPlaneBasisEpsilon) {
        axis_z = bx::mul(axis_z, 1.0f / std::sqrt(axis_z_length_squared));
      }
      plane_basis_x_view = to_view(axis_x);
      plane_basis_y_view = to_view(axis_y);
      plane_spin_axis_view = to_view(axis_z);
    }

    const bool spin_rotates_corners = def.spin != 0.0f || def.spin_vary != 0.0f;

    sorted_particles.clear();
    sorted_particles.reserve(state.particles.size());
    for (std::size_t pi = 0; pi < state.particles.size(); ++pi) {
      const bx::Vec3 world =
          TransformPoint(particle_to_world, state.particles[pi].position);
      sorted_particles.push_back({
          .index = static_cast<std::uint32_t>(pi),

          .depth = bx::dot(world, cam_fwd),
      });
    }
    if ((def.flags & kM2ParticleFlagSortByViewDepth) != 0u) {
      std::sort(sorted_particles.begin(), sorted_particles.end(),
                [](const ParticleSortEntry &a, const ParticleSortEntry &b) {
                  return a.depth > b.depth;
                });
    }

    if (!IsM2ParticleAtlasValid(def.texture_tile_rows, def.texture_tile_cols)) {
      continue;
    }

    const std::uint32_t rows = static_cast<std::uint32_t>(def.texture_tile_rows);
    const std::uint32_t cols = static_cast<std::uint32_t>(def.texture_tile_cols);
    const std::uint32_t total_tiles = rows * cols;
    const float tu_size = 1.0f / static_cast<float>(cols);
    const float tv_size = 1.0f / static_cast<float>(rows);

    for (const auto &sort_entry : sorted_particles) {
      const std::size_t pi = sort_entry.index;
      const auto &p = state.particles[pi];
      const float t = std::min(1.0f, p.age_ms / p.max_age_ms);

      const auto col3 = SampleParticleLifetimeVec3(def.color_track, t);
      const float alpha = SampleParticleLifetimeU16Norm(def.alpha_track, t);
      const auto scale2 = SampleParticleLifetimeVec2(def.scale_track, t);
      M2ParticleRandomState visual_random(p.visual_random_seed);
      const float scale_factor_x =
          std::max(0.0001f, 1.0f + visual_random.NextSignedRange(def.scale_vary.x));
      const float scale_factor_y =
          (def.flags & kM2ParticleFlagIndependentScaleVariation) != 0u
              ? std::max(0.0001f, 1.0f + visual_random.NextSignedRange(def.scale_vary.y))
              : scale_factor_x;
      const auto twinkle_scale = ResolveM2ParticleTwinkleScale(
          def, p.age_ms * kMsToSec, reinterpret_cast<std::uintptr_t>(&p));
      if (!twinkle_scale.has_value()) {
        continue;
      }

      const float emitter_matrix_scale =
          (def.flags & kM2ParticleFlagScaleByEmitterMatrix) != 0u ? state.emitter_matrix_x_scale
                                                                   : 1.0f;
      const float half_w = scale2.x * scale_factor_x * *twinkle_scale * emitter_matrix_scale;
      const float half_h = scale2.y * scale_factor_y * *twinkle_scale * emitter_matrix_scale;
      if (!(half_w > 0.0f) || !(half_h > 0.0f)) {
        continue;
      }

      const std::uint32_t color = PackM2ParticleABGR(col3.x, col3.y, col3.z, alpha);

      const bx::Vec3 world_pos = TransformPoint(particle_to_world, p.position);

      bx::Vec3 right = cam_right;
      bx::Vec3 up = cam_up;
      if (!align_to_emitter_plane) {

        if (spin_rotates_corners) {
          const float cs = std::cos(p.spin_rad);
          const float sn = std::sin(p.spin_rad);
          right = bx::add(bx::mul(cam_right, cs), bx::mul(cam_up, sn));
          up = bx::add(bx::mul(cam_up, cs), bx::mul(cam_right, -sn));
        }
      } else {

        bx::Vec3 right_view = plane_basis_x_view;
        bx::Vec3 up_view = plane_basis_y_view;
        if (spin_rotates_corners) {
          const auto spin_matrix = BuildM2ParticleAxisAngleMatrix(plane_spin_axis_view, p.spin_rad);
          right_view = ApplyM2ParticleAxisAngleMatrix(spin_matrix, right_view);
          up_view = ApplyM2ParticleAxisAngleMatrix(spin_matrix, up_view);
        }
        right = from_view(right_view);
        up = from_view(up_view);
      }
      if (bx::length(p.angular_velocity) > kM2ParticleTumbleAngularSpeedEpsilon) {
        right = RotateM2ParticleVector(right, p.orientation);
        up = RotateM2ParticleVector(up, p.orientation);
      }

      const bx::Vec3 r_scaled = bx::mul(right, half_w);
      const bx::Vec3 u_scaled = bx::mul(up, half_h);

      const std::uint32_t head_tile =
          static_cast<std::uint32_t>(
              SampleM2ParticleU16LifetimeTrack(def.head_cell_track, t,
                                               static_cast<std::uint16_t>(p.tile_index))) %
          total_tiles;
      const auto atlas_rect = [&](const std::uint32_t tile) {
        const std::uint32_t tile_row = tile / cols;
        const std::uint32_t tile_col = tile % cols;
        const float u0 = static_cast<float>(tile_col) * tu_size;
        const float v0 = static_cast<float>(tile_row) * tv_size;
        return std::array<float, 4>{u0, v0, u0 + tu_size, v0 + tv_size};
      };

      if (emit_head) {
        const auto rect = atlas_rect(head_tile);
        PushParticleQuad(&vertices_, BuildM2ParticleQuadCorners(world_pos, r_scaled, u_scaled),
                         color, rect[0], rect[1], rect[2], rect[3]);
      }

      if (emit_tail) {
        const std::uint32_t tail_tile =
            static_cast<std::uint32_t>(
                SampleM2ParticleU16LifetimeTrack(def.tail_cell_track, t, 0u)) %
            total_tiles;
        const auto rect = atlas_rect(tail_tile);

        float tail_time = std::max(0.0f, def.tail_length);
        if ((def.flags & kM2ParticleFlagTailLengthClampToAge) != 0u) {
          tail_time = std::min(tail_time, p.age_ms * kMsToSec);
        }

        const bx::Vec3 tail_delta =
            TransformDir(particle_to_world, bx::mul(p.velocity, -tail_time));
        const float tail_x = bx::dot(tail_delta, cam_right);
        const float tail_y = bx::dot(tail_delta, cam_up);
        const float tail_len_sq = tail_x * tail_x + tail_y * tail_y;

        std::array<bx::Vec3, 4> tail_corners{{
            bx::Vec3{0.0f, 0.0f, 0.0f},
            bx::Vec3{0.0f, 0.0f, 0.0f},
            bx::Vec3{0.0f, 0.0f, 0.0f},
            bx::Vec3{0.0f, 0.0f, 0.0f},
        }};
        if (tail_len_sq >= kM2ParticleTailScreenLengthSquaredMin) {
          const float inv_tail_len = 1.0f / std::sqrt(tail_len_sq);
          const bx::Vec3 side = bx::add(bx::mul(cam_right, -tail_y * inv_tail_len * half_h),
                                        bx::mul(cam_up, tail_x * inv_tail_len * half_w));
          const bx::Vec3 tail_end = bx::add(world_pos, tail_delta);
          tail_corners = {{
              bx::add(world_pos, side),
              bx::sub(world_pos, side),
              bx::sub(tail_end, side),
              bx::add(tail_end, side),
          }};
        } else {

          tail_corners = BuildM2ParticleQuadCorners(world_pos, bx::mul(cam_right, half_w),
                                                    bx::mul(cam_up, half_h));
        }

        PushParticleQuad(&vertices_, tail_corners, color, rect[0], rect[1], rect[2], rect[3]);
      }
    }
    emitter_render_vertex_counts_[ei] =
        static_cast<std::uint32_t>(vertices_.size() - emitter_first_vertex);
  }

  return vertices_;
}

std::size_t M2ParticleSystem::TotalParticleCount() const {
  std::size_t total = 0;
  for (const auto &es : emitters_)
    total += es.particles.size();
  return total;
}

std::optional<std::uint64_t> M2ParticleSystem::BlendStateForType(std::uint8_t blending_type) {
  if (blending_type >= static_cast<std::uint8_t>(M2BlendMode::Count)) {
    return std::nullopt;
  }
  const auto mode = static_cast<M2BlendMode>(blending_type);
  const auto blend_state = ResolveM2BlendState(mode);
  if (!blend_state.has_value()) {
    return std::nullopt;
  }

  const bool depth_tested =
      mode == M2BlendMode::Opaque || mode == M2BlendMode::AlphaKey;
  std::uint64_t state = *blend_state;
  if (!depth_tested) {
    state &= ~(BGFX_STATE_DEPTH_TEST_MASK | BGFX_STATE_WRITE_Z);
  }
  return state | BGFX_STATE_MSAA;
}

ParticleShaderHandles LoadParticleProgram() {
  ParticleShaderHandles out;
  const auto type = bgfx::getRendererType();
  out.program = openwow::render::CreateEmbeddedProgram(
      openwow::render::ShaderProgramId::Particle, type);
  out.sampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
  out.flags = bgfx::createUniform("u_particleFlags", bgfx::UniformType::Vec4);
  out.fog_params = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
  out.fog_color = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
  return out;
}

void DestroyParticleProgram(ParticleShaderHandles &h) {
  if (bgfx::isValid(h.program)) {
    bgfx::destroy(h.program);
    h.program = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(h.sampler)) {
    bgfx::destroy(h.sampler);
    h.sampler = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(h.flags)) {
    bgfx::destroy(h.flags);
    h.flags = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(h.fog_params)) {
    bgfx::destroy(h.fog_params);
    h.fog_params = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(h.fog_color)) {
    bgfx::destroy(h.fog_color);
    h.fog_color = BGFX_INVALID_HANDLE;
  }
}

}
