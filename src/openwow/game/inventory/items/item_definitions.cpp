#include "openwow/game/inventory/items/item_definitions.h"

#include "openwow/data/db_cache_instances.h"
#include "openwow/data/wdb_cache.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/foundation/text/ascii.h"

#include <utility>

namespace openwow::game {

namespace {

struct ItemQualityColorEntry {
    uint32_t argb;
    const char* argb_hex;
    const char* hyperlink_color;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

constexpr uint32_t kCommonQualityIndex = 1;

constexpr ItemQualityColorEntry kItemQualityColors[] = {
    {0xff9d9d9d, "ff9d9d9d", "|cff9d9d9d", 0x9d, 0x9d, 0x9d},
    {0xffffffff, "ffffffff", "|cffffffff", 0xff, 0xff, 0xff},
    {0xff1eff00, "ff1eff00", "|cff1eff00", 0x1e, 0xff, 0x00},
    {0xff0070dd, "ff0070dd", "|cff0070dd", 0x00, 0x70, 0xdd},
    {0xffa335ee, "ffa335ee", "|cffa335ee", 0xa3, 0x35, 0xee},
    {0xffff8000, "ffff8000", "|cffff8000", 0xff, 0x80, 0x00},
    {0xffe6cc80, "ffe6cc80", "|cffe6cc80", 0xe6, 0xcc, 0x80},
    {0xffe6cc80, "ffe6cc80", "|cffe6cc80", 0xe6, 0xcc, 0x80},
};

constexpr std::size_t kItemQualityColorCount =
    sizeof(kItemQualityColors) / sizeof(kItemQualityColors[0]);

constexpr const ItemQualityColorEntry& GetItemQualityColorEntry(
    uint32_t quality) {
    if (quality >= kItemQualityColorCount) {
        quality = kCommonQualityIndex;
    }
    return kItemQualityColors[quality];
}

}

uint32_t ItemTemplate::GetQualityColor(ItemQuality quality) {
    return GetItemQualityColorEntry(static_cast<uint32_t>(quality)).argb;
}

const char* ItemTemplate::GetQualityColorCode(ItemQuality quality) {
    return GetItemQualityColorEntry(static_cast<uint32_t>(quality)).argb_hex;
}

ItemQualityColorInfo ItemTemplate::GetQualityColorInfo(uint32_t quality) {
    const auto& color = GetItemQualityColorEntry(quality);
    return {
        color.argb,
        color.argb_hex,
        color.hyperlink_color,
        color.red,
        color.green,
        color.blue,
    };
}

bool ItemTemplate::IsEquippable() const {
    return inventory_type != InventoryType::NonEquip;
}

bool ItemTemplate::IsWeapon() const {
    return item_class == ItemClass::Weapon;
}

bool ItemTemplate::IsArmor() const {
    return item_class == ItemClass::Armor;
}

void ItemDefinitions::CacheItem(const ItemTemplate& item) {
    items_[item.entry] = item;
}

void ItemDefinitions::CacheItem(uint32_t entry, ItemTemplate&& item) {
    item.entry = entry;
    items_[entry] = std::move(item);
}

bool ItemDefinitions::InvalidateItem(const std::uint32_t entry) {
    return items_.erase(entry) != 0;
}

void ItemDefinitions::CacheItemName(const std::uint32_t entry, std::string name,
                              const std::uint32_t inventory_type) {
    if (entry == 0) {
        return;
    }

    item_names_[entry] = ItemNameEntry{
        .name = std::move(name),
        .inventory_type = inventory_type,
    };
}

bool ItemDefinitions::InvalidateItemName(const std::uint32_t entry) {
    return item_names_.erase(entry) != 0;
}

bool ItemDefinitions::HydrateRetailItemNames(openwow::data::WDBCache& cache) {
    std::unordered_map<uint32_t, ItemNameEntry> hydrated;
    const auto keys = cache.GetKeysInPersistenceOrder(
        openwow::data::WDBCacheType::ItemName);
    hydrated.reserve(keys.size());

    for (const auto entry : keys) {
        const auto record = cache.Get(openwow::data::WDBCacheType::ItemName,
                                      entry);
        openwow::data::ItemNameWdbPayload payload;
        if (entry == 0 || !record.has_value() ||
            !openwow::data::ParseItemNameWdbPayload(record->data, payload)) {
            cache.ClearType(openwow::data::WDBCacheType::ItemName);
            ClearItemNames();
            return false;
        }

        auto canonical = openwow::data::SerializeItemNameWdbPayload(
            payload.name, payload.inventory_type);
        if (canonical != record->data) {
            cache.Insert(openwow::data::WDBCacheType::ItemName, entry,
                         std::move(canonical), record->version);
        }
        hydrated.insert_or_assign(
            entry, ItemNameEntry{.name = std::move(payload.name),
                                 .inventory_type = payload.inventory_type});
    }

    item_names_ = std::move(hydrated);
    return true;
}

const ItemTemplate* ItemDefinitions::GetItem(uint32_t entry) const {
    auto it = items_.find(entry);
    return it != items_.end() ? &it->second : nullptr;
}

const ItemTemplate* ItemDefinitions::FindByName(const std::string& name) const {
    for (const auto& [entry, item] : items_) {
        (void)entry;
        if (openwow::text::EqualsIgnoreCaseAscii(item.name, name)) {
            return &item;
        }
    }
    return nullptr;
}

std::optional<ItemTemplate> ItemDefinitions::GetItemSnapshot(
    const uint32_t entry) const {
    const auto it = items_.find(entry);
    if (it == items_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::string> ItemDefinitions::GetItemNameSnapshot(
    const uint32_t entry) const {
    const auto item = items_.find(entry);
    if (item != items_.end()) {
        return item->second.name;
    }

    const auto item_name = item_names_.find(entry);
    if (item_name != item_names_.end()) {
        return item_name->second.name;
    }
    return std::nullopt;
}

bool ItemDefinitions::HasItem(uint32_t entry) const {
    return items_.count(entry) > 0;
}

size_t ItemDefinitions::GetCacheSize() const {
    return items_.size();
}

void ItemDefinitions::ClearItems() {
    items_.clear();
}

void ItemDefinitions::ClearItemNames() {
    item_names_.clear();
}

void ItemDefinitions::Clear() {
    items_.clear();
    item_names_.clear();
}

}
