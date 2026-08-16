#include "openwow/net/wotlk/realm_list.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <sstream>

namespace openwow::net::wotlk {

std::vector<RealmInfo> ParseRealmList(const std::string& serialized) {
  if (serialized.empty()) {
    return {{.id = 1, .name = "AzerothCore Realm", .address = "127.0.0.1:8085"}};
  }

  std::vector<RealmInfo> realms;
  std::stringstream ss(serialized);
  std::string token;
  int id = 1;
  while (std::getline(ss, token, ';')) {
    if (token.empty()) {
      continue;
    }
    const auto separator = token.find('=');
    if (separator == std::string::npos) {
      realms.push_back({.id = id++, .name = "Realm " + std::to_string(id - 1), .address = token});
      continue;
    }

    std::string name = token.substr(0, separator);
    std::string address = token.substr(separator + 1);
    if (name.empty()) {
      name = "Realm " + std::to_string(id);
    }
    if (address.empty()) {
      continue;
    }
    realms.push_back({.id = id++, .name = name, .address = address});
  }

  if (realms.empty()) {
    realms.push_back({.id = 1, .name = "AzerothCore Realm", .address = serialized});
  }
  return realms;
}

std::string SerializeRealmList(const std::vector<RealmInfo>& realms) {
  std::string result;
  for (std::size_t i = 0; i < realms.size(); ++i) {
    if (i > 0) result += ';';
    result += realms[i].name;
    result += '=';
    result += realms[i].address;
  }
  return result;
}

bool ValidateRealmInfo(const RealmInfo& info) {
  if (info.name.empty()) return false;
  if (info.address.empty()) return false;

  bool has_content = false;
  for (char c : info.address) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      has_content = true;
      break;
    }
  }
  return has_content;
}

std::vector<RealmInfo> FilterRealms(
    const std::vector<RealmInfo>& realms,
    const std::function<bool(const RealmInfo&)>& pred) {
  std::vector<RealmInfo> result;
  result.reserve(realms.size());
  for (const auto& r : realms) {
    if (pred(r)) {
      result.push_back(r);
    }
  }
  return result;
}

std::optional<RealmInfo> FindRealmByName(
    const std::vector<RealmInfo>& realms,
    const std::string& name) {
  for (const auto& r : realms) {

    if (r.name.size() != name.size()) continue;
    bool match = true;
    for (std::size_t i = 0; i < r.name.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(r.name[i])) !=
          std::tolower(static_cast<unsigned char>(name[i]))) {
        match = false;
        break;
      }
    }
    if (match) return r;
  }
  return std::nullopt;
}

std::string FormatRealmInfo(const RealmInfo& info) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "[%d] %s (%s) type=%s pop=%.1f chars=%d%s%s",
                info.id,
                info.name.c_str(),
                info.address.c_str(),
                RealmTypeToString(info.type).c_str(),
                info.population,
                info.num_characters,
                info.locked ? " [LOCKED]" : "",
                info.is_full ? " [FULL]" : "");
  return buf;
}

std::string RealmTypeToString(RealmType type) {
  switch (type) {
    case RealmType::kNormal: return "Normal";
    case RealmType::kPvP:    return "PvP";
    case RealmType::kRP:     return "RP";
    case RealmType::kRPPvP:  return "RP-PvP";
  }
  return "Unknown";
}

bool ParseAddress(const std::string& address, std::string& host, uint16_t& port) {
  if (address.empty()) return false;

  if (address.front() == '[') {
    auto closing = address.find(']');
    if (closing == std::string::npos) return false;
    host = address.substr(1, closing - 1);
    if (closing + 1 < address.size() && address[closing + 1] == ':') {
      auto port_str = address.substr(closing + 2);
      auto [ptr, ec] = std::from_chars(port_str.data(),
                                        port_str.data() + port_str.size(),
                                        port);
      if (ec != std::errc{}) return false;
    } else {
      port = 8085;
    }
    return !host.empty();
  }

  auto colon = address.rfind(':');
  if (colon == std::string::npos) {
    host = address;
    port = 8085;
    return true;
  }

  host = address.substr(0, colon);
  auto port_str = address.substr(colon + 1);
  if (port_str.empty()) {
    port = 8085;
    return !host.empty();
  }

  auto [ptr, ec] = std::from_chars(port_str.data(),
                                    port_str.data() + port_str.size(),
                                    port);
  if (ec != std::errc{}) return false;

  return !host.empty();
}

}
