
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

struct ArmoryEquipEntry {
    uint32_t slotIndex = 0;
    uint32_t itemId    = 0;
    uint32_t enchantId = 0;
    std::vector<uint32_t> gems;
};

struct ArmoryTalentEntry {
    uint32_t spellId      = 0;
    uint32_t tabIndex     = 0;
    uint32_t tierIndex    = 0;
    uint32_t columnIndex  = 0;
    uint32_t rank         = 0;
};

struct ArmoryExportData {
    std::string name;
    uint32_t level               = 0;
    uint32_t raceId              = 0;
    uint32_t classId             = 0;
    uint32_t genderId            = 0;
    std::string guildName;
    std::string realmName;
    std::vector<ArmoryEquipEntry>   equipment;
    std::vector<ArmoryTalentEntry>  talents;
    uint32_t achievementPoints   = 0;
};

class ArmoryExport {
 public:
    ArmoryExport() = default;

    void SetCharacterData(ArmoryExportData data);
    [[nodiscard]] const ArmoryExportData& GetData() const { return data_; }

    [[nodiscard]] std::string ExportToJSON() const;
    [[nodiscard]] std::string ExportToString() const;

    [[nodiscard]] const std::vector<ArmoryEquipEntry>& GetEquipment() const {
        return data_.equipment;
    }
    [[nodiscard]] const std::vector<ArmoryTalentEntry>& GetTalents() const {
        return data_.talents;
    }

    [[nodiscard]] std::string GetTalentBuild() const;

    [[nodiscard]] float GetAverageItemLevel() const;

    [[nodiscard]] bool IsComplete() const;

    void Reset();

 private:
    ArmoryExportData data_{};
};

}
