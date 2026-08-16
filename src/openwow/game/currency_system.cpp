
#include "openwow/game/currency_system.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/update_fields.h"
#include "openwow/ui/game/cvar_system.h"

#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>

namespace openwow::game {

namespace {

struct CurrencyBitfieldPair {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
};

std::uint32_t ParseStoredBitfield(std::string_view text) {
    if (text.empty()) {
        return 0;
    }

    try {
        const auto parsed = std::stoll(std::string(text));
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(parsed));
    } catch (...) {
        return 0;
    }
}

std::uint32_t ReadCurrencyBitfield(const char* cvar_name) {
    auto& cvars = openwow::ui::game::CVarSystem::Instance();
    return ParseStoredBitfield(cvars.GetCVar(cvar_name));
}

CurrencyBitfieldPair ReadCurrencyBitfields(const char* low_name,
                                           const char* high_name) {
    return {ReadCurrencyBitfield(low_name), ReadCurrencyBitfield(high_name)};
}

void WriteCurrencyBitfield(const char* cvar_name, std::uint32_t bits) {
    auto& cvars = openwow::ui::game::CVarSystem::Instance();
    cvars.SetCVar(cvar_name,
                  std::to_string(static_cast<std::int32_t>(bits)));
}

bool TestCurrencyBit(const CurrencyBitfieldPair& bits, std::uint32_t bit_index) {
    if (bit_index == 0) {
        return false;
    }

    if (bit_index <= 32) {
        const auto mask = std::uint32_t{1} << (bit_index - 1);
        return (bits.low & mask) != 0;
    }

    const auto high_bit = bit_index - 33;
    if (high_bit >= 32) {
        return false;
    }

    const auto mask = std::uint32_t{1} << high_bit;
    return (bits.high & mask) != 0;
}

bool TestCurrencyBit(const char* low_name, const char* high_name,
                     std::uint32_t bit_index) {
    return TestCurrencyBit(ReadCurrencyBitfields(low_name, high_name), bit_index);
}

void SetCurrencyBit(const char* low_name, const char* high_name,
                    std::uint32_t bit_index, bool enabled) {
    if (bit_index == 0) {
        return;
    }

    const char* cvar_name = low_name;
    std::uint32_t bit = bit_index - 1;
    if (bit_index > 32) {
        bit = bit_index - 33;
        cvar_name = high_name;
    }
    if (bit >= 32) {
        return;
    }

    auto bits = ReadCurrencyBitfield(cvar_name);
    const auto mask = std::uint32_t{1} << bit;
    if (enabled) {
        bits |= mask;
    } else {
        bits &= ~mask;
    }

    WriteCurrencyBitfield(cvar_name, bits);
}

std::vector<CurrencyCategoryEntry> MakeFallbackCurrencyCategories() {
    return {
        {static_cast<std::uint32_t>(CurrencyCategory::PvE), true, "PvE", 0},
        {static_cast<std::uint32_t>(CurrencyCategory::PvP), true, "PvP", 0},
        {static_cast<std::uint32_t>(CurrencyCategory::Other), true, "Other", 0},
    };
}

void SortCurrencyCategories(std::vector<CurrencyCategoryEntry>& categories) {
    std::sort(categories.begin(), categories.end(),
              [](const CurrencyCategoryEntry& left,
                 const CurrencyCategoryEntry& right) {
                  return CurrencySystem::CompareCurrencyCategories(&left, &right)
                      < 0;
              });
}

const CurrencyTypeRecord* FindCurrencyTypeByItemId(
    const std::vector<CurrencyTypeRecord>& currency_types, std::uint32_t item_id) {
    for (const auto& type : currency_types) {
        if (type.itemId == item_id) {
            return &type;
        }
    }
    return nullptr;
}

}

CurrencySystem& CurrencySystem::Get() {
    static CurrencySystem instance;
    return instance;
}

const ItemDefinitions& CurrencySystem::RequireItemDefinitions() const {
    if (item_definitions_ == nullptr) {
        throw std::logic_error("CurrencySystem requires an ItemDefinitions binding");
    }
    return *item_definitions_;
}

CurrencyEntry& CurrencySystem::GetOrCreate(uint32_t id) {
    auto it = currencies_.find(id);
    if (it != currencies_.end()) return it->second;

    auto& e = currencies_[id];
    e.id = id;
    return e;
}

void CurrencySystem::SetAmount(uint32_t id, uint32_t amount) {
    std::lock_guard lock(mutex_);
    GetOrCreate(id).amount = amount;
}

void CurrencySystem::SetWeekEarned(uint32_t id, uint32_t earned) {
    std::lock_guard lock(mutex_);
    GetOrCreate(id).week_earned = earned;
}

void CurrencySystem::SetMaxAmount(uint32_t id, uint32_t max) {
    std::lock_guard lock(mutex_);
    GetOrCreate(id).max_amount = max;
}

void CurrencySystem::SetWeekCap(uint32_t id, uint32_t cap) {
    std::lock_guard lock(mutex_);
    GetOrCreate(id).week_cap = cap;
}

void CurrencySystem::SetEntry(const CurrencyEntry& entry) {
    std::lock_guard lock(mutex_);
    currencies_[entry.id] = entry;
}

void CurrencySystem::LoadDefaults() {
    std::lock_guard lock(mutex_);
    for (const auto& def : GetDefaultCurrencyDefs()) {
        auto& e = currencies_[def.id];
        e.id         = def.id;
        e.name       = def.name;
        e.max_amount = def.max_amount;
        e.week_cap   = def.week_cap;
        e.category   = def.category;

    }
}

uint32_t CurrencySystem::GetAmount(uint32_t id) const {
    std::lock_guard lock(mutex_);
    auto it = currencies_.find(id);
    return it != currencies_.end() ? it->second.amount : 0;
}

uint32_t CurrencySystem::GetWeekEarned(uint32_t id) const {
    std::lock_guard lock(mutex_);
    auto it = currencies_.find(id);
    return it != currencies_.end() ? it->second.week_earned : 0;
}

uint32_t CurrencySystem::GetWeekCap(uint32_t id) const {
    std::lock_guard lock(mutex_);
    auto it = currencies_.find(id);
    return it != currencies_.end() ? it->second.week_cap : 0;
}

uint32_t CurrencySystem::GetMaxAmount(uint32_t id) const {
    std::lock_guard lock(mutex_);
    auto it = currencies_.find(id);
    return it != currencies_.end() ? it->second.max_amount : 0;
}

const CurrencyEntry* CurrencySystem::GetEntry(uint32_t id) const {
    std::lock_guard lock(mutex_);
    auto it = currencies_.find(id);
    return it != currencies_.end() ? &it->second : nullptr;
}

bool CurrencySystem::IsAtCap(uint32_t id) const {
    std::lock_guard lock(mutex_);
    auto it = currencies_.find(id);
    if (it == currencies_.end()) return false;
    if (it->second.max_amount == 0) return false;
    return it->second.amount >= it->second.max_amount;
}

bool CurrencySystem::IsAtWeekCap(uint32_t id) const {
    std::lock_guard lock(mutex_);
    auto it = currencies_.find(id);
    if (it == currencies_.end()) return false;
    if (it->second.week_cap == 0) return false;
    return it->second.week_earned >= it->second.week_cap;
}

void CurrencySystem::ForEach(const Visitor& fn) const {
    std::lock_guard lock(mutex_);
    for (const auto& [_, e] : currencies_) {
        fn(e);
    }
}

void CurrencySystem::ForEachByCategory(CurrencyCategory cat,
                                        const Visitor& fn) const {
    std::lock_guard lock(mutex_);
    for (const auto& [_, e] : currencies_) {
        if (e.category == cat) fn(e);
    }
}

std::vector<const CurrencyEntry*> CurrencySystem::GetAll() const {
    std::lock_guard lock(mutex_);
    std::vector<const CurrencyEntry*> result;
    result.reserve(currencies_.size());
    for (const auto& [_, e] : currencies_) {
        result.push_back(&e);
    }
    return result;
}

std::vector<const CurrencyEntry*> CurrencySystem::GetByCategory(
    CurrencyCategory cat) const {
    std::lock_guard lock(mutex_);
    std::vector<const CurrencyEntry*> result;
    for (const auto& [_, e] : currencies_) {
        if (e.category == cat) result.push_back(&e);
    }
    return result;
}

size_t CurrencySystem::GetTotalCurrencyTypes() const {
    std::lock_guard lock(mutex_);
    return currencies_.size();
}

void CurrencySystem::SetBackpackCurrencies(const std::vector<uint32_t>& ids) {
    std::lock_guard lock(mutex_);
    backpack_ids_ = ids;
}

std::vector<uint32_t> CurrencySystem::GetBackpackCurrencies() const {
    std::lock_guard lock(mutex_);
    return backpack_ids_;
}

CurrencySystem::TooltipInfo CurrencySystem::GetTooltipInfo(uint32_t id) const {
    std::lock_guard lock(mutex_);
    TooltipInfo info;
    auto it = currencies_.find(id);
    if (it != currencies_.end()) {
        const auto& e = it->second;
        info.name        = e.name;
        info.amount      = e.amount;
        info.max_amount  = e.max_amount;
        info.week_earned = e.week_earned;
        info.week_cap    = e.week_cap;
        info.at_cap      = (e.max_amount > 0 && e.amount >= e.max_amount);
        info.at_week_cap = (e.week_cap > 0 && e.week_earned >= e.week_cap);
    }
    return info;
}

void CurrencySystem::Reset() {
    std::lock_guard lock(mutex_);
    currencies_.clear();
    backpack_ids_.clear();
    currency_types_by_item_.clear();
    currency_types_.clear();
    dbc_categories_.clear();
    flat_list_.clear();
    categories_.clear();
}

int CurrencySystem::CompareCurrencyCategories(const CurrencyCategoryEntry* a,
                                               const CurrencyCategoryEntry* b) {
    if (!a || !b) return 0;
    int diff = static_cast<int>(a->flags & 1u) - static_cast<int>(b->flags & 1u);
    if (diff != 0) return diff;
    return openwow::core::SStrCmpNoCaseCollate(a->name.c_str(), b->name.c_str(),
                                               0x7FFFFFFFu);
}

const CurrencyListEntry* CurrencySystem::GetCurrencyListEntryByIndex(int index) const {
    std::lock_guard lock(mutex_);
    if (index < 0 || index >= static_cast<int>(flat_list_.size()))
        return nullptr;
    return &flat_list_[index];
}

void CurrencySystem::SetCategoryExpanded(int listIndex, bool expanded) {
    std::lock_guard lock(mutex_);
    if (listIndex < 0 || listIndex >= static_cast<int>(flat_list_.size()))
        return;
    if (!flat_list_[listIndex].isHeader)
        return;
    uint32_t catId = flat_list_[listIndex].categoryId;
    for (auto& cat : categories_) {
        if (cat.id == catId) {
            cat.expanded = expanded;
            return;
        }
    }
}

void CurrencySystem::CacheDbcData(const data::dbc::DbcLoader& dbc) {
    std::lock_guard lock(mutex_);

    std::unordered_map<uint32_t, bool> expanded_by_category;
    expanded_by_category.reserve(categories_.size());
    for (const auto& category : categories_) {
        expanded_by_category.emplace(category.id, category.expanded);
    }

    currency_types_.clear();
    currency_types_by_item_.clear();
    currency_types_.reserve(dbc.currency_types().size());
    currency_types_by_item_.reserve(dbc.currency_types().size());
    for (const auto& type : dbc.currency_types().entries()) {
        CurrencyTypeRecord info{};
        info.dbcId = type.id;
        info.itemId = type.item_id;
        info.categoryId = type.category;
        info.bitIndex = type.bit_index;
        currency_types_.push_back(info);
        currency_types_by_item_.try_emplace(info.itemId, info);
    }

    dbc_categories_.clear();
    dbc_categories_.reserve(dbc.currency_category().size());
    for (const auto& category : dbc.currency_category().entries()) {
        CurrencyCategoryEntry entry{};
        entry.id = category.id;
        entry.name = std::string(category.name);
        entry.flags = category.flags;
        dbc_categories_.push_back(std::move(entry));
    }

    categories_.clear();
    categories_.reserve(dbc_categories_.size());
    for (const auto& category : dbc_categories_) {
        CurrencyCategoryEntry entry = category;
        const auto expanded_it = expanded_by_category.find(category.id);
        entry.expanded = expanded_it != expanded_by_category.end()
            ? expanded_it->second
            : true;
        categories_.push_back(std::move(entry));
    }
    SortCurrencyCategories(categories_);

    flat_list_.clear();
}

void CurrencySystem::InitCurrencyCategories() {
    std::lock_guard lock(mutex_);

    if (!dbc_categories_.empty()) {
        categories_ = dbc_categories_;
    } else if (categories_.empty()) {
        categories_ = MakeFallbackCurrencyCategories();
    }

    for (auto& category : categories_) {
        category.expanded = true;
    }
    SortCurrencyCategories(categories_);
    flat_list_.clear();
}

bool CurrencySystem::IsCurrencyKnown(int categoryIndex, uint32_t knownLow,
                                      uint32_t knownHigh, uint32_t unusedLow,
                                      uint32_t unusedHigh) const {
    if (categoryIndex < 0
        || categoryIndex >= static_cast<int>(categories_.size())) {
        return false;
    }

    const auto current_category_id = categories_[categoryIndex].id;
    const bool is_last_category =
        static_cast<std::size_t>(categoryIndex + 1) == categories_.size();
    const CurrencyBitfieldPair known{knownLow, knownHigh};
    const CurrencyBitfieldPair unused{unusedLow, unusedHigh};

    for (const auto& type : currency_types_) {
        const bool matches_category = type.categoryId == current_category_id;
        if ((matches_category && TestCurrencyBit(known, type.bitIndex))
            || (is_last_category && TestCurrencyBit(unused, type.bitIndex))) {
            return true;
        }
    }

    return false;
}

int CurrencySystem::CompareCurrencyEntries(const CurrencyListEntry* a,
                                            const CurrencyListEntry* b) const {
    if (!a || !b) return 0;

    const auto name_a = RequireItemDefinitions().GetItemNameSnapshot(a->itemId);
    const auto name_b = RequireItemDefinitions().GetItemNameSnapshot(b->itemId);
    const char* nameA = name_a ? name_a->c_str() : nullptr;
    const char* nameB = name_b ? name_b->c_str() : nullptr;

    if (!nameA) return nameB ? 1 : 0;
    if (!nameB) return -1;

    return openwow::core::SStrCmpNoCaseCollate(nameA, nameB, 0x7FFFFFFFu);
}

const CurrencyTypeRecord* CurrencySystem::FindCurrencyTypeByItemIdUnlocked(
    const uint32_t itemId) const {
    return FindCurrencyTypeByItemId(currency_types_, itemId);
}

const CurrencyTypeRecord* CurrencySystem::FindCurrencyDBCEntryUnlocked(
    const int listIndex) const {
    if (listIndex < 0 || listIndex >= static_cast<int>(flat_list_.size())) {
        return nullptr;
    }
    return FindCurrencyTypeByItemIdUnlocked(flat_list_[listIndex].itemId);
}

const CurrencyTypeRecord* CurrencySystem::FindCurrencyDBCEntry(
    const int listIndex) const {
    std::lock_guard lock(mutex_);
    return FindCurrencyDBCEntryUnlocked(listIndex);
}

void CurrencySystem::RebuildCurrencyList(const ObjectManager& objects) {
    std::lock_guard lock(mutex_);
    if (categories_.empty()) {
        categories_ = MakeFallbackCurrencyCategories();
    }

    flat_list_.clear();
    const auto* active_player = objects.GetActivePlayer();
    if (!currency_types_.empty() && active_player != nullptr) {
        const CurrencyBitfieldPair known{
            active_player->GetUInt32(PLAYER_FIELD_KNOWN_CURRENCIES),
            active_player->GetUInt32(PLAYER_FIELD_KNOWN_CURRENCIES + 1),
        };
        const CurrencyBitfieldPair unused = ReadCurrencyBitfields(
            CurrencyCVar::kUnusedLow, CurrencyCVar::kUnusedHigh);
        const CurrencyBitfieldPair backpack = ReadCurrencyBitfields(
            CurrencyCVar::kBackpackLow, CurrencyCVar::kBackpackHigh);

        backpack_ids_.clear();
        for (const auto& type : currency_types_) {
            if (TestCurrencyBit(known, type.bitIndex)
                && TestCurrencyBit(backpack, type.bitIndex)) {
                backpack_ids_.push_back(type.itemId);
            }
        }

        for (std::size_t category_index = 0; category_index < categories_.size();
             ++category_index) {
            const auto& category = categories_[category_index];
            if (!IsCurrencyKnown(static_cast<int>(category_index), known.low,
                                 known.high, unused.low, unused.high)) {
                continue;
            }

            CurrencyListEntry header{};
            header.displayName = category.name;
            header.categoryId = category.id;
            header.isHeader = true;
            header.isExpanded = category.expanded;
            header.flags = category.flags;
            flat_list_.push_back(std::move(header));

            if (!category.expanded) {
                continue;
            }

            const auto start_idx = flat_list_.size();
            for (const auto& type : currency_types_) {
                if (!TestCurrencyBit(known, type.bitIndex)) {
                    continue;
                }

                const bool unused_bit = TestCurrencyBit(unused, type.bitIndex);
                if (category_index + 1 < categories_.size()) {
                    if (type.categoryId != category.id || unused_bit) {
                        continue;
                    }
                } else if (!unused_bit) {
                    continue;
                }

                CurrencyListEntry entry{};
                if (const auto item_name =
                        RequireItemDefinitions().GetItemNameSnapshot(type.itemId);
                    item_name.has_value()) {
                    entry.displayName = *item_name;
                }
                entry.itemId = type.itemId;
                entry.categoryId = category.id;
                entry.showInBackpack = TestCurrencyBit(backpack, type.bitIndex);
                flat_list_.push_back(std::move(entry));
            }

            const auto end_idx = flat_list_.size();
            if (end_idx - start_idx > 1) {
                std::sort(flat_list_.begin()
                              + static_cast<std::ptrdiff_t>(start_idx),
                          flat_list_.begin()
                              + static_cast<std::ptrdiff_t>(end_idx),
                          [this](const CurrencyListEntry& left,
                             const CurrencyListEntry& right) {
                              return CompareCurrencyEntries(&left, &right) < 0;
                          });
            }
        }
        return;
    }

    for (const auto& cat : categories_) {
        CurrencyListEntry header{};
        header.displayName = cat.name;
        header.categoryId  = cat.id;
        header.isHeader    = true;
        header.isExpanded  = cat.expanded;
        header.flags       = cat.flags;

        size_t headerIdx = flat_list_.size();
        flat_list_.push_back(header);

        size_t startIdx = flat_list_.size();
        bool hasCategoryItems = false;

        for (const auto& [id, entry] : currencies_) {
            if (static_cast<uint32_t>(entry.category) != cat.id)
                continue;

            hasCategoryItems = true;
            if (!cat.expanded) {
                continue;
            }

            CurrencyListEntry le{};
            const auto* item = RequireItemDefinitions().GetItem(entry.id);
            le.displayName = item ? item->name : entry.name;
            le.itemId      = id;
            le.categoryId  = cat.id;
            le.showInBackpack = std::find(backpack_ids_.begin(),
                                          backpack_ids_.end(),
                                          id) != backpack_ids_.end();
            flat_list_.push_back(std::move(le));
        }

        size_t endIdx = flat_list_.size();
        if (endIdx - startIdx > 1) {
            std::sort(flat_list_.begin() + static_cast<ptrdiff_t>(startIdx),
                      flat_list_.begin() + static_cast<ptrdiff_t>(endIdx),
                      [this](const CurrencyListEntry& x, const CurrencyListEntry& y) {
                          return CompareCurrencyEntries(&x, &y) < 0;
                      });
        }

        if (!hasCategoryItems) {
            flat_list_.erase(flat_list_.begin()
                             + static_cast<ptrdiff_t>(headerIdx));
        }
    }
}

const CurrencyTypeRecord* CurrencySystem::GetBackpackCurrencyEntry(
    const ObjectManager& objects, const int index) const {
    std::lock_guard lock(mutex_);
    if (index < 0) {
        return nullptr;
    }

    const auto* active_player = objects.GetActivePlayer();
    if (active_player == nullptr) {
        return nullptr;
    }

    const CurrencyBitfieldPair known{
        active_player->GetUInt32(PLAYER_FIELD_KNOWN_CURRENCIES),
        active_player->GetUInt32(PLAYER_FIELD_KNOWN_CURRENCIES + 1),
    };
    const CurrencyBitfieldPair backpack = ReadCurrencyBitfields(
        CurrencyCVar::kBackpackLow, CurrencyCVar::kBackpackHigh);

    int current_index = 0;
    for (const auto& type : currency_types_) {
        if (!TestCurrencyBit(known, type.bitIndex)
            || !TestCurrencyBit(backpack, type.bitIndex)) {
            continue;
        }
        if (current_index == index) {
            return &type;
        }
        ++current_index;
    }
    return nullptr;
}

void CurrencySystem::SetCurrencyUnusedFlag(int listIndex, bool unused) {
    std::lock_guard lock(mutex_);
    if (listIndex < 0 || listIndex >= static_cast<int>(flat_list_.size()))
        return;
    if (flat_list_[listIndex].isHeader)
        return;

    if (const auto* entry = FindCurrencyDBCEntryUnlocked(listIndex)) {
        SetCurrencyBit(CurrencyCVar::kUnusedLow, CurrencyCVar::kUnusedHigh,
                       entry->bitIndex, unused);
    }
}

bool CurrencySystem::IsCurrencyUnused(int listIndex) const {
    std::lock_guard lock(mutex_);
    if (listIndex < 0 || listIndex >= static_cast<int>(flat_list_.size()))
        return false;
    if (flat_list_[listIndex].isHeader) {
        if (categories_.empty()) return false;
        return flat_list_[listIndex].categoryId == categories_.back().id;
    }

    if (const auto* entry = FindCurrencyDBCEntryUnlocked(listIndex)) {
        return TestCurrencyBit(CurrencyCVar::kUnusedLow,
                               CurrencyCVar::kUnusedHigh,
                               entry->bitIndex);
    }
    return false;
}

void CurrencySystem::SetCurrencyBackpackFlag(int listIndex, bool backpack) {
    std::lock_guard lock(mutex_);
    if (listIndex < 0 || listIndex >= static_cast<int>(flat_list_.size()))
        return;
    if (flat_list_[listIndex].isHeader)
        return;

    const auto* entry = FindCurrencyDBCEntryUnlocked(listIndex);
    if (!entry) {
        return;
    }

    const uint32_t itemId = flat_list_[listIndex].itemId;
    SetCurrencyBit(CurrencyCVar::kBackpackLow, CurrencyCVar::kBackpackHigh,
                   entry->bitIndex, backpack);

    auto it = std::find(backpack_ids_.begin(), backpack_ids_.end(), itemId);
    if (backpack && it == backpack_ids_.end()) {
        backpack_ids_.push_back(itemId);
        if (!currency_types_.empty()) {
            std::sort(backpack_ids_.begin(), backpack_ids_.end(),
                      [this](uint32_t left, uint32_t right) {
                          const auto left_it = currency_types_by_item_.find(left);
                          const auto right_it = currency_types_by_item_.find(right);
                          const auto left_order = left_it != currency_types_by_item_.end()
                              ? left_it->second.bitIndex
                              : std::numeric_limits<uint32_t>::max();
                          const auto right_order = right_it != currency_types_by_item_.end()
                              ? right_it->second.bitIndex
                              : std::numeric_limits<uint32_t>::max();
                          if (left_order != right_order) {
                              return left_order < right_order;
                          }
                          return left < right;
                      });
        }
    } else if (!backpack && it != backpack_ids_.end()) {
        backpack_ids_.erase(it);
    }
    flat_list_[listIndex].showInBackpack = backpack;
}

void CurrencySystem::OnKnownCurrencyTypesChanged(const ObjectManager& objects) {
    RebuildCurrencyList(objects);

}

void CurrencySystem::OnCurrencyDisplayUpdate(const ObjectManager& objects) {
    RebuildCurrencyList(objects);

}

void CurrencySystem::RegisterCurrencyScripts() {

}

void CurrencySystem::UnregisterCurrencyScripts() {

}

size_t CurrencySystem::GetCurrencyListSize() const {
    std::lock_guard lock(mutex_);
    return flat_list_.size();
}

}
