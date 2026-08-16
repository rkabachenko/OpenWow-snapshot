
#include "openwow/game/game_table.h"

#include <algorithm>
#include <array>

namespace openwow::game {

namespace {

std::array<const data::dbc::GameTablesEntry *, 12> s_resolved_tables{};

}

const data::dbc::GameTablesEntry *GameTable_ResolveName(
    const data::dbc::DbcStore<data::dbc::GameTablesEntry> &store,
    std::string_view name) {
  return store.LookupByNameCaseInsensitive(name);

}

void DBClient_InitializeGameTables(
    const data::dbc::DbcStore<data::dbc::GameTablesEntry> &store) {
  for (std::size_t i = 0; i < kGameTableNames.size(); ++i) {
    s_resolved_tables[i] = GameTable_ResolveName(store, kGameTableNames[i]);
  }
}

const data::dbc::GameTablesEntry *GetResolvedGameTable(std::size_t index) {
  if (index >= s_resolved_tables.size()) return nullptr;
  return s_resolved_tables[index];
}

const data::dbc::GameTablesEntry *GetResolvedGameTable(std::string_view name) {
  for (std::size_t i = 0; i < kGameTableNames.size(); ++i) {
    if (kGameTableNames[i] == name) {
      return s_resolved_tables[i];
    }
  }
  return nullptr;
}

}
