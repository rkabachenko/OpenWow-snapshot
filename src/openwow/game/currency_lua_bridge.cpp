
#include "openwow/game/currency_lua_bridge.h"

#include <sstream>

namespace openwow::game {

CurrencyLuaBridge& CurrencyLuaBridge::Get() {
  static CurrencyLuaBridge instance;
  return instance;
}

std::uint32_t CurrencyLuaBridge::GetCurrencyListSize() const {
  return static_cast<std::uint32_t>(currencies_.size());
}

std::optional<CurrencyInfo> CurrencyLuaBridge::GetCurrencyListInfo(
    std::uint32_t index) const {
  if (index >= currencies_.size()) return std::nullopt;
  return currencies_[index];
}

std::optional<CurrencyInfo> CurrencyLuaBridge::GetCurrencyInfo(
    std::uint32_t tokenId) const {
  for (const auto& c : currencies_) {
    if (c.tokenId == tokenId) return c;
  }
  return std::nullopt;
}

void CurrencyLuaBridge::ExpandCurrencyList(std::uint32_t ,
                                            bool ) {

}

std::uint64_t CurrencyLuaBridge::GetMoney() const { return money_; }

std::string CurrencyLuaBridge::FormatMoney(std::uint64_t copper) {
  std::uint64_t gold = copper / 10000;
  std::uint64_t silver = (copper % 10000) / 100;
  std::uint64_t cop = copper % 100;

  std::ostringstream oss;
  bool any = false;
  if (gold > 0) {
    oss << gold << "g";
    any = true;
  }
  if (silver > 0) {
    if (any) oss << " ";
    oss << silver << "s";
    any = true;
  }
  if (cop > 0 || !any) {
    if (any) oss << " ";
    oss << cop << "c";
  }
  return oss.str();
}

std::uint32_t CurrencyLuaBridge::GetHonorCurrency() const { return honor_; }
std::uint32_t CurrencyLuaBridge::GetArenaCurrency() const { return arena_; }
std::uint32_t CurrencyLuaBridge::GetBadgeOfJustice() const { return badge_; }

void CurrencyLuaBridge::SetMoney(std::uint64_t copper) { money_ = copper; }

void CurrencyLuaBridge::AddCurrency(const CurrencyInfo& info) {
  currencies_.push_back(info);
}

void CurrencyLuaBridge::SetHonorCurrency(std::uint32_t amount) {
  honor_ = amount;
}
void CurrencyLuaBridge::SetArenaCurrency(std::uint32_t amount) {
  arena_ = amount;
}
void CurrencyLuaBridge::SetBadgeOfJustice(std::uint32_t amount) {
  badge_ = amount;
}

std::size_t CurrencyLuaBridge::GetCurrencyCount() const {
  return currencies_.size();
}

void CurrencyLuaBridge::Clear() {
  currencies_.clear();
  money_ = 0;
  honor_ = 0;
  arena_ = 0;
  badge_ = 0;
}

}
