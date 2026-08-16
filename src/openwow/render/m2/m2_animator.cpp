#include "openwow/render/m2/m2_animator.h"

#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/m2/m2_animation_simd.h"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace openwow::render::m2 {

namespace {

namespace math = animation_math;
using math::M2Float4;
using math::M2Matrix4x4;
using BonePose = math::M2BonePose;
using Kernels = math::M2Kernels;

constexpr std::size_t kBoneMatrixFloatCount = 16u;

[[nodiscard]] std::optional<RenderMatrix4x4View>
BoneMatrixAt(const std::span<const float> bone_matrices, const std::size_t bone_index) noexcept {
  if (bone_index > std::numeric_limits<std::size_t>::max() / kBoneMatrixFloatCount) {
    return std::nullopt;
  }

  const std::size_t offset = bone_index * kBoneMatrixFloatCount;
  if (offset > bone_matrices.size() ||
      bone_matrices.size() - offset < kBoneMatrixFloatCount) {
    return std::nullopt;
  }
  return RenderMatrix4x4View{bone_matrices.data() + offset, kBoneMatrixFloatCount};
}

[[nodiscard]] RenderVec3 TransformPoint(const RenderVec3View point,
                                        const RenderMatrix4x4View matrix) noexcept {
  return {
      matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
      matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
      matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14],
  };
}

bx::Vec3 Vec3From(const openwow::data::model::M2Vec3 &v) {
  return bx::Vec3{v.x, v.y, v.z};
}

[[nodiscard]] M2Float4 DecodeCompactQuaternion(
    const openwow::data::model::M2Quat16 &q) noexcept {
  return Kernels::DecodeCompactQuaternion(q);
}

[[nodiscard]] M2Float4 Nlerp(const M2Float4 &a, const M2Float4 &b, const float t) noexcept {
  return math::NlerpQuaternion<math::kM2AnimationMathBackend>(a, b, t);
}

std::uint32_t WrapTimeMs(std::uint32_t time_ms, std::uint32_t duration_ms) {
  if (duration_ms == 0)
    return 0;
  return time_ms % duration_ms;
}

template <typename T> struct TrackRef {
  const openwow::data::model::M2Track<T> *track{nullptr};
  int animation_index{0};
  std::uint32_t time_ms{0};
  std::uint32_t animation_duration_ms{0};
  const std::vector<std::uint32_t> *global_sequences_ms{nullptr};

  bool has_animation_wrapped_time{false};
  std::uint32_t animation_wrapped_time_ms{0};
};

template <typename T> struct TrackSet {
  std::span<const std::uint32_t> times;
  std::span<const T> values;

  [[nodiscard]] bool Empty() const noexcept { return times.empty() || values.empty(); }
};

template <typename T>
[[nodiscard]] BX_FORCE_INLINE TrackSet<T> ResolveTrackSet(const TrackRef<T> &ref) {
  if (ref.track == nullptr) {
    return {};
  }
  const auto &segments = ref.track->segments;
  if (segments.empty()) {
    return {};
  }
  std::size_t set_index = 0;
  if (ref.track->global_sequence < 0) {
    set_index = static_cast<std::size_t>(std::max(0, ref.animation_index));
  }
  if (set_index >= segments.size()) {
    set_index = 0;
  }
  const openwow::data::model::M2TrackSegment segment = segments[set_index];
  return TrackSet<T>{
      .times = {ref.track->key_times_ms.data() + segment.first_time, segment.time_count},
      .values = {ref.track->key_values.data() + segment.first_value, segment.value_count},
  };
}

const std::vector<std::uint32_t> *
ResolveDiscreteTrackTimes(const openwow::data::model::M2DiscreteTrack *track, int animation_index) {
  if (track == nullptr || track->times_ms.empty()) {
    return nullptr;
  }

  std::size_t set_index = 0;
  if (track->global_sequence < 0) {
    set_index = static_cast<std::size_t>(std::max(0, animation_index));
  }
  if (set_index >= track->times_ms.size()) {
    set_index = 0;
  }
  return &track->times_ms[set_index];
}

std::uint32_t ResolveDiscreteTrackPeriodMs(const openwow::data::model::M2DiscreteTrack &track,
                                           std::uint32_t animation_duration_ms,
                                           const std::vector<std::uint32_t> &global_sequences_ms,
                                           const std::vector<std::uint32_t> &event_times_ms) {
  if (track.global_sequence >= 0 &&
      static_cast<std::size_t>(track.global_sequence) < global_sequences_ms.size()) {
    return global_sequences_ms[static_cast<std::size_t>(track.global_sequence)];
  }
  if (animation_duration_ms > 0) {
    return animation_duration_ms;
  }
  return event_times_ms.empty() ? 0u : event_times_ms.back();
}

void AppendDiscreteEventsForInterval(std::vector<M2TriggeredEvent> *out,
                                     const openwow::data::model::M2Event &event,
                                     const std::vector<std::uint32_t> &event_times_ms,
                                     std::uint32_t start_ms, std::uint32_t end_ms,
                                     std::uint64_t loop_base_ms, std::uint32_t current_time_ms,
                                     const openwow::data::model::M2Model *model,
                                     int animation_index, std::span<const float> bone_matrices,
                                     const std::optional<RenderMatrix4x4> &model_matrix) {
  if (out == nullptr || end_ms <= start_ms) {
    return;
  }
  for (const std::uint32_t event_time_ms : event_times_ms) {
    if (event_time_ms > start_ms && event_time_ms <= end_ms) {
      M2TriggeredEvent triggered{
          .identifier = event.identifier,
          .data = event.data,
          .bone = event.bone,
          .model_position =
              {
                  event.position[0],
                  event.position[1],
                  event.position[2],
              },
          .world_position =
              {
                  event.position[0],
                  event.position[1],
                  event.position[2],
              },
      };

      RenderVec3 model_position{event.position[0], event.position[1], event.position[2]};

      if (event.bone >= 0) {

        const std::size_t bone_index = static_cast<std::size_t>(event.bone);
        const bool has_own_pose = model != nullptr && !model->bones.empty();
        std::optional<RenderMatrix4x4> bone_matrix;
        if (has_own_pose) {

          if (bone_index < model->bones.size()) {
            const M2Animator animator(model);
            bone_matrix =
                animator.ComputeSingleBoneMatrix(bone_index, animation_index, event_time_ms, {});
            if (!bone_matrix.has_value()) {

              if (const auto palette =
                      animator.ComputeBoneMatrices(animation_index, event_time_ms)) {
                if (const auto view = BoneMatrixAt(*palette, bone_index)) {
                  bone_matrix.emplace();
                  std::copy(view->begin(), view->end(), bone_matrix->begin());
                }
              }
            }
          }
        } else if (const auto view = BoneMatrixAt(bone_matrices, bone_index)) {
          bone_matrix.emplace();
          std::copy(view->begin(), view->end(), bone_matrix->begin());
        }
        if (bone_matrix.has_value()) {
          model_position =
              TransformPoint(RenderVec3View{model_position}, RenderMatrix4x4View{*bone_matrix});
        }
      }

      triggered.model_position = model_position;
      triggered.world_position = triggered.model_position;

      if (model_matrix.has_value()) {
        triggered.world_position =
            TransformPoint(RenderVec3View{model_position}, RenderMatrix4x4View{*model_matrix});
      }

      const std::uint64_t absolute_event_time_ms = loop_base_ms + event_time_ms;
      if (absolute_event_time_ms <= current_time_ms) {
        triggered.delay_to_interval_end_ms =
            current_time_ms - static_cast<std::uint32_t>(absolute_event_time_ms);
      }

      out->push_back(triggered);
    }
  }
}

template <typename T> struct TrackSamplePosition {
  std::span<const T> values;
  std::size_t index{0};
  float alpha{0.0f};

  bool interpolate{false};

  bool valid{false};
};

template <typename T>
[[nodiscard]] BX_FORCE_INLINE TrackSamplePosition<T> LocateTrackSample(
    const TrackRef<T> &ref) {
  TrackSamplePosition<T> at;
  if (ref.track == nullptr) {
    return at;
  }
  const TrackSet<T> set = ResolveTrackSet(ref);
  if (set.Empty()) {
    return at;
  }

  const std::span<const std::uint32_t> times = set.times;
  at.values = set.values;
  at.valid = true;
  if (times.size() <= 1u) {
    return at;
  }

  std::uint32_t local_time = ref.time_ms;
  if (ref.track->global_sequence >= 0 && ref.global_sequences_ms != nullptr &&
      static_cast<std::size_t>(ref.track->global_sequence) < ref.global_sequences_ms->size()) {
    local_time = WrapTimeMs(
        local_time,
        (*ref.global_sequences_ms)[static_cast<std::size_t>(ref.track->global_sequence)]);
  } else if (ref.animation_duration_ms > 0u) {
    local_time = ref.has_animation_wrapped_time
                     ? ref.animation_wrapped_time_ms
                     : WrapTimeMs(local_time, ref.animation_duration_ms);
  } else if (const std::uint32_t last = times.back(); last > 0u) {
    local_time = WrapTimeMs(local_time, last);
  }

  const auto it = std::upper_bound(times.begin(), times.end(), local_time);
  if (it == times.begin()) {
    return at;
  }

  const std::size_t index = static_cast<std::size_t>(std::distance(times.begin(), it) - 1);
  if (index + 1u >= times.size() || index + 1u >= at.values.size()) {
    at.index = std::min(index, at.values.size() - 1u);
    return at;
  }

  at.index = index;
  const std::uint32_t first_time = times[index];
  const std::uint32_t second_time = times[index + 1u];
  if (second_time <= first_time || ref.track->interpolation == 0u) {
    return at;
  }

  at.alpha = static_cast<float>(local_time - first_time) /
             static_cast<float>(second_time - first_time);
  at.interpolate = true;
  return at;
}

template <typename T, typename InterpFn>
T SampleTrack(const TrackRef<T> &ref, const T &default_value, InterpFn interp) {
  const auto at = LocateTrackSample(ref);
  if (!at.valid) {
    return default_value;
  }
  if (!at.interpolate) {
    return at.values[at.index];
  }

  return interp(at.values[at.index], at.values[at.index + 1u], at.alpha);
}

[[nodiscard]] M2Float4 SampleBoneVec3Track(const TrackRef<openwow::data::model::M2Vec3> &ref,
                                           const M2Float4 &default_value) {
  static_assert(sizeof(openwow::data::model::M2Vec3) == 3u * sizeof(float),
                "LerpVec3FromTrack indexes an M2Vec3 value array as a flat float array.");
  const auto at = LocateTrackSample(ref);
  if (!at.valid) {
    return default_value;
  }
  if (!at.interpolate) {
    const auto &value = at.values[at.index];
    return M2Float4{{value.x, value.y, value.z, 0.0f}};
  }
  return Kernels::LerpVec3FromTrack(reinterpret_cast<const float *>(at.values.data()),
                                    at.index, at.alpha);
}

[[nodiscard]] M2Float4 SampleQuaternionTrack(
    const TrackRef<openwow::data::model::M2Quat16> &ref) {
  constexpr M2Float4 kIdentity{{0.0f, 0.0f, 0.0f, 1.0f}};
  const auto at = LocateTrackSample(ref);
  if (!at.valid) {
    return kIdentity;
  }

  const M2Float4 first = DecodeCompactQuaternion(at.values[at.index]);
  if (!at.interpolate) {
    return first;
  }
  return Nlerp(first, DecodeCompactQuaternion(at.values[at.index + 1u]), at.alpha);
}

[[nodiscard]] M2Float4 SampleQuaternionTrack(
    const TrackRef<openwow::data::model::M2Quat> &ref) {
  constexpr M2Float4 kIdentity{{0.0f, 0.0f, 0.0f, 1.0f}};
  const auto at = LocateTrackSample(ref);
  if (!at.valid) {
    return kIdentity;
  }

  const auto to_float4 = [](const openwow::data::model::M2Quat &q) {
    return M2Float4{{q.x, q.y, q.z, q.w}};
  };
  const M2Float4 first = to_float4(at.values[at.index]);
  if (!at.interpolate) {
    return first;
  }
  return Nlerp(first, to_float4(at.values[at.index + 1u]), at.alpha);
}

float BlendSplineScalar(const float value0, const float in0, const float out0, const float value1,
                        const float in1, const float out1, const float t,
                        const std::uint16_t interpolation) {
  (void)in0;
  (void)out1;
  if (interpolation == 0) {
    return value0;
  }
  if (interpolation == 2) {
    const float t2 = t * t;
    const float t3 = t * t2;
    return ((t2 * 3.0f - t3) + t * -3.0f + 1.0f) * value0 +
           (t3 * 3.0f + t2 * -6.0f + t * 3.0f) * out0 + (t2 * 3.0f + t3 * -3.0f) * in1 +
           t3 * value1;
  }
  if (interpolation == 3) {
    const float t2 = t * t;
    const float t3 = t * t2;
    return (t3 + t3 + t2 * -3.0f + 1.0f) * value0 + (t + t2 * -2.0f + t3) * out0 + (t3 - t2) * in1 +
           (t3 * -2.0f + t2 * 3.0f) * value1;
  }
  return value0 + (value1 - value0) * t;
}

openwow::data::model::M2Vec3 BlendSplineVec3(const openwow::data::model::M2Vec3 &value0,
                                             const openwow::data::model::M2Vec3 &in0,
                                             const openwow::data::model::M2Vec3 &out0,
                                             const openwow::data::model::M2Vec3 &value1,
                                             const openwow::data::model::M2Vec3 &in1,
                                             const openwow::data::model::M2Vec3 &out1,
                                             const float t, const std::uint16_t interpolation) {
  return {
      BlendSplineScalar(value0.x, in0.x, out0.x, value1.x, in1.x, out1.x, t, interpolation),
      BlendSplineScalar(value0.y, in0.y, out0.y, value1.y, in1.y, out1.y, t, interpolation),
      BlendSplineScalar(value0.z, in0.z, out0.z, value1.z, in1.z, out1.z, t, interpolation),
  };
}

template <typename T, typename BlendFn>
T SampleCameraSplineTrack(const TrackRef<T> &ref, const T &default_value, BlendFn blend) {
  if (ref.track == nullptr)
    return default_value;
  const TrackSet<T> set = ResolveTrackSet(ref);
  if (set.Empty())
    return default_value;
  const std::span<const std::uint32_t> times = set.times;
  const std::span<const T> values = set.values;

  constexpr std::size_t kSplineStride = 3;
  if (values.size() < kSplineStride)
    return default_value;
  if (times.size() <= 1)
    return values[0];

  std::uint32_t local_time = ref.time_ms;
  if (ref.track->global_sequence >= 0 && ref.global_sequences_ms != nullptr &&
      static_cast<std::size_t>(ref.track->global_sequence) < ref.global_sequences_ms->size()) {
    local_time = WrapTimeMs(
        local_time,
        (*ref.global_sequences_ms)[static_cast<std::size_t>(ref.track->global_sequence)]);
  } else if (ref.animation_duration_ms > 0) {

    local_time = ref.has_animation_wrapped_time
                     ? ref.animation_wrapped_time_ms
                     : WrapTimeMs(local_time, ref.animation_duration_ms);
  } else {
    const std::uint32_t last = times.back();
    if (last > 0) {
      local_time = WrapTimeMs(local_time, last);
    }
  }

  const auto it = std::upper_bound(times.begin(), times.end(), local_time);
  if (it == times.begin()) {
    return values[0];
  }

  const std::size_t idx = static_cast<std::size_t>(std::distance(times.begin(), it) - 1);
  const std::size_t value_index = idx * kSplineStride;
  const std::size_t next_value_index = (idx + 1) * kSplineStride;
  if (idx + 1 >= times.size() || next_value_index + 2 >= values.size()) {
    return values[std::min(value_index, values.size() - 1)];
  }

  const std::uint32_t t0 = times[idx];
  const std::uint32_t t1 = times[idx + 1];
  if (t1 <= t0) {
    return values[value_index];
  }

  const float alpha = static_cast<float>(local_time - t0) / static_cast<float>(t1 - t0);
  return blend(values[value_index], values[value_index + 1], values[value_index + 2],
               values[next_value_index], values[next_value_index + 1],
               values[next_value_index + 2], alpha, ref.track->interpolation);
}

template <typename T> bool TrackHasAnyValues(const openwow::data::model::M2Track<T> &track) {
  return !track.key_values.empty();
}

std::uint32_t AnimationDurationMs(const openwow::data::model::M2Model &model,
                                  const int animation_index) {
  if (animation_index >= 0 &&
      static_cast<std::size_t>(animation_index) < model.animation_durations_ms.size()) {
    return model.animation_durations_ms[static_cast<std::size_t>(animation_index)];
  }
  return 0u;
}

struct ChannelClock {
  int animation_index{0};
  std::uint32_t time_ms{0};
  std::uint32_t animation_duration_ms{0};

  bool wrap_by_animation{false};
  std::uint32_t animation_wrapped_time_ms{0};

  const std::uint64_t *keyless_rows{nullptr};
};

[[nodiscard]] ChannelClock MakeChannelClock(const openwow::data::model::M2Model &model,
                                            const int animation_index,
                                            const std::uint32_t time_ms) {
  const std::uint32_t anim_duration = AnimationDurationMs(model, animation_index);
  const bool wrap_by_animation = anim_duration > 0u;
  return ChannelClock{
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .wrap_by_animation = wrap_by_animation,
      .animation_wrapped_time_ms =
          wrap_by_animation ? WrapTimeMs(time_ms, anim_duration) : 0u,
      .keyless_rows = model.bone_pose_index.KeylessRowsFor(animation_index),
  };
}

struct BoneTrackKeyless {
  bool translation{false};
  bool rotation{false};
  bool scaling{false};

  [[nodiscard]] bool AllThree() const noexcept {
    return translation && rotation && scaling;
  }
};

[[nodiscard]] BoneTrackKeyless ResolveBoneTrackKeyless(
    const openwow::data::model::M2BonePoseIndex &index, const ChannelClock &clock,
    const std::size_t bone_index) noexcept {
  if (clock.keyless_rows == nullptr) {
    return BoneTrackKeyless{};
  }
  using Index = openwow::data::model::M2BonePoseIndex;
  return BoneTrackKeyless{
      .translation = index.TestBit(clock.keyless_rows, Index::kTranslationRow, bone_index),
      .rotation = index.TestBit(clock.keyless_rows, Index::kRotationRow, bone_index),
      .scaling = index.TestBit(clock.keyless_rows, Index::kScalingRow, bone_index),
  };
}

[[nodiscard]] std::uint32_t SlotTimeMs(const float time_seconds) noexcept {
  return static_cast<std::uint32_t>(std::max(time_seconds, 0.0f) * 1000.0f);
}

BonePose SampleBonePose(const openwow::data::model::M2Model &model,
                        const openwow::data::model::M2Bone &bone,
                        const ChannelClock &clock,
                        const BoneTrackKeyless &keyless) {
  BonePose pose;
  if (!keyless.translation) {
    const TrackRef<openwow::data::model::M2Vec3> t_ref{
        .track = &bone.translation,
        .animation_index = clock.animation_index,
        .time_ms = clock.time_ms,
        .animation_duration_ms = clock.animation_duration_ms,
        .global_sequences_ms = &model.global_sequences_ms,
        .has_animation_wrapped_time = clock.wrap_by_animation,
        .animation_wrapped_time_ms = clock.animation_wrapped_time_ms,
    };
    pose.translation = SampleBoneVec3Track(t_ref, M2Float4{{0.0f, 0.0f, 0.0f, 0.0f}});
  }
  if (!keyless.scaling) {
    const TrackRef<openwow::data::model::M2Vec3> s_ref{
        .track = &bone.scaling,
        .animation_index = clock.animation_index,
        .time_ms = clock.time_ms,
        .animation_duration_ms = clock.animation_duration_ms,
        .global_sequences_ms = &model.global_sequences_ms,
        .has_animation_wrapped_time = clock.wrap_by_animation,
        .animation_wrapped_time_ms = clock.animation_wrapped_time_ms,
    };
    pose.scale = SampleBoneVec3Track(s_ref, M2Float4{{1.0f, 1.0f, 1.0f, 0.0f}});
  }
  if (!keyless.rotation) {
    const TrackRef<openwow::data::model::M2Quat16> r_ref{
        .track = &bone.rotation,
        .animation_index = clock.animation_index,
        .time_ms = clock.time_ms,
        .animation_duration_ms = clock.animation_duration_ms,
        .global_sequences_ms = &model.global_sequences_ms,
        .has_animation_wrapped_time = clock.wrap_by_animation,
        .animation_wrapped_time_ms = clock.animation_wrapped_time_ms,
    };
    pose.rotation = SampleQuaternionTrack(r_ref);
  }
  return pose;
}

BonePose BlendBonePose(const BonePose &source, const BonePose &target, const float blend_factor) {
  return math::BlendBonePose<math::kM2AnimationMathBackend>(source, target, blend_factor);
}

struct BoneChannelClocks {
  ChannelClock pose;

  bool has_blend_source{false};
  ChannelClock blend_source;
  float blend_factor{1.0f};
};

[[nodiscard]] BoneChannelClocks MakeSlotChannelClocks(
    const openwow::data::model::M2Model &model, const M2AnimationSlotState &slot) {
  BoneChannelClocks clocks{
      .pose = MakeChannelClock(model, static_cast<int>(slot.sequence_index),
                               SlotTimeMs(slot.time_seconds)),
  };
  if (slot.blend.IsBlending()) {
    clocks.has_blend_source = true;
    clocks.blend_source =
        MakeChannelClock(model, static_cast<int>(slot.blend.source_sequence_index),
                         SlotTimeMs(slot.blend.source_time));
    clocks.blend_factor = M2PoseBlendFactor(slot.blend);
  }
  return clocks;
}

[[nodiscard]] BoneChannelClocks MakeBaseChannelClocks(
    const openwow::data::model::M2Model &model, const int animation_index,
    const std::uint32_t time_ms,
    const std::optional<int> blend_source_animation_index,
    const std::uint32_t blend_source_time_ms, const float blend_factor) {
  BoneChannelClocks clocks{.pose = MakeChannelClock(model, animation_index, time_ms)};
  if (blend_source_animation_index.has_value()) {
    clocks.has_blend_source = true;
    clocks.blend_source =
        MakeChannelClock(model, *blend_source_animation_index, blend_source_time_ms);
    clocks.blend_factor = blend_factor;
  }
  return clocks;
}

namespace {

constexpr float kBillboardDegenerateAxisEpsilon = 2.3841858e-07f;

constexpr RenderMatrix4x4 kSphericalBillboardBasis{
    0.0f, 0.0f, -1.0f, 0.0f,
    1.0f, 0.0f,  0.0f, 0.0f,
    0.0f, 1.0f,  0.0f, 0.0f,
    0.0f, 0.0f,  0.0f, 1.0f,
};

[[nodiscard]] RenderVec3 NormalizedOrFallback(const RenderVec3 &v,
                                              const RenderVec3 &fallback) {
  const float length_squared = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  if (length_squared <= kBillboardDegenerateAxisEpsilon) {
    return fallback;
  }
  const float inverse_length = 1.0f / std::sqrt(length_squared);
  return {v[0] * inverse_length, v[1] * inverse_length, v[2] * inverse_length};
}

[[nodiscard]] RenderVec3 CrossProduct(const RenderVec3 &a, const RenderVec3 &b) {
  return {a[1] * b[2] - a[2] * b[1],
          a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

[[nodiscard]] RenderVec3 ReadRow(const RenderMatrix4x4 &m, const int row) {
  return {m[row * 4], m[row * 4 + 1], m[row * 4 + 2]};
}

void WriteRow(RenderMatrix4x4 &m, const int row, const RenderVec3 &v,
              const float scale) {
  m[row * 4] = v[0] * scale;
  m[row * 4 + 1] = v[1] * scale;
  m[row * 4 + 2] = v[2] * scale;
}

[[nodiscard]] RenderVec3 UnitAxis(const int row) {
  return {row == 0 ? 1.0f : 0.0f, row == 1 ? 1.0f : 0.0f,
          row == 2 ? 1.0f : 0.0f};
}

}

void ApplyBillboardToMatrix(
    const openwow::data::model::M2Bone &bone,
    const std::optional<RenderMatrix4x4View> &camera_inverse_view,
    float *const bone_matrix) {
  const std::uint32_t billboard_flags =
      bone.flags & openwow::data::model::kM2BoneFlagBillboardMask;
  if (!camera_inverse_view.has_value() || billboard_flags == 0u) {
    return;
  }

  RenderMatrix4x4 m{};
  std::memcpy(m.data(), bone_matrix, sizeof(m));

  RenderMatrix4x4 to_model{};
  std::copy(camera_inverse_view->begin(), camera_inverse_view->end(),
            to_model.begin());
  const RenderMatrix4x4 to_view = TransposeMatrix4x4(to_model);

  const RenderVec3 scales{
      std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]),
      std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]),
      std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10])};

  const RenderVec3 rest_pivot{bone.pivot[0], bone.pivot[1], bone.pivot[2]};
  const RenderVec3 pivot_anchor{
      rest_pivot[0] * m[0] + rest_pivot[1] * m[4] + rest_pivot[2] * m[8] + m[12],
      rest_pivot[0] * m[1] + rest_pivot[1] * m[5] + rest_pivot[2] * m[9] + m[13],
      rest_pivot[0] * m[2] + rest_pivot[1] * m[6] + rest_pivot[2] * m[10] + m[14]};

  RenderMatrix4x4 oriented{};
  if ((billboard_flags &
       openwow::data::model::kM2BoneFlagBillboardSpherical) != 0u) {

    oriented = kSphericalBillboardBasis;
  } else {
    const RenderMatrix4x4 view_space = MultiplyMatrix4x4(m, to_view);
    const int locked_row =
        (billboard_flags &
         openwow::data::model::kM2BoneFlagBillboardCylindricalLockX) != 0u ? 0
        : (billboard_flags &
           openwow::data::model::kM2BoneFlagBillboardCylindricalLockY) != 0u ? 1
                                                                             : 2;
    const int companion_row = locked_row == 0 ? 1 : locked_row == 1 ? 2 : 1;
    const int third_row = locked_row == 0 ? 2 : locked_row == 1 ? 0 : 0;

    const RenderVec3 locked =
        NormalizedOrFallback(ReadRow(view_space, locked_row), UnitAxis(locked_row));

    const RenderVec3 companion =
        locked_row == 1
            ? NormalizedOrFallback(RenderVec3{-locked[1], locked[0], 0.0f},
                                   RenderVec3{1.0f, 0.0f, 0.0f})
            : NormalizedOrFallback(RenderVec3{locked[1], -locked[0], 0.0f},
                                   RenderVec3{1.0f, 0.0f, 0.0f});
    const RenderVec3 third = locked_row == 2
                                 ? CrossProduct(locked, companion)
                                 : CrossProduct(companion, locked);

    oriented = view_space;
    WriteRow(oriented, locked_row, locked, 1.0f);
    WriteRow(oriented, companion_row, companion, 1.0f);
    WriteRow(oriented, third_row, third, 1.0f);
  }

  RenderMatrix4x4 result = MultiplyMatrix4x4(oriented, to_model);
  for (int row = 0; row < 3; ++row) {
    WriteRow(result, row,
             NormalizedOrFallback(ReadRow(result, row), UnitAxis(row)),
             scales[row]);
  }
  result[3] = 0.0f;
  result[7] = 0.0f;
  result[11] = 0.0f;
  result[12] = pivot_anchor[0] - (rest_pivot[0] * result[0] +
                                 rest_pivot[1] * result[4] +
                                 rest_pivot[2] * result[8]);
  result[13] = pivot_anchor[1] - (rest_pivot[0] * result[1] +
                                 rest_pivot[1] * result[5] +
                                 rest_pivot[2] * result[9]);
  result[14] = pivot_anchor[2] - (rest_pivot[0] * result[2] +
                                 rest_pivot[1] * result[6] +
                                 rest_pivot[2] * result[10]);
  result[15] = 1.0f;
  m = result;
  std::memcpy(bone_matrix, m.data(), sizeof(m));
}

struct BonePoseEvaluationInputs {
  const openwow::data::model::M2Model &model;

  BoneChannelClocks base_clocks;
  const std::optional<RenderMatrix4x4View> &camera_inverse_view;

  std::span<const openwow::render::m2::M2BoneBasisOverride> bone_basis_overrides{};
};

class BoneChannelClockResolver {
public:
  BoneChannelClockResolver(const openwow::data::model::M2Model &model,
                           const BoneChannelClocks &base_clocks) noexcept
      : model_(model), base_clocks_(base_clocks) {}

  [[nodiscard]] const BoneChannelClocks &For(
      const M2AnimationSlotState *const slot) {
    if (slot == nullptr) {
      return base_clocks_;
    }
    if (slot != memoized_slot_) {
      memoized_clocks_ = MakeSlotChannelClocks(model_, *slot);
      memoized_slot_ = slot;
    }
    return memoized_clocks_;
  }

private:
  const openwow::data::model::M2Model &model_;
  const BoneChannelClocks &base_clocks_;
  const M2AnimationSlotState *memoized_slot_{nullptr};
  BoneChannelClocks memoized_clocks_{};
};

[[nodiscard]] const RenderMatrix4x4 *FindBoneBasisOverride(
    const std::span<const openwow::render::m2::M2BoneBasisOverride> overrides,
    const std::size_t bone_index) {
  for (const auto &entry : overrides) {
    if (entry.bone_index == bone_index) {
      return &entry.basis;
    }
  }
  return nullptr;
}

void EvaluateBoneMatrix(
    const BonePoseEvaluationInputs &in, const std::size_t bone_index,
    const BoneChannelClocks &clocks,
    const float *const parent_matrix, float *const out) {
  const auto &bone = in.model.bones[bone_index];
  const auto &pose_index = in.model.bone_pose_index;
  const BoneTrackKeyless keyless =
      ResolveBoneTrackKeyless(pose_index, clocks.pose, bone_index);

  const RenderMatrix4x4 *const basis_override =
      FindBoneBasisOverride(in.bone_basis_overrides, bone_index);

  if (keyless.AllThree() && !clocks.has_blend_source && basis_override == nullptr &&
      pose_index.HasFinitePivot(bone_index)) {
    if (parent_matrix != nullptr) {
      Kernels::MultiplyMatrix(kRenderIdentityMatrix4x4.data(), parent_matrix, out);
    } else {
      std::memcpy(out, kRenderIdentityMatrix4x4.data(),
                  kBoneMatrixFloatCount * sizeof(float));
    }
    ApplyBillboardToMatrix(bone, in.camera_inverse_view, out);
    return;
  }

  const M2Float4 pivot{{bone.pivot[0], bone.pivot[1], bone.pivot[2], 0.0f}};

  BonePose pose = SampleBonePose(in.model, bone, clocks.pose, keyless);
  if (clocks.has_blend_source) {
    const BonePose source_pose =
        SampleBonePose(in.model, bone, clocks.blend_source,
                       ResolveBoneTrackKeyless(pose_index, clocks.blend_source, bone_index));
    pose = BlendBonePose(source_pose, pose, clocks.blend_factor);
  }

  const float *const basis_override_rows =
      basis_override != nullptr ? basis_override->data() : nullptr;

  if (parent_matrix != nullptr) {

    M2Matrix4x4 local;
    Kernels::BuildBoneLocalMatrix(pivot, pose, basis_override_rows, local.m);
    Kernels::MultiplyMatrix(local.m, parent_matrix, out);
  } else {
    Kernels::BuildBoneLocalMatrix(pivot, pose, basis_override_rows, out);
  }

  ApplyBillboardToMatrix(bone, in.camera_inverse_view, out);
}

[[nodiscard]] std::optional<RenderMatrix4x4> EvaluateBoneChainMatrix(
    const openwow::data::model::M2Model &model, const std::size_t bone_index,
    const int animation_index, const std::uint32_t time_ms,
    const std::optional<int> blend_source_animation_index,
    const std::uint32_t blend_source_time_ms, const float blend_factor,
    const std::optional<RenderMatrix4x4View> &camera_inverse_view,
    const std::span<const M2AnimationSlotState> animation_slots,
    const std::span<const openwow::render::m2::M2BoneBasisOverride>
        bone_basis_overrides = {}) {
  if (bone_index >= model.bones.size()) {
    return std::nullopt;
  }

  constexpr std::size_t kMaxBoneChainDepth = 64;
  std::array<std::size_t, kMaxBoneChainDepth> chain{};
  std::size_t chain_length = 0;
  for (std::size_t cursor = bone_index;;) {
    if (chain_length == kMaxBoneChainDepth) {
      return std::nullopt;
    }
    chain[chain_length++] = cursor;
    const auto parent = model.bones[cursor].parent;
    if (parent < 0 || static_cast<std::size_t>(parent) >= cursor) {
      break;
    }
    cursor = static_cast<std::size_t>(parent);
  }

  const std::size_t lookup_count =
      std::min(model.key_bone_lookup.size(), animation_slots.size());
  std::array<const M2AnimationSlotState *, kMaxBoneChainDepth> direct_slot{};
  for (std::size_t slot_index = 0; slot_index < lookup_count; ++slot_index) {
    const auto &candidate = animation_slots[slot_index];
    if (!candidate.active ||
        candidate.sequence_index == kInvalidM2AnimationSequenceIndex ||
        static_cast<std::size_t>(candidate.sequence_index) >=
            model.animation_durations_ms.size()) {
      continue;
    }
    const std::int16_t keyed_bone = model.key_bone_lookup[slot_index];
    if (keyed_bone < 0 || static_cast<std::size_t>(keyed_bone) >= model.bones.size()) {
      continue;
    }
    for (std::size_t position = 0; position < chain_length; ++position) {
      if (chain[position] == static_cast<std::size_t>(keyed_bone)) {
        direct_slot[position] = &candidate;
        break;
      }
    }
  }

  const BonePoseEvaluationInputs inputs{
      model,
      MakeBaseChannelClocks(model, animation_index, time_ms,
                            blend_source_animation_index, blend_source_time_ms,
                            blend_factor),
      camera_inverse_view, bone_basis_overrides};
  BoneChannelClockResolver clocks(model, inputs.base_clocks);

  M2Matrix4x4 parent{};
  M2Matrix4x4 matrix{};
  const M2AnimationSlotState *inherited_slot = nullptr;
  bool have_parent = false;
  for (std::size_t position = chain_length; position-- > 0;) {
    const M2AnimationSlotState *const own_slot = direct_slot[position];
    inherited_slot = own_slot != nullptr ? own_slot : inherited_slot;
    EvaluateBoneMatrix(inputs, chain[position], clocks.For(inherited_slot),
                       have_parent ? parent.m : nullptr, matrix.m);
    parent = matrix;
    have_parent = true;
  }

  RenderMatrix4x4 result{};
  std::memcpy(result.data(), matrix.m, sizeof(result));
  return result;
}

[[nodiscard]] bool ComputeBoneMatricesIntoInternal(
    std::vector<float> *const out,
    const openwow::data::model::M2Model &model, const int animation_index,
    const std::uint32_t time_ms, const std::optional<int> blend_source_animation_index,
    const std::uint32_t blend_source_time_ms, const float blend_factor,
    const std::optional<RenderMatrix4x4View> &camera_inverse_view,
    const std::span<const M2AnimationSlotState> animation_slots = {},
    const std::span<const openwow::render::m2::M2BoneBasisOverride>
        bone_basis_overrides = {}) {
  if (out == nullptr || model.bones.empty()) {
    return false;
  }

  const std::size_t bone_count = model.bones.size();
  std::vector<float> &matrices = *out;
  matrices.resize(bone_count * 16u);

  constexpr std::size_t kStackSlotBoneCount = 256u;
  std::array<const M2AnimationSlotState *, kStackSlotBoneCount> stack_slots;
  std::vector<const M2AnimationSlotState *> heap_slots;
  std::span<const M2AnimationSlotState *> slot_by_bone;
  const auto materialize_slot_by_bone = [&]() {
    if (!slot_by_bone.empty()) {
      return;
    }
    if (bone_count <= kStackSlotBoneCount) {
      slot_by_bone = std::span{stack_slots}.first(bone_count);
      std::fill(slot_by_bone.begin(), slot_by_bone.end(), nullptr);
    } else {
      heap_slots.assign(bone_count, nullptr);
      slot_by_bone = heap_slots;
    }
  };

  const std::size_t lookup_count =
      std::min(model.key_bone_lookup.size(), animation_slots.size());
  for (std::size_t slot_index = 0; slot_index < lookup_count; ++slot_index) {
    const auto &candidate = animation_slots[slot_index];
    if (!candidate.active ||
        candidate.sequence_index == kInvalidM2AnimationSequenceIndex ||
        static_cast<std::size_t>(candidate.sequence_index) >=
            model.animation_durations_ms.size()) {
      continue;
    }

    const std::int16_t bone_index = model.key_bone_lookup[slot_index];
    if (bone_index < 0 || static_cast<std::size_t>(bone_index) >= model.bones.size()) {
      continue;
    }

    materialize_slot_by_bone();
    slot_by_bone[static_cast<std::size_t>(bone_index)] = &candidate;
  }

  const BonePoseEvaluationInputs inputs{
      model,
      MakeBaseChannelClocks(model, animation_index, time_ms,
                            blend_source_animation_index, blend_source_time_ms,
                            blend_factor),
      camera_inverse_view, bone_basis_overrides};
  BoneChannelClockResolver clocks(model, inputs.base_clocks);
  const bool inherit_slots = !slot_by_bone.empty();
  for (std::size_t i = 0; i < bone_count; ++i) {
    const auto &bone = model.bones[i];
    const M2AnimationSlotState *bone_slot = nullptr;
    if (inherit_slots) {
      bone_slot = slot_by_bone[i];
      if (bone_slot == nullptr && bone.parent >= 0 &&
          static_cast<std::size_t>(bone.parent) < i) {
        bone_slot = slot_by_bone[static_cast<std::size_t>(bone.parent)];
      }
      slot_by_bone[i] = bone_slot;
    }

    const bool has_parent =
        bone.parent >= 0 && static_cast<std::size_t>(bone.parent) < i;

    const float *const parent_matrix =
        has_parent ? &matrices[static_cast<std::size_t>(bone.parent) * 16u] : nullptr;
    EvaluateBoneMatrix(inputs, i, clocks.For(bone_slot), parent_matrix,
                       &matrices[i * 16u]);
  }

  return true;
}

[[nodiscard]] std::optional<std::vector<float>> ComputeBoneMatricesInternal(
    const openwow::data::model::M2Model &model, const int animation_index,
    const std::uint32_t time_ms, const std::optional<int> blend_source_animation_index,
    const std::uint32_t blend_source_time_ms, const float blend_factor,
    const std::optional<RenderMatrix4x4View> &camera_inverse_view,
    const std::span<const M2AnimationSlotState> animation_slots = {},
    const std::span<const openwow::render::m2::M2BoneBasisOverride>
        bone_basis_overrides = {}) {
  std::vector<float> matrices;
  if (!ComputeBoneMatricesIntoInternal(
          &matrices, model, animation_index, time_ms, blend_source_animation_index,
          blend_source_time_ms, blend_factor, camera_inverse_view, animation_slots,
          bone_basis_overrides)) {
    return std::nullopt;
  }
  return matrices;
}

}

std::optional<std::vector<float>>
M2Animator::ComputeBoneMatrices(int animation_index, std::uint32_t time_ms,
                                const std::optional<RenderMatrix4x4View> &camera_inverse_view)
    const {
  if (model_ == nullptr) {
    return std::nullopt;
  }

  return ComputeBoneMatricesInternal(*model_, animation_index, time_ms, std::nullopt, 0u, 1.0f,
                                     camera_inverse_view);
}

std::optional<std::vector<float>> M2Animator::ComputeBlendedBoneMatrices(
    const int animation_index, const std::uint32_t time_ms,
    const int blend_source_animation_index, const std::uint32_t blend_source_time_ms,
    const float blend_factor,
    const std::optional<RenderMatrix4x4View> &camera_inverse_view) const {
  if (model_ == nullptr) {
    return std::nullopt;
  }

  return ComputeBoneMatricesInternal(*model_, animation_index, time_ms, blend_source_animation_index,
                                     blend_source_time_ms, blend_factor, camera_inverse_view);
}

bool M2Animator::ComputeLayeredBoneMatricesInto(
    std::vector<float> *const out, const int animation_index,
    const std::uint32_t time_ms,
    const std::span<const M2AnimationSlotState> animation_slots,
    const std::optional<int> blend_source_animation_index,
    const std::uint32_t blend_source_time_ms, const float blend_factor,
    const std::optional<RenderMatrix4x4View> &camera_inverse_view,
    const std::span<const M2BoneBasisOverride> bone_basis_overrides) const {
  if (model_ == nullptr) {
    return false;
  }

  return ComputeBoneMatricesIntoInternal(
      out, *model_, animation_index, time_ms, blend_source_animation_index,
      blend_source_time_ms, blend_factor, camera_inverse_view, animation_slots,
      bone_basis_overrides);
}

std::optional<std::vector<float>> M2Animator::ComputeLayeredBoneMatrices(
    const int animation_index, const std::uint32_t time_ms,
    const std::span<const M2AnimationSlotState> animation_slots,
    const std::optional<int> blend_source_animation_index,
    const std::uint32_t blend_source_time_ms, const float blend_factor,
    const std::optional<RenderMatrix4x4View> &camera_inverse_view,
    const std::span<const M2BoneBasisOverride> bone_basis_overrides) const {
  std::vector<float> matrices;
  if (!ComputeLayeredBoneMatricesInto(
          &matrices, animation_index, time_ms, animation_slots,
          blend_source_animation_index, blend_source_time_ms, blend_factor,
          camera_inverse_view, bone_basis_overrides)) {
    return std::nullopt;
  }
  return matrices;
}

std::optional<RenderMatrix4x4> M2Animator::ComputeSingleBoneMatrix(
    const std::size_t bone_index, const int animation_index, const std::uint32_t time_ms,
    const std::span<const M2AnimationSlotState> animation_slots,
    const std::optional<int> blend_source_animation_index,
    const std::uint32_t blend_source_time_ms, const float blend_factor,
    const std::optional<RenderMatrix4x4View> &camera_inverse_view,
    const std::span<const M2BoneBasisOverride> bone_basis_overrides) const {
  if (model_ == nullptr) {
    return std::nullopt;
  }
  return EvaluateBoneChainMatrix(*model_, bone_index, animation_index, time_ms,
                                 blend_source_animation_index, blend_source_time_ms,
                                 blend_factor, camera_inverse_view, animation_slots,
                                 bone_basis_overrides);
}

std::optional<M2CameraPose> M2Animator::SampleCamera(int camera_index, int animation_index,
                                                     std::uint32_t time_ms) const {
  if (model_ == nullptr)
    return std::nullopt;
  if (camera_index < 0 || static_cast<std::size_t>(camera_index) >= model_->cameras.size())
    return std::nullopt;
  const auto &cam = model_->cameras[static_cast<std::size_t>(camera_index)];

  std::uint32_t anim_duration = 0;
  if (animation_index >= 0 &&
      static_cast<std::size_t>(animation_index) < model_->animation_durations_ms.size()) {
    anim_duration = model_->animation_durations_ms[static_cast<std::size_t>(animation_index)];
  }

  const bool wrap_by_animation = anim_duration > 0u;
  const std::uint32_t animation_wrapped_time_ms =
      wrap_by_animation ? WrapTimeMs(time_ms, anim_duration) : 0u;

  const TrackRef<openwow::data::model::M2Vec3> p_ref{
      .track = &cam.position,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  const TrackRef<openwow::data::model::M2Vec3> t_ref{
      .track = &cam.target,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  const TrackRef<float> r_ref{
      .track = &cam.roll,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };

  const bx::Vec3 delta_pos = Vec3From(SampleCameraSplineTrack(
      p_ref, openwow::data::model::M2Vec3{},
      [](const auto &value0, const auto &in0, const auto &out0, const auto &value1, const auto &in1,
         const auto &out1, const float k, const std::uint16_t interpolation) {
        return BlendSplineVec3(value0, in0, out0, value1, in1, out1, k, interpolation);
      }));
  const bx::Vec3 delta_tgt = Vec3From(SampleCameraSplineTrack(
      t_ref, openwow::data::model::M2Vec3{},
      [](const auto &value0, const auto &in0, const auto &out0, const auto &value1, const auto &in1,
         const auto &out1, const float k, const std::uint16_t interpolation) {
        return BlendSplineVec3(value0, in0, out0, value1, in1, out1, k, interpolation);
      }));
  const float roll = SampleCameraSplineTrack(
      r_ref, 0.0f,
      [](const float value0, const float in0, const float out0, const float value1, const float in1,
         const float out1, const float k, const std::uint16_t interpolation) {
        return BlendSplineScalar(value0, in0, out0, value1, in1, out1, k, interpolation);
      });

  M2CameraPose pose;
  const bx::Vec3 position = bx::add(Vec3From(cam.position_base), delta_pos);
  const bx::Vec3 target = bx::add(Vec3From(cam.target_base), delta_tgt);
  pose.position = {position.x, position.y, position.z};
  pose.target = {target.x, target.y, target.z};
  pose.fov_rad = cam.fov;
  pose.near_clip = cam.near_clip;
  pose.far_clip = cam.far_clip;
  pose.roll_rad = roll;
  return pose;
}

std::optional<M2CameraPose> M2Animator::SampleBlendedCamera(
    const int camera_index, const int animation_index, const std::uint32_t time_ms,
    const int blend_source_animation_index, const std::uint32_t blend_source_time_ms,
    const float blend_factor) const {
  if (model_ == nullptr) {
    return std::nullopt;
  }
  if (camera_index < 0 || static_cast<std::size_t>(camera_index) >= model_->cameras.size()) {
    return std::nullopt;
  }

  auto current = SampleCamera(camera_index, animation_index, time_ms);
  if (!current.has_value()) {
    return std::nullopt;
  }
  if (blend_source_animation_index < 0 || blend_factor >= 1.0f) {
    return current;
  }

  auto source =
      SampleCamera(camera_index, blend_source_animation_index, blend_source_time_ms);
  if (!source.has_value()) {
    return current;
  }

  const auto &camera = model_->cameras[static_cast<std::size_t>(camera_index)];
  const float target_weight = std::clamp(blend_factor, 0.0f, 1.0f);
  const float source_weight = 1.0f - target_weight;

  auto blend_vec3 = [&](RenderVec3 &target, const RenderVec3 &source_value) {
    for (std::size_t i = 0; i < target.size(); ++i) {
      target[i] = source_value[i] * source_weight + target[i] * target_weight;
    }
  };

  if (camera.position.global_sequence < 0) {
    blend_vec3(current->position, source->position);
  }
  if (camera.target.global_sequence < 0) {
    blend_vec3(current->target, source->target);
  }
  if (camera.roll.global_sequence < 0) {
    current->roll_rad = source->roll_rad * source_weight + current->roll_rad * target_weight;
  }

  return current;
}

std::optional<M2UvTransform> M2Animator::SampleUvTransform(int uv_animation_index,
                                                           int animation_index,
                                                           std::uint32_t time_ms) const {
  if (model_ == nullptr)
    return std::nullopt;
  if (uv_animation_index < 0 ||
      static_cast<std::size_t>(uv_animation_index) >= model_->uv_animations.size()) {
    return std::nullopt;
  }
  const auto &uv = model_->uv_animations[static_cast<std::size_t>(uv_animation_index)];

  std::uint32_t anim_duration = 0;
  if (animation_index >= 0 &&
      static_cast<std::size_t>(animation_index) < model_->animation_durations_ms.size()) {
    anim_duration = model_->animation_durations_ms[static_cast<std::size_t>(animation_index)];
  }

  const bool wrap_by_animation = anim_duration > 0u;
  const std::uint32_t animation_wrapped_time_ms =
      wrap_by_animation ? WrapTimeMs(time_ms, anim_duration) : 0u;

  const TrackRef<openwow::data::model::M2Vec3> trans_ref{
      .track = &uv.translation,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  const TrackRef<openwow::data::model::M2Vec3> scale_ref{
      .track = &uv.scaling,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };

  const bx::Vec3 trans =
      Vec3From(SampleTrack(trans_ref, openwow::data::model::M2Vec3{}, [](auto a, auto b, float k) {
        openwow::data::model::M2Vec3 out;
        out.x = a.x + (b.x - a.x) * k;
        out.y = a.y + (b.y - a.y) * k;
        out.z = a.z + (b.z - a.z) * k;
        return out;
      }));
  const bx::Vec3 scale = Vec3From(SampleTrack(
      scale_ref, openwow::data::model::M2Vec3{1.0F, 1.0F, 1.0F}, [](auto a, auto b, float k) {
        openwow::data::model::M2Vec3 out;
        out.x = a.x + (b.x - a.x) * k;
        out.y = a.y + (b.y - a.y) * k;
        out.z = a.z + (b.z - a.z) * k;
        return out;
      }));

  const TrackRef<openwow::data::model::M2Quat> rotation_ref{
      .track = &uv.rotation,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  const M2Float4 rotation = SampleQuaternionTrack(rotation_ref);

  RenderMatrix4x4 matrix{kRenderIdentityMatrix4x4};

  const RenderVec3 uv_center{0.5f, 0.5f, 0.0f};
  const RenderVec3 neg_uv_center{-0.5f, -0.5f, 0.0f};
  if (TrackHasAnyValues(uv.rotation)) {
    const RenderVec4 rotation_xyzw{rotation.lane[0], rotation.lane[1], rotation.lane[2],
                                   rotation.lane[3]};
    matrix = openwow::render::PrependMatrix4x4Translation(matrix, uv_center);
    matrix = openwow::render::PrependRotationMatrix4x4Quaternion(matrix, rotation_xyzw);
    matrix = openwow::render::PrependMatrix4x4Translation(matrix, neg_uv_center);
  }
  if (TrackHasAnyValues(uv.scaling)) {
    const RenderVec3 scale_xyz{scale.x, scale.y, scale.z};
    matrix = openwow::render::PrependMatrix4x4Translation(matrix, uv_center);
    matrix = openwow::render::ScaleMatrix4x4BasisRows(matrix, scale_xyz);
    matrix = openwow::render::PrependMatrix4x4Translation(matrix, neg_uv_center);
  }
  if (TrackHasAnyValues(uv.translation)) {
    const RenderVec3 translation_xyz{trans.x, trans.y, trans.z};
    matrix = openwow::render::PrependMatrix4x4Translation(matrix, translation_xyz);
  }

  M2UvTransform out;
  out.row0[0] = matrix[0];
  out.row0[1] = matrix[4];
  out.row0[2] = matrix[12];
  out.row0[3] = 0.0f;
  out.row1[0] = matrix[1];
  out.row1[1] = matrix[5];
  out.row1[2] = matrix[13];
  out.row1[3] = 0.0f;
  return out;
}

float M2Animator::SampleAlpha(int transparency_index, int animation_index,
                              std::uint32_t time_ms) const {
  if (model_ == nullptr)
    return 1.0f;
  if (transparency_index < 0 ||
      static_cast<std::size_t>(transparency_index) >= model_->transparencies.size()) {
    return 1.0f;
  }
  const auto &t = model_->transparencies[static_cast<std::size_t>(transparency_index)];

  std::uint32_t anim_duration = 0;
  if (animation_index >= 0 &&
      static_cast<std::size_t>(animation_index) < model_->animation_durations_ms.size()) {
    anim_duration = model_->animation_durations_ms[static_cast<std::size_t>(animation_index)];
  }

  const TrackRef<std::uint16_t> a_ref{
      .track = &t.alpha,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
  };
  const std::uint16_t v = SampleTrack(
      a_ref, static_cast<std::uint16_t>(32767), [](std::uint16_t a, std::uint16_t b, float k) {
        return static_cast<std::uint16_t>(std::lround(
            static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * k));
      });
  const float alpha = std::clamp(static_cast<float>(v) / 32767.0f, 0.0f, 1.0f);
  return alpha;
}

M2ColorSample M2Animator::SampleColor(int color_index, int animation_index,
                                      std::uint32_t time_ms) const {
  M2ColorSample result;
  if (model_ == nullptr)
    return result;
  if (color_index < 0 || static_cast<std::size_t>(color_index) >= model_->colors.size()) {
    return result;
  }
  const auto &c = model_->colors[static_cast<std::size_t>(color_index)];

  std::uint32_t anim_duration = 0;
  if (animation_index >= 0 &&
      static_cast<std::size_t>(animation_index) < model_->animation_durations_ms.size()) {
    anim_duration = model_->animation_durations_ms[static_cast<std::size_t>(animation_index)];
  }

  const bool wrap_by_animation = anim_duration > 0u;
  const std::uint32_t animation_wrapped_time_ms =
      wrap_by_animation ? WrapTimeMs(time_ms, anim_duration) : 0u;

  const TrackRef<openwow::data::model::M2Vec3> c_ref{
      .track = &c.color,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  const auto v =
      SampleTrack(c_ref, openwow::data::model::M2Vec3{1.0f, 1.0f, 1.0f},
                  [](const openwow::data::model::M2Vec3 &a, const openwow::data::model::M2Vec3 &b,
                     float k) -> openwow::data::model::M2Vec3 {
                    return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k};
                  });
  result.r = std::clamp(v.x, 0.0f, 1.0f);
  result.g = std::clamp(v.y, 0.0f, 1.0f);
  result.b = std::clamp(v.z, 0.0f, 1.0f);

  const TrackRef<std::uint16_t> a_ref{
      .track = &c.alpha,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  const std::uint16_t av = SampleTrack(
      a_ref, static_cast<std::uint16_t>(32767), [](std::uint16_t a, std::uint16_t b, float k) {
        return static_cast<std::uint16_t>(std::lround(
            static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * k));
      });
  result.a = std::clamp(static_cast<float>(av) / 32767.0f, 0.0f, 1.0f);
  return result;
}

std::optional<M2RibbonEmitterSample>
M2Animator::SampleRibbonEmitter(const std::size_t ribbon_index, const int animation_index,
                                const std::uint32_t time_ms) const {
  if (model_ == nullptr || ribbon_index >= model_->ribbon_emitters.size()) {
    return std::nullopt;
  }

  std::uint32_t anim_duration = 0;
  if (animation_index >= 0 &&
      static_cast<std::size_t>(animation_index) < model_->animation_durations_ms.size()) {
    anim_duration = model_->animation_durations_ms[static_cast<std::size_t>(animation_index)];
  }

  const auto &ribbon = model_->ribbon_emitters[ribbon_index];
  M2RibbonEmitterSample sample{};

  const bool wrap_by_animation = anim_duration > 0u;
  const std::uint32_t animation_wrapped_time_ms =
      wrap_by_animation ? WrapTimeMs(time_ms, anim_duration) : 0u;

  const TrackRef<openwow::data::model::M2Vec3> color_ref{
      .track = &ribbon.color,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  const auto color =
      SampleTrack(color_ref, openwow::data::model::M2Vec3{1.0f, 1.0f, 1.0f},
                  [](const openwow::data::model::M2Vec3 &a, const openwow::data::model::M2Vec3 &b,
                     const float k) -> openwow::data::model::M2Vec3 {
                    return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k};
                  });
  sample.color = {
      std::clamp(color.x, 0.0f, 1.0f),
      std::clamp(color.y, 0.0f, 1.0f),
      std::clamp(color.z, 0.0f, 1.0f),
  };

  const TrackRef<std::int16_t> alpha_ref{
      .track = &ribbon.alpha,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  const std::int16_t alpha = SampleTrack(
      alpha_ref, static_cast<std::int16_t>(32767),
      [](const std::int16_t a, const std::int16_t b, const float k) {
        return static_cast<std::int16_t>(std::lround(
            static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * k));
      });
  sample.alpha = std::clamp(static_cast<float>(alpha) / 32767.0f, 0.0f, 1.0f);

  const auto sample_float_track = [&](const openwow::data::model::M2Track<float> &track,
                                      const float default_value) {
    const TrackRef<float> ref{
        .track = &track,
        .animation_index = animation_index,
        .time_ms = time_ms,
        .animation_duration_ms = anim_duration,
        .global_sequences_ms = &model_->global_sequences_ms,
        .has_animation_wrapped_time = wrap_by_animation,
        .animation_wrapped_time_ms = animation_wrapped_time_ms,
    };
    return SampleTrack(ref, default_value,
                       [](const float a, const float b, const float k) { return a + (b - a) * k; });
  };
  sample.height_above = sample_float_track(ribbon.height_above, 10.0f);
  sample.height_below = sample_float_track(ribbon.height_below, 10.0f);

  const TrackRef<std::uint16_t> tex_slot_ref{
      .track = &ribbon.tex_slot,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  sample.texture_slot = SampleTrack(
      tex_slot_ref, static_cast<std::uint16_t>(0),
      [](const std::uint16_t a, const std::uint16_t b, const float k) {
        return static_cast<std::uint16_t>(std::lround(
            static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * k));
      });

  const TrackRef<std::uint8_t> visibility_ref{
      .track = &ribbon.visibility,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  sample.visible = SampleTrack(visibility_ref, static_cast<std::uint8_t>(1),
                               [](const std::uint8_t a, const std::uint8_t b, const float k) {
                                 return k < 1.0f ? a : b;
                               }) != 0u;

  return sample;
}

std::optional<M2ParticleEmitterSample>
M2Animator::SampleParticleEmitter(const std::size_t emitter_index, const int animation_index,
                                  const std::uint32_t time_ms) const {
  if (model_ == nullptr || emitter_index >= model_->particle_emitters.size()) {
    return std::nullopt;
  }

  std::uint32_t anim_duration = 0;
  if (animation_index >= 0 &&
      static_cast<std::size_t>(animation_index) < model_->animation_durations_ms.size()) {
    anim_duration = model_->animation_durations_ms[static_cast<std::size_t>(animation_index)];
  }

  const auto &emitter = model_->particle_emitters[emitter_index];

  const bool wrap_by_animation = anim_duration > 0u;
  const std::uint32_t animation_wrapped_time_ms =
      wrap_by_animation ? WrapTimeMs(time_ms, anim_duration) : 0u;

  const auto sample_float_track = [&](const openwow::data::model::M2Track<float> &track) {
    const TrackRef<float> ref{
        .track = &track,
        .animation_index = animation_index,
        .time_ms = time_ms,
        .animation_duration_ms = anim_duration,
        .global_sequences_ms = &model_->global_sequences_ms,
        .has_animation_wrapped_time = wrap_by_animation,
        .animation_wrapped_time_ms = animation_wrapped_time_ms,
    };
    return SampleTrack(ref, 0.0f,
                       [](const float a, const float b, const float k) { return a + (b - a) * k; });
  };

  M2ParticleEmitterSample sample;
  sample.emission_speed = sample_float_track(emitter.emission_speed);
  sample.speed_variation = sample_float_track(emitter.speed_variation);
  sample.vertical_range = sample_float_track(emitter.vertical_range);
  sample.horizontal_range = sample_float_track(emitter.horizontal_range);
  sample.gravity = sample_float_track(emitter.gravity);
  sample.lifespan = sample_float_track(emitter.lifespan);
  sample.emission_rate = sample_float_track(emitter.emission_rate);
  sample.emission_area_length = sample_float_track(emitter.emission_area_length);
  sample.emission_area_width = sample_float_track(emitter.emission_area_width);
  sample.z_source = sample_float_track(emitter.z_source);

  const TrackRef<std::uint8_t> enabled_ref{
      .track = &emitter.enabled_in,
      .animation_index = animation_index,
      .time_ms = time_ms,
      .animation_duration_ms = anim_duration,
      .global_sequences_ms = &model_->global_sequences_ms,
      .has_animation_wrapped_time = wrap_by_animation,
      .animation_wrapped_time_ms = animation_wrapped_time_ms,
  };
  sample.enabled = SampleTrack(enabled_ref, static_cast<std::uint8_t>(1),
                               [](const std::uint8_t a, const std::uint8_t b, const float k) {
                                 return k < 1.0f ? a : b;
                               }) != 0u;

  return sample;
}

M2LightSample M2Animator::SampleLight(int light_index, int animation_index, std::uint32_t time_ms,
                                      std::span<const float> bone_matrices) const {
  M2LightSample result;
  if (model_ == nullptr)
    return result;
  if (light_index < 0 || static_cast<std::size_t>(light_index) >= model_->lights.size()) {
    return result;
  }
  const auto &light = model_->lights[static_cast<std::size_t>(light_index)];

  std::uint32_t anim_duration = 0;
  if (animation_index >= 0 &&
      static_cast<std::size_t>(animation_index) < model_->animation_durations_ms.size()) {
    anim_duration = model_->animation_durations_ms[static_cast<std::size_t>(animation_index)];
  }

  const bool wrap_by_animation = anim_duration > 0u;
  const std::uint32_t animation_wrapped_time_ms =
      wrap_by_animation ? WrapTimeMs(time_ms, anim_duration) : 0u;

  result.type = light.type;

  {
    const TrackRef<openwow::data::model::M2Vec3> ref{
        .track = &light.ambient_color,
        .animation_index = animation_index,
        .time_ms = time_ms,
        .animation_duration_ms = anim_duration,
        .global_sequences_ms = &model_->global_sequences_ms,
        .has_animation_wrapped_time = wrap_by_animation,
        .animation_wrapped_time_ms = animation_wrapped_time_ms,
    };
    const auto v =
        SampleTrack(ref, openwow::data::model::M2Vec3{0.0f, 0.0f, 0.0f},
                    [](const openwow::data::model::M2Vec3 &a, const openwow::data::model::M2Vec3 &b,
                       float k) -> openwow::data::model::M2Vec3 {
                      return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k};
                    });
    result.ambient_color = {v.x, v.y, v.z};
  }

  {
    const TrackRef<float> ref{
        .track = &light.ambient_intensity,
        .animation_index = animation_index,
        .time_ms = time_ms,
        .animation_duration_ms = anim_duration,
        .global_sequences_ms = &model_->global_sequences_ms,
        .has_animation_wrapped_time = wrap_by_animation,
        .animation_wrapped_time_ms = animation_wrapped_time_ms,
    };
    result.ambient_intensity =
        SampleTrack(ref, 0.0f, [](float a, float b, float k) { return a + (b - a) * k; });
  }

  {
    const TrackRef<openwow::data::model::M2Vec3> ref{
        .track = &light.diffuse_color,
        .animation_index = animation_index,
        .time_ms = time_ms,
        .animation_duration_ms = anim_duration,
        .global_sequences_ms = &model_->global_sequences_ms,
        .has_animation_wrapped_time = wrap_by_animation,
        .animation_wrapped_time_ms = animation_wrapped_time_ms,
    };
    const auto v =
        SampleTrack(ref, openwow::data::model::M2Vec3{0.0f, 0.0f, 0.0f},
                    [](const openwow::data::model::M2Vec3 &a, const openwow::data::model::M2Vec3 &b,
                       float k) -> openwow::data::model::M2Vec3 {
                      return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k};
                    });
    result.diffuse_color = {v.x, v.y, v.z};
  }

  {
    const TrackRef<float> ref{
        .track = &light.diffuse_intensity,
        .animation_index = animation_index,
        .time_ms = time_ms,
        .animation_duration_ms = anim_duration,
        .global_sequences_ms = &model_->global_sequences_ms,
        .has_animation_wrapped_time = wrap_by_animation,
        .animation_wrapped_time_ms = animation_wrapped_time_ms,
    };
    result.diffuse_intensity =
        SampleTrack(ref, 0.0f, [](float a, float b, float k) { return a + (b - a) * k; });
  }

  {
    const TrackRef<float> ref{
        .track = &light.attenuation_start,
        .animation_index = animation_index,
        .time_ms = time_ms,
        .animation_duration_ms = anim_duration,
        .global_sequences_ms = &model_->global_sequences_ms,
        .has_animation_wrapped_time = wrap_by_animation,
        .animation_wrapped_time_ms = animation_wrapped_time_ms,
    };
    result.attenuation_start =
        SampleTrack(ref, 0.0f, [](float a, float b, float k) { return a + (b - a) * k; });
  }

  {
    const TrackRef<float> ref{
        .track = &light.attenuation_end,
        .animation_index = animation_index,
        .time_ms = time_ms,
        .animation_duration_ms = anim_duration,
        .global_sequences_ms = &model_->global_sequences_ms,
        .has_animation_wrapped_time = wrap_by_animation,
        .animation_wrapped_time_ms = animation_wrapped_time_ms,
    };
    result.attenuation_end =
        SampleTrack(ref, 0.0f, [](float a, float b, float k) { return a + (b - a) * k; });
  }

  if (light.visibility.SetCount() != 0u) {
    const TrackRef<std::uint16_t> ref{
        .track = &light.visibility,
        .animation_index = animation_index,
        .time_ms = time_ms,
        .animation_duration_ms = anim_duration,
        .global_sequences_ms = &model_->global_sequences_ms,
        .has_animation_wrapped_time = wrap_by_animation,
        .animation_wrapped_time_ms = animation_wrapped_time_ms,
    };
    const std::uint16_t vis =
        SampleTrack(ref, static_cast<std::uint16_t>(0),
                    [](std::uint16_t a, std::uint16_t , float ) { return a; });
    result.visible = (vis != 0);
  }

  RenderVec3 position{light.position[0], light.position[1], light.position[2]};

  if (light.bone >= 0) {
    const auto bone_matrix = BoneMatrixAt(bone_matrices, static_cast<std::size_t>(light.bone));
    if (bone_matrix.has_value()) {
      position = TransformPoint(RenderVec3View{position}, *bone_matrix);
    }
  }

  result.position = position;

  return result;
}

std::vector<M2TriggeredEvent> M2Animator::CollectTriggeredEvents(
    int animation_index, std::uint32_t previous_time_ms, std::uint32_t current_time_ms,
    std::span<const float> bone_matrices,
    const std::optional<RenderMatrix4x4> &model_matrix) const {
  std::vector<M2TriggeredEvent> triggered;
  if (model_ == nullptr || model_->events.empty() || current_time_ms <= previous_time_ms) {
    return triggered;
  }

  std::uint32_t animation_duration_ms = 0;
  if (animation_index >= 0 &&
      static_cast<std::size_t>(animation_index) < model_->animation_durations_ms.size()) {
    animation_duration_ms =
        model_->animation_durations_ms[static_cast<std::size_t>(animation_index)];
  }

  for (const auto &event : model_->events) {
    const auto *event_times_ms = ResolveDiscreteTrackTimes(&event.timings, animation_index);
    if (event_times_ms == nullptr || event_times_ms->empty()) {
      continue;
    }

    const std::uint32_t period_ms = ResolveDiscreteTrackPeriodMs(
        event.timings, animation_duration_ms, model_->global_sequences_ms, *event_times_ms);
    if (period_ms == 0) {
      AppendDiscreteEventsForInterval(&triggered, event, *event_times_ms, previous_time_ms,
                                      current_time_ms, 0, current_time_ms, model_, animation_index,
                                      bone_matrices, model_matrix);
      continue;
    }

    const std::uint64_t start_loop = previous_time_ms / period_ms;
    const std::uint64_t end_loop = current_time_ms / period_ms;
    const std::uint32_t start_local_ms = previous_time_ms % period_ms;
    const std::uint32_t end_local_ms = current_time_ms % period_ms;

    if (start_loop == end_loop) {
      AppendDiscreteEventsForInterval(&triggered, event, *event_times_ms, start_local_ms,
                                      end_local_ms, start_loop * period_ms, current_time_ms, model_,
                                      animation_index, bone_matrices, model_matrix);
      continue;
    }

    AppendDiscreteEventsForInterval(&triggered, event, *event_times_ms, start_local_ms, period_ms,
                                    start_loop * period_ms, current_time_ms, model_,
                                    animation_index, bone_matrices, model_matrix);
    for (std::uint64_t loop = start_loop + 1; loop < end_loop; ++loop) {
      AppendDiscreteEventsForInterval(&triggered, event, *event_times_ms, 0, period_ms,
                                      loop * period_ms, current_time_ms, model_, animation_index,
                                      bone_matrices, model_matrix);
    }
    AppendDiscreteEventsForInterval(&triggered, event, *event_times_ms, 0, end_local_ms,
                                    end_loop * period_ms, current_time_ms, model_, animation_index,
                                    bone_matrices, model_matrix);
  }

  return triggered;
}

}
