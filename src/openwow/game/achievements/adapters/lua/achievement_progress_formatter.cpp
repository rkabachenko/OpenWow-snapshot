#include "openwow/game/achievements/adapters/lua/achievement_progress_formatter.h"

#include "openwow/game/localization.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace openwow::ui::game::detail {
namespace {

using AchievementEntry = openwow::data::dbc::AchievementEntry;
using CriteriaEntry = openwow::data::dbc::AchievementCriteriaEntry;

constexpr std::uint32_t kDisplayDashForZeroFlag = 0x1;
constexpr std::uint32_t kAggregateSumFlag = 0x8;
constexpr std::uint32_t kAggregateMaxFlag = 0x10;
constexpr std::uint32_t kAggregateCountFlag = 0x20;
constexpr std::uint32_t kPerDayFlag = 0x40;
constexpr std::uint32_t kShowDenominatorFlag = 0x1;
constexpr std::uint32_t kDateDisplayFlag = 0x10;
constexpr std::uint32_t kMoneyDisplayFlag = 0x20;
constexpr std::uint32_t kClearMaxOnTieFlag = 0x400;
constexpr int kDefaultCoinTextureFontHeight = 14;
constexpr std::array<std::string_view, 3> kCoinTextGlobalKeys{
    "GOLD_AMOUNT", "SILVER_AMOUNT", "COPPER_AMOUNT"};
constexpr std::array<std::string_view, 3> kCoinTextureGlobalKeys{
    "GOLD_AMOUNT_TEXTURE", "SILVER_AMOUNT_TEXTURE", "COPPER_AMOUNT_TEXTURE"};

struct CoinAmountParts final {
  std::uint32_t gold = 0;
  std::uint32_t silver = 0;
  std::uint32_t copper = 0;
};

const AchievementEntry* LookupAchievementEntry(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::AchievementId achievement_id) {
  return dbc.achievement().LookupEntry(achievement_id.value);
}

std::string GetLuaOrLocalizedGlobalString(lua_State* state,
                                         const std::string_view key) {
  const auto value = openwow::ui::lua::LuaCall(state).ReadGlobalString(key);
  if (!value.empty()) {
    return value;
  }
  const std::string key_string(key);
  return openwow::game::Localization::Get().GetString(key_string, key_string);
}

template <std::size_t Size>
std::string RenderCoinGlobalString(lua_State* state,
                                   const std::string_view key,
                                   const std::array<int, Size>& arguments) {
  std::vector<std::string> format_arguments;
  format_arguments.reserve(Size);
  for (const int argument : arguments) {
    format_arguments.emplace_back(std::to_string(argument));
  }
  return openwow::game::Localization::Get().FormatString(
      GetLuaOrLocalizedGlobalString(state, key), format_arguments);
}

CoinAmountParts SplitCoinAmount(const std::uint32_t amount) {
  CoinAmountParts parts;
  std::uint32_t remaining = amount;
  if (remaining >= 10000) {
    parts.gold = remaining / 10000;
    remaining %= 10000;
  }
  if (remaining >= 100) {
    parts.silver = remaining / 100;
    remaining %= 100;
  }
  parts.copper = remaining;
  return parts;
}

std::string BuildCoinTextString(lua_State* state, const std::uint32_t amount,
                                const std::string_view separator) {
  if (amount == 0) {
    return RenderCoinGlobalString(state, kCoinTextGlobalKeys[2],
                                  std::array<int, 1>{0});
  }

  const auto parts = SplitCoinAmount(amount);
  const std::array<int, 3> values{static_cast<int>(parts.gold),
                                  static_cast<int>(parts.silver),
                                  static_cast<int>(parts.copper)};
  std::string result;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (values[index] == 0) {
      continue;
    }
    if (!result.empty()) {
      result += separator;
    }
    result += RenderCoinGlobalString(
        state, kCoinTextGlobalKeys[index],
        std::array<int, 1>{values[index]});
  }
  return result;
}

std::string BuildCoinTextureString(lua_State* state,
                                   const std::uint32_t amount) {
  if (amount == 0) {
    return RenderCoinGlobalString(
        state, kCoinTextureGlobalKeys[2],
        std::array<int, 3>{0, kDefaultCoinTextureFontHeight,
                           kDefaultCoinTextureFontHeight});
  }

  const auto parts = SplitCoinAmount(amount);
  const std::array<int, 3> values{static_cast<int>(parts.gold),
                                  static_cast<int>(parts.silver),
                                  static_cast<int>(parts.copper)};
  std::string result;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (values[index] == 0) {
      continue;
    }
    if (!result.empty()) {
      result.push_back(' ');
    }
    result += RenderCoinGlobalString(
        state, kCoinTextureGlobalKeys[index],
        std::array<int, 3>{values[index], kDefaultCoinTextureFontHeight,
                           kDefaultCoinTextureFontHeight});
  }
  return result;
}

std::uint32_t TruncateQuantity(const std::uint64_t value) {
  return static_cast<std::uint32_t>(value & 0xFFFFFFFFu);
}

std::string FormatFixedTwoDecimals(const double value) {
  std::ostringstream stream;
  stream.setf(std::ios::fixed, std::ios::floatfield);
  stream.precision(2);
  stream << value;
  return stream.str();
}

std::string FormatPackedDateString(
    const openwow::game::PackedAchievementTime packed_time) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%02u/%02u/%u",
                static_cast<unsigned>(packed_time.Month() + 1),
                static_cast<unsigned>(packed_time.Day()),
                static_cast<unsigned>(packed_time.Year() + 1900));
  return buffer;
}

std::optional<std::time_t> PackedDateToTimeT(
    const openwow::game::PackedAchievementTime packed_time) {
  const int year = static_cast<int>(packed_time.Year() + 1900);
  const int month = static_cast<int>(packed_time.Month() + 1);
  const int day = static_cast<int>(packed_time.Day());
  if (month < 1 || month > 12 || day < 1 || day > 31) {
    return std::nullopt;
  }

  std::tm time{};
  time.tm_year = year - 1900;
  time.tm_mon = month - 1;
  time.tm_mday = day;
  time.tm_isdst = -1;
  const auto converted = std::mktime(&time);
  if (converted == static_cast<std::time_t>(-1)) {
    return std::nullopt;
  }
  return converted;
}

std::uint32_t ComputeElapsedDays(
    const openwow::game::PackedAchievementTime packed_time) {
  const auto start = PackedDateToTimeT(packed_time);
  if (!start) {
    return 0;
  }
  const auto now = std::time(nullptr);
  if (now <= *start) {
    return 0;
  }
  return static_cast<std::uint32_t>((now - *start) / 86400);
}

std::string FormatMoneyValue(lua_State* state, const std::uint32_t amount,
                             const bool colorblind_mode) {
  if (colorblind_mode) {
    return BuildCoinTextString(state, amount, " ");
  }
  return BuildCoinTextureString(state, amount);
}

std::string FormatZeroValue(const AchievementEntry& achievement) {
  if ((achievement.flags & kDisplayDashForZeroFlag) != 0) {
    return "--";
  }
  return "0";
}

}

CriteriaProgressMap BuildCriteriaProgressMap(
    const std::unordered_map<openwow::game::AchievementCriteriaId,
                             openwow::game::CriteriaProgress,
                             openwow::game::AchievementCriteriaIdHash>&
        criteria) {
  CriteriaProgressMap snapshots;
  snapshots.reserve(criteria.size());
  for (const auto& [criteria_id, progress] : criteria) {
    snapshots.emplace(criteria_id,
                      CriteriaProgressSnapshot{criteria_id, progress.counter,
                                               progress.date,
                                               progress.player_guid});
  }
  return snapshots;
}

CriteriaProgressMap BuildCriteriaProgressMap(
    const std::vector<openwow::game::CriteriaProgress>& criteria) {
  CriteriaProgressMap snapshots;
  snapshots.reserve(criteria.size());
  for (const auto& progress : criteria) {
    snapshots.emplace(
        progress.criteria_id,
        CriteriaProgressSnapshot{progress.criteria_id, progress.counter,
                                 progress.date, progress.player_guid});
  }
  return snapshots;
}

bool HasCompletedAchievement(
    const std::unordered_map<openwow::game::AchievementId,
                             openwow::game::CompletedAchievement,
                             openwow::game::AchievementIdHash>& achievements,
    const openwow::game::AchievementId achievement_id) {
  return achievements.contains(achievement_id);
}

bool HasCompletedAchievement(
    const std::vector<openwow::game::CompletedAchievement>& achievements,
    const openwow::game::AchievementId achievement_id) {
  return std::any_of(
      achievements.begin(), achievements.end(),
      [achievement_id](const openwow::game::CompletedAchievement& achievement) {
        return achievement.id == achievement_id;
      });
}

const CriteriaEntry* LookupAchievementCriteriaEntry(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::AchievementCriteriaId criteria_id) {
  return dbc.achievement_criteria().LookupEntry(criteria_id.value);
}

std::vector<const CriteriaEntry*> CollectAchievementCriteriaEntries(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::AchievementId achievement_id) {
  std::vector<const CriteriaEntry*> entries;
  for (const auto& criteria : dbc.achievement_criteria()) {
    if (criteria.achievement_id == achievement_id.value) {
      entries.push_back(&criteria);
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const CriteriaEntry* left, const CriteriaEntry* right) {
              if (left->order != right->order) {
                return left->order < right->order;
              }
              return left->id < right->id;
            });
  return entries;
}

const AchievementEntry* ResolveCriteriaOwnerAchievement(
    const openwow::data::dbc::DbcLoader& dbc, const CriteriaEntry& criteria) {
  return LookupAchievementEntry(
      dbc, openwow::game::AchievementId{criteria.achievement_id});
}

const AchievementEntry* ResolveCriteriaRootAchievement(
    const openwow::data::dbc::DbcLoader& dbc,
    const AchievementEntry& achievement) {
  if (achievement.parent_achievement == 0) {
    return &achievement;
  }
  const auto* parent = LookupAchievementEntry(
      dbc, openwow::game::AchievementId{achievement.parent_achievement});
  if (parent != nullptr) {
    return parent;
  }
  return &achievement;
}

AggregatedCriteriaState BuildAggregatedCriteriaState(
    const openwow::data::dbc::DbcLoader& dbc,
    const AchievementEntry& achievement,
    const CriteriaProgressMap& progress_snapshots,
    const std::uint32_t tie_clear_flags) {
  AggregatedCriteriaState state;
  const auto* root = ResolveCriteriaRootAchievement(dbc, achievement);

  for (const auto* criteria : CollectAchievementCriteriaEntries(
           dbc, openwow::game::AchievementId{root->id})) {
    const auto progress_it = progress_snapshots.find(
        openwow::game::AchievementCriteriaId{criteria->id});
    if (progress_it == progress_snapshots.end()) {
      continue;
    }

    const auto& progress = progress_it->second;
    state.last_progress_criteria = criteria;
    if ((achievement.flags & kAggregateSumFlag) != 0) {
      state.sum_value += progress.counter.value;
    }
    if ((achievement.flags & kAggregateMaxFlag) != 0) {
      if (progress.counter.value > state.max_value) {
        state.max_value = progress.counter.value;
        state.max_criteria_id =
            openwow::game::AchievementCriteriaId{criteria->id};
        state.has_max_criteria = true;
        state.max_value_tied = false;
      } else if (progress.counter.value == state.max_value &&
                 state.max_value != 0) {
        state.max_value_tied = true;
      }
    }
    if ((achievement.flags & kAggregateCountFlag) != 0 &&
        progress.counter.value >= criteria->quantity) {
      ++state.completed_count;
    }
    if (state.representative_progress == nullptr ||
        progress.date.ToWireValue() <
            state.representative_progress->date.ToWireValue()) {
      state.representative_progress = &progress;
    }
  }

  if (state.max_value_tied &&
      (tie_clear_flags & kClearMaxOnTieFlag) != 0) {
    state.max_criteria_id = {};
    state.has_max_criteria = false;
  }
  return state;
}

FormattedCriteriaProgress FormatAchievementProgressString(
    lua_State* state, const openwow::data::dbc::DbcLoader& dbc,
    const AchievementEntry& achievement, const std::uint64_t sum_value,
    const std::uint64_t max_value,
    const openwow::game::AchievementCriteriaId max_criteria_id,
    const bool has_max_criteria, const std::uint64_t completed_count,
    const CriteriaProgressSnapshot* current_progress,
    const std::uint32_t effective_criteria_flags,
    const bool has_completed_achievement,
    const CriteriaEntry* suffix_criteria,
    const std::uint32_t override_max_quantity) {
  const bool colorblind_mode =
      CVarSystem::Instance().GetCVarBool("colorblindMode");
  FormattedCriteriaProgress formatted;
  const auto append_denominator = [&](std::string text) {
    if ((effective_criteria_flags & kShowDenominatorFlag) == 0 ||
        suffix_criteria == nullptr) {
      return text;
    }
    const std::uint32_t denominator =
        override_max_quantity != 0 ? override_max_quantity
                                   : suffix_criteria->quantity;
    const std::string denominator_text =
        (effective_criteria_flags & kMoneyDisplayFlag) != 0
            ? FormatMoneyValue(state, denominator, colorblind_mode)
            : std::to_string(denominator);
    return text + " / " + denominator_text;
  };

  if ((achievement.flags & kAggregateSumFlag) != 0) {
    std::uint64_t display_value = sum_value;
    double average = 0.0;
    if (current_progress != nullptr &&
        (achievement.flags & kPerDayFlag) != 0) {
      const auto elapsed_days = ComputeElapsedDays(current_progress->date);
      if (elapsed_days != 0) {
        average = static_cast<double>(display_value) /
                  static_cast<double>(elapsed_days);
        display_value = static_cast<std::uint64_t>(average);
      }
    }
    formatted.quantity = TruncateQuantity(display_value);
    if ((effective_criteria_flags & kMoneyDisplayFlag) != 0) {
      formatted.text =
          FormatMoneyValue(state, formatted.quantity, colorblind_mode);
    } else if (average != 0.0) {
      formatted.text = FormatFixedTwoDecimals(average);
    } else if (display_value != 0) {
      formatted.text = std::to_string(display_value);
    } else {
      formatted.text = FormatZeroValue(achievement);
    }
    formatted.text = append_denominator(std::move(formatted.text));
    return formatted;
  }

  if ((achievement.flags & kAggregateMaxFlag) != 0) {
    formatted.quantity = TruncateQuantity(max_value);
    const auto* max_criteria =
        has_max_criteria
            ? LookupAchievementCriteriaEntry(dbc, max_criteria_id)
            : nullptr;
    if (max_criteria != nullptr &&
        (achievement.flags & kDisplayDashForZeroFlag) != 0) {
      formatted.text = std::to_string(max_value) + " (" +
                       std::string(max_criteria->description) + ")";
    } else if (max_value != 0) {
      formatted.text = std::to_string(max_value);
    } else {
      formatted.text = FormatZeroValue(achievement);
    }
    formatted.text = append_denominator(std::move(formatted.text));
    return formatted;
  }

  if ((achievement.flags & kAggregateCountFlag) != 0) {
    formatted.quantity = TruncateQuantity(completed_count);
    if (completed_count != 0) {
      formatted.text = std::to_string(completed_count);
    } else {
      formatted.text = FormatZeroValue(achievement);
    }
    formatted.text = append_denominator(std::move(formatted.text));
    return formatted;
  }

  if (current_progress != nullptr) {
    std::uint64_t display_value = current_progress->counter.value;
    double per_day_value = 0.0;
    if (suffix_criteria != nullptr &&
        (achievement.flags & kDisplayDashForZeroFlag) == 0 &&
        display_value >= suffix_criteria->quantity) {
      display_value = suffix_criteria->quantity;
    }
    if ((achievement.flags & kPerDayFlag) != 0) {
      const auto elapsed_days = ComputeElapsedDays(current_progress->date);
      if (elapsed_days != 0) {
        per_day_value = static_cast<double>(display_value) /
                        static_cast<double>(elapsed_days);
        display_value = static_cast<std::uint64_t>(per_day_value);
      }
    }

    formatted.quantity = TruncateQuantity(display_value);
    const auto* current_criteria = LookupAchievementCriteriaEntry(
        dbc, current_progress->criteria_id);
    const std::uint32_t current_flags =
        current_criteria != nullptr ? current_criteria->flags : 0;
    if ((current_flags & kDateDisplayFlag) != 0) {
      formatted.text = FormatPackedDateString(
          openwow::game::PackedAchievementTime::FromWireValue(
              formatted.quantity));
    } else if ((current_flags & kMoneyDisplayFlag) != 0) {
      formatted.text =
          FormatMoneyValue(state, formatted.quantity, colorblind_mode);
    } else if ((achievement.flags & kDisplayDashForZeroFlag) != 0) {
      if (per_day_value != 0.0) {
        formatted.text = FormatFixedTwoDecimals(per_day_value);
      } else if (display_value != 0) {
        formatted.text = std::to_string(display_value);
      } else {
        formatted.text = "--";
      }
    } else {
      formatted.text =
          display_value != 0 ? std::to_string(display_value) : "0";
    }
    formatted.text = append_denominator(std::move(formatted.text));
    return formatted;
  }

  if (has_completed_achievement && achievement.count == 0 &&
      suffix_criteria != nullptr) {
    formatted.quantity = suffix_criteria->quantity;
    formatted.text =
        (effective_criteria_flags & kMoneyDisplayFlag) != 0
            ? FormatMoneyValue(state, formatted.quantity, colorblind_mode)
            : std::to_string(suffix_criteria->quantity);
    formatted.text = append_denominator(std::move(formatted.text));
    return formatted;
  }

  formatted.quantity = 0;
  if ((effective_criteria_flags & kMoneyDisplayFlag) != 0 &&
      (effective_criteria_flags & kShowDenominatorFlag) != 0) {
    formatted.text = FormatMoneyValue(state, 0, colorblind_mode);
  } else {
    formatted.text = FormatZeroValue(achievement);
  }
  formatted.text = append_denominator(std::move(formatted.text));
  return formatted;
}

}
