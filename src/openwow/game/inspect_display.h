#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct InspectDisplaySlot {
  uint8_t slot = 0;
  uint32_t itemId = 0;
  std::string itemName;
  uint32_t iconId = 0;
  uint32_t quality = 0;
  int32_t enchantId = 0;
  uint32_t itemLevel = 0;
  std::vector<uint32_t> gemIds;
};

struct InspectTalentTreeInfo {
  std::string treeName;
  uint32_t treeIconId = 0;
  uint32_t pointsSpent = 0;
  std::string backgroundTex;
};

struct InspectDisplayData {
  ObjectGuid targetGuid{ObjectGuid(0)};
  std::string name;
  uint8_t level = 0;
  uint8_t classId = 0;
  uint8_t raceId = 0;
  std::string guildName;
  uint32_t achievementPoints = 0;
  std::vector<InspectDisplaySlot> equipment;
  std::vector<uint32_t> talentSpells;
  std::vector<InspectTalentTreeInfo> talentTrees;
  uint32_t honorKills = 0;
  uint32_t arenaRating2v2 = 0;
  uint32_t arenaRating3v3 = 0;
  uint32_t arenaRating5v5 = 0;
  float timestamp = 0.0f;
};

class InspectDisplay {
 public:
  InspectDisplay() = default;

  void RequestInspect(ObjectGuid target);
  [[nodiscard]] bool IsPending() const;
  [[nodiscard]] ObjectGuid GetPendingTarget() const;

  void SetInspectData(InspectDisplayData data);
  void ClearInspectData();
  [[nodiscard]] std::optional<InspectDisplayData> GetInspectData() const;
  [[nodiscard]] bool HasInspectData() const;

  [[nodiscard]] size_t GetEquipmentCount() const;
  [[nodiscard]] size_t GetTalentCount() const;

  [[nodiscard]] float GetAverageItemLevel() const;

  [[nodiscard]] std::string GetTalentSpecSummary() const;

  [[nodiscard]] std::optional<InspectTalentTreeInfo> GetPrimaryTalentTree() const;

  [[nodiscard]] bool IsTimedOut(float currentTime, float timeout = 30.0f) const;

  [[nodiscard]] float GetTimeRemaining(float currentTime, float timeout = 30.0f) const;

  [[nodiscard]] std::string GetHonorSummary() const;

  [[nodiscard]] std::string GetArenaSummary() const;

  void Reset();

 private:
  bool pending_ = false;
  ObjectGuid pendingTarget_{ObjectGuid(0)};
  std::optional<InspectDisplayData> data_;
};

}
