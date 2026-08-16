#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct CurrencyInfo {
  std::uint32_t tokenId{0};
  std::string name;
  std::string icon;
  std::uint32_t count{0};
  std::uint32_t weeklyMax{0};
  std::uint32_t weeklyCount{0};
  std::uint32_t totalMax{0};
  bool isDiscovered{false};
  bool isUnused{false};
};

class CurrencyLuaBridge {
 public:
  static CurrencyLuaBridge& Get();

  [[nodiscard]] std::uint32_t GetCurrencyListSize() const;

  [[nodiscard]] std::optional<CurrencyInfo> GetCurrencyListInfo(
      std::uint32_t index) const;

  [[nodiscard]] std::optional<CurrencyInfo> GetCurrencyInfo(
      std::uint32_t tokenId) const;

  void ExpandCurrencyList(std::uint32_t index, bool expand);

  [[nodiscard]] std::uint64_t GetMoney() const;

  [[nodiscard]] static std::string FormatMoney(std::uint64_t copper);

  [[nodiscard]] std::uint32_t GetHonorCurrency() const;

  [[nodiscard]] std::uint32_t GetArenaCurrency() const;

  [[nodiscard]] std::uint32_t GetBadgeOfJustice() const;

  void SetMoney(std::uint64_t copper);

  void AddCurrency(const CurrencyInfo& info);

  void SetHonorCurrency(std::uint32_t amount);
  void SetArenaCurrency(std::uint32_t amount);
  void SetBadgeOfJustice(std::uint32_t amount);

  [[nodiscard]] std::size_t GetCurrencyCount() const;

  void Clear();

 private:
  CurrencyLuaBridge() = default;

  std::vector<CurrencyInfo> currencies_;
  std::uint64_t money_{0};
  std::uint32_t honor_{0};
  std::uint32_t arena_{0};
  std::uint32_t badge_{0};
};

}
