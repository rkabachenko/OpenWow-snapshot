#pragma once

#include "openwow/game/object_guid.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct AbsorbShieldEntry {
  std::uint32_t auraId{0};
  std::uint32_t spellId{0};
  ObjectGuid casterGuid;
  std::uint32_t currentAmount{0};
  std::uint32_t maxAmount{0};
  std::uint32_t schoolMask{0};
  std::uint32_t priority{0};
};

class AbsorbTracker {
 public:
  void AddShield(ObjectGuid unitGuid, AbsorbShieldEntry shield);
  void RemoveShield(ObjectGuid unitGuid, std::uint32_t auraId);
  [[nodiscard]] std::vector<AbsorbShieldEntry> GetShields(
      ObjectGuid unitGuid) const;
  [[nodiscard]] std::uint32_t GetTotalAbsorb(ObjectGuid unitGuid) const;
  [[nodiscard]] std::uint32_t GetAbsorbForSchool(ObjectGuid unitGuid,
                                                  std::uint32_t schoolMask) const;

  std::uint32_t ConsumeAbsorb(ObjectGuid unitGuid, std::uint32_t amount,
                              std::uint32_t schoolMask);

  void UpdateShieldAmount(ObjectGuid unitGuid, std::uint32_t auraId,
                          std::uint32_t newAmount);
  [[nodiscard]] bool HasShield(ObjectGuid unitGuid) const;
  [[nodiscard]] std::uint32_t GetShieldCount(ObjectGuid unitGuid) const;
  void ClearUnit(ObjectGuid unitGuid);
  void Reset();

 private:
  std::unordered_map<std::uint64_t, std::vector<AbsorbShieldEntry>> shields_;
};

}
