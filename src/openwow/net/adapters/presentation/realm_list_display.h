#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::net {

enum class RealmType : uint8_t {
  Normal = 0,
  PvP    = 1,
  RP     = 6,
  RPPvP  = 8,
};

namespace RealmDisplayFlag {
  inline constexpr uint8_t None        = 0x00;
  inline constexpr uint8_t Invalid     = 0x01;
  inline constexpr uint8_t Offline     = 0x02;
  inline constexpr uint8_t SpecifyBuild = 0x04;
  inline constexpr uint8_t NewPlayers  = 0x20;
  inline constexpr uint8_t Recommended = 0x40;
}

struct RealmDisplayEntry {
  uint32_t id{0};
  std::string name;
  std::string address;
  uint16_t port{0};
  RealmType type{RealmType::Normal};
  uint8_t flags{RealmDisplayFlag::None};
  uint8_t population{0};
  uint8_t numChars{0};
  float load{0.0f};
  uint8_t timezone{0};
};

class RealmListDisplay {
 public:
  RealmListDisplay() = default;

  void SetRealms(std::vector<RealmDisplayEntry> realms);
  [[nodiscard]] const std::vector<RealmDisplayEntry>& GetRealms() const;

  [[nodiscard]] std::optional<RealmDisplayEntry> GetRealm(uint32_t id) const;
  [[nodiscard]] size_t GetRealmCount() const;
  [[nodiscard]] size_t GetOnlineRealmCount() const;

  void SetSelectedRealm(uint32_t id);
  [[nodiscard]] std::optional<uint32_t> GetSelectedRealm() const;

  void SortByName();
  void SortByPopulation();
  void SortByType();

  [[nodiscard]] static std::string GetRealmTypeName(RealmType type);
  [[nodiscard]] bool IsRealmOnline(uint32_t id) const;
  [[nodiscard]] std::optional<RealmDisplayEntry> GetRecommendedRealm() const;
  [[nodiscard]] static std::string GetPopulationText(float load);

  [[nodiscard]] std::optional<RealmDisplayEntry> FindByName(const std::string& name) const;

  [[nodiscard]] std::optional<RealmDisplayEntry> GetLowestPopulationRealm() const;

  [[nodiscard]] std::string FormatSummary() const;

  [[nodiscard]] std::vector<std::string> GetRealmNames() const;

  void Clear();

 private:
  std::vector<RealmDisplayEntry> realms_;
  std::optional<uint32_t> selected_realm_;
};

}
