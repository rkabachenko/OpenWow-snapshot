
#include "openwow/ui/game/autocomplete.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_string.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_ui_manager.h"

#include <algorithm>
#include <cctype>

namespace openwow::ui::game {

namespace {

std::uint32_t ReadRecentTargetTimestamp() {
  return openwow::core::GameClock::GetTickCount32();
}

enum class RecentTargetPruneMode : std::uint8_t {
  kAlways,
  kWhenContextFilteringEnabled,
};

bool ShouldUseLuaContextFilter() {
  auto &cvars = CVarSystem::Instance();
  if (!cvars.Exists("autoCompleteUseContext")) {
    return true;
  }
  return cvars.GetCVarBool("autoCompleteUseContext");
}

bool ShouldResortRecentNamesOnRecency() {
  auto &cvars = CVarSystem::Instance();
  if (!cvars.Exists("autoCompleteResortNamesOnRecency")) {
    return true;
  }
  return cvars.GetCVarBool("autoCompleteResortNamesOnRecency");
}

bool ShouldMatchWhenEditingFromCenter() {
  auto &cvars = CVarSystem::Instance();
  if (!cvars.Exists("autoCompleteWhenEditingFromCenter")) {
    return true;
  }
  return cvars.GetCVarBool("autoCompleteWhenEditingFromCenter");
}

bool EqualsUtf8NoCase(std::string_view left, std::string_view right) {
  const char *const left_ptr = left.empty() ? "" : left.data();
  const char *const right_ptr = right.empty() ? "" : right.data();
  return openwow::core::SStrCmpUTF8NoCase(left_ptr, right_ptr, 0x7FFFFFFFu) == 0;
}

bool PrefixMatchesUtf8NoCase(std::string_view prefix, std::string_view name) {
  const char *const prefix_ptr = prefix.empty() ? "" : prefix.data();
  const char *const name_ptr = name.empty() ? "" : name.data();
  const auto prefix_codepoints = openwow::core::CountLegacyUtf8Codepoints(prefix);
  return prefix_codepoints <= openwow::core::CountLegacyUtf8Codepoints(name) &&
         openwow::core::SStrCmpUTF8NoCase(prefix_ptr, name_ptr, prefix_codepoints) == 0;
}

bool MatchesRecentPlayerQuery(std::string_view text, std::string_view name,
                              std::size_t cursor_position, bool allow_full_match) {
  if (text.empty() || name.empty()) {
    return false;
  }

  if (!allow_full_match && EqualsUtf8NoCase(text, name)) {
    return false;
  }

  const auto clamped_cursor =
      std::min(cursor_position, openwow::core::CountLegacyUtf8Codepoints(text));
  if (!ShouldMatchWhenEditingFromCenter()) {
    return PrefixMatchesUtf8NoCase(text, name);
  }
  return openwow::core::LegacyUtf8CursorContainsNoCase(name, text, clamped_cursor);
}

bool RecentTargetPrecedes(const AutoComplete::RecentTarget &left,
                          const AutoComplete::RecentTarget &right) {
  if (ShouldResortRecentNamesOnRecency() && left.timestamp != right.timestamp) {
    return left.timestamp > right.timestamp;
  }

  const auto max_count = std::max(left.name.size(), right.name.size()) + std::size_t{1};
  return openwow::core::SStrCmpNoCaseCollate(left.name.c_str(), right.name.c_str(), max_count) < 0;
}

template <typename Predicate>
void ClearRecentTargetContextBits(std::vector<AutoComplete::RecentTarget> &targets,
                                  const Predicate &predicate,
                                  const AutoComplete::ContextBits context_bits,
                                  const RecentTargetPruneMode prune_mode) {
  const bool prune_empty_target = prune_mode == RecentTargetPruneMode::kAlways ||
                                  ShouldUseLuaContextFilter();

  targets.erase(std::remove_if(targets.begin(), targets.end(),
                               [&](AutoComplete::RecentTarget &target) {
                                 if (!predicate(target)) {
                                   return false;
                                 }

                                 target.context_bits &= ~context_bits;
                                 return prune_empty_target &&
                                        (target.context_bits &
                                         ~AutoComplete::ContextBits{0x20}) == 0;
                               }),
                targets.end());
}

}

AutoComplete::AutoComplete() : timestamp_provider_(&ReadRecentTargetTimestamp) {}

AutoComplete &AutoComplete::Get() {
  static AutoComplete instance;
  return instance;
}

void AutoComplete::AddPlayerName(const std::string &name, bool is_friend, bool is_guild) {
  AddPlayerNameWithContextBits(name, 0, is_friend, is_guild);
}

void AutoComplete::AddPlayerNameWithContextBits(const std::string &name, ContextBits context_bits,
                                                bool is_friend, bool is_guild) {
  if (name.empty())
    return;
  std::lock_guard<std::mutex> lock(mutex_);
  auto *entry = FindEntry(name);
  if (entry) {
    entry->is_friend = entry->is_friend || is_friend;
    entry->is_guild = entry->is_guild || is_guild;
    entry->context_bits |= context_bits;
    entry->hit_count++;
  } else {
    entries_.push_back({name, is_friend, is_guild, false, context_bits, 1});
  }
}

void AutoComplete::AddChannelName(const std::string &name) {
  AddChannelNameWithContextBits(name, 0);
}

void AutoComplete::AddChannelNameWithContextBits(const std::string &name,
                                                 ContextBits context_bits) {
  if (name.empty())
    return;
  std::lock_guard<std::mutex> lock(mutex_);
  auto *entry = FindEntry(name);
  if (entry) {
    entry->is_channel = true;
    entry->context_bits |= context_bits;
    entry->hit_count++;
  } else {
    entries_.push_back({name, false, false, true, context_bits, 1});
  }
}

void AutoComplete::AddFromWhoResults(const std::vector<std::string> &names) {
  for (const auto &name : names) {
    AddPlayerName(name);
  }
}

void AutoComplete::AddFromGuildRoster(const std::vector<std::string> &names) {
  for (const auto &name : names) {
    AddPlayerName(name, false, true);
  }
}

std::vector<std::string> AutoComplete::GetCompletions(const std::string &prefix,
                                                      std::size_t max_results) const {
  return GetCompletions(prefix, Context::kAny, max_results);
}

std::vector<std::string> AutoComplete::GetCompletions(const std::string &prefix, Context context,
                                                      std::size_t max_results) const {
  if (prefix.empty())
    return {};

  std::lock_guard<std::mutex> lock(mutex_);

  struct Match {
    const NameEntry *entry;
  };
  std::vector<Match> matches;
  matches.reserve(entries_.size());

  for (const auto &entry : entries_) {
    if (PrefixMatch(entry.name, prefix) && MatchesContext(entry, context)) {
      matches.push_back({&entry});
    }
  }

  std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
    if (a.entry->hit_count != b.entry->hit_count) {
      return a.entry->hit_count > b.entry->hit_count;
    }
    return a.entry->name < b.entry->name;
  });

  if (matches.size() > max_results) {
    matches.resize(max_results);
  }

  std::vector<std::string> result;
  result.reserve(matches.size());
  for (const auto &m : matches) {
    result.push_back(m.entry->name);
  }
  return result;
}

std::vector<std::string> AutoComplete::GetLuaCompletions(const std::string &prefix,
                                                         ContextBits include_bits,
                                                         ContextBits exclude_bits,
                                                         std::size_t max_results) const {
  if (prefix.empty() || max_results == 0)
    return {};

  const bool use_context_filter = ShouldUseLuaContextFilter();

  std::lock_guard<std::mutex> lock(mutex_);

  struct Match {
    const NameEntry *entry;
  };
  std::vector<Match> matches;
  matches.reserve(entries_.size());

  for (const auto &entry : entries_) {
    if (!PrefixMatch(entry.name, prefix)) {
      continue;
    }
    if (use_context_filter) {
      if ((entry.context_bits & exclude_bits) != 0) {
        continue;
      }
      if ((entry.context_bits & include_bits) == 0) {
        continue;
      }
    }
    matches.push_back({&entry});
  }

  std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
    if (a.entry->hit_count != b.entry->hit_count) {
      return a.entry->hit_count > b.entry->hit_count;
    }
    return a.entry->name < b.entry->name;
  });

  if (matches.size() > max_results) {
    matches.resize(max_results);
  }

  std::vector<std::string> result;
  result.reserve(matches.size());
  for (const auto &match : matches) {
    result.push_back(match.entry->name);
  }
  return result;
}

void AutoComplete::TouchRecentPlayerGuid(const std::uint64_t raw_guid,
                                         const ContextBits context_bits,
                                         const bool update_timestamp, std::string_view name) {
  TouchRecentTarget(RecentTargetKind::kPlayerGuid, raw_guid, context_bits, update_timestamp, name);
}

void AutoComplete::TouchRecentPresenceId(const std::int32_t presence_id,
                                         const ContextBits context_bits,
                                         const bool update_timestamp, std::string_view name) {
  const auto identifier = static_cast<std::uint32_t>(presence_id);

  TouchRecentTarget(RecentTargetKind::kPresenceId, identifier, context_bits | ContextBits{0x8},
                    update_timestamp, name);
}

void AutoComplete::TouchRecentTarget(const RecentTargetKind kind, const std::uint64_t identifier,
                                     const ContextBits context_bits,
                                     const bool update_timestamp, std::string_view name) {
  if (identifier == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto *target = FindRecentTarget(kind, identifier);
  if (target == nullptr) {
    RecentTarget recent_target;
    recent_target.kind = kind;
    recent_target.identifier = identifier;
    recent_target.context_bits = context_bits;
    recent_target.name.assign(name);
    if (update_timestamp) {
      recent_target.timestamp = timestamp_provider_();
    }
    recent_targets_.push_back(std::move(recent_target));
  } else {
    target->context_bits |= context_bits;
    if (update_timestamp) {
      target->timestamp = timestamp_provider_();
    }
    if (!name.empty() && target->name.empty()) {
      target->name.assign(name);
    }
  }

  std::stable_sort(recent_targets_.begin(), recent_targets_.end(), RecentTargetPrecedes);
}

bool AutoComplete::UpdateRecentPlayerGuidName(const std::uint64_t raw_guid, std::string_view name,
                                              const bool overwrite_existing) {
  return UpdateRecentTargetName(RecentTargetKind::kPlayerGuid, raw_guid, name, overwrite_existing);
}

bool AutoComplete::UpdateRecentPresenceIdName(const std::int32_t presence_id, std::string_view name,
                                              const bool overwrite_existing) {
  return UpdateRecentTargetName(RecentTargetKind::kPresenceId,
                                static_cast<std::uint32_t>(presence_id), name,
                                overwrite_existing);
}

bool AutoComplete::UpdateRecentTargetName(const RecentTargetKind kind,
                                          const std::uint64_t identifier, std::string_view name,
                                          const bool overwrite_existing) {
  if (identifier == 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto *target = FindRecentTarget(kind, identifier);
  if (target == nullptr) {
    return false;
  }
  if (!target->name.empty() && !overwrite_existing) {
    return false;
  }

  target->name.assign(name);
  std::stable_sort(recent_targets_.begin(), recent_targets_.end(), RecentTargetPrecedes);
  return true;
}

void AutoComplete::ClearRecentPlayerNameContextBits(const std::string_view name,
                                                    const ContextBits context_bits) {
  if (name.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  ClearRecentTargetContextBits(
      recent_targets_,
      [&](const RecentTarget &target) {
        return target.kind == RecentTargetKind::kPlayerGuid &&
               EqualsUtf8NoCase(target.name, name);
      },
      context_bits, RecentTargetPruneMode::kWhenContextFilteringEnabled);
}

void AutoComplete::ClearRecentPlayerGuidContextBits(const std::uint64_t raw_guid,
                                                    const ContextBits context_bits) {
  if (raw_guid == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  ClearRecentTargetContextBits(
      recent_targets_,
      [raw_guid](const RecentTarget &target) {
        return target.kind == RecentTargetKind::kPlayerGuid && target.identifier == raw_guid;
      },
      context_bits, RecentTargetPruneMode::kAlways);
}

void AutoComplete::ClearRecentPlayerContextBits(const ContextBits context_bits) {
  std::lock_guard<std::mutex> lock(mutex_);

  ClearRecentTargetContextBits(recent_targets_, [](const RecentTarget &) { return true; },
                               context_bits,
                               RecentTargetPruneMode::kWhenContextFilteringEnabled);
}

void AutoComplete::ClearRecentPresenceIdContextBits(const std::int32_t presence_id,
                                                    const ContextBits context_bits) {
  const auto identifier = static_cast<std::uint32_t>(presence_id);
  if (identifier == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  ClearRecentTargetContextBits(
      recent_targets_,
      [identifier](const RecentTarget &target) {
        return target.kind == RecentTargetKind::kPresenceId && target.identifier == identifier;
      },
      context_bits, RecentTargetPruneMode::kAlways);
}

void AutoComplete::ClearRecentPresenceId(const std::int32_t presence_id) {
  const auto identifier = static_cast<std::uint32_t>(presence_id);
  if (identifier == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  recent_targets_.erase(
      std::remove_if(recent_targets_.begin(), recent_targets_.end(),
                     [identifier](const RecentTarget &target) {
                       return target.kind == RecentTargetKind::kPresenceId &&
                              target.identifier == identifier;
                     }),
      recent_targets_.end());
}

void AutoComplete::ClearRecentPresenceTargets() {
  std::lock_guard<std::mutex> lock(mutex_);
  recent_targets_.erase(
      std::remove_if(recent_targets_.begin(), recent_targets_.end(), [](const RecentTarget &target) {
        return target.kind == RecentTargetKind::kPresenceId;
      }),
      recent_targets_.end());
}

void AutoComplete::SetTimestampProviderForTesting(const TimestampProvider provider) {
  std::lock_guard<std::mutex> lock(mutex_);
  timestamp_provider_ = provider != nullptr ? provider : &ReadRecentTargetTimestamp;
}

void AutoComplete::ResetTimestampProviderForTesting() {
  SetTimestampProviderForTesting(&ReadRecentTargetTimestamp);
}

std::optional<std::int32_t>
AutoComplete::GetRecentPresenceIdForName(const std::string_view name) const {
  if (name.empty()) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto match = std::find_if(recent_targets_.begin(), recent_targets_.end(),
                                  [&](const RecentTarget &target) {
                                    return target.kind == RecentTargetKind::kPresenceId &&
                                           EqualsUtf8NoCase(target.name, name);
                                  });
  if (match == recent_targets_.end()) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(match->identifier));
}

std::vector<std::string> AutoComplete::GetRecentLuaCompletions(
    const std::string_view text, const ContextBits include_bits, const ContextBits exclude_bits,
    const std::size_t max_results, const std::size_t cursor_position,
    const bool allow_full_match) const {
  return GetRecentLuaCompletionsForKind(text, include_bits, exclude_bits, max_results,
                                        cursor_position, allow_full_match, std::nullopt);
}

std::vector<std::string> AutoComplete::GetRecentPlayerLuaCompletions(
    const std::string_view text, const ContextBits include_bits, const ContextBits exclude_bits,
    const std::size_t max_results, const std::size_t cursor_position,
    const bool allow_full_match) const {
  return GetRecentLuaCompletionsForKind(text, include_bits, exclude_bits, max_results,
                                        cursor_position, allow_full_match,
                                        RecentTargetKind::kPlayerGuid);
}

std::vector<std::string> AutoComplete::GetRecentPresenceLuaCompletions(
    const std::string_view text, const ContextBits include_bits, const ContextBits exclude_bits,
    const std::size_t max_results, const std::size_t cursor_position,
    const bool allow_full_match) const {
  return GetRecentLuaCompletionsForKind(text, include_bits, exclude_bits, max_results,
                                        cursor_position, allow_full_match,
                                        RecentTargetKind::kPresenceId);
}

std::vector<std::string> AutoComplete::GetRecentLuaCompletionsForKind(
    const std::string_view text, const ContextBits include_bits, const ContextBits exclude_bits,
    const std::size_t max_results, const std::size_t cursor_position,
    const bool allow_full_match, const std::optional<RecentTargetKind> kind) const {
  std::vector<std::string> results;
  if (text.empty() || max_results == 0) {
    return results;
  }

  const bool use_context_filter = ShouldUseLuaContextFilter();
  const auto* const manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  const auto* const session = manager != nullptr ? manager->world_session() : nullptr;
  const auto active_player_guid = session != nullptr
                                      ? session->objects().GetActivePlayerGuid().GetRawValue()
                                      : 0;

  std::lock_guard<std::mutex> lock(mutex_);
  results.reserve(std::min(max_results, recent_targets_.size()));
  for (const auto &target : recent_targets_) {
    if ((kind.has_value() && target.kind != *kind) || target.identifier == 0 ||
        target.identifier == active_player_guid || target.name.empty()) {
      continue;
    }
    if (use_context_filter) {
      if ((target.context_bits & exclude_bits) != 0) {
        continue;
      }
      if ((target.context_bits & include_bits) == 0) {
        continue;
      }
    }
    if (!MatchesRecentPlayerQuery(text, target.name, cursor_position, allow_full_match)) {
      continue;
    }

    results.push_back(target.name);
    if (results.size() == max_results) {
      break;
    }
  }
  return results;
}

void AutoComplete::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  recent_targets_.clear();
}

std::size_t AutoComplete::GetNumEntries() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size() + recent_targets_.size();
}

AutoComplete::NameEntry *AutoComplete::FindEntry(const std::string &name) {
  for (auto &entry : entries_) {
    if (entry.name.size() == name.size()) {
      bool match = true;
      for (std::size_t i = 0; i < name.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(entry.name[i])) !=
            std::tolower(static_cast<unsigned char>(name[i]))) {
          match = false;
          break;
        }
      }
      if (match)
        return &entry;
    }
  }
  return nullptr;
}

const AutoComplete::NameEntry *AutoComplete::FindEntry(const std::string &name) const {
  return const_cast<AutoComplete *>(this)->FindEntry(name);
}

AutoComplete::RecentTarget *AutoComplete::FindRecentTarget(const RecentTargetKind kind,
                                                           const std::uint64_t identifier) {
  for (auto &target : recent_targets_) {
    if (target.kind == kind && target.identifier == identifier) {
      return &target;
    }
  }
  return nullptr;
}

const AutoComplete::RecentTarget *
AutoComplete::FindRecentTarget(const RecentTargetKind kind,
                               const std::uint64_t identifier) const {
  return const_cast<AutoComplete *>(this)->FindRecentTarget(kind, identifier);
}

bool AutoComplete::PrefixMatch(const std::string &name, const std::string &prefix) {
  if (prefix.size() > name.size())
    return false;
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(name[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

bool AutoComplete::MatchesContext(const NameEntry &entry, Context context) {
  switch (context) {
  case Context::kAny:
    return true;
  case Context::kGuild:
    return entry.is_guild;
  case Context::kChat:
  case Context::kMail:
  case Context::kInvite:
  case Context::kTrade:

    return true;
  }
  return true;
}

}
