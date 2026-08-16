#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class SkillDisplayCategory : std::uint8_t {
    Weapons     = 0,
    Armor       = 1,
    Languages   = 2,
    Professions = 3,
    Class       = 4,
};

struct SkillDisplayEntry {
    std::uint16_t        skillId     = 0;
    std::string          name;
    SkillDisplayCategory category    = SkillDisplayCategory::Class;
    std::uint16_t        currentRank = 0;
    std::uint16_t        maxRank     = 0;
    std::uint16_t        bonus       = 0;
    bool                 canUnlearn  = false;
};

class SkillLineDisplay {
public:
    void AddSkill(SkillDisplayEntry entry);
    void RemoveSkill(std::uint16_t skillId);

    [[nodiscard]] std::optional<SkillDisplayEntry> GetSkill(
        std::uint16_t skillId) const;
    [[nodiscard]] std::vector<SkillDisplayEntry> GetByCategory(
        SkillDisplayCategory cat) const;
    [[nodiscard]] std::vector<SkillDisplayEntry> GetAllSkills() const;

    void UpdateRank(std::uint16_t skillId, std::uint16_t current,
                    std::uint16_t max);
    [[nodiscard]] float GetSkillPercent(std::uint16_t skillId) const;
    [[nodiscard]] bool  IsMaxed(std::uint16_t skillId) const;

    [[nodiscard]] std::vector<SkillDisplayEntry> GetProfessionSkills() const;
    [[nodiscard]] std::vector<SkillDisplayEntry> GetWeaponSkills() const;
    [[nodiscard]] std::size_t GetSkillCount() const;

    [[nodiscard]] std::vector<SkillDisplayEntry> Search(
        const std::string& query) const;

    void Reset();

private:
    std::unordered_map<std::uint16_t, SkillDisplayEntry> skills_;
};

}
