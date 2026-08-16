
#include "openwow/game/barber_shop_detail.h"

#include <algorithm>

namespace openwow::game {

void BarberShopDetail::Open(uint8_t race, uint8_t gender,
                            const std::map<BarberCustomizeCategory, uint8_t>& currentValues) {
    race_   = race;
    gender_ = gender;
    open_   = true;
    categories_.clear();

    for (const auto& [cat, idx] : currentValues) {
        BarberCategoryState st;
        st.category      = cat;
        st.currentIndex  = idx;
        st.maxIndex      = 0;
        st.originalIndex = idx;
        categories_[cat] = st;
    }
}

void BarberShopDetail::Close() {
    open_ = false;
}

bool BarberShopDetail::IsOpen() const {
    return open_;
}

void BarberShopDetail::SetCategoryOptions(BarberCustomizeCategory category,
                                          const std::vector<BarberOptionInfo>& options,
                                          uint8_t currentIndex) {
    auto& st         = categories_[category];
    st.category      = category;
    st.options       = options;
    st.maxIndex      = options.empty() ? 0 : static_cast<uint8_t>(options.size() - 1);
    st.currentIndex  = std::min(currentIndex, st.maxIndex);
    st.originalIndex = st.currentIndex;
}

BarberCategoryState BarberShopDetail::GetCategoryState(BarberCustomizeCategory cat) const {
    auto it = categories_.find(cat);
    if (it != categories_.end()) return it->second;
    return BarberCategoryState{cat, 0, 0, {}, 0};
}

void BarberShopDetail::CycleNext(BarberCustomizeCategory cat) {
    auto it = categories_.find(cat);
    if (it == categories_.end()) return;
    auto& st = it->second;
    if (st.maxIndex == 0) return;
    st.currentIndex = (st.currentIndex >= st.maxIndex) ? 0 : st.currentIndex + 1;
}

void BarberShopDetail::CyclePrev(BarberCustomizeCategory cat) {
    auto it = categories_.find(cat);
    if (it == categories_.end()) return;
    auto& st = it->second;
    if (st.maxIndex == 0) return;
    st.currentIndex = (st.currentIndex == 0) ? st.maxIndex : st.currentIndex - 1;
}

void BarberShopDetail::SetCategoryIndex(BarberCustomizeCategory cat, uint8_t index) {
    auto it = categories_.find(cat);
    if (it == categories_.end()) return;
    auto& st = it->second;
    st.currentIndex = std::min(index, st.maxIndex);
}

bool BarberShopDetail::HasChanges() const {
    for (const auto& [_, st] : categories_) {
        if (st.currentIndex != st.originalIndex) return true;
    }
    return false;
}

std::vector<BarberCustomizeCategory> BarberShopDetail::GetChangedCategories() const {
    std::vector<BarberCustomizeCategory> result;
    for (const auto& [cat, st] : categories_) {
        if (st.currentIndex != st.originalIndex) {
            result.push_back(cat);
        }
    }
    return result;
}

uint32_t BarberShopDetail::GetCost() const {
    uint32_t total = 0;
    for (const auto& [cat, st] : categories_) {
        if (st.currentIndex == st.originalIndex) continue;

        if (cat == BarberCustomizeCategory::HairColor) {
            total += kBaseCostCopper / 2;
        } else {
            total += kBaseCostCopper;
        }
    }
    return total;
}

void BarberShopDetail::ResetToOriginal() {
    for (auto& [_, st] : categories_) {
        st.currentIndex = st.originalIndex;
    }
}

void BarberShopDetail::Confirm() {

    for (auto& [_, st] : categories_) {
        st.originalIndex = st.currentIndex;
    }
    open_ = false;
}

uint8_t BarberShopDetail::GetRace() const   { return race_; }
uint8_t BarberShopDetail::GetGender() const { return gender_; }

}
