
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::game {

enum class PaperDollSlotId : uint8_t {
    Head     = 0,
    Neck     = 1,
    Shoulder = 2,
    Shirt    = 3,
    Chest    = 4,
    Waist    = 5,
    Legs     = 6,
    Feet     = 7,
    Wrist    = 8,
    Hands    = 9,
    Ring1    = 10,
    Ring2    = 11,
    Trinket1 = 12,
    Trinket2 = 13,
    Back     = 14,
    MainHand = 15,
    OffHand  = 16,
    Ranged   = 17,
    Tabard   = 18,
};

inline constexpr uint8_t kPaperDollTotalSlots = 19;

struct PaperDollSlot {
    PaperDollSlotId       id          = PaperDollSlotId::Head;
    uint32_t              itemId      = 0;
    std::string           itemName;
    uint32_t              iconId      = 0;
    uint8_t               quality     = 0;
    uint32_t              durCurrent  = 0;
    uint32_t              durMax      = 0;
    uint32_t              enchantId   = 0;
    std::vector<uint32_t> gemIds;
    bool                  isEmpty     = true;
};

class PaperDoll {
public:
    PaperDoll() = default;

    void SetSlot(PaperDollSlotId id, PaperDollSlot slot);
    void ClearSlot(PaperDollSlotId id);

    [[nodiscard]] std::optional<PaperDollSlot> GetSlot(PaperDollSlotId id) const;
    [[nodiscard]] std::vector<PaperDollSlot>   GetAllSlots() const;
    [[nodiscard]] std::vector<PaperDollSlot>   GetEquippedSlots() const;
    [[nodiscard]] std::vector<PaperDollSlotId> GetEmptySlots() const;
    [[nodiscard]] size_t                       GetEquippedCount() const;
    [[nodiscard]] float                        GetAverageItemLevel() const;
    [[nodiscard]] bool                         HasWeapon() const;
    [[nodiscard]] bool                         IsDualWielding() const;

    [[nodiscard]] static std::string GetSlotName(PaperDollSlotId id);
    [[nodiscard]] static constexpr size_t GetTotalSlots() { return kPaperDollTotalSlots; }

    void Reset();

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint8_t, PaperDollSlot> slots_;
};

enum class StatCategory : uint8_t {
    Attributes   = 0,
    MeleeAttack  = 1,
    RangedAttack = 2,
    SpellAttack  = 3,
    Defenses     = 4,
    Resistances  = 5,
};

struct StatLine {
    std::string label;
    std::string value;
    std::string tooltip;
};

struct StatValue {
    float base         = 0.0f;
    float total        = 0.0f;
    float posBonus     = 0.0f;
    float negBonus     = 0.0f;
    StatCategory category = StatCategory::Attributes;
};

struct EquippedItem {
    uint32_t itemId    = 0;
    uint32_t displayId = 0;
    uint32_t enchantId = 0;
    uint32_t itemLevel = 0;
};

struct SlotDurability {
    uint32_t current = 0;
    uint32_t maximum = 0;
};

class PaperDollStats {
public:
    PaperDollStats() = default;

    void SetStat(const std::string& statName,
                 float baseValue, float totalValue,
                 float positiveBonus, float negativeBonus);

    [[nodiscard]] StatValue GetStat(const std::string& statName) const;

    [[nodiscard]] std::vector<StatLine> GetStatLines(StatCategory category) const;

    void SetEquippedItem(uint32_t slot, uint32_t itemId,
                         uint32_t displayId, uint32_t enchantId);

    [[nodiscard]] EquippedItem GetEquippedItem(uint32_t slot) const;

    void ClearSlot(uint32_t slot);

    [[nodiscard]] float GetItemLevel() const;

    [[nodiscard]] uint32_t GetEquippedSlotCount() const;

    void SetDurability(uint32_t slot, uint32_t current, uint32_t maximum);

    [[nodiscard]] std::pair<uint32_t, uint32_t> GetDurability(uint32_t slot) const;

    [[nodiscard]] bool HasBrokenItems() const;

    [[nodiscard]] uint32_t GetRepairCost() const;

    void Clear();
    void Reset();

private:
    static constexpr uint32_t kMaxSlots = 19;

    std::unordered_map<std::string, StatValue> stats_;
    std::unordered_map<uint32_t, EquippedItem> equipment_;
    std::unordered_map<uint32_t, SlotDurability> durability_;

    mutable std::mutex mutex_;
};

}
