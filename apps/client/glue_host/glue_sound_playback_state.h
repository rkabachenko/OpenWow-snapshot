#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace openwow::client {

class GlueSoundPlaybackState {
 public:
  [[nodiscard]] bool ShouldSuppressRestart(
      const bool is_currently_playing,
      const std::optional<std::uint32_t> requested_sound_kit_id) const {
    return is_currently_playing &&
           current_sound_kit_id_.has_value() &&
           requested_sound_kit_id.has_value() &&
           current_sound_kit_id_ == requested_sound_kit_id;
  }

  void RememberStartedTrack(std::string track_name, const std::uint32_t sound_kit_id) {
    current_track_name_ = std::move(track_name);
    current_sound_kit_id_ = sound_kit_id;
  }

  [[nodiscard]] const std::string& current_track_name() const {
    return current_track_name_;
  }

  [[nodiscard]] std::optional<std::uint32_t> current_sound_kit_id() const {
    return current_sound_kit_id_;
  }

 private:
  std::string current_track_name_;
  std::optional<std::uint32_t> current_sound_kit_id_;
};

}
