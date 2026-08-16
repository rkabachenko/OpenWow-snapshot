#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class RealmInfoPopulation : std::uint8_t {
  Low,
  Medium,
  High,
  Full,
  Locked,
};

enum class RealmInfoType : std::uint8_t {
  Normal,
  PvP,
  RP,
  RPPvP,
};

struct RealmInfoEntry {
  std::uint32_t realmId = 0;
  std::string name;
  std::string address;
  RealmInfoPopulation population = RealmInfoPopulation::Low;
  RealmInfoType realmType = RealmInfoType::Normal;
  std::uint32_t timezone = 0;
  std::uint32_t characterCount = 0;
  bool isRecommended = false;
  bool isNew = false;
  bool hasQueue = false;
  std::uint32_t queuePosition = 0;
};

class RealmInfoDisplay {
 public:
  RealmInfoDisplay() = default;

  void SetRealms(const std::vector<RealmInfoEntry>& realms);
  [[nodiscard]] std::vector<RealmInfoEntry> GetRealms() const;
  [[nodiscard]] std::optional<RealmInfoEntry> GetRealm(std::uint32_t realmId) const;
  [[nodiscard]] std::uint32_t GetRealmCount() const;

  [[nodiscard]] std::vector<RealmInfoEntry> GetRealmsByType(RealmInfoType type) const;
  [[nodiscard]] std::vector<RealmInfoEntry> GetRecommendedRealms() const;
  [[nodiscard]] std::vector<RealmInfoEntry> GetNewRealms() const;

  void SetSelectedRealm(std::uint32_t realmId);
  [[nodiscard]] std::uint32_t GetSelectedRealm() const;
  [[nodiscard]] bool HasSelectedRealm() const;

  void SortByName();
  void SortByPopulation();
  void SortByType();

  [[nodiscard]] static std::string GetPopulationName(RealmInfoPopulation pop);
  [[nodiscard]] static std::uint32_t GetPopulationColor(RealmInfoPopulation pop);
  [[nodiscard]] static std::string GetTypeName(RealmInfoType type);

  void Reset();

 private:
  std::vector<RealmInfoEntry> realms_;
  std::uint32_t selected_realm_id_ = 0;
};

}
