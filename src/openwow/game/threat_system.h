#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

namespace ThreatStatus {
inline constexpr std::uint8_t kLow = 0;
inline constexpr std::uint8_t kOvernuking = 1;
inline constexpr std::uint8_t kPulling = 2;
inline constexpr std::uint8_t kTanking = 3;
}

struct ThreatInfo {
  ObjectGuid unit_guid;
  std::uint32_t threat_value = 0;
  float threat_percent = 0.0f;
  bool is_tanking = false;
  std::uint8_t threat_status = ThreatStatus::kLow;
  std::uint8_t raw_percent = 255;
};

using ThreatEntry = ThreatInfo;

struct ThreatQueryData {
  bool has_entry = false;
  bool has_highest_guid = false;
  ObjectGuid highest_guid;
  ThreatInfo entry;
};

class ThreatSystem {
public:
  static ThreatSystem &Get();

  void SetThreatList(const ObjectGuid &target, const std::vector<ThreatInfo> &threats);
  void UpdateThreat(const ObjectGuid &target, const ObjectGuid &unit, std::uint32_t threat);
  void ApplyThreatPacketUpdate(const ObjectGuid &target, const std::vector<ThreatInfo> &threats);
  void ApplyHighestThreatPacketUpdate(const ObjectGuid &target, const ObjectGuid &highest_guid,
                                      const std::vector<ThreatInfo> &threats);
  void RemoveThreatEntry(const ObjectGuid &target, const ObjectGuid &unit);
  void RemoveThreatTargetForUnit(const ObjectGuid &unit, const ObjectGuid &target);
  void ClearThreatList(const ObjectGuid &target);
  void ClearThreatLists();

  [[nodiscard]] const std::vector<ThreatInfo> &GetThreatList(const ObjectGuid &target) const;
  [[nodiscard]] float GetThreatPercent(const ObjectGuid &target, const ObjectGuid &unit) const;
  [[nodiscard]] bool IsTanking(const ObjectGuid &target, const ObjectGuid &unit) const;
  [[nodiscard]] std::uint8_t GetThreatStatus(const ObjectGuid &target,
                                             const ObjectGuid &unit) const;

  [[nodiscard]] float GetPlayerThreatPercent() const;
  [[nodiscard]] std::uint8_t GetPlayerThreatStatus() const;
  [[nodiscard]] bool IsPlayerTanking() const;

  void SetPlayerGuid(const ObjectGuid &guid);
  void SetLocalPlayerGuid(const ObjectGuid &guid) {
    SetPlayerGuid(guid);
  }

  [[nodiscard]] float GetMyThreat(const ObjectGuid &target) const;
  [[nodiscard]] float GetMyThreatPercent(const ObjectGuid &target) const;
  [[nodiscard]] bool AmITanking(const ObjectGuid &target) const;
  [[nodiscard]] std::optional<ThreatInfo> GetHighestThreat(const ObjectGuid &target) const;
  [[nodiscard]] std::uint32_t GetTrackedTargetCount() const;
  [[nodiscard]] bool HasThreatData(const ObjectGuid &target) const;
  [[nodiscard]] ObjectGuid GetHighestThreatGuid(const ObjectGuid &target) const;
  [[nodiscard]] bool TryGetThreatQueryData(const ObjectGuid &target, const ObjectGuid &unit,
                                           ThreatQueryData *out) const;
  [[nodiscard]] std::vector<ObjectGuid> GetTargetsForUnit(const ObjectGuid &unit) const;

  void Reset();

private:
  struct ReverseThreatTargetList {
    std::vector<ObjectGuid> targets;
  };

  struct ThreatTargetState {
    std::vector<ThreatInfo> entries;
    ObjectGuid highest_guid;
  };

  ThreatSystem() = default;

  static ThreatInfo *FindEntry(ThreatTargetState &state, const ObjectGuid &unit);
  static const ThreatInfo *FindEntry(const ThreatTargetState &state, const ObjectGuid &unit);
  static ThreatInfo &FindOrCreateEntry(ThreatTargetState &state, const ObjectGuid &unit);
  static void InitializeEntry(ThreatInfo &entry, const ObjectGuid &unit);
  static std::uint8_t ComputeRawPercent(std::uint32_t threat_value,
                                        std::uint32_t highest_threat_value);
  static bool RefreshRelativeThreatStatus(ThreatInfo &entry, std::uint32_t highest_threat_value);
  static void RefreshHighestThreatStatus(ThreatTargetState &state);
  void RecomputeThreatTable(ThreatTargetState &state, bool recompute_all,
                            const std::vector<ObjectGuid> &touched_units);
  void LinkThreatTargetForUnit(const ObjectGuid &unit, const ObjectGuid &target);
  void UnlinkThreatTargetForUnit(const ObjectGuid &unit, const ObjectGuid &target);

  std::unordered_map<std::uint64_t, ThreatTargetState> threat_lists_;
  std::unordered_map<std::uint64_t, ReverseThreatTargetList> unit_targets_;
  ObjectGuid player_guid_;
  static const std::vector<ThreatInfo> kEmptyList;
  mutable std::mutex mutex_;
};

}
