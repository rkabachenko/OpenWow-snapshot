
#include "openwow/game/char_sections.h"

#include <algorithm>
#include <unordered_map>

namespace openwow::game {

const char* CharSectionTypeName(CharSectionType type) {
    switch (type) {
        case CharSectionType::BaseSkin:   return "BaseSkin";
        case CharSectionType::Face:       return "Face";
        case CharSectionType::FacialHair: return "FacialHair";
        case CharSectionType::Hair:       return "Hair";
        case CharSectionType::Underwear:  return "Underwear";
        default:                          return "Unknown";
    }
}

void CharSectionStore::AddEntry(const CharSectionEntry& entry) {
    entries_.push_back(entry);
}

void CharSectionStore::AddEntry(uint32_t id, uint32_t raceId, uint32_t sexId,
                                CharSectionType type,
                                const std::string& tex0,
                                const std::string& tex1,
                                const std::string& tex2,
                                uint32_t flags, uint32_t variation,
                                uint32_t color) {
    CharSectionEntry e;
    e.id              = id;
    e.raceId          = raceId;
    e.sexId           = sexId;
    e.sectionType     = type;
    e.textureName[0]  = tex0;
    e.textureName[1]  = tex1;
    e.textureName[2]  = tex2;
    e.flags           = flags;
    e.variationIndex  = variation;
    e.colorIndex      = color;
    entries_.push_back(e);
}

void CharSectionStore::Clear() {
    entries_.clear();
}

static const struct {
    uint32_t raceId;
    const char* raceName;
    uint32_t maxSkinM, maxSkinF;
    uint32_t maxFaceM, maxFaceF;
    uint32_t maxHairM, maxHairF;
    uint32_t maxFacialM, maxFacialF;
    uint32_t maxHairColorM, maxHairColorF;
} kRaceTable[] = {
    {  1, "Human",    9, 9,  11,14,  11,18,  8, 6,  9, 9 },
    {  2, "Orc",      8, 8,   8, 8,   6, 7, 10, 6,  7, 7 },
    {  3, "Dwarf",    8, 8,   9, 9,  10,13, 10, 5,  9, 9 },
    {  4, "NightElf", 8, 8,   8, 8,   6, 6,  5, 9,  7, 7 },
    {  5, "Scourge",  5, 5,   9, 9,  10, 9, 16, 7,  9, 9 },
    {  6, "Tauren",  18,10,   4, 3,   7, 6,  6, 4,  2, 2 },
    {  7, "Gnome",    4, 4,   6, 6,   6, 6,  7, 2,  8, 8 },
    {  8, "Troll",    5, 5,   4, 5,   5, 4, 10, 5,  9, 9 },
    { 10, "BloodElf", 9, 9,   9, 9,  10,12, 10, 9,  9, 9 },
    { 11, "Draenei", 13,13,   9, 9,   7,10,  7, 6,  6, 6 },
};

void CharSectionStore::LoadWotLKDefaults() {
    Clear();
    uint32_t nextId = 1;

    for (const auto& race : kRaceTable) {
        for (uint32_t sex = 0; sex <= 1; ++sex) {
            const char* genderStr = (sex == 0) ? "Male" : "Female";

            uint32_t maxSkin = (sex == 0) ? race.maxSkinM : race.maxSkinF;
            for (uint32_t v = 0; v <= maxSkin; ++v) {
                std::string path = std::string("Character\\") + race.raceName
                    + "\\" + genderStr + "\\" + race.raceName + genderStr
                    + "Skin" + std::to_string(v) + "_00.blp";
                AddEntry(nextId++, race.raceId, sex, CharSectionType::BaseSkin,
                         path, "", "", CHAR_SECTION_FLAG_NONE, v, 0);
            }

            uint32_t maxFace = (sex == 0) ? race.maxFaceM : race.maxFaceF;
            for (uint32_t v = 0; v <= maxFace; ++v) {
                for (uint32_t c = 0; c <= maxSkin; ++c) {
                    std::string upper = std::string("Character\\") + race.raceName
                        + "\\" + genderStr + "\\" + race.raceName + genderStr
                        + "FaceUpper" + std::to_string(v) + "_"
                        + std::to_string(c) + ".blp";
                    std::string lower = std::string("Character\\") + race.raceName
                        + "\\" + genderStr + "\\" + race.raceName + genderStr
                        + "FaceLower" + std::to_string(v) + "_"
                        + std::to_string(c) + ".blp";
                    AddEntry(nextId++, race.raceId, sex, CharSectionType::Face,
                             upper, lower, "", CHAR_SECTION_FLAG_NONE, v, c);
                }
            }

            uint32_t maxHair = (sex == 0) ? race.maxHairM : race.maxHairF;
            uint32_t maxHairColor = (sex == 0) ? race.maxHairColorM : race.maxHairColorF;
            for (uint32_t v = 0; v <= maxHair; ++v) {
                for (uint32_t c = 0; c <= maxHairColor; ++c) {
                    std::string tex = std::string("Character\\") + race.raceName
                        + "\\" + genderStr + "\\Hair" + std::to_string(v) + "_"
                        + std::to_string(c) + ".blp";
                    std::string scalp = std::string("Character\\") + race.raceName
                        + "\\" + genderStr + "\\Scalp" + std::to_string(v) + "_"
                        + std::to_string(c) + ".blp";
                    AddEntry(nextId++, race.raceId, sex, CharSectionType::Hair,
                             tex, scalp, "", CHAR_SECTION_FLAG_NONE, v, c);
                }
            }

            uint32_t maxFacial = (sex == 0) ? race.maxFacialM : race.maxFacialF;
            for (uint32_t v = 0; v <= maxFacial; ++v) {
                for (uint32_t c = 0; c <= maxHairColor; ++c) {
                    std::string lower = std::string("Character\\") + race.raceName
                        + "\\" + genderStr + "\\FacialHairLower" + std::to_string(v) + "_"
                        + std::to_string(c) + ".blp";
                    std::string upper = std::string("Character\\") + race.raceName
                        + "\\" + genderStr + "\\FacialHairUpper" + std::to_string(v) + "_"
                        + std::to_string(c) + ".blp";
                    std::string extra = std::string("Character\\") + race.raceName
                        + "\\" + genderStr + "\\FacialHairExtra" + std::to_string(v) + "_"
                        + std::to_string(c) + ".blp";
                    AddEntry(nextId++, race.raceId, sex,
                             CharSectionType::FacialHair,
                             lower, upper, extra, CHAR_SECTION_FLAG_NONE, v, c);
                }
            }

            for (uint32_t v = 0; v <= maxSkin; ++v) {
                std::string upper = std::string("Character\\") + race.raceName
                    + "\\" + genderStr + "\\UnderwearUpper" + std::to_string(v)
                    + ".blp";
                std::string lower = std::string("Character\\") + race.raceName
                    + "\\" + genderStr + "\\UnderwearLower" + std::to_string(v)
                    + ".blp";
                AddEntry(nextId++, race.raceId, sex, CharSectionType::Underwear,
                         upper, lower, "", CHAR_SECTION_FLAG_NONE, v, 0);
            }

            for (uint32_t v = 0; v <= maxSkin; ++v) {
                std::string path = std::string("Character\\") + race.raceName
                    + "\\" + genderStr + "\\" + race.raceName + genderStr
                    + "SkinDK" + std::to_string(v) + ".blp";
                AddEntry(nextId++, race.raceId, sex, CharSectionType::BaseSkin,
                         path, "", "", CHAR_SECTION_FLAG_DEATH_KNIGHT, v, 0);
            }
        }
    }
}

std::optional<CharSectionEntry> CharSectionStore::GetEntry(
    uint32_t raceId, uint32_t sexId, CharSectionType type,
    uint32_t variation, uint32_t color) const {
    for (const auto& e : entries_) {
        if (e.raceId == raceId && e.sexId == sexId &&
            e.sectionType == type &&
            e.variationIndex == variation && e.colorIndex == color &&
            !(e.flags & CHAR_SECTION_FLAG_NPC_ONLY)) {
            return e;
        }
    }
    return std::nullopt;
}

std::optional<CharSectionEntry> CharSectionStore::GetEntryById(uint32_t id) const {
    for (const auto& e : entries_) {
        if (e.id == id) return e;
    }
    return std::nullopt;
}

std::vector<CharSectionEntry> CharSectionStore::GetEntriesForSection(
    uint32_t raceId, uint32_t sexId, CharSectionType type) const {
    std::vector<CharSectionEntry> result;
    for (const auto& e : entries_) {
        if (e.raceId == raceId && e.sexId == sexId && e.sectionType == type) {
            result.push_back(e);
        }
    }
    return result;
}

std::vector<uint32_t> CharSectionStore::GetVariationsForSection(
    uint32_t raceId, uint32_t sexId, CharSectionType type,
    uint32_t color) const {
    std::vector<uint32_t> result;
    for (const auto& e : entries_) {
        if (e.raceId == raceId && e.sexId == sexId && e.sectionType == type &&
            e.colorIndex == color &&
            !(e.flags & CHAR_SECTION_FLAG_NPC_ONLY)) {
            if (std::find(result.begin(), result.end(), e.variationIndex)
                == result.end()) {
                result.push_back(e.variationIndex);
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<uint32_t> CharSectionStore::GetColorsForVariation(
    uint32_t raceId, uint32_t sexId, CharSectionType type,
    uint32_t variation) const {
    std::vector<uint32_t> result;
    for (const auto& e : entries_) {
        if (e.raceId == raceId && e.sexId == sexId && e.sectionType == type &&
            e.variationIndex == variation &&
            !(e.flags & CHAR_SECTION_FLAG_NPC_ONLY)) {
            if (std::find(result.begin(), result.end(), e.colorIndex)
                == result.end()) {
                result.push_back(e.colorIndex);
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

uint32_t CharSectionStore::GetMaxVariation(
    uint32_t raceId, uint32_t sexId, CharSectionType type) const {
    uint32_t maxV = 0;
    bool found = false;
    for (const auto& e : entries_) {
        if (e.raceId == raceId && e.sexId == sexId && e.sectionType == type &&
            !(e.flags & CHAR_SECTION_FLAG_NPC_ONLY)) {
            if (!found || e.variationIndex > maxV) {
                maxV = e.variationIndex;
                found = true;
            }
        }
    }
    return maxV;
}

uint32_t CharSectionStore::GetMaxColor(
    uint32_t raceId, uint32_t sexId, CharSectionType type,
    uint32_t variation) const {
    uint32_t maxC = 0;
    bool found = false;
    for (const auto& e : entries_) {
        if (e.raceId == raceId && e.sexId == sexId && e.sectionType == type &&
            e.variationIndex == variation &&
            !(e.flags & CHAR_SECTION_FLAG_NPC_ONLY)) {
            if (!found || e.colorIndex > maxC) {
                maxC = e.colorIndex;
                found = true;
            }
        }
    }
    return maxC;
}

size_t CharSectionStore::GetEntryCount() const {
    return entries_.size();
}

std::vector<CharSectionEntry> CharSectionStore::GetPlayerEntries(
    uint32_t raceId, uint32_t sexId, CharSectionType type) const {
    std::vector<CharSectionEntry> result;
    for (const auto& e : entries_) {
        if (e.raceId == raceId && e.sexId == sexId && e.sectionType == type &&
            !(e.flags & CHAR_SECTION_FLAG_NPC_ONLY)) {
            result.push_back(e);
        }
    }
    return result;
}

std::vector<CharSectionEntry> CharSectionStore::GetDeathKnightEntries(
    uint32_t raceId, uint32_t sexId, CharSectionType type) const {
    std::vector<CharSectionEntry> result;
    for (const auto& e : entries_) {
        if (e.raceId == raceId && e.sexId == sexId && e.sectionType == type &&
            (e.flags & CHAR_SECTION_FLAG_DEATH_KNIGHT)) {
            result.push_back(e);
        }
    }
    return result;
}

std::string CharSectionStore::GetPrimaryTexture(
    uint32_t raceId, uint32_t sexId, CharSectionType type,
    uint32_t variation, uint32_t color) const {
    auto entry = GetEntry(raceId, sexId, type, variation, color);
    if (entry) return entry->textureName[0];
    return {};
}

std::optional<CharSectionStore::TexturePaths> CharSectionStore::GetTexturePaths(
    uint32_t raceId, uint32_t sexId, CharSectionType type,
    uint32_t variation, uint32_t color) const {
    auto entry = GetEntry(raceId, sexId, type, variation, color);
    if (!entry) return std::nullopt;
    return TexturePaths{
        entry->textureName[0], entry->textureName[1], entry->textureName[2]
    };
}

}
