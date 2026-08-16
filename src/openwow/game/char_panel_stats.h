
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::game {

enum class CharStatCategory : uint8_t {
    Attributes  = 0,
    Melee       = 1,
    Ranged      = 2,
    Spell       = 3,
    Defenses    = 4,
    Resistances = 5,
};

struct CharStatLine {
    std::string  label;
    std::string  value;
    std::string  tooltip;
    bool         isPercent   = false;
    int32_t      comparison  = 0;
};

class CharPanelStats {
public:
    CharPanelStats() = default;

    void SetStat(CharStatCategory cat,
                 const std::string& label,
                 const std::string& value,
                 const std::string& tooltip);

    [[nodiscard]] std::vector<CharStatLine> GetStats(CharStatCategory cat) const;
    [[nodiscard]] std::vector<CharStatCategory> GetAllCategories() const;
    [[nodiscard]] size_t GetStatCount(CharStatCategory cat) const;

    void SetStrength(int32_t base, int32_t bonus);
    [[nodiscard]] std::pair<int32_t, int32_t> GetStrength() const;

    void SetAgility(int32_t base, int32_t bonus);
    [[nodiscard]] std::pair<int32_t, int32_t> GetAgility() const;

    void SetStamina(int32_t base, int32_t bonus);
    [[nodiscard]] std::pair<int32_t, int32_t> GetStamina() const;

    void SetIntellect(int32_t base, int32_t bonus);
    [[nodiscard]] std::pair<int32_t, int32_t> GetIntellect() const;

    void SetSpirit(int32_t base, int32_t bonus);
    [[nodiscard]] std::pair<int32_t, int32_t> GetSpirit() const;

    void SetArmor(int32_t value);
    [[nodiscard]] int32_t GetArmor() const;

    [[nodiscard]] static float GetDamageReduction(int32_t armor,
                                                  uint8_t attackerLevel);

    void SetSelectedCategory(CharStatCategory cat);
    [[nodiscard]] CharStatCategory GetSelectedCategory() const;

    void Reset();

private:
    struct AttrPair {
        int32_t base  = 0;
        int32_t bonus = 0;
    };

    mutable std::mutex mutex_;

    std::unordered_map<uint8_t, std::vector<CharStatLine>> catStats_;

    AttrPair strength_;
    AttrPair agility_;
    AttrPair stamina_;
    AttrPair intellect_;
    AttrPair spirit_;

    int32_t armor_ = 0;

    CharStatCategory selectedCat_ = CharStatCategory::Attributes;
};

}
