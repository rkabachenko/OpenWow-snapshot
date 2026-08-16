#include "openwow/render/m2/m2_animation_runtime.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/render/m2/m2_animation_selection.h"
#include "openwow/render/m2/m2_animator.h"
#include "openwow/render/m2/m2_spatial_queries.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace openwow::render::m2 {

namespace {

constexpr float kMillisecondsToSeconds = 0.001f;

constexpr double kAnimationInstanceMicroseconds = 1.15;

[[nodiscard]] std::optional<M2AnimationAliasInfo> LookupAnimationAliasInfo(
    const std::uint32_t animation_id, const data::dbc::DbcLoader *dbc) {
  if (dbc == nullptr) {
    return std::nullopt;
  }
  const auto *entry = dbc->animation_data().LookupEntry(animation_id);
  if (entry == nullptr) {
    return std::nullopt;
  }
  return M2AnimationAliasInfo{.flags = entry->flags,
                              .target_animation_id = entry->fallback};
}

[[nodiscard]] std::uint32_t SeedForInstanceVariantRandom(
    const std::uint32_t instance_id) noexcept {
  const std::uint32_t seed = instance_id * 0x9E3779B1u + 0x6D2B79F5u;
  return seed == 0u ? 0x1u : seed;
}

[[nodiscard]] std::uint32_t DrawInstanceVariantRandom(
    detail::M2Instance &instance, const std::uint32_t instance_id) noexcept {
  if (!instance.animation_variant_random_seeded) {
    instance.animation_variant_random.Seed(
        SeedForInstanceVariantRandom(instance_id));
    instance.animation_variant_random_seeded = true;
  }
  return instance.animation_variant_random.Next();
}

[[nodiscard]] std::uint32_t ResolveSequenceDurationMs(
    const data::model::M2Model &model,
    const std::uint16_t sequence_index) noexcept {
  if (sequence_index == kInvalidM2AnimationSequenceIndex ||
      static_cast<std::size_t>(sequence_index) >=
          model.animation_durations_ms.size()) {
    return 0u;
  }
  return model.animation_durations_ms[sequence_index];
}

[[nodiscard]] M2AnimationSlotState BuildAnimationSlotState(
    const data::model::M2Model &model, const M2AnimationRequest &request,
    const M2AnimationSelectionResult &selection,
    const M2AnimationSlotState &previous) noexcept {
  M2AnimationSlotState state;
  state.active = selection.resolved;
  state.requested_animation_id = request.animation_id;
  state.resolved_animation_id =
      selection.resolved ? selection.resolved_animation_id : request.animation_id;
  state.sequence_index = selection.resolved
                             ? selection.resolved_sequence_index
                             : kInvalidM2AnimationSequenceIndex;
  state.sub_animation_index = selection.resolved
                                  ? selection.resolved_sub_animation_index
                                  : request.sub_animation_index;
  state.animation_lookup_id = request.animation_lookup_id;
  state.animation_lookup_sequence_index = selection.resolved_lookup_sequence_index;
  state.loop_count = request.loop_count;
  state.used_random_variant = selection.used_random_variant;
  state.speed = request.speed;
  state.duration_ms = selection.resolved
                          ? ResolveSequenceDurationMs(
                                model, selection.resolved_sequence_index)
                          : 0u;

  if (selection.resolved && previous.active &&
      previous.sequence_index != kInvalidM2AnimationSequenceIndex &&
      previous.sequence_index != selection.resolved_sequence_index &&
      static_cast<std::size_t>(previous.sequence_index) <
          model.animation_sequences.size() &&
      static_cast<std::size_t>(selection.resolved_sequence_index) <
          model.animation_sequences.size()) {

    if (!request.zero_blend && previous.blend.IsBlending() &&
        (1.0f - M2PoseBlendFactor(previous.blend)) > 0.5f) {
      state.blend = previous.blend;
    } else {
      const auto &incoming =
          model.animation_sequences[selection.resolved_sequence_index];

      const float blend_time =
          request.zero_blend
              ? 0.0f
              : static_cast<float>(incoming.blend_time_ms) * kMillisecondsToSeconds;
      if (blend_time > 0.0f) {
        const auto &source = model.animation_sequences[previous.sequence_index];
        state.blend.source_sequence_index = previous.sequence_index;
        state.blend.source_time = previous.time_seconds;
        state.blend.duration = blend_time;
        state.blend.remaining = blend_time;
        state.blend.source_duration_ms = source.length_ms;
        state.blend.source_clamped_at_end =
            (source.flags &
             data::model::kM2SequenceFlagBlendSourceClampedAtEnd) != 0u;
        state.blend.source_speed = previous.speed;
      }
    }
  }
  return state;
}

void AdvancePoseBlend(M2PoseBlendState &blend,
                      const float real_delta_seconds) noexcept {
  if (!blend.IsBlending()) {
    return;
  }
  const float duration =
      static_cast<float>(blend.source_duration_ms) * kMillisecondsToSeconds;
  float time = blend.source_time + real_delta_seconds * blend.source_speed;
  if (duration <= 0.0f) {
    blend.source_time = std::max(time, 0.0f);
  } else if (blend.source_clamped_at_end) {
    blend.source_time = std::clamp(time, 0.0f, duration);
  } else {
    time = std::fmod(time, duration);
    blend.source_time = time < 0.0f ? time + duration : time;
  }
  blend.remaining = std::max(0.0f, blend.remaining - real_delta_seconds);
  if (blend.remaining == 0.0f) {
    blend.source_sequence_index = kInvalidM2AnimationSequenceIndex;
  }
}

void AssignAnimationSlot(detail::M2Instance &instance, const std::size_t index,
                         M2AnimationSlotState new_state) {
  auto &slot = instance.animation_slots[index];
  const bool was_active = slot.active;
  const auto previous_sequence = slot.sequence_index;
  slot = std::move(new_state);
  if (slot.active != was_active) {
    instance.active_animation_slot_count += slot.active ? 1u : -1u;
  }

  if (!slot.active || slot.sequence_index != previous_sequence) {
    instance.slot_processed_event_time_ms[index] = 0u;
  }
}

void QueueBaseLoopCompletions(detail::M2Instance &instance,
                              const float previous_time,
                              std::vector<PendingCompletion> *completions) {
  if (instance.base_clock_sample_driven ||
      instance.current_animation_duration_ms == 0u ||
      !(instance.animation_speed > 0.0f)) {
    return;
  }
  const float duration =
      static_cast<float>(instance.current_animation_duration_ms) *
      kMillisecondsToSeconds;
  if (instance.current_animation_play_once) {
    if (!instance.animation_completion_fired && previous_time < duration &&
        instance.animation_time >= duration) {
      instance.animation_completion_fired = true;
      if (instance.animation_completion_callback && completions != nullptr) {
        completions->push_back({instance.animation_completion_callback,
                                instance.resolved_animation_id});
      }
    }
    return;
  }
  if (instance.animation_time < duration) {
    return;
  }

  const auto cycles_before = static_cast<std::uint64_t>(
      static_cast<double>(std::max(previous_time, 0.0f)) /
      static_cast<double>(duration));
  const auto cycles_after = static_cast<std::uint64_t>(
      static_cast<double>(instance.animation_time) /
      static_cast<double>(duration));
  if (cycles_after <= cycles_before) {
    return;
  }
  const std::uint64_t crossed = cycles_after - cycles_before;
  instance.animation_completion_fired = true;
  instance.base_loop_boundaries_pending += static_cast<std::uint32_t>(crossed);
  if (instance.animation_completion_callback && completions != nullptr) {
    for (std::uint64_t i = 0; i < crossed; ++i) {
      completions->push_back({instance.animation_completion_callback,
                              instance.resolved_animation_id});
    }
  }
}

struct DeferredCallbackRunner {
  M2AnimationRuntime::DeferredCallbacks *events;
  ~DeferredCallbackRunner() {
    if (events == nullptr) {
      return;
    }
    for (auto &callback : *events) {
      callback();
    }
  }
};

[[nodiscard]] bool RequiresAnimationTick(
    const detail::M2Instance &instance,
    const detail::M2ModelResource *model_resource) noexcept {

  if (instance.active_animation_slot_count > 0u) {
    return true;
  }
  if (instance.pose_blend_remaining > 0.0f) {
    return true;
  }
  if (model_resource == nullptr) {

    return true;
  }

  if (model_resource->has_live_global_sequence) {
    return true;
  }
  if (instance.animation_speed == 0.0f) {

    return false;
  }
  const auto &model = model_resource->model_data;
  const int animation_index = ResolveM2RenderableAnimationIndex(instance);
  const bool resolved_sequence_has_duration =
      animation_index >= 0 &&
      static_cast<std::size_t>(animation_index) < model.animation_durations_ms.size() &&
      model.animation_durations_ms[static_cast<std::size_t>(animation_index)] > 0u;
  return resolved_sequence_has_duration;
}

void AdvancePoseBlendSourceClock(detail::M2Instance &instance,
                                 const float real_delta_seconds) noexcept {
  if (instance.pose_blend_source_sequence_index ==
      kInvalidM2AnimationSequenceIndex) {
    return;
  }
  const float duration = static_cast<float>(instance.pose_blend_source_duration_ms) *
                         kMillisecondsToSeconds;
  float time = instance.pose_blend_source_time +
               real_delta_seconds * instance.pose_blend_source_speed;
  if (duration <= 0.0f) {
    instance.pose_blend_source_time = std::max(time, 0.0f);
    return;
  }
  if (instance.pose_blend_source_clamped_at_end) {
    instance.pose_blend_source_time = std::clamp(time, 0.0f, duration);
    return;
  }
  time = std::fmod(time, duration);
  if (time < 0.0f) {
    time += duration;
  }
  instance.pose_blend_source_time = time;
}

void AdvanceAnimation(detail::M2Instance &instance, const float delta_time,
                      std::vector<PendingCompletion> *completions) {
  const float scaled_delta = delta_time * instance.animation_speed;
  const float previous_time = instance.animation_time;
  instance.animation_time += scaled_delta;

  std::uint32_t remaining_active_slots = instance.active_animation_slot_count;
  for (std::size_t slot_index = 0;
       remaining_active_slots > 0u && slot_index < instance.animation_slots.size();
       ++slot_index) {
    auto &slot = instance.animation_slots[slot_index];
    if (!slot.active) {
      continue;
    }
    --remaining_active_slots;
    slot.time_seconds += delta_time * slot.speed;
    const float duration =
        static_cast<float>(slot.duration_ms) * kMillisecondsToSeconds;
    if (duration > 0.0f &&
        (slot.time_seconds >= duration || slot.time_seconds < 0.0f)) {

      if (slot.time_seconds >= duration) {
        instance.slot_loop_wrapped_mask |= 1ull << slot_index;
      }
      slot.time_seconds = std::fmod(slot.time_seconds, duration);
      if (slot.time_seconds < 0.0f) {
        slot.time_seconds += duration;
      }
    } else if (slot.time_seconds < 0.0f) {
      slot.time_seconds = 0.0f;
    }

    AdvancePoseBlend(slot.blend, delta_time);

    ++instance.animation_state_generation;
  }
  if (instance.pose_blend_remaining > 0.0f) {

    AdvancePoseBlendSourceClock(instance, delta_time);
    instance.pose_blend_remaining =
        std::max(0.0f, instance.pose_blend_remaining - delta_time);
    if (instance.pose_blend_remaining == 0.0f) {
      instance.pose_blend_source_sequence_index =
          kInvalidM2AnimationSequenceIndex;
    }
  }
  QueueBaseLoopCompletions(instance, previous_time, completions);
}

}

M2AnimationRuntime::M2AnimationRuntime(
    M2SystemMutex &mutex, ModelStore &models, M2InstanceStore &instances,
    const data::dbc::DbcLoader *&dbc, M2SequenceStreamer &sequence_streamer)
    : mutex_(mutex), models_(models), instances_(instances), dbc_(dbc),
      sequence_streamer_(sequence_streamer),
      resume_pending_sequence_loads_(
          [this](const std::uint32_t model_id, DeferredCallbacks *callbacks) {
            ResumePendingAnimationsLocked(model_id, callbacks);
          }) {}

void M2AnimationRuntime::PumpSequenceLoads() {
  sequence_streamer_.Pump(resume_pending_sequence_loads_);
}

M2ResultStatus M2AnimationRuntime::SetAnimationChangedCallback(
    const std::uint32_t instance_id, M2AnimationChangedCallback callback) {
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  it->second->animation_changed_callback = std::move(callback);
  return M2ResultStatus::kReady;
}

M2ResultStatus M2AnimationRuntime::ClearAnimationChangedCallback(
    const std::uint32_t instance_id) {
  return SetAnimationChangedCallback(instance_id, {});
}

M2ResultStatus M2AnimationRuntime::SetAnimationRequestCallback(
    const std::uint32_t instance_id, M2AnimationRequestCallback callback) {
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  it->second->animation_request_callback = std::move(callback);
  return M2ResultStatus::kReady;
}

M2ResultStatus M2AnimationRuntime::ClearAnimationRequestCallback(
    const std::uint32_t instance_id) {
  return SetAnimationRequestCallback(instance_id, {});
}

M2ResultStatus M2AnimationRuntime::SetAnimationCompletionCallback(
    const std::uint32_t instance_id, M2AnimationCompletionCallback callback) {
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  auto &instance = *it->second;
  instance.animation_completion_callback = std::move(callback);
  const float duration =
      static_cast<float>(instance.current_animation_duration_ms) *
      kMillisecondsToSeconds;
  instance.animation_completion_fired =
      instance.current_animation_duration_ms != 0u &&
      instance.animation_time >= duration;
  return M2ResultStatus::kReady;
}

M2ResultStatus M2AnimationRuntime::ClearAnimationCompletionCallback(
    const std::uint32_t instance_id) {
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  it->second->animation_completion_callback = {};
  it->second->animation_completion_fired = false;
  return M2ResultStatus::kReady;
}

M2ResultStatus M2AnimationRuntime::SetTriggeredEventCallback(
    const std::uint32_t instance_id, M2TriggeredEventCallback callback) {
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  it->second->triggered_event_callback = std::move(callback);
  return M2ResultStatus::kReady;
}

M2ResultStatus M2AnimationRuntime::ClearTriggeredEventCallback(
    const std::uint32_t instance_id) {
  return SetTriggeredEventCallback(instance_id, {});
}

void M2AnimationRuntime::ApplyAnimationSelectionLocked(
    detail::M2Instance &instance, const data::model::M2Model &model,
    const M2AnimationRequest &request,
    const M2AnimationSelectionResult &selection) {
  const std::uint16_t previous_sequence =
      instance.pose_playback_bound ? instance.pose_sequence_index
                                   : instance.animation_sequence_index;
  const float previous_time = instance.animation_time;
  const float previous_speed = instance.animation_speed;

  const std::uint16_t previous_blend_source =
      instance.pose_blend_source_sequence_index;
  const float previous_blend_source_time = instance.pose_blend_source_time;
  const float previous_blend_duration = instance.pose_blend_duration;
  const float previous_blend_remaining = instance.pose_blend_remaining;
  const std::uint32_t previous_blend_source_duration_ms =
      instance.pose_blend_source_duration_ms;
  const bool previous_blend_source_clamped =
      instance.pose_blend_source_clamped_at_end;
  const float previous_blend_source_speed =
      instance.pose_blend_source_speed;
  instance.animation_id = request.animation_id;
  instance.resolved_animation_id = request.animation_id;
  instance.animation_sequence_index = kInvalidM2AnimationSequenceIndex;
  instance.animation_sub_index = -1;
  instance.animation_lookup_id = request.animation_lookup_id;
  instance.animation_lookup_sequence_index = selection.resolved_lookup_sequence_index;
  instance.animation_loop_count = request.loop_count;
  instance.animation_used_random_variant = false;
  instance.animation_speed = request.speed;
  instance.animation_time = 0.0f;

  instance.animation_sample_clock_rebased = true;
  instance.current_animation_duration_ms = 0u;
  instance.current_animation_play_once = false;

  instance.base_loop_boundaries_pending = 0u;
  instance.animation_completion_fired = false;
  instance.processed_event_time_ms = 0u;
  instance.pose_playback_bound = false;
  instance.pose_sequence_index = kInvalidM2AnimationSequenceIndex;
  instance.pose_blend_source_sequence_index = kInvalidM2AnimationSequenceIndex;
  instance.pose_blend_source_time = 0.0f;
  instance.pose_blend_duration = 0.0f;
  instance.pose_blend_remaining = 0.0f;
  instance.pose_blend_source_duration_ms = 0u;
  instance.pose_blend_source_clamped_at_end = false;
  instance.pose_blend_source_speed = 1.0f;
  if (!selection.resolved) {
    return;
  }
  instance.resolved_animation_id = selection.resolved_animation_id;
  instance.animation_sequence_index = selection.resolved_sequence_index;
  instance.animation_sub_index = selection.resolved_sub_animation_index;
  instance.animation_used_random_variant = selection.used_random_variant;
  if (selection.resolved_sequence_index < model.animation_sequences.size()) {
    const auto &sequence = model.animation_sequences[selection.resolved_sequence_index];
    instance.current_animation_duration_ms = sequence.length_ms;
    instance.current_animation_play_once =
        (sequence.flags & data::model::kM2SequenceFlagPlayOnce) != 0u;
    instance.pose_playback_bound = true;
    instance.pose_sequence_index = selection.resolved_sequence_index;

    const float blend_time =
        request.zero_blend
            ? 0.0f
            : static_cast<float>(sequence.blend_time_ms) * kMillisecondsToSeconds;
    if (blend_time > 0.0f &&
        previous_sequence != kInvalidM2AnimationSequenceIndex &&
        previous_sequence < model.animation_sequences.size() &&
        previous_sequence != selection.resolved_sequence_index) {

      const bool keep_existing_blend = [&] {
        if (previous_blend_source == kInvalidM2AnimationSequenceIndex ||
            previous_blend_duration <= 0.0f ||
            previous_blend_remaining <= 0.0f) {
          return false;
        }
        float fraction = previous_blend_remaining / previous_blend_duration;
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        return fraction * fraction * (fraction * -2.0f + 3.0f) > 0.5f;
      }();
      if (keep_existing_blend) {
        instance.pose_blend_source_sequence_index = previous_blend_source;
        instance.pose_blend_source_time = previous_blend_source_time;
        instance.pose_blend_duration = previous_blend_duration;
        instance.pose_blend_remaining = previous_blend_remaining;
        instance.pose_blend_source_duration_ms =
            previous_blend_source_duration_ms;
        instance.pose_blend_source_clamped_at_end =
            previous_blend_source_clamped;
        instance.pose_blend_source_speed = previous_blend_source_speed;
      } else {
        instance.pose_blend_source_sequence_index = previous_sequence;
        instance.pose_blend_source_time = previous_time;
        instance.pose_blend_duration = blend_time;
        instance.pose_blend_remaining = blend_time;

        const auto &source_sequence =
            model.animation_sequences[previous_sequence];
        instance.pose_blend_source_duration_ms = source_sequence.length_ms;
        instance.pose_blend_source_clamped_at_end =
            (source_sequence.flags &
             data::model::kM2SequenceFlagBlendSourceClampedAtEnd) != 0u;
        instance.pose_blend_source_speed = previous_speed;
      }
    }
  }
}

void M2AnimationRuntime::CollectSampledAnimationEventCallbacksLocked(
    detail::M2Instance &instance, const detail::M2ModelResource &resource,
    DeferredCallbacks *callbacks) {
  if (callbacks == nullptr || !instance.triggered_event_callback ||
      resource.model_data.events.empty()) {
    return;
  }
  const auto sample_ms =
      static_cast<std::uint32_t>(std::max(instance.animation_time, 0.0f) * 1000.0f);
  const std::uint32_t previous_ms = instance.processed_event_time_ms;
  instance.processed_event_time_ms = sample_ms;
  if (sample_ms == previous_ms) {
    return;
  }

  M2Animator animator(&resource.model_data);
  const int animation_index = ResolveM2RenderableAnimationIndex(instance);
  const auto model_matrix = ComputeM2ModelMatrix(instance);

  std::vector<M2TriggeredEvent> events;
  if (sample_ms > previous_ms) {
    events = animator.CollectTriggeredEvents(animation_index, previous_ms,
                                             sample_ms, {}, model_matrix);
  } else {

    const std::uint32_t duration_ms = instance.current_animation_duration_ms;
    if (duration_ms > previous_ms) {
      events = animator.CollectTriggeredEvents(animation_index, previous_ms,
                                               duration_ms, {}, model_matrix);
    }
    auto head = animator.CollectTriggeredEvents(animation_index, 0, sample_ms,
                                                {}, model_matrix);
    events.insert(events.end(), head.begin(), head.end());
  }

  const auto callback = instance.triggered_event_callback;
  for (const auto &event : events) {
    callbacks->push_back([callback, event] { callback(event); });
  }
}

void M2AnimationRuntime::CollectSlotAnimationEventCallbacksLocked(
    detail::M2Instance &instance, const detail::M2ModelResource &resource,
    const std::uint32_t slot_index, DeferredCallbacks *callbacks) {
  if (callbacks == nullptr || !instance.triggered_event_callback ||
      resource.model_data.events.empty() ||
      slot_index >= instance.animation_slots.size()) {
    return;
  }
  const auto &slot = instance.animation_slots[slot_index];
  if (!slot.active ||
      slot.sequence_index == kInvalidM2AnimationSequenceIndex) {

    instance.slot_processed_event_time_ms[slot_index] = 0u;
    return;
  }
  const auto sample_ms = static_cast<std::uint32_t>(
      std::max(slot.time_seconds, 0.0f) * 1000.0f);
  const std::uint32_t previous_ms =
      instance.slot_processed_event_time_ms[slot_index];
  instance.slot_processed_event_time_ms[slot_index] = sample_ms;
  if (sample_ms == previous_ms) {
    return;
  }

  M2Animator animator(&resource.model_data);
  const int animation_index = static_cast<int>(slot.sequence_index);
  const auto model_matrix = ComputeM2ModelMatrix(instance);

  std::vector<M2TriggeredEvent> events;
  if (sample_ms > previous_ms) {
    events = animator.CollectTriggeredEvents(animation_index, previous_ms,
                                             sample_ms, {}, model_matrix);
  } else {

    const std::uint32_t duration_ms = slot.duration_ms;
    if (duration_ms > previous_ms) {
      events = animator.CollectTriggeredEvents(animation_index, previous_ms,
                                               duration_ms, {}, model_matrix);
    }
    auto head = animator.CollectTriggeredEvents(animation_index, 0, sample_ms,
                                                {}, model_matrix);
    events.insert(events.end(), head.begin(), head.end());
  }

  const auto callback = instance.triggered_event_callback;
  for (const auto &event : events) {
    callbacks->push_back([callback, event] { callback(event); });
  }
}

void M2AnimationRuntime::ApplySequenceSampleLocked(
    detail::M2Instance &instance, const data::model::M2Model &model,
    const std::uint16_t sequence_index, const std::uint32_t time_ms,
    const float speed) {
  const auto &sequence = model.animation_sequences[sequence_index];
  instance.animation_id = sequence.animation_id;
  instance.resolved_animation_id = sequence.animation_id;
  instance.animation_sequence_index = sequence_index;
  instance.animation_sub_index = sequence.sub_animation_id;
  instance.animation_lookup_id = -1;
  instance.animation_time = static_cast<float>(time_ms) * kMillisecondsToSeconds;
  instance.animation_speed = speed;
  instance.current_animation_duration_ms = sequence.length_ms;
  instance.current_animation_play_once =
      (sequence.flags & data::model::kM2SequenceFlagPlayOnce) != 0u;
  instance.base_loop_boundaries_pending = 0u;

  instance.base_clock_sample_driven = true;
  instance.animation_completion_fired =
      sequence.length_ms != 0u && time_ms >= sequence.length_ms;
  instance.pose_playback_bound = true;
  instance.pose_sequence_index = sequence_index;
  instance.pose_blend_source_sequence_index = kInvalidM2AnimationSequenceIndex;
  instance.pose_blend_source_time = 0.0f;
  instance.pose_blend_duration = 0.0f;
  instance.pose_blend_remaining = 0.0f;
  instance.pose_blend_source_duration_ms = 0u;
  instance.pose_blend_source_clamped_at_end = false;
  instance.pose_blend_source_speed = 1.0f;
}

bool M2AnimationRuntime::RerollBaseVariantOnLoopLocked(
    detail::M2Instance &instance, const detail::M2ModelResource &resource,
    const std::uint32_t model_id, const std::uint32_t instance_id) {
  const auto &model = resource.model_data;
  if (instance.animation_sequence_index == kInvalidM2AnimationSequenceIndex ||
      static_cast<std::size_t>(instance.animation_sequence_index) >=
          model.animation_sequences.size()) {
    return false;
  }
  if ((model.animation_sequences[instance.animation_sequence_index].flags &
       data::model::kM2SequenceFlagPlayOnce) != 0u) {
    return false;
  }

  if (!instance.animation_used_random_variant) {
    return false;
  }
  if (CountM2AnimationVariants(model, instance.resolved_animation_id) <= 1u) {
    return false;
  }
  const M2AnimationRequest request{
      .animation_lookup_id = instance.animation_lookup_id,
      .animation_id = instance.animation_id,
      .sub_animation_index = -1,
      .loop_count = instance.animation_loop_count,
      .speed = instance.animation_speed,
  };
  const auto selection = ResolveM2AnimationSelection(
      model, request, DrawInstanceVariantRandom(instance, instance_id),
      [this](const auto id) { return LookupAnimationAliasInfo(id, dbc_); });
  if (!selection.resolved ||
      selection.resolved_sequence_index == kInvalidM2AnimationSequenceIndex) {
    return false;
  }
  const auto residency = sequence_streamer_.EnsureResidentLocked(
      model_id, selection.resolved_sequence_index, resume_pending_sequence_loads_);
  if (residency != M2ResultStatus::kReady) {
    return false;
  }
  ApplyAnimationSelectionLocked(instance, model, request, selection);
  return true;
}

bool M2AnimationRuntime::RerollSlotVariantOnLoopLocked(
    detail::M2Instance &instance, const detail::M2ModelResource &resource,
    const std::uint32_t model_id, const std::uint32_t instance_id,
    const std::uint32_t slot_index) {
  auto &slot = instance.animation_slots[slot_index];
  if (!slot.active || !slot.used_random_variant ||
      slot.sequence_index == kInvalidM2AnimationSequenceIndex) {
    return false;
  }
  const auto &model = resource.model_data;
  if (static_cast<std::size_t>(slot.sequence_index) >=
      model.animation_sequences.size()) {
    return false;
  }
  if ((model.animation_sequences[slot.sequence_index].flags &
       data::model::kM2SequenceFlagPlayOnce) != 0u) {
    return false;
  }
  if (CountM2AnimationVariants(model, slot.resolved_animation_id) <= 1u) {
    return false;
  }
  const M2AnimationRequest request{
      .animation_lookup_id = slot.animation_lookup_id,
      .animation_id = slot.requested_animation_id,
      .sub_animation_index = -1,
      .loop_count = slot.loop_count,
      .speed = slot.speed,
  };
  const auto selection = ResolveM2AnimationSelection(
      model, request, DrawInstanceVariantRandom(instance, instance_id),
      [this](const auto id) { return LookupAnimationAliasInfo(id, dbc_); });
  if (!selection.resolved ||
      selection.resolved_sequence_index == kInvalidM2AnimationSequenceIndex) {
    return false;
  }
  const auto residency = sequence_streamer_.EnsureResidentLocked(
      model_id, selection.resolved_sequence_index, resume_pending_sequence_loads_);
  if (residency != M2ResultStatus::kReady) {
    return false;
  }
  const float overshoot_seconds = slot.time_seconds;
  AssignAnimationSlot(
      instance, slot_index,
      BuildAnimationSlotState(model, request, selection, slot));
  instance.animation_slots[slot_index].time_seconds = overshoot_seconds;
  ++instance.animation_state_generation;
  return true;
}

void M2AnimationRuntime::ProcessLoopBoundariesLocked(
    detail::M2Instance &instance, const std::uint32_t instance_id) {
  const std::uint32_t base_boundaries =
      std::exchange(instance.base_loop_boundaries_pending, 0u);
  std::uint64_t mask = std::exchange(instance.slot_loop_wrapped_mask, 0u);
  if (base_boundaries == 0u && mask == 0u) {
    return;
  }
  const auto model_it = models_.find(instance.model_id);
  if (model_it == models_.end() || !model_it->second->loaded) {
    return;
  }
  const detail::M2ModelResource &resource = *model_it->second;

  if (base_boundaries != 0u && !instance.base_clock_sample_driven &&
      !instance.animation_completion_callback &&
      instance.current_animation_duration_ms != 0u) {
    const float pre_reroll_time = instance.animation_time;
    const float pre_reroll_duration =
        static_cast<float>(instance.current_animation_duration_ms) *
        kMillisecondsToSeconds;
    if (RerollBaseVariantOnLoopLocked(instance, resource, instance.model_id,
                                      instance_id)) {

      instance.animation_time =
          std::fmod(std::max(pre_reroll_time, 0.0f), pre_reroll_duration);
      ++instance.animation_state_generation;
    }
  }
  while (mask != 0u) {
    const auto slot_index =
        static_cast<std::uint32_t>(std::countr_zero(mask));
    mask &= mask - 1u;
    const auto &slot = instance.animation_slots[slot_index];

    if (!slot.active || slot.loop_count == 0) {
      continue;
    }
    (void)RerollSlotVariantOnLoopLocked(instance, resource,
                                        instance.model_id, instance_id,
                                        slot_index);
  }
}

M2ResultStatus M2AnimationRuntime::SetAnimationRequest(
    const std::uint32_t instance_id, const M2AnimationRequest &request) {
  PumpSequenceLoads();
  M2AnimationChangedCallback callback;
  M2AnimationRequestCallback request_callback;
  std::uint32_t callback_animation_id = request.animation_id;
  M2AnimationRequestEvent request_event{.requested_animation_id = request.animation_id};
  M2ResultStatus result = M2ResultStatus::kUnsupported;
  bool invoke_callback = false;
  {
    std::lock_guard lock(mutex_);
    const auto instance_it = instances_.find(instance_id);
    if (instance_it == instances_.end()) {
      return M2ResultStatus::kFailed;
    }
    const auto model_it = models_.find(instance_it->second->model_id);
    if (model_it == models_.end() || !model_it->second->loaded) {
      result = M2ResultStatus::kNotReady;
    } else {
      const auto selection = ResolveM2AnimationSelection(
          model_it->second->model_data, request,
          DrawInstanceVariantRandom(*instance_it->second, instance_id),
          [this](const auto id) { return LookupAnimationAliasInfo(id, dbc_); });
      if (selection.resolved) {
        result = sequence_streamer_.EnsureResidentLocked(
            instance_it->second->model_id, selection.resolved_sequence_index,
            resume_pending_sequence_loads_);
        if (result == M2ResultStatus::kNotReady) {
          instance_it->second->pending_base_animation =
              detail::M2Instance::PendingBaseAnimation{
                  .kind = detail::M2Instance::PendingBaseAnimation::Kind::kRequest,
                  .request = request,
                  .selection = selection,
                  .sequence_index = selection.resolved_sequence_index,
                  .speed = request.speed};
        } else if (result == M2ResultStatus::kReady) {
          instance_it->second->pending_base_animation.reset();
          ApplyAnimationSelectionLocked(*instance_it->second,
                                        model_it->second->model_data, request,
                                        selection);
          invoke_callback = true;
        } else {
          instance_it->second->pending_base_animation.reset();
        }
      } else {
        instance_it->second->pending_base_animation.reset();
        ApplyAnimationSelectionLocked(*instance_it->second,
                                      model_it->second->model_data, request,
                                      selection);
        invoke_callback = true;
        if (model_it->second->model_data.animation_sequences.empty()) {
          result = M2ResultStatus::kReady;
        }
      }
    }
    callback_animation_id = instance_it->second->resolved_animation_id;
    callback = instance_it->second->animation_changed_callback;
    request_callback = instance_it->second->animation_request_callback;
    request_event.resolved_animation_id = instance_it->second->resolved_animation_id;
    request_event.resolved_sub_animation_index = instance_it->second->animation_sub_index;
  }
  if (invoke_callback && callback) {
    callback(callback_animation_id);
  }
  if (invoke_callback && request_callback) {
    request_callback(request_event);
  }
  return result;
}

M2ResultStatus M2AnimationRuntime::SetAnimation(
    const std::uint32_t instance_id, const std::uint32_t animation_id,
    const float speed) {
  return SetAnimationRequest(instance_id,
                             {.animation_lookup_id = -1,
                              .animation_id = animation_id,
                              .sub_animation_index = -1,
                              .loop_count = 0,
                              .speed = speed});
}

bool M2AnimationRuntime::RecordSampleOnPendingRequestLocked(
    detail::M2Instance &instance, const std::uint32_t animation_id,
    const std::uint32_t time_ms, const float speed) noexcept {
  if (!instance.pending_base_animation.has_value()) {
    return false;
  }
  auto &pending = *instance.pending_base_animation;
  if (pending.kind != detail::M2Instance::PendingBaseAnimation::Kind::kRequest ||
      pending.request.animation_id != animation_id) {
    return false;
  }
  pending.sample_time_ms = time_ms;
  pending.speed = speed;
  pending.has_sample_time = true;
  return true;
}

bool M2AnimationRuntime::SampleNeedsRequestLocked(
    const detail::M2Instance &instance,
    const std::uint32_t animation_id) noexcept {
  return instance.animation_id != animation_id ||
         instance.animation_sequence_index == kInvalidM2AnimationSequenceIndex;
}

void M2AnimationRuntime::RecordSampleOnPendingLocked(
    detail::M2Instance &instance, const std::uint32_t time_ms,
    const float speed) noexcept {
  if (!instance.pending_base_animation.has_value()) {
    return;
  }
  auto &pending = *instance.pending_base_animation;
  pending.sample_time_ms = time_ms;
  pending.speed = speed;
  pending.has_sample_time = true;
}

M2ResultStatus M2AnimationRuntime::SetAnimationSample(
    const std::uint32_t instance_id, const std::uint32_t animation_id,
    const std::uint32_t time_ms, const float speed, const bool zero_blend) {
  PumpSequenceLoads();
  bool needs_request = false;
  {
    std::lock_guard lock(mutex_);
    const auto it = instances_.find(instance_id);
    if (it == instances_.end()) {
      return M2ResultStatus::kFailed;
    }
    if (RecordSampleOnPendingRequestLocked(*it->second, animation_id, time_ms,
                                           speed)) {
      return M2ResultStatus::kNotReady;
    }
    needs_request = SampleNeedsRequestLocked(*it->second, animation_id);
  }
  M2ResultStatus result = M2ResultStatus::kReady;
  if (needs_request) {

    result = SetAnimationRequest(instance_id,
                                 {.animation_lookup_id = -1,
                                  .animation_id = animation_id,
                                  .sub_animation_index = -1,
                                  .loop_count = 0,
                                  .speed = speed,
                                  .zero_blend = zero_blend});
    if (result != M2ResultStatus::kReady) {
      if (result == M2ResultStatus::kNotReady) {
        std::lock_guard lock(mutex_);
        const auto it = instances_.find(instance_id);
        if (it != instances_.end()) {
          RecordSampleOnPendingLocked(*it->second, time_ms, speed);
        }
      }
      return result;
    }
  }

  DeferredCallbacks sampled_events;
  const DeferredCallbackRunner sampled_event_runner{&sampled_events};
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  ApplyAnimationSampleLocked(instance_id, *it->second, time_ms, speed,
                             sampled_events);
  return result;
}

void M2AnimationRuntime::ApplyAnimationSampleLocked(
    const std::uint32_t instance_id, detail::M2Instance &instance,
    const std::uint32_t time_ms, const float speed,
    DeferredCallbacks &sampled_events) {
  const float target_time = static_cast<float>(time_ms) * kMillisecondsToSeconds;
  float previous_time = instance.animation_time;

  if (speed > 0.0f && previous_time > 0.0f && target_time < previous_time) {

    if (instance.animation_completion_callback) {
      sampled_events.push_back(
          [callback = instance.animation_completion_callback,
           completed_id = instance.resolved_animation_id] {
            callback(completed_id);
          });
      instance.animation_completion_fired = true;
    } else if (const auto model_it = models_.find(instance.model_id);
               model_it != models_.end() && model_it->second->loaded &&
               RerollBaseVariantOnLoopLocked(instance, *model_it->second,
                                             instance.model_id, instance_id)) {
      previous_time = instance.animation_time;
    }
  }
  instance.animation_time = target_time;
  instance.animation_speed = speed;

  instance.base_clock_sample_driven = true;
  instance.pose_playback_bound =
      instance.animation_sequence_index != kInvalidM2AnimationSequenceIndex;
  instance.pose_sequence_index = instance.animation_sequence_index;

  const bool sample_clock_rebased =
      std::exchange(instance.animation_sample_clock_rebased, false);
  if (instance.pose_blend_remaining > 0.0f &&
      target_time > previous_time && !sample_clock_rebased) {

    const float sample_delta = target_time - previous_time;
    const float real_delta = speed > 0.0f ? sample_delta / speed : sample_delta;
    AdvancePoseBlendSourceClock(instance, real_delta);
    instance.pose_blend_remaining =
        std::max(0.0f, instance.pose_blend_remaining - real_delta);
    if (instance.pose_blend_remaining == 0.0f) {
      instance.pose_blend_source_sequence_index =
          kInvalidM2AnimationSequenceIndex;
    }
  }
  if (const auto model_it = models_.find(instance.model_id);
      model_it != models_.end() && model_it->second->loaded) {
    CollectSampledAnimationEventCallbacksLocked(instance, *model_it->second,
                                                &sampled_events);
  }
}

M2ResultStatus M2AnimationRuntime::SetAnimationSequenceSample(
    const std::uint32_t instance_id, const std::uint16_t sequence_index,
    const std::uint32_t time_ms, const float speed) {
  PumpSequenceLoads();
  DeferredCallbacks sampled_events;
  const DeferredCallbackRunner sampled_event_runner{&sampled_events};
  std::lock_guard lock(mutex_);
  const auto instance_it = instances_.find(instance_id);
  if (instance_it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  const auto clear_pose = [&instance_it] {
    auto &instance = *instance_it->second;
    instance.animation_sequence_index = kInvalidM2AnimationSequenceIndex;
    instance.current_animation_duration_ms = 0u;
    instance.animation_completion_fired = false;
    instance.pose_playback_bound = false;
    instance.pose_sequence_index = kInvalidM2AnimationSequenceIndex;
    instance.pose_blend_source_sequence_index = kInvalidM2AnimationSequenceIndex;
    instance.pose_blend_source_time = 0.0f;
    instance.pose_blend_duration = 0.0f;
    instance.pose_blend_remaining = 0.0f;
    instance.pose_blend_source_duration_ms = 0u;
    instance.pose_blend_source_clamped_at_end = false;
    instance.pose_blend_source_speed = 1.0f;
  };
  const auto model_it = models_.find(instance_it->second->model_id);
  if (model_it == models_.end() || !model_it->second->loaded) {
    clear_pose();
    return M2ResultStatus::kNotReady;
  }
  if (sequence_index >= model_it->second->model_data.animation_sequences.size()) {
    clear_pose();
    return M2ResultStatus::kUnsupported;
  }
  const auto residency = sequence_streamer_.EnsureResidentLocked(
      instance_it->second->model_id, sequence_index, resume_pending_sequence_loads_);
  if (residency == M2ResultStatus::kNotReady) {
    instance_it->second->pending_base_animation =
        detail::M2Instance::PendingBaseAnimation{
            .kind = detail::M2Instance::PendingBaseAnimation::Kind::kSequenceSample,
            .sequence_index = sequence_index,
            .sample_time_ms = time_ms,
            .speed = speed,
            .has_sample_time = true};
    return residency;
  }
  if (residency != M2ResultStatus::kReady) {
    instance_it->second->pending_base_animation.reset();
    return residency;
  }
  instance_it->second->pending_base_animation.reset();
  ApplySequenceSampleLocked(*instance_it->second, model_it->second->model_data,
                            sequence_index, time_ms, speed);
  CollectSampledAnimationEventCallbacksLocked(*instance_it->second,
                                              *model_it->second, &sampled_events);
  return M2ResultStatus::kReady;
}

M2ResultStatus M2AnimationRuntime::SetAnimationSlotRequest(
    const std::uint32_t instance_id, const std::uint32_t slot_index,
    const M2AnimationRequest &request) {
  PumpSequenceLoads();
  if (slot_index >= kM2RetailAnimationSlotCount) {
    return M2ResultStatus::kFailed;
  }
  std::lock_guard lock(mutex_);
  const auto instance_it = instances_.find(instance_id);
  if (instance_it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  const auto model_it = models_.find(instance_it->second->model_id);
  if (model_it == models_.end() || !model_it->second->loaded) {
    AssignAnimationSlot(*instance_it->second, slot_index, {});
    ++instance_it->second->animation_state_generation;
    return M2ResultStatus::kNotReady;
  }
  const auto selection = ResolveM2AnimationSelection(
      model_it->second->model_data, request,
      DrawInstanceVariantRandom(*instance_it->second, instance_id),
      [this](const auto id) { return LookupAnimationAliasInfo(id, dbc_); });
  if (selection.resolved) {
    const auto residency = sequence_streamer_.EnsureResidentLocked(
        instance_it->second->model_id, selection.resolved_sequence_index,
        resume_pending_sequence_loads_);
    if (residency == M2ResultStatus::kNotReady) {
      instance_it->second->pending_slot_animations[slot_index] =
          detail::M2Instance::PendingSlotAnimation{
              .request = request, .selection = selection, .speed = request.speed};
      return residency;
    }
    if (residency != M2ResultStatus::kReady) {
      instance_it->second->pending_slot_animations[slot_index].reset();
      return residency;
    }
  }
  instance_it->second->pending_slot_animations[slot_index].reset();
  AssignAnimationSlot(*instance_it->second, slot_index,
                     BuildAnimationSlotState(
                         model_it->second->model_data, request, selection,
                         instance_it->second->animation_slots[slot_index]));
  ++instance_it->second->animation_state_generation;
  return selection.resolved ? M2ResultStatus::kReady
                            : M2ResultStatus::kUnsupported;
}

M2ResultStatus M2AnimationRuntime::SetAnimationSlotSample(
    const std::uint32_t instance_id, const std::uint32_t slot_index,
    const std::uint32_t animation_id, const std::uint32_t time_ms,
    const float speed, const bool zero_blend) {
  PumpSequenceLoads();
  if (slot_index >= kM2RetailAnimationSlotCount) {
    return M2ResultStatus::kFailed;
  }
  bool needs_request = false;
  {
    std::lock_guard lock(mutex_);
    const auto it = instances_.find(instance_id);
    if (it == instances_.end()) {
      return M2ResultStatus::kFailed;
    }
    auto &pending = it->second->pending_slot_animations[slot_index];
    if (pending.has_value() && pending->request.animation_id == animation_id) {
      pending->sample_time_ms = time_ms;
      pending->speed = speed;
      pending->has_sample_time = true;
      return M2ResultStatus::kNotReady;
    }
    const auto &slot = it->second->animation_slots[slot_index];
    needs_request = !slot.active || slot.requested_animation_id != animation_id;
  }
  M2ResultStatus result = M2ResultStatus::kReady;
  if (needs_request) {
    result = SetAnimationSlotRequest(
        instance_id, slot_index,
        {.animation_lookup_id = -1,
         .animation_id = animation_id,
         .sub_animation_index = -1,
         .loop_count = 0,
         .speed = speed,
         .zero_blend = zero_blend});
    if (result != M2ResultStatus::kReady) {
      if (result == M2ResultStatus::kNotReady) {
        std::lock_guard lock(mutex_);
        const auto it = instances_.find(instance_id);
        if (it != instances_.end()) {
          auto &pending = it->second->pending_slot_animations[slot_index];
          if (pending.has_value()) {
            pending->sample_time_ms = time_ms;
            pending->speed = speed;
            pending->has_sample_time = true;
          }
        }
      }
      return result;
    }
  }

  DeferredCallbacks sampled_events;
  const DeferredCallbackRunner sampled_event_runner{&sampled_events};
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  auto &slot = it->second->animation_slots[slot_index];
  const float target_time =
      static_cast<float>(time_ms) * kMillisecondsToSeconds;

  if (target_time > slot.time_seconds) {
    const float sample_delta = target_time - slot.time_seconds;
    AdvancePoseBlend(slot.blend,
                     speed > 0.0f ? sample_delta / speed : sample_delta);
  } else if (target_time < slot.time_seconds && slot.loop_count == 0) {

    it->second->slot_processed_event_time_ms[slot_index] = 0u;
  }
  slot.time_seconds = target_time;
  slot.speed = speed;
  ++it->second->animation_state_generation;

  if (const auto model_it = models_.find(it->second->model_id);
      model_it != models_.end() && model_it->second->loaded) {
    CollectSlotAnimationEventCallbacksLocked(*it->second, *model_it->second,
                                             slot_index, &sampled_events);
  }
  return result;
}

M2ResultStatus M2AnimationRuntime::SetAnimationSlotTimes(
    const std::uint32_t instance_id,
    const std::span<const std::uint32_t> slot_indices,
    const std::uint32_t time_ms) {
  PumpSequenceLoads();
  for (const auto slot_index : slot_indices) {
    if (slot_index >= kM2RetailAnimationSlotCount) {
      return M2ResultStatus::kFailed;
    }
  }
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  const float time = static_cast<float>(time_ms) * kMillisecondsToSeconds;
  for (const auto slot_index : slot_indices) {
    if (auto &pending = it->second->pending_slot_animations[slot_index];
        pending.has_value()) {
      pending->sample_time_ms = time_ms;
      pending->has_sample_time = true;
    }
    if (auto &slot = it->second->animation_slots[slot_index]; slot.active) {
      slot.time_seconds = time;
      ++it->second->animation_state_generation;
    }
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2AnimationRuntime::ClearAnimationSlot(
    const std::uint32_t instance_id, const std::uint32_t slot_index) {
  if (slot_index >= kM2RetailAnimationSlotCount) {
    return M2ResultStatus::kFailed;
  }
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  AssignAnimationSlot(*it->second, slot_index, {});
  it->second->pending_slot_animations[slot_index].reset();
  ++it->second->animation_state_generation;
  return M2ResultStatus::kReady;
}

M2ResultStatus M2AnimationRuntime::SetKeyBoneBasisOverride(
    const std::uint32_t instance_id, const std::uint32_t key_bone_lookup_index,
    const RenderMatrix4x4 &basis) {
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  const auto model = models_.find(it->second->model_id);
  if (model == models_.end() || !model->second->loaded) {
    return M2ResultStatus::kNotReady;
  }

  const auto &model_data = model->second->model_data;
  if (key_bone_lookup_index >= model_data.key_bone_lookup.size()) {
    return M2ResultStatus::kUnsupported;
  }
  const std::int16_t bone_index =
      model_data.key_bone_lookup[key_bone_lookup_index];
  if (bone_index < 0 ||
      static_cast<std::size_t>(bone_index) >= model_data.bones.size()) {
    return M2ResultStatus::kUnsupported;
  }
  auto &overrides = it->second->bone_basis_overrides;
  const auto existing = std::find_if(
      overrides.begin(), overrides.end(),
      [bone_index](const M2BoneBasisOverride &entry) {
        return entry.bone_index == static_cast<std::uint32_t>(bone_index);
      });
  if (existing != overrides.end()) {
    existing->basis = basis;
  } else {
    overrides.push_back({static_cast<std::uint32_t>(bone_index), basis});
  }
  ++it->second->animation_state_generation;
  return M2ResultStatus::kReady;
}

M2ResultStatus M2AnimationRuntime::ClearKeyBoneBasisOverride(
    const std::uint32_t instance_id,
    const std::uint32_t key_bone_lookup_index) {
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  const auto model = models_.find(it->second->model_id);
  if (model == models_.end() || !model->second->loaded) {
    return M2ResultStatus::kNotReady;
  }
  const auto &model_data = model->second->model_data;
  if (key_bone_lookup_index >= model_data.key_bone_lookup.size()) {
    return M2ResultStatus::kUnsupported;
  }
  const std::int16_t bone_index =
      model_data.key_bone_lookup[key_bone_lookup_index];
  if (bone_index < 0 ||
      static_cast<std::size_t>(bone_index) >= model_data.bones.size()) {
    return M2ResultStatus::kUnsupported;
  }
  auto &overrides = it->second->bone_basis_overrides;
  const auto removed = std::remove_if(
      overrides.begin(), overrides.end(),
      [bone_index](const M2BoneBasisOverride &entry) {
        return entry.bone_index == static_cast<std::uint32_t>(bone_index);
      });
  if (removed == overrides.end()) {
    return M2ResultStatus::kReady;
  }
  overrides.erase(removed, overrides.end());
  ++it->second->animation_state_generation;
  return M2ResultStatus::kReady;
}

M2AnimationSlotStateQuery M2AnimationRuntime::QueryAnimationSlotState(
    const std::uint32_t instance_id, const std::uint32_t slot_index) const {
  if (slot_index >= kM2RetailAnimationSlotCount) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidQuery,
            .detail = "slot_index=" + std::to_string(slot_index)};
  }
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "instance_id=" + std::to_string(instance_id)};
  }
  const auto &slot = it->second->animation_slots[slot_index];
  return {.status = M2ResultStatus::kReady, .has_slot = slot.active, .slot = slot};
}

M2ResultStatus M2AnimationRuntime::CopyActiveAnimationState(
    const std::uint32_t source_instance_id,
    const std::uint32_t destination_instance_id) {
  detail::M2Instance source;
  {
    std::lock_guard lock(mutex_);
    const auto source_it = instances_.find(source_instance_id);
    if (source_it == instances_.end() ||
        instances_.find(destination_instance_id) == instances_.end()) {
      return M2ResultStatus::kFailed;
    }
    source.animation_lookup_id = source_it->second->animation_lookup_id;
    source.resolved_animation_id = source_it->second->resolved_animation_id;
    source.animation_sub_index = source_it->second->animation_sub_index;
    source.animation_loop_count = source_it->second->animation_loop_count;
    source.animation_speed = source_it->second->animation_speed;
    source.animation_time = source_it->second->animation_time;
    source.animation_sequence_index = source_it->second->animation_sequence_index;
    source.animation_slots = source_it->second->animation_slots;
  }
  M2ResultStatus result = M2ResultStatus::kReady;
  if (source.resolved_animation_id >= kM2RetailInvalidAnimationIdThreshold ||
      source.animation_sequence_index == kInvalidM2AnimationSequenceIndex) {
    result = SetAnimation(destination_instance_id, 0u, 1.0f);
  } else {
    result = SetAnimationRequest(
        destination_instance_id,
        {.animation_lookup_id = source.animation_lookup_id,
         .animation_id = source.resolved_animation_id,
         .sub_animation_index = source.animation_sub_index,
         .loop_count = source.animation_loop_count,
         .speed = source.animation_speed});
    if (result == M2ResultStatus::kReady ||
        result == M2ResultStatus::kNotReady) {
      result = SetAnimationSample(
          destination_instance_id, source.resolved_animation_id,
          static_cast<std::uint32_t>(std::max(source.animation_time, 0.0f) *
                                     1000.0f),
          source.animation_speed);
    }
  }
  for (std::uint32_t index = 0; index < kM2RetailAnimationSlotCount; ++index) {
    const auto &slot = source.animation_slots[index];
    if (!slot.active ||
        slot.resolved_animation_id >= kM2RetailInvalidAnimationIdThreshold ||
        slot.sequence_index == kInvalidM2AnimationSequenceIndex) {
      result = MergeM2ResultStatus(
          result, ClearAnimationSlot(destination_instance_id, index));
      continue;
    }
    M2ResultStatus slot_status = SetAnimationSlotRequest(
        destination_instance_id, index,
        {.animation_lookup_id = slot.animation_lookup_id,
         .animation_id = slot.resolved_animation_id,
         .sub_animation_index = slot.sub_animation_index,
         .loop_count = slot.loop_count,
         .speed = slot.speed});
    if (slot_status == M2ResultStatus::kReady ||
        slot_status == M2ResultStatus::kNotReady) {
      slot_status = SetAnimationSlotSample(
          destination_instance_id, index, slot.resolved_animation_id,
          static_cast<std::uint32_t>(std::max(slot.time_seconds, 0.0f) * 1000.0f),
          slot.speed);
    }
    result = MergeM2ResultStatus(result, slot_status);
  }
  return result;
}

M2ResultStatus M2AnimationRuntime::UpdateAnimation(
    const std::uint32_t instance_id, const float delta_time) {
  PumpSequenceLoads();
  std::vector<PendingCompletion> completions;
  DeferredCallbacks events;
  {
    std::lock_guard lock(mutex_);
    const auto it = instances_.find(instance_id);
    if (it == instances_.end()) {
      return M2ResultStatus::kFailed;
    }
    const auto model_it = models_.find(it->second->model_id);
    const detail::M2ModelResource *model_resource =
        (model_it != models_.end() && model_it->second->loaded)
            ? model_it->second.get()
            : nullptr;
    if (RequiresAnimationTick(*it->second, model_resource)) {
      AdvanceAnimation(*it->second, delta_time, &completions);
      ProcessLoopBoundariesLocked(*it->second, instance_id);
      CollectTriggeredAnimationEventCallbacksLockedFor(*it->second, &events);
    }
  }
  for (auto &completion : completions) {
    completion.callback(completion.animation_id);
  }
  for (auto &event : events) {
    event();
  }
  return M2ResultStatus::kReady;
}

void M2AnimationRuntime::UpdateAnimations(
    const std::span<const std::uint32_t> instance_ids, const float delta_time,
    std::vector<std::uint32_t>* const missing_instance_ids) {
  PumpSequenceLoads();
  std::vector<PendingCompletion> completions;
  DeferredCallbacks events;
  const std::size_t n = instance_ids.size();

  const std::size_t parallel_break_even =
      frame_job_system_ != nullptr
          ? core::ParallelDispatchBreakEven(
                frame_job_system_->WorkerCount() + 1u,
                kAnimationInstanceMicroseconds)
          : std::numeric_limits<std::size_t>::max();
  if (frame_job_system_ == nullptr || n < parallel_break_even) {

    std::lock_guard lock(mutex_);
    for (const auto instance_id : instance_ids) {
      const auto it = instances_.find(instance_id);
      if (it == instances_.end()) {
        if (missing_instance_ids != nullptr) {
          missing_instance_ids->push_back(instance_id);
        }
        continue;
      }
      const auto model_it = models_.find(it->second->model_id);
      const detail::M2ModelResource *model_resource =
          (model_it != models_.end() && model_it->second->loaded)
              ? model_it->second.get()
              : nullptr;
      if (!RequiresAnimationTick(*it->second, model_resource)) {

        continue;
      }
      AdvanceAnimation(*it->second, delta_time, &completions);
      ProcessLoopBoundariesLocked(*it->second, instance_id);
      CollectTriggeredAnimationEventCallbacksLockedFor(*it->second, &events);
    }
  } else {

    std::lock_guard lock(mutex_);

    auto &resolved = parallel_resolved_scratch_;
    resolved.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      const auto it = instances_.find(instance_ids[i]);
      if (it == instances_.end()) {
        if (missing_instance_ids != nullptr) {
          missing_instance_ids->push_back(instance_ids[i]);
        }
        resolved[i] = nullptr;
        continue;
      }
      resolved[i] = it->second.get();
    }

    auto &per_instance_completions = parallel_completions_scratch_;
    per_instance_completions.resize(n);
    for (auto &slot : per_instance_completions) {
      slot.clear();
    }
    auto &per_instance_events = parallel_events_scratch_;
    per_instance_events.resize(n);
    for (auto &slot : per_instance_events) {
      slot.clear();
    }
    frame_job_system_->ParallelFor(
        n, [&](const std::size_t begin, const std::size_t end) {
          for (std::size_t i = begin; i < end; ++i) {
            if (resolved[i] == nullptr) {
              continue;
            }
            const auto model_it = models_.find(resolved[i]->model_id);
            const detail::M2ModelResource *model_resource =
                (model_it != models_.end() && model_it->second->loaded)
                    ? model_it->second.get()
                    : nullptr;
            if (!RequiresAnimationTick(*resolved[i], model_resource)) {
              continue;
            }
            AdvanceAnimation(*resolved[i], delta_time, &per_instance_completions[i]);
            CollectTriggeredAnimationEventCallbacksLockedFor(
                *resolved[i], &per_instance_events[i]);
          }
        });

    for (std::size_t i = 0; i < n; ++i) {
      if (resolved[i] != nullptr) {

        ProcessLoopBoundariesLocked(*resolved[i], instance_ids[i]);
      }
      for (auto &completion : per_instance_completions[i]) {
        completions.push_back(std::move(completion));
      }
      for (auto &event : per_instance_events[i]) {
        events.push_back(std::move(event));
      }
    }

    resolved.clear();
  }

  for (auto &completion : completions) {
    completion.callback(completion.animation_id);
  }
  for (auto &event : events) {
    event();
  }
}

void M2AnimationRuntime::UpdateAllAnimations(const float delta_time) {
  PumpSequenceLoads();
  std::vector<PendingCompletion> completions;
  DeferredCallbacks events;
  {
    std::lock_guard lock(mutex_);
    for (auto &[id, instance] : instances_) {
      static_cast<void>(id);
      const auto model_it = models_.find(instance->model_id);
      const detail::M2ModelResource *model_resource =
          (model_it != models_.end() && model_it->second->loaded)
              ? model_it->second.get()
              : nullptr;
      if (!RequiresAnimationTick(*instance, model_resource)) {
        continue;
      }
      AdvanceAnimation(*instance, delta_time, &completions);
      ProcessLoopBoundariesLocked(*instance, id);
      CollectTriggeredAnimationEventCallbacksLockedFor(*instance, &events);
    }
  }
  for (auto &completion : completions) {
    completion.callback(completion.animation_id);
  }
  for (auto &event : events) {
    event();
  }
}

void M2AnimationRuntime::ResumePendingAnimationsLocked(
    const std::uint32_t model_id, DeferredCallbacks *callbacks) {
  const auto model_it = models_.find(model_id);
  if (model_it == models_.end()) {
    return;
  }
  auto &resource = *model_it->second;
  for (auto &[instance_id, instance_ptr] : instances_) {
    static_cast<void>(instance_id);
    auto &instance = *instance_ptr;
    if (instance.model_id != model_id) {
      continue;
    }
    if (instance.pending_base_animation.has_value()) {
      auto &pending = *instance.pending_base_animation;
      const std::uint16_t sequence_index =
          pending.kind == detail::M2Instance::PendingBaseAnimation::Kind::kRequest
              ? pending.selection.resolved_sequence_index
              : pending.sequence_index;
      const auto residency = sequence_streamer_.PendingResidencyStatusLocked(
          model_id, sequence_index);
      if (residency == M2ResultStatus::kReady) {
        if (pending.kind ==
            detail::M2Instance::PendingBaseAnimation::Kind::kRequest) {
          ApplyAnimationSelectionLocked(instance, resource.model_data,
                                        pending.request, pending.selection);
          if (pending.has_sample_time) {
            instance.animation_time =
                static_cast<float>(pending.sample_time_ms) * kMillisecondsToSeconds;
            instance.animation_speed = pending.speed;
          }
          if (callbacks != nullptr && instance.animation_changed_callback) {
            const auto callback = instance.animation_changed_callback;
            const auto animation_id = instance.resolved_animation_id;
            callbacks->push_back(
                [callback, animation_id] { callback(animation_id); });
          }
          if (callbacks != nullptr && instance.animation_request_callback) {
            const auto callback = instance.animation_request_callback;
            const M2AnimationRequestEvent event{
                .requested_animation_id = pending.request.animation_id,
                .resolved_animation_id = instance.resolved_animation_id,
                .resolved_sub_animation_index = instance.animation_sub_index};
            callbacks->push_back([callback, event] { callback(event); });
          }
        } else {
          ApplySequenceSampleLocked(instance, resource.model_data,
                                    pending.sequence_index,
                                    pending.sample_time_ms, pending.speed);
        }
        instance.pending_base_animation.reset();
      } else if (residency == M2ResultStatus::kFailed) {
        instance.pending_base_animation.reset();
      }
    }
    for (std::size_t index = 0;
         index < instance.pending_slot_animations.size(); ++index) {
      auto &pending = instance.pending_slot_animations[index];
      if (!pending.has_value()) {
        continue;
      }
      const auto residency = sequence_streamer_.PendingResidencyStatusLocked(
          model_id, pending->selection.resolved_sequence_index);
      if (residency == M2ResultStatus::kReady) {
        AssignAnimationSlot(instance, index,
                           BuildAnimationSlotState(resource.model_data, pending->request,
                                                   pending->selection,
                                                   instance.animation_slots[index]));
        if (pending->has_sample_time) {
          instance.animation_slots[index].time_seconds =
              static_cast<float>(pending->sample_time_ms) * kMillisecondsToSeconds;
          instance.animation_slots[index].speed = pending->speed;
        }
        ++instance.animation_state_generation;
        pending.reset();
      } else if (residency == M2ResultStatus::kFailed) {
        pending.reset();
      }
    }
  }
}

void M2AnimationRuntime::CollectTriggeredAnimationEventCallbacksLockedFor(
    detail::M2Instance &instance, DeferredCallbacks *callbacks) {

  if (!instance.triggered_event_callback) {
    return;
  }
  const auto model_it = models_.find(instance.model_id);
  if (model_it == models_.end() || !model_it->second->loaded) {
    return;
  }
  CollectTriggeredAnimationEventCallbacksLocked(instance, *model_it->second,
                                                callbacks);
}

void M2AnimationRuntime::CollectTriggeredAnimationEventCallbacksLocked(
    detail::M2Instance &instance, const detail::M2ModelResource &resource,
    DeferredCallbacks *callbacks) {
  if (!instance.triggered_event_callback || resource.model_data.events.empty()) {
    return;
  }
  const std::uint32_t current_time_ms = static_cast<std::uint32_t>(
      std::max(instance.animation_time, 0.0f) * 1000.0f);
  if (current_time_ms <= instance.processed_event_time_ms) {
    instance.processed_event_time_ms = current_time_ms;
    return;
  }
  M2Animator animator(&resource.model_data);
  const auto events = animator.CollectTriggeredEvents(
      ResolveM2RenderableAnimationIndex(instance), instance.processed_event_time_ms,
      current_time_ms, {}, ComputeM2ModelMatrix(instance));
  instance.processed_event_time_ms = current_time_ms;
  if (callbacks == nullptr) {
    return;
  }
  const auto callback = instance.triggered_event_callback;
  for (const auto &event : events) {
    callbacks->push_back([callback, event] { callback(event); });
  }
}

M2InstanceFramePreparer::M2InstanceFramePreparer(
    M2AnimationRuntime &runtime, M2ModelQueries &model_queries,
    M2SpatialQueries &spatial_queries) noexcept
    : runtime_(runtime), model_queries_(model_queries),
      spatial_queries_(spatial_queries), mutex_(runtime.mutex_) {}

void M2InstanceFramePreparer::Begin() {
  assert(!locked_ && "M2InstanceFramePreparer::Begin while a pass is open");
  mutex_.lock();
  locked_ = true;
  DropMemo();
}

void M2InstanceFramePreparer::End() {
  assert(locked_ && "M2InstanceFramePreparer::End without an open pass");
  DropMemo();
  locked_ = false;
  mutex_.unlock();
}

void M2InstanceFramePreparer::Suspend() {
  assert(locked_ && "M2InstanceFramePreparer::Suspend while released");

  DropMemo();
  locked_ = false;
  mutex_.unlock();
}

void M2InstanceFramePreparer::Resume() {
  assert(!locked_ && "M2InstanceFramePreparer::Resume while held");
  mutex_.lock();
  locked_ = true;
}

void M2InstanceFramePreparer::DropMemo() noexcept {
  memo_instance_valid_ = false;
  memo_instance_ = nullptr;
  memo_instance_model_valid_ = false;
  memo_instance_model_ = nullptr;
  memo_request_model_valid_ = false;
  memo_request_model_ = nullptr;
}

detail::M2Instance *M2InstanceFramePreparer::ResolveInstance(
    const std::uint32_t instance_id) {
  if (memo_instance_valid_ && memo_instance_id_ == instance_id) {
    return memo_instance_;
  }
  const auto found = runtime_.instances_.find(instance_id);
  memo_instance_id_ = instance_id;
  memo_instance_valid_ = true;
  memo_instance_ =
      found != runtime_.instances_.end() ? found->second.get() : nullptr;

  memo_instance_model_valid_ = false;
  memo_instance_model_ = nullptr;
  return memo_instance_;
}

const detail::M2ModelResource *M2InstanceFramePreparer::ResolveInstanceModel() {
  if (memo_instance_model_valid_) {
    return memo_instance_model_;
  }
  memo_instance_model_valid_ = true;
  memo_instance_model_ = nullptr;
  if (memo_instance_ != nullptr) {
    const auto found = runtime_.models_.find(memo_instance_->model_id);
    memo_instance_model_ =
        found != runtime_.models_.end() ? found->second.get() : nullptr;
  }
  return memo_instance_model_;
}

const detail::M2ModelResource *M2InstanceFramePreparer::ResolveRequestModel(
    const std::uint32_t model_id) {
  if (memo_request_model_valid_ && memo_request_model_id_ == model_id) {
    return memo_request_model_;
  }
  const auto found = runtime_.models_.find(model_id);
  memo_request_model_id_ = model_id;
  memo_request_model_valid_ = true;
  memo_request_model_ =
      found != runtime_.models_.end() ? found->second.get() : nullptr;
  return memo_request_model_;
}

M2InstanceFrameSpatialResult M2InstanceFramePreparer::PrepareSpatial(
    const M2InstanceFrameSpatialRequest &request) {
  M2InstanceFrameSpatialResult result{};

  const detail::M2ModelResource *const request_model =
      request.model_id != 0u ? ResolveRequestModel(request.model_id) : nullptr;
  if (request.model_id == 0u || request.instance_id == 0u ||
      request_model == nullptr || !request_model->loaded) {
    return result;
  }

  detail::M2Instance *const instance = ResolveInstance(request.instance_id);
  if (instance == nullptr || request.world_transform == nullptr ||
      M2InstanceStore::ApplyWorldTransformMatrixLocked(
          *instance, *request.world_transform) != M2ResultStatus::kReady) {
    return result;
  }
  result.pose_installed = true;

  if (request.visible_submeshes_applied && request.query_spatial) {
    const detail::M2ModelResource *const model = ResolveInstanceModel();
    if (model != nullptr && model->loaded) {
      result.spatial = M2SpatialQueries::BuildSpatialInfoLocked(*instance, *model);
      result.spatial_ready = true;
    }
  }
  return result;
}

bool M2InstanceFramePreparer::QueryRenderReady(const std::uint32_t instance_id) {
  const detail::M2Instance *const instance = ResolveInstance(instance_id);
  if (instance == nullptr) {
    return false;
  }
  const auto readiness =
      spatial_queries_.QueryReadinessOfLocked(instance_id, *instance);
  return readiness.status == M2ResultStatus::kReady && readiness.render_ready;
}

void M2InstanceFramePreparer::PumpSequenceLoadsUnlocked() {
  if (!runtime_.sequence_streamer_.HasPendingCompletions()) {

    return;
  }
  Suspend();
  runtime_.PumpSequenceLoads();
  Resume();
}

M2ResultStatus M2InstanceFramePreparer::ApplyAnimationSample(
    const std::uint32_t instance_id, const std::uint32_t animation_id,
    const std::uint32_t time_ms, const float speed, const bool zero_blend) {

  PumpSequenceLoadsUnlocked();
  detail::M2Instance *instance = ResolveInstance(instance_id);
  if (instance == nullptr) {
    return M2ResultStatus::kFailed;
  }
  if (M2AnimationRuntime::RecordSampleOnPendingRequestLocked(
          *instance, animation_id, time_ms, speed)) {
    return M2ResultStatus::kNotReady;
  }
  const bool needs_request =
      M2AnimationRuntime::SampleNeedsRequestLocked(*instance, animation_id);
  M2ResultStatus result = M2ResultStatus::kReady;
  if (needs_request) {

    Suspend();
    result = runtime_.SetAnimationRequest(instance_id,
                                          {.animation_lookup_id = -1,
                                           .animation_id = animation_id,
                                           .sub_animation_index = -1,
                                           .loop_count = 0,
                                           .speed = speed,
                                           .zero_blend = zero_blend});
    Resume();
    if (result != M2ResultStatus::kReady) {
      if (result == M2ResultStatus::kNotReady) {
        instance = ResolveInstance(instance_id);
        if (instance != nullptr) {
          M2AnimationRuntime::RecordSampleOnPendingLocked(*instance, time_ms,
                                                          speed);
        }
      }
      return result;
    }
    instance = ResolveInstance(instance_id);
    if (instance == nullptr) {
      return M2ResultStatus::kFailed;
    }
  }
  M2AnimationRuntime::DeferredCallbacks sampled_events;
  runtime_.ApplyAnimationSampleLocked(instance_id, *instance, time_ms, speed,
                                      sampled_events);
  if (!sampled_events.empty()) {

    Suspend();
    for (auto &callback : sampled_events) {
      callback();
    }
    Resume();
  }
  return result;
}

std::uint32_t M2InstanceFramePreparer::ResolvePresentationDurationMs(
    const M2InstanceFramePresentationRequest &request) {

  if (const detail::M2Instance *const instance = ResolveInstance(request.instance_id);
      instance != nullptr) {
    if (const detail::M2ModelResource *const model = ResolveInstanceModel();
        model != nullptr && model->loaded) {
      const M2InstanceAnimationInfo info =
          M2SpatialQueries::BuildAnimationInfoLocked(*instance, *model);
      if (info.requested_animation_id == request.animation_id &&
          info.sequence_index != kInvalidM2AnimationSequenceIndex &&
          info.duration_ms != 0u) {
        return info.duration_ms;
      }
    }
  }

  if (const detail::M2ModelResource *const model = ResolveRequestModel(request.model_id);
      model != nullptr && model->loaded) {
    const auto sequence = model_queries_.QueryAnimationSequenceLocked(
        *model, request.animation_id, 0);
    if (sequence.status == M2ResultStatus::kReady && sequence.has_sequence) {
      return sequence.sequence.duration_ms;
    }
  }
  return 0u;
}

bool M2InstanceFramePreparer::SamplePresentation(
    const M2InstanceFramePresentationRequest &request) {
  if (request.sample_animation) {
    std::uint32_t sample_time_ms = request.sample_time_ms;
    if (request.clamp_to_duration) {

      const std::uint32_t duration_ms = ResolvePresentationDurationMs(request);
      if (duration_ms != 0u) {
        sample_time_ms = std::min(sample_time_ms, duration_ms - 1u);
      }
    }
    if (ApplyAnimationSample(request.instance_id, request.animation_id,
                             sample_time_ms, request.speed,
                             request.zero_blend) != M2ResultStatus::kReady) {
      return false;
    }
  }

  detail::M2Instance *const instance = ResolveInstance(request.instance_id);
  if (instance == nullptr ||
      M2InstanceStore::ApplyVisualPresentationLocked(
          *instance, request.visible, request.tint_rgba, request.alpha) !=
          M2ResultStatus::kReady) {
    return false;
  }
  return request.visible_submeshes_applied;
}

M2ResultStatus M2InstanceFramePreparer::SetSharedBatchUniforms(
    const std::uint32_t instance_id, const M2SharedBatchUniformsHandle handle) {
  detail::M2Instance *const instance = ResolveInstance(instance_id);
  if (instance == nullptr) {
    return M2ResultStatus::kFailed;
  }
  return runtime_.instances_.ApplySharedBatchUniformsLocked(*instance, handle);
}

M2ResultStatus M2InstanceFramePreparer::ClearReplaceableTexturePath(
    const std::uint32_t instance_id, const std::uint32_t texture_type) {
  detail::M2Instance *const instance = ResolveInstance(instance_id);
  if (instance == nullptr) {
    return M2ResultStatus::kFailed;
  }
  M2InstanceStore::ApplyClearReplaceableTexturePathLocked(*instance, texture_type);
  return M2ResultStatus::kReady;
}

}
