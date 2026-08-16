
#pragma once

#include "openwow/game/chat_types.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}
namespace openwow::audio { class SoundRuntime; }

namespace openwow::game {

class CGPlayer_C;
class ObjectManager;

namespace ChatDisplayType {
constexpr int Value(const ChatMsg type) noexcept {
  return static_cast<int>(type);
}

constexpr int kSystem = Value(ChatMsg::kSystem);
constexpr int kSay = Value(ChatMsg::kSay);
constexpr int kParty = Value(ChatMsg::kParty);
constexpr int kRaid = Value(ChatMsg::kRaid);
constexpr int kGuild = Value(ChatMsg::kGuild);
constexpr int kOfficer = Value(ChatMsg::kOfficer);
constexpr int kYell = Value(ChatMsg::kYell);
constexpr int kWhisper = Value(ChatMsg::kWhisper);
constexpr int kWhisperForeign = Value(ChatMsg::kWhisperForeign);
constexpr int kWhisperInform = Value(ChatMsg::kWhisperInform);
constexpr int kReply = kWhisperInform;
constexpr int kEmote = Value(ChatMsg::kEmote);
constexpr int kTextEmote = Value(ChatMsg::kTextEmote);
constexpr int kMonsterSay = Value(ChatMsg::kMonsterSay);
constexpr int kMonsterParty = Value(ChatMsg::kMonsterParty);
constexpr int kMonsterYell = Value(ChatMsg::kMonsterYell);
constexpr int kMonsterWhisper = Value(ChatMsg::kMonsterWhisper);
constexpr int kMonsterEmote = Value(ChatMsg::kMonsterEmote);
constexpr int kChannel = Value(ChatMsg::kChannel);
constexpr int kZoneUnderAttack = kChannel;
constexpr int kChannelJoin = Value(ChatMsg::kChannelJoin);
constexpr int kChannelLeave = Value(ChatMsg::kChannelLeave);
constexpr int kChannelList = Value(ChatMsg::kChannelList);
constexpr int kChannelNotice = Value(ChatMsg::kChannelNotice);
constexpr int kChannelNoticeUser = Value(ChatMsg::kChannelNoticeUser);
constexpr int kLoot = Value(ChatMsg::kLoot);
constexpr int kMoney = Value(ChatMsg::kMoney);
constexpr int kOpening = Value(ChatMsg::kOpening);
constexpr int kCombatSkill = kOpening;
constexpr int kTradeskills = Value(ChatMsg::kTradeskills);
constexpr int kCombatTrade = kTradeskills;
constexpr int kPetInfo = Value(ChatMsg::kPetInfo);
constexpr int kCombatPet = kPetInfo;
constexpr int kCombatMisc = Value(ChatMsg::kCombatMiscInfo);
constexpr int kCombatXP = Value(ChatMsg::kCombatXpGain);
constexpr int kCombatHonor = Value(ChatMsg::kCombatHonorGain);
constexpr int kCombatFactionChange = Value(ChatMsg::kCombatFactionChange);
constexpr int kBGSystem = Value(ChatMsg::kBgSystemNeutral);
constexpr int kBGSystemAlliance = Value(ChatMsg::kBgSystemAlliance);
constexpr int kBGSystemHorde = Value(ChatMsg::kBgSystemHorde);
constexpr int kRaidLeader = Value(ChatMsg::kRaidLeader);
constexpr int kRaidWarning = Value(ChatMsg::kRaidWarning);
constexpr int kRaidBossEmote = Value(ChatMsg::kRaidBossEmote);
constexpr int kRaidBossWhisper = Value(ChatMsg::kRaidBossWhisper);
constexpr int kFiltered = Value(ChatMsg::kFiltered);
constexpr int kBattleground = Value(ChatMsg::kBattleground);
constexpr int kBattlegroundLeader = Value(ChatMsg::kBattlegroundLeader);
constexpr int kRestricted = Value(ChatMsg::kRestricted);
constexpr int kAchievement = Value(ChatMsg::kAchievement);
constexpr int kGuildAchieve = Value(ChatMsg::kGuildAchievement);
constexpr int kPartyLeader = Value(ChatMsg::kPartyLeader);
constexpr int kTargetIcons = Value(ChatMsg::kTargetIcons);
constexpr int kBnWhisper = Value(ChatMsg::kBnWhisper);
constexpr int kBnWhisperInform = Value(ChatMsg::kBnWhisperInform);
constexpr int kBnConversation = Value(ChatMsg::kBnConversation);
constexpr int kBnConversationNotice = Value(ChatMsg::kBnConversationNotice);
constexpr int kBnConversationList = Value(ChatMsg::kBnConversationList);
constexpr int kBnInlineToastAlert = Value(ChatMsg::kBnInlineToastAlert);
constexpr int kBnInlineToastBroadcast = Value(ChatMsg::kBnInlineToastBroadcast);
constexpr int kBnInlineToastBroadcastInform =
    Value(ChatMsg::kBnInlineToastBroadcastInform);
constexpr int kBnInlineToastConversation = Value(ChatMsg::kBnInlineToastConversation);
}

struct ChatFrameFormatOptions {
  std::size_t output_limit = std::numeric_limits<std::size_t>::max();
  bool preserve_angle_bracket_spans = false;
  bool preserve_separators = false;
};

[[nodiscard]] std::string ChatFrame_FormatMessage(
    const ObjectManager& objects,
    std::uint32_t language_id,
    std::uint32_t comprehension_value,
    std::string_view message,
    const ChatFrameFormatOptions& options = {});

void ChatFrame_SetWorldUiReadyAndFlush(const ObjectManager& objects);
void ChatFrame_ResetWorldUiReady();

void ChatFrame_DisplayMessage(
    const ObjectManager& objects,
    const char* message,
    int chat_type,
    const char* sender_name,
    int language_id,
    const char* channel_name,
    const char* secondary_name,
    const char* flag_tag,
    std::uint64_t sender_guid,
    int aux_value,
    std::uint64_t target_guid,
    int aux_flags,
    int is_gm,
    const void* extra_data);

bool ChatFrame_CheckProfanityFilter(std::string& message, bool cache_result);

bool ChatFrame_MatureLanguageFilter(std::string& message, bool cache_result,
                                    bool ignore_cvar = false);

void BindChatDisplayDbcLoader(const openwow::data::dbc::DbcLoader* dbc);

void SetChatDisplayServerSpamFilters(std::vector<std::string> patterns);

void ResetChatDisplaySpamFilterState();

struct ChatLanguageInfo {
  std::uint32_t id = 0;
  std::string_view name;
};

[[nodiscard]] std::optional<ChatLanguageInfo> FindChatLanguageByName(
    const openwow::data::dbc::DbcLoader& dbc, std::string_view language_name);
[[nodiscard]] std::optional<ChatLanguageInfo> FindChatLanguageById(
    const openwow::data::dbc::DbcLoader& dbc, std::uint32_t language_id);
[[nodiscard]] std::uint32_t ResolveDefaultChatLanguageId(
    const CGPlayer_C& player, const openwow::data::dbc::DbcLoader& dbc);
[[nodiscard]] std::uint32_t GetChatLanguageComprehensionValue(
    const CGPlayer_C& player, const openwow::data::dbc::DbcLoader& dbc,
    std::uint32_t language_id);
[[nodiscard]] std::vector<ChatLanguageInfo> CollectAvailableChatLanguages(
    const CGPlayer_C& player, const openwow::data::dbc::DbcLoader& dbc);

void ThrottledChat_ProcessQueue();

void Chat_RegisterOpcodes();

void Chat_Shutdown(openwow::audio::SoundRuntime& sound_runtime);

}
