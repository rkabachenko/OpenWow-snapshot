
#include "openwow/ui/game/api/game_lua_api_quest.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/group_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/localization.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/quest_dialog_close.h"
#include "openwow/game/power_lua_bridge.h"
#include "openwow/game/quest_dialog_text.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_manager.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/template_name_variant.h"
#include "openwow/game/quest_xp_calc.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/spell_missile_runtime.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/net/realm_config_tables.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/quest_leaderboard_builder.h"
#include "openwow/ui/game/quest_log_interleaved.h"
#include "openwow/ui/game/quest_special_item.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/tooltip_formatter.h"
#include "openwow/ui/game/world_map_system.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <map>
#include <optional>
#include <string_view>

namespace openwow::ui::game::detail {

static int s_selected_quest_log_index = 0;
static std::uint32_t s_selected_quest_id = 0;
static std::uint32_t s_abandon_quest_id = 0;

namespace {

constexpr double kRecruitAFriendQuestDistanceSq = 10000.0;
constexpr std::uint32_t kRecruitAFriendQuestBonusMaxLevel = 60;
constexpr std::uint32_t kQuestFrameProgressTutorialIndex = 0x21u;

constexpr std::uint32_t kDefaultConfirmAcceptQuestId = 0u;
constexpr int kQuestPreviewItemVisibleRows = 6;
constexpr int kQuestPreviewItemLuaMaxIndex = 7;
constexpr std::uint32_t kUnitFlagReferAFriendLinked = 0x40;
constexpr std::uint32_t kSpellAuraXpGainPct = 291;
enum class QuestPreviewItemType : std::uint8_t {
  kReward,
  kChoice,
  kRequired,
  kInvalid,
};

struct QuestPreviewItemSlot {
  bool is_valid = false;
  ::openwow::game::QuestRewardItem item;
};

}

QuestPreviewItemType ParseQuestPreviewItemType(std::string_view item_type);

const ::openwow::game::CGPlayer_C *
GetQuestPreviewActivePlayer(const ::openwow::game::WorldSession *session) {
  return session != nullptr ? session->objects().GetActivePlayer() : nullptr;
}

bool LocalPlayerMeetsItemTemplateRequirements(
    const ::openwow::game::WorldSession *session,
    const ::openwow::game::ItemUseRequirementView &item_template) {
  const auto *player = GetQuestPreviewActivePlayer(session);
  if (player == nullptr) {
    return true;
  }
  const auto proficiency =
      session != nullptr
          ? session->session().GetProficiencyMask(
                static_cast<std::uint8_t>(item_template.item_class))
          : 0u;
  return ::openwow::game::PlayerMeetsItemUseRequirements(
      *player, item_template,
      session != nullptr ? session->item_use_requirement_sources()
                         : ::openwow::game::ItemUseRequirementSources{},
      proficiency);
}

std::string ResolveQuestPreviewTexturePath(lua_State *L, std::uint32_t display_info_id) {
  const auto *dbc = GetDbcLoader(L);
  if (dbc != nullptr && display_info_id != 0) {
    if (const auto *display = dbc->item_display_info().LookupEntry(display_info_id);
        display != nullptr && !std::string_view(display->inventory_icon).empty()) {
      return BuildItemIconTexturePath(display->inventory_icon);
    }
  }

  return BuildItemIconTexturePath(kFallbackItemIconName);
}

std::uint64_t ResolveQuestAcceptGuid(const ::openwow::game::QuestDetailsDialog &dialog) {
  const auto sharer_guid = dialog.sharer_guid.GetRawValue();
  return sharer_guid != 0 ? sharer_guid : dialog.npc_guid.GetRawValue();
}

bool HasQuestFrameActivePlayer(const ::openwow::game::WorldSession &session) {
  return session.objects().GetActivePlayer() != nullptr;
}

int GetQuestGreetingSelectionLuaIndex(lua_State *L) {
  return static_cast<int>(TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1)));
}

void TriggerQuestFrameProgressTutorial() {
  ::openwow::game::TutorialSystem::Instance().TriggerTutorial(
      kQuestFrameProgressTutorialIndex);
}

static const ::openwow::game::QuestTemplate *
GetSelectedQuestLogTemplate(::openwow::game::WorldSession *session);
static const ::openwow::game::QuestTemplate *
GetOrRequestQuestTemplate(::openwow::game::WorldSession &session, std::uint32_t quest_id);
static int PushEmptyQuestLogRewardInfo(lua_State *L);
static bool LocalPlayerCanUseQuestLogItem(const ::openwow::game::WorldSession &session,
                                          const ::openwow::game::ItemTemplate &item_template);
template <std::size_t N>
static int PushQuestLogItemInfoFromTemplate(lua_State *L,
                                            const ::openwow::game::QuestRewardItem (&rewards)[N],
                                            int item_index);

static int PushQuestGreetingLevel(lua_State *L, bool want_active_entry, const char *usage) {
  auto *session = GetWorldSession(L);
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, usage);
  }

  const int index = static_cast<int>(lua_tonumber(L, 1));
  int level = 0;
  if (session) {
    if (const auto entry = GetQuestGreetingEntry(*session, index, want_active_entry)) {
      level = entry->quest_level;
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(level));
  return 1;
}

inline constexpr std::uint32_t kWotlkMaxPlayerLevel = 80;

static const std::vector<::openwow::game::QuestRewardItem> *
GetSharedQuestRewardPreviewChoiceRewards(const ::openwow::game::QuestManager &quests) {
  if (quests.has_active_reward()) {
    return &quests.active_reward().reward_choice_items;
  }
  if (quests.has_active_details()) {
    return &quests.active_details().reward_choice_items;
  }
  return nullptr;
}

static const std::vector<::openwow::game::QuestRewardItem> *
GetSharedQuestRewardPreviewRewards(const ::openwow::game::QuestManager &quests) {
  if (quests.has_active_reward()) {
    return &quests.active_reward().reward_items;
  }
  if (quests.has_active_details()) {
    return &quests.active_details().reward_items;
  }
  return nullptr;
}

static std::size_t
CountContiguousQuestPreviewItems(const std::vector<::openwow::game::QuestRewardItem> *items) {
  if (!items) {
    return 0;
  }

  const auto visible_end =
      items->begin() +
      std::min(items->size(), static_cast<std::size_t>(kQuestPreviewItemVisibleRows));
  const auto first_empty =
      std::find_if(items->begin(), visible_end,
                   [](const ::openwow::game::QuestRewardItem &item) { return item.item_id == 0; });
  return static_cast<std::size_t>(std::distance(items->begin(), first_empty));
}

template <std::size_t N>
static std::size_t
CountContiguousQuestTemplateItems(const ::openwow::game::QuestRewardItem (&items)[N]) {
  std::size_t count = 0;
  for (const auto &item : items) {
    if (item.item_id == 0) {
      break;
    }
    ++count;
  }
  return count;
}

static std::uint32_t GetSharedQuestRewardPreviewMoney(const ::openwow::game::QuestManager &quests) {
  if (quests.has_active_reward()) {
    return quests.active_reward().reward_money;
  }
  if (quests.has_active_details()) {
    return quests.active_details().reward_money;
  }
  return 0;
}

static std::uint32_t
GetQuestLogRewardMoneyValue(const ::openwow::game::QuestTemplate &quest_template,
                            const ::openwow::game::CGPlayer_C *player) {
  const auto signed_reward_money = static_cast<std::int32_t>(quest_template.reward_money);
  std::uint32_t reward_money =
      signed_reward_money > 0 ? static_cast<std::uint32_t>(signed_reward_money) : 0;
  if (player != nullptr && player->State().GetLevel() >= player->GetMaxLevel()) {
    reward_money = std::max(reward_money, quest_template.rew_money_max_level);
  }
  return reward_money;
}

static std::uint32_t
GetSharedQuestRewardPreviewArenaPoints(const ::openwow::game::QuestManager &quests) {
  if (quests.has_active_reward()) {
    return quests.active_reward().rew_arena_points;
  }
  if (quests.has_active_details()) {
    return quests.active_details().rew_arena_points;
  }
  return 0;
}

static std::uint32_t GetSharedQuestRewardPreviewHonor(const ::openwow::game::QuestManager &quests) {
  constexpr std::uint32_t kQuestRewardHonorDivisor = 10;

  if (quests.has_active_reward()) {
    return quests.active_reward().reward_honor / kQuestRewardHonorDivisor;
  }
  if (quests.has_active_details()) {
    return quests.active_details().reward_honor / kQuestRewardHonorDivisor;
  }
  return 0;
}

static std::uint32_t
GetSharedQuestRewardPreviewTalentPoints(const ::openwow::game::QuestManager &quests) {
  if (quests.has_active_reward()) {
    return quests.active_reward().bonus_talents;
  }
  if (quests.has_active_details()) {
    return quests.active_details().bonus_talents;
  }
  return 0;
}

static std::uint32_t GetSharedQuestRewardPreviewXp(const ::openwow::game::WorldSession &session) {
  const auto *player = session.objects().GetLocalPlayerTyped();
  if (!player || player->State().GetLevel() >= kWotlkMaxPlayerLevel) {
    return 0;
  }

  const auto &quests = session.quests();
  if (quests.has_active_reward()) {
    return quests.active_reward().reward_xp;
  }
  if (quests.has_active_details()) {
    return quests.active_details().reward_xp;
  }
  return 0;
}

static std::uint32_t
GetSharedQuestRewardPreviewSpellId(const ::openwow::game::QuestManager &quests) {
  if (quests.has_active_reward()) {
    return quests.active_reward().rew_spell;
  }
  if (quests.has_active_details()) {
    return quests.active_details().rew_spell;
  }
  return 0;
}

static std::int32_t
GetSharedQuestRewardPreviewSpellCastId(const ::openwow::game::QuestManager &quests) {
  if (quests.has_active_reward()) {
    return quests.active_reward().rew_spell_cast;
  }
  if (quests.has_active_details()) {
    return quests.active_details().rew_spell_cast;
  }
  return 0;
}

static std::uint32_t
GetSharedQuestRewardPreviewTitleId(const ::openwow::game::QuestManager &quests) {
  if (quests.has_active_reward()) {
    return quests.active_reward().char_title_id;
  }
  if (quests.has_active_details()) {
    return quests.active_details().char_title_id;
  }
  return 0;
}

template <typename T, typename Accessor>
static T GetActiveQuestDialogValue(const ::openwow::game::QuestManager &quests, Accessor accessor,
                                   T fallback) {
  if (quests.has_active_request()) {
    return accessor(quests.active_request());
  }
  if (quests.has_active_reward()) {
    return accessor(quests.active_reward());
  }
  if (quests.has_active_details()) {
    return accessor(quests.active_details());
  }
  return fallback;
}

static bool AreContiguousRequiredQuestPreviewItemsComplete(
    const ::openwow::game::PlayerInventoryReplica &inventory,
    const std::vector<::openwow::game::QuestRewardItem> &required_items) {
  for (const auto &item : required_items) {
    if (item.item_id == 0) {
      return true;
    }
    if (inventory.GetItemCount(item.item_id) < item.count) {
      return false;
    }
  }

  return true;
}

static std::uint32_t
GetActiveQuestDialogSuggestedPlayers(const ::openwow::game::QuestManager &quests) {
  return GetActiveQuestDialogValue<std::uint32_t>(
      quests, [](const auto &dialog) { return dialog.suggested_players; }, 0);
}

static ::openwow::game::QuestFlags
GetActiveQuestDialogFlags(const ::openwow::game::QuestManager &quests) {
  return GetActiveQuestDialogValue<::openwow::game::QuestFlags>(
      quests, [](const auto &dialog) { return dialog.quest_flags; },
      ::openwow::game::QuestFlags::kNone);
}

static std::uint32_t GetQuestMoneyToGetValue(const ::openwow::game::QuestManager &quests) {

  if (quests.has_active_reward()) {
    return quests.active_reward().required_money;
  }
  if (quests.has_active_request()) {
    return quests.active_request().required_money;
  }
  return 0;
}

static bool EqualsAsciiCaseInsensitive(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const auto left = static_cast<unsigned char>(lhs[i]);
    const auto right = static_cast<unsigned char>(rhs[i]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }

  return true;
}

QuestPreviewItemType ParseQuestPreviewItemType(std::string_view item_type) {
  if (EqualsAsciiCaseInsensitive(item_type, "reward")) {
    return QuestPreviewItemType::kReward;
  }
  if (EqualsAsciiCaseInsensitive(item_type, "choice")) {
    return QuestPreviewItemType::kChoice;
  }
  if (EqualsAsciiCaseInsensitive(item_type, "required")) {
    return QuestPreviewItemType::kRequired;
  }
  return QuestPreviewItemType::kInvalid;
}

static const std::vector<::openwow::game::QuestRewardItem> *
GetQuestPreviewItems(const ::openwow::game::QuestManager &quests, QuestPreviewItemType item_type) {
  if (item_type == QuestPreviewItemType::kReward) {
    return GetSharedQuestRewardPreviewRewards(quests);
  }
  if (item_type == QuestPreviewItemType::kChoice) {
    return GetSharedQuestRewardPreviewChoiceRewards(quests);
  }
  if (item_type == QuestPreviewItemType::kRequired) {
    return quests.has_active_request() ? &quests.active_request().required_items : nullptr;
  }
  return nullptr;
}

static bool HasSharedQuestRewardPreviewState(const ::openwow::game::QuestManager &quests) {
  return quests.has_active_reward() || quests.has_active_details();
}

static std::uint32_t ReadQuestTitleBufferDword(const std::string &title,
                                               const std::size_t byte_offset) {
  std::uint32_t value = 0;
  for (std::size_t byte_index = 0; byte_index < 4; ++byte_index) {
    const auto index = byte_offset + byte_index;
    const auto byte =
        index < title.size() ? static_cast<unsigned char>(title[index]) : static_cast<unsigned char>(0);
    value |= static_cast<std::uint32_t>(byte) << (8u * byte_index);
  }
  return value;
}

static ::openwow::game::QuestRewardItem
BuildOverflowQuestPreviewItem(const ::openwow::game::WorldSession &session,
                              const QuestPreviewItemType item_type) {
  ::openwow::game::QuestRewardItem item;

  const auto active_entry = GetQuestGreetingEntry(session, 1, true);
  static const std::string kEmptyTitle;
  const auto &title = (active_entry && active_entry->title != nullptr) ? *active_entry->title
                                                                      : kEmptyTitle;

  switch (item_type) {
  case QuestPreviewItemType::kReward:
    item.item_id = 0;
    item.display_info_id = 0;
    item.count = active_entry ? active_entry->quest_id : 0u;
    break;
  case QuestPreviewItemType::kChoice:
    item.item_id = active_entry ? static_cast<std::uint32_t>(active_entry->quest_level) : 0u;
    item.display_info_id = active_entry ? active_entry->quest_flags : 0u;
    item.count = ReadQuestTitleBufferDword(title, 0);
    break;
  case QuestPreviewItemType::kRequired:
    item.item_id = ReadQuestTitleBufferDword(title, 4);
    item.display_info_id = ReadQuestTitleBufferDword(title, 8);
    item.count = ReadQuestTitleBufferDword(title, 12);
    break;
  case QuestPreviewItemType::kInvalid:
    break;
  }

  return item;
}

static ::openwow::game::QuestRewardItem
BuildDefaultQuestPreviewItem(const ::openwow::game::QuestManager &quests,
                             const QuestPreviewItemType item_type) {
  ::openwow::game::QuestRewardItem item;
  if (item_type != QuestPreviewItemType::kRequired && HasSharedQuestRewardPreviewState(quests)) {
    item.count = 1;
  }
  return item;
}

static QuestPreviewItemSlot
ResolveQuestPreviewItemSlot(const ::openwow::game::WorldSession *session,
                            QuestPreviewItemType item_type, int one_based_index) {
  if (item_type == QuestPreviewItemType::kInvalid || one_based_index < 1 ||
      one_based_index > kQuestPreviewItemLuaMaxIndex) {
    return {};
  }

  QuestPreviewItemSlot slot;
  slot.is_valid = true;
  if (session == nullptr) {
    return slot;
  }

  const auto &quests = session->quests();
  if (one_based_index == kQuestPreviewItemLuaMaxIndex) {
    slot.item = BuildOverflowQuestPreviewItem(*session, item_type);
    return slot;
  }

  const auto *items = GetQuestPreviewItems(quests, item_type);
  if (items == nullptr) {
    slot.item = BuildDefaultQuestPreviewItem(quests, item_type);
    return slot;
  }

  const auto item_index = static_cast<std::size_t>(one_based_index - 1);
  if (item_index < items->size()) {
    slot.item = (*items)[item_index];
  } else {
    slot.item = BuildDefaultQuestPreviewItem(quests, item_type);
  }
  return slot;
}

std::optional<::openwow::game::QuestRewardItem>
GetQuestPreviewItem(const openwow::game::WorldSession *session, std::string_view item_type,
                    int one_based_index) {
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto slot =
      ResolveQuestPreviewItemSlot(session, ParseQuestPreviewItemType(item_type), one_based_index);
  if (!slot.is_valid) {
    return std::nullopt;
  }

  return slot.item;
}

const ::openwow::game::ItemTemplate *
GetOrRequestQuestPreviewItemTemplate(::openwow::game::WorldSession *session,
                                     const std::uint32_t item_id) {
  if (session == nullptr || item_id == 0) {
    return nullptr;
  }

  const auto owner = session->lifetime_token();
  return session->query_cache().GetOrRequestItemTemplate(
      item_id,
      ::openwow::game::QueryCache::QueryRequestOptions{
          .dedupe_callbacks = false,
          .callback = [owner, session, item_id](const bool success) {
            if (owner.expired()) {
              return;
            }
            if (!success || session->query_cache().GetItemTemplate(item_id) == nullptr) {
              return;
            }

            ScriptEventDispatch::Get().FireEvent(events::QUEST_ITEM_UPDATE);
          }});
}

static std::string BuildQuestItemLink(openwow::game::WorldSession &session, std::uint32_t item_id) {
  if (item_id == 0) {
    return {};
  }

  std::string item_name;
  auto item_quality = ::openwow::game::ItemQuality::Common;

  if (const auto *tmpl = session.query_cache().GetOrRequestItemTemplate(item_id)) {
    item_name = tmpl->name;
    item_quality = tmpl->quality;
  } else if (const auto *cached = session.item_definitions().GetItem(item_id)) {
    item_name = cached->name;
    item_quality = cached->quality;
  }

  if (item_name.empty()) {
    return {};
  }

  const auto *player = session.objects().GetLocalPlayerTyped();
  const auto player_level = player ? static_cast<std::int32_t>(player->State().GetLevel()) : 0;
  return ::openwow::game::HyperlinkParser::BuildItemLink(
      item_id, item_name, static_cast<std::uint32_t>(item_quality), 0, 0, 0,
      0, 0, 0, 0, player_level);
}

static std::string BuildQuestPreviewItemLink(openwow::game::WorldSession &session,
                                             const ::openwow::game::QuestRewardItem &item) {
  return BuildQuestItemLink(session, item.item_id);
}

static int PushSpellHyperlinkOrNil(lua_State *L, std::uint32_t spell_id) {
  const auto *spell = LookupSpellEntry(L, spell_id);
  if (!spell) {
    lua_pushnil(L);
    return 1;
  }

  const auto link =
      ::openwow::game::HyperlinkParser::BuildSpellLink(spell_id, std::string(spell->spell_name));
  lua_pushstring(L, link.c_str());
  return 1;
}

constexpr std::uint32_t kTradeSkillSpellAttribute = 0x20u;
constexpr std::array<std::uint32_t, 2> kRewardSpellLearnEffectIds = {36u, 57u};

static void PushLuaStringView(lua_State *L, const std::string_view value) {
  lua_pushlstring(L, value.data() != nullptr ? value.data() : "", value.size());
}

static std::optional<std::string_view>
LookupSpellIconTexture(lua_State *L, const openwow::data::dbc::SpellEntry &spell) {
  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr || spell.spell_icon_id == 0) {
    return std::nullopt;
  }

  const auto *icon = dbc->spell_icon().LookupEntry(spell.spell_icon_id);
  if (icon == nullptr) {
    return std::nullopt;
  }

  return icon->icon_path;
}

static bool RewardSpellCastTeachesSpell(const openwow::data::dbc::SpellEntry &spell) {
  return std::any_of(
      spell.effect.begin(), spell.effect.end(), [](const std::uint32_t effect_id) {
        return std::find(kRewardSpellLearnEffectIds.begin(), kRewardSpellLearnEffectIds.end(),
                         effect_id) != kRewardSpellLearnEffectIds.end();
      });
}

static int PushRewardSpellInfo(lua_State *L, const openwow::data::dbc::SpellEntry *reward_spell,
                               const std::int32_t reward_spell_cast_id) {
  if (reward_spell == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 4;
  }

  if (const auto icon_path = LookupSpellIconTexture(L, *reward_spell); icon_path.has_value()) {
    PushLuaStringView(L, *icon_path);
  } else {
    lua_pushnil(L);
  }

  PushLuaStringView(L, reward_spell->spell_name);
  lua_pushwowbool(L, (reward_spell->attributes & kTradeSkillSpellAttribute) != 0);

  const auto *reward_spell_cast =
      reward_spell_cast_id != 0
          ? LookupSpellEntry(L, static_cast<std::uint32_t>(reward_spell_cast_id))
          : nullptr;
  lua_pushwowbool(L, reward_spell_cast != nullptr && RewardSpellCastTeachesSpell(*reward_spell_cast));
  return 4;
}

static bool SyncSelectedQuestLogIndex(::openwow::game::WorldSession &session) {
  if (s_selected_quest_id == 0) {
    s_selected_quest_log_index = 0;
    return false;
  }

  s_selected_quest_log_index = FindInterleavedQuestIndexById(session, s_selected_quest_id);
  return s_selected_quest_log_index >= 1;
}

static std::uint32_t ResolveSelectedVisibleQuestId(::openwow::game::WorldSession &session) {
  if (s_selected_quest_id == 0) {
    return 0;
  }

  const auto view = BuildQuestLogView(session);
  const auto visible_limit = std::min(view.visible_count, view.entries.size());
  const auto &log = session.quests().quest_log();
  for (std::size_t index = 0; index < visible_limit; ++index) {
    const auto &entry = view.entries[index];
    if (entry.is_header || entry.quest_log_index >= log.size()) {
      continue;
    }

    if (log[entry.quest_log_index].quest_id == s_selected_quest_id) {
      return s_selected_quest_id;
    }
  }

  return 0;
}

static bool SetQuestHeaderCollapsed(::openwow::game::WorldSession *session,
                                    const int interleaved_1based, const bool collapsed) {
  if (session == nullptr) {
    SetAllQuestLogHeadersCollapsed(collapsed);
    return true;
  }

  const auto view = BuildQuestLogView(*session);
  const auto index = static_cast<std::size_t>(interleaved_1based - 1);
  if (interleaved_1based >= 1 && index < view.entries.size() && view.entries[index].is_header) {
    const auto sort_slot =
        std::find(view.sort_order.begin(), view.sort_order.end(), view.entries[index].sort_key);
    if (sort_slot != view.sort_order.end()) {
      SetQuestLogHeaderCollapsedBySortSlot(
          static_cast<std::size_t>(std::distance(view.sort_order.begin(), sort_slot)), collapsed);
    } else {
      return false;
    }
  } else {
    SetAllQuestLogHeadersCollapsed(collapsed);
  }

  SyncSelectedQuestLogIndex(*session);
  return true;
}

static bool IsSelectedQuestMarkedFailed(const ::openwow::game::QuestManager &qm) {
  if (s_selected_quest_id == 0)
    return false;
  const auto *entry = qm.FindQuestLogEntry(s_selected_quest_id);
  return entry && entry->status == ::openwow::game::QuestStatus::kFailed;
}

static int FindPlayerQuestSlotById(const ::openwow::game::CGPlayer_C &player,
                                   std::uint32_t quest_id) {
  for (int slot = 0; slot < ::openwow::game::kMaxQuestLogEntries; ++slot) {
    if (player.GetQuestLog(static_cast<std::uint8_t>(slot)).quest_id == quest_id) {
      return slot;
    }
  }
  return -1;
}

static int FindAbandonQuestSlot(::openwow::game::WorldSession &session,
                                std::uint32_t quest_id) {
  if (quest_id == 0) {
    return -1;
  }

  const auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return -1;
  }

  return FindPlayerQuestSlotById(*player, quest_id);
}

struct TimedQuestView {
  int visible_index = 0;
  int player_slot = -1;
  std::uint32_t quest_id = 0;
  std::uint32_t raw_state = 0;
  std::uint32_t raw_timer = 0;
  int seconds_left = 0;
  ::openwow::game::QuestLogEntry *quest_log_entry = nullptr;
};

static std::optional<TimedQuestView>
BuildTimedQuestViewFromPlayerSlot(::openwow::game::WorldSession &session,
                                  int quest_slot,
                                  int visible_index);

static std::int64_t GetQuestTimerLocalOffsetSeconds(const ::openwow::game::WorldSession &session) {
  return session.query_time().local_time_offset_secs;
}

static bool IsQuestTimerSuppressed(const TimedQuestView &timed_quest) {
  return (timed_quest.raw_state & 0x02u) != 0;
}

static bool IsQuestTimerFailureSuppressed(const ::openwow::game::WorldSession &session,
                                          std::uint32_t quest_id) {
  if (quest_id == 0) {
    return true;
  }

  return IsQuestTurnInReady(session, quest_id);
}

static std::optional<TimedQuestView> BuildTimedQuestView(::openwow::game::WorldSession &session,
                                                         int visible_index) {
  if (visible_index < 1) {
    return std::nullopt;
  }

  auto *player = session.objects().GetLocalPlayerTyped();
  if (!player) {
    return std::nullopt;
  }

  const std::uint32_t quest_id = ResolveQuestIdFromInterleavedIndex(session, visible_index);
  if (quest_id == 0) {
    return std::nullopt;
  }

  const int quest_slot = FindPlayerQuestSlotById(*player, quest_id);
  if (quest_slot < 0) {
    return std::nullopt;
  }

  return BuildTimedQuestViewFromPlayerSlot(session, quest_slot, visible_index);
}

static std::optional<TimedQuestView>
BuildTimedQuestViewFromPlayerSlot(::openwow::game::WorldSession &session,
                                  const int quest_slot,
                                  const int visible_index) {
  if (quest_slot < 0 || quest_slot >= ::openwow::game::kMaxQuestLogEntries) {
    return std::nullopt;
  }

  auto *player = session.objects().GetLocalPlayerTyped();
  if (!player) {
    return std::nullopt;
  }

  const auto player_entry = player->GetQuestLog(static_cast<std::uint8_t>(quest_slot));
  if (player_entry.quest_id == 0 || player_entry.timer == 0) {
    return std::nullopt;
  }

  TimedQuestView timed_quest;
  timed_quest.visible_index = visible_index;
  timed_quest.player_slot = quest_slot;
  timed_quest.quest_id = player_entry.quest_id;
  timed_quest.raw_state = player_entry.state;
  timed_quest.raw_timer = player_entry.timer;
  timed_quest.quest_log_entry = session.quests().FindQuestLogEntry(player_entry.quest_id);

  const auto local_now = static_cast<std::int64_t>(std::time(nullptr));
  const auto timer_deadline =
      static_cast<std::int64_t>(timed_quest.raw_timer) + GetQuestTimerLocalOffsetSeconds(session);
  timed_quest.seconds_left = static_cast<int>(timer_deadline - local_now - 1);
  return timed_quest;
}

static void DisplayTimedQuestFailureMessage(
    const ::openwow::game::QuestTemplate &quest_template) {
  constexpr int kQuestFailedSystemMessageId = 148;
  DisplaySystemMessage(kQuestFailedSystemMessageId, quest_template.title.c_str());
}

static bool IsTimedQuestFailureSlotValid(const ::openwow::game::WorldSession &session,
                                         const int player_slot,
                                         const std::uint32_t quest_id) {
  if (player_slot < 0 || player_slot >= ::openwow::game::kMaxQuestLogEntries) {
    return false;
  }

  const auto *player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return false;
  }

  const auto player_entry = player->GetQuestLog(static_cast<std::uint8_t>(player_slot));
  return player_entry.quest_id == quest_id && player_entry.timer != 0;
}

static ::openwow::game::AsyncQueryChannel::Callback
BuildTimedQuestFailureMessageCallback(::openwow::game::WorldSession &session,
                                      const int player_slot,
                                      const std::uint32_t quest_id) {
  return [&session, player_slot, quest_id](const bool success) {
    if (!IsTimedQuestFailureSlotValid(session, player_slot, quest_id)) {
      ::openwow::debug::DebugConsole::Get().Write("Invalid quest log entry");
      return;
    }

    if (!success) {
      return;
    }

    const auto *quest_template = session.quests().GetTemplate(quest_id);
    if (quest_template == nullptr) {
      return;
    }

    DisplayTimedQuestFailureMessage(*quest_template);
  };
}

static std::uint32_t ResolveQuestItemDropCountQuestId(lua_State *L,
                                                      ::openwow::game::WorldSession &session) {
  if (!lua_isnumber(L, 1)) {
    return s_selected_quest_id;
  }

  const int visible_index = static_cast<int>(lua_tonumber(L, 1)) - 1;
  if (visible_index < 0) {
    return 0;
  }

  const auto view = BuildQuestLogView(session);
  if (visible_index >= static_cast<int>(view.visible_count)) {
    return 0;
  }

  const auto &entry = view.entries[static_cast<std::size_t>(visible_index)];
  if (entry.is_header) {
    return 0;
  }

  const auto &log = session.quests().quest_log();
  if (entry.quest_log_index >= log.size()) {
    return 0;
  }

  return log[entry.quest_log_index].quest_id;
}

static std::uint32_t ResolveQuestLogItemDropQuestId(lua_State *L,
                                                    ::openwow::game::WorldSession &session) {
  if (!lua_isnumber(L, 2)) {
    return s_selected_quest_id;
  }

  const int visible_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 2)) - 1;
  if (visible_index < 0) {
    return s_selected_quest_id;
  }

  const auto view = BuildQuestLogView(session);
  if (visible_index >= static_cast<int>(view.visible_count)) {
    return s_selected_quest_id;
  }

  const auto &entry = view.entries[static_cast<std::size_t>(visible_index)];
  if (entry.is_header) {
    return s_selected_quest_id;
  }

  const auto &log = session.quests().quest_log();
  if (entry.quest_log_index >= log.size()) {
    return s_selected_quest_id;
  }

  return log[entry.quest_log_index].quest_id;
}

static int CountMissingQuestItemDropObjectives(
    const ::openwow::game::PlayerInventoryReplica &inventory,
    const ::openwow::game::QuestTemplate &tmpl) {
  int count = 0;
  for (const auto &objective : tmpl.item_drop_objectives) {
    if (objective.item_id == 0) {
      continue;
    }

    if (objective.item_id == tmpl.src_item_id && objective.required_count <= 1) {
      continue;
    }

    if (inventory.GetItemCount(objective.item_id) == 0) {
      ++count;
    }
  }

  return count;
}

template <std::size_t N>
static std::string ResolvePluralizedName(const std::string &base_name,
                                         const std::array<std::string, N> &alternate_names,
                                         std::uint32_t count) {
  return std::string(
      ::openwow::game::GetQuestObjectiveNameVariantOrBase(base_name, alternate_names, count));
}

static std::string ResolveCreatureObjectiveName(::openwow::game::WorldSession &session,
                                                std::int32_t creature_entry,
                                                std::uint32_t required_count) {
  if (creature_entry <= 0)
    return " ";
  const auto *tmpl = session.query_cache().GetOrRequestCreatureTemplate(
      static_cast<std::uint32_t>(creature_entry),
      {.dedupe_callbacks = false,
       .callback = BuildQuestRequirementQueryCallback("Invalid creature in quest")});
  if (tmpl) {
    return ResolvePluralizedName(tmpl->name, tmpl->alternate_names, required_count);
  }
  return " ";
}

static std::string ResolveGameObjectObjectiveName(::openwow::game::WorldSession &session,
                                                  std::int32_t object_entry,
                                                  std::uint32_t required_count) {
  const auto raw_entry = static_cast<std::uint32_t>(-object_entry);
  if (raw_entry == 0)
    return " ";
  const auto *tmpl = session.query_cache().GetOrRequestGameObjectTemplate(
      raw_entry,
      {.dedupe_callbacks = false,
       .callback = BuildQuestRequirementQueryCallback("Invalid object in quest")});
  if (tmpl) {
    return ResolvePluralizedName(tmpl->name, tmpl->alternate_names, required_count);
  }
  return " ";
}

static std::string ResolveItemObjectiveName(::openwow::game::WorldSession &session,
                                            std::uint32_t item_id) {
  if (item_id == 0)
    return " ";
  if (const auto *item = session.query_cache().GetOrRequestItemTemplate(
          item_id,
          {.dedupe_callbacks = false, .callback = BuildQuestRequirementQueryCallback("")})) {
    if (!item->name.empty())
      return item->name;
  }
  return " ";
}

static void ClearSelectedQuestGuidePointOfInterest(
    openwow::ui::MinimapSystem &minimap) {
  minimap.ClearGuidePointOfInterest();
}

static void ApplySelectedQuestGuidePointOfInterest(
    openwow::ui::MinimapSystem &minimap,
    const ::openwow::game::QuestTemplate &quest_template) {
  minimap.SetGuidePointOfInterest(
      quest_template.poi_x, quest_template.poi_y, quest_template.poi_option,
      quest_template.title);
}

static void RefreshSelectedQuestGuidePointOfInterest(
    ::openwow::game::WorldSession &session,
    openwow::ui::MinimapSystem &minimap);

static ::openwow::game::AsyncQueryChannel::Callback
BuildSelectedQuestGuidePointOfInterestCallback(
    ::openwow::game::WorldSession &session,
    openwow::ui::MinimapSystem &minimap) {
  return [&session, &minimap](bool success) {
    if (success) {
      RefreshSelectedQuestGuidePointOfInterest(session, minimap);
      return;
    }
    ::openwow::debug::DebugConsole::Get().Write("Invalid quest log entry");
  };
}

static const ::openwow::game::QuestTemplate *
GetOrRequestQuestTemplate(::openwow::game::WorldSession &session, std::uint32_t quest_id) {
  return session.quests().GetOrRequestTemplate(
      quest_id,
      {.dedupe_callbacks = false,
       .callback = BuildQuestRequirementQueryCallback("Invalid quest log entry")});
}

static const ::openwow::game::QuestTemplate *
GetOrRequestSelectedQuestLogTemplate(
    ::openwow::game::WorldSession &session,
    openwow::ui::MinimapSystem &minimap) {
  return session.quests().GetOrRequestTemplate(
      s_selected_quest_id,
      {.dedupe_callbacks = false,
       .callback =
           BuildSelectedQuestGuidePointOfInterestCallback(session, minimap)});
}

static void RefreshSelectedQuestGuidePointOfInterest(
    ::openwow::game::WorldSession &session,
    openwow::ui::MinimapSystem &minimap) {
  if (s_selected_quest_id == 0) {
    ClearSelectedQuestGuidePointOfInterest(minimap);
    return;
  }

  const auto *quest_template =
      GetOrRequestSelectedQuestLogTemplate(session, minimap);
  if (quest_template == nullptr) {
    return;
  }

  ApplySelectedQuestGuidePointOfInterest(minimap, *quest_template);
}

struct QuestWorldMapAreaSelection {
  std::uint32_t world_map_area_id = 0;
  std::uint32_t floor_id = 0;
};

static bool IsQuestReadyForTurnInOnWorldMap(const ::openwow::game::WorldSession &session,
                                            const ::openwow::game::QuestTemplate &quest_template,
                                            const CGPlayer_C::QuestLogEntry &player_slot) {
  if ((player_slot.state & 0x02u) != 0) {
    return false;
  }

  if ((player_slot.state & 0x10000u) != 0) {
    return true;
  }

  if ((HasFlag(quest_template.flags, ::openwow::game::QuestFlags::kPartyAccept) ||
       HasFlag(quest_template.flags, ::openwow::game::QuestFlags::kExploration)) &&
      (player_slot.state & 0x01u) == 0) {
    return false;
  }

  const auto *player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return false;
  }

  const auto &reputation = ::openwow::game::ReputationInfo::Get();
  if (quest_template.required_reputation_faction != 0 &&
      reputation.GetCurrentStanding(static_cast<std::int32_t>(
          quest_template.required_reputation_faction)) < quest_template.required_reputation_value) {
    return false;
  }

  if (quest_template.required_reputation_faction_max != 0 &&
      reputation.GetCurrentStanding(
          static_cast<std::int32_t>(quest_template.required_reputation_faction_max)) >
          quest_template.required_reputation_value_max) {
    return false;
  }

  const auto required_money = DecodeQuestMoneyRequirement(quest_template.reward_money);
  if (required_money > 0 && player->GetMoney() < static_cast<std::uint32_t>(required_money)) {
    return false;
  }

  for (int objective_index = 0; objective_index < ::openwow::game::kQuestObjectivesCount;
       ++objective_index) {
    const auto &objective = quest_template.npc_or_go_objectives[objective_index];
    if (objective.creature_or_go != 0 &&
        player_slot.counts[objective_index] < objective.required_count) {
      return false;
    }
  }

  for (const auto &item_objective : quest_template.item_objectives) {
    if (item_objective.item_id != 0 &&
        session.inventory_replica().GetItemCount(item_objective.item_id) <
            item_objective.required_count) {
      return false;
    }
  }

  return player_slot.counts[0] >= quest_template.required_player_kills;
}
static std::vector<std::uint32_t> BuildVisibleWorldMapQuestIds(
    ::openwow::game::WorldSession &session,
    const ::openwow::ui::WorldMapSystem &world_map,
    const ::openwow::ui::WorldMapSystem::QuestPoiSelectionContext &selection) {
  auto &poi_data = ::openwow::game::QuestPOIData::Get();

  std::vector<std::uint32_t> visible_query_slot_quests;
  visible_query_slot_quests.reserve(::openwow::game::QuestPOIData::kMaxQuerySlots);
  std::unordered_set<std::uint32_t> visible_set;

  for (std::size_t slot = 1; slot <= ::openwow::game::QuestPOIData::kMaxQuerySlots; ++slot) {
    const auto quest_id = poi_data.GetQuestIdByQuerySlot(slot);
    if (quest_id == 0) {
      continue;
    }

    const auto objective_mask = BuildQuestPoiIncompleteObjectiveMask(session, quest_id);
    for (const auto &poi : poi_data.GetPOIsForQuest(quest_id)) {
      if (!QuestPoiPassesObjectiveMask(poi, objective_mask)) {
        continue;
      }
      if (!IsQuestPoiVisibleOnCurrentSelection(world_map, selection, poi)) {
        continue;
      }

      visible_query_slot_quests.push_back(quest_id);
      visible_set.insert(quest_id);
      break;
    }
  }

  std::vector<std::uint32_t> ordered_visible_quests;
  ordered_visible_quests.reserve(visible_query_slot_quests.size());
  for (const auto quest_id : visible_query_slot_quests) {
    if (IsQuestTurnInReady(session, quest_id)) {
      ordered_visible_quests.push_back(quest_id);
    }
  }

  std::unordered_set<std::uint32_t> watched_visible_quests;
  auto &quest_log = ::openwow::game::QuestLog::Get();
  for (std::size_t watch_index = 0; watch_index < quest_log.GetNumTracked(); ++watch_index) {
    const auto quest_id = quest_log.GetTrackedQuestId(watch_index);
    if (visible_set.count(quest_id) == 0 || IsQuestTurnInReady(session, quest_id)) {
      continue;
    }

    ordered_visible_quests.push_back(quest_id);
    watched_visible_quests.insert(quest_id);
  }

  for (const auto quest_id : visible_query_slot_quests) {
    if (!IsQuestTurnInReady(session, quest_id) &&
        watched_visible_quests.count(quest_id) == 0) {
      ordered_visible_quests.push_back(quest_id);
    }
  }

  return ordered_visible_quests;
}

static std::uint32_t FindWorldMapAreaIdForArea(const openwow::data::dbc::DbcLoader &dbc,
                                               std::uint32_t area_id) {
  if (area_id == 0) {
    return 0;
  }

  for (const auto &world_map_area : dbc.world_map_area()) {
    if (world_map_area.area_id == area_id) {
      return world_map_area.id;
    }
  }

  return 0;
}

static std::uint32_t
ResolveQuestHeaderWorldMapAreaId(const openwow::data::dbc::DbcLoader &dbc,
                                 const ::openwow::game::QuestTemplate &quest_template) {
  auto area_id =
      static_cast<std::uint32_t>(std::max(DecodeQuestSortKey(quest_template.zone_or_sort), 0));
  while (area_id != 0) {
    if (const auto world_map_area_id = FindWorldMapAreaIdForArea(dbc, area_id);
        world_map_area_id != 0) {
      return world_map_area_id;
    }

    const auto *area = dbc.area_table().LookupEntry(area_id);
    if (area == nullptr || area->parent_area == 0) {
      break;
    }
    area_id = area->parent_area;
  }

  return 0;
}

static QuestWorldMapAreaSelection ResolveQuestPoiWorldMapAreaSelection(
    const ::openwow::game::WorldSession &session,
    const openwow::data::dbc::DbcLoader &dbc,
    const ::openwow::ui::WorldMapSystem &world_map,
    const std::uint32_t quest_id, const bool turn_in_ready, const std::uint32_t objective_mask) {
  const auto *player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return {};
  }

  const auto selected_world_map_area_for_lua = world_map.GetCurrentMapAreaIdForLua();
  if (selected_world_map_area_for_lua <= 0) {
    return {};
  }

  const auto selected_world_map_area_id =
      static_cast<std::uint32_t>(selected_world_map_area_for_lua - 1);
  const auto *selected_world_map_area =
      dbc.world_map_area().LookupEntry(selected_world_map_area_id);
  if (selected_world_map_area == nullptr) {
    return {};
  }

  std::uint32_t selected_floor_id = 0;
  if (const auto dungeon_map_id = world_map.GetCurrentDungeonMapId(); dungeon_map_id > 0) {
    if (const auto *dungeon_map =
            dbc.dungeon_map().LookupEntry(static_cast<std::uint32_t>(dungeon_map_id));
        dungeon_map != nullptr) {
      selected_floor_id = dungeon_map->floor_index;
    }
  }

  QuestWorldMapAreaSelection first_resolved_candidate;
  QuestWorldMapAreaSelection best_sibling_candidate;
  auto best_sibling_floor_delta = std::numeric_limits<int>::max();
  auto best_sibling_distance_sq = std::numeric_limits<float>::max();

  for (const auto &poi : ::openwow::game::QuestPOIData::Get().GetPOIsForQuest(quest_id)) {
    if (turn_in_ready) {
      if (poi.objectiveIndex != -1) {
        continue;
      }
    } else {
      if (poi.objectiveIndex < 0 || poi.objectiveIndex >= 31 ||
          (objective_mask & (1u << poi.objectiveIndex)) == 0) {
        continue;
      }
    }

    if (poi.areaId == selected_world_map_area_id && poi.floorId == selected_floor_id) {
      return {poi.areaId, poi.floorId};
    }

    const auto *poi_world_map_area = dbc.world_map_area().LookupEntry(poi.areaId);
    if (poi_world_map_area == nullptr) {
      continue;
    }

    if (first_resolved_candidate.world_map_area_id == 0) {
      first_resolved_candidate = {poi.areaId, poi.floorId};
    }

    if (poi_world_map_area->parent_world_map_id != selected_world_map_area->parent_world_map_id) {
      continue;
    }

    const auto centroid = ::openwow::game::QuestPOIData::Get().GetCentroid(poi);
    const auto dx = centroid.x - player->GetX();
    const auto dy = centroid.y - player->GetY();
    const auto distance_sq = dx * dx + dy * dy;
    const auto floor_delta =
        std::abs(static_cast<int>(selected_floor_id) - static_cast<int>(poi.floorId));
    if (best_sibling_candidate.world_map_area_id == 0 || floor_delta < best_sibling_floor_delta ||
        (floor_delta == best_sibling_floor_delta && distance_sq < best_sibling_distance_sq)) {
      best_sibling_candidate = {poi.areaId, poi.floorId};
      best_sibling_floor_delta = floor_delta;
      best_sibling_distance_sq = distance_sq;
    }
  }

  if (best_sibling_candidate.world_map_area_id != 0) {
    return best_sibling_candidate;
  }

  return first_resolved_candidate;
}

static const ::openwow::game::QuestTemplate *
GetQuestLogTemplateByInterleavedIndex(::openwow::game::WorldSession &session,
                                      int interleaved_1based) {
  const auto quest_id = ResolveQuestIdFromInterleavedIndex(session, interleaved_1based);
  if (quest_id == 0) {
    return nullptr;
  }

  return session.quests().GetTemplate(quest_id);
}

static std::int32_t DecodeQuestLogRequiredMoney(std::uint32_t raw_reward_money) {
  return DecodeQuestMoneyRequirement(raw_reward_money);
}

static bool IsQuestNonTrivialForPlayerLevel(const std::uint32_t player_level,
                                            const std::int32_t quest_level) {
  if (quest_level == -1) {
    return true;
  }

  return static_cast<std::int32_t>(player_level) <=
         quest_level +
             static_cast<std::int32_t>(GetQuestTrivialLevelOffsetForPlayerLevel(player_level));
}

static bool IsRecruitAFriendQuestBonusEligible(const CGPlayer_C &player,
                                               const CGUnit_C &party_member,
                                               const QuestTemplate &quest_template) {
  if ((party_member.State().GetUnitFlags() & kUnitFlagReferAFriendLinked) == 0) {
    return false;
  }

  if (player.GetSquaredDistanceToPosition(party_member.GetPosition()) >=
      kRecruitAFriendQuestDistanceSq) {
    return false;
  }

  if (quest_template.quest_level == -1) {
    return true;
  }

  const auto player_level = player.State().GetLevel();
  const auto party_member_level = party_member.State().GetLevel();
  if (!IsQuestNonTrivialForPlayerLevel(player_level, quest_template.quest_level) ||
      !IsQuestNonTrivialForPlayerLevel(party_member_level, quest_template.quest_level)) {
    return false;
  }

  return player_level >= quest_template.min_level && party_member_level >= quest_template.min_level;
}

static float GetQuestXpAuraMultiplier(const WorldSession &session, const CGPlayer_C &player,
                                      const openwow::data::dbc::DbcLoader &dbc) {
  float multiplier = 1.0f;
  const auto &auras = session.aura().GetAuras(player.GetGuid().GetRawValue());
  for (const auto &aura : auras) {
    const auto *spell = dbc.spell().LookupEntry(aura.spell_id);
    if (spell == nullptr) {
      continue;
    }

    for (std::size_t effect_index = 0; effect_index < spell->effect_apply_aura.size();
         ++effect_index) {
      if (spell->effect_apply_aura[effect_index] != kSpellAuraXpGainPct) {
        continue;
      }

      multiplier += static_cast<float>(spell->effect_base_points[effect_index] + 1) * 0.01f;
    }
  }

  return multiplier;
}

inline constexpr std::uint32_t kQuestItemBonding = 4;

static const ::openwow::game::QuestTemplate *
GetAbandonQuestTemplate(::openwow::game::WorldSession &session) {
  if (s_abandon_quest_id == 0) {
    return nullptr;
  }

  return session.quests().GetTemplate(s_abandon_quest_id);
}

static void AppendUniqueQuestItemId(std::vector<std::uint32_t> *item_ids, std::uint32_t item_id) {
  if (!item_ids || item_id == 0) {
    return;
  }
  if (std::find(item_ids->begin(), item_ids->end(), item_id) == item_ids->end()) {
    item_ids->push_back(item_id);
  }
}

static void RemoveQuestItemId(std::vector<std::uint32_t> *item_ids, std::uint32_t item_id) {
  if (!item_ids || item_id == 0) {
    return;
  }
  item_ids->erase(std::remove(item_ids->begin(), item_ids->end(), item_id), item_ids->end());
}

static void CollectAbandonQuestItemIds(::openwow::game::WorldSession &session,
                                       const ::openwow::game::QuestTemplate &tmpl,
                                       std::vector<std::uint32_t> *item_ids) {
  if (!item_ids) {
    return;
  }

  for (const auto &objective : tmpl.item_objectives) {
    if (objective.item_id == 0 ||
        session.inventory_replica().GetItemCount(objective.item_id) == 0) {
      continue;
    }

    const auto *item = session.query_cache().GetItemTemplate(objective.item_id);
    if (!item || item->bonding != kQuestItemBonding) {
      continue;
    }
    AppendUniqueQuestItemId(item_ids, objective.item_id);
  }

  if (tmpl.src_item_id != 0 &&
      session.inventory_replica().GetItemCount(tmpl.src_item_id) > 0) {
    const auto *source_item = session.query_cache().GetItemTemplate(tmpl.src_item_id);
    if (source_item && source_item->bonding == kQuestItemBonding) {
      AppendUniqueQuestItemId(item_ids, tmpl.src_item_id);
    }
  }

  for (const auto &drop_item : tmpl.item_drop_objectives) {
    if (drop_item.item_id == 0 ||
        session.inventory_replica().GetItemCount(drop_item.item_id) == 0) {
      continue;
    }
    AppendUniqueQuestItemId(item_ids, drop_item.item_id);
  }
}

static void RemoveSharedQuestItemIds(::openwow::game::WorldSession &session,
                                     std::vector<std::uint32_t> *item_ids) {
  if (!item_ids || item_ids->empty()) {
    return;
  }

  for (const auto &entry : session.quests().quest_log()) {
    if (entry.quest_id == 0 || entry.quest_id == s_abandon_quest_id) {
      continue;
    }

    const auto *other_template = session.quests().GetTemplate(entry.quest_id);
    if (!other_template) {
      continue;
    }

    for (const auto &objective : other_template->item_objectives) {
      RemoveQuestItemId(item_ids, objective.item_id);
    }
    RemoveQuestItemId(item_ids, other_template->src_item_id);
    if (item_ids->empty()) {
      return;
    }
  }
}

static std::string FormatQuestLeaderboardText(const char *global_string_key,
                                              const std::string &objective_name,
                                              std::uint32_t progress,
                                              std::uint32_t required_count) {
  auto &localization = ::openwow::game::Localization::Get();
  return localization.FormatString(
      localization.GetString(global_string_key),
      {objective_name, std::to_string(progress), std::to_string(required_count)});
}

static bool BuildNpcOrGoLeaderboardLine(::openwow::game::WorldSession &session,
                                        const ::openwow::game::CGPlayer_C &player,
                                        std::uint32_t quest_id, int objective_slot,
                                        QuestLeaderboardLine *out,
                                        bool use_object_format_for_explicit_text = false) {
  if (!out || objective_slot < 0 || objective_slot >= ::openwow::game::kQuestObjectivesCount) {
    return false;
  }

  const auto *tmpl = GetOrRequestQuestTemplate(session, quest_id);
  if (!tmpl)
    return false;

  const auto &objective = tmpl->npc_or_go_objectives[objective_slot];
  if (objective.creature_or_go == 0)
    return false;

  const int quest_slot = FindPlayerQuestSlotById(player, quest_id);
  if (quest_slot < 0)
    return false;

  const auto player_quest = player.GetQuestLog(static_cast<std::uint8_t>(quest_slot));
  const std::uint32_t progress = player_quest.counts[objective_slot];

  std::string label = objective.text;
  const bool has_explicit_text = !label.empty();
  if (label.empty()) {
    if (objective.creature_or_go >= 0) {
      label =
          ResolveCreatureObjectiveName(session, objective.creature_or_go, objective.required_count);
    } else {
      label = ResolveGameObjectObjectiveName(session, objective.creature_or_go,
                                             objective.required_count);
    }
  }

  const char *format_key =
      objective.creature_or_go >= 0 ? "QUEST_MONSTERS_KILLED" : "QUEST_OBJECTS_FOUND";
  if (use_object_format_for_explicit_text && has_explicit_text) {
    format_key = "QUEST_OBJECTS_FOUND";
  }

  out->text = FormatQuestLeaderboardText(format_key, label, progress, objective.required_count);
  out->type = objective.creature_or_go >= 0 ? "monster" : "object";
  out->finished = progress >= objective.required_count;
  return true;
}

static bool BuildItemLeaderboardLine(::openwow::game::WorldSession &session,
                                     const ::openwow::game::CGPlayer_C &player,
                                     std::uint32_t quest_id, int item_slot,
                                     QuestLeaderboardLine *out) {
  if (!out || item_slot < 0 || item_slot >= ::openwow::game::kQuestItemObjectivesCount) {
    return false;
  }

  const auto *tmpl = GetOrRequestQuestTemplate(session, quest_id);
  if (!tmpl)
    return false;

  const auto &objective = tmpl->item_objectives[item_slot];
  if (objective.item_id == 0 || objective.required_count == 0)
    return false;
  if (objective.item_id == tmpl->src_item_id && objective.required_count <= 1) {
    return false;
  }

  const int quest_slot = FindPlayerQuestSlotById(player, quest_id);
  if (quest_slot < 0)
    return false;

  const std::uint32_t item_count =
      session.inventory_replica().GetItemCount(objective.item_id);
  const std::uint32_t progress = std::min(item_count, objective.required_count);
  const std::string item_name = ResolveItemObjectiveName(session, objective.item_id);

  out->text = FormatQuestLeaderboardText("QUEST_ITEMS_NEEDED", item_name, progress,
                                         objective.required_count);
  out->type = "item";
  out->finished = progress >= objective.required_count;
  return true;
}

static bool BuildItemDropLeaderboardLine(::openwow::game::WorldSession &session,
                                         std::uint32_t quest_id, int drop_slot,
                                         QuestLeaderboardLine *out,
                                         const bool include_progress = true) {
  if (!out || drop_slot < 0 || drop_slot >= ::openwow::game::kQuestRewardsCount) {
    return false;
  }

  const auto *tmpl = GetOrRequestQuestTemplate(session, quest_id);
  if (!tmpl)
    return false;

  const auto &objective = tmpl->item_drop_objectives[drop_slot];
  if (objective.item_id == 0)
    return false;

  const std::uint32_t item_count =
      session.inventory_replica().GetItemCount(objective.item_id);
  const std::uint32_t progress = std::min(item_count, objective.required_count);
  const std::string item_name = ResolveItemObjectiveName(session, objective.item_id);

  if (include_progress) {
    out->text =
        FormatQuestLeaderboardText("QUEST_ITEMS_NEEDED", item_name, progress, objective.required_count);
  } else {
    auto &localization = ::openwow::game::Localization::Get();
    out->text = localization.FormatString(
        localization.GetString("QUEST_ITEMS_NEEDED_NOPROGRESS"),
        {item_name, std::to_string(objective.required_count)});
  }
  out->type = "item";
  out->finished = include_progress && progress >= objective.required_count;
  return true;
}

static bool BuildQuestLogItemDropLine(::openwow::game::WorldSession &session,
                                      std::uint32_t quest_id, int objective_index,
                                      QuestLeaderboardLine *out) {
  if (!out || objective_index < 0) {
    return false;
  }

  const auto *player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return false;
  }

  if (quest_id == 0) {
    quest_id = s_selected_quest_id;
  }
  if (quest_id == 0 || FindPlayerQuestSlotById(*player, quest_id) < 0) {
    return false;
  }

  const auto *tmpl = GetOrRequestQuestTemplate(session, quest_id);
  if (tmpl == nullptr) {
    return false;
  }

  int visible_drop_index = -1;
  for (int drop_slot = 0; drop_slot < ::openwow::game::kQuestRewardsCount; ++drop_slot) {
    if (tmpl->item_drop_objectives[drop_slot].item_id == 0) {
      continue;
    }

    if (++visible_drop_index != objective_index) {
      continue;
    }

    return BuildItemDropLeaderboardLine(session, quest_id, drop_slot, out);
  }

  return false;
}

static int PushLeaderboardLine(lua_State *L, const QuestLeaderboardLine &line) {
  lua_pushstring(L, line.text.c_str());
  lua_pushstring(L, line.type.c_str());
  lua_pushwowbool(L, line.finished);
  return 3;
}

int LuaGetNumQuestLogEntries(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }

  const auto view = BuildQuestLogView(*session);
  lua_pushnumber(L, static_cast<lua_Number>(view.visible_count));
  lua_pushnumber(L, static_cast<lua_Number>(view.quest_count));
  return 2;
}

int LuaGetQuestLogTitle(lua_State *L) {
  auto *session = GetWorldSession(L);
  int index = static_cast<int>(lua_tonumber(L, 1));

  if (!session) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 9;
  }

  const auto interleaved = BuildInterleavedQuestLog(*session);
  if (index < 1 || index > static_cast<int>(interleaved.size())) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 9;
  }

  const auto &ie = interleaved[static_cast<std::size_t>(index - 1)];

  if (ie.is_header) {

    if (ie.header_name.empty()) {
      lua_pushnil(L);
    } else {
      lua_pushstring(L, ie.header_name.c_str());
    }
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1.0);
    lua_pushwowbool(L, ie.collapsed);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 9;
  }

  const auto &log = session->quests().quest_log();
  const auto &entry = log[ie.quest_log_index];
  const auto *tmpl = GetOrRequestQuestTemplate(*session, entry.quest_id);

  if (tmpl && !tmpl->title.empty()) {
    lua_pushstring(L, tmpl->title.c_str());
  } else {
    lua_pushnil(L);
  }

  const auto *player = session->objects().GetLocalPlayerTyped();
  lua_pushnumber(
      L, tmpl ? static_cast<lua_Number>(ResolveQuestTemplateDisplayLevel(*tmpl, player)) : 0);

  {
    const char *quest_tag = nullptr;
    if (tmpl && tmpl->type != 0) {
      const auto *dbc = session->GetDbcLoader();
      if (!dbc)
        dbc = GetDbcLoader(L);
      if (dbc) {
        if (const auto *info = dbc->quest_info().LookupEntry(tmpl->type);
            info != nullptr && !info->name.empty()) {
          quest_tag = info->name.data();
        }
      }
    }
    if (quest_tag)
      lua_pushstring(L, quest_tag);
    else
      lua_pushnil(L);
  }

  lua_pushnumber(L, tmpl ? static_cast<lua_Number>(tmpl->suggested_players) : 0);

  lua_pushnil(L);

  lua_pushnil(L);

  if (entry.status == ::openwow::game::QuestStatus::kFailed) {
    lua_pushnumber(L, -1.0);
  } else if (entry.status == ::openwow::game::QuestStatus::kComplete ||
             entry.status == ::openwow::game::QuestStatus::kRewarded) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  if (tmpl && HasFlag(tmpl->flags, ::openwow::game::QuestFlags::kDaily))
    lua_pushnumber(L, 1.0);
  else
    lua_pushnil(L);

  lua_pushnumber(L, static_cast<lua_Number>(entry.quest_id));

  return 9;
}

int LuaGetQuestLogLeaderBoard(lua_State *L) {
  auto *session = GetWorldSession(L);
  int obj_index = static_cast<int>(lua_tonumber(L, 1));
  int quest_interleaved = lua_gettop(L) >= 2 ? static_cast<int>(lua_tonumber(L, 2)) : 0;

  if (!session || obj_index < 1) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  std::uint32_t quest_id = 0;
  if (quest_interleaved >= 1) {
    quest_id = ResolveQuestIdFromInterleavedIndex(*session, quest_interleaved);
  } else {
    quest_id = s_selected_quest_id;
  }

  if (quest_id == 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  QuestRequirementQueryCallbacks callbacks;
  callbacks.on_quest_template_query =
      BuildQuestRequirementQueryCallback("Invalid quest log entry");
  callbacks.on_gameobject_template_query =
      BuildQuestRequirementQueryCallback("Invalid object in quest");
  callbacks.on_creature_template_query =
      BuildQuestRequirementQueryCallback("Invalid creature in quest");
  callbacks.on_item_template_query = BuildQuestRequirementQueryCallback("");

  QuestLeaderboardLine line;
  if (!BuildQuestLeaderboardLine(*session, quest_id, obj_index - 1, true, callbacks, &line)) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }
  return PushLeaderboardLine(L, line);
}

int LuaSelectQuestLogEntry(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SelectQuestLogEntry(index)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  s_selected_quest_log_index = 0;
  s_selected_quest_id = 0;
  ::openwow::game::QuestLog::Get().SelectQuest(0);
  auto *minimap = MinimapStateOrNull(L);
  if (minimap != nullptr) {
    ClearSelectedQuestGuidePointOfInterest(*minimap);
  }

  auto *session = GetWorldSession(L);
  if (session) {
    const auto quest_id = ResolveQuestIdFromInterleavedIndex(*session, index);
    if (quest_id != 0) {
      s_selected_quest_log_index = index;
      s_selected_quest_id = quest_id;
      ::openwow::game::QuestLog::Get().SelectQuest(quest_id);
      if (minimap != nullptr) {
        RefreshSelectedQuestGuidePointOfInterest(*session, *minimap);
      }
    }
  }
  return 0;
}

int LuaGetQuestLogQuestText(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !SyncSelectedQuestLogIndex(*session)) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  int real_idx = ResolveInterleavedToQuestIndex(*session, s_selected_quest_log_index);
  if (real_idx < 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto &log = session->quests().quest_log();
  const auto &entry = log[static_cast<std::size_t>(real_idx)];
  const auto *tmpl = GetOrRequestQuestTemplate(*session, entry.quest_id);
  if (!tmpl) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto details = ExpandQuestDialogText(*session, tmpl->details, false);
  const auto objectives = ExpandQuestDialogText(*session, tmpl->objectives, false);
  lua_pushstring(L, details.c_str());
  lua_pushstring(L, objectives.c_str());
  return 2;
}

int LuaGetQuestLogRewardInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GetQuestLogRewardInfo(index)");
  }

  const int item_index = static_cast<int>(lua_tonumber(L, 1));
  const auto *tmpl = GetSelectedQuestLogTemplate(GetWorldSession(L));
  if (tmpl == nullptr) {
    return PushEmptyQuestLogRewardInfo(L);
  }

  return PushQuestLogItemInfoFromTemplate(L, tmpl->reward_items, item_index);
}

int LuaGetQuestLogRewardMoney(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || s_selected_quest_id == 0) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto *tmpl = GetSelectedQuestLogTemplate(session);
  const auto *player = session->objects().GetLocalPlayerTyped();
  const auto reward_money = tmpl != nullptr ? GetQuestLogRewardMoneyValue(*tmpl, player) : 0u;
  lua_pushnumber(L, static_cast<lua_Number>(reward_money));
  return 1;
}

int LuaGetQuestLogRewardHonor(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr && session != nullptr) {
    dbc = session->GetDbcLoader();
  }

  const auto *player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  const auto *quest = session != nullptr ? GetSelectedQuestLogTemplate(session) : nullptr;
  if (player == nullptr || quest == nullptr) {
    FrameScript_PushNumberFromInt(L, 0);
    return 1;
  }

  const auto fixed_honor = static_cast<std::int32_t>(quest->rew_honor_addition);
  if (fixed_honor < 1 && quest->rew_honor_multiplier <= 0.0f) {
    FrameScript_PushNumberFromInt(L, 0);
    return 1;
  }

  float scaled_honor = 0.0f;
  if (dbc != nullptr) {
    if (const auto *contribution =
            dbc->team_contribution_points().LookupEntryByRowIndex(
                static_cast<int>(player->State().GetLevel()) - 1)) {

      scaled_honor = quest->rew_honor_multiplier * contribution->data;
    }
  }

  const auto rounded_scaled_honor =
      static_cast<std::int32_t>(std::lrintf(scaled_honor / 10.0f));
  const auto total_honor = static_cast<std::int32_t>(
      static_cast<std::uint32_t>(fixed_honor) +
      static_cast<std::uint32_t>(rounded_scaled_honor));
  FrameScript_PushNumberFromInt(L, total_honor);
  return 1;
}

int LuaGetQuestLogRewardXP(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr && session != nullptr) {
    dbc = session->GetDbcLoader();
  }

  if (!session || !dbc || s_selected_quest_id == 0) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto *player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto *tmpl = GetSelectedQuestLogTemplate(session);
  if (tmpl == nullptr || player->State().GetLevel() >= player->GetMaxLevel()) {
    lua_pushnumber(L, 0);
    return 1;
  }

  std::uint32_t reward_xp =
      QuestXPCalc::CalculateRewardXP(*dbc, tmpl->quest_level, player->State().GetLevel(), tmpl->xp_id);
  if (reward_xp != 0 && player->State().GetLevel() < kRecruitAFriendQuestBonusMaxLevel) {
    auto &group_system = ::openwow::game::GroupSystem::Get();
    for (std::uint32_t slot = 0; slot < 4; ++slot) {
      const auto party_guid = group_system.GetTrackedPartyMemberGuid(slot);
      if (party_guid == 0) {
        continue;
      }

      const auto *party_member =
          session->objects().GetUnit(::openwow::game::ObjectGuid(party_guid));
      if (party_member == nullptr) {
        continue;
      }

      if (!IsRecruitAFriendQuestBonusEligible(*player, *party_member, *tmpl)) {
        continue;
      }

      reward_xp *= 3;
    }
  }

  const auto multiplier = GetQuestXpAuraMultiplier(*session, *player, *dbc);
  const auto scaled_reward =
      static_cast<float>(static_cast<double>(reward_xp) * static_cast<double>(multiplier));

  lua_pushnumber(L, static_cast<lua_Number>(
                        std::round(static_cast<double>(scaled_reward))));
  return 1;
}

int LuaGetQuestTimers(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->objects().GetLocalPlayer()) {
    return 0;
  }

  bool fire_quest_watch_update = false;
  int return_count = 0;
  std::vector<int> processed_player_slots;

  auto process_timed_quest = [&](const std::optional<TimedQuestView> &timed_quest) {
    if (!timed_quest || IsQuestTimerSuppressed(*timed_quest)) {
      return;
    }

    processed_player_slots.push_back(timed_quest->player_slot);

    if (timed_quest->quest_log_entry && timed_quest->quest_log_entry->timer_expiration_reported) {
      return;
    }

    if (timed_quest->seconds_left < 0) {
      if (timed_quest->quest_log_entry &&
          !IsQuestTimerFailureSuppressed(*session, timed_quest->quest_id)) {
        timed_quest->quest_log_entry->timer_expiration_reported = true;
        const auto *quest_template = session->quests().GetOrRequestTemplate(
            timed_quest->quest_id,
            {.dedupe_callbacks = false,
             .callback = BuildTimedQuestFailureMessageCallback(
                 *session, timed_quest->player_slot, timed_quest->quest_id)});
        if (quest_template != nullptr) {
          DisplayTimedQuestFailureMessage(*quest_template);
        }
        fire_quest_watch_update = true;
      }
      return;
    }

    lua_pushnumber(L, static_cast<lua_Number>(timed_quest->seconds_left));
    ++return_count;
  };

  const auto interleaved = BuildInterleavedQuestLog(*session);
  for (std::size_t i = 0; i < interleaved.size(); ++i) {
    if (interleaved[i].is_header) {
      continue;
    }

    process_timed_quest(BuildTimedQuestView(*session, static_cast<int>(i + 1)));
  }

  for (int slot = 0; slot < ::openwow::game::kMaxQuestLogEntries; ++slot) {
    if (std::find(processed_player_slots.begin(), processed_player_slots.end(), slot) !=
        processed_player_slots.end()) {
      continue;
    }

    process_timed_quest(BuildTimedQuestViewFromPlayerSlot(*session, slot, 0));
  }

  if (fire_quest_watch_update) {
    ::openwow::game::QuestLog::Get().SignalWatchUpdate();
  }

  return return_count;
}

int LuaGetQuestLogTimeLeft(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !SyncSelectedQuestLogIndex(*session)) {
    lua_pushnil(L);
    return 1;
  }
  int real_idx = ResolveInterleavedToQuestIndex(*session, s_selected_quest_log_index);
  if (real_idx < 0) {
    lua_pushnil(L);
    return 1;
  }

  const auto timed_quest = BuildTimedQuestView(*session, s_selected_quest_log_index);
  if (!timed_quest || IsQuestTimerSuppressed(*timed_quest) ||
      GetQuestTimerLocalOffsetSeconds(*session) == 0) {
    lua_pushnil(L);
    return 1;
  }

  const int seconds_left = std::max(0, timed_quest->seconds_left);
  lua_pushnumber(L, static_cast<lua_Number>(seconds_left));
  return 1;
}

int LuaGetQuestLogRequiredMoney(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const ::openwow::game::QuestTemplate *tmpl = nullptr;
  if (lua_isnumber(L, 1)) {
    const auto visible_index = static_cast<int>(lua_tonumber(L, 1));
    tmpl = GetQuestLogTemplateByInterleavedIndex(*session, visible_index);
  } else if (s_selected_quest_id != 0) {
    tmpl = session->quests().GetTemplate(s_selected_quest_id);
  }

  const auto money = tmpl ? DecodeQuestLogRequiredMoney(tmpl->reward_money) : 0;
  lua_pushnumber(L, static_cast<lua_Number>(money));
  return 1;
}

int LuaGetQuestLogGroupNum(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto quest_id = session != nullptr ? ResolveSelectedVisibleQuestId(*session) : 0;
  if (session != nullptr && quest_id != 0) {
    if (const auto *tmpl = GetOrRequestQuestTemplate(*session, quest_id)) {
      lua_pushnumber(L, static_cast<lua_Number>(tmpl->suggested_players));
      return 1;
    }
  }

  lua_pushnumber(L, 0);
  return 1;
}

int LuaAcceptQuest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->quests().has_active_details() ||
      session->quests().is_dialog_action_pending() || !HasQuestFrameActivePlayer(*session)) {
    return 0;
  }
  const auto &d = session->quests().active_details();
  const auto dialog = ::openwow::game::GetActiveQuestDialogCloseState(session->quests());
  session->interaction().SendQuestGiverAcceptQuest(ResolveQuestAcceptGuid(d), d.quest_id,
                                                   d.accept_packet_value);
  session->quests().MarkQuestDetailsAcceptSubmitted();
  ::openwow::game::CloseQuestDialogLikeIda58CA70(
      *session, dialog, false,
      false);
  return 0;
}

int LuaDeclineQuest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || session->quests().is_dialog_action_pending()) {
    return 0;
  }

  const auto dialog = ::openwow::game::GetActiveQuestDialogCloseState(session->quests());
  if (!dialog.is_open) {
    return 0;
  }

  if (dialog.interaction_guid.GetRawValue() == 0) {
    return 0;
  }

  const auto *object = session->objects().Get(dialog.interaction_guid);
  if (object == nullptr) {
    return 0;
  }

  if (object->IsUnit()) {
    const auto *unit = static_cast<const ::openwow::game::CGUnit_C *>(object);
    if ((unit->State().GetNpcFlags() & ::openwow::game::UNIT_NPC_FLAG_GOSSIP) != 0 &&
        session->objects().GetLocalPlayer() != nullptr) {
      session->interaction().SendGossipHello(dialog.interaction_guid.GetRawValue());
      session->quests().MarkDialogActionPending();
      return 0;
    }
  }

  if (object->IsPlayer() || object->IsItem() || dialog.close_on_decline) {
    ::openwow::game::CloseQuestDialogLikeIda58CA70(*session, dialog, false, true);
    return 0;
  }

  if (object->IsGameObject()) {
    const auto *game_object = session->objects().GetGameObject(dialog.interaction_guid);
    if (game_object != nullptr) {
      game_object->Interact(session);
      session->quests().MarkDialogActionPending();
    }
    return 0;
  }

  session->interaction().SendQuestGiverHello(dialog.interaction_guid.GetRawValue());
  session->quests().MarkDialogActionPending();
  return 0;
}

int LuaCompleteQuest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->quests().has_active_request() ||
      session->quests().is_dialog_action_pending() || !HasQuestFrameActivePlayer(*session)) {
    return 0;
  }
  const auto &r = session->quests().active_request();
  TriggerQuestFrameProgressTutorial();
  session->interaction().SendQuestGiverRequestReward(r.npc_guid.GetRawValue(), r.quest_id);
  session->quests().MarkDialogActionPending();
  return 0;
}

int LuaGetQuestReward(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->quests().has_active_reward() ||
      session->quests().is_dialog_action_pending() || !HasQuestFrameActivePlayer(*session)) {
    return 0;
  }

  int choice_index = -1;
  if (lua_isnumber(L, 1)) {
    choice_index = static_cast<int>(lua_tonumber(L, 1)) - 1;
  }

  const auto *choice_rewards = GetSharedQuestRewardPreviewChoiceRewards(session->quests());
  const auto choice_count = static_cast<int>(CountContiguousQuestPreviewItems(choice_rewards));
  if (choice_count > 0 && (choice_index < 0 || choice_index >= choice_count)) {
    return luaL_error(L, "Invalid reward choice in GetQuestReward([choice])");
  }

  const auto &r = session->quests().active_reward();
  const std::uint32_t reward_index =
      choice_index <= 0 ? 0u : static_cast<std::uint32_t>(choice_index);
  session->quests().RecordPendingRewardSelection(r.quest_id, reward_index);
  TriggerQuestFrameProgressTutorial();
  session->interaction().SendQuestGiverChooseReward(r.npc_guid.GetRawValue(), r.quest_id,
                                                    reward_index);
  session->quests().MarkDialogActionPending();
  return 0;
}

int LuaAbandonQuest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || s_abandon_quest_id == 0) {
    return 0;
  }

  const int quest_slot = FindAbandonQuestSlot(*session, s_abandon_quest_id);
  if (quest_slot >= 0) {
    session->interaction().SendQuestLogRemoveQuest(static_cast<std::uint8_t>(quest_slot));
  }
  return 0;
}

int LuaSetAbandonQuest(lua_State *L) {
  (void)L;
  s_abandon_quest_id = s_selected_quest_id;
  return 0;
}

int LuaGetTitleText(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto title = session && session->quests().dialog_text().has_title_text
                         ? session->quests().dialog_text().title_text
                         : std::string();
  lua_pushstring(L, title.c_str());
  return 1;
}

int LuaGetObjectiveText(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto objective = session && session->quests().dialog_text().has_objective_text
                             ? session->quests().dialog_text().objective_text
                             : std::string();
  lua_pushstring(L, objective.c_str());
  return 1;
}

int LuaGetProgressText(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto progress = session && session->quests().dialog_text().has_progress_text
                            ? session->quests().dialog_text().progress_text
                            : std::string();
  lua_pushstring(L, progress.c_str());
  return 1;
}

int LuaGetRewardText(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto reward = session && session->quests().dialog_text().has_reward_text
                          ? session->quests().dialog_text().reward_text
                          : std::string();
  lua_pushstring(L, reward.c_str());
  return 1;
}

int LuaGetGreetingText(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto greeting = session && session->quests().dialog_text().has_greeting_text
                            ? session->quests().dialog_text().greeting_text
                            : std::string();
  lua_pushstring(L, greeting.c_str());
  return 1;
}

int LuaGetNumQuestChoices(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *rewards =
      session ? GetSharedQuestRewardPreviewChoiceRewards(session->quests()) : nullptr;
  lua_pushnumber(L, static_cast<lua_Number>(CountContiguousQuestPreviewItems(rewards)));
  return 1;
}

int LuaGetNumQuestRewards(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *rewards = session ? GetSharedQuestRewardPreviewRewards(session->quests()) : nullptr;
  lua_pushnumber(L, static_cast<lua_Number>(CountContiguousQuestPreviewItems(rewards)));
  return 1;
}

int LuaGetQuestItemInfo(lua_State *L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Invalid quest item in GetQuestItemInfo(\"type\", index)");
  }

  auto *session = GetWorldSession(L);
  const char *item_type = lua_tostring(L, 1);
  const int index = static_cast<int>(std::trunc(lua_tonumber(L, 2)));
  const auto slot = ResolveQuestPreviewItemSlot(
      session, ParseQuestPreviewItemType(item_type != nullptr ? item_type : ""), index);
  if (!slot.is_valid) {
    return luaL_error(L, "Invalid quest item in GetQuestItemInfo(\"type\", index)");
  }

  std::string item_name;

  std::int32_t item_quality = static_cast<std::int32_t>(::openwow::game::ItemQuality::Poor);
  bool item_usable = true;

  if (slot.item.item_id != 0) {
    if (session != nullptr) {
      if (const auto *item_template =
              GetOrRequestQuestPreviewItemTemplate(session, slot.item.item_id);
          item_template != nullptr) {
        item_name = item_template->name;

        item_quality =
            item_template->inventory_type != ::openwow::game::InventoryType::NonEquip
                ? static_cast<std::int32_t>(item_template->quality)
                : -1;
        item_usable =
            LocalPlayerMeetsItemTemplateRequirements(
                session,
                ::openwow::game::BuildItemUseRequirementView(*item_template));
      }
    }
  }

  const auto texture_path = ResolveQuestPreviewTexturePath(L, slot.item.display_info_id);
  lua_pushstring(L, item_name.c_str());
  lua_pushstring(L, texture_path.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(slot.item.count));
  lua_pushnumber(L, static_cast<lua_Number>(item_quality));
  lua_pushwowbool(L, item_usable);
  return 5;
}

int LuaGetQuestItemLink(lua_State *L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetQuestItemLink(\"type\", index)");
  }

  auto *session = GetWorldSession(L);
  const char *item_type = lua_tostring(L, 1);
  const int index = static_cast<int>(lua_tonumber(L, 2));
  const auto item = GetQuestPreviewItem(session, item_type ? item_type : "", index);
  if (!session || !item.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  const auto link = BuildQuestPreviewItemLink(*session, *item);
  if (link.empty()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetQuestSpellLink(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto spell_id = session ? GetSharedQuestRewardPreviewSpellId(session->quests()) : 0;
  return PushSpellHyperlinkOrNil(L, spell_id);
}

int LuaGetNumQuestItems(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session && session->quests().has_active_request()) {
    lua_pushnumber(L, static_cast<lua_Number>(CountContiguousQuestPreviewItems(
                          &session->quests().active_request().required_items)));
  } else {
    lua_pushnumber(L, 0);
  }
  return 1;
}

int LuaGetNumQuestItemDrops(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  int count = 0;

  if (session && player) {
    const auto quest_id = ResolveQuestItemDropCountQuestId(L, *session);
    if (const auto *tmpl = GetOrRequestQuestTemplate(*session, quest_id)) {
      count = CountMissingQuestItemDropObjectives(
          session->inventory_replica(), *tmpl);
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaCloseQuest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {
    const auto dialog =
        ::openwow::game::GetActiveQuestDialogCloseState(session->quests());
    ::openwow::game::CloseQuestDialogLikeIda58CA70(
        *session, dialog, false, true);
  }
  return 0;
}

int LuaConfirmAcceptQuest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  const auto quest_id = session->quests().has_pending_confirm_accept()
                            ? session->quests().pending_confirm_accept().quest_id
                            : kDefaultConfirmAcceptQuestId;
  session->interaction().SendQuestConfirmAccept(quest_id);
  return 0;
}

int LuaSelectActiveQuest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: SelectActiveQuest(index)");
  }
  if (!session->objects().GetLocalPlayer())
    return 0;
  if (session->quests().is_dialog_action_pending()) {
    return 0;
  }

  const int index = GetQuestGreetingSelectionLuaIndex(L);
  const auto entry = GetQuestGreetingEntry(*session, index, true);
  if (entry) {
    session->interaction().SendQuestGiverCompleteQuest(entry->npc_guid, entry->quest_id);
    session->quests().MarkDialogActionPending();
  }
  return 0;
}

int LuaSelectAvailableQuest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: SelectAvailableQuest(index)");
  }
  if (!session->objects().GetLocalPlayer())
    return 0;
  if (session->quests().is_dialog_action_pending()) {
    return 0;
  }

  const int index = GetQuestGreetingSelectionLuaIndex(L);
  const auto entry = GetQuestGreetingEntry(*session, index, false);
  if (entry) {
    if (entry->quest_icon == 0) {
      session->interaction().SendQuestGiverCompleteQuest(entry->npc_guid, entry->quest_id);
    } else {
      session->interaction().SendQuestGiverQueryQuest(entry->npc_guid, entry->quest_id);
    }
    session->quests().MarkDialogActionPending();
  }
  return 0;
}

int LuaSortQuestWatches(lua_State *L) {
  const auto* const session = GetWorldSession(L);
  lua_pushwowbool(
      L, session != nullptr &&
             ::openwow::game::QuestLog::Get().SortQuestWatches(
                 session->objects()));
  return 1;
}

int LuaGetAbandonQuestItems(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (!session || !player) {
    return 0;
  }

  const auto *tmpl = GetAbandonQuestTemplate(*session);
  if (!tmpl) {
    return 0;
  }

  std::vector<std::uint32_t> item_ids;
  CollectAbandonQuestItemIds(*session, *tmpl, &item_ids);
  RemoveSharedQuestItemIds(*session, &item_ids);
  if (item_ids.empty()) {
    return 0;
  }

  std::string item_list;
  for (const auto item_id : item_ids) {
    const auto *item = session->query_cache().GetItemTemplate(item_id);
    if (!item || item->name.empty()) {
      continue;
    }
    if (!item_list.empty()) {
      item_list += ", ";
    }
    item_list += item->name;
  }

  if (item_list.empty()) {
    return 0;
  }

  lua_pushstring(L, item_list.c_str());
  return 1;
}

int LuaGetActiveLevel(lua_State *L) {
  return PushQuestGreetingLevel(L, true, "Usage: GetGetActiveLevel(index)");
}

int LuaGetActiveTitle(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GetActiveTitle(index)");
  }
  const int index = static_cast<int>(lua_tonumber(L, 1));
  if (!session) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto entry = GetQuestGreetingEntry(*session, index, true);
  if (entry) {
    lua_pushstring(L, entry->title->c_str());
    lua_pushwowbool(L, IsQuestTurnInReady(*session, entry->quest_id));
    return 2;
  }

  lua_pushnil(L);
  lua_pushnil(L);
  return 2;
}

int LuaGetAvailableLevel(lua_State *L) {
  return PushQuestGreetingLevel(L, false, "Usage: GetGetAvailableLevel(index)");
}

int LuaGetAvailableQuestInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GetAvailableQuestInfo(index)");
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const int index = static_cast<int>(lua_tonumber(L, 1));
  const auto entry = GetQuestGreetingEntry(*session, index, false);
  if (!entry) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  lua_pushwowbool(L, IsQuestLevelTrivial(*session, entry->quest_level));
  lua_pushwowbool(L, (entry->quest_flags & 0x1000u) != 0);
  lua_pushwowbool(L, entry->is_repeatable);
  return 3;
}

int LuaGetAvailableTitle(lua_State *L) {
  if (!lua_isnumber(L, 1))
    luaL_error(L, "Usage: GetAvailableTitle(index)");
  auto *session = GetWorldSession(L);
  const int index = static_cast<int>(lua_tonumber(L, 1));
  if (!session) {
    lua_pushnil(L);
    return 1;
  }

  const auto entry = GetQuestGreetingEntry(*session, index, false);
  if (entry) {
    lua_pushstring(L, entry->title->c_str());
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetNumActiveQuests(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(CountQuestGreetingEntries(*session, true)));
  return 1;
}

int LuaGetNumAvailableQuests(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(CountQuestGreetingEntries(*session, false)));
  return 1;
}

int LuaGetQuestBackgroundMaterial(lua_State *L) {
  const auto *session = GetWorldSession(L);
  if (session != nullptr && session->quests().has_active_details()) {
    if (const auto material_name = ResolveReadableObjectPageMaterialName(
            L, *session, session->quests().active_details().npc_guid);
        material_name.has_value()) {
      const char *value = material_name->empty() ? "" : material_name->data();
      lua_pushlstring(L, value, static_cast<std::size_t>(material_name->size()));
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetQuestGreenRange(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto *player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  const auto green_range =
      player != nullptr ? GetQuestGreenRangeForPlayerLevel(player->State().GetLevel()) : 0u;
  lua_pushnumber(L, static_cast<lua_Number>(green_range));
  return 1;
}

int LuaGetQuestLogCompletionText(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) return 0;

  std::uint32_t quest_id = 0;
  if (lua_isnumber(L, 1)) {
    const int index = static_cast<int>(lua_tonumber(L, 1));
    const auto interleaved = BuildInterleavedQuestLog(*session);
    const auto &log = session->quests().quest_log();
    if (index >= 1 && index <= static_cast<int>(interleaved.size())) {
      const auto &ie = interleaved[static_cast<std::size_t>(index - 1)];
      if (!ie.is_header && ie.quest_log_index < log.size()) {
        quest_id = log[ie.quest_log_index].quest_id;
      }
    }
  } else {
    quest_id = s_selected_quest_id;
  }

  const auto *tmpl = session->quests().GetTemplate(quest_id);
  if (!tmpl) return 0;

  const auto &raw_text =
      (HasFlag(tmpl->flags, ::openwow::game::QuestFlags::kObjText) ||
       tmpl->completed_text.empty())
          ? tmpl->details
          : tmpl->completed_text;

  const auto expanded = ExpandQuestDialogText(*session, raw_text, false);
  lua_pushstring(L, expanded.c_str());
  return 1;
}

static const ::openwow::game::QuestTemplate *
GetSelectedQuestLogTemplate(openwow::game::WorldSession *session) {
  if (!session || s_selected_quest_id == 0) {
    return nullptr;
  }

  return session->quests().GetTemplate(s_selected_quest_id);
}

int LuaProcessQuestLogRewardFactions(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session != nullptr && s_selected_quest_id != 0) {
    session->quests().BuildQuestLogRewardFactionPreview(s_selected_quest_id);
  }
  return 0;
}

int LuaGetNumQuestLogRewardFactions(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto count = session != nullptr ? session->quests().reward_faction_preview_count() : 0;
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaGetQuestLogRewardFactionInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: Script_GetQuestLogRewardFactionInfo(index)");
  }

  const auto *session = GetWorldSession(L);

  const double zero_based = lua_tonumber(L, 1) - 1.0;
  std::uint32_t index = 0;
  if (std::isnan(zero_based)) {
    index = 0x80000000u;
  } else if (zero_based >= 4294967295.0) {
    index = 0xFFFFFFFFu;
  } else if (zero_based > 0.0) {
    index = static_cast<std::uint32_t>(zero_based);
  }

  std::int32_t faction_id = 0;
  std::int32_t amount = 0;
  if (session != nullptr && static_cast<std::size_t>(index) <
          ::openwow::game::QuestManager::kMaxRewardFactionPreviewEntries) {
    if (const auto *entry =
            session->quests().reward_faction_preview_entry(static_cast<std::size_t>(index));
        entry != nullptr) {
      faction_id = entry->faction_id;
      amount = entry->amount;
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(faction_id));
  lua_pushnumber(L, static_cast<lua_Number>(amount));
  return 2;
}

static int PushEmptyQuestLogRewardInfo(lua_State *L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 1.0);
  lua_pushnumber(L, 0.0);
  lua_pushnil(L);
  return 5;
}

static bool LocalPlayerCanUseQuestLogItem(const ::openwow::game::WorldSession &session,
                                          const ::openwow::game::ItemTemplate &item_template) {
  return LocalPlayerMeetsItemTemplateRequirements(
      &session,
      ::openwow::game::BuildItemUseRequirementView(item_template));
}

template <std::size_t N>
static int PushQuestLogItemInfoFromTemplate(lua_State *L,
                                            const ::openwow::game::QuestRewardItem (&rewards)[N],
                                            int item_index) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || item_index < 1 || item_index > static_cast<int>(N)) {
    return PushEmptyQuestLogRewardInfo(L);
  }

  const auto &reward = rewards[static_cast<std::size_t>(item_index - 1)];
  if (reward.item_id == 0) {
    return PushEmptyQuestLogRewardInfo(L);
  }

  const auto *item_template = session->query_cache().GetOrRequestItemTemplate(reward.item_id);
  if (item_template == nullptr || item_template->name.empty()) {
    return PushEmptyQuestLogRewardInfo(L);
  }

  const auto texture_path = ResolveItemEntryIconTexturePathOrFallback(L, reward.item_id);
  lua_pushstring(L, item_template->name.c_str());
  lua_pushstring(L, texture_path.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(reward.count));
  lua_pushnumber(
      L, static_cast<lua_Number>(
             static_cast<std::uint8_t>(item_template->quality)));
  lua_pushwowbool(L, LocalPlayerCanUseQuestLogItem(*session, *item_template));
  return 5;
}

static std::uint32_t ClampQuestLogSignedRewardValue(std::uint32_t raw_value) {
  constexpr auto kMaxNonNegativeQuestRewardValue =
      static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
  return raw_value <= kMaxNonNegativeQuestRewardValue ? raw_value : 0;
}

template <typename Accessor>
static int PushSelectedQuestLogSignedRewardValue(lua_State *L, Accessor accessor) {
  const auto *tmpl = GetSelectedQuestLogTemplate(GetWorldSession(L));
  const auto value = tmpl ? ClampQuestLogSignedRewardValue(accessor(*tmpl)) : 0;
  lua_pushnumber(L, static_cast<lua_Number>(value));
  return 1;
}

std::uint32_t GetSelectedQuestLogItemId(openwow::game::WorldSession *session,
                                        std::string_view item_type,
                                        int one_based_index) {
  if (one_based_index <= 0) {
    return 0;
  }

  const auto *tmpl = GetSelectedQuestLogTemplate(session);
  if (!tmpl) {
    return 0;
  }

  const auto item_index = static_cast<std::size_t>(one_based_index - 1);
  if (EqualsAsciiCaseInsensitive(item_type, "reward")) {
    if (item_index >= ::openwow::game::kQuestRewardsCount) {
      return 0;
    }
    return tmpl->reward_items[item_index].item_id;
  }

  if (EqualsAsciiCaseInsensitive(item_type, "choice")) {
    if (item_index >= ::openwow::game::kQuestRewardChoicesCount) {
      return 0;
    }
    return tmpl->reward_choice_items[item_index].item_id;
  }

  return 0;
}

int LuaGetQuestLogItemLink(lua_State *L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetQuestLogItemLink(\"type\", index)");
  }

  auto *session = GetWorldSession(L);
  const char *item_type = lua_tostring(L, 1);
  const int index = static_cast<int>(lua_tonumber(L, 2));
  const auto item_id = GetSelectedQuestLogItemId(session, item_type ? item_type : "", index);
  if (!session || item_id == 0) {
    lua_pushnil(L);
    return 1;
  }

  const auto link = BuildQuestItemLink(*session, item_id);
  if (link.empty()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetQuestLogSpecialItemCooldown(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetQuestLogSpecialItemCooldown(index)");
  }

  auto *session = GetWorldSession(L);
  const auto quest_log_index = static_cast<std::uint32_t>(
      std::max(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)), 0));
  const auto *item = session != nullptr
                         ? ResolveQuestLogSpecialItem(*session, quest_log_index)
                         : nullptr;
  if (session == nullptr || item == nullptr) {
    return 0;
  }

  std::uint32_t duration_ms = 0;
  std::uint32_t start_time_ms = 0;
  bool enabled = false;
  if (const auto *live_item =
          session->objects().GetItem(::openwow::game::ObjectGuid(item->guid));
      live_item != nullptr) {
    (void)::openwow::game::FillItemCooldownByInventoryItem(
        *live_item, &duration_ms, &start_time_ms, &enabled);
  } else {
    (void)::openwow::game::FillItemCooldownByEntry(
        *session, item->entry, &duration_ms, &start_time_ms, &enabled);
  }

  lua_pushnumber(L, static_cast<lua_Number>(start_time_ms) / 1000.0);
  lua_pushnumber(L, static_cast<lua_Number>(duration_ms) / 1000.0);
  lua_pushnumber(L, enabled ? 1.0 : 0.0);
  return 3;
}

int LuaGetQuestLogSpellLink(lua_State *L) {
  const auto *tmpl = GetSelectedQuestLogTemplate(GetWorldSession(L));
  if (!tmpl) {
    lua_pushnil(L);
    return 1;
  }

  return PushSpellHyperlinkOrNil(L, tmpl->rew_spell);
}

int LuaGetQuestMoneyToGet(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto money_to_get = session ? GetQuestMoneyToGetValue(session->quests()) : 0;
  lua_pushnumber(L, static_cast<lua_Number>(money_to_get));
  return 1;
}

int LuaGetQuestText(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto quest_text = session && session->quests().dialog_text().has_quest_text
                              ? session->quests().dialog_text().quest_text
                              : std::string();
  lua_pushstring(L, quest_text.c_str());
  return 1;
}

int LuaGetQuestWorldMapAreaID(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GetQuestWorldMapAreaID(questID)");
  }

  auto *session = GetWorldSession(L);
  const auto *dbc =
      session != nullptr && session->GetDbcLoader() != nullptr
          ? session->GetDbcLoader()
          : GetDbcLoader(L);
  const auto quest_id =
      static_cast<std::uint32_t>(std::max(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)), 0));

  QuestWorldMapAreaSelection selection;
  if (session != nullptr && dbc != nullptr && quest_id != 0) {
    if (const auto player_slot = FindLocalPlayerQuestLogSlot(*session, quest_id);
        player_slot.has_value() && (player_slot->state & 0x02u) == 0) {
      if (const auto *quest_template = GetOrRequestQuestTemplate(*session, quest_id);
          quest_template != nullptr) {
        const auto turn_in_ready =
            IsQuestReadyForTurnInOnWorldMap(*session, *quest_template, *player_slot);
        const auto objective_mask =
            turn_in_ready
                ? 0u
                : ComputeQuestWorldMapObjectiveMask(
                      *quest_template, *player_slot,
                      session->inventory_replica());
        if (const auto *world_map = WorldMapStateOrNull(L);
            world_map != nullptr) {
          selection = ResolveQuestPoiWorldMapAreaSelection(
              *session, *dbc, *world_map, quest_id, turn_in_ready,
              objective_mask);
        }
        if (selection.world_map_area_id == 0) {
          selection.world_map_area_id = ResolveQuestHeaderWorldMapAreaId(*dbc, *quest_template);
        }
      }
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(selection.world_map_area_id));
  lua_pushnumber(L, static_cast<lua_Number>(selection.floor_id));
  return 2;
}

int LuaGetRewardMoney(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto reward_money = session ? GetSharedQuestRewardPreviewMoney(session->quests()) : 0;
  lua_pushnumber(L, static_cast<lua_Number>(reward_money));
  return 1;
}

int LuaGetRewardArenaPoints(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto reward_arena_points =
      session ? GetSharedQuestRewardPreviewArenaPoints(session->quests()) : 0;
  lua_pushnumber(L, static_cast<lua_Number>(reward_arena_points));
  return 1;
}

int LuaGetRewardHonor(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto reward_honor = session ? GetSharedQuestRewardPreviewHonor(session->quests()) : 0;
  FrameScript_PushNumberFromInt(L, static_cast<int>(reward_honor));
  return 1;
}

int LuaGetRewardTalents(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto reward_talents =
      session ? GetSharedQuestRewardPreviewTalentPoints(session->quests()) : 0;
  FrameScript_PushNumberFromInt(L, static_cast<int>(reward_talents));
  return 1;
}

int LuaGetRewardSpell(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto spell_id = session ? GetSharedQuestRewardPreviewSpellId(session->quests()) : 0;
  const auto spell_cast_id =
      session ? GetSharedQuestRewardPreviewSpellCastId(session->quests()) : 0;
  return PushRewardSpellInfo(L, LookupSpellEntry(L, spell_id), spell_cast_id);
}

int LuaGetRewardTitle(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto title_id = session ? GetSharedQuestRewardPreviewTitleId(session->quests()) : 0;
  const auto *entry = FindCharTitleEntryById(L, title_id);
  if (entry == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto formatted_title = FormatCharTitleForActivePlayer(L, *entry);
  lua_pushlstring(L, formatted_title.data(), formatted_title.size());
  return 1;
}

int LuaGetRewardXP(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto reward_xp = session ? GetSharedQuestRewardPreviewXp(*session) : 0;
  lua_pushnumber(L, static_cast<lua_Number>(reward_xp));
  return 1;
}

int LuaGetSuggestedGroupNum(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto suggested_players =
      session ? GetActiveQuestDialogSuggestedPlayers(session->quests()) : 0;
  lua_pushnumber(L, static_cast<lua_Number>(suggested_players));
  return 1;
}

int LuaIsCurrentQuestFailed(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (!session || !player) {
    lua_pushnil(L);
    return 1;
  }

  const int selected_index = FindSelectedInterleavedQuestIndex(*session, s_selected_quest_id);
  int quest_slot = 0;
  if (selected_index >= 0) {
    quest_slot = FindPlayerQuestSlotById(*player, s_selected_quest_id);
    if (quest_slot < 0) {
      lua_pushnil(L);
      return 1;
    }
  }

  const auto player_quest = player->GetQuestLog(static_cast<std::uint8_t>(quest_slot));
  const bool failed_from_player_state = (player_quest.state & 0x2u) != 0;
  const bool failed_from_quest_list =
      selected_index >= 0 && IsSelectedQuestMarkedFailed(session->quests());
  if (failed_from_player_state || failed_from_quest_list) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsQuestCompletable(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (!session || !player || !session->quests().has_active_request() ||
      !session->quests().active_request().is_completable) {
    lua_pushnil(L);
    return 1;
  }

  if (AreContiguousRequiredQuestPreviewItemsComplete(
          session->inventory_replica(),
          session->quests().active_request().required_items)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsUnitOnQuest(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: IsUnitOnQuest(index, \"unit\")");
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const int visible_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto interleaved = BuildInterleavedQuestLog(*session);
  if (visible_index < 1 ||
      static_cast<std::size_t>(visible_index) > interleaved.size()) {
    lua_pushnil(L);
    return 1;
  }

  const auto &selected = interleaved[static_cast<std::size_t>(visible_index - 1)];
  const auto &quest_log = session->quests().quest_log();
  if (selected.is_header || selected.quest_log_index >= quest_log.size()) {
    lua_pushnil(L);
    return 1;
  }

  const std::uint32_t quest_id = quest_log[selected.quest_log_index].quest_id;
  const auto *unit = ResolveUnitObject(ResolveUnit(session, UnitIdArg(L, 2)));
  const auto *player = dynamic_cast<const ::openwow::game::CGPlayer_C *>(unit);
  if (quest_id != 0 && player != nullptr) {
    for (std::uint8_t slot = 0; slot < ::openwow::game::kMaxQuestLogEntries; ++slot) {
      if (player->GetQuestLog(slot).quest_id == quest_id) {
        lua_pushnumber(L, 1.0);
        return 1;
      }
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaQuestChooseRewardError(lua_State *L) {
  (void)L;
  DisplaySystemMessage(165);
  return 0;
}

int LuaQuestGetAutoAccept(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {
    const auto flags = GetActiveQuestDialogFlags(session->quests());
    if (::openwow::game::HasFlag(flags, ::openwow::game::QuestFlags::kAutoAccept)) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

int LuaQuestFlagsPVP(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {
    const auto flags = GetActiveQuestDialogFlags(session->quests());
    if (::openwow::net::RealmConfigTables::Get().GetSelectedRealmPlayerKillingAllowed()) {
      lua_pushnil(L);
      return 1;
    }
    if (::openwow::game::HasFlag(flags, ::openwow::game::QuestFlags::kPvp)) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

static int ResolveQuestIndex(lua_State *L, int arg_pos) {
  if (lua_gettop(L) >= arg_pos && lua_isnumber(L, arg_pos))
    return static_cast<int>(lua_tonumber(L, arg_pos));
  return 0;
}

int LuaGetQuestLogChoiceInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GetQuestLogRewardInfo(index)");
  }

  const int item_index = static_cast<int>(lua_tonumber(L, 1));
  const auto *tmpl = GetSelectedQuestLogTemplate(GetWorldSession(L));
  if (tmpl == nullptr) {
    return PushEmptyQuestLogRewardInfo(L);
  }

  return PushQuestLogItemInfoFromTemplate(L, tmpl->reward_choice_items, item_index);
}

int LuaGetQuestLogRewardTalents(lua_State *L) {
  return PushSelectedQuestLogSignedRewardValue(
      L, [](const ::openwow::game::QuestTemplate &tmpl) { return tmpl.bonus_talents; });
}

int LuaGetQuestLogRewardArenaPoints(lua_State *L) {
  return PushSelectedQuestLogSignedRewardValue(
      L, [](const ::openwow::game::QuestTemplate &tmpl) { return tmpl.rew_arena_points; });
}

int LuaGetQuestLogRewardTitle(lua_State *L) {
  (void)ResolveQuestIndex(L, 1);
  const auto *tmpl = GetSelectedQuestLogTemplate(GetWorldSession(L));
  const auto *entry = tmpl != nullptr ? FindCharTitleEntryById(L, tmpl->char_title_id) : nullptr;
  if (entry == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto formatted_title = FormatCharTitleForActivePlayer(L, *entry);
  lua_pushlstring(L, formatted_title.data(), formatted_title.size());
  return 1;
}

int LuaGetQuestLogRewardSpell(lua_State *L) {
  (void)ResolveQuestIndex(L, 1);
  const auto *tmpl = GetSelectedQuestLogTemplate(GetWorldSession(L));
  return PushRewardSpellInfo(L, LookupSpellEntry(L, tmpl != nullptr ? tmpl->rew_spell : 0),
                             tmpl != nullptr ? tmpl->rew_spell_cast : 0);
}

int LuaCGTooltip_SetQuestLogRewardSpell(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  const auto *tmpl = GetSelectedQuestLogTemplate(session);
  if (tmpl != nullptr && tmpl->rew_spell != 0) {
    TooltipSystem::Get().SetSpellById(tmpl->rew_spell);
  }
  return 0;
}

int LuaGetNumQuestLogRewards(lua_State *L) {
  const auto *tmpl = GetSelectedQuestLogTemplate(GetWorldSession(L));
  lua_pushnumber(
      L, static_cast<lua_Number>(tmpl ? CountContiguousQuestTemplateItems(tmpl->reward_items) : 0));
  return 1;
}

int LuaGetNumQuestLogChoices(lua_State *L) {
  const auto *tmpl = GetSelectedQuestLogTemplate(GetWorldSession(L));
  lua_pushnumber(L, static_cast<lua_Number>(
                        tmpl ? CountContiguousQuestTemplateItems(tmpl->reward_choice_items) : 0));
  return 1;
}

int LuaGetQuestLogItemDrop(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetQuestLogItemDrop(index)");
  }

  auto *session = GetWorldSession(L);
  const int objective_index = static_cast<int>(lua_tonumber(L, 1)) - 1;
  if (!session || objective_index < 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const auto quest_id = ResolveQuestLogItemDropQuestId(L, *session);
  QuestLeaderboardLine line;
  if (!BuildQuestLogItemDropLine(*session, quest_id, objective_index, &line)) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  return PushLeaderboardLine(L, line);
}

int LuaGetQuestLink(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetQuestLink(questID)");
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto visible_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1)) - 1;
  if (visible_index < 0) {
    lua_pushnil(L);
    return 1;
  }

  const auto view = BuildQuestLogView(*session);
  if (visible_index >= static_cast<int>(view.visible_count)) {
    lua_pushnil(L);
    return 1;
  }

  const auto &visible_entry = view.entries[static_cast<std::size_t>(visible_index)];
  if (visible_entry.is_header) {
    lua_pushnil(L);
    return 1;
  }

  const auto &log = session->quests().quest_log();
  if (visible_entry.quest_log_index >= log.size()) {
    lua_pushnil(L);
    return 1;
  }

  const auto quest_id = log[visible_entry.quest_log_index].quest_id;
  const auto *tmpl = session->quests().GetTemplate(quest_id);
  if (tmpl == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto *player = session->objects().GetActivePlayer();
  const auto quest_level = ResolveQuestTemplateDisplayLevel(*tmpl, player);
  lua_pushstring(L, FormatQuestLink(quest_id, quest_level, tmpl->title, player));
  return 1;
}

int LuaGetAbandonQuestName(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {
    if (const auto *tmpl = GetAbandonQuestTemplate(*session)) {
      lua_pushstring(L, tmpl->title.c_str());
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

int LuaGetNumQuestLeaderBoards(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }

  int index = static_cast<int>(luaL_optinteger(L, 1, 0));
  std::uint32_t quest_id = 0;
  if (index >= 1) {
    quest_id = ResolveQuestIdFromInterleavedIndex(*session, index);
  } else {
    quest_id = s_selected_quest_id;
  }

  QuestRequirementQueryCallbacks callbacks;
  callbacks.on_quest_template_query =
      BuildQuestRequirementQueryCallback("Invalid quest log entry");
  const auto count = quest_id ? CountQuestLeaderboardObjectives(*session, quest_id, callbacks) : 0;
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaGetQuestIndexForWatch(lua_State *L) {
  const int watch_index = static_cast<int>(luaL_checkinteger(L, 1));
  if (watch_index < 1) {
    lua_pushnil(L);
    return 1;
  }

  auto &quest_log = ::openwow::game::QuestLog::Get();
  const auto quest_id = quest_log.GetTrackedQuestId(static_cast<std::size_t>(watch_index - 1));
  if (quest_id == 0) {
    lua_pushnil(L);
    return 1;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const int visible_index = FindInterleavedQuestIndexById(*session, quest_id);
  if (visible_index == 0) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(visible_index));
  return 1;
}

int LuaGetQuestIndexForTimer(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetQuestIndexForTimer(index)");
  }

  auto *session = GetWorldSession(L);
  const int wanted_timer_index = static_cast<int>(lua_tointeger(L, 1)) - 1;
  if (!session || wanted_timer_index < 0 || !session->objects().GetLocalPlayer()) {
    lua_pushnil(L);
    return 1;
  }

  const auto interleaved = BuildInterleavedQuestLog(*session);
  int timer_index = 0;
  for (std::size_t i = 0; i < interleaved.size(); ++i) {
    if (interleaved[i].is_header) {
      continue;
    }

    const auto timed_quest = BuildTimedQuestView(*session, static_cast<int>(i + 1));
    if (!timed_quest || IsQuestTimerSuppressed(*timed_quest) || timed_quest->seconds_left < 0) {
      continue;
    }

    if (timer_index == wanted_timer_index) {
      lua_pushnumber(L, static_cast<lua_Number>(i + 1));
      return 1;
    }

    ++timer_index;
  }

  lua_pushnil(L);
  return 1;
}

int LuaIsQuestWatched(lua_State *L) {
  const int index = static_cast<int>(luaL_checkinteger(L, 1));
  if (index < 1) {
    lua_pushnil(L);
    return 1;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  auto &quest_log = ::openwow::game::QuestLog::Get();
  const auto quest_id = ResolveQuestIdFromInterleavedIndex(*session, index);
  if (quest_id == 0 || !quest_log.IsTracked(quest_id)) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaExpandQuestHeader(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: ExpandQuestHeader(index)");
  }

  const int index = static_cast<int>(lua_tointeger(L, 1));
  if (SetQuestHeaderCollapsed(GetWorldSession(L), index, false)) {
    ScriptEventDispatch::Get().FireQuestLogUpdate();
  }
  return 0;
}

int LuaCollapseQuestHeader(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CollapseQuestHeader(index)");
  }

  const int index = static_cast<int>(lua_tointeger(L, 1));
  if (SetQuestHeaderCollapsed(GetWorldSession(L), index, true)) {
    ScriptEventDispatch::Get().FireQuestLogUpdate();
  }
  return 0;
}

int LuaGetDailyQuestsCompleted(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (!player) {
    FrameScript_PushNumberFromInt(L, 0);
    return 1;
  }

  FrameScript_PushNumberFromInt(
      L, static_cast<int>(player->GetDailyQuestCount()));
  return 1;
}

int LuaGetMaxDailyQuests(lua_State *L) {
  lua_pushnumber(L, 25);
  return 1;
}

int LuaGetQuestResetTime(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  const auto now = static_cast<std::int64_t>(std::time(nullptr));
  const auto &query_time = session->query_time();
  if (query_time.local_refresh_deadline_secs != 0 &&
      now >= query_time.local_refresh_deadline_secs) {
    session->interaction().SendQueryTime();
  }

  const auto seconds_until_reset = query_time.local_daily_reset_deadline_secs - now;
  lua_pushnumber(L, static_cast<lua_Number>(seconds_until_reset));
  return 1;
}

int LuaQuestIsDaily(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {
    const auto flags = GetActiveQuestDialogFlags(session->quests());
    if (::openwow::game::HasFlag(flags, ::openwow::game::QuestFlags::kDaily)) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

int LuaQuestIsWeekly(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {
    const auto flags = GetActiveQuestDialogFlags(session->quests());
    if (::openwow::game::HasFlag(flags, ::openwow::game::QuestFlags::kWeekly)) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

int LuaGetQuestSortIndex(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GetQuestSortIndex(questIndex)");
  }

  int sort_index = 0;
  if (auto *session = GetWorldSession(L)) {
    const int quest_index = static_cast<int>(lua_tonumber(L, 1));
    const int sort_slot = ResolveQuestSortSlotFromInterleavedIndex(*session, quest_index);
    if (sort_slot >= 0) {
      sort_index = sort_slot + 1;
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(sort_index));
  return 1;
}

int LuaIsActiveQuestTrivial(lua_State *L) {
  auto *session = GetWorldSession(L);
  const int index = static_cast<int>(luaL_checknumber(L, 1));
  if (!session) {
    lua_pushnil(L);
    return 1;
  }

  const auto entry = GetQuestGreetingEntry(*session, index, true);
  if (!entry) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushwowbool(L, IsQuestLevelTrivial(*session, entry->quest_level));
  return 1;
}

int LuaIsAvailableQuestTrivial(lua_State *L) {
  auto *session = GetWorldSession(L);
  const int index = static_cast<int>(luaL_checknumber(L, 1));
  if (!session) {
    lua_pushnil(L);
    return 1;
  }

  const auto entry = GetQuestGreetingEntry(*session, index, false);
  if (!entry) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushwowbool(L, IsQuestLevelTrivial(*session, entry->quest_level));
  return 1;
}

int LuaQuestPOIGetIconInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: QuestPOIGetIconInfo(questID)");
  }

  const auto quest_id = static_cast<std::uint32_t>(
      std::max(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)), 0));
  auto &poi_data = ::openwow::game::QuestPOIData::Get();
  if (!poi_data.FindQuerySlotIndex(quest_id).has_value()) {
    return 0;
  }

  auto layout = poi_data.GetWorldMapIconLayout(quest_id);
  if (!layout.has_value()) {
    auto *session = GetWorldSession(L);
    if (session == nullptr) {
      return 0;
    }

    const bool turn_in_ready = IsQuestTurnInReady(*session, quest_id);
    const auto objective_mask = BuildQuestPoiIncompleteObjectiveMask(*session, quest_id);
    for (const auto &poi : poi_data.GetPOIsForQuest(quest_id)) {
      if ((turn_in_ready && poi.objectiveIndex != -1) ||
          (!turn_in_ready && !QuestPoiPassesObjectiveMask(poi, objective_mask)) ||
          poi.points.empty()) {
        continue;
      }

      const auto center = ::openwow::game::QuestPOIData::GetCentroid(poi);
      layout = ::openwow::game::QuestPOIWorldMapIconLayout{
          .turnInReady = turn_in_ready,
          .objectiveIndex = poi.objectiveIndex,
          .mapId = poi.mapId,
          .worldX = static_cast<std::int32_t>(std::nearbyint(center.x)),
          .worldY = static_cast<std::int32_t>(std::nearbyint(center.y)),
      };
      poi_data.SetWorldMapIconLayout(quest_id, *layout);
      break;
    }
  }

  if (!layout.has_value()) {
    return 0;
  }

  auto *world_map = WorldMapStateOrNull(L);
  if (world_map == nullptr) {
    return 0;
  }
  const auto map_coord = world_map->WorldToMapForCurrentSelection(
      layout->mapId, static_cast<float>(layout->worldX),
      static_cast<float>(layout->worldY));
  if (!map_coord.valid || std::fabs(map_coord.x * map_coord.y) < 0.001f) {
    return 0;
  }

  lua_pushwowbool(L, layout->turnInReady);
  lua_pushnumber(L, static_cast<lua_Number>(map_coord.x));
  lua_pushnumber(L, static_cast<lua_Number>(map_coord.y));
  lua_pushnumber(L, static_cast<lua_Number>(layout->objectiveIndex));
  return 4;
}

int LuaQuestPOIGetQuestIDByIndex(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: QuestPOIGetQuestIDByIndex(index)");
  }

  const auto index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  if (index < 1 ||
      index > static_cast<std::int32_t>(::openwow::game::QuestPOIData::kMaxQuerySlots)) {
    return 0;
  }

  lua_pushnumber(L,
                 static_cast<lua_Number>(::openwow::game::QuestPOIData::Get().GetQuestIdByQuerySlot(
                     static_cast<std::size_t>(index))));
  return 1;
}

int LuaQuestMapUpdateAllQuests(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  int count = 0;

  const auto *world_map = WorldMapStateOrNull(L);
  if (session != nullptr && player != nullptr && world_map != nullptr) {
    const auto selection = world_map->GetQuestPoiSelectionContext();
    if (selection.can_update) {
      auto visible_quests =
          BuildVisibleWorldMapQuestIds(*session, *world_map, selection);
      count = static_cast<int>(visible_quests.size());
      ::openwow::game::QuestPOIData::Get().SetVisibleWorldMapQuestIds(
          std::move(visible_quests));
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaQuestPOIGetQuestIDByVisibleIndex(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: Script_QuestPOIGetQuestIDByVisibleIndex(index)");
  }

  const auto index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  if (index < 1 ||
      index > static_cast<std::int32_t>(::openwow::game::QuestPOIData::kMaxQuerySlots)) {
    return 0;
  }

  const auto quest_id = ::openwow::game::QuestPOIData::Get().GetQuestIdByVisibleWorldMapIndex(
      static_cast<std::size_t>(index));
  if (quest_id == 0) {
    return 0;
  }

  const auto visible_index = ::openwow::game::QuestLog::Get().GetVisibleIndexByQuestId(quest_id);
  if (visible_index < 0) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(quest_id));
  lua_pushnumber(L, static_cast<lua_Number>(visible_index + 1));
  return 2;
}

int LuaGetQuestPOILeaderBoard(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetQuestLogLeaderBoard(index)");
  }

  const int raw_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  auto *session = GetWorldSession(L);
  auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (!session || !player) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const auto quest_id = ResolveQuestLogItemDropQuestId(L, *session);

  if (quest_id == 0 || raw_index < 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  QuestLeaderboardLine line;
  bool ok = false;
  if (raw_index < ::openwow::game::kQuestObjectivesCount) {
    ok = BuildNpcOrGoLeaderboardLine(*session, *player, quest_id, raw_index, &line, true);
  } else if (raw_index <
             ::openwow::game::kQuestObjectivesCount + ::openwow::game::kQuestItemObjectivesCount) {
    ok = BuildItemLeaderboardLine(*session, *player, quest_id,
                                  raw_index - ::openwow::game::kQuestObjectivesCount, &line);
  } else if (raw_index < ::openwow::game::kQuestObjectivesCount +
                             ::openwow::game::kQuestItemObjectivesCount +
                             ::openwow::game::kQuestRewardsCount) {
    ok = BuildItemDropLeaderboardLine(*session, quest_id,
                                      raw_index - (::openwow::game::kQuestObjectivesCount +
                                                   ::openwow::game::kQuestItemObjectivesCount),
                                      &line);
  } else if (raw_index == 16 || raw_index == 17) {
    const auto *tmpl = GetOrRequestQuestTemplate(*session, quest_id);
    const int quest_slot = FindPlayerQuestSlotById(*player, quest_id);
    if (tmpl && quest_slot >= 0 && !tmpl->area_description.empty()) {
      const auto player_quest = player->GetQuestLog(static_cast<std::uint8_t>(quest_slot));
      line.text = tmpl->area_description;
      line.type = "event";
      line.finished = (player_quest.state & 0x1u) != 0;
      ok = true;
    }
  }

  if (!ok) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  return PushLeaderboardLine(L, line);
}

int LuaGetQuestLogSpecialItemInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetQuestLogSpecialItemInfo(index)");
  }

  auto *session = GetWorldSession(L);
  const auto quest_log_index = static_cast<std::uint32_t>(
      std::max(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)), 0));
  const auto *item = session != nullptr
                         ? ResolveQuestLogSpecialItem(*session, quest_log_index)
                         : nullptr;
  if (session == nullptr || item == nullptr) {
    return 0;
  }

  const auto link = BuildQuestSpecialItemLink(GetDbcLoader(L), *session, *item);
  const auto texture = TryResolveItemEntryIconTexturePath(L, item->entry);
  lua_pushlstring(L, link.data(), link.size());
  if (texture.has_value()) {
    lua_pushlstring(L, texture->data(), texture->size());
  } else {
    lua_pushliteral(L, "");
  }
  lua_pushnumber(L, static_cast<lua_Number>(ResolveQuestSpecialItemCharges(*session, *item)));
  return 3;
}

int LuaGetQuestLogSelection(lua_State *L) {
  auto *session = GetWorldSession(L);
  const int selected =
      session ? FindSelectedInterleavedQuestIndex(*session, s_selected_quest_id) : -1;
  lua_pushnumber(L, selected >= 0 ? selected + 1 : 0);
  return 1;
}

int LuaGetQuestWatchIndex(lua_State *L) {
  const int log_index = static_cast<int>(luaL_checkinteger(L, 1));
  if (log_index < 1) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  auto &quest_log = ::openwow::game::QuestLog::Get();
  const auto quest_id = ResolveQuestIdFromInterleavedIndex(*session, log_index);
  if (quest_id != 0) {
    const int watch_index = quest_log.GetTrackedIndex(quest_id);
    if (watch_index >= 0) {
      lua_pushnumber(L, static_cast<lua_Number>(watch_index + 1));
      return 1;
    }
  }

  return 0;
}

namespace {

std::uint32_t ResolveQuestWatchQuestId(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto &quest_log = ::openwow::game::QuestLog::Get();
  if (lua_isnumber(L, 1)) {
    const auto visible_index = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
    if (session == nullptr || visible_index == 0 ||
        visible_index > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
      return 0;
    }

    return ResolveQuestIdFromInterleavedIndex(*session, static_cast<int>(visible_index));
  }

  return quest_log.GetSelectedQuestId();
}

std::uint32_t ResolveQuestShareQuestId(::openwow::game::WorldSession *session,
                                       lua_State *L) {
  if (lua_isnumber(L, 1)) {
    if (session == nullptr) {
      return 0;
    }

    const int interleaved_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
    return ResolveQuestIdFromInterleavedIndex(*session, interleaved_index);
  }

  return s_selected_quest_id;
}

const ::openwow::game::QuestTemplate *
GetQuestShareTemplate(::openwow::game::WorldSession *session, const std::uint32_t quest_id) {
  if (session == nullptr || quest_id == 0) {
    return nullptr;
  }

  return session->quests().GetOrRequestTemplate(quest_id);
}

bool HasQuestShareRecipients(const ::openwow::game::GroupSystem &group_system) {
  return group_system.HasPartyMembers() ||
         group_system.GetRealRaidMemberCount() != 0;
}

}

int LuaAddQuestWatch(lua_State *L) {
  auto &quest_log = ::openwow::game::QuestLog::Get();
  const std::uint32_t quest_id = ResolveQuestWatchQuestId(L);
  if (quest_id != 0) {
    const auto duration_seconds =
        lua_isnumber(L, 2) ? openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2)) : 0;
    auto* const session = GetWorldSession(L);
    quest_log.AddQuestWatchFromLua(
        session != nullptr ? &session->objects() : nullptr, quest_id, duration_seconds);
  }
  return 0;
}

int LuaRemoveQuestWatch(lua_State *L) {
  auto &quest_log = ::openwow::game::QuestLog::Get();
  const std::uint32_t quest_id = ResolveQuestWatchQuestId(L);
  if (quest_id != 0) {
    quest_log.RemoveQuestWatch(quest_id);
  }
  return 0;
}

int LuaGetNumQuestWatches(lua_State *L) {
  auto &ql = ::openwow::game::QuestLog::Get();
  lua_pushnumber(L, static_cast<lua_Number>(ql.GetNumTracked()));
  return 1;
}

int LuaGetQuestLogPushable(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto quest_id = ResolveQuestShareQuestId(session, L);
  const auto *quest_template = GetQuestShareTemplate(session, quest_id);
  if (quest_template == nullptr ||
      !::openwow::game::HasFlag(quest_template->flags, ::openwow::game::QuestFlags::kSharable)) {
    return 0;
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaQuestLogPushQuest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto quest_id = ResolveQuestShareQuestId(session, L);
  if (quest_id == 0) {
    return 0;
  }

  const auto *quest_template = GetQuestShareTemplate(session, quest_id);
  if (session->objects().GetActivePlayer() == nullptr || quest_template == nullptr) {
    return 0;
  }

  if (!::openwow::game::HasFlag(quest_template->flags, ::openwow::game::QuestFlags::kSharable)) {
    return 0;
  }

  const auto &group_system = ::openwow::game::GroupSystem::Get();
  if (!HasQuestShareRecipients(group_system)) {
    return 0;
  }

  session->interaction().SendPushQuestToParty(quest_id);
  return 0;
}

int LuaShiftQuestWatches(lua_State *L) {
  const int from = static_cast<int>(luaL_checkinteger(L, 1));
  const int to = static_cast<int>(luaL_checkinteger(L, 2));
  if (from < 1 || to < 1) {
    return 0;
  }

  ::openwow::game::QuestLog::Get().MoveQuestWatch(static_cast<std::size_t>(from - 1),
                                                  static_cast<std::size_t>(to - 1));
  return 0;
}

int LuaIsQuestLogSpecialItemInRange(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: IsQuestLogSpecialItemInRange(index [,target])");
  }

  auto *session = GetWorldSession(L);
  const auto *player =
      session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (session == nullptr || player == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const int quest_log_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *item = ResolveQuestLogSpecialItem(*session,
                                                static_cast<std::uint32_t>(quest_log_index));
  if (!item) {
    lua_pushnil(L);
    return 1;
  }

  const auto *item_template =
      session->query_cache().GetOrRequestItemTemplate(item->entry);
  if (!item_template) {
    lua_pushnil(L);
    return 1;
  }

  const auto *dbc = GetDbcLoader(L);
  const auto spell_id = openwow::game::ResolveItemInstanceUseSpellId(
      *item, *item_template, dbc);
  if (spell_id == 0) {
    lua_pushnil(L);
    return 1;
  }

  const auto unit_id = SafeLuaString(L, 2);
  const auto target_guid = unit_id.empty()
                               ? session->objects().GetTargetGuid()
                               : ResolveUnitId(session, std::string(unit_id));
  if (target_guid.IsEmpty()) {
    lua_pushnil(L);
    return 1;
  }

  const auto *target = session->objects().GetUnit(target_guid);
  if (!target || !dbc) {
    lua_pushnil(L);
    return 1;
  }

  const auto result = openwow::game::SpellTargetValidator::ValidateUnitTarget(
      *session, *dbc, spell_id, *target);
  switch (result) {
  case openwow::game::SpellTargetResult::kValid:
    lua_pushnumber(L, 1.0);
    return 1;
  case openwow::game::SpellTargetResult::kOutOfRange:
  case openwow::game::SpellTargetResult::kTooClose:
    lua_pushnumber(L, 0.0);
    return 1;
  default:
    lua_pushnil(L);
    return 1;
  }
}

int LuaUseQuestLogSpecialItem(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: UseQuestLogSpecialItem(index)");
  }

  auto *session = GetWorldSession(L);
  const auto quest_log_index = static_cast<std::uint32_t>(
      std::max(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)), 0));
  const auto *item = session != nullptr
                         ? ResolveQuestLogSpecialItem(*session, quest_log_index)
                         : nullptr;
  if (session != nullptr && item != nullptr) {
    (void)session->interaction().SendUseItemByGuid(item->guid, 0);
  }
  return 0;
}

}
