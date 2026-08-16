
#pragma once

#include <ctime>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "openwow/game/packet_reader.h"

namespace openwow::data::dbc {
class DbcLoader;
struct WorldStateUIEntry;
}

namespace openwow::game {

struct WorldStateEntry {
  std::int32_t variable_id{0};
  std::int32_t value{0};
};

namespace WorldStateId {

  inline constexpr std::int32_t kWSGFlagCapturesAlliance = 1581;
  inline constexpr std::int32_t kWSGFlagCapturesHorde    = 1582;
  inline constexpr std::int32_t kWSGMaxScore             = 1601;

  inline constexpr std::int32_t kABResourcesAlliance     = 1776;
  inline constexpr std::int32_t kABResourcesHorde        = 1777;
  inline constexpr std::int32_t kABMaxResources          = 1780;

  inline constexpr std::int32_t kAVReinforcementsAlliance = 3127;
  inline constexpr std::int32_t kAVReinforcementsHorde    = 3128;

  inline constexpr std::int32_t kEoTSScoreAlliance       = 2749;
  inline constexpr std::int32_t kEoTSScoreHorde          = 2750;

  inline constexpr std::int32_t kSotATimerMinutes        = 3559;
  inline constexpr std::int32_t kSotATimerTenSeconds     = 3560;
  inline constexpr std::int32_t kSotATimerSeconds        = 3561;

  inline constexpr std::int32_t kWGTimeToNextBattle      = 4354;

  inline constexpr std::int32_t kCurrentArenaSeason      = 3191;
  inline constexpr std::int32_t kPreviousArenaSeason     = 3901;
}

using WorldStateCallback =
    std::function<void(std::int32_t, std::int32_t, std::int32_t)>;

class WorldStateManager {
 public:
  bool HandleInitWorldStates(const std::uint8_t* data, std::size_t len);
  bool HandleUpdateWorldState(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] std::int32_t map_id()  const { return map_id_; }
  [[nodiscard]] std::int32_t zone_id() const { return zone_id_; }
  [[nodiscard]] std::int32_t area_id() const { return area_id_; }
  [[nodiscard]] std::uint32_t world_state_ui_filter_mask() const {
    return world_state_ui_filter_mask_;
  }
  [[nodiscard]] std::int32_t world_state_ui_time_offset_seconds() const {
    return world_state_ui_time_offset_seconds_;
  }
  [[nodiscard]] std::uint64_t world_state_ui_revision() const {
    return world_state_ui_revision_;
  }

  void SetMapId(std::int32_t id);
  void SetZoneId(std::int32_t id);
  void SetAreaId(std::int32_t id);
  bool SetWorldStateUiFilterMask(std::uint32_t mask);
  void SetWorldStateUiServerTime(std::uint32_t absolute_time_seconds);
  [[nodiscard]] std::int32_t world_state_ui_current_time_seconds(
      std::time_t now_seconds) const;
  [[nodiscard]] bool IsVisibleWorldStateUiEntry(
      const openwow::data::dbc::WorldStateUIEntry& entry) const;
  [[nodiscard]] bool HasVisibleWorldStateUiType(
      const openwow::data::dbc::DbcLoader& dbc,
      std::uint32_t type) const;
  [[nodiscard]] std::size_t CountVisibleWorldStateUiEntries(
      const openwow::data::dbc::DbcLoader& dbc) const;
  [[nodiscard]] const openwow::data::dbc::WorldStateUIEntry*
      FindVisibleWorldStateUiEntry(
          const openwow::data::dbc::DbcLoader& dbc,
          std::size_t visible_index) const;

  [[nodiscard]] std::int32_t GetWorldState(std::int32_t variable_id) const;
  [[nodiscard]] bool HasWorldState(std::int32_t variable_id) const;

  void SetWorldState(std::int32_t variable_id, std::int32_t value);

  bool RemoveWorldState(std::int32_t variable_id);

  [[nodiscard]] const std::unordered_map<std::int32_t, std::int32_t>&
      states() const { return states_; }

  [[nodiscard]] std::vector<WorldStateEntry> GetAllEntries() const;

  [[nodiscard]] std::size_t GetStateCount() const { return states_.size(); }

  void ApplyBatch(const std::vector<WorldStateEntry>& entries);

  [[nodiscard]] std::unordered_map<std::int32_t, std::int32_t> Snapshot() const;

  [[nodiscard]] std::vector<WorldStateEntry>
      Diff(const std::unordered_map<std::int32_t, std::int32_t>& snapshot) const;

  using CallbackHandle = std::uint32_t;

  CallbackHandle RegisterCallback(WorldStateCallback cb);
  void UnregisterCallback(CallbackHandle handle);

  [[nodiscard]] std::string DebugDump() const;

  void Clear();

 private:

  bool SetPacketWorldStateUiLocation(std::int32_t map_id, std::int32_t zone_id,
                                     std::int32_t area_id);
  bool ApplyWorldStateValue(std::int32_t variable_id, std::int32_t value);
  void BumpWorldStateUiRevision();
  void FireCallbacks(std::int32_t var_id, std::int32_t old_val,
                     std::int32_t new_val);

  std::int32_t map_id_{-1};
  std::int32_t zone_id_{0};
  std::int32_t area_id_{0};

  std::int32_t ui_map_id_{-1};
  std::int32_t ui_zone_id_{0};
  std::int32_t ui_area_id_{0};
  bool world_state_ui_location_active_{false};
  std::uint32_t world_state_ui_filter_mask_{1};
  std::int32_t world_state_ui_time_offset_seconds_{0};
  std::uint64_t world_state_ui_revision_{0};
  std::unordered_map<std::int32_t, std::int32_t> states_;

  struct CallbackEntry {
    CallbackHandle handle;
    WorldStateCallback cb;
  };
  std::vector<CallbackEntry> callbacks_;
  CallbackHandle next_handle_{1};
};

}
