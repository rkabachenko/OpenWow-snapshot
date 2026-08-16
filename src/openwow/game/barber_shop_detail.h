
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace openwow::game {

enum class BarberCustomizeCategory : uint8_t {
    HairStyle  = 0,
    HairColor  = 1,
    FacialHair = 2,
    SkinColor  = 3,
    Face       = 4,
};

struct BarberOptionInfo {
    uint8_t     optionIndex  = 0;
    std::string displayName;
    uint32_t    previewValue = 0;
};

struct BarberCategoryState {
    BarberCustomizeCategory        category      = BarberCustomizeCategory::HairStyle;
    uint8_t                        currentIndex  = 0;
    uint8_t                        maxIndex      = 0;
    std::vector<BarberOptionInfo>  options;
    uint8_t                        originalIndex = 0;
};

class BarberShopDetail {
 public:
    static constexpr uint32_t kBaseCostCopper = 1000;

    BarberShopDetail() = default;

    void Open(uint8_t race, uint8_t gender,
              const std::map<BarberCustomizeCategory, uint8_t>& currentValues);
    void Close();
    [[nodiscard]] bool IsOpen() const;

    void SetCategoryOptions(BarberCustomizeCategory category,
                            const std::vector<BarberOptionInfo>& options,
                            uint8_t currentIndex);

    [[nodiscard]] BarberCategoryState GetCategoryState(BarberCustomizeCategory cat) const;

    void CycleNext(BarberCustomizeCategory cat);
    void CyclePrev(BarberCustomizeCategory cat);
    void SetCategoryIndex(BarberCustomizeCategory cat, uint8_t index);

    [[nodiscard]] bool HasChanges() const;
    [[nodiscard]] std::vector<BarberCustomizeCategory> GetChangedCategories() const;
    [[nodiscard]] uint32_t GetCost() const;

    void ResetToOriginal();
    void Confirm();

    [[nodiscard]] uint8_t GetRace() const;
    [[nodiscard]] uint8_t GetGender() const;

 private:
    bool    open_   = false;
    uint8_t race_   = 0;
    uint8_t gender_ = 0;

    std::map<BarberCustomizeCategory, BarberCategoryState> categories_;
};

}
