
#include "openwow/game/ammo_display.h"

#include <algorithm>
#include <utility>

namespace openwow::game {

void AmmoDisplay::SetAmmo(std::uint32_t itemId, std::string name,
                           std::uint32_t iconId, AmmoType type) {
    itemId_ = itemId;
    name_   = std::move(name);
    iconId_ = iconId;
    type_   = type;

    count_ = 0;
    dps_   = 0.0f;
}

void AmmoDisplay::ClearAmmo() {
    itemId_ = 0;
    name_.clear();
    iconId_ = 0;
    type_   = AmmoType::None;
    count_  = 0;
    dps_    = 0.0f;
}

std::uint32_t AmmoDisplay::GetAmmoItemId() const {
    return itemId_;
}

const std::string& AmmoDisplay::GetAmmoName() const {
    return name_;
}

AmmoType AmmoDisplay::GetAmmoType() const {
    return type_;
}

std::uint32_t AmmoDisplay::GetAmmoIconId() const {
    return iconId_;
}

void AmmoDisplay::SetAmmoCount(std::uint32_t count) {
    count_ = count;
}

std::uint32_t AmmoDisplay::GetAmmoCount() const {
    return count_;
}

bool AmmoDisplay::IsAmmoEquipped() const {
    return itemId_ != 0;
}

float AmmoDisplay::GetDPS() const {
    return dps_;
}

void AmmoDisplay::SetDPS(float dps) {

    dps_ = std::max(0.0f, dps);
}

bool AmmoDisplay::IsLow() const {
    return IsAmmoEquipped() && count_ < kAmmoLowThreshold;
}

std::uint32_t AmmoDisplay::GetLowThreshold() const {
    return kAmmoLowThreshold;
}

bool AmmoDisplay::NeedsAmmo() const {
    return hasRangedWeapon_ && !IsAmmoEquipped();
}

void AmmoDisplay::SetHasRangedWeapon(bool has) {
    hasRangedWeapon_ = has;
}

bool AmmoDisplay::HasRangedWeapon() const {
    return hasRangedWeapon_;
}

void AmmoDisplay::Reset() {
    ClearAmmo();
    hasRangedWeapon_ = false;
}

}
