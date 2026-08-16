
#pragma once

#include "openwow/game/character_appearance_geoset_resolver.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class CharRace : uint8_t;
enum class CharGender : uint8_t;
class CharSectionStore;

enum class BarberDataCategory : uint8_t {
    HairStyle  = 0,
    HairColor  = 1,
    FacialHair = 2,
    SkinColor  = 3,
    Count      = 4
};

struct BarberCustomizeBounds {
    uint32_t raceId        = 0;
    uint32_t genderId      = 0;
    uint8_t  maxHairStyle  = 0;
    uint8_t  maxHairColor  = 0;
    uint8_t  maxFacialHair = 0;
    uint8_t  maxSkinColor  = 0;
};

struct DefaultAppearance {
    uint32_t raceId     = 0;
    uint32_t genderId   = 0;
    uint8_t  skinColor  = 0;
    uint8_t  face       = 0;
    uint8_t  hairStyle  = 0;
    uint8_t  hairColor  = 0;
    uint8_t  facialHair = 0;
};

struct BarberStylePreview {
    BarberDataCategory category;
    uint8_t  index         = 0;
    uint32_t geosetId      = 0;
    std::string texturePath;
    std::string displayName;
};

class BarberShopData {
public:
    BarberShopData() = default;

    void LoadWotLKDefaults();

    void SetSectionStore(const CharSectionStore* store);
    void SetAppearanceDbcStores(
        const openwow::data::dbc::DbcStore<openwow::data::dbc::CharHairGeosetsEntry>* hair_geosets,
        const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>* facial_hair_styles);

    void Clear();

    [[nodiscard]] BarberCustomizeBounds GetBounds(
        uint32_t raceId, uint32_t genderId) const;

    [[nodiscard]] uint8_t GetMaxHairStyle(uint32_t raceId, uint32_t genderId) const;
    [[nodiscard]] uint8_t GetMaxHairColor(uint32_t raceId, uint32_t genderId) const;
    [[nodiscard]] uint8_t GetMaxFacialHair(uint32_t raceId, uint32_t genderId) const;
    [[nodiscard]] uint8_t GetMaxSkinColor(uint32_t raceId, uint32_t genderId) const;

    [[nodiscard]] bool IsValidOption(
        uint32_t raceId, uint32_t genderId,
        BarberDataCategory category, uint8_t value) const;

    [[nodiscard]] DefaultAppearance GetDefaultAppearance(
        uint32_t raceId, uint32_t genderId) const;

    static constexpr uint32_t kBaseCostPerChange = 100;

    [[nodiscard]] static uint32_t CalculateCost(
        uint32_t playerLevel, uint32_t numChanges);

    [[nodiscard]] static uint32_t CalculateCostDetailed(
        uint32_t playerLevel,
        uint8_t oldHairStyle, uint8_t newHairStyle,
        uint8_t oldHairColor, uint8_t newHairColor,
        uint8_t oldFacialHair, uint8_t newFacialHair);

    [[nodiscard]] std::vector<BarberStylePreview> GetHairStylePreviews(
        uint32_t raceId, uint32_t genderId) const;

    [[nodiscard]] std::vector<BarberStylePreview> GetHairColorPreviews(
        uint32_t raceId, uint32_t genderId, uint8_t hairStyle) const;

    [[nodiscard]] std::vector<BarberStylePreview> GetFacialHairPreviews(
        uint32_t raceId, uint32_t genderId) const;

    [[nodiscard]] std::vector<uint32_t> GetSupportedRaces() const;
    [[nodiscard]] bool IsRaceSupported(uint32_t raceId) const;

    [[nodiscard]] static uint32_t HairStyleToGeosetId(
        uint32_t raceId, uint32_t genderId, uint8_t hairStyle,
        const openwow::data::dbc::DbcStore<openwow::data::dbc::CharHairGeosetsEntry>* hair_geosets = nullptr);

    struct FacialHairGeosets {
        uint32_t group100 = 0;
        uint32_t group300 = 0;
        uint32_t group200 = 0;
        uint32_t group1600 = 0;
        uint32_t group1700 = 0;
        uint32_t accessory702 = 0;
    };
    [[nodiscard]] static FacialHairGeosets FacialHairToGeosets(
        uint32_t raceId, uint32_t genderId, uint8_t facialHair,
        const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>* facial_hair_styles = nullptr);

private:
    std::vector<BarberCustomizeBounds> bounds_;
    std::vector<DefaultAppearance>     defaults_;
    const CharSectionStore*            sectionStore_ = nullptr;
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharHairGeosetsEntry>* hair_geosets_{
        nullptr};
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>*
        facial_hair_styles_{nullptr};

    const BarberCustomizeBounds* FindBounds(
        uint32_t raceId, uint32_t genderId) const;
    const DefaultAppearance* FindDefault(
        uint32_t raceId, uint32_t genderId) const;
};

[[nodiscard]] const char* BarberDataCategoryName(BarberDataCategory cat);

}
