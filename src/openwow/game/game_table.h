#pragma once

#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_entries_extended.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace openwow::game {

[[nodiscard]] const data::dbc::GameTablesEntry *GameTable_ResolveName(
    const data::dbc::DbcStore<data::dbc::GameTablesEntry> &store,
    std::string_view name);

void DBClient_InitializeGameTables(
    const data::dbc::DbcStore<data::dbc::GameTablesEntry> &store);

inline constexpr std::array<std::string_view, 12> kGameTableNames = {{
    "BarberShopCostBase",
    "CombatRatings",
    "ChanceToMeleeCrit",
    "ChanceToMeleeCritBase",
    "ChanceToSpellCrit",
    "ChanceToSpellCritBase",
    "NPCManaCostScaler",
    "OCTRegenHP",
    "OCTRegenMP",
    "RegenHPPerSpt",
    "RegenMPPerSpt",
    "OCTClassCombatRatingScalar",
}};

[[nodiscard]] const data::dbc::GameTablesEntry *GetResolvedGameTable(std::size_t index);
[[nodiscard]] const data::dbc::GameTablesEntry *GetResolvedGameTable(std::string_view name);

enum class GameTableFieldIndex : std::uint32_t {
  kField0 = 0,
  kField1 = 1,
};

template <typename T>
class GameTable {
public:
  GameTable(std::string_view name,
            const data::dbc::DbcStore<T>& store,
            std::uint32_t num_rows,
            std::uint32_t num_columns,
            GameTableFieldIndex field_index = GameTableFieldIndex::kField0)
      : name_(name),
        store_(&store),
        num_rows_(num_rows),
        num_columns_(num_columns),
        field_index_(field_index) {}

  [[nodiscard]] float LookupValue(std::uint32_t column, std::uint32_t row) const {
    const auto index = static_cast<int>(column + row * num_columns_);
    const auto* entry = store_->LookupEntryByRowIndex(index);
    if (!entry) {
      return 0.0f;
    }
    return entry->value;
  }

  [[nodiscard]] float LookupValue(std::uint32_t row) const {
    return LookupValue(0, row);
  }

  [[nodiscard]] std::string_view name() const { return name_; }

  [[nodiscard]] std::uint32_t num_rows() const { return num_rows_; }
  [[nodiscard]] std::uint32_t num_columns() const { return num_columns_; }

  [[nodiscard]] GameTableFieldIndex field_index() const { return field_index_; }

  [[nodiscard]] bool is_valid() const { return store_ && !store_->empty(); }

private:
  std::string_view name_;
  const data::dbc::DbcStore<T>* store_ = nullptr;
  std::uint32_t num_rows_ = 0;
  std::uint32_t num_columns_ = 0;
  GameTableFieldIndex field_index_ = GameTableFieldIndex::kField0;
};

}
