#include "openwow/game/money_display.h"

#include "openwow/game/localization.h"

#include <algorithm>
#include <array>

namespace openwow::game {

static constexpr std::uint32_t kCopperPerSilver = 100;
static constexpr std::uint32_t kCopperPerGold = 10000;

void MoneyDisplay::SetMoney(std::uint32_t copper) { money_ = copper; }
std::uint32_t MoneyDisplay::GetMoney() const { return money_; }

std::uint32_t MoneyDisplay::GetGold() const {
  return money_ / kCopperPerGold;
}

std::uint32_t MoneyDisplay::GetSilver() const {
  return (money_ / kCopperPerSilver) % 100;
}

std::uint32_t MoneyDisplay::GetCopper() const {
  return money_ % kCopperPerSilver;
}

std::string MoneyDisplay::FormatMoney(std::uint32_t copper) {
  std::uint32_t g = copper / kCopperPerGold;
  std::uint32_t s = (copper / kCopperPerSilver) % 100;
  std::uint32_t c = copper % kCopperPerSilver;

  std::string result;
  if (g > 0) result += std::to_string(g) + "g";
  if (s > 0) {
    if (!result.empty()) result += ' ';
    result += std::to_string(s) + "s";
  }
  if (c > 0 || result.empty()) {
    if (!result.empty()) result += ' ';
    result += std::to_string(c) + "c";
  }
  return result;
}

std::string MoneyDisplay::FormatMoneyShort(std::uint32_t copper) {
  std::uint32_t g = copper / kCopperPerGold;
  std::uint32_t s = (copper / kCopperPerSilver) % 100;
  std::uint32_t c = copper % kCopperPerSilver;

  std::string result;
  if (g > 0) result += std::to_string(g) + "g";
  if (s > 0) {
    if (!result.empty()) result += ' ';
    result += std::to_string(s) + "s";
  }
  if (c > 0) {
    if (!result.empty()) result += ' ';
    result += std::to_string(c) + "c";
  }
  if (result.empty()) result = "0c";
  return result;
}

std::string MoneyDisplay::FormatMoneyFull(std::uint32_t copper) {
  std::uint32_t g = copper / kCopperPerGold;
  std::uint32_t s = (copper / kCopperPerSilver) % 100;
  std::uint32_t c = copper % kCopperPerSilver;

  std::string result;
  if (g > 0) result += std::to_string(g) + " Gold";
  if (s > 0) {
    if (!result.empty()) result += ' ';
    result += std::to_string(s) + " Silver";
  }
  if (c > 0 || result.empty()) {
    if (!result.empty()) result += ' ';
    result += std::to_string(c) + " Copper";
  }
  return result;
}

std::string MoneyDisplay::FormatLocalizedCoinText(
    const std::uint32_t copper, const std::string_view separator) {
  if (copper == 0) {
    return {};
  }

  struct Denomination {
    const char* localization_key;
    std::uint32_t amount;
  };

  const std::array denominations{
      Denomination{"GOLD_AMOUNT", copper / kCopperPerGold},
      Denomination{"SILVER_AMOUNT", (copper / kCopperPerSilver) % 100u},
      Denomination{"COPPER_AMOUNT", copper % kCopperPerSilver},
  };

  auto& localization = Localization::Get();
  std::string result;
  for (const auto& denomination : denominations) {
    if (denomination.amount == 0) {
      continue;
    }

    const std::string format =
        localization.GetString(denomination.localization_key, "");
    if (format.empty()) {
      return {};
    }

    if (!result.empty()) {
      result += separator;
    }
    result += localization.FormatString(
        format, {std::to_string(denomination.amount)});
  }

  return result;
}

const char* MoneyDisplay::GetCoinIconName(std::int32_t copper) {
  if (copper < 10) return "INV_Misc_Coin_05";
  if (copper < 100) return "INV_Misc_Coin_06";
  if (copper < 1000) return "INV_Misc_Coin_03";
  if (copper < 10000) return "INV_Misc_Coin_04";
  if (copper < 100000) return "INV_Misc_Coin_01";
  return "INV_Misc_Coin_02";
}

std::string MoneyDisplay::GetCoinIconPath(std::int32_t copper) {
  return std::string("Interface\\Icons\\") + GetCoinIconName(copper);
}

void MoneyDisplay::AddMoney(std::uint32_t copper) {
  money_ += copper;
  sessionGained_ += copper;
  recentChange_ = static_cast<std::int32_t>(copper);
  recentChangeFade_ = 1.0f;
}

void MoneyDisplay::RemoveMoney(std::uint32_t copper) {
  std::uint32_t actual = std::min(copper, money_);
  money_ -= actual;
  sessionSpent_ += actual;
  recentChange_ = -static_cast<std::int32_t>(actual);
  recentChangeFade_ = 1.0f;
}

std::uint32_t MoneyDisplay::GetSessionGained() const {
  return sessionGained_;
}

std::uint32_t MoneyDisplay::GetSessionSpent() const { return sessionSpent_; }

std::int32_t MoneyDisplay::GetNetGain() const {
  return static_cast<std::int32_t>(sessionGained_) -
         static_cast<std::int32_t>(sessionSpent_);
}

float MoneyDisplay::GetGoldPerHour() const {
  if (sessionTime_ < 1.0f) return 0.0f;
  float netCopper = static_cast<float>(GetNetGain());
  float hours = sessionTime_ / 3600.0f;
  return (netCopper / static_cast<float>(kCopperPerGold)) / hours;
}

std::int32_t MoneyDisplay::GetRecentChange() const { return recentChange_; }

void MoneyDisplay::SetRecentChange(std::int32_t amount) {
  recentChange_ = amount;
  recentChangeFade_ = 1.0f;
}

float MoneyDisplay::GetRecentChangeFade() const { return recentChangeFade_; }

void MoneyDisplay::Update(float dt) {
  sessionTime_ += dt;
  if (recentChangeFade_ > 0.0f) {
    recentChangeFade_ -= dt / kFadeDuration;
    if (recentChangeFade_ < 0.0f) recentChangeFade_ = 0.0f;
  }
}

void MoneyDisplay::Reset() {
  money_ = 0;
  sessionGained_ = 0;
  sessionSpent_ = 0;
  sessionTime_ = 0.0f;
  recentChange_ = 0;
  recentChangeFade_ = 0.0f;
}

}
