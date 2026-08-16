#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace openwow::data {
class WDBCache;
}

namespace openwow::game {

enum class ItemQuality : uint8_t {
    Poor = 0,
    Common = 1,
    Uncommon = 2,
    Rare = 3,
    Epic = 4,
    Legendary = 5,
    Artifact = 6,
    Heirloom = 7,
};

enum class ItemClass : uint8_t {
    Consumable = 0,
    Container = 1,
    Weapon = 2,
    Gem = 3,
    Armor = 4,
    Reagent = 5,
    Projectile = 6,
    TradeGoods = 7,
    Generic = 8,
    Recipe = 9,
    Money = 10,
    Quiver = 11,
    Quest = 12,
    Key = 13,
    Permanent = 14,
    Misc = 15,
    Glyph = 16,
};

enum class InventoryType : uint8_t {
    NonEquip = 0,
    Head = 1,
    Neck = 2,
    Shoulders = 3,
    Body = 4,
    Chest = 5,
    Waist = 6,
    Legs = 7,
    Feet = 8,
    Wrists = 9,
    Hands = 10,
    Finger = 11,
    Trinket = 12,
    Weapon = 13,
    Shield = 14,
    Ranged = 15,
    Cloak = 16,
    TwoHand = 17,
    Bag = 18,
    Tabard = 19,
    Robe = 20,
    MainHand = 21,
    OffHand = 22,
    Holdable = 23,
    Ammo = 24,
    Thrown = 25,
    RangedRight = 26,
    Quiver = 27,
    Relic = 28,
};

struct ItemStat {
    uint32_t type = 0;
    int32_t value = 0;
};

struct ItemDamage {
    float min_damage = 0;
    float max_damage = 0;
    uint32_t type = 0;
};

struct ItemSpellData {
    uint32_t spell_id = 0;
    uint32_t trigger = 0;
    int32_t charges = 0;
    int32_t cooldown = -1;
    uint32_t category = 0;
    int32_t category_cooldown = -1;
};

struct ItemSocket {
    uint32_t color = 0;
    uint32_t content = 0;
};

struct ItemQualityColorInfo {
    uint32_t argb = 0xffffffff;
    const char* argb_hex = "ffffffff";
    const char* hyperlink_color = "|cffffffff";
    uint8_t red = 0xff;
    uint8_t green = 0xff;
    uint8_t blue = 0xff;
};

struct ItemTemplate {
    uint32_t entry = 0;
    ItemClass item_class = ItemClass::Misc;
    uint32_t subclass = 0;
    int32_t sound_override = -1;
    std::string name;
    uint32_t display_id = 0;
    ItemQuality quality = ItemQuality::Common;
    uint32_t flags = 0;
    uint32_t flags2 = 0;
    uint32_t buy_price = 0;
    uint32_t sell_price = 0;
    InventoryType inventory_type = InventoryType::NonEquip;
    int32_t allowable_class = -1;
    int32_t allowable_race = -1;
    uint32_t item_level = 0;
    uint32_t required_level = 0;
    uint32_t required_skill = 0;
    uint32_t required_skill_rank = 0;
    uint32_t required_spell = 0;
    uint32_t required_honor_rank = 0;
    uint32_t required_city_rank = 0;
    uint32_t required_reputation_faction = 0;
    uint32_t required_reputation_rank = 0;
    uint32_t max_count = 0;
    uint32_t stackable = 1;
    uint32_t container_slots = 0;

    std::array<ItemStat, 10> stats = {};
    uint32_t scaling_stat_distribution = 0;
    uint32_t scaling_stat_value = 0;

    std::array<ItemDamage, 2> damage = {};

    int32_t armor = 0;
    int32_t holy_res = 0;
    int32_t fire_res = 0;
    int32_t nature_res = 0;
    int32_t frost_res = 0;
    int32_t shadow_res = 0;
    int32_t arcane_res = 0;

    uint32_t delay = 0;
    uint32_t ammo_type = 0;
    float range_mod = 0;

    std::array<ItemSpellData, 5> spells = {};

    uint32_t bonding = 0;
    std::string description;
    uint32_t page_text = 0;
    uint32_t language_id = 0;
    uint32_t page_material = 0;
    uint32_t start_quest = 0;
    uint32_t lock_id = 0;
    int32_t material = 0;
    uint32_t sheath = 0;
    uint32_t random_property = 0;
    uint32_t random_suffix = 0;
    uint32_t block = 0;
    uint32_t item_set = 0;
    uint32_t max_durability = 0;
    uint32_t area = 0;
    uint32_t map = 0;
    uint32_t bag_family = 0;
    uint32_t totem_category = 0;

    std::array<ItemSocket, 3> sockets = {};
    uint32_t socket_bonus = 0;

    uint32_t gem_properties = 0;
    uint32_t required_disenchant_skill = 0;
    float armor_damage_modifier = 0;
    uint32_t duration = 0;
    uint32_t item_limit_category = 0;
    uint32_t holiday_id = 0;

    static uint32_t GetQualityColor(ItemQuality quality);

    static const char* GetQualityColorCode(ItemQuality quality);

    static ItemQualityColorInfo GetQualityColorInfo(uint32_t quality);

    [[nodiscard]] bool IsEquippable() const;
    [[nodiscard]] bool IsWeapon() const;
    [[nodiscard]] bool IsArmor() const;
};

class ItemDefinitions {
public:
    void CacheItem(const ItemTemplate& item);
    void CacheItem(uint32_t entry, ItemTemplate&& item);
    bool InvalidateItem(uint32_t entry);

    void CacheItemName(uint32_t entry, std::string name,
                       uint32_t inventory_type);
    bool InvalidateItemName(uint32_t entry);
    [[nodiscard]] bool HydrateRetailItemNames(
        openwow::data::WDBCache& cache);

    const ItemTemplate* GetItem(uint32_t entry) const;
    [[nodiscard]] const ItemTemplate* FindByName(
        const std::string& name) const;
    [[nodiscard]] std::optional<ItemTemplate> GetItemSnapshot(
        uint32_t entry) const;
    [[nodiscard]] std::optional<std::string> GetItemNameSnapshot(
        uint32_t entry) const;
    bool HasItem(uint32_t entry) const;

    size_t GetCacheSize() const;

    void ClearItems();
    void ClearItemNames();
    void Clear();

private:
    struct ItemNameEntry {
        std::string name;
        uint32_t inventory_type = 0;
    };

    std::unordered_map<uint32_t, ItemTemplate> items_;
    std::unordered_map<uint32_t, ItemNameEntry> item_names_;
};

}
