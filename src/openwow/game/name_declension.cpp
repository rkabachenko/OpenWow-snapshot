#include "openwow/game/name_declension.h"

#include "openwow/core/storm_utils.h"
#include "openwow/game/client_config.h"
#include "openwow/game/name_case.h"
#include "openwow/game/name_validation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game::declension {
namespace {

using Utf16View = std::u16string_view;

struct DeclensionRule {
  std::array<Utf16View, 9> suffix_groups;
  int gender = 2;
  std::array<int, 3> set_codes{};
};

struct DeclensionEndingSet {
  std::array<Utf16View, 5> forms;
};

struct SelectedRule {
  std::u16string lower_name;
  const DeclensionRule* rule = nullptr;
  std::size_t stem_length = 0;
  std::size_t compare_stem_length = 0;
};

constexpr int kFemaleGenderIndex = 1;
constexpr int kWildcardGenderIndex = 2;
constexpr int kUnavailableSetCode = 71;
constexpr std::size_t kRetailDeclensionBufferSize = 0x400;

constexpr std::array<DeclensionRule, 55> kDeclensionRules{{
    {{{u"и", u"у", u"ы", u"э", u"ю", u"аа", u"", u"", u""}}, 2, {{0, 71, 71}}},
    {{{u"ай", u"ей", u"уй", u"эй", u"юй", u"яй", u"ёй", u"", u""}}, 0, {{1, 0, 71}}},
    {{{u"ай", u"ей", u"уй", u"эй", u"юй", u"яй", u"", u"", u""}}, 1, {{0, 1, 71}}},
    {{{u"ок", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{2, 3, 0}}},
    {{{u"чек", u"шек", u"", u"", u"", u"", u"", u"", u""}}, 0, {{4, 5, 0}}},
    {{{u"чек", u"", u"", u"", u"", u"", u"", u"", u""}}, 1, {{4, 6, 0}}},
    {{{u"лек", u"мек", u"нек", u"рек", u"сек", u"тек", u"", u"", u""}}, 2, {{8, 7, 0}}},
    {{{u"аек", u"еек", u"иек", u"уек", u"ёек", u"", u"", u"", u""}}, 2, {{9, 10, 0}}},
    {{{u"аец", u"еец", u"иец", u"оец", u"уец", u"ыец", u"эец", u"юец", u"яец"}}, 2, {{11, 0, 71}}},
    {{{u"ец", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{12, 13, 0}}},
    {{{u"ка", u"га", u"ха", u"", u"", u"", u"", u"", u""}}, 2, {{14, 0, 71}}},
    {{{u"ия", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{15, 0, 71}}},
    {{{u"ея", u"ёя", u"", u"", u"", u"", u"", u"", u""}}, 2, {{16, 0, 71}}},
    {{{u"емя", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{17, 0, 71}}},
    {{{u"ой", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{18, 19, 0}}},
    {{{u"ый", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{20, 21, 0}}},
    {{{u"ий", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{22, 23, 52}}},
    {{{u"чая", u"щая", u"", u"", u"", u"", u"", u"", u""}}, 2, {{24, 0, 71}}},
    {{{u"ая", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{25, 26, 0}}},
    {{{u"яя", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{27, 28, 0}}},
    {{{u"ое", u"", u"", u"", u"", u"", u"", u"", u""}}, 0, {{29, 30, 0}}},
    {{{u"ое", u"", u"", u"", u"", u"", u"", u"", u""}}, 1, {{29, 31, 0}}},
    {{{u"ее", u"", u"", u"", u"", u"", u"", u"", u""}}, 0, {{32, 33, 0}}},
    {{{u"ее", u"", u"", u"", u"", u"", u"", u"", u""}}, 1, {{32, 34, 0}}},
    {{{u"о", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{0, 35, 71}}},
    {{{u"ие", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{37, 0, 0}}},
    {{{u"е", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{0, 38, 71}}},
    {{{u"я", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{39, 0, 71}}},
    {{{u"ь", u"", u"", u"", u"", u"", u"", u"", u""}}, 0, {{40, 41, 0}}},
    {{{u"ь", u"", u"", u"", u"", u"", u"", u"", u""}}, 1, {{41, 40, 0}}},
    {{{u"ча", u"ша", u"ща", u"жа", u"", u"", u"", u"", u""}}, 2, {{42, 0, 71}}},
    {{{u"ца", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{43, 0, 71}}},
    {{{u"а", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{44, 0, 71}}},
    {{{u"ч", u"щ", u"ж", u"ш", u"ж", u"", u"", u"", u""}}, 0, {{45, 46, 0}}},
    {{{u"ч", u"щ", u"ж", u"ш", u"ж", u"", u"", u"", u""}}, 1, {{0, 45, 46}}},
    {{{u"", u"", u"", u"", u"", u"", u"", u"", u""}}, 0, {{47, 0, 71}}},
    {{{u"", u"", u"", u"", u"", u"", u"", u"", u""}}, 1, {{0, 47, 71}}},
    {{{u"ийся", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{48, 0, 71}}},
    {{{u"ееся", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{49, 0, 71}}},
    {{{u"аяся", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{50, 0, 71}}},
    {{{u"иеся", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{51, 0, 71}}},
    {{{u"ё", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{0, 53, 71}}},
    {{{u"ень", u"", u"", u"", u"", u"", u"", u"", u""}}, 0, {{54, 55, 0}}},
    {{{u"ень", u"", u"", u"", u"", u"", u"", u"", u""}}, 1, {{0, 54, 56}}},
    {{{u"ёнь", u"", u"", u"", u"", u"", u"", u"", u""}}, 0, {{57, 58, 0}}},
    {{{u"ёнь", u"", u"", u"", u"", u"", u"", u"", u""}}, 1, {{0, 57, 59}}},
    {{{u"оть", u"", u"", u"", u"", u"", u"", u"", u""}}, 0, {{60, 61, 0}}},
    {{{u"оть", u"", u"", u"", u"", u"", u"", u"", u""}}, 1, {{61, 60, 0}}},
    {{{u"ще", u"ше", u"че", u"це", u"", u"", u"", u"", u""}}, 2, {{62, 0, 71}}},
    {{{u"лёк", u"мёк", u"нёк", u"рёк", u"сёк", u"тёк", u"", u"", u""}}, 2, {{63, 64, 0}}},
    {{{u"аёк", u"еёк", u"иёк", u"уёк", u"", u"", u"", u"", u""}}, 2, {{9, 10, 0}}},
    {{{u"аёц", u"еёц", u"иёц", u"оёц", u"уёц", u"ыёц", u"эёц", u"юёц", u"яёц"}}, 2, {{11, 0, 71}}},
    {{{u"лец", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{65, 66, 0}}},
    {{{u"лёц", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{67, 68, 0}}},
    {{{u"ёц", u"", u"", u"", u"", u"", u"", u"", u""}}, 2, {{69, 70, 0}}},
}};

constexpr std::array<DeclensionEndingSet, 71> kDeclensionEndings{{
    {{{u"", u"", u"", u"", u""}}},
    {{{u"я", u"ю", u"я", u"ем", u"е"}}},
    {{{u"ка", u"ку", u"ка", u"ком", u"ке"}}},
    {{{u"ока", u"оку", u"ока", u"оком", u"оке"}}},
    {{{u"ка", u"ку", u"ек", u"ком", u"ке"}}},
    {{{u"ека", u"еку", u"ека", u"еком", u"еке"}}},
    {{{u"ек", u"еку", u"ек", u"еком", u"еке"}}},
    {{{u"ька", u"ьку", u"ька", u"ьком", u"ьке"}}},
    {{{u"ека", u"еку", u"ека", u"еком", u"еке"}}},
    {{{u"йка", u"йку", u"йка", u"йком", u"йке"}}},
    {{{u"ека", u"еку", u"ека", u"еком", u"еке"}}},
    {{{u"йца", u"йцу", u"йца", u"йцем", u"йце"}}},
    {{{u"ца", u"цу", u"ца", u"цом", u"це"}}},
    {{{u"еца", u"ецу", u"еца", u"ецом", u"еце"}}},
    {{{u"и", u"е", u"у", u"ой", u"е"}}},
    {{{u"и", u"и", u"ю", u"ей", u"и"}}},
    {{{u"и", u"е", u"ю", u"ей", u"е"}}},
    {{{u"ени", u"ени", u"я", u"енем", u"ени"}}},
    {{{u"ого", u"ому", u"ого", u"ым", u"ом"}}},
    {{{u"оя", u"ою", u"оя", u"оем", u"ое"}}},
    {{{u"ого", u"ому", u"ого", u"ым", u"ом"}}},
    {{{u"ыя", u"ыю", u"ыя", u"ыем", u"ые"}}},
    {{{u"его", u"ему", u"его", u"им", u"ем"}}},
    {{{u"ия", u"ию", u"ия", u"ием", u"ии"}}},
    {{{u"ей", u"ей", u"ую", u"ей", u"ей"}}},
    {{{u"ой", u"ой", u"ую", u"ой", u"ой"}}},
    {{{u"аи", u"ае", u"аю", u"аей", u"ае"}}},
    {{{u"ей", u"ей", u"юю", u"ей", u"ей"}}},
    {{{u"яи", u"яе", u"яю", u"яей", u"яе"}}},
    {{{u"ого", u"ому", u"ое", u"ым", u"ом"}}},
    {{{u"я", u"ю", u"оя", u"оем", u"ое"}}},
    {{{u"ои", u"ое", u"ою", u"оей", u"ое"}}},
    {{{u"его", u"ему", u"его", u"им", u"ем"}}},
    {{{u"ея", u"ею", u"ея", u"еем", u"ее"}}},
    {{{u"еи", u"ее", u"ею", u"еей", u"ее"}}},
    {{{u"а", u"у", u"о", u"ом", u"е"}}},
    {{{u"я", u"ю", u"е", u"ем", u"е"}}},
    {{{u"я", u"ю", u"е", u"ем", u"и"}}},
    {{{u"я", u"ю", u"е", u"ем", u"е"}}},
    {{{u"и", u"е", u"ю", u"ей", u"е"}}},
    {{{u"я", u"ю", u"я", u"ем", u"е"}}},
    {{{u"и", u"и", u"ь", u"ью", u"и"}}},
    {{{u"и", u"е", u"у", u"ей", u"е"}}},
    {{{u"ы", u"е", u"у", u"ей", u"е"}}},
    {{{u"ы", u"е", u"у", u"ой", u"е"}}},
    {{{u"а", u"у", u"а", u"ем", u"е"}}},
    {{{u"а", u"у", u"а", u"ом", u"е"}}},
    {{{u"а", u"у", u"а", u"ом", u"е"}}},
    {{{u"егося", u"емуся", u"егося", u"имся", u"емся"}}},
    {{{u"егося", u"емуся", u"егося", u"имся", u"емся"}}},
    {{{u"ейся", u"ейся", u"уюся", u"ейся", u"ейся"}}},
    {{{u"ихся", u"имся", u"ихся", u"имися", u"ихся"}}},
    {{{u"ого", u"ому", u"ого", u"им", u"ом"}}},
    {{{u"я", u"ю", u"ё", u"ём", u"е"}}},
    {{{u"ня", u"ню", u"ня", u"нем", u"не"}}},
    {{{u"еня", u"еню", u"еня", u"енем", u"ене"}}},
    {{{u"ени", u"ени", u"ень", u"енью", u"ени"}}},
    {{{u"ня", u"ню", u"ня", u"нем", u"не"}}},
    {{{u"ёня", u"ёню", u"ёня", u"ёнем", u"ёне"}}},
    {{{u"ёни", u"ёни", u"ёнь", u"ёнью", u"ёни"}}},
    {{{u"тя", u"тю", u"тя", u"тем", u"те"}}},
    {{{u"оти", u"оти", u"оть", u"отью", u"оти"}}},
    {{{u"а", u"у", u"е", u"ем", u"е"}}},
    {{{u"ька", u"ьку", u"ька", u"ьком", u"ьке"}}},
    {{{u"ёка", u"ёку", u"ёка", u"ёком", u"ёке"}}},
    {{{u"ьца", u"ьцу", u"ьца", u"ьцем", u"ьце"}}},
    {{{u"еца", u"ецу", u"еца", u"ецом", u"еце"}}},
    {{{u"ьца", u"ьцу", u"ьца", u"ьцом", u"ьце"}}},
    {{{u"ёца", u"ёцу", u"ёца", u"ёцом", u"ёце"}}},
    {{{u"ца", u"цу", u"ца", u"цом", u"це"}}},
    {{{u"ёца", u"ёцу", u"ёца", u"ёцом", u"ёце"}}},
}};

bool IsRussianLocale() {
  return ClientConfig::Get().GetLocale() == "ruRU";
}

std::u16string Utf8ToUtf16(std::string_view utf8) {
  const std::string null_terminated(utf8);
  std::array<char16_t, kRetailDeclensionBufferSize> output{};
  core::StormUtf8ToUtf16Bounded(
      output.data(), static_cast<int>(output.size()), null_terminated.c_str(),
      -1, nullptr, nullptr);
  const auto end = std::find(output.begin(), output.end(), u'\0');
  return std::u16string(output.begin(), end);
}

std::string Utf16ToUtf8(std::u16string_view utf16) {
  std::array<char16_t, kRetailDeclensionBufferSize> input{};
  const auto payload_size =
      std::min(utf16.size(), input.size() - static_cast<std::size_t>(1));
  std::copy_n(utf16.begin(), payload_size, input.begin());

  std::array<char, kRetailDeclensionBufferSize> output{};
  core::StormUtf16ToUtf8Bounded(output.data(), output.size(), input.data(),
                                -1, nullptr, nullptr);
  const auto end = std::find(output.begin(), output.end(), '\0');
  return std::string(output.begin(), end);
}

bool EndsWith(std::u16string_view value, Utf16View suffix) {
  if (suffix.empty() || suffix.size() > value.size()) {
    return false;
  }
  return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

std::size_t MatchLengthForRule(std::u16string_view lower_name, const DeclensionRule& rule) {
  for (const Utf16View suffix : rule.suffix_groups) {
    if (!suffix.empty() && EndsWith(lower_name, suffix)) {
      return suffix.size();
    }
  }
  return 0;
}

SelectedRule SelectRule(std::string_view name, int gender_index) {
  SelectedRule result;
  result.lower_name = Utf8ToUtf16(name);
  std::transform(result.lower_name.begin(), result.lower_name.end(),
                 result.lower_name.begin(), RetailNameToLower);

  int rule_index = (gender_index == kFemaleGenderIndex) ? 36 : 35;
  std::size_t stem_length = result.lower_name.size();
  std::size_t best_match_length = 0;

  for (std::size_t i = 0; i < kDeclensionRules.size(); ++i) {
    const DeclensionRule& rule = kDeclensionRules[i];
    if (rule.gender != kWildcardGenderIndex &&
        gender_index != kWildcardGenderIndex && gender_index != rule.gender) {
      continue;
    }

    const std::size_t match_length = MatchLengthForRule(result.lower_name, rule);
    if (match_length > best_match_length) {
      best_match_length = match_length;
      rule_index = static_cast<int>(i);
      stem_length = result.lower_name.size() - match_length;
    }
  }

  result.rule = &kDeclensionRules[rule_index];
  result.stem_length = stem_length;

  result.compare_stem_length = stem_length;
  const std::size_t name_len = result.lower_name.size();
  if (name_len > 2) {
    std::size_t check_pos = 0;
    bool should_check = false;

    if (rule_index == 35 || rule_index == 36) {
      check_pos = name_len - 2;
      should_check = true;
    } else if (name_len > 3 && result.lower_name.back() == u'\u044C') {
      for (const auto& suffix : result.rule->suffix_groups) {
        if (!suffix.empty() && suffix.front() == u'\u044C') {
          check_pos = name_len - 3;
          should_check = true;
          break;
        }
      }
    }

    if (should_check && check_pos > 0) {
      const char16_t ch = result.lower_name[check_pos];
      if (ch == u'\u0435' || ch == u'\u043E' || ch == u'\u0451') {
        result.compare_stem_length = check_pos;
      }
    }
  }

  return result;
}

std::u16string BuildTitleCase(std::u16string_view value) {
  std::u16string result(value);
  if (!result.empty()) {
    result.front() = RetailNameToUpper(result.front());
  }
  return result;
}

}

int MapLuaGenderValueToIndex(const std::int32_t gender_value) {
  for (std::size_t i = 0; i < kLuaGenderValues.size(); ++i) {
    if (gender_value == kLuaGenderValues[i]) {
      return static_cast<int>(i);
    }
  }
  return kWildcardGenderIndex;
}

bool StartsWithCyrillicCodeUnit(const std::string_view text) {
  const std::u16string wide = Utf8ToUtf16(text);
  return !wide.empty() && wide.front() >= 0x0400u && wide.front() < 0x0500u;
}

int GetNumSets(std::string_view name, int gender_index) {
  if (!IsRussianLocale()) {
    return 0;
  }

  const SelectedRule selected_rule = SelectRule(name, gender_index);
  int count = 0;
  for (const int set_code : selected_rule.rule->set_codes) {
    if (set_code != kUnavailableSetCode) {
      ++count;
    }
  }
  return count;
}

bool BuildForms(std::string_view name, int gender_index,
                unsigned declension_set_index,
                std::array<std::string, 5>& out_forms) {
  if (!IsRussianLocale() || declension_set_index >= 3) {
    return false;
  }

  const SelectedRule selected_rule = SelectRule(name, gender_index);
  const int set_code = selected_rule.rule->set_codes[declension_set_index];
  if (set_code == kUnavailableSetCode ||
      set_code < 0 ||
      set_code >= static_cast<int>(kDeclensionEndings.size())) {
    return false;
  }

  if (set_code == 0) {
    const std::string normalized = Utf16ToUtf8(BuildTitleCase(selected_rule.lower_name));
    out_forms.fill(normalized);
    return true;
  }

  for (std::size_t form_index = 0; form_index < out_forms.size(); ++form_index) {
    std::u16string wide_form(selected_rule.lower_name.begin(),
                             selected_rule.lower_name.begin() +
                                 static_cast<std::ptrdiff_t>(selected_rule.stem_length));
    wide_form.append(kDeclensionEndings[set_code].forms[form_index]);
    out_forms[form_index] = Utf16ToUtf8(BuildTitleCase(wide_form));
  }

  return true;
}

std::uint8_t ValidateDeclinedCharacterForm(std::string_view base_name,
                                           std::string_view declined_form) {
  constexpr std::uint8_t kNameSuccess = 87;
  constexpr std::uint8_t kDeclensionMismatch = 103;

  if (!IsRussianLocale()) {
    return static_cast<std::uint8_t>(
        kNameSuccess
        + static_cast<std::uint8_t>(openwow::game::NameValidationResult::kFailure));
  }

  std::uint32_t code_unit_length = 0;
  int locale_group = -1;
  const auto validation_result = openwow::game::ValidateName(
      8, nullptr, std::string(declined_form), code_unit_length, locale_group,
      true, true, false, false, false, -1);
  if (validation_result != openwow::game::NameValidationResult::kOk) {
    return static_cast<std::uint8_t>(
        kNameSuccess + static_cast<std::uint8_t>(validation_result));
  }

  std::uint32_t base_limit = 12;
  if (locale_group == 3 || locale_group == 4) {
    base_limit = (locale_group == 4) ? 6u : 8u;
  }
  if (code_unit_length > base_limit + 5) {
    return static_cast<std::uint8_t>(
        kNameSuccess
        + static_cast<std::uint8_t>(openwow::game::NameValidationResult::kLengthExceeded));
  }

  const SelectedRule selected_rule = SelectRule(base_name, kWildcardGenderIndex);
  const std::u16string declined_wide = Utf8ToUtf16(declined_form);
  const auto compare_len = selected_rule.compare_stem_length;
  if (declined_wide.size() < compare_len) {
    return kDeclensionMismatch;
  }

  for (std::size_t i = 0; i < compare_len; ++i) {
    if (selected_rule.lower_name[i] != RetailNameToLower(declined_wide[i])) {
      return kDeclensionMismatch;
    }
  }

  if (declined_wide.size() - compare_len > 5) {
    return kDeclensionMismatch;
  }

  return kNameSuccess;
}

}
