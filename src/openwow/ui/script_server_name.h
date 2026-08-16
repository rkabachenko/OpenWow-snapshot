#pragma once

#include "openwow/net/client_services.h"
#include "openwow/net/realm_config_tables.h"

#include <optional>
#include <string>

namespace openwow::ui {

struct ScriptServerNameResult {
  std::string realm_name;
  bool player_killing_allowed{false};
  bool roleplaying{false};
  bool pvp_flag{true};
};

inline ScriptServerNameResult BuildScriptServerNameResult(
    std::optional<openwow::net::SelectedRealmScriptMetadata> selected_realm = std::nullopt,
    const bool allow_cached_metadata = true) {
  if (!selected_realm.has_value() && allow_cached_metadata) {
    selected_realm = openwow::net::ClientServices::Instance().GetSelectedRealmScriptMetadata();
  }

  const char *realm_name = openwow::net::GetRealmName();
  ScriptServerNameResult result{
      .realm_name = realm_name != nullptr ? std::string(realm_name) : std::string(),
  };

  if (!selected_realm.has_value()) {
    return result;
  }

  const auto config =
      openwow::net::RealmConfigTables::Get().FindRealmTypeConfig(selected_realm->realm_type);
  if (!config.has_value()) {
    return result;
  }

  result.player_killing_allowed = config->player_killing_allowed;
  result.roleplaying = config->roleplaying;
  result.pvp_flag = selected_realm->is_pvp_flag;
  return result;
}

}
