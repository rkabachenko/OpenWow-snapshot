
#include "openwow/game/bg_specific_display.h"

#include <sstream>

namespace openwow::game {

const std::array<std::string, 5> ABDisplay::kBaseNames = {
    "Stables", "Blacksmith", "Farm", "Lumber Mill", "Gold Mine"};

const std::array<std::string, 4> EotSDisplay::kTowerNames = {
    "Blood Elf Tower", "Fel Reaver Ruins", "Mage Tower", "Draenei Ruins"};

const std::array<std::string, 5> SotADisplay::kGateNames = {
    "Green Gate", "Blue Gate", "Red Gate", "Purple Gate", "Yellow Gate (Chamber)"};

const std::array<std::string, 7> IoCDisplay::kNodeNames = {
    "Docks", "Hangar", "Workshop", "Quarry", "Refinery",
    "Alliance Keep", "Horde Keep"};

const std::array<std::string, 6> IoCDisplay::kGateNames = {
    "Alliance Front Gate", "Alliance West Gate", "Alliance East Gate",
    "Horde Front Gate", "Horde West Gate", "Horde East Gate"};

void BGSpecificDisplay::SetBGType(BGSpecificType type) {
    bgType_ = type;
    active_ = true;
}

BGSpecificType BGSpecificDisplay::GetBGType() const {
    return bgType_;
}

void BGSpecificDisplay::SetWSGData(const WSGDisplay& data) {
    wsgData_ = data;
    active_ = true;
}

std::optional<WSGDisplay> BGSpecificDisplay::GetWSGData() const {
    return wsgData_;
}

void BGSpecificDisplay::SetABData(const ABDisplay& data) {
    abData_ = data;
    active_ = true;
}

std::optional<ABDisplay> BGSpecificDisplay::GetABData() const {
    return abData_;
}

void BGSpecificDisplay::SetAVData(const AVDisplay& data) {
    avData_ = data;
    active_ = true;
}

std::optional<AVDisplay> BGSpecificDisplay::GetAVData() const {
    return avData_;
}

void BGSpecificDisplay::SetEotSData(const EotSDisplay& data) {
    eotsData_ = data;
    active_ = true;
}

std::optional<EotSDisplay> BGSpecificDisplay::GetEotSData() const {
    return eotsData_;
}

void BGSpecificDisplay::SetSotAData(const SotADisplay& data) {
    sotaData_ = data;
    active_ = true;
}

std::optional<SotADisplay> BGSpecificDisplay::GetSotAData() const {
    return sotaData_;
}

void BGSpecificDisplay::SetIoCData(const IoCDisplay& data) {
    iocData_ = data;
    active_ = true;
}

std::optional<IoCDisplay> BGSpecificDisplay::GetIoCData() const {
    return iocData_;
}

std::string BGSpecificDisplay::GetStatusText() const {
    if (!active_) return "";

    std::ostringstream oss;

    switch (bgType_) {
        case BGSpecificType::WarsongGulch: {
            if (!wsgData_) return "Warsong Gulch";
            const auto& d = *wsgData_;
            oss << "WSG: Alliance " << static_cast<int>(d.allianceCaptures)
                << "/" << static_cast<int>(WSGDisplay::kMaxCaptures)
                << " - Horde " << static_cast<int>(d.hordeCaptures)
                << "/" << static_cast<int>(WSGDisplay::kMaxCaptures);
            break;
        }
        case BGSpecificType::ArathiBasin: {
            if (!abData_) return "Arathi Basin";
            const auto& d = *abData_;
            oss << "AB: Alliance " << d.allianceResources
                << "/" << ABDisplay::kMaxResources
                << " - Horde " << d.hordeResources
                << "/" << ABDisplay::kMaxResources;
            break;
        }
        case BGSpecificType::AlteracValley: {
            if (!avData_) return "Alterac Valley";
            const auto& d = *avData_;
            oss << "AV: Alliance " << d.allianceReinforcements
                << " - Horde " << d.hordeReinforcements;
            break;
        }
        case BGSpecificType::EyeOfTheStorm: {
            if (!eotsData_) return "Eye of the Storm";
            const auto& d = *eotsData_;
            oss << "EotS: Alliance " << d.allianceScore
                << "/" << EotSDisplay::kMaxScore
                << " - Horde " << d.hordeScore
                << "/" << EotSDisplay::kMaxScore;
            break;
        }
        case BGSpecificType::StrandOfTheAncients: {
            if (!sotaData_) return "Strand of the Ancients";
            const auto& d = *sotaData_;
            oss << "SotA: Round " << static_cast<int>(d.round)
                << " - " << (d.attackingFaction == 0 ? "Alliance" : "Horde")
                << " attacking";
            break;
        }
        case BGSpecificType::IsleOfConquest: {
            if (!iocData_) return "Isle of Conquest";
            const auto& d = *iocData_;
            oss << "IoC: Alliance " << d.allianceReinforcements
                << " - Horde " << d.hordeReinforcements;
            break;
        }
    }
    return oss.str();
}

bool BGSpecificDisplay::IsActive() const {
    return active_;
}

void BGSpecificDisplay::Reset() {
    active_ = false;
    wsgData_.reset();
    abData_.reset();
    avData_.reset();
    eotsData_.reset();
    sotaData_.reset();
    iocData_.reset();
    bgType_ = BGSpecificType::WarsongGulch;
}

}
