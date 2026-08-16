
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class CharRace : uint8_t;
enum class CharGender : uint8_t;

enum class CharSectionType : uint32_t {
    BaseSkin    = 0,
    Face        = 1,
    FacialHair  = 2,
    Hair        = 3,
    Underwear   = 4,
    Count       = 5
};

enum CharSectionFlags : uint32_t {
    CHAR_SECTION_FLAG_NONE          = 0x00,
    CHAR_SECTION_FLAG_NPC_ONLY      = 0x01,
    CHAR_SECTION_FLAG_DEATH_KNIGHT  = 0x04,
};

struct CharSectionEntry {
    uint32_t         id             = 0;
    uint32_t         raceId         = 0;
    uint32_t         sexId          = 0;
    CharSectionType  sectionType    = CharSectionType::BaseSkin;
    std::string      textureName[3];
    uint32_t         flags          = 0;
    uint32_t         variationIndex = 0;
    uint32_t         colorIndex     = 0;
};

struct CharSectionKey {
    uint32_t        raceId        = 0;
    uint32_t        sexId         = 0;
    CharSectionType sectionType   = CharSectionType::BaseSkin;
    uint32_t        variationIndex = 0;
    uint32_t        colorIndex    = 0;

    bool operator==(const CharSectionKey& o) const = default;
};

class CharSectionStore {
public:
    CharSectionStore() = default;

    void AddEntry(const CharSectionEntry& entry);

    void AddEntry(uint32_t id, uint32_t raceId, uint32_t sexId,
                  CharSectionType type,
                  const std::string& tex0, const std::string& tex1,
                  const std::string& tex2,
                  uint32_t flags, uint32_t variation, uint32_t color);

    void LoadWotLKDefaults();

    void Clear();

    [[nodiscard]] std::optional<CharSectionEntry> GetEntry(
        uint32_t raceId, uint32_t sexId, CharSectionType type,
        uint32_t variation, uint32_t color) const;

    [[nodiscard]] std::optional<CharSectionEntry> GetEntryById(uint32_t id) const;

    [[nodiscard]] std::vector<CharSectionEntry> GetEntriesForSection(
        uint32_t raceId, uint32_t sexId, CharSectionType type) const;

    [[nodiscard]] std::vector<uint32_t> GetVariationsForSection(
        uint32_t raceId, uint32_t sexId, CharSectionType type,
        uint32_t color) const;

    [[nodiscard]] std::vector<uint32_t> GetColorsForVariation(
        uint32_t raceId, uint32_t sexId, CharSectionType type,
        uint32_t variation) const;

    [[nodiscard]] uint32_t GetMaxVariation(
        uint32_t raceId, uint32_t sexId, CharSectionType type) const;

    [[nodiscard]] uint32_t GetMaxColor(
        uint32_t raceId, uint32_t sexId, CharSectionType type,
        uint32_t variation) const;

    [[nodiscard]] size_t GetEntryCount() const;

    [[nodiscard]] std::vector<CharSectionEntry> GetPlayerEntries(
        uint32_t raceId, uint32_t sexId, CharSectionType type) const;

    [[nodiscard]] std::vector<CharSectionEntry> GetDeathKnightEntries(
        uint32_t raceId, uint32_t sexId, CharSectionType type) const;

    [[nodiscard]] std::string GetPrimaryTexture(
        uint32_t raceId, uint32_t sexId, CharSectionType type,
        uint32_t variation, uint32_t color) const;

    struct TexturePaths {
        std::string tex0, tex1, tex2;
    };
    [[nodiscard]] std::optional<TexturePaths> GetTexturePaths(
        uint32_t raceId, uint32_t sexId, CharSectionType type,
        uint32_t variation, uint32_t color) const;

private:
    std::vector<CharSectionEntry> entries_;
};

[[nodiscard]] const char* CharSectionTypeName(CharSectionType type);

}
