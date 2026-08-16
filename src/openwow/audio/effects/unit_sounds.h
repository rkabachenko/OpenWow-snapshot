
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::audio {

struct UnitSoundDisplayInfo {
  uint32_t sound_slot_index{0};
};

struct UnitSoundLookupRow {
  uint32_t sound_lookup_id{0};
  int32_t sound_slot_index{-1};
  uint32_t dry_sound_kit_id{0};
  uint32_t wet_sound_kit_id{0};
};

class UnitSoundKitLookup {
 public:
  void Reset() {
    min_display_info_id_ = 1;
    max_display_info_id_ = 0;
    display_info_.clear();
    sound_kits_by_lookup_id_.clear();
  }

  void ConfigureDisplayInfoRange(uint32_t min_display_info_id,
                                 uint32_t max_display_info_id) {
    if (max_display_info_id < min_display_info_id) {
      Reset();
      return;
    }

    min_display_info_id_ = min_display_info_id;
    max_display_info_id_ = max_display_info_id;
    display_info_.assign(
        static_cast<std::size_t>(max_display_info_id_ - min_display_info_id_ + 1),
        std::nullopt);
  }

  bool SetDisplayInfo(uint32_t display_info_id, uint32_t sound_slot_index) {
    const auto slot = GetDisplayInfoSlot(display_info_id);
    if (!slot.has_value()) {
      return false;
    }

    display_info_[*slot] = UnitSoundDisplayInfo{sound_slot_index};
    return true;
  }

  bool ClearDisplayInfo(uint32_t display_info_id) {
    const auto slot = GetDisplayInfoSlot(display_info_id);
    if (!slot.has_value()) {
      return false;
    }

    display_info_[*slot].reset();
    return true;
  }

  void RebuildSoundKitIndex(const std::vector<UnitSoundLookupRow>& rows,
                            uint32_t max_sound_slot_index) {
    sound_kits_by_lookup_id_.clear();

    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
      if (it->sound_slot_index < 0) {
        continue;
      }

      const auto sound_slot_index =
          static_cast<uint32_t>(it->sound_slot_index);
      if (sound_slot_index > max_sound_slot_index) {
        continue;
      }

      auto& variants = sound_kits_by_lookup_id_[it->sound_lookup_id];
      if (variants.dry_sound_kits.empty()) {
        const auto size =
            static_cast<std::size_t>(max_sound_slot_index) + 1;
        variants.dry_sound_kits.assign(size, 0);
        variants.wet_sound_kits.assign(size, 0);
      }

      variants.dry_sound_kits[sound_slot_index] = it->dry_sound_kit_id;
      variants.wet_sound_kits[sound_slot_index] = it->wet_sound_kit_id;
    }
  }

  [[nodiscard]] uint32_t ResolveSoundKit(uint32_t display_info_id,
                                         uint32_t sound_lookup_id,
                                         bool use_wet_variant) const {
    const auto display_slot = GetDisplayInfoSlot(display_info_id);
    if (!display_slot.has_value()) {
      return 0;
    }

    const auto& display_info = display_info_[*display_slot];
    if (!display_info.has_value()) {
      return 0;
    }

    const auto it = sound_kits_by_lookup_id_.find(sound_lookup_id);
    if (it == sound_kits_by_lookup_id_.end()) {
      return 0;
    }

    const auto sound_slot_index = display_info->sound_slot_index;
    const auto& sound_kits = use_wet_variant ? it->second.wet_sound_kits
                                             : it->second.dry_sound_kits;
    if (sound_slot_index >= sound_kits.size()) {
      return 0;
    }

    return sound_kits[sound_slot_index];
  }

  [[nodiscard]] bool empty() const { return sound_kits_by_lookup_id_.empty(); }

 private:
  struct SoundKitVariants {
    std::vector<uint32_t> dry_sound_kits;
    std::vector<uint32_t> wet_sound_kits;
  };

  [[nodiscard]] std::optional<std::size_t> GetDisplayInfoSlot(
      uint32_t display_info_id) const {
    if (display_info_id < min_display_info_id_ ||
        display_info_id > max_display_info_id_) {
      return std::nullopt;
    }

    return static_cast<std::size_t>(display_info_id - min_display_info_id_);
  }

  uint32_t min_display_info_id_{1};
  uint32_t max_display_info_id_{0};
  std::vector<std::optional<UnitSoundDisplayInfo>> display_info_;
  std::unordered_map<uint32_t, SoundKitVariants> sound_kits_by_lookup_id_;
};

struct UnitSoundPlaybackParams {
  uint32_t sound_type_override{0};
  uint32_t playback_priority{std::numeric_limits<uint32_t>::max()};
  float volume_scale{1.0f};
  float max_distance_override{-1.0f};
  uint32_t channel_play_state_action{1};
};

struct UnitSoundCallbacks {

  std::function<uint32_t(uint32_t display_info_id, uint32_t sound_lookup_id,
                         bool use_wet_variant)>
      resolve_sound_kit;

  std::function<bool(uintptr_t unit_obj)> is_player_unit;

  std::function<bool(uintptr_t unit_obj)> is_active_player_object;

  std::function<std::optional<bool>()> query_listener_at_character_cvar;

  std::function<void(uint32_t kit_id, const float* position,
                     const UnitSoundPlaybackParams& params)> play_sound_kit;
};

inline void PlayUnitSound(uint32_t sound_lookup_id, const float* position,
                          uint32_t display_info_id, bool use_wet_variant,
                          bool use_extended_max_distance, uintptr_t unit_obj,
                          const UnitSoundCallbacks& cb) {
  if (!unit_obj)
    return;

  const auto listener_at_character = cb.query_listener_at_character_cvar();
  if (!listener_at_character.has_value())
    return;

  uint32_t kit_id =
      cb.resolve_sound_kit(display_info_id, sound_lookup_id, use_wet_variant);
  if (!kit_id) {
    kit_id = cb.resolve_sound_kit(0, sound_lookup_id, use_wet_variant);
  }
  if (!kit_id)
    return;

  UnitSoundPlaybackParams params;
  const bool is_active_player =
      cb.is_player_unit(unit_obj) && cb.is_active_player_object(unit_obj);
  const float* playback_position = position;

  if (is_active_player) {
    params.sound_type_override = 17;
    params.playback_priority = 115;

    if (*listener_at_character) {
      playback_position = nullptr;
      params.volume_scale = 0.65f;
      params.max_distance_override =
          use_extended_max_distance ? 50.0f : 20.0f;
    }
  } else {
    params.sound_type_override = 13;
    params.channel_play_state_action = 0;
  }

  cb.play_sound_kit(kit_id, playback_position, params);
}

}
