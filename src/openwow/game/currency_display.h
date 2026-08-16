
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct CurrencyDisplayEntry {
    uint32_t    currencyId = 0;
    std::string name;
    uint32_t    count      = 0;
    uint32_t    maxCount   = 0;
    std::string icon;
    std::string category;
    bool        isHidden   = false;
};

class CurrencyDisplay {
public:

    void AddCurrency(const CurrencyDisplayEntry& entry);

    void SetCurrencyCount(uint32_t currencyId, uint32_t count);

    [[nodiscard]] std::optional<CurrencyDisplayEntry> GetCurrency(uint32_t currencyId) const;

    [[nodiscard]] std::vector<CurrencyDisplayEntry> GetAllCurrencies() const;

    [[nodiscard]] std::vector<CurrencyDisplayEntry> GetVisibleCurrencies() const;

    [[nodiscard]] std::vector<CurrencyDisplayEntry> GetCurrenciesByCategory(const std::string& category) const;

    [[nodiscard]] uint32_t GetCurrencyCount(uint32_t currencyId) const;

    [[nodiscard]] bool IsAtMax(uint32_t currencyId) const;

    void SetHidden(uint32_t currencyId, bool hidden);

    void AddToTracked(uint32_t currencyId);

    void RemoveFromTracked(uint32_t currencyId);

    [[nodiscard]] std::vector<uint32_t> GetTrackedCurrencies() const;

    [[nodiscard]] bool IsTracked(uint32_t currencyId) const;

    void Reset();

private:
    static constexpr uint32_t kMaxTracked = 3;

    std::vector<CurrencyDisplayEntry> entries_;
    std::vector<uint32_t>             tracked_;
};

}
