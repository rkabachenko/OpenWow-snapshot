#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game {

class MoneyDisplay {
 public:
  void SetMoney(std::uint32_t copper);
  [[nodiscard]] std::uint32_t GetMoney() const;

  [[nodiscard]] std::uint32_t GetGold() const;
  [[nodiscard]] std::uint32_t GetSilver() const;
  [[nodiscard]] std::uint32_t GetCopper() const;

  [[nodiscard]] static std::string FormatMoney(std::uint32_t copper);

  [[nodiscard]] static std::string FormatMoneyShort(std::uint32_t copper);

  [[nodiscard]] static std::string FormatMoneyFull(std::uint32_t copper);

  [[nodiscard]] static std::string FormatLocalizedCoinText(
      std::uint32_t copper, std::string_view separator);

  [[nodiscard]] static const char* GetCoinIconName(std::int32_t copper);

  [[nodiscard]] static std::string GetCoinIconPath(std::int32_t copper);

  void AddMoney(std::uint32_t copper);

  void RemoveMoney(std::uint32_t copper);

  [[nodiscard]] std::uint32_t GetSessionGained() const;
  [[nodiscard]] std::uint32_t GetSessionSpent() const;

  [[nodiscard]] std::int32_t GetNetGain() const;

  [[nodiscard]] float GetGoldPerHour() const;

  [[nodiscard]] std::int32_t GetRecentChange() const;
  void SetRecentChange(std::int32_t amount);

  [[nodiscard]] float GetRecentChangeFade() const;

  void Update(float dt);

  void Reset();

 private:
  std::uint32_t money_{0};
  std::uint32_t sessionGained_{0};
  std::uint32_t sessionSpent_{0};
  float sessionTime_{0.0f};

  std::int32_t recentChange_{0};
  float recentChangeFade_{0.0f};

  static constexpr float kFadeDuration = 3.0f;
};

}
