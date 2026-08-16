
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace openwow::game {

enum class Locale : std::uint8_t {
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
  Unknown = 12,
  NumLocales = 12,
};

inline constexpr std::size_t kStockMessageFormatBufferBytes = 0xBB8;

int FormatRuntimeStringTemplateInto(char* buffer, std::size_t buffer_bytes,
                                    const char* format, ...);

[[nodiscard]] std::string ExpandLocalizedTextTags(std::string_view text,
                                                  Locale locale);

[[nodiscard]] int GetPluralRuleMode(Locale locale);

[[nodiscard]] int SelectPluralFormIndex(int value, int plural_rule_mode);

[[nodiscard]] int SelectPluralFormIndex(int value);

[[nodiscard]] std::string ResolveLocalizedGlobalString(lua_State* state,
                                                       std::string_view token,
                                                       int ordinal = -1,
                                                       int gender = 0);

void SyncLocalizedGlobalStrings(lua_State* state);

class Localization {
 public:
  static Localization& Get();

  void SetLocale(Locale locale);
  [[nodiscard]] Locale GetLocale() const;
  [[nodiscard]] std::string GetLocaleName() const;
  [[nodiscard]] std::uint8_t GetLocaleIndex() const;

  void SetString(const std::string& key, const std::string& value);
  void MergeStrings(std::unordered_map<std::string, std::string> strings);
  [[nodiscard]] std::string GetString(const std::string& key) const;
  [[nodiscard]] std::string GetString(const std::string& key,
                                      const std::string& defaultValue) const;
  [[nodiscard]] bool HasString(const std::string& key) const;

  [[nodiscard]] std::string FormatString(
      const std::string& format,
      const std::vector<std::string>& args) const;

  static std::string WrapColor(const std::string& text,
                               const std::string& colorHex);
  static std::string StripColors(const std::string& text);
  static std::string StripHyperlinks(const std::string& text);

  void LoadGlobalStrings();

  [[nodiscard]] std::size_t GetStringCount() const;
  void Clear();

 private:
  Localization() = default;

  Locale locale_ = Locale::enUS;
  std::unordered_map<std::string, std::string> strings_;
  mutable std::mutex mutex_;
};

}
