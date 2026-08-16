
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct SanctuaryZone {
  std::uint32_t zoneId{0};
  std::string name;
  bool canDuel{false};
  bool canAttackCritters{false};
  bool isMajorCity{false};
};

class SanctuarySystem {
 public:

  void RegisterSanctuary(const SanctuaryZone& zone);

  [[nodiscard]] bool IsSanctuary(std::uint32_t zoneId) const;
  [[nodiscard]] std::optional<SanctuaryZone> GetSanctuaryInfo(
      std::uint32_t zoneId) const;
  [[nodiscard]] std::vector<SanctuaryZone> GetAllSanctuaries() const;
  [[nodiscard]] std::uint32_t GetSanctuaryCount() const {
    return static_cast<std::uint32_t>(sanctuaries_.size());
  }

  [[nodiscard]] bool CanDuelInZone(std::uint32_t zoneId) const;
  [[nodiscard]] bool CanPvPInZone(std::uint32_t zoneId) const;
  [[nodiscard]] bool IsMajorCity(std::uint32_t zoneId) const;

  void SetCurrentZone(std::uint32_t zoneId) { current_zone_ = zoneId; }
  [[nodiscard]] std::uint32_t GetCurrentZone() const {
    return current_zone_;
  }

  [[nodiscard]] bool IsInSanctuary() const;

  void RegisterWotLKSanctuaries();

  void Reset();

 private:
  std::unordered_map<std::uint32_t, SanctuaryZone> sanctuaries_;
  std::uint32_t current_zone_{0};
};

}
