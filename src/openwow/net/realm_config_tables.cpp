#include "openwow/net/realm_config_tables.h"

#include "openwow/data/formats/dbc/dbc_loader.h"

namespace openwow::net {

void RealmConfigTables::LoadFrom(const openwow::data::dbc::DbcLoader& loader) {
  std::vector<RealmCategoryRecord> categories;
  categories.reserve(loader.cfg_categories().size());

  for (const auto& row : loader.cfg_categories().entries()) {
    categories.push_back(RealmCategoryRecord{
        .id = row.id,
        .locale_mask = row.locale_mask,
        .create_charset_mask = row.create_charset_mask,
        .flags = row.flags,
        .name = std::string(row.name),
    });
  }

  std::vector<RealmTypeConfig> realm_types;
  realm_types.reserve(loader.cfg_configs().size());
  for (const auto& row : loader.cfg_configs().entries()) {
    realm_types.push_back(RealmTypeConfig{
        .realm_type = row.realm_type,
        .player_killing_allowed = row.player_killing_allowed != 0u,
        .roleplaying = row.roleplaying != 0u,
    });
  }

  std::lock_guard<std::mutex> lock(mutex_);
  categories_ = std::move(categories);
  realm_types_ = std::move(realm_types);
}

}
