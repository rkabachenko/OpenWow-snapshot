
#include "openwow/game/barber_shop_data.h"
#include "openwow/game/char_sections.h"

#include <algorithm>

namespace openwow::game {

const char* BarberDataCategoryName(BarberDataCategory cat) {
    switch (cat) {
        case BarberDataCategory::HairStyle:  return "HairStyle";
        case BarberDataCategory::HairColor:  return "HairColor";
        case BarberDataCategory::FacialHair: return "FacialHair";
        case BarberDataCategory::SkinColor:  return "SkinColor";
        default:                             return "Unknown";
    }
}

static const struct {
    uint32_t raceId;

    uint8_t maxSkinM, maxFaceM, maxHairM, maxFacialM, maxHairColorM;

    uint8_t maxSkinF, maxFaceF, maxHairF, maxFacialF, maxHairColorF;
} kWotlkLimits[] = {
    {  1,  9, 11, 11,  8, 9,   9, 14, 18,  6, 9 },
    {  2,  8,  8,  6, 10, 7,   8,  8,  7,  6, 7 },
    {  3,  8,  9, 10, 10, 9,   8,  9, 13,  5, 9 },
    {  4,  8,  8,  6,  5, 7,   8,  8,  6,  9, 7 },
    {  5,  5,  9, 10, 16, 9,   5,  9,  9,  7, 9 },
    {  6, 18,  4,  7,  6, 2,  10,  3,  6,  4, 2 },
    {  7,  4,  6,  6,  7, 8,   4,  6,  6,  2, 8 },
    {  8,  5,  4,  5, 10, 9,   5,  5,  4,  5, 9 },
    { 10,  9,  9, 10, 10, 9,   9,  9, 12,  9, 9 },
    { 11, 13,  9,  7,  7, 6,  13,  9, 10,  6, 6 },
};

void BarberShopData::LoadWotLKDefaults() {
    Clear();

    for (const auto& r : kWotlkLimits) {

        bounds_.push_back({
            r.raceId, 0,
            r.maxHairM, r.maxHairColorM, r.maxFacialM, r.maxSkinM
        });

        bounds_.push_back({
            r.raceId, 1,
            r.maxHairF, r.maxHairColorF, r.maxFacialF, r.maxSkinF
        });

        defaults_.push_back({ r.raceId, 0, 0, 0, 0, 0, 0 });

        defaults_.push_back({ r.raceId, 1, 0, 0, 0, 0, 0 });
    }
}

void BarberShopData::SetSectionStore(const CharSectionStore* store) {
    sectionStore_ = store;
}

void BarberShopData::SetAppearanceDbcStores(
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharHairGeosetsEntry>* hair_geosets,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>* facial_hair_styles) {
    hair_geosets_ = hair_geosets;
    facial_hair_styles_ = facial_hair_styles;
}

void BarberShopData::Clear() {
    bounds_.clear();
    defaults_.clear();
    sectionStore_ = nullptr;
    hair_geosets_ = nullptr;
    facial_hair_styles_ = nullptr;
}

const BarberCustomizeBounds* BarberShopData::FindBounds(
    uint32_t raceId, uint32_t genderId) const {
    for (const auto& b : bounds_) {
        if (b.raceId == raceId && b.genderId == genderId) return &b;
    }
    return nullptr;
}

const DefaultAppearance* BarberShopData::FindDefault(
    uint32_t raceId, uint32_t genderId) const {
    for (const auto& d : defaults_) {
        if (d.raceId == raceId && d.genderId == genderId) return &d;
    }
    return nullptr;
}

BarberCustomizeBounds BarberShopData::GetBounds(
    uint32_t raceId, uint32_t genderId) const {
    auto* b = FindBounds(raceId, genderId);
    if (b) return *b;
    return {};
}

uint8_t BarberShopData::GetMaxHairStyle(uint32_t raceId, uint32_t genderId) const {
    auto* b = FindBounds(raceId, genderId);
    return b ? b->maxHairStyle : 0;
}

uint8_t BarberShopData::GetMaxHairColor(uint32_t raceId, uint32_t genderId) const {
    auto* b = FindBounds(raceId, genderId);
    return b ? b->maxHairColor : 0;
}

uint8_t BarberShopData::GetMaxFacialHair(uint32_t raceId, uint32_t genderId) const {
    auto* b = FindBounds(raceId, genderId);
    return b ? b->maxFacialHair : 0;
}

uint8_t BarberShopData::GetMaxSkinColor(uint32_t raceId, uint32_t genderId) const {
    auto* b = FindBounds(raceId, genderId);
    return b ? b->maxSkinColor : 0;
}

bool BarberShopData::IsValidOption(
    uint32_t raceId, uint32_t genderId,
    BarberDataCategory category, uint8_t value) const {
    auto* b = FindBounds(raceId, genderId);
    if (!b) return false;

    switch (category) {
        case BarberDataCategory::HairStyle:  return value <= b->maxHairStyle;
        case BarberDataCategory::HairColor:  return value <= b->maxHairColor;
        case BarberDataCategory::FacialHair: return value <= b->maxFacialHair;
        case BarberDataCategory::SkinColor:  return value <= b->maxSkinColor;
        default: return false;
    }
}

DefaultAppearance BarberShopData::GetDefaultAppearance(
    uint32_t raceId, uint32_t genderId) const {
    auto* d = FindDefault(raceId, genderId);
    if (d) return *d;
    return { raceId, genderId, 0, 0, 0, 0, 0 };
}

uint32_t BarberShopData::CalculateCost(uint32_t playerLevel, uint32_t numChanges) {
    if (numChanges == 0) return 0;
    return kBaseCostPerChange * playerLevel * numChanges;
}

uint32_t BarberShopData::CalculateCostDetailed(
    uint32_t playerLevel,
    uint8_t oldHairStyle, uint8_t newHairStyle,
    uint8_t oldHairColor, uint8_t newHairColor,
    uint8_t oldFacialHair, uint8_t newFacialHair) {
    uint32_t numChanges = 0;
    if (oldHairStyle != newHairStyle) ++numChanges;
    if (oldHairColor != newHairColor) ++numChanges;
    if (oldFacialHair != newFacialHair) ++numChanges;
    return CalculateCost(playerLevel, numChanges);
}

std::vector<BarberStylePreview> BarberShopData::GetHairStylePreviews(
    uint32_t raceId, uint32_t genderId) const {
    std::vector<BarberStylePreview> result;
    auto* b = FindBounds(raceId, genderId);
    if (!b) return result;

    for (uint8_t i = 0; i <= b->maxHairStyle; ++i) {
        BarberStylePreview preview;
        preview.category    = BarberDataCategory::HairStyle;
        preview.index       = i;
        preview.geosetId    = HairStyleToGeosetId(raceId, genderId, i, hair_geosets_);
        preview.displayName = "Style " + std::to_string(i);

        if (sectionStore_) {
            preview.texturePath = sectionStore_->GetPrimaryTexture(
                raceId, genderId, CharSectionType::Hair, i, 0);
        }

        result.push_back(preview);
    }
    return result;
}

std::vector<BarberStylePreview> BarberShopData::GetHairColorPreviews(
    uint32_t raceId, uint32_t genderId, uint8_t hairStyle) const {
    std::vector<BarberStylePreview> result;
    auto* b = FindBounds(raceId, genderId);
    if (!b) return result;

    for (uint8_t i = 0; i <= b->maxHairColor; ++i) {
        BarberStylePreview preview;
        preview.category    = BarberDataCategory::HairColor;
        preview.index       = i;
        preview.geosetId    = 0;
        preview.displayName = "Color " + std::to_string(i);

        if (sectionStore_) {
            preview.texturePath = sectionStore_->GetPrimaryTexture(
                raceId, genderId, CharSectionType::Hair, hairStyle, i);
        }

        result.push_back(preview);
    }
    return result;
}

std::vector<BarberStylePreview> BarberShopData::GetFacialHairPreviews(
    uint32_t raceId, uint32_t genderId) const {
    std::vector<BarberStylePreview> result;
    auto* b = FindBounds(raceId, genderId);
    if (!b) return result;

    for (uint8_t i = 0; i <= b->maxFacialHair; ++i) {
        BarberStylePreview preview;
        preview.category    = BarberDataCategory::FacialHair;
        preview.index       = i;
        auto geosets        = FacialHairToGeosets(raceId, genderId, i, facial_hair_styles_);
        preview.geosetId    = geosets.group100;
        preview.displayName = (i == 0) ? "None" : ("Style " + std::to_string(i));

        if (sectionStore_) {
            preview.texturePath = sectionStore_->GetPrimaryTexture(
                raceId, genderId, CharSectionType::FacialHair, i, 0);
        }

        result.push_back(preview);
    }
    return result;
}

std::vector<uint32_t> BarberShopData::GetSupportedRaces() const {
    std::vector<uint32_t> result;
    for (const auto& b : bounds_) {
        if (std::find(result.begin(), result.end(), b.raceId) == result.end()) {
            result.push_back(b.raceId);
        }
    }
    return result;
}

bool BarberShopData::IsRaceSupported(uint32_t raceId) const {
    for (const auto& b : bounds_) {
        if (b.raceId == raceId) return true;
    }
    return false;
}

uint32_t BarberShopData::HairStyleToGeosetId(
    uint32_t raceId, uint32_t genderId, uint8_t hairStyle,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharHairGeosetsEntry>* hair_geosets) {
    return ResolveHairGeosetId(raceId, genderId, hairStyle, hair_geosets);
}

BarberShopData::FacialHairGeosets
BarberShopData::FacialHairToGeosets(
    uint32_t raceId, uint32_t genderId, uint8_t facialHair,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>* facial_hair_styles) {
    const auto resolved = ResolveFacialHairGeosets(raceId, genderId, facialHair, facial_hair_styles);
    if (!resolved.found) {
        return {};
    }

    return {
        resolved.group100,
        resolved.group300,
        resolved.group200,
        resolved.group1600,
        resolved.group1700,
        resolved.accessory702,
    };
}

}
