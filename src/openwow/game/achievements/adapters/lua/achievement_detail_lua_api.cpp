
#include "openwow/game/achievements/adapters/lua/achievement_detail_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_category_selection.h"
#include "openwow/game/achievements/adapters/lua/achievement_faction_filter.h"
#include "openwow/game/achievements/adapters/lua/achievement_lua_visibility.h"
#include "openwow/game/achievements/adapters/lua/achievement_progress_formatter.h"
#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/game/achievements/rules/achievement_category_resolver.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <array>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

namespace openwow::ui::game::detail {

namespace {

using AchievementDbcEntry = openwow::data::dbc::AchievementEntry;
using LuaCall = openwow::ui::lua::LuaCall;

constexpr std::uint32_t kStatisticsCompletionCategoryId = 81;
constexpr std::uint32_t kAchievementHiddenFlag = 0x2;
constexpr std::uint32_t kAchievementIncompleteCountExclusionFlag = 0x800;
constexpr std::uint32_t kAchievementCategorySelectorFlag = 0x1;

std::string ExpandAchievementObjectText(const ::openwow::game::WorldSession *session,
                                        std::string_view text,
                                        std::uint32_t achievement_id);
std::string ResolveAchievementIconPath(const openwow::data::dbc::DbcLoader &dbc,
                                       const AchievementDbcEntry &achievement);

const AchievementDbcEntry *LookupAchievementEntry(const openwow::data::dbc::DbcLoader &dbc,
                                                  const std::uint32_t achievement_id) {
  return dbc.achievement().LookupEntry(achievement_id);
}

void AppendAchievementCompletionDate(
    LuaCall& results,
    const ::openwow::game::CompletedAchievement *completed_achievement) {
  if (completed_achievement == nullptr) {
    results.PushNil().PushNil().PushNil();
    return;
  }

  const auto raw = completed_achievement->completion_date.ToWireValue();
  results.PushNumber(((raw >> 20) & 0x0Fu) + 1u)
      .PushNumber(((raw >> 14) & 0x3Fu) + 1u)
      .PushNumber((raw >> 24) & 0x1Fu);
}

int PushAchievementInfoResult(
    lua_State *L, const ::openwow::game::WorldSession *session,
    const openwow::data::dbc::DbcLoader &dbc, const AchievementDbcEntry &achievement,
    const ::openwow::game::CompletedAchievement *completed_achievement) {
  const auto name = ExpandAchievementObjectText(session, achievement.name, achievement.id);
  const auto description =
      ExpandAchievementObjectText(session, achievement.description, achievement.id);
  const auto reward_text =
      ExpandAchievementObjectText(session, achievement.reward_text, achievement.id);
  const auto icon_path = ResolveAchievementIconPath(dbc, achievement);

  LuaCall results(L);
  results.PushNumber(achievement.id)
      .PushString(name)
      .PushNumber(achievement.points)
      .PushBoolean(completed_achievement != nullptr);
  AppendAchievementCompletionDate(results, completed_achievement);
  return results.PushString(description)
      .PushNumber(achievement.flags)
      .PushString(icon_path)
      .PushString(reward_text)
      .ResultCount();
}

const AchievementDbcEntry *FindAchievementForCategoryIndex(
    const openwow::data::dbc::DbcLoader &dbc, const ::openwow::game::WorldSession &session,
    const AchievementCategorySelector selector, std::int32_t index,
    const AchievementIdSet &local_completed_ids,
    const AchievementIdSet &comparison_completed_ids) {
  if (index <= 0) {
    return nullptr;
  }

  const auto matches_completed_selection =
      [selector](const AchievementDbcEntry &achievement) {
        return selector.Matches(achievement) &&
               (achievement.flags & kAchievementHiddenFlag) == 0;
      };

  const auto consume_completed_entry =
      [&dbc, selector, &local_completed_ids, &index,
       &matches_completed_selection](
          const AchievementDbcEntry *achievement) {
        if (achievement == nullptr || !matches_completed_selection(*achievement)) {
          return false;
        }

        if (!selector.IsWildcard() &&
            HasCompletedChildAchievement(
                dbc, local_completed_ids,
                openwow::game::AchievementId{achievement->id})) {
          return false;
        }

        --index;
        return index == 0;
      };

  for (const auto &completed : session.achievements().completed_list()) {
    if (!comparison_completed_ids.contains(completed.id)) {
      continue;
    }
    if (const auto *achievement =
            LookupAchievementEntry(dbc, completed.id.value);
        consume_completed_entry(achievement)) {
      return achievement;
    }
  }

  for (const auto &completed : session.achievements().completed_list()) {
    if (comparison_completed_ids.contains(completed.id)) {
      continue;
    }
    if (const auto *achievement =
            LookupAchievementEntry(dbc, completed.id.value);
        consume_completed_entry(achievement)) {
      return achievement;
    }
  }

  for (const auto &completed : session.achievements().last_inspect().achievements) {
    if (local_completed_ids.contains(completed.id)) {
      continue;
    }
    if (const auto *achievement =
            LookupAchievementEntry(dbc, completed.id.value);
        consume_completed_entry(achievement)) {
      return achievement;
    }
  }

  if (selector.IsStatisticsCategory()) {
    return nullptr;
  }

  const auto *player = session.objects().GetLocalPlayer();
  const auto local_faction = ResolveAchievementFaction(player);
  openwow::game::AchievementId previous_chain_id;
  for (const auto &achievement : dbc.achievement()) {
    if ((achievement.flags & kAchievementHiddenFlag) != 0) {
      continue;
    }
    if (player != nullptr && !MatchesAchievementFaction(achievement, local_faction)) {
      continue;
    }

    if (selector.mode ==
        AchievementCategorySelectionMode::kPrimaryAchievements) {
      if ((achievement.flags & kAchievementCategorySelectorFlag) != 0 ||
          achievement.category == kStatisticsCompletionCategoryId ||
          local_completed_ids.contains(
              openwow::game::AchievementId{achievement.id}) ||
          comparison_completed_ids.contains(
              openwow::game::AchievementId{achievement.id}) ||
          (achievement.flags & kAchievementIncompleteCountExclusionFlag) != 0) {
        continue;
      }
      --index;
      if (index == 0) {
        return &achievement;
      }
      continue;
    }

    if (selector.mode ==
        AchievementCategorySelectionMode::kMetaAchievements) {
      if ((achievement.flags & kAchievementCategorySelectorFlag) == 0 ||
          achievement.category == kStatisticsCompletionCategoryId ||
          (achievement.flags & kAchievementIncompleteCountExclusionFlag) != 0) {
        continue;
      }
      --index;
      if (index == 0) {
        return &achievement;
      }
      continue;
    }

    if (achievement.category != selector.category_id) {
      continue;
    }

    if (previous_chain_id.value != 0 &&
        previous_chain_id.value == achievement.parent_achievement) {
      previous_chain_id = openwow::game::AchievementId{achievement.id};
      continue;
    }

    if (local_completed_ids.contains(
            openwow::game::AchievementId{achievement.id}) ||
        comparison_completed_ids.contains(
            openwow::game::AchievementId{achievement.id}) ||
        (achievement.flags & kAchievementIncompleteCountExclusionFlag) != 0) {
      continue;
    }

    --index;
    previous_chain_id = openwow::game::AchievementId{achievement.id};
    if (index == 0) {
      return &achievement;
    }
  }

  return nullptr;
}

std::string ExpandAchievementObjectText(const ::openwow::game::WorldSession *session,
                                        const std::string_view raw_text,
                                        const std::uint32_t achievement_id) {
  if (raw_text.empty()) {
    return {};
  }

  std::array<char, 3000> expanded_text{};
  std::array<char, 256> name_buffer{};
  std::uint64_t active_player_guid = 0;
  if (session != nullptr) {
    const auto player_guid = session->objects().GetLocalPlayerGuid();
    active_player_guid = player_guid.GetRawValue();
    const auto player_name = session->objects().GetPlayerName(player_guid);
    if (!player_name.empty()) {
      std::snprintf(name_buffer.data(), name_buffer.size(), "%s", player_name.c_str());
    }
  }

  const auto current_time_seconds = session != nullptr
      ? session->world_states().world_state_ui_current_time_seconds(std::time(nullptr))
      : 0;
  ::openwow::game::SpellTextFormatter::ExpandObjectTextVariables(
      raw_text.data(), expanded_text.data(), static_cast<std::uint32_t>(expanded_text.size()),
      active_player_guid, name_buffer.data(),
      static_cast<std::int32_t>(name_buffer.size()),
      [session](std::int32_t variable_id) {
        if (session == nullptr || variable_id == 0) {
          return 0;
        }
        return session->world_states().GetWorldState(variable_id);
      },
      current_time_seconds,
      achievement_id);
  return std::string(expanded_text.data());
}

std::string ResolveAchievementIconPath(const openwow::data::dbc::DbcLoader &dbc,
                                       const AchievementDbcEntry &achievement) {
  const auto *icon = dbc.spell_icon().LookupEntry(achievement.icon);
  if (icon == nullptr) {
    return {};
  }
  return std::string(icon->icon_path);
}

}

int LuaGetAchievementInfo(lua_State *L) {
  const LuaCall arguments(L);
  const auto first_argument = TruncateLuaNumberToSseI32(
      arguments.RequireNumber(1, "Usage: GetAchievementInfo(achievementID)"));
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr || session == nullptr) {
    return 0;
  }

  std::int32_t achievement_id = first_argument;
  if (arguments.IsNumber(2)) {
    const auto category_id = first_argument;
    const auto index = TruncateLuaNumberToSseI32(arguments.Number(2));
    const auto selector =
        AchievementCategorySelector::FromLuaValue(category_id);
    const auto local_completed_ids =
        CollectAchievementIds(session->achievements().completed_list(),
                              [](const auto &achievement) { return achievement.id; });
    const auto comparison_completed_ids =
        CollectAchievementIds(session->achievements().last_inspect().achievements,
                              [](const auto &achievement) { return achievement.id; });
    const auto *selected = FindAchievementForCategoryIndex(
        *dbc, *session, selector, index, local_completed_ids,
        comparison_completed_ids);
    if (selected == nullptr) {
      return 0;
    }
    achievement_id = static_cast<std::int32_t>(selected->id);
  }

  const auto *achievement = FindVisibleAchievement(*dbc, achievement_id);
  if (achievement == nullptr) {
    return 0;
  }

  const auto *completed_achievement =
      session->achievements().FindCompletedAchievement(
          ::openwow::game::AchievementId{achievement->id});
  if ((achievement->flags & kAchievementIncompleteCountExclusionFlag) != 0 &&
      completed_achievement == nullptr) {
    return 0;
  }

  return PushAchievementInfoResult(L, session, *dbc, *achievement, completed_achievement);
}

int LuaGetAchievementInfoFromCriteria(lua_State *L) {
  const LuaCall arguments(L);
  const auto raw_criteria_id = arguments.RequireNumber(
      1, "Usage: GetAchievementInfoFromCriteria(criteriaID)");

  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return 0;
  }

  const auto criteria_id = TruncateLuaNumberToSseI32(raw_criteria_id);
  if (criteria_id < 0) {
    return 0;
  }
  const auto *criteria = LookupAchievementCriteriaEntry(
      *dbc, openwow::game::AchievementCriteriaId{
                static_cast<std::uint32_t>(criteria_id)});
  if (criteria == nullptr) {
    return 0;
  }

  const auto *achievement = ResolveCriteriaOwnerAchievement(*dbc, *criteria);
  if (achievement == nullptr) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  const auto name =
      ExpandAchievementObjectText(session, achievement->name, achievement->id);
  const auto description =
      ExpandAchievementObjectText(session, achievement->description, achievement->id);
  const auto reward_text =
      ExpandAchievementObjectText(session, achievement->reward_text, achievement->id);
  const auto icon_path = ResolveAchievementIconPath(*dbc, *achievement);

  return LuaCall(L)
      .PushNumber(achievement->id)
      .PushString(name)
      .PushNumber(achievement->points)
      .PushString(description)
      .PushNumber(achievement->flags)
      .PushString(icon_path)
      .PushString(reward_text)
      .ResultCount();
}

}
