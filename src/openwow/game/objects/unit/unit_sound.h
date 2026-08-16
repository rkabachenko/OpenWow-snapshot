#pragma once

#include <cstdint>

namespace openwow::data::dbc {
struct CreatureSoundDataEntry;
}

namespace openwow::game {

class CGUnit_C;
class WorldSession;

class UnitSoundComponent final {
public:
  UnitSoundComponent() = default;
  UnitSoundComponent(const UnitSoundComponent &) = delete;
  UnitSoundComponent &operator=(const UnitSoundComponent &) = delete;
  UnitSoundComponent(UnitSoundComponent &&) noexcept = default;
  UnitSoundComponent &operator=(UnitSoundComponent &&) noexcept = default;
  ~UnitSoundComponent() = default;

  void SetActiveCreatureSoundDataId(std::uint32_t id) noexcept {
    active_sound_data_id_ = id;
  }
  [[nodiscard]] std::uint32_t ActiveCreatureSoundDataId() const noexcept {
    return active_sound_data_id_;
  }

  [[nodiscard]] const data::dbc::CreatureSoundDataEntry *
  ResolveActive(const CGUnit_C &unit) const;

  [[nodiscard]] const data::dbc::CreatureSoundDataEntry *
  ResolveMount(const CGUnit_C &unit) const;

  void PlayCreatureSound(CGUnit_C &unit, std::uint32_t creature_sound_type,
                         bool force_play);

  int PlayPrioritySound(const CGUnit_C &unit, std::uint32_t priority_state) const;

  static void SetCreatureStandSoundThrottle(std::uint32_t last_tick_ms);

  void RefreshAmbientLoopSound(CGUnit_C &unit) const;

  int PlayServerObjectSound(const CGUnit_C &unit,
                            std::uint32_t sound_kit_id) const;

private:
  std::uint32_t active_sound_data_id_{0};
  mutable std::uint32_t priority_sound_handle_{0};
  mutable std::uint32_t priority_sound_state_{0};
  mutable std::uint32_t ambient_loop_sound_handle_{0};
  mutable std::uint32_t ambient_loop_sound_kit_id_{0};
  mutable std::uint32_t server_object_sound_handle_{0};
};

}
