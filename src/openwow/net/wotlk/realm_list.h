#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace openwow::net::wotlk {

enum class RealmType : std::uint32_t {
  kNormal = 0,
  kPvP    = 1,
  kRP     = 4,
  kRPPvP  = 5,
};

struct RealmInfo {
  int id{0};
  std::uint32_t cache_index{0};

  std::string name;
  std::string address;
  RealmType type{RealmType::kNormal};
  bool locked{false};

  bool is_offline{false};
  bool is_pvp_flag{false};
  bool has_version_data{false};
  bool is_new{false};
  bool is_recommended{false};
  bool is_full{false};

  float population{0.0F};
  int num_characters{0};
  std::uint8_t timezone{0};

  std::array<std::uint32_t, 4> tail_words{};

  std::uint8_t  version_major{0};
  std::uint8_t  version_minor{0};
  std::uint8_t  version_revision{0};
  std::uint16_t version_build{0};
};

std::vector<RealmInfo> ParseRealmList(const std::string& serialized);

std::string SerializeRealmList(const std::vector<RealmInfo>& realms);

bool ValidateRealmInfo(const RealmInfo& info);

std::vector<RealmInfo> FilterRealms(
    const std::vector<RealmInfo>& realms,
    const std::function<bool(const RealmInfo&)>& pred);

std::optional<RealmInfo> FindRealmByName(
    const std::vector<RealmInfo>& realms,
    const std::string& name);

std::string FormatRealmInfo(const RealmInfo& info);

std::string RealmTypeToString(RealmType type);

bool ParseAddress(const std::string& address, std::string& host, uint16_t& port);

}
