
#include "openwow/game/chat_display.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/channel_manager.h"
#include "openwow/game/chat_bubble.h"
#include "openwow/game/chat_link.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/chat_types.h"
#include "openwow/game/client_config.h"
#include "openwow/game/client_text_log_files.h"
#include "openwow/game/gm_ticket_chat_log.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/player_chat_flags.h"
#include "openwow/game/skill_line_ability_lookup.h"
#include "openwow/game/voice_chat.h"
#include "openwow/ui/game/chat_frame_manager.h"
#include "openwow/ui/game/chat_window_state.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/target_frame_data.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/retail_regex.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::game {

namespace {

constexpr char kFilterReplacementChars[] = {'#', '@', '$', '%', '&', '!', '*', '#'};
constexpr std::size_t kFilterReplacementCharCount = 8;
int g_filter_char_index = 0;

constexpr std::array<std::string_view, 9> kChatFilterLocales = {
    "enUS", "koKR", "frFR", "deDE", "zhCN", "zhTW", "esES", "enGB", "ruRU",
};
constexpr std::array<int, 18> kMatureFilterChatTypes = {
    1, 2, 51, 3, 39, 4, 5, 6, 7, 9, 10, 17, 23, 24, 40, 44, 45, 0,
};

struct CompiledChatExpression {
  std::string source;
  openwow::foundation::text::RetailRegex compiled;
};

struct SpamFilterCacheEntry {
  bool matched = false;
  std::string canonical_message;
};

struct MatureFilterCacheEntry {
  std::string source;
  bool matched = false;
  std::string filtered;
};

using CompiledChatExpressionList = std::vector<CompiledChatExpression>;
using MatureFilterExpressionTable =
    std::array<CompiledChatExpressionList, kChatFilterLocales.size()>;
using MatureFilterCacheBuckets =
    std::unordered_map<std::uint32_t, std::vector<MatureFilterCacheEntry>>;
using LanguageWordBuckets = std::unordered_map<std::size_t, std::vector<std::string>>;
using LanguageWordTable = std::unordered_map<std::uint32_t, LanguageWordBuckets>;
using LanguageWordBucketCounts = std::unordered_map<std::size_t, std::size_t>;
using LanguageWordTableLayout = std::unordered_map<std::uint32_t, LanguageWordBucketCounts>;
using LanguageSkillCatalog =
    std::array<std::vector<openwow::data::dbc::SkillLineAbilityEntry>, 39>;

std::vector<CompiledChatExpression> g_builtin_spam_filters;
std::vector<CompiledChatExpression> g_server_spam_filters;
std::unordered_map<std::string, SpamFilterCacheEntry> g_spam_filter_cache;
std::mutex g_spam_filter_mutex;

MatureFilterExpressionTable g_builtin_mature_filters;
MatureFilterCacheBuckets g_mature_filter_cache;
std::mutex g_mature_filter_mutex;

LanguageWordTable g_language_words;
LanguageSkillCatalog g_language_skills;
const openwow::data::dbc::DbcLoader *g_chat_display_dbc = nullptr;
std::mutex g_language_word_mutex;

bool g_chat_initialized = false;
std::atomic<std::uint32_t> g_chat_display_message_counter{0};

constexpr std::uint32_t kLanguageHashModulo = 300;
constexpr std::uint32_t kSpellEffectApplyAura = 6;
constexpr std::uint32_t kAuraModLanguageComprehension = 244;
constexpr std::uint32_t kMaxLanguageComprehension = 300;

constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 14>
    kLanguageLearningSpells = {{
        {1u, 669u},
        {2u, 671u},
        {3u, 670u},
        {6u, 672u},
        {7u, 668u},
        {8u, 815u},
        {9u, 816u},
        {10u, 813u},
        {11u, 814u},
        {12u, 817u},
        {13u, 7340u},
        {14u, 7341u},
        {33u, 17737u},
        {35u, 29932u},
    }};
constexpr std::array<std::uint32_t, 16> kStormNibbleHashTable = {
    1215178478u, 3702134451u, 3784412911u, 539865051u,  874282439u, 473322243u,
    1089416503u, 1711103561u, 3590680951u, 2421083795u, 473432655u, 2566730299u,
    3808828135u, 2744848289u, 2543317599u, 3829679459u,
};

std::string ToLowerAscii(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const unsigned char c : text) {
    result.push_back(static_cast<char>(std::tolower(c)));
  }
  return result;
}

[[nodiscard]] std::string GetLocalizedGlobalString(const std::string_view key) {
  return Localization::Get().GetString(std::string(key), std::string(key));
}

[[nodiscard]] std::string CollapseEscapedPipes(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch == '|' && i + 1 < text.size() && text[i + 1] == '|') {
      result.push_back('|');
      ++i;
      continue;
    }
    result.push_back(ch);
  }
  return result;
}

[[nodiscard]] std::string SanitizeChatLogMessage(std::string_view message) {
  const std::string with_literal_pipes = CollapseEscapedPipes(message);
  const std::string without_colors = Localization::StripColors(with_literal_pipes);
  return ChatLinkSystem::StripLinks(without_colors);
}

[[nodiscard]] std::string TrimChatLogChannelPrefix(const std::string_view display_name) {
  if (display_name.empty()) {
    return {};
  }

  std::string trimmed(display_name);
  if (const std::size_t separator = trimmed.find(" - "); separator != std::string::npos) {
    trimmed.erase(separator);
  }
  return "[" + trimmed + "] ";
}

[[nodiscard]] const char *ResolveChatLogTypeToken(const int chat_type) {
  switch (chat_type) {
  case static_cast<int>(ChatMsg::kSay):
    return "SAY";
  case static_cast<int>(ChatMsg::kParty):
    return "PARTY";
  case static_cast<int>(ChatMsg::kRaid):
    return "RAID";
  case static_cast<int>(ChatMsg::kGuild):
    return "GUILD";
  case static_cast<int>(ChatMsg::kOfficer):
    return "OFFICER";
  case static_cast<int>(ChatMsg::kYell):
    return "YELL";
  case static_cast<int>(ChatMsg::kWhisper):
    return "WHISPER";
  case static_cast<int>(ChatMsg::kWhisperForeign):
    return "WHISPER_FOREIGN";
  case static_cast<int>(ChatMsg::kWhisperInform):
    return "WHISPER_INFORM";
  case static_cast<int>(ChatMsg::kEmote):
    return "EMOTE";
  case static_cast<int>(ChatMsg::kMonsterSay):
    return "MONSTER_SAY";
  case static_cast<int>(ChatMsg::kMonsterParty):
    return "MONSTER_PARTY";
  case static_cast<int>(ChatMsg::kMonsterYell):
    return "MONSTER_YELL";
  case static_cast<int>(ChatMsg::kMonsterWhisper):
    return "MONSTER_WHISPER";
  case static_cast<int>(ChatMsg::kMonsterEmote):
    return "MONSTER_EMOTE";
  case static_cast<int>(ChatMsg::kRaidLeader):
    return "RAID_LEADER";
  case static_cast<int>(ChatMsg::kRaidWarning):
    return "RAID_WARNING";
  case static_cast<int>(ChatMsg::kBattleground):
    return "BATTLEGROUND";
  case static_cast<int>(ChatMsg::kBattlegroundLeader):
    return "BATTLEGROUND_LEADER";
  case static_cast<int>(ChatMsg::kPartyLeader):
    return "PARTY_LEADER";
  case static_cast<int>(ChatMsg::kBnWhisper):
    return "BN_WHISPER";
  case static_cast<int>(ChatMsg::kBnWhisperInform):
    return "BN_WHISPER_INFORM";
  default:
    return nullptr;
  }
}

[[nodiscard]] std::string FormatChatLogPrefixFromTemplate(const std::string_view key,
                                                          const std::string_view sender_name) {
  const std::string format = GetLocalizedGlobalString(key);
  if (format == key) {
    return {};
  }
  return Localization::Get().FormatString(format, {std::string(sender_name)});
}

std::optional<CompiledChatExpression> CompileChatExpression(const std::string &expression,
                                                            const std::string_view log_prefix) {
  auto compiled =
      openwow::foundation::text::RetailRegex::Compile(expression);
  if (!compiled.has_value()) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
              std::string(log_prefix) + " expression \"" + expression + "\"");
    return std::nullopt;
  }

  return CompiledChatExpression{
      .source = expression,
      .compiled = std::move(*compiled),
  };
}

CompiledChatExpressionList CompileChatExpressions(const std::vector<std::string> &expressions,
                                                  const std::string_view log_prefix) {
  CompiledChatExpressionList compiled;
  compiled.reserve(expressions.size());
  for (const auto &expression : expressions) {
    if (auto regex = CompileChatExpression(expression, log_prefix); regex.has_value()) {
      compiled.push_back(std::move(*regex));
    }
  }
  return compiled;
}

CompiledChatExpressionList
CompileServerSpamExpressions(const std::vector<std::string> &expressions) {
  return CompileChatExpressions(expressions, "Chat spam filter: skipping invalid server");
}

CompiledChatExpressionList BuildBuiltinSpamExpressions(const openwow::data::dbc::DbcLoader *dbc) {
  if (dbc == nullptr) {
    return {};
  }

  std::vector<std::string> patterns;
  patterns.reserve(dbc->spam_messages().entries().size());
  for (const auto &entry : dbc->spam_messages().entries()) {
    patterns.emplace_back(entry.pattern);
  }
  return CompileChatExpressions(patterns, "Chat spam filter: skipping invalid");
}

MatureFilterExpressionTable
BuildBuiltinMatureExpressions(const openwow::data::dbc::DbcLoader *dbc) {
  MatureFilterExpressionTable compiled;
  if (dbc == nullptr) {
    return compiled;
  }

  for (const auto &entry : dbc->chat_profanity().entries()) {
    auto expression =
        CompileChatExpression(std::string(entry.text), "Chat mature filter: skipping invalid");
    if (!expression.has_value()) {
      continue;
    }

    if (entry.language == 0xFFFFFFFFu) {
      for (auto &locale_expressions : compiled) {
        auto locale_expression =
            CompileChatExpression(expression->source, "Chat mature filter: skipping invalid");
        if (locale_expression.has_value()) {
          locale_expressions.push_back(std::move(*locale_expression));
        }
      }
      continue;
    }

    if (entry.language >= compiled.size()) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                "Chat mature filter: skipping expression with unsupported locale index " +
                    std::to_string(entry.language));
      continue;
    }

    compiled[entry.language].push_back(std::move(*expression));
  }

  return compiled;
}

bool MatchesAnySpamExpression(const std::string &message,
                              const CompiledChatExpressionList &expressions) {
  for (const auto &expression : expressions) {
    if (expression.compiled.Matches(message)) {
      return true;
    }
  }
  return false;
}

void ClearSpamFilterCacheLocked() {
  g_spam_filter_cache.clear();
}

void ClearMatureFilterCacheLocked() {
  g_mature_filter_cache.clear();
}

std::size_t GetCurrentChatFilterLocaleIndex() {
  const std::string locale = ClientConfig::Get().GetLocale();
  for (std::size_t i = 0; i < kChatFilterLocales.size(); ++i) {
    if (openwow::core::SStrCmpI(locale.c_str(), kChatFilterLocales[i].data(), 0x7FFFFFFFu) == 0) {
      return i;
    }
  }
  return 0;
}

bool LocaleUsesAsianLanguageChunking() {
  const auto locale_index = GetCurrentChatFilterLocaleIndex();
  return locale_index == 4 || locale_index == 5;
}

bool LocaleUsesBytewiseReplacementCasing() {
  const auto locale_index = GetCurrentChatFilterLocaleIndex();
  return locale_index == 1 || locale_index == 4 || locale_index == 5;
}

std::size_t ResolveMaxOutputBytes(const std::size_t output_limit) {
  if (output_limit == 0) {
    return 0;
  }
  if (output_limit == std::numeric_limits<std::size_t>::max()) {
    return output_limit;
  }
  return output_limit - 1;
}

void AppendTruncatedBytes(std::string *out, const std::string_view text,
                          const std::size_t max_output_bytes) {
  if (out == nullptr || text.empty() || max_output_bytes == 0 || out->size() >= max_output_bytes) {
    return;
  }

  const std::size_t remaining = max_output_bytes - out->size();
  out->append(text.data(), std::min(text.size(), remaining));
}

void AppendTruncatedByte(std::string *out, const char value, const std::size_t max_output_bytes) {
  if (out == nullptr || max_output_bytes == 0 || out->size() >= max_output_bytes) {
    return;
  }

  out->push_back(value);
}

bool IsAsciiUpperByte(const unsigned char value) {
  return value >= 'A' && value <= 'Z';
}

char ToAsciiUpper(const char value) {
  return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
}

char ToLanguageUpperByte(const char value) {
  const unsigned char byte = static_cast<unsigned char>(value);
  if (byte >= 'a' && byte <= 'z') {
    return static_cast<char>(byte - 32);
  }
  if (byte >= 224 && byte <= 254) {
    return static_cast<char>(byte - 32);
  }
  if (byte == 0x9C) {
    return static_cast<char>(0x8C);
  }
  return value;
}

char ToLanguageLowerByte(const char value) {
  const unsigned char byte = static_cast<unsigned char>(value);
  if (byte >= 'A' && byte <= 'Z') {
    return static_cast<char>(byte + 32);
  }
  if (byte >= 192 && byte <= 222) {
    return static_cast<char>(byte + 32);
  }
  if (byte == 0x8C) {
    return static_cast<char>(0x9C);
  }
  return value;
}

struct Utf8Step {
  std::size_t size = 0;
  std::uint32_t raw = 0;
  std::uint32_t upper = 0;
};

Utf8Step ReadUtf8Step(const std::string_view text, const std::size_t offset) {
  if (offset >= text.size()) {
    return {};
  }

  const char *cursor = text.data() + offset;
  std::uint32_t raw = 0;
  std::uint32_t upper = 0;
  const int consumed = openwow::core::SStrGetNextUTF8Char_ToUpper(&raw, &cursor, &upper);
  if (consumed <= 0) {
    return {
        .size = 1,
        .raw = static_cast<unsigned char>(text[offset]),
        .upper = static_cast<unsigned char>(text[offset]),
    };
  }

  return {
      .size = static_cast<std::size_t>(consumed),
      .raw = raw,
      .upper = upper,
  };
}

bool IsLanguageWordCodepoint(const std::uint32_t codepoint) {
  if (codepoint > 0xFFu) {
    return true;
  }
  if (codepoint == '\'' || (codepoint >= '0' && codepoint <= '9') ||
      (codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= 'a' && codepoint <= 'z')) {
    return true;
  }
  if (codepoint >= 192u && codepoint <= 221u && codepoint != 215u) {
    return true;
  }
  if (codepoint == 223u) {
    return true;
  }
  return codepoint >= 224u && codepoint <= 255u && codepoint != 247u && codepoint != 254u;
}

bool IsMatureFilterMessageType(const int chat_type) {
  return std::find(kMatureFilterChatTypes.begin(), kMatureFilterChatTypes.end(), chat_type) !=
         kMatureFilterChatTypes.end();
}

bool StringEqualsNoCase(const std::string_view lhs, const std::string_view rhs) {
  if (lhs.empty() || rhs.empty()) {
    return lhs.empty() && rhs.empty();
  }

  return openwow::core::SStrCmpNoCase(std::string(lhs).c_str(), std::string(rhs).c_str(),
                                      0x7FFFFFFFu) == 0;
}

bool IsGameMasterTag(const std::string_view tag) {
  return IsGameMasterChatTagToken(tag);
}

bool IsSpamFilterEligibleMessageType(const int chat_type) {
  return chat_type != 0 && chat_type != 43;
}

const char *ResolveAddonMessageDistribution(const int chat_type) {
  switch (static_cast<ChatMsg>(static_cast<std::uint8_t>(chat_type))) {
  case ChatMsg::kParty:
    return "PARTY";
  case ChatMsg::kRaid:
    return "RAID";
  case ChatMsg::kGuild:
    return "GUILD";
  case ChatMsg::kWhisper:
  case ChatMsg::kWhisperForeign:
  case ChatMsg::kBattlenet:
    return "WHISPER";
  case ChatMsg::kBattleground:
    return "BATTLEGROUND";
  default:
    return "UNKNOWN";
  }
}

void FireAddonMessageEvent(const int chat_type, const std::string_view wire_message,
                           const char *sender_name) {
  const std::size_t separator = wire_message.find('\t');
  const std::string prefix(wire_message.substr(0, separator));
  const std::string payload = separator == std::string_view::npos
                                  ? std::string()
                                  : std::string(wire_message.substr(separator + 1));

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::CHAT_MSG_ADDON,
      {prefix, payload, std::string(ResolveAddonMessageDistribution(chat_type)),
       sender_name != nullptr ? std::string(sender_name) : std::string()});
}

const char *ResolveChatEventName(const int chat_type) {
  using namespace openwow::ui::game::events;
  if (chat_type < 0 || chat_type > static_cast<int>(kMaxChatMsgType)) {
    return nullptr;
  }
  switch (static_cast<ChatMsg>(static_cast<std::uint8_t>(chat_type))) {
  case ChatMsg::kSystem:
    return CHAT_MSG_SYSTEM;
  case ChatMsg::kSay:
    return CHAT_MSG_SAY;
  case ChatMsg::kParty:
    return CHAT_MSG_PARTY;
  case ChatMsg::kRaid:
    return CHAT_MSG_RAID;
  case ChatMsg::kGuild:
    return CHAT_MSG_GUILD;
  case ChatMsg::kOfficer:
    return CHAT_MSG_OFFICER;
  case ChatMsg::kYell:
    return CHAT_MSG_YELL;
  case ChatMsg::kWhisper:
    return CHAT_MSG_WHISPER;
  case ChatMsg::kWhisperForeign:
    return CHAT_MSG_WHISPER;
  case ChatMsg::kWhisperInform:
    return CHAT_MSG_WHISPER_INFORM;
  case ChatMsg::kEmote:
    return CHAT_MSG_EMOTE;
  case ChatMsg::kTextEmote:
    return CHAT_MSG_TEXT_EMOTE;
  case ChatMsg::kMonsterSay:
    return CHAT_MSG_MONSTER_SAY;
  case ChatMsg::kMonsterParty:
    return CHAT_MSG_MONSTER_PARTY;
  case ChatMsg::kMonsterYell:
    return CHAT_MSG_MONSTER_YELL;
  case ChatMsg::kMonsterWhisper:
    return CHAT_MSG_MONSTER_WHISPER;
  case ChatMsg::kMonsterEmote:
    return CHAT_MSG_MONSTER_EMOTE;
  case ChatMsg::kChannel:
    return CHAT_MSG_CHANNEL;
  case ChatMsg::kChannelJoin:
    return CHAT_MSG_CHANNEL_JOIN;
  case ChatMsg::kChannelLeave:
    return CHAT_MSG_CHANNEL_LEAVE;
  case ChatMsg::kChannelList:
    return CHAT_MSG_CHANNEL_LIST;
  case ChatMsg::kChannelNotice:
    return CHAT_MSG_CHANNEL_NOTICE;
  case ChatMsg::kChannelNoticeUser:
    return CHAT_MSG_CHANNEL_NOTICE_USER;
  case ChatMsg::kAfk:
    return CHAT_MSG_AFK;
  case ChatMsg::kDnd:
    return CHAT_MSG_DND;
  case ChatMsg::kIgnored:
    return CHAT_MSG_IGNORED;
  case ChatMsg::kSkill:
    return CHAT_MSG_SKILL;
  case ChatMsg::kLoot:
    return CHAT_MSG_LOOT;
  case ChatMsg::kMoney:
    return CHAT_MSG_MONEY;
  case ChatMsg::kOpening:
    return CHAT_MSG_OPENING;
  case ChatMsg::kTradeskills:
    return CHAT_MSG_TRADESKILLS;
  case ChatMsg::kPetInfo:
    return CHAT_MSG_PET_INFO;
  case ChatMsg::kCombatMiscInfo:
    return CHAT_MSG_COMBAT_MISC_INFO;
  case ChatMsg::kCombatXpGain:
    return CHAT_MSG_COMBAT_XP_GAIN;
  case ChatMsg::kCombatHonorGain:
    return CHAT_MSG_COMBAT_HONOR_GAIN;
  case ChatMsg::kCombatFactionChange:
    return CHAT_MSG_COMBAT_FACTION_CHANGE;
  case ChatMsg::kBgSystemNeutral:
    return CHAT_MSG_BG_SYSTEM_NEUTRAL;
  case ChatMsg::kBgSystemAlliance:
    return CHAT_MSG_BG_SYSTEM_ALLIANCE;
  case ChatMsg::kBgSystemHorde:
    return CHAT_MSG_BG_SYSTEM_HORDE;
  case ChatMsg::kRaidLeader:
    return CHAT_MSG_RAID_LEADER;
  case ChatMsg::kRaidWarning:
    return CHAT_MSG_RAID_WARNING;
  case ChatMsg::kRaidBossEmote:
    return CHAT_MSG_RAID_BOSS_EMOTE;
  case ChatMsg::kRaidBossWhisper:
    return CHAT_MSG_RAID_BOSS_WHISPER;
  case ChatMsg::kFiltered:
    return CHAT_MSG_FILTERED;
  case ChatMsg::kBattleground:
    return CHAT_MSG_BATTLEGROUND;
  case ChatMsg::kBattlegroundLeader:
    return CHAT_MSG_BATTLEGROUND_LEADER;
  case ChatMsg::kRestricted:
    return CHAT_MSG_RESTRICTED;
  case ChatMsg::kAchievement:
    return CHAT_MSG_ACHIEVEMENT;
  case ChatMsg::kGuildAchievement:
    return CHAT_MSG_GUILD_ACHIEVEMENT;
  case ChatMsg::kPartyLeader:
    return CHAT_MSG_PARTY_LEADER;
  case ChatMsg::kTargetIcons:
    return CHAT_MSG_TARGETICONS;
  case ChatMsg::kBnWhisper:
    return CHAT_MSG_BN_WHISPER;
  case ChatMsg::kBnWhisperInform:
    return CHAT_MSG_BN_WHISPER_INFORM;
  case ChatMsg::kBnConversation:
    return CHAT_MSG_BN_CONVERSATION;
  case ChatMsg::kBnConversationNotice:
    return CHAT_MSG_BN_CONVERSATION_NOTICE;
  case ChatMsg::kBnConversationList:
    return CHAT_MSG_BN_CONVERSATION_LIST;
  case ChatMsg::kBnInlineToastAlert:
    return CHAT_MSG_BN_INLINE_TOAST_ALERT;
  case ChatMsg::kBnInlineToastBroadcast:
    return CHAT_MSG_BN_INLINE_TOAST_BROADCAST;
  case ChatMsg::kBnInlineToastBroadcastInform:
    return CHAT_MSG_BN_INLINE_TOAST_BROADCAST_INFORM;
  case ChatMsg::kBnInlineToastConversation:
    return CHAT_MSG_BN_INLINE_TOAST_CONVERSATION;
  default:
    return nullptr;
  }
}

std::string ResolveLanguageNameForDisplay(const std::uint32_t language_id) {
  std::lock_guard<std::mutex> lock(g_language_word_mutex);
  if (g_chat_display_dbc != nullptr) {
    if (const auto *language = g_chat_display_dbc->languages().LookupEntry(language_id)) {
      if (!language->name.empty()) {
        return std::string(language->name);
      }
    }
  }

  if (language_id == static_cast<std::uint32_t>(Language::kUniversal)) {
    return "Universal";
  }

  return std::string(GetLanguageName(static_cast<Language>(language_id)));
}

std::string StripChatFormattingForLanguageDisplay(const std::string_view message,
                                                  const std::size_t output_limit) {
  const std::size_t max_output_bytes = ResolveMaxOutputBytes(output_limit);
  std::string stripped;
  if (max_output_bytes != std::numeric_limits<std::size_t>::max()) {
    stripped.reserve(std::min(message.size(), max_output_bytes));
  } else {
    stripped.reserve(message.size());
  }

  const char *cursor = message.data();
  const char *const end = cursor + message.size();
  bool inside_link_text = false;

  while (cursor < end) {
    if (*cursor == '|') {
      const char *const token = cursor + 1;
      if (token >= end) {
        AppendTruncatedByte(&stripped, '|', max_output_bytes);
        break;
      }

      switch (*token) {
      case 'c':
      case 'C': {
        cursor = token + 1;
        std::size_t skipped = 0;
        while (cursor < end && skipped < 8) {
          ++cursor;
          ++skipped;
        }
        continue;
      }
      case 'r':
      case 'R':
        cursor = token + 1;
        continue;
      case 'H': {
        cursor = token + 1;
        while (cursor < end) {
          if (*cursor == '|' && cursor + 1 < end && cursor[1] == 'h') {
            cursor += 2;
            break;
          }
          ++cursor;
        }
        inside_link_text = true;
        continue;
      }
      case 'h':
        cursor = token + 1;
        inside_link_text = false;
        continue;
      case 'T': {
        cursor = token + 1;
        while (cursor < end) {
          if (*cursor == '|' && cursor + 1 < end && cursor[1] == 't') {
            cursor += 2;
            break;
          }
          ++cursor;
        }
        continue;
      }
      default:
        AppendTruncatedByte(&stripped, '|', max_output_bytes);
        ++cursor;
        continue;
      }
    }

    if (!inside_link_text || (*cursor != '[' && *cursor != ']')) {
      AppendTruncatedByte(&stripped, *cursor, max_output_bytes);
    }
    ++cursor;
  }

  return stripped;
}

struct ChatDisplayExtraData {
  std::uint32_t primary = 0;
  std::uint32_t secondary = 0;
  std::uint8_t flags = 0xFF;
};

ChatDisplayExtraData ReadChatDisplayExtraData(const void *extra_data) {
  ChatDisplayExtraData parsed;
  if (extra_data == nullptr) {
    return parsed;
  }

  std::memcpy(&parsed.primary, extra_data, sizeof(parsed.primary));
  std::memcpy(&parsed.secondary,
              static_cast<const std::byte *>(extra_data) + sizeof(parsed.primary),
              sizeof(parsed.secondary));
  std::memcpy(&parsed.flags, static_cast<const std::byte *>(extra_data) + 8, sizeof(parsed.flags));
  return parsed;
}

struct ChatEventChannelState {
  std::string display_name;
  std::string base_name;
  int lookup_id = 0;
  int channel_number = 0;
  int instance_id = 0;
};

ChatEventChannelState ResolveChannelState(const char *channel_name,
                                          const ChatDisplayExtraData &extra_data) {
  ChatEventChannelState state;

  if (channel_name != nullptr && channel_name[0] != '\0') {
    state.base_name = channel_name;
    for (const ChatChannel &channel : ChatSystem::Get().GetChannelsSnapshot()) {
      if (!StringEqualsNoCase(channel.name, channel_name)) {
        continue;
      }

      state.channel_number = static_cast<int>(channel.id);
      state.lookup_id = static_cast<int>(channel.lookup_id);
      state.instance_id = static_cast<int>(channel.instance_id);
      break;
    }

    if (state.channel_number > 0) {
      char buffer[256];
      std::snprintf(buffer, sizeof(buffer), "%d. %s", state.channel_number, channel_name);
      state.display_name = buffer;
    } else {
      state.display_name = channel_name;
    }
    return state;
  }

  if (extra_data.flags == 0) {
    state.channel_number = static_cast<int>(extra_data.primary) + 1;
    const std::string format = GetLocalizedGlobalString("CONVERSATION_NAME");
    state.display_name =
        Localization::Get().FormatString(format, {std::to_string(state.channel_number)});
  }

  return state;
}

[[nodiscard]] std::optional<std::string>
BuildClientChatLogLine(const int chat_type, const std::string_view sender_name,
                       const std::string_view message, const ChatEventChannelState &channel_state) {
  const std::string sanitized_message = SanitizeChatLogMessage(message);
  if (chat_type == static_cast<int>(ChatMsg::kTextEmote)) {
    if (sanitized_message.empty()) {
      return std::nullopt;
    }
    return sanitized_message;
  }

  if (chat_type == static_cast<int>(ChatMsg::kSystem) ||
      chat_type == static_cast<int>(ChatMsg::kChannelNotice) ||
      chat_type == static_cast<int>(ChatMsg::kChannelNoticeUser) ||
      chat_type == static_cast<int>(ChatMsg::kIgnored) ||
      chat_type == static_cast<int>(ChatMsg::kFiltered) ||
      chat_type == static_cast<int>(ChatMsg::kRestricted) ||
      chat_type == static_cast<int>(ChatMsg::kChannelList) ||
      chat_type == static_cast<int>(ChatMsg::kBnConversationNotice)) {
    if (sanitized_message.empty()) {
      return std::nullopt;
    }
    return sanitized_message;
  }

  std::string prefix;
  if (chat_type == static_cast<int>(ChatMsg::kChannel)) {
    prefix = TrimChatLogChannelPrefix(channel_state.display_name);
    if (!sender_name.empty()) {
      prefix += std::string(sender_name) + ": ";
    }
  } else if (const char *token = ResolveChatLogTypeToken(chat_type); token != nullptr) {
    const std::string get_key = std::string("CHAT_") + token + "_GET";
    prefix = FormatChatLogPrefixFromTemplate(get_key, sender_name);
    if (prefix.empty()) {
      const std::string send_key = std::string("CHAT_") + token + "_SEND";
      prefix = FormatChatLogPrefixFromTemplate(send_key, sender_name);
    }
  }

  if (prefix.empty()) {
    if (sanitized_message.empty()) {
      return std::nullopt;
    }
    return sanitized_message;
  }

  return prefix + sanitized_message;
}

std::string FormatGuidForChatEvent(const std::uint64_t guid, const std::string_view flag_tag) {
  if (guid == 0 || IsGameMasterTag(flag_tag)) {
    return {};
  }

  char buffer[19];
  std::snprintf(buffer, sizeof(buffer), "0x%016llX", static_cast<unsigned long long>(guid));
  return buffer;
}

bool ShouldCreateSpeechBubble(const int chat_type) {
  switch (chat_type) {
  case ChatDisplayType::kSay:
  case ChatDisplayType::kParty:
  case ChatDisplayType::kYell:
  case ChatDisplayType::kMonsterSay:
  case ChatDisplayType::kMonsterYell:
  case ChatDisplayType::kPartyLeader:
    return true;
  default:
    return false;
  }
}

std::uint32_t MakeArgbColor(const ChatColor &color) {
  const auto clamp_channel = [](const float value) {
    return static_cast<std::uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
  };

  return 0xFF000000u | (clamp_channel(color.r) << 16) | (clamp_channel(color.g) << 8) |
         clamp_channel(color.b);
}

std::string NormalizeSpeechBubbleText(const int chat_type, const std::string_view message) {
  if (chat_type != ChatDisplayType::kMonsterSay && chat_type != ChatDisplayType::kMonsterYell) {
    return std::string(message);
  }

  std::string normalized;
  normalized.reserve(message.size());
  for (std::size_t i = 0; i < message.size(); ++i) {
    normalized.push_back(message[i]);
    if (message[i] == '%' && i + 1 < message.size() && message[i + 1] == '%') {
      ++i;
    }
  }
  return normalized;
}

void MaybeCreateSpeechBubble(const ObjectManager& objects,
                             const std::uint64_t sender_guid, const int chat_type,
                             const std::string_view message) {
  if (sender_guid == 0 || message.empty() || !ShouldCreateSpeechBubble(chat_type)) {
    return;
  }

  const bool use_party_cvar =
      chat_type == ChatDisplayType::kParty || chat_type == ChatDisplayType::kPartyLeader;
  const char *cvar_name = use_party_cvar ? "chatBubblesParty" : "chatBubbles";
  if (!ui::game::CVarSystem::Instance().GetCVarBool(cvar_name)) {
    return;
  }

  auto &bubbles = ChatBubbleSystem::Get();
  const ObjectGuid guid(sender_guid);
  const bool is_local_player =
      guid.GetRawValue() == objects.GetActivePlayerGuid().GetRawValue();
  bubbles.RemoveBubblesForUnit(guid);
  bubbles.AddSpeechBubble(guid, NormalizeSpeechBubbleText(chat_type, message), is_local_player,
                          MakeArgbColor(ChatSystem::GetDefaultColor(
                              static_cast<ChatMsg>(static_cast<std::uint8_t>(chat_type)))));
}

bool MaybeReplaceEmptyChatText(std::string *message, int *chat_type) {
  if (message == nullptr || chat_type == nullptr || !message->empty()) {
    return false;
  }

  switch (*chat_type) {
  case ChatDisplayType::kSay:
    *message = GetLocalizedGlobalString("CHAT_SAY_UNKNOWN");
    *chat_type = ChatDisplayType::kEmote;
    return true;
  case ChatDisplayType::kYell:
    *message = GetLocalizedGlobalString("CHAT_YELL_UNKNOWN");
    *chat_type = ChatDisplayType::kEmote;
    return true;
  case ChatDisplayType::kEmote:
    *message = GetLocalizedGlobalString("CHAT_EMOTE_UNKNOWN");
    return true;
  default:
    return false;
  }
}

void RecolorQuestLinksForDisplay(const ObjectManager& objects, std::string *message) {
  if (message == nullptr || message->empty()) {
    return;
  }

  const auto *active_player = objects.GetActivePlayer();
  if (active_player == nullptr) {
    return;
  }

  const auto player_level = static_cast<std::int32_t>(active_player->State().GetLevel());
  std::size_t search_offset = 0;
  while ((search_offset = message->find("|Hquest:", search_offset)) != std::string::npos) {
    if (search_offset != 0 && (*message)[search_offset - 1] == '|') {
      search_offset += 8;
      continue;
    }

    const auto level_separator = message->find(':', search_offset + 8);
    const auto link_terminator = message->find("|h", search_offset + 8);
    if (level_separator == std::string::npos || link_terminator == std::string::npos ||
        level_separator > link_terminator) {
      search_offset += 8;
      continue;
    }

    const auto level_field_end = message->find('|', level_separator + 1);
    if (level_field_end == std::string::npos || level_field_end > link_terminator) {
      search_offset += 8;
      continue;
    }

    const std::string level_field =
        message->substr(level_separator + 1, level_field_end - level_separator - 1);
    char *parse_end = nullptr;
    long parsed_level = std::strtol(level_field.c_str(), &parse_end, 10);
    if (parse_end == nullptr || *parse_end != '\0') {
      search_offset += 8;
      continue;
    }

    const auto quest_level =
        static_cast<std::int32_t>(parsed_level == -1 ? player_level : parsed_level);
    if (search_offset < 10 || message->compare(search_offset - 10, 2, "|c") != 0) {
      search_offset += 8;
      continue;
    }

    char color_prefix[11];
    std::snprintf(color_prefix, sizeof(color_prefix), "|c%08X",
                  openwow::ui::TargetFrameProvider::GetLevelColor(player_level, quest_level));
    message->replace(search_offset - 10, 10, color_prefix);
    search_offset += 8;
  }
}

LanguageWordTable BuildLanguageWordTable(const openwow::data::dbc::DbcLoader *dbc) {
  LanguageWordTable table;
  if (dbc == nullptr) {
    return table;
  }

  LanguageWordTableLayout layout;
  for (const auto &entry : dbc->language_words().entries()) {
    if (entry.word.empty()) {
      continue;
    }
    ++layout[entry.language_id][entry.word.size()];
  }

  table.reserve(layout.size());
  for (const auto &[language_id, bucket_counts] : layout) {
    auto &buckets = table[language_id];
    buckets.reserve(bucket_counts.size());
    for (const auto &[word_length, word_count] : bucket_counts) {
      buckets[word_length].reserve(word_count);
    }
  }

  for (const auto &entry : dbc->language_words().entries()) {
    if (entry.word.empty()) {
      continue;
    }
    table[entry.language_id][entry.word.size()].emplace_back(entry.word);
  }

  return table;
}

[[nodiscard]] std::uint32_t LanguageIdForLearningSpell(const std::uint32_t spell_id) {
  for (const auto &[language_id, learning_spell_id] : kLanguageLearningSpells) {
    if (learning_spell_id == spell_id) {
      return language_id;
    }
  }
  return 0;
}

LanguageSkillCatalog BuildLanguageSkillCatalog(const openwow::data::dbc::DbcLoader *dbc) {
  LanguageSkillCatalog catalog;
  if (dbc == nullptr) {
    return catalog;
  }

  for (const auto &ability : dbc->skill_line_ability().entries()) {
    const std::uint32_t language_id = LanguageIdForLearningSpell(ability.spell_id);
    if (language_id == 0 || language_id >= catalog.size()) {
      continue;
    }
    catalog[language_id].push_back(ability);
  }
  return catalog;
}

[[nodiscard]] std::uint32_t ResolveLanguageSkillLineId(
    const CGPlayer_C &player, const std::uint32_t language_id,
    const LanguageSkillCatalog &catalog, const openwow::data::dbc::DbcLoader *dbc) {
  if (dbc == nullptr || language_id >= catalog.size()) {
    return 0;
  }

  const auto &candidates = catalog[language_id];
  if (candidates.empty()) {
    return 0;
  }

  const auto *ability = FindSkillLineAbilityForRaceClassSpell(
      candidates, dbc->skill_race_class_info().entries(), player.State().GetRace(), player.State().GetClass(),
      candidates.front().spell_id);
  return ability != nullptr ? ability->skill_id : 0;
}

std::uint32_t BuildStormWordHash(const std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  std::uint32_t hash = 2146271213u;
  std::uint32_t mix = 0xEEEEEEEFu;
  for (unsigned char value : text) {
    if (value >= 'a' && value <= 'z') {
      value = static_cast<unsigned char>(value - 32);
    }
    if (value == '/') {
      value = '\\';
    }

    hash = (kStormNibbleHashTable[value >> 4] - kStormNibbleHashTable[value & 0x0F]) ^ (mix + hash);
    mix += value + 32u * mix + hash + 3u;
  }

  return hash == 0 ? 1u : hash;
}

const std::string *FindReplacementWord(const LanguageWordBuckets &buckets,
                                       const std::string_view source_word,
                                       const std::uint32_t hash) {
  for (std::size_t length = std::min<std::size_t>(source_word.size(), 18u); length > 0; --length) {
    const auto bucket_it = buckets.find(length);
    if (bucket_it == buckets.end() || bucket_it->second.empty()) {
      continue;
    }

    const auto &replacements = bucket_it->second;
    return &replacements[hash % replacements.size()];
  }

  return nullptr;
}

std::string ApplyReplacementWordCasing(const std::string_view replacement,
                                       const std::string_view source_word) {
  std::string cased;
  cased.reserve(replacement.size());
  if (LocaleUsesBytewiseReplacementCasing()) {
    for (std::size_t i = 0; i < replacement.size() && i < source_word.size(); ++i) {
      const unsigned char source_byte = static_cast<unsigned char>(source_word[i]);
      cased.push_back(IsAsciiUpperByte(source_byte) ? ToAsciiUpper(replacement[i])
                                                    : replacement[i]);
    }
    return cased;
  }

  std::size_t source_offset = 0;
  for (const char replacement_char : replacement) {
    if (source_offset >= source_word.size()) {
      break;
    }

    const Utf8Step step = ReadUtf8Step(source_word, source_offset);
    if (step.size == 0) {
      break;
    }

    const bool source_is_lowercase = (step.raw >= 'a' && step.raw <= 'z') ||
                                     (step.raw >= 224u && step.raw <= 254u) ||
                                     (step.raw >= 1072u && step.raw <= 1103u) || step.raw == 1105u;
    cased.push_back(source_is_lowercase ? ToLanguageLowerByte(replacement_char)
                                        : ToLanguageUpperByte(replacement_char));
    source_offset += step.size;
  }

  return cased;
}

bool ActivePlayerHasLanguageComprehensionAura(const CGPlayer_C &player,
                                              const std::uint32_t language_id,
                                              const openwow::data::dbc::DbcLoader *dbc) {
  if (dbc == nullptr) {
    return false;
  }

  for (const AuraInfo &aura : player.Auras().All()) {
    const auto *spell = dbc->spell().LookupEntry(aura.spell_id);
    if (spell == nullptr) {
      continue;
    }

    for (std::size_t effect_index = 0; effect_index < spell->effect.size(); ++effect_index) {
      if (spell->effect[effect_index] == kSpellEffectApplyAura &&
          spell->effect_apply_aura[effect_index] == kAuraModLanguageComprehension &&
          spell->effect_misc_value[effect_index] == static_cast<std::int32_t>(language_id)) {
        return true;
      }
    }
  }

  return false;
}

std::uint32_t GetLanguageComprehensionValue(
    const CGPlayer_C &player, const std::uint32_t language_id,
    const LanguageSkillCatalog &language_skills,
    const openwow::data::dbc::DbcLoader *dbc) {
  if (ActivePlayerHasLanguageComprehensionAura(player, language_id, dbc)) {
    return kMaxLanguageComprehension;
  }

  const std::uint32_t skill_line_id =
      ResolveLanguageSkillLineId(player, language_id, language_skills, dbc);
  if (skill_line_id == 0) {
    return 0;
  }

  const auto skill_id = static_cast<std::uint16_t>(skill_line_id);
  const std::uint32_t value = player.GetSkillValue(skill_id) + player.GetSkillBonusValue(skill_id);
  return std::min(value, kMaxLanguageComprehension);
}

}

std::optional<ChatLanguageInfo> FindChatLanguageByName(const openwow::data::dbc::DbcLoader &dbc,
                                                       const std::string_view language_name) {
  if (language_name.empty()) {
    return std::nullopt;
  }

  const std::string requested_name(language_name);
  for (const auto &entry : dbc.languages().entries()) {
    if (entry.name.empty()) {
      continue;
    }
    if (openwow::core::SStrCmpUTF8NoCase(entry.name.data(), requested_name.c_str(), 0x7FFFFFFF) ==
        0) {
      return ChatLanguageInfo{.id = entry.id, .name = entry.name};
    }
  }

  return std::nullopt;
}

std::optional<ChatLanguageInfo> FindChatLanguageById(const openwow::data::dbc::DbcLoader &dbc,
                                                     const std::uint32_t language_id) {
  if (language_id == 0) {
    return std::nullopt;
  }

  const auto *const entry = dbc.languages().LookupEntry(language_id);
  if (entry == nullptr || entry->name.empty()) {
    return std::nullopt;
  }

  return ChatLanguageInfo{.id = entry->id, .name = entry->name};
}

std::uint32_t ResolveDefaultChatLanguageId(const CGPlayer_C &player,
                                           const openwow::data::dbc::DbcLoader &dbc) {
  const auto *const race_entry = dbc.chr_races().LookupEntry(player.State().GetRace());
  if (race_entry == nullptr) {
    return 0;
  }

  return race_entry->default_language_id;
}

std::uint32_t GetChatLanguageComprehensionValue(const CGPlayer_C &player,
                                                const openwow::data::dbc::DbcLoader &dbc,
                                                const std::uint32_t language_id) {
  const auto language_skills = BuildLanguageSkillCatalog(&dbc);
  return GetLanguageComprehensionValue(player, language_id, language_skills, &dbc);
}

std::vector<ChatLanguageInfo>
CollectAvailableChatLanguages(const CGPlayer_C &player, const openwow::data::dbc::DbcLoader &dbc) {
  const auto language_skills = BuildLanguageSkillCatalog(&dbc);
  std::vector<ChatLanguageInfo> languages;
  languages.reserve(dbc.languages().entries().size());

  for (const auto &entry : dbc.languages().entries()) {
    if (entry.name.empty()) {
      continue;
    }
    if (GetLanguageComprehensionValue(player, entry.id, language_skills, &dbc) == 0) {
      continue;
    }
    languages.push_back(ChatLanguageInfo{.id = entry.id, .name = entry.name});
  }

  return languages;
}

namespace {
void AppendFormattedWordRun(std::string *out, const LanguageWordBuckets &buckets,
                            std::string_view text, std::size_t word_start, std::size_t word_end,
                            std::uint32_t threshold, std::size_t max_output_bytes);
}

std::string ChatFrame_FormatMessage(const ObjectManager& objects,
                                    const std::uint32_t language_id,
                                    const std::uint32_t comprehension_value,
                                    const std::string_view message,
                                    const ChatFrameFormatOptions &options) {
  const std::size_t max_output_bytes = ResolveMaxOutputBytes(options.output_limit);
  if (max_output_bytes == 0 || message.empty()) {
    return {};
  }

  if (language_id == 0 || comprehension_value >= kMaxLanguageComprehension ||
      objects.GetActivePlayer() == nullptr) {
    return std::string(message.substr(0, std::min(message.size(), max_output_bytes)));
  }

  const std::string formatted_input =
      StripChatFormattingForLanguageDisplay(message, options.output_limit);

  std::lock_guard<std::mutex> lock(g_language_word_mutex);
  const LanguageWordBuckets empty_buckets;
  const LanguageWordBuckets *buckets = &empty_buckets;
  if (const auto language_it = g_language_words.find(language_id);
      language_it != g_language_words.end()) {
    buckets = &language_it->second;
  }

  std::string formatted;
  formatted.reserve(std::min(formatted_input.size(), max_output_bytes));
  bool saw_word = false;
  std::size_t offset = 0;

  while (offset < formatted_input.size()) {
    bool emitted_separator = false;
    while (offset < formatted_input.size()) {
      const Utf8Step step = ReadUtf8Step(formatted_input, offset);
      if (step.size == 0 || IsLanguageWordCodepoint(step.raw)) {
        break;
      }

      if (step.raw == '<' && options.preserve_angle_bracket_spans) {
        const std::size_t tag_start = offset;
        offset += step.size;
        while (offset < formatted_input.size()) {
          const Utf8Step tag_step = ReadUtf8Step(formatted_input, offset);
          if (tag_step.size == 0) {
            break;
          }
          offset += tag_step.size;
          if (tag_step.raw == '>') {
            break;
          }
        }
        AppendTruncatedBytes(&formatted, formatted_input.substr(tag_start, offset - tag_start),
                             max_output_bytes);
        continue;
      }

      if (options.preserve_separators) {
        AppendTruncatedBytes(&formatted, formatted_input.substr(offset, step.size),
                             max_output_bytes);
      } else if (!emitted_separator) {
        AppendTruncatedByte(&formatted, ' ', max_output_bytes);
        emitted_separator = true;
      }
      offset += step.size;
    }

    if (offset >= formatted_input.size()) {
      break;
    }

    saw_word = true;
    const std::size_t word_start = offset;
    std::size_t word_end = offset;
    while (word_end < formatted_input.size()) {
      const Utf8Step word_step = ReadUtf8Step(formatted_input, word_end);
      if (word_step.size == 0 || !IsLanguageWordCodepoint(word_step.raw)) {
        break;
      }
      word_end += word_step.size;
    }

    AppendFormattedWordRun(&formatted, *buckets, formatted_input, word_start, word_end,
                           comprehension_value, max_output_bytes);
    offset = word_end;
  }

  if (!saw_word) {
    return std::string(
        formatted_input.substr(0, std::min(formatted_input.size(), max_output_bytes)));
  }

  return formatted;
}

namespace {

void AppendTranslatedWordSegment(std::string *out, const LanguageWordBuckets &buckets,
                                 const std::string_view segment, const std::uint32_t threshold,
                                 const std::size_t max_output_bytes) {
  if (out == nullptr || segment.empty()) {
    return;
  }

  const std::uint32_t hash = BuildStormWordHash(segment);
  if (hash % kLanguageHashModulo < threshold) {
    AppendTruncatedBytes(out, segment, max_output_bytes);
    return;
  }

  const std::string *replacement = FindReplacementWord(buckets, segment, hash);
  if (replacement == nullptr) {
    return;
  }

  AppendTruncatedBytes(out, ApplyReplacementWordCasing(*replacement, segment), max_output_bytes);
}

void AppendFormattedWordRun(std::string *out, const LanguageWordBuckets &buckets,
                            const std::string_view text, const std::size_t word_start,
                            const std::size_t word_end, const std::uint32_t threshold,
                            const std::size_t max_output_bytes) {
  std::size_t segment_start = word_start;
  std::size_t scan = word_start;
  std::uint32_t segment_target = LocaleUsesAsianLanguageChunking() ? 2u : 0u;
  std::uint32_t segment_word_count = 0;

  while (scan < word_end) {
    const Utf8Step word_step = ReadUtf8Step(text, scan);
    if (word_step.size == 0) {
      break;
    }

    scan += word_step.size;
    if (segment_target == 0u) {
      continue;
    }

    ++segment_word_count;
    if (segment_word_count < segment_target) {
      continue;
    }

    AppendTranslatedWordSegment(out, buckets, text.substr(segment_start, scan - segment_start),
                                threshold, max_output_bytes);
    if (scan < word_end) {
      AppendTruncatedByte(out, ' ', max_output_bytes);
    }

    segment_start = scan;
    segment_word_count = 0;
    segment_target = segment_target >= 5u ? 2u : segment_target + 1u;
  }

  if (segment_start < word_end) {
    AppendTranslatedWordSegment(out, buckets, text.substr(segment_start, word_end - segment_start),
                                threshold, max_output_bytes);
  }
}

bool IsInsideColorEscapeSequence(const std::string &text, const std::size_t byte_offset) {
  bool inside_color = false;
  std::size_t remaining = byte_offset;
  const char *cursor = text.c_str();

  while (remaining != 0) {
    const char value = *cursor++;
    --remaining;
    if (value != '|') {
      continue;
    }
    if (remaining == 0) {
      return inside_color;
    }

    const char token = *cursor++;
    --remaining;
    if (token == 'c' || token == 'C') {
      inside_color = true;
    } else if ((token == 'r' || token == 'R') && inside_color) {
      inside_color = false;
    }
  }

  return inside_color;
}

const MatureFilterCacheEntry *FindMatureFilterCacheEntryLocked(const std::string &message) {
  const std::uint32_t hash = openwow::core::SStrHashCI(message.c_str());
  const auto bucket_it = g_mature_filter_cache.find(hash);
  if (bucket_it == g_mature_filter_cache.end()) {
    return nullptr;
  }

  for (const MatureFilterCacheEntry &entry : bucket_it->second) {
    if (openwow::core::SStrCmpI(entry.source.c_str(), message.c_str(), 0x7FFFFFFFu) == 0) {
      return &entry;
    }
  }

  return nullptr;
}

void StoreMatureFilterCacheEntryLocked(const std::string &source, const bool matched,
                                       const std::string &filtered) {
  const std::uint32_t hash = openwow::core::SStrHashCI(source.c_str());
  auto &bucket = g_mature_filter_cache[hash];
  for (MatureFilterCacheEntry &entry : bucket) {
    if (openwow::core::SStrCmpI(entry.source.c_str(), source.c_str(), 0x7FFFFFFFu) == 0) {
      entry.matched = matched;
      entry.filtered = filtered;
      return;
    }
  }

  bucket.push_back(MatureFilterCacheEntry{
      .source = source,
      .matched = matched,
      .filtered = filtered,
  });
}

bool ApplyMatureLanguageExpressions(std::string &message,
                                    const CompiledChatExpressionList &expressions) {
  bool matched = false;
  for (const auto &expression : expressions) {
    std::size_t search_start = 0;
    while (search_start < message.size()) {
      const auto match = expression.compiled.Search(message, search_start);
      if (!match.has_value()) {
        break;
      }

      const std::size_t match_offset = match->start;
      const std::size_t match_length = match->length();
      if (match_length == 0) {
        break;
      }
      if (IsInsideColorEscapeSequence(message, match_offset)) {
        break;
      }

      matched = true;
      std::size_t filter_index = static_cast<std::size_t>(g_filter_char_index);
      for (std::size_t i = 0; i < match_length; ++i) {
        message[match_offset + i] = kFilterReplacementChars[filter_index];
        filter_index = (filter_index + 1) % kFilterReplacementCharCount;
      }
      g_filter_char_index = static_cast<int>(filter_index);
      search_start = match_offset + match_length;
    }
  }
  return matched;
}

}

namespace {

struct PendingWorldChatMessage {
  std::string message;
  int chat_type{0};
  std::string sender_name;
  int language_id{0};
  std::string channel_name;
  std::string secondary_name;
  std::string flag_tag;
  std::uint64_t sender_guid{0};
  int aux_value{0};
  std::uint64_t target_guid{0};
  int is_gm{0};
  bool has_sender{false};
  bool has_channel{false};
  bool has_secondary{false};
  bool has_flag{false};
};

bool g_world_chat_ui_ready = false;
std::vector<PendingWorldChatMessage> g_pending_world_chat_messages;

}

void ChatFrame_SetWorldUiReadyAndFlush(const ObjectManager& objects) {
  g_world_chat_ui_ready = true;
  const auto pending = std::move(g_pending_world_chat_messages);
  g_pending_world_chat_messages.clear();
  for (const auto& line : pending) {
    ChatFrame_DisplayMessage(
        objects, line.message.c_str(), line.chat_type,
        line.has_sender ? line.sender_name.c_str() : nullptr,
        line.language_id,
        line.has_channel ? line.channel_name.c_str() : nullptr,
        line.has_secondary ? line.secondary_name.c_str() : nullptr,
        line.has_flag ? line.flag_tag.c_str() : nullptr, line.sender_guid,
        line.aux_value, line.target_guid, 0, line.is_gm, nullptr);
  }
}

void ChatFrame_ResetWorldUiReady() {
  g_world_chat_ui_ready = false;
  g_pending_world_chat_messages.clear();
}

void ChatFrame_DisplayMessage(const ObjectManager& objects, const char *message,
                              int chat_type, const char *sender_name,
                              int language_id, const char *channel_name, const char *secondary_name,
                              const char *flag_tag, std::uint64_t sender_guid, int aux_value,
                              std::uint64_t target_guid, int , int is_gm,
                              const void *extra_data) {
  if (message == nullptr) {
    openwow::core::SErrSetLastError(87);
    return;
  }
  if (!g_world_chat_ui_ready) {
    PendingWorldChatMessage line;
    line.message = message;
    line.chat_type = chat_type;
    if (sender_name != nullptr) { line.sender_name = sender_name; line.has_sender = true; }
    line.language_id = language_id;
    if (channel_name != nullptr) { line.channel_name = channel_name; line.has_channel = true; }
    if (secondary_name != nullptr) { line.secondary_name = secondary_name; line.has_secondary = true; }
    if (flag_tag != nullptr) { line.flag_tag = flag_tag; line.has_flag = true; }
    line.sender_guid = sender_guid;
    line.aux_value = aux_value;
    line.target_guid = target_guid;
    line.is_gm = is_gm;
    (void)extra_data;
    g_pending_world_chat_messages.push_back(std::move(line));
    return;
  }

  if (static_cast<std::uint32_t>(language_id) ==
      static_cast<std::uint32_t>(Language::kAddon)) {
    FireAddonMessageEvent(chat_type, message, sender_name);
    return;
  }

  std::uint32_t comprehension_value = 0;
  if (const auto *active_player = objects.GetActivePlayer();
      active_player != nullptr) {
    std::lock_guard<std::mutex> lock(g_language_word_mutex);
    comprehension_value =
        GetLanguageComprehensionValue(*active_player, static_cast<std::uint32_t>(language_id),
                                      g_language_skills, g_chat_display_dbc);
  }

  std::string rendered_message =
      ChatFrame_FormatMessage(objects, static_cast<std::uint32_t>(language_id),
                              comprehension_value, message,
                              {.output_limit = 3000});
  RecolorQuestLinksForDisplay(objects, &rendered_message);

  if (IsSpamFilterEligibleMessageType(chat_type) &&
      !IsGameMasterTag(flag_tag != nullptr ? std::string_view(flag_tag) : std::string_view()) &&
      ui::game::CVarSystem::Instance().GetCVarBool("spamFilter") && !rendered_message.empty() &&
      ChatFrame_CheckProfanityFilter(rendered_message, false)) {
    return;
  }

  MaybeReplaceEmptyChatText(&rendered_message, &chat_type);

  const ChatDisplayExtraData parsed_extra = ReadChatDisplayExtraData(extra_data);
  ChatEventChannelState channel_state = ResolveChannelState(channel_name, parsed_extra);
  const bool profanity_filter_enabled =
      ui::game::CVarSystem::Instance().GetCVarBool("profanityFilter");
  if (profanity_filter_enabled && !channel_state.display_name.empty()) {
    ChatFrame_MatureLanguageFilter(channel_state.display_name, true);
  }
  if (profanity_filter_enabled && IsMatureFilterMessageType(chat_type) &&
      !rendered_message.empty()) {
    ChatFrame_MatureLanguageFilter(rendered_message, true);
  }

  const std::uint32_t display_line_id = ++g_chat_display_message_counter;
  GMTicketChatLog::Get().RecordDisplayMessage(
      chat_type, channel_name, sender_name, sender_guid, static_cast<std::uint32_t>(aux_value),
      static_cast<std::uint32_t>(channel_state.lookup_id), display_line_id,
      objects.GetActivePlayerGuid().GetRawValue(), rendered_message.c_str());

  ChatMessage msg;
  msg.type = static_cast<ChatMsg>(static_cast<std::uint8_t>(chat_type));
  msg.message = rendered_message;
  msg.sender_guid = ObjectGuid(sender_guid);
  msg.receiver_guid = ObjectGuid(target_guid);
  msg.language = static_cast<Language>(language_id);
  msg.channel_name = channel_name != nullptr ? channel_name : "";
  msg.sender_name = sender_name != nullptr ? sender_name : "";
  msg.secondary_name = secondary_name != nullptr ? secondary_name : "";
  msg.is_gm = is_gm != 0;
  ChatSystem::Get().AddMessage(msg);

  if (const char *event_name = ResolveChatEventName(chat_type)) {

    ui::game::ScriptEventDispatch::Get().FireEventArgs(
        event_name,
        {
            rendered_message,
            sender_name != nullptr ? std::string(sender_name) : std::string(),
            ResolveLanguageNameForDisplay(static_cast<std::uint32_t>(language_id)),
            channel_state.display_name,
            secondary_name != nullptr ? std::string(secondary_name) : std::string(),
            flag_tag != nullptr ? std::string(flag_tag) : std::string(),
            channel_state.lookup_id,
            channel_state.channel_number,
            channel_state.base_name,
            channel_state.instance_id,
            static_cast<int>(display_line_id),
            FormatGuidForChatEvent(sender_guid, flag_tag != nullptr ? std::string_view(flag_tag)
                                                                    : std::string_view()),
            static_cast<int>(parsed_extra.secondary),
        });
  }

  MaybeCreateSpeechBubble(objects, sender_guid, chat_type, rendered_message);

  if (const auto chat_log_line = BuildClientChatLogLine(
          chat_type, sender_name != nullptr ? std::string_view(sender_name) : std::string_view(),
          rendered_message, channel_state);
      chat_log_line.has_value()) {
    AppendClientTextLogLine(ClientTextLogKind::Chat, *chat_log_line);
  }
}

bool ChatFrame_CheckProfanityFilter(std::string &message, bool cache_result) {
  if (message.empty())
    return false;

  const std::string original_message = message;
  const std::string cache_key = ToLowerAscii(message);

  {
    std::lock_guard<std::mutex> lock(g_spam_filter_mutex);
    const auto it = g_spam_filter_cache.find(cache_key);
    if (it != g_spam_filter_cache.end()) {
      if (it->second.matched) {
        message = it->second.canonical_message;
      }
      return it->second.matched;
    }
  }

  bool found = false;
  {
    std::lock_guard<std::mutex> lock(g_spam_filter_mutex);
    found = MatchesAnySpamExpression(message, g_builtin_spam_filters) ||
            MatchesAnySpamExpression(message, g_server_spam_filters);
  }

  if (cache_result) {
    std::lock_guard<std::mutex> lock(g_spam_filter_mutex);
    g_spam_filter_cache[cache_key] = SpamFilterCacheEntry{
        .matched = found,
        .canonical_message = found ? original_message : std::string(),
    };
  }

  return found;
}

bool ChatFrame_MatureLanguageFilter(std::string &message, bool cache_result,
                                    const bool ignore_cvar) {
  if (message.empty())
    return false;
  if (!ignore_cvar && !ui::game::CVarSystem::Instance().GetCVarBool("profanityFilter")) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_mature_filter_mutex);
  if (const MatureFilterCacheEntry *cached = FindMatureFilterCacheEntryLocked(message)) {
    if (cached->matched) {
      message = cached->filtered;
    }
    return cached->matched;
  }

  std::string filtered = message;
  const bool found = ApplyMatureLanguageExpressions(
      filtered, g_builtin_mature_filters[GetCurrentChatFilterLocaleIndex()]);

  if (cache_result) {
    StoreMatureFilterCacheEntryLocked(message, found, found ? filtered : "");
  }
  if (found) {
    message = std::move(filtered);
  }
  return found;
}

void BindChatDisplayDbcLoader(const openwow::data::dbc::DbcLoader *dbc) {
  std::scoped_lock lock(g_spam_filter_mutex, g_mature_filter_mutex, g_language_word_mutex);
  g_chat_display_dbc = dbc;
  g_builtin_spam_filters = BuildBuiltinSpamExpressions(dbc);
  g_builtin_mature_filters = BuildBuiltinMatureExpressions(dbc);
  g_language_words = BuildLanguageWordTable(dbc);
  g_language_skills = BuildLanguageSkillCatalog(dbc);
  ClearSpamFilterCacheLocked();
  ClearMatureFilterCacheLocked();
}

void SetChatDisplayServerSpamFilters(std::vector<std::string> patterns) {
  std::lock_guard<std::mutex> lock(g_spam_filter_mutex);
  g_server_spam_filters = CompileServerSpamExpressions(patterns);
}

void ResetChatDisplaySpamFilterState() {
  std::scoped_lock lock(g_spam_filter_mutex, g_mature_filter_mutex, g_language_word_mutex);
  g_chat_display_dbc = nullptr;
  g_builtin_spam_filters.clear();
  g_server_spam_filters.clear();
  for (auto &locale_expressions : g_builtin_mature_filters) {
    locale_expressions.clear();
  }
  g_language_words.clear();
  g_language_skills = {};
  ClearSpamFilterCacheLocked();
  ClearMatureFilterCacheLocked();
  g_filter_char_index = 0;
  g_chat_display_message_counter = 0;
  g_chat_initialized = false;
}

void ThrottledChat_ProcessQueue() {}

void Chat_RegisterOpcodes() {
  if (g_chat_initialized)
    return;
  g_chat_initialized = true;

  auto &sys = ChatSystem::Get();
  sys.Reset();

  diagnostics::Log(diagnostics::LogLevel::kInfo, "Chat_RegisterOpcodes: chat system initialized");
}

void Chat_Shutdown(openwow::audio::SoundRuntime& sound_runtime) {
  {

    std::scoped_lock lock(g_spam_filter_mutex, g_mature_filter_mutex, g_language_word_mutex);
    g_server_spam_filters.clear();
    ClearSpamFilterCacheLocked();
    ClearMatureFilterCacheLocked();
    g_filter_char_index = 0;
    g_chat_display_message_counter = 0;
    g_chat_initialized = false;
  }

  ShutdownClientTextLogs();
  ChatSystem::Get().Reset();
  ui::ChatFrameManager::Get().Reset();
  ui::game::ChatWindowState::Get().Reset();
  ChannelManager::Get().Reset();
  VoiceChat::Get().Reset(sound_runtime);

  diagnostics::Log(diagnostics::LogLevel::kInfo, "Chat_Shutdown: chat runtime reset");
}

}
