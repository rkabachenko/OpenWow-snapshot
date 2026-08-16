#include "openwow/data/model/m2_external_sequence_tracks.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::data::model {

namespace {

struct RecordShape {
  std::size_t resident_count;
  std::size_t parsed_count;
  std::string_view label;
};

template <typename T>
bool ValidateTrackShape(const M2Track<T> &resident,
                        const M2Track<T> &parsed,
                        const std::string_view label, std::string *error) {
  if (resident.interpolation != parsed.interpolation ||
      resident.global_sequence != parsed.global_sequence) {
    if (error != nullptr) {
      *error = "M2: external sequence track metadata changed: " +
               std::string(label);
    }
    return false;
  }
  if (resident.global_sequence >= 0) {
    return true;
  }

  if (resident.SetCount() != parsed.SetCount()) {
    if (error != nullptr) {
      *error = "M2: external sequence track shape changed: " +
               std::string(label);
    }
    return false;
  }
  return true;
}

bool ValidateDiscreteTrackShape(const M2DiscreteTrack &resident,
                                const M2DiscreteTrack &parsed,
                                const std::string_view label,
                                std::string *error) {
  if (resident.interpolation != parsed.interpolation ||
      resident.global_sequence != parsed.global_sequence) {
    if (error != nullptr) {
      *error = "M2: external discrete track metadata changed: " +
               std::string(label);
    }
    return false;
  }
  if (resident.global_sequence >= 0) {
    return true;
  }
  if (resident.times_ms.size() != parsed.times_ms.size()) {
    if (error != nullptr) {
      *error = "M2: external discrete track shape changed: " +
               std::string(label);
    }
    return false;
  }
  return true;
}

template <typename T>
void AdoptTrackSlots(M2Track<T> &resident, const M2Track<T> &parsed,
                     const std::span<const std::uint16_t> sequence_indices) {
  if (resident.global_sequence >= 0 || resident.SetCount() == 0u) {
    return;
  }

  std::vector<bool> from_parsed(resident.SetCount(), false);
  bool any = false;
  for (const std::uint16_t sequence_index : sequence_indices) {
    const auto index = static_cast<std::size_t>(sequence_index);
    if (index >= resident.SetCount()) {
      continue;
    }
    from_parsed[index] = true;
    any = true;
  }
  if (!any) {
    return;
  }

  M2Track<T> rebuilt;
  rebuilt.interpolation = resident.interpolation;
  rebuilt.global_sequence = resident.global_sequence;
  rebuilt.segments.reserve(resident.SetCount());
  rebuilt.key_times_ms.reserve(resident.key_times_ms.size());
  rebuilt.key_values.reserve(resident.key_values.size());
  for (std::size_t index = 0; index < resident.SetCount(); ++index) {
    const M2Track<T> &source = from_parsed[index] ? parsed : resident;
    rebuilt.AppendSet(source.SetTimes(index), source.SetValues(index));
  }
  resident = std::move(rebuilt);
}

void AdoptDiscreteTrackSlots(
    M2DiscreteTrack &resident, M2DiscreteTrack &parsed,
    const std::span<const std::uint16_t> sequence_indices) noexcept {
  if (resident.global_sequence >= 0 || resident.times_ms.empty()) {
    return;
  }
  for (const std::uint16_t sequence_index : sequence_indices) {
    const auto index = static_cast<std::size_t>(sequence_index);
    if (index >= resident.times_ms.size()) {
      continue;
    }
    resident.times_ms[index] = std::move(parsed.times_ms[index]);
  }
}

template <typename TrackFn, typename DiscreteTrackFn>
void VisitExternalTrackPairs(M2Model &resident, M2Model &parsed,
                             TrackFn &&track,
                             DiscreteTrackFn &&discrete_track) {
  for (std::size_t index = 0; index < resident.bones.size(); ++index) {
    auto &to = resident.bones[index];
    auto &from = parsed.bones[index];
    track(to.translation, from.translation, "bone.translation");
    track(to.rotation, from.rotation, "bone.rotation");
    track(to.scaling, from.scaling, "bone.scaling");
  }
  for (std::size_t index = 0; index < resident.colors.size(); ++index) {
    auto &to = resident.colors[index];
    auto &from = parsed.colors[index];
    track(to.color, from.color, "color.color");
    track(to.alpha, from.alpha, "color.alpha");
  }
  for (std::size_t index = 0; index < resident.transparencies.size(); ++index) {
    track(resident.transparencies[index].alpha,
          parsed.transparencies[index].alpha, "transparency.alpha");
  }
  for (std::size_t index = 0; index < resident.uv_animations.size(); ++index) {
    auto &to = resident.uv_animations[index];
    auto &from = parsed.uv_animations[index];
    track(to.translation, from.translation, "uv.translation");
    track(to.rotation, from.rotation, "uv.rotation");
    track(to.scaling, from.scaling, "uv.scaling");
  }
  for (std::size_t index = 0; index < resident.cameras.size(); ++index) {
    auto &to = resident.cameras[index];
    auto &from = parsed.cameras[index];
    track(to.position, from.position, "camera.position");
    track(to.target, from.target, "camera.target");
    track(to.roll, from.roll, "camera.roll");
  }
  for (std::size_t index = 0; index < resident.events.size(); ++index) {
    discrete_track(resident.events[index].timings,
                   parsed.events[index].timings, "event.timings");
  }
  for (std::size_t index = 0; index < resident.lights.size(); ++index) {
    auto &to = resident.lights[index];
    auto &from = parsed.lights[index];
    track(to.ambient_color, from.ambient_color, "light.ambient_color");
    track(to.ambient_intensity, from.ambient_intensity,
          "light.ambient_intensity");
    track(to.diffuse_color, from.diffuse_color, "light.diffuse_color");
    track(to.diffuse_intensity, from.diffuse_intensity,
          "light.diffuse_intensity");
    track(to.attenuation_start, from.attenuation_start,
          "light.attenuation_start");
    track(to.attenuation_end, from.attenuation_end,
          "light.attenuation_end");
    track(to.visibility, from.visibility, "light.visibility");
  }
  for (std::size_t index = 0; index < resident.ribbon_emitters.size(); ++index) {
    auto &to = resident.ribbon_emitters[index];
    auto &from = parsed.ribbon_emitters[index];
    track(to.color, from.color, "ribbon.color");
    track(to.alpha, from.alpha, "ribbon.alpha");
    track(to.height_above, from.height_above, "ribbon.height_above");
    track(to.height_below, from.height_below, "ribbon.height_below");
    track(to.tex_slot, from.tex_slot, "ribbon.tex_slot");
    track(to.visibility, from.visibility, "ribbon.visibility");
  }
  for (std::size_t index = 0; index < resident.particle_emitters.size(); ++index) {
    auto &to = resident.particle_emitters[index];
    auto &from = parsed.particle_emitters[index];
    track(to.emission_speed, from.emission_speed, "particle.emission_speed");
    track(to.speed_variation, from.speed_variation,
          "particle.speed_variation");
    track(to.vertical_range, from.vertical_range, "particle.vertical_range");
    track(to.horizontal_range, from.horizontal_range,
          "particle.horizontal_range");
    track(to.gravity, from.gravity, "particle.gravity");
    track(to.lifespan, from.lifespan, "particle.lifespan");
    track(to.emission_rate, from.emission_rate, "particle.emission_rate");
    track(to.emission_area_length, from.emission_area_length,
          "particle.emission_area_length");
    track(to.emission_area_width, from.emission_area_width,
          "particle.emission_area_width");
    track(to.z_source, from.z_source, "particle.z_source");
    track(to.enabled_in, from.enabled_in, "particle.enabled_in");
  }
}

}

bool AdoptM2ExternalSequenceTracks(
    M2Model *const destination, M2Model &&source,
    const std::span<const std::uint16_t> sequence_indices,
    std::string *const error) {
  if (error != nullptr) {
    error->clear();
  }
  if (destination == nullptr || sequence_indices.empty()) {
    if (error != nullptr) {
      *error = "M2: external sequence publish has no destination slots";
    }
    return false;
  }
  if (destination->animation_sequences.size() !=
      source.animation_sequences.size()) {
    if (error != nullptr) {
      *error = "M2: external sequence table shape changed";
    }
    return false;
  }

  std::vector<bool> visited(destination->animation_sequences.size(), false);
  for (const std::uint16_t sequence_index : sequence_indices) {
    const auto index = static_cast<std::size_t>(sequence_index);
    if (index >= destination->animation_sequences.size() || visited[index] ||
        destination->animation_sequences[index].animation_id !=
            source.animation_sequences[index].animation_id ||
        destination->animation_sequences[index].sub_animation_id !=
            source.animation_sequences[index].sub_animation_id) {
      if (error != nullptr) {
        *error = "M2: external sequence identity changed";
      }
      return false;
    }
    visited[index] = true;
  }

  const std::array<RecordShape, 9> shapes{{
      {destination->bones.size(), source.bones.size(), "bones"},
      {destination->colors.size(), source.colors.size(), "colors"},
      {destination->transparencies.size(), source.transparencies.size(),
       "transparencies"},
      {destination->uv_animations.size(), source.uv_animations.size(),
       "uv_animations"},
      {destination->cameras.size(), source.cameras.size(), "cameras"},
      {destination->events.size(), source.events.size(), "events"},
      {destination->lights.size(), source.lights.size(), "lights"},
      {destination->ribbon_emitters.size(), source.ribbon_emitters.size(),
       "ribbon_emitters"},
      {destination->particle_emitters.size(), source.particle_emitters.size(),
       "particle_emitters"},
  }};
  for (const auto &shape : shapes) {
    if (shape.resident_count != shape.parsed_count) {
      if (error != nullptr) {
        *error = "M2: external sequence record count changed: " +
                 std::string(shape.label);
      }
      return false;
    }
  }

  bool valid = true;
  VisitExternalTrackPairs(
      *destination, source,
      [&](const auto &resident, const auto &parsed,
          const std::string_view label) {
        if (valid) {
          valid = ValidateTrackShape(resident, parsed, label, error);
        }
      },
      [&](const M2DiscreteTrack &resident, const M2DiscreteTrack &parsed,
          const std::string_view label) {
        if (valid) {
          valid = ValidateDiscreteTrackShape(resident, parsed, label, error);
        }
      });
  if (!valid) {
    return false;
  }

  VisitExternalTrackPairs(
      *destination, source,
      [&](auto &resident, auto &parsed, const std::string_view) {
        AdoptTrackSlots(resident, parsed, sequence_indices);
      },
      [&](M2DiscreteTrack &resident, M2DiscreteTrack &parsed,
          const std::string_view) {
        AdoptDiscreteTrackSlots(resident, parsed, sequence_indices);
      });

  RebuildM2BonePoseIndex(*destination);
  return true;
}

}
