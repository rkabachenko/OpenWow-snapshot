
#include "openwow/ui/game/quest_log_interleaved.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/quest_manager.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"

#include <algorithm>
#include <limits>
#include <map>
#include <string_view>

namespace openwow::ui::game::detail {

std::int32_t DecodeQuestSortKey(const std::uint32_t raw_sort_key) {
  return static_cast<std::int32_t>(raw_sort_key);
}

namespace {
constexpr std::size_t kQuestLogHeaderCollapseSlotLimit = 25;
constexpr std::uint32_t kQuestLogAllHeadersExpandedMask =
    std::numeric_limits<std::uint32_t>::max();
constexpr char kQuestLogCollapseFilterCVarName[] = "questLogCollapseFilter";
constexpr char kQuestLogCollapseFilterCVarDescription[] =
    "bit field for saving off the state of the headers in Quest Log";

void EnsureQuestLogCollapseFilterCVar() {
  auto &cvars = CVarSystem::Instance();
  if (cvars.Exists(kQuestLogCollapseFilterCVarName)) {
    return;
  }

  cvars.RegisterCVar(kQuestLogCollapseFilterCVarName, "0", CVarFlags::Archive,
                     kQuestLogCollapseFilterCVarDescription);
  (void)cvars.SetCVar(kQuestLogCollapseFilterCVarName, "-1", true);
}

std::uint32_t GetQuestLogHeaderVisibilityMask() {
  EnsureQuestLogCollapseFilterCVar();
  return static_cast<std::uint32_t>(
      CVarSystem::Instance().GetCVarInt(kQuestLogCollapseFilterCVarName));
}

bool IsQuestLogHeaderCollapsed(const std::size_t sort_slot) {
  if (sort_slot >= kQuestLogHeaderCollapseSlotLimit) {
    return false;
  }

  const std::uint32_t header_bit = std::uint32_t{1} << sort_slot;
  return (GetQuestLogHeaderVisibilityMask() & header_bit) == 0;
}

std::string_view LookupQuestSortDisplayName(const openwow::data::dbc::DbcLoader *dbc,
                                            const std::int32_t sort_key) {
  if (dbc == nullptr) {
    return {};
  }

  if (sort_key > 0) {
    const auto *area = dbc->area_table().LookupEntry(static_cast<std::uint32_t>(sort_key));
    return area != nullptr ? area->name : std::string_view{};
  }

  if (sort_key < 0) {
    const auto *quest_sort = dbc->quest_sort().LookupEntry(static_cast<std::uint32_t>(-sort_key));
    return quest_sort != nullptr ? quest_sort->name : std::string_view{};
  }

  return {};
}

int CompareQuestSortKeys(const std::int32_t lhs, const std::int32_t rhs,
                         const openwow::data::dbc::DbcLoader *dbc) {
  if (lhs == rhs) {
    return 0;
  }
  if (lhs == 0) {
    return -1;
  }
  if (rhs == 0) {
    return 1;
  }

  const auto lhs_name = LookupQuestSortDisplayName(dbc, lhs);
  const auto rhs_name = LookupQuestSortDisplayName(dbc, rhs);
  return ::openwow::core::SStrCmpNoCaseCollate(lhs_name.empty() ? "" : lhs_name.data(),
                                               rhs_name.empty() ? "" : rhs_name.data(),
                                               0x7FFFFFFFu);
}

std::string BuildQuestLogHeaderTitle(const openwow::data::dbc::DbcLoader *dbc,
                                     const std::int32_t sort_key) {
  if (sort_key > 0) {
    if (const auto *area = dbc != nullptr
                               ? dbc->area_table().LookupEntry(static_cast<std::uint32_t>(sort_key))
                               : nullptr) {
      return std::string(area->name);
    }
    return {};
  }

  if (sort_key < 0) {
    if (const auto *quest_sort =
            dbc != nullptr ? dbc->quest_sort().LookupEntry(static_cast<std::uint32_t>(-sort_key))
                           : nullptr) {
      return std::string(quest_sort->name);
    }
    return {};
  }

  return "Missing header! (quest designers)";
}

}

int ResolveQuestTemplateDisplayLevel(const ::openwow::game::QuestTemplate &quest_template,
                                     const ::openwow::game::CGPlayer_C *player) {
  if (quest_template.quest_level != -1 || player == nullptr) {
    return quest_template.quest_level;
  }

  return static_cast<int>(player->State().GetLevel());
}

namespace {

std::vector<std::int32_t> BuildQuestSortOrder(const ::openwow::game::WorldSession &session,
                                              const std::vector<std::int32_t> &sort_keys) {
  const auto *dbc = session.GetDbcLoader();
  auto ordered_sort_keys = sort_keys;
  std::stable_sort(ordered_sort_keys.begin(), ordered_sort_keys.end(),
                   [dbc](const std::int32_t lhs, const std::int32_t rhs) {
                     return CompareQuestSortKeys(lhs, rhs, dbc) < 0;
                   });
  return ordered_sort_keys;
}

}

std::vector<InterleavedQuestLogEntry>
BuildInterleavedQuestLog(::openwow::game::WorldSession &session) {
  return BuildQuestLogView(session).entries;
}

QuestLogView BuildQuestLogView(::openwow::game::WorldSession &session) {
  const auto &log = session.quests().quest_log();
  if (log.empty()) {
    return {};
  }

  struct QuestBucket {
    std::vector<std::size_t> resolved_indices;
  };

  std::map<std::int32_t, QuestBucket> buckets;
  std::vector<std::int32_t> discovered_sort_keys;
  discovered_sort_keys.reserve(log.size());
  const auto *player = session.objects().GetLocalPlayerTyped();

  for (std::size_t quest_log_index = 0; quest_log_index < log.size(); ++quest_log_index) {
    const auto *tmpl = session.quests().GetTemplate(log[quest_log_index].quest_id);
    if (tmpl == nullptr) {
      continue;
    }

    const auto sort_key = DecodeQuestSortKey(tmpl->zone_or_sort);
    auto [bucket_it, inserted] = buckets.try_emplace(sort_key);
    if (inserted) {
      discovered_sort_keys.push_back(sort_key);
    }
    bucket_it->second.resolved_indices.push_back(quest_log_index);
  }

  QuestLogView view;
  view.sort_order = BuildQuestSortOrder(session, discovered_sort_keys);
  view.entries.reserve(log.size() + view.sort_order.size());
  std::vector<InterleavedQuestLogEntry> hidden_entries;

  for (std::size_t sort_slot = 0; sort_slot < view.sort_order.size(); ++sort_slot) {
    const auto sort_key = view.sort_order[sort_slot];
    auto bucket_it = buckets.find(sort_key);
    if (bucket_it == buckets.end()) {
      continue;
    }

    auto &bucket = bucket_it->second;
    std::stable_sort(bucket.resolved_indices.begin(), bucket.resolved_indices.end(),
                     [&session, &log, player](const std::size_t lhs, const std::size_t rhs) {
                       const auto *lhs_template = session.quests().GetTemplate(log[lhs].quest_id);
                       const auto *rhs_template = session.quests().GetTemplate(log[rhs].quest_id);
                       if (lhs_template == nullptr || rhs_template == nullptr) {
                         return false;
                       }

                       const int lhs_level =
                           ResolveQuestTemplateDisplayLevel(*lhs_template, player);
                       const int rhs_level =
                           ResolveQuestTemplateDisplayLevel(*rhs_template, player);
                       if (lhs_level != rhs_level) {
                         return lhs_level < rhs_level;
                       }

                       return ::openwow::core::SStrCmpNoCaseCollate(lhs_template->title.c_str(),
                                                                    rhs_template->title.c_str(),
                                                                    0x7FFFFFFFu) < 0;
                     });

    const bool collapsed = IsQuestLogHeaderCollapsed(sort_slot);

    InterleavedQuestLogEntry header;
    header.is_header = true;
    header.collapsed = collapsed;
    header.sort_key = sort_key;
    header.header_name = BuildQuestLogHeaderTitle(session.GetDbcLoader(), sort_key);
    view.entries.push_back(std::move(header));
    ++view.visible_count;

    auto append_quest_entry = [sort_key](std::vector<InterleavedQuestLogEntry> &target,
                                         const std::size_t quest_log_index) {
      InterleavedQuestLogEntry entry;
      entry.sort_key = sort_key;
      entry.quest_log_index = quest_log_index;
      target.push_back(std::move(entry));
    };

    for (const auto quest_log_index : bucket.resolved_indices) {
      ++view.quest_count;
      if (collapsed) {
        append_quest_entry(hidden_entries, quest_log_index);
      } else {
        append_quest_entry(view.entries, quest_log_index);
        ++view.visible_count;
      }
    }
  }

  view.entries.insert(view.entries.end(), hidden_entries.begin(), hidden_entries.end());
  return view;
}

int ResolveInterleavedToQuestIndex(::openwow::game::WorldSession &session,
                                   const int interleaved_1based) {
  if (interleaved_1based < 1) {
    return -1;
  }

  const auto interleaved = BuildInterleavedQuestLog(session);
  const auto index = static_cast<std::size_t>(interleaved_1based - 1);
  if (index >= interleaved.size() || interleaved[index].is_header) {
    return -1;
  }

  return static_cast<int>(interleaved[index].quest_log_index);
}

std::uint32_t ResolveQuestIdFromInterleavedIndex(::openwow::game::WorldSession &session,
                                                 const int interleaved_1based) {
  const int quest_index = ResolveInterleavedToQuestIndex(session, interleaved_1based);
  if (quest_index < 0) {
    return 0;
  }

  const auto &log = session.quests().quest_log();
  return log[static_cast<std::size_t>(quest_index)].quest_id;
}

int FindInterleavedQuestIndexById(::openwow::game::WorldSession &session,
                                  const std::uint32_t quest_id) {
  if (quest_id == 0) {
    return 0;
  }

  const auto interleaved = BuildInterleavedQuestLog(session);
  const auto &log = session.quests().quest_log();
  for (std::size_t index = 0; index < interleaved.size(); ++index) {
    if (interleaved[index].is_header) {
      continue;
    }

    const auto quest_log_index = interleaved[index].quest_log_index;
    if (quest_log_index < log.size() && log[quest_log_index].quest_id == quest_id) {
      return static_cast<int>(index + 1);
    }
  }

  return 0;
}

bool IsVisibleQuestFailedById(::openwow::game::WorldSession &session,
                              const std::uint32_t quest_id) {
  if (quest_id == 0) {
    return false;
  }

  const auto view = BuildQuestLogView(session);
  const auto &log = session.quests().quest_log();
  const auto visible_limit = std::min(view.visible_count, view.entries.size());
  for (std::size_t index = 0; index < visible_limit; ++index) {
    const auto &entry = view.entries[index];
    if (entry.is_header || entry.quest_log_index >= log.size()) {
      continue;
    }

    const auto &quest = log[entry.quest_log_index];
    if (quest.quest_id == quest_id) {
      return quest.status == ::openwow::game::QuestStatus::kFailed;
    }
  }

  return false;
}

std::optional<std::int32_t> ResolveQuestSortKeyFromInterleavedIndex(
    ::openwow::game::WorldSession &session, const int interleaved_1based) {
  if (interleaved_1based < 1) {
    return std::nullopt;
  }

  const auto interleaved = BuildInterleavedQuestLog(session);
  const auto index = static_cast<std::size_t>(interleaved_1based - 1);
  if (index >= interleaved.size()) {
    return std::nullopt;
  }

  const auto &entry = interleaved[index];
  if (entry.is_header) {
    return entry.sort_key;
  }

  const auto &log = session.quests().quest_log();
  if (entry.quest_log_index >= log.size()) {
    return std::nullopt;
  }

  const auto *tmpl = session.quests().GetOrRequestTemplate(log[entry.quest_log_index].quest_id);
  if (tmpl == nullptr) {
    return std::nullopt;
  }

  return DecodeQuestSortKey(tmpl->zone_or_sort);
}

int ResolveQuestSortSlotFromInterleavedIndex(::openwow::game::WorldSession &session,
                                             const int interleaved_1based) {
  const auto sort_key = ResolveQuestSortKeyFromInterleavedIndex(session, interleaved_1based);
  if (!sort_key.has_value()) {
    return -1;
  }

  const auto sort_order = BuildQuestLogView(session).sort_order;
  for (std::size_t index = 0; index < sort_order.size(); ++index) {
    if (sort_order[index] == *sort_key) {
      return static_cast<int>(index);
    }
  }

  return -1;
}

int FindSelectedInterleavedQuestIndex(::openwow::game::WorldSession &session,
                                      const std::uint32_t selected_quest_id) {
  if (selected_quest_id == 0) {
    return -1;
  }

  const auto view = BuildQuestLogView(session);
  const auto &log = session.quests().quest_log();
  const auto visible_limit = std::min(view.visible_count, view.entries.size());

  for (std::size_t index = 0; index < visible_limit; ++index) {
    const auto &entry = view.entries[index];
    if (entry.is_header) {
      continue;
    }

    if (entry.quest_log_index < log.size() &&
        log[entry.quest_log_index].quest_id == selected_quest_id) {
      return static_cast<int>(index);
    }
  }

  return -1;
}

void SetAllQuestLogHeadersCollapsed(const bool collapsed) {
  EnsureQuestLogCollapseFilterCVar();
  (void)CVarSystem::Instance().SetCVar(
      kQuestLogCollapseFilterCVarName,
      std::to_string(static_cast<std::int32_t>(
          collapsed ? 0u : kQuestLogAllHeadersExpandedMask)));
}

void SetQuestLogHeaderCollapsedBySortSlot(const std::size_t sort_slot, const bool collapsed) {
  if (sort_slot >= kQuestLogHeaderCollapseSlotLimit) {
    return;
  }

  auto visibility_mask = GetQuestLogHeaderVisibilityMask();
  const std::uint32_t header_bit = std::uint32_t{1} << sort_slot;
  if (collapsed) {
    visibility_mask &= ~header_bit;
  } else {
    visibility_mask |= header_bit;
  }

  EnsureQuestLogCollapseFilterCVar();
  (void)CVarSystem::Instance().SetCVar(
      kQuestLogCollapseFilterCVarName,
      std::to_string(static_cast<std::int32_t>(visibility_mask)));
}

}
