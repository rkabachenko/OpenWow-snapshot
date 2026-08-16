#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::core {

enum class GameLocale : uint8_t {
  enUS = 0,
  koKR = 1,
  frFR = 2,
  deDE = 3,
  zhCN = 4,
  zhTW = 5,
  esES = 6,
  esMX = 7,
  ruRU = 8,
  jaJP = 9,
  ptBR = 10,
  itIT = 11,
  enGB = 12,
};

inline constexpr uint8_t kNumGameLocales = 12;

class LocaleSystem {
 public:
  LocaleSystem() = default;

  void SetLocale(GameLocale locale);
  [[nodiscard]] GameLocale GetLocale() const;

  [[nodiscard]] std::string GetLocaleString() const;

  bool SetLocaleFromString(const std::string& locale_str);

  [[nodiscard]] std::string GetLocalePath() const;

  [[nodiscard]] std::string GetLocalizedString(const std::string& key) const;
  void SetLocalizedString(const std::string& key, const std::string& value);

  void LoadStringTable(
      const std::vector<std::pair<std::string, std::string>>& entries);

  [[nodiscard]] uint32_t GetStringCount() const;
  [[nodiscard]] bool HasString(const std::string& key) const;

  [[nodiscard]] static std::vector<GameLocale> GetSupportedLocales();

  [[nodiscard]] static bool IsLocaleSupported(GameLocale locale);

  [[nodiscard]] static std::string GetLocaleName(GameLocale locale);

  [[nodiscard]] static std::string GetFontForLocale(GameLocale locale);

  [[nodiscard]] static bool NeedsSpecialRendering(GameLocale locale);

  [[nodiscard]] static std::string GetDateFormat(GameLocale locale);

  [[nodiscard]] static std::string GetTimeFormat(GameLocale locale);

  [[nodiscard]] std::string FormatMoney(uint32_t copper) const;

  void Reset();

 private:
  GameLocale locale_ = GameLocale::enUS;
  std::unordered_map<std::string, std::string> strings_;
};

}
