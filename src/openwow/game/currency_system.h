
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class ItemDefinitions;
class ObjectManager;

enum class CurrencyCategory : uint8_t {
    PvE   = 0,
    PvP   = 1,
    Other = 2,
};

struct CurrencyDef {
    uint32_t    id;
    const char* name;
    uint32_t    max_amount;
    uint32_t    week_cap;
    CurrencyCategory category;
};

struct CurrencyTypeRecord {
    std::uint32_t dbcId = 0;
    std::uint32_t itemId = 0;
    std::uint32_t categoryId = 0;
    std::uint32_t bitIndex = 0;
};

namespace CurrencyId {
    inline constexpr uint32_t kBadgeOfJustice         = 29434;
    inline constexpr uint32_t kStoneKeepersShard      = 43228;
    inline constexpr uint32_t kWintergraspMarkOfHonor = 43589;
    inline constexpr uint32_t kEmblemOfHeroism        = 40752;
    inline constexpr uint32_t kEmblemOfValor          = 40753;
    inline constexpr uint32_t kEmblemOfConquest       = 47241;
    inline constexpr uint32_t kEmblemOfTriumph        = 47557;
    inline constexpr uint32_t kEmblemOfFrost          = 49426;
}

inline const std::vector<CurrencyDef>& GetDefaultCurrencyDefs() {
    static const std::vector<CurrencyDef> defs = {
        {CurrencyId::kBadgeOfJustice,         "Badge of Justice",          0, 0, CurrencyCategory::PvE},
        {CurrencyId::kStoneKeepersShard,      "Stone Keeper's Shard",      0, 0, CurrencyCategory::PvP},
        {CurrencyId::kWintergraspMarkOfHonor, "Wintergrasp Mark of Honor", 0, 0, CurrencyCategory::PvP},
        {CurrencyId::kEmblemOfHeroism,        "Emblem of Heroism",         0, 0, CurrencyCategory::PvE},
        {CurrencyId::kEmblemOfValor,          "Emblem of Valor",           0, 0, CurrencyCategory::PvE},
        {CurrencyId::kEmblemOfConquest,       "Emblem of Conquest",        0, 0, CurrencyCategory::PvE},
        {CurrencyId::kEmblemOfTriumph,        "Emblem of Triumph",         0, 0, CurrencyCategory::PvE},
        {CurrencyId::kEmblemOfFrost,          "Emblem of Frost",           0, 0, CurrencyCategory::PvE},
    };
    return defs;
}

struct CurrencyEntry {
    uint32_t id          = 0;
    std::string name;
    uint32_t amount      = 0;
    uint32_t max_amount  = 0;
    uint32_t week_earned = 0;
    uint32_t week_cap    = 0;
    CurrencyCategory category = CurrencyCategory::Other;
};

struct CurrencyListEntry {
    std::string displayName;
    uint32_t    itemId      = 0;
    uint32_t    categoryId  = 0;
    bool        isHeader    = false;
    bool        isExpanded  = false;
    bool        showInBackpack = false;
    uint32_t    flags       = 0;
    bool        isUnused    = false;
};

struct CurrencyCategoryEntry {
    uint32_t    id       = 0;
    bool        expanded = true;
    std::string name;
    uint32_t    flags    = 0;
};

namespace CurrencyCVar {
    inline constexpr const char* kUnusedLow    = "currencyTokensUnused1";
    inline constexpr const char* kUnusedHigh   = "currencyTokensUnused2";
    inline constexpr const char* kBackpackLow  = "currencyTokensBackpack1";
    inline constexpr const char* kBackpackHigh = "currencyTokensBackpack2";
}

namespace CurrencyEvent {
    inline constexpr uint32_t kKnownCurrencyTypesUpdate = 604;
    inline constexpr uint32_t kCurrencyDisplayUpdate    = 605;
    inline constexpr const char* kKnownCurrencyTypesUpdateName =
        "KNOWN_CURRENCY_TYPES_UPDATE";
    inline constexpr const char* kCurrencyDisplayUpdateName =
        "CURRENCY_DISPLAY_UPDATE";
}

class CurrencySystem {
 public:
    static CurrencySystem& Get();
    void BindItemDefinitions(const ItemDefinitions& item_definitions) noexcept {
      item_definitions_ = &item_definitions;
    }

    void SetAmount(uint32_t id, uint32_t amount);
    void SetWeekEarned(uint32_t id, uint32_t earned);
    void SetMaxAmount(uint32_t id, uint32_t max);
    void SetWeekCap(uint32_t id, uint32_t cap);

    void SetEntry(const CurrencyEntry& entry);

    void LoadDefaults();

    [[nodiscard]] uint32_t GetAmount(uint32_t id) const;
    [[nodiscard]] uint32_t GetWeekEarned(uint32_t id) const;
    [[nodiscard]] uint32_t GetWeekCap(uint32_t id) const;
    [[nodiscard]] uint32_t GetMaxAmount(uint32_t id) const;
    [[nodiscard]] const CurrencyEntry* GetEntry(uint32_t id) const;

    [[nodiscard]] bool IsAtCap(uint32_t id) const;
    [[nodiscard]] bool IsAtWeekCap(uint32_t id) const;

    using Visitor = std::function<void(const CurrencyEntry&)>;

    void ForEach(const Visitor& fn) const;
    void ForEachByCategory(CurrencyCategory cat, const Visitor& fn) const;

    [[nodiscard]] std::vector<const CurrencyEntry*> GetAll() const;
    [[nodiscard]] std::vector<const CurrencyEntry*> GetByCategory(CurrencyCategory cat) const;

    [[nodiscard]] size_t GetTotalCurrencyTypes() const;

    void SetBackpackCurrencies(const std::vector<uint32_t>& ids);
    [[nodiscard]] std::vector<uint32_t> GetBackpackCurrencies() const;

    struct TooltipInfo {
        std::string name;
        uint32_t amount = 0;
        uint32_t max_amount = 0;
        uint32_t week_earned = 0;
        uint32_t week_cap = 0;
        bool at_cap = false;
        bool at_week_cap = false;
    };
    [[nodiscard]] TooltipInfo GetTooltipInfo(uint32_t id) const;

    void Reset();

    static int CompareCurrencyCategories(const CurrencyCategoryEntry* a,
                                         const CurrencyCategoryEntry* b);

    [[nodiscard]] const CurrencyListEntry* GetCurrencyListEntryByIndex(int index) const;

    void SetCategoryExpanded(int listIndex, bool expanded);

    void InitCurrencyCategories();

    void CacheDbcData(const data::dbc::DbcLoader& dbc);

    [[nodiscard]] bool IsCurrencyKnown(int categoryIndex, uint32_t knownLow,
                                       uint32_t knownHigh, uint32_t unusedLow,
                                       uint32_t unusedHigh) const;

    int CompareCurrencyEntries(const CurrencyListEntry* a,
                               const CurrencyListEntry* b) const;

    [[nodiscard]] const CurrencyTypeRecord* FindCurrencyDBCEntry(int listIndex) const;

    void RebuildCurrencyList(const ObjectManager& objects);

    [[nodiscard]] const CurrencyTypeRecord* GetBackpackCurrencyEntry(
        const ObjectManager& objects, int index) const;

    void SetCurrencyUnusedFlag(int listIndex, bool unused);

    [[nodiscard]] bool IsCurrencyUnused(int listIndex) const;

    void SetCurrencyBackpackFlag(int listIndex, bool backpack);

    void OnKnownCurrencyTypesChanged(const ObjectManager& objects);

    void OnCurrencyDisplayUpdate(const ObjectManager& objects);

    void RegisterCurrencyScripts();

    void UnregisterCurrencyScripts();

    [[nodiscard]] size_t GetCurrencyListSize() const;

 private:
    CurrencySystem() = default;
    [[nodiscard]] const ItemDefinitions& RequireItemDefinitions() const;

    CurrencyEntry& GetOrCreate(uint32_t id);

    [[nodiscard]] const CurrencyTypeRecord* FindCurrencyDBCEntryUnlocked(int listIndex) const;
    [[nodiscard]] const CurrencyTypeRecord* FindCurrencyTypeByItemIdUnlocked(uint32_t itemId) const;

    std::unordered_map<uint32_t, CurrencyEntry> currencies_;
    std::vector<uint32_t> backpack_ids_;
    std::unordered_map<uint32_t, CurrencyTypeRecord> currency_types_by_item_;
    std::vector<CurrencyTypeRecord> currency_types_;
    std::vector<CurrencyCategoryEntry> dbc_categories_;
    const ItemDefinitions* item_definitions_ = nullptr;

    std::vector<CurrencyListEntry>    flat_list_;
    std::vector<CurrencyCategoryEntry> categories_;

    mutable std::mutex mutex_;
};

}
