
#include "openwow/game/skill_display.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

namespace {

std::string ToLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

}

void SkillLineDisplay::AddSkill(SkillDisplayEntry entry) {
    skills_[entry.skillId] = std::move(entry);
}

void SkillLineDisplay::RemoveSkill(std::uint16_t skillId) {
    skills_.erase(skillId);
}

std::optional<SkillDisplayEntry> SkillLineDisplay::GetSkill(
    std::uint16_t skillId) const {
    auto it = skills_.find(skillId);
    if (it == skills_.end()) return std::nullopt;
    return it->second;
}

std::vector<SkillDisplayEntry> SkillLineDisplay::GetByCategory(
    SkillDisplayCategory cat) const {
    std::vector<SkillDisplayEntry> result;
    for (const auto& [id, entry] : skills_) {
        if (entry.category == cat) result.push_back(entry);
    }
    return result;
}

std::vector<SkillDisplayEntry> SkillLineDisplay::GetAllSkills() const {
    std::vector<SkillDisplayEntry> result;
    result.reserve(skills_.size());
    for (const auto& [id, entry] : skills_) {
        result.push_back(entry);
    }
    return result;
}

void SkillLineDisplay::UpdateRank(std::uint16_t skillId,
                                  std::uint16_t current,
                                  std::uint16_t max) {
    auto it = skills_.find(skillId);
    if (it == skills_.end()) return;
    it->second.currentRank = current;
    it->second.maxRank     = max;
}

float SkillLineDisplay::GetSkillPercent(std::uint16_t skillId) const {
    auto it = skills_.find(skillId);
    if (it == skills_.end()) return 0.0f;
    if (it->second.maxRank == 0) return 0.0f;
    return static_cast<float>(it->second.currentRank) /
           static_cast<float>(it->second.maxRank);
}

bool SkillLineDisplay::IsMaxed(std::uint16_t skillId) const {
    auto it = skills_.find(skillId);
    if (it == skills_.end()) return false;
    return it->second.maxRank > 0 &&
           it->second.currentRank >= it->second.maxRank;
}

std::vector<SkillDisplayEntry> SkillLineDisplay::GetProfessionSkills() const {
    return GetByCategory(SkillDisplayCategory::Professions);
}

std::vector<SkillDisplayEntry> SkillLineDisplay::GetWeaponSkills() const {
    return GetByCategory(SkillDisplayCategory::Weapons);
}

std::size_t SkillLineDisplay::GetSkillCount() const {
    return skills_.size();
}

std::vector<SkillDisplayEntry> SkillLineDisplay::Search(
    const std::string& query) const {
    std::vector<SkillDisplayEntry> result;
    if (query.empty()) return GetAllSkills();
    const auto lowerQuery = ToLower(query);
    for (const auto& [id, entry] : skills_) {
        if (ToLower(entry.name).find(lowerQuery) != std::string::npos) {
            result.push_back(entry);
        }
    }
    return result;
}

void SkillLineDisplay::Reset() {
    skills_.clear();
}

}
