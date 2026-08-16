
#include "openwow/core/locale_system.h"

#include <algorithm>
#include <array>
#include <sstream>

namespace openwow::core {

struct LocaleEntry {
  GameLocale locale;
  const char* code;
  const char* name;
  const char* font;
  const char* date_fmt;
  const char* time_fmt;
  bool cjk;
};

static constexpr std::array<LocaleEntry, 13> kLocaleTable = {{
    {GameLocale::enUS, "enUS", "English", "FRIZQT__.TTF", "MM/DD/YYYY",
     "h:mm A", false},
    {GameLocale::koKR, "koKR", "한국어", "2002.TTF", "YYYY/MM/DD", "A h:mm",
     true},
    {GameLocale::frFR, "frFR", "Français", "FRIZQT__.TTF", "DD/MM/YYYY",
     "HH:mm", false},
    {GameLocale::deDE, "deDE", "Deutsch", "FRIZQT__.TTF", "DD.MM.YYYY",
     "HH:mm", false},
    {GameLocale::zhCN, "zhCN", "简体中文", "ARKai_T.TTF", "YYYY/MM/DD",
     "HH:mm", true},
    {GameLocale::zhTW, "zhTW", "繁體中文", "bLEI00D.TTF", "YYYY/MM/DD",
     "A h:mm", true},
    {GameLocale::esES, "esES", "Español", "FRIZQT__.TTF", "DD/MM/YYYY",
     "H:mm", false},
    {GameLocale::esMX, "esMX", "Español (MX)", "FRIZQT__.TTF", "DD/MM/YYYY",
     "h:mm A", false},
    {GameLocale::ruRU, "ruRU", "Русский", "FRIZQT___CYR.TTF", "DD.MM.YYYY",
     "HH:mm", false},
    {GameLocale::jaJP, "jaJP", "日本語", "ARKai_T.TTF", "YYYY/MM/DD",
     "HH:mm", true},
    {GameLocale::ptBR, "ptBR", "Português", "FRIZQT__.TTF", "DD/MM/YYYY",
     "HH:mm", false},
    {GameLocale::itIT, "itIT", "Italiano", "FRIZQT__.TTF", "DD/MM/YYYY",
     "HH:mm", false},
    {GameLocale::enGB, "enGB", "English (GB)", "FRIZQT__.TTF", "DD/MM/YYYY",
     "HH:mm", false},
}};

static const LocaleEntry& FindLocaleEntry(GameLocale locale) {
  for (const auto& e : kLocaleTable) {
    if (e.locale == locale) return e;
  }
  return kLocaleTable[0];
}

void LocaleSystem::SetLocale(GameLocale locale) { locale_ = locale; }

GameLocale LocaleSystem::GetLocale() const { return locale_; }

std::string LocaleSystem::GetLocaleString() const {
  return FindLocaleEntry(locale_).code;
}

bool LocaleSystem::SetLocaleFromString(const std::string& locale_str) {
  for (const auto& e : kLocaleTable) {
    if (locale_str == e.code) {
      locale_ = e.locale;
      return true;
    }
  }
  return false;
}

std::string LocaleSystem::GetLocalePath() const {
  return std::string("Data/") + FindLocaleEntry(locale_).code + "/";
}

std::string LocaleSystem::GetLocalizedString(const std::string& key) const {
  auto it = strings_.find(key);
  if (it != strings_.end()) return it->second;
  return key;
}

void LocaleSystem::SetLocalizedString(const std::string& key,
                                      const std::string& value) {
  strings_[key] = value;
}

void LocaleSystem::LoadStringTable(
    const std::vector<std::pair<std::string, std::string>>& entries) {
  for (const auto& [key, value] : entries) {
    strings_[key] = value;
  }
}

uint32_t LocaleSystem::GetStringCount() const {
  return static_cast<uint32_t>(strings_.size());
}

bool LocaleSystem::HasString(const std::string& key) const {
  return strings_.find(key) != strings_.end();
}

std::vector<GameLocale> LocaleSystem::GetSupportedLocales() {
  std::vector<GameLocale> result;
  result.reserve(kLocaleTable.size());
  for (const auto& e : kLocaleTable) {
    result.push_back(e.locale);
  }
  return result;
}

bool LocaleSystem::IsLocaleSupported(GameLocale locale) {
  for (const auto& e : kLocaleTable) {
    if (e.locale == locale) return true;
  }
  return false;
}

std::string LocaleSystem::GetLocaleName(GameLocale locale) {
  return FindLocaleEntry(locale).name;
}

std::string LocaleSystem::GetFontForLocale(GameLocale locale) {
  return FindLocaleEntry(locale).font;
}

bool LocaleSystem::NeedsSpecialRendering(GameLocale locale) {
  return FindLocaleEntry(locale).cjk;
}

std::string LocaleSystem::GetDateFormat(GameLocale locale) {
  return FindLocaleEntry(locale).date_fmt;
}

std::string LocaleSystem::GetTimeFormat(GameLocale locale) {
  return FindLocaleEntry(locale).time_fmt;
}

std::string LocaleSystem::FormatMoney(uint32_t copper) const {
  uint32_t gold = copper / 10000;
  uint32_t silver = (copper % 10000) / 100;
  uint32_t remaining_copper = copper % 100;

  std::ostringstream oss;
  bool need_space = false;

  if (gold > 0) {
    oss << gold << "g";
    need_space = true;
  }
  if (silver > 0 || gold > 0) {
    if (need_space) oss << " ";
    oss << silver << "s";
    need_space = true;
  }
  if (need_space) oss << " ";
  oss << remaining_copper << "c";

  return oss.str();
}

void LocaleSystem::Reset() {
  locale_ = GameLocale::enUS;
  strings_.clear();
}

}
