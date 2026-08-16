
#include "openwow/game/pvp_currency.h"

#include <algorithm>

namespace openwow::game {

void PvPCurrency::SetHonorPoints(uint32_t points) {
    honorPoints_ = std::min(points, kHonorCap);
}

uint32_t PvPCurrency::GetHonorPoints() const {
    return honorPoints_;
}

void PvPCurrency::AddHonor(uint32_t amount) {
    uint32_t room = (honorPoints_ < kHonorCap) ? (kHonorCap - honorPoints_) : 0;
    uint32_t add  = std::min(amount, room);
    honorPoints_ += add;
}

bool PvPCurrency::SpendHonor(uint32_t amount) {
    if (honorPoints_ < amount) return false;
    honorPoints_ -= amount;
    return true;
}

bool PvPCurrency::IsHonorCapped() const {
    return honorPoints_ >= kHonorCap;
}

void PvPCurrency::SetArenaPoints(uint32_t points) {
    arenaPoints_ = std::min(points, kArenaPointsCap);
}

uint32_t PvPCurrency::GetArenaPoints() const {
    return arenaPoints_;
}

void PvPCurrency::AddArenaPoints(uint32_t amount) {
    uint32_t room = (arenaPoints_ < kArenaPointsCap) ? (kArenaPointsCap - arenaPoints_) : 0;
    uint32_t add  = std::min(amount, room);
    arenaPoints_ += add;
}

bool PvPCurrency::SpendArenaPoints(uint32_t amount) {
    if (arenaPoints_ < amount) return false;
    arenaPoints_ -= amount;
    return true;
}

bool PvPCurrency::IsArenaPointsCapped() const {
    return arenaPoints_ >= kArenaPointsCap;
}

void PvPCurrency::SetWeeklyHonorEarned(uint32_t amount) {
    weeklyHonor_ = amount;
}

uint32_t PvPCurrency::GetWeeklyHonorEarned() const {
    return weeklyHonor_;
}

void PvPCurrency::SetWeeklyArenaPoints(uint32_t amount) {
    weeklyArenaPoints_ = amount;
}

uint32_t PvPCurrency::GetWeeklyArenaPoints() const {
    return weeklyArenaPoints_;
}

void PvPCurrency::ResetWeekly() {
    weeklyHonor_       = 0;
    weeklyArenaPoints_ = 0;
}

uint32_t PvPCurrency::GetSessionHonorGain() const {
    return sessionHonor_;
}

void PvPCurrency::AddSessionHonor(uint32_t amount) {
    sessionHonor_ += amount;
}

void PvPCurrency::Reset() {
    honorPoints_       = 0;
    arenaPoints_       = 0;
    weeklyHonor_       = 0;
    weeklyArenaPoints_ = 0;
    sessionHonor_      = 0;
}

}
