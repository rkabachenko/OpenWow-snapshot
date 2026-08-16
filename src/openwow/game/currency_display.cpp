
#include "openwow/game/currency_display.h"

#include <algorithm>

namespace openwow::game {

void CurrencyDisplay::AddCurrency(const CurrencyDisplayEntry& entry) {
    for (auto& e : entries_) {
        if (e.currencyId == entry.currencyId) {
            e = entry;
            return;
        }
    }
    entries_.push_back(entry);
}

void CurrencyDisplay::SetCurrencyCount(uint32_t currencyId, uint32_t count) {
    for (auto& e : entries_) {
        if (e.currencyId == currencyId) {
            e.count = count;
            return;
        }
    }
}

std::optional<CurrencyDisplayEntry> CurrencyDisplay::GetCurrency(uint32_t currencyId) const {
    for (const auto& e : entries_) {
        if (e.currencyId == currencyId) return e;
    }
    return std::nullopt;
}

std::vector<CurrencyDisplayEntry> CurrencyDisplay::GetAllCurrencies() const {
    return entries_;
}

std::vector<CurrencyDisplayEntry> CurrencyDisplay::GetVisibleCurrencies() const {
    std::vector<CurrencyDisplayEntry> result;
    for (const auto& e : entries_) {
        if (!e.isHidden) result.push_back(e);
    }
    return result;
}

std::vector<CurrencyDisplayEntry> CurrencyDisplay::GetCurrenciesByCategory(
    const std::string& category) const {
    std::vector<CurrencyDisplayEntry> result;
    for (const auto& e : entries_) {
        if (e.category == category) result.push_back(e);
    }
    return result;
}

uint32_t CurrencyDisplay::GetCurrencyCount(uint32_t currencyId) const {
    for (const auto& e : entries_) {
        if (e.currencyId == currencyId) return e.count;
    }
    return 0;
}

bool CurrencyDisplay::IsAtMax(uint32_t currencyId) const {
    for (const auto& e : entries_) {
        if (e.currencyId == currencyId) {
            return e.maxCount > 0 && e.count >= e.maxCount;
        }
    }
    return false;
}

void CurrencyDisplay::SetHidden(uint32_t currencyId, bool hidden) {
    for (auto& e : entries_) {
        if (e.currencyId == currencyId) {
            e.isHidden = hidden;
            return;
        }
    }
}

void CurrencyDisplay::AddToTracked(uint32_t currencyId) {
    if (tracked_.size() >= kMaxTracked) return;
    if (IsTracked(currencyId)) return;
    tracked_.push_back(currencyId);
}

void CurrencyDisplay::RemoveFromTracked(uint32_t currencyId) {
    tracked_.erase(std::remove(tracked_.begin(), tracked_.end(), currencyId),
                   tracked_.end());
}

std::vector<uint32_t> CurrencyDisplay::GetTrackedCurrencies() const {
    return tracked_;
}

bool CurrencyDisplay::IsTracked(uint32_t currencyId) const {
    return std::find(tracked_.begin(), tracked_.end(), currencyId) != tracked_.end();
}

void CurrencyDisplay::Reset() {
    entries_.clear();
    tracked_.clear();
}

}
