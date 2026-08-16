
#include "openwow/game/armory_export.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <utility>

namespace openwow::game {

void ArmoryExport::SetCharacterData(ArmoryExportData data) {
    data_ = std::move(data);
}

std::string ArmoryExport::ExportToJSON() const {

    std::ostringstream o;
    o << "{\n";
    o << "  \"name\": \"" << data_.name << "\",\n";
    o << "  \"level\": " << data_.level << ",\n";
    o << "  \"raceId\": " << data_.raceId << ",\n";
    o << "  \"classId\": " << data_.classId << ",\n";
    o << "  \"genderId\": " << data_.genderId << ",\n";
    o << "  \"guildName\": \"" << data_.guildName << "\",\n";
    o << "  \"realmName\": \"" << data_.realmName << "\",\n";
    o << "  \"achievementPoints\": " << data_.achievementPoints << ",\n";
    o << "  \"talentBuild\": \"" << GetTalentBuild() << "\",\n";

    o << "  \"equipment\": [";
    for (size_t i = 0; i < data_.equipment.size(); ++i) {
        auto& e = data_.equipment[i];
        if (i) o << ",";
        o << "\n    {\"slot\":" << e.slotIndex
          << ",\"itemId\":" << e.itemId
          << ",\"enchantId\":" << e.enchantId
          << ",\"gems\":[";
        for (size_t g = 0; g < e.gems.size(); ++g) {
            if (g) o << ",";
            o << e.gems[g];
        }
        o << "]}";
    }
    o << "\n  ],\n";

    o << "  \"talents\": [";
    for (size_t i = 0; i < data_.talents.size(); ++i) {
        auto& t = data_.talents[i];
        if (i) o << ",";
        o << "\n    {\"spellId\":" << t.spellId
          << ",\"tab\":" << t.tabIndex
          << ",\"tier\":" << t.tierIndex
          << ",\"col\":" << t.columnIndex
          << ",\"rank\":" << t.rank << "}";
    }
    o << "\n  ]\n";

    o << "}";
    return o.str();
}

std::string ArmoryExport::ExportToString() const {
    std::ostringstream o;
    o << data_.name << " — Level " << data_.level;
    if (data_.classId > 0)
        o << " (Class " << data_.classId << ")";
    if (!data_.guildName.empty())
        o << " <" << data_.guildName << ">";
    if (!data_.realmName.empty())
        o << " @ " << data_.realmName;
    o << "\n";
    o << "Talent build: " << GetTalentBuild() << "\n";
    o << "Achievement points: " << data_.achievementPoints << "\n";
    o << "Equipment slots: " << data_.equipment.size() << "\n";
    return o.str();
}

std::string ArmoryExport::GetTalentBuild() const {
    if (data_.talents.empty()) return "0/0/0";

    uint32_t maxTab = 0;
    for (auto& t : data_.talents)
        if (t.tabIndex > maxTab) maxTab = t.tabIndex;

    std::array<uint32_t, 3> sums = {0, 0, 0};
    for (auto& t : data_.talents) {
        if (t.tabIndex < 3)
            sums[t.tabIndex] += t.rank;
    }

    std::ostringstream o;
    o << sums[0] << "/" << sums[1] << "/" << sums[2];
    return o.str();
}

float ArmoryExport::GetAverageItemLevel() const {
    if (data_.equipment.empty()) return 0.0f;
    uint32_t count = 0;
    uint64_t sum = 0;
    for (auto& e : data_.equipment) {
        if (e.itemId > 0) {

            sum += e.itemId;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum) / static_cast<float>(count)
                     : 0.0f;
}

bool ArmoryExport::IsComplete() const {
    return !data_.name.empty() && data_.level > 0 && data_.classId > 0;
}

void ArmoryExport::Reset() {
    data_ = ArmoryExportData{};
}

}
