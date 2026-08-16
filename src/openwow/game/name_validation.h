
#pragma once

#include <cstdint>
#include <string>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

enum class NameValidationResult : int {
  kOk = 0,
  kFailure = 1,
  kNoName = 2,
  kTooShort = 3,
  kTooShortForLocale = kTooShort,
  kTooLong = 4,
  kLengthExceeded = kTooLong,
  kInvalidCharacter = 5,
  kInvalidCharacters = kInvalidCharacter,
  kMixedLanguages = 6,
  kMixedLocales = kMixedLanguages,
  kProfanity = 8,
  kReserved = kProfanity,
  kLeadingApostrophe = 9,
  kMultipleApostrophes = 10,
  kTripleSameChar = kMultipleApostrophes,
  kTripleLetterDisallowed = 11,
  kLeadingSpace = 12,
  kConsecutiveSpaces = 13,
  kConsecutiveSoftHardSign = 14,
  kLeadingSoftHardSign = 15,
};

enum class NameLocale : int {
  kLatin = 0,
  kAsciiAlpha = 1,
  kRussian = 2,
  kKorean = 3,
  kCjk = 4,
  kUnknown = -1,
};

NameValidationResult ValidateName(std::uint32_t locale_index, const char16_t *allowed_extra,
                                  const std::string &name, std::uint32_t &out_length,
                                  int &out_locale, bool check_profanity, bool check_reserved,
                                  bool check_spam, bool skip_structure, bool check_force_english,
                                  int explicit_locale = -1);

void BindNameValidationDbcLoader(const openwow::data::dbc::DbcLoader *dbc,
                                 std::uint32_t locale_mask = 0,
                                 std::uint32_t default_locale_mask = 0);

std::uint32_t GetValidationDefaultLocaleMask();

bool MatchesChatProfanity(const std::string &text, std::uint32_t locale_index);

NameValidationResult ValidateGlueCharacterName(const std::string &name);

std::uint8_t ValidateGlueCharacterNameResultCode(const std::string &name);

NameValidationResult ValidatePetName(const std::string &name, std::uint32_t max_extra);

NameValidationResult ValidatePetitionName(std::uint32_t locale_index, const std::string &name);

NameValidationResult ValidateGuildBankTabName(const std::string &name);

bool CapitalizeName(const std::string &input, std::string &output);

}
