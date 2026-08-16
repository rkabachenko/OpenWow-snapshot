#include "openwow/game/inventory/loot/adapters/ui/loot_roll_result_presenter.h"

#include <memory>
#include <mutex>
#include <utility>

#include "openwow/core/localized_format.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/ui/game/cvar_system.h"

namespace openwow::game {

namespace {

constexpr bool IsChoiceOnly(std::uint8_t roll_number) {
  return static_cast<std::int8_t>(roll_number) < 0;
}

}

std::string FormatLootRollResultMessage(
    Localization& localization, const LootRollResultDisplayParams& params) {
  char buf[3000];

  const bool is_self = (params.roller_guid != 0 &&
                        params.roller_guid == params.active_player_guid);
  if (!params.detailed_loot_messages &&
      !(is_self && params.auto_pass)) {
    return {};
  }
  const auto roll_type = params.roll_type;
  const bool choice_only = IsChoiceOnly(params.roll_number);
  const int roll_value = static_cast<std::int8_t>(params.roll_number);

  auto& loc = localization;
  if (is_self) {

    if (roll_type == GroupRollType::kGreed && choice_only) {
      const auto fmt = loc.GetString("LOOT_ROLL_GREED_SELF",
                                      "You have selected Greed for: %s");
      core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.item_link.c_str());
      return buf;
    }
    if (roll_type == GroupRollType::kDisenchant && choice_only) {
      const auto fmt = loc.GetString("LOOT_ROLL_DISENCHANT_SELF",
                                      "You have selected Disenchant for: %s");
      core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.item_link.c_str());
      return buf;
    }

    if (choice_only) {
      const auto fmt =
          params.auto_pass
              ? loc.GetString("LOOT_ROLL_PASSED_SELF_AUTO",
                              "You automatically passed on: %s because you "
                              "cannot loot that item.")
              : loc.GetString("LOOT_ROLL_PASSED_SELF", "You passed on: %s");
      core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.item_link.c_str());
      return buf;
    }

    if (roll_type == GroupRollType::kPass && roll_value == 0) {
      const auto fmt = loc.GetString("LOOT_ROLL_NEED_SELF",
                                     "You have selected Need for: %s");
      core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.item_link.c_str());
      return buf;
    }

    const char* key = nullptr;
    switch (roll_type) {
      case GroupRollType::kGreed:
        key = "LOOT_ROLL_ROLLED_GREED";
        break;
      case GroupRollType::kDisenchant:
        key = "LOOT_ROLL_ROLLED_DE";
        break;
      default:
        key = "LOOT_ROLL_ROLLED_NEED";
        break;
    }
    const auto fmt = loc.GetString(key, "%s rolled - %d for: %s");
    core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.player_name.c_str(),
                  roll_value, params.item_link.c_str());
    return buf;
  }

  if (roll_type == GroupRollType::kGreed && choice_only) {
    const auto fmt =
        loc.GetString("LOOT_ROLL_GREED", "%s has selected Greed for: %s");
    core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.player_name.c_str(),
                  params.item_link.c_str());
    return buf;
  }
  if (roll_type == GroupRollType::kDisenchant && choice_only) {
    const auto fmt = loc.GetString("LOOT_ROLL_DISENCHANT",
                                    "%s has selected Disenchant for: %s");
    core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.player_name.c_str(),
                  params.item_link.c_str());
    return buf;
  }
  if (choice_only) {
    const auto fmt =
        params.auto_pass
            ? loc.GetString("LOOT_ROLL_PASSED_AUTO",
                            "%s automatically passed on: %s")
            : loc.GetString("LOOT_ROLL_PASSED", "%s passed on: %s");
    core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.player_name.c_str(),
                  params.item_link.c_str());
    return buf;
  }

  if (roll_type == GroupRollType::kPass && roll_value == 0) {
    const auto fmt =
        loc.GetString("LOOT_ROLL_NEED", "%s has selected Need for: %s");
    core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.player_name.c_str(),
                  params.item_link.c_str());
    return buf;
  }

  const char* key = nullptr;
  switch (roll_type) {
    case GroupRollType::kGreed:
      key = "LOOT_ROLL_ROLLED_GREED";
      break;
    case GroupRollType::kDisenchant:
      key = "LOOT_ROLL_ROLLED_DE";
      break;
    default:
      key = "LOOT_ROLL_ROLLED_NEED";
      break;
  }
  const auto fmt = loc.GetString(key, "%s rolled - %d for: %s");
  core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.player_name.c_str(),
                roll_value, params.item_link.c_str());
  return buf;
}

std::string FormatLootRollWinnerMessage(
    Localization& localization, const LootRollResultDisplayParams& params) {
  const bool is_self = params.roller_guid != 0 &&
                       params.roller_guid == params.active_player_guid;
  auto& loc = localization;
  char buf[3000]{};

  if (params.detailed_loot_messages) {
    if (is_self) {
      const auto fmt = loc.GetString("LOOT_ROLL_YOU_WON", "You won: %s");
      core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.item_link.c_str());
    } else {
      const auto fmt = loc.GetString("LOOT_ROLL_WON", "%s won: %s");
      core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.player_name.c_str(),
                    params.item_link.c_str());
    }
    return buf;
  }

  const char* key = nullptr;
  switch (params.roll_type) {
    case GroupRollType::kNeed:
      key = is_self ? "LOOT_ROLL_YOU_WON_NO_SPAM_NEED"
                    : "LOOT_ROLL_WON_NO_SPAM_NEED";
      break;
    case GroupRollType::kGreed:
      key = is_self ? "LOOT_ROLL_YOU_WON_NO_SPAM_GREED"
                    : "LOOT_ROLL_WON_NO_SPAM_GREED";
      break;
    case GroupRollType::kDisenchant:
      key = is_self ? "LOOT_ROLL_YOU_WON_NO_SPAM_DE"
                    : "LOOT_ROLL_WON_NO_SPAM_DE";
      break;
    default:
      return {};
  }

  const int roll_number = static_cast<std::int8_t>(params.roll_number);
  if (is_self) {
    const auto fmt = loc.GetString(key, "You won: %s (%d)");
    core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.item_link.c_str(),
                  roll_number);
  } else {
    const auto fmt = loc.GetString(key, "%s won: %s (%d)");
    core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), params.player_name.c_str(),
                  params.item_link.c_str(), roll_number);
  }
  return buf;
}

namespace {

enum class ResolvedLootMessageKind : std::uint8_t {
  kRoll,
  kWinner,
  kAllPassed,
};

struct PendingLootMessageResolution {
  std::mutex mutex;
  ObjectManager* objects = nullptr;
  QueryCache* queries = nullptr;
  Localization* localization = nullptr;
  ui::game::CVarSystem* cvars = nullptr;
  const data::dbc::DbcLoader* dbc = nullptr;
  ObjectHandle active_player_handle{};
  ResolvedLootMessageKind kind = ResolvedLootMessageKind::kRoll;
  std::uint64_t roller_guid = 0;
  std::uint32_t item_id = 0;
  std::int32_t random_property_id = 0;
  std::int32_t random_suffix = 0;
  std::uint8_t roll_number = 0;
  GroupRollType roll_type = GroupRollType::kPass;
  bool auto_pass = false;
  bool item_ready = false;
  bool name_ready = false;
  bool failed = false;
  bool completed = false;
  std::function<void(bool)> on_complete;
  std::function<void(std::string)> display;
};

bool IsLootMessageContextCurrent(
    const PendingLootMessageResolution& pending) {
  if (pending.objects == nullptr || pending.active_player_handle.guid.IsEmpty()) {
    return false;
  }

  const auto* player = pending.objects->ResolveObjectHandle(
      pending.active_player_handle);
  return player != nullptr &&
         player->GetGuid() == pending.objects->GetActivePlayerGuid();
}

bool DetailedLootMessagesEnabled(const ui::game::CVarSystem& cvars) {
  return !cvars.Exists("showLootSpam") || cvars.GetCVarBool("showLootSpam");
}

std::string BuildResolvedLootItemLink(
    const data::dbc::DbcLoader* dbc, const ItemTemplate& item,
    const PendingLootMessageResolution& pending) {
  const auto display_name = FormatItemDisplayNameWithRandomProperty(
      *pending.localization, dbc, item.name, pending.random_property_id);
  return HyperlinkParser::BuildItemLink(
      pending.item_id, display_name,
      static_cast<std::uint32_t>(item.quality), 0, 0, 0, 0,
      pending.random_property_id, pending.random_suffix, 0, 0);
}

void DisplayResolvedLootMessage(PendingLootMessageResolution& pending) {
  const auto* item = pending.queries->GetItemTemplate(pending.item_id);
  if (item == nullptr) {
    return;
  }

  const std::string item_link =
      BuildResolvedLootItemLink(pending.dbc, *item, pending);
  std::string message;
  if (pending.kind == ResolvedLootMessageKind::kAllPassed) {
    const auto fmt = pending.localization->GetString(
        "LOOT_ROLL_ALL_PASSED", "Everyone passed on: %s");
    char buf[3000]{};
    core::FormatLocalized(buf, sizeof(buf), fmt.c_str(), item_link.c_str());
    message = buf;
  } else {
    const auto* player_name = pending.queries->GetPlayerName(pending.roller_guid);
    LootRollResultDisplayParams params;
    params.player_name = player_name != nullptr ? player_name->name : "";
    params.roller_guid = pending.roller_guid;
    params.item_id = pending.item_id;
    params.item_quality = item->quality;
    params.random_property_id = pending.random_property_id;
    params.random_suffix = pending.random_suffix;
    params.item_link = item_link;
    params.roll_number = pending.roll_number;
    params.roll_type = pending.roll_type;
    params.auto_pass = pending.auto_pass;
    params.active_player_guid =
        pending.objects->GetActivePlayerGuid().GetRawValue();
    params.detailed_loot_messages =
        DetailedLootMessagesEnabled(*pending.cvars);
    message = pending.kind == ResolvedLootMessageKind::kWinner
                  ? FormatLootRollWinnerMessage(*pending.localization, params)
                  : FormatLootRollResultMessage(*pending.localization, params);
  }

  if (!message.empty() && pending.display) {
    pending.display(std::move(message));
  }
}

void TryFinishLootMessageResolution(
    const std::shared_ptr<PendingLootMessageResolution>& pending) {
  bool should_display = false;
  std::function<void(bool)> on_complete;
  {
    std::lock_guard lock(pending->mutex);
    if (pending->completed ||
        (!pending->failed &&
         !(pending->item_ready && pending->name_ready))) {
      return;
    }
    if (!pending->failed && !IsLootMessageContextCurrent(*pending)) {
      pending->failed = true;
    }
    pending->completed = true;
    should_display = !pending->failed;
    on_complete = std::move(pending->on_complete);
  }

  if (should_display) {
    DisplayResolvedLootMessage(*pending);
  }
  if (on_complete) {
    on_complete(should_display);
  }
}

void MarkLootMessageDependency(
    const std::shared_ptr<PendingLootMessageResolution>& pending,
    const bool item_dependency, const bool success) {
  {
    std::lock_guard lock(pending->mutex);
    if (pending->completed) {
      return;
    }
    if (!success) {
      pending->failed = true;
    } else if (item_dependency) {
      pending->item_ready = true;
    } else {
      pending->name_ready = true;
    }
  }
  TryFinishLootMessageResolution(pending);
}

void RequestLootMessagePlayerName(
    const std::shared_ptr<PendingLootMessageResolution>& pending) {
  if (!IsLootMessageContextCurrent(*pending)) {
    MarkLootMessageDependency(pending, false, false);
    return;
  }

  if (pending->kind == ResolvedLootMessageKind::kAllPassed ||
      pending->roller_guid == 0) {
    MarkLootMessageDependency(pending, false, true);
    return;
  }

  const auto* player = pending->queries->GetOrRequestPlayerName(
      pending->roller_guid,
      QueryCache::QueryRequestOptions{
          .dedupe_callbacks = false,
          .callback = [pending](const bool success) {
            MarkLootMessageDependency(pending, false, success);
          }});
  if (player != nullptr) {
    MarkLootMessageDependency(pending, false, true);
  }
}

void ResolveLootMessageItemDependency(
    const std::shared_ptr<PendingLootMessageResolution>& pending,
    const bool success) {
  MarkLootMessageDependency(pending, true, success);
  if (success) {

    RequestLootMessagePlayerName(pending);
  }
}

void BeginLootMessageResolution(
    const std::shared_ptr<PendingLootMessageResolution>& pending) {
  if (pending->item_id == 0 ||
      !IsLootMessageContextCurrent(*pending)) {
    MarkLootMessageDependency(pending, true, false);
    return;
  }

  const auto* item = pending->queries->GetOrRequestItemTemplate(
      pending->item_id,
      QueryCache::QueryRequestOptions{
          .dedupe_callbacks = false,
          .callback = [pending](const bool success) {
            ResolveLootMessageItemDependency(pending, success);
          }});
  if (item != nullptr) {
    ResolveLootMessageItemDependency(pending, true);
  }
}

std::shared_ptr<PendingLootMessageResolution> MakeLootMessageResolution(
    ObjectManager& objects, QueryCache& queries,
    Localization& localization, ui::game::CVarSystem& cvars,
    std::function<void(std::string)> display,
    const data::dbc::DbcLoader* dbc, const ResolvedLootMessageKind kind,
    const std::uint64_t roller_guid, const std::uint32_t item_id,
    const std::uint32_t random_suffix,
    const std::uint32_t random_property_id, const std::uint8_t roll_number,
    const GroupRollType roll_type, const bool auto_pass,
    std::function<void(bool)> on_complete) {
  auto pending = std::make_shared<PendingLootMessageResolution>();
  pending->objects = &objects;
  pending->queries = &queries;
  pending->localization = &localization;
  pending->cvars = &cvars;
  pending->display = std::move(display);
  pending->dbc = dbc;
  if (const auto handle = objects.GetObjectHandle(objects.GetActivePlayerGuid());
      handle.has_value()) {
    pending->active_player_handle = *handle;
  }
  pending->kind = kind;
  pending->roller_guid = roller_guid;
  pending->item_id = item_id;
  pending->random_property_id =
      static_cast<std::int32_t>(random_property_id);
  pending->random_suffix = static_cast<std::int32_t>(random_suffix);
  pending->roll_number = roll_number;
  pending->roll_type = roll_type;
  pending->auto_pass = auto_pass;
  pending->on_complete = std::move(on_complete);
  return pending;
}

}

void ResolveAndDisplayLootRollResult(ObjectManager& objects, QueryCache& queries,
                                     Localization& localization,
                                     ui::game::CVarSystem& cvars,
                                     std::function<void(std::string)> display,
                                     const data::dbc::DbcLoader* dbc,
                                     const GroupLootRollResult& result,
                                     std::function<void(bool)> on_complete) {
  BeginLootMessageResolution(MakeLootMessageResolution(
      objects, queries, localization, cvars, std::move(display), dbc,
      ResolvedLootMessageKind::kRoll, result.roller_guid,
      result.item_id, result.random_suffix, result.random_prop_id,
      result.roll_number, result.roll_type, result.auto_pass,
      std::move(on_complete)));
}

void ResolveAndDisplayLootRollWinner(ObjectManager& objects, QueryCache& queries,
                                     Localization& localization,
                                     ui::game::CVarSystem& cvars,
                                     std::function<void(std::string)> display,
                                     const data::dbc::DbcLoader* dbc,
                                     const LootRollWon& winner,
                                     std::function<void(bool)> on_complete) {
  BeginLootMessageResolution(MakeLootMessageResolution(
      objects, queries, localization, cvars, std::move(display), dbc,
      ResolvedLootMessageKind::kWinner, winner.winner_guid,
      winner.item_id, winner.random_suffix, winner.random_property_id,
      winner.roll_number, static_cast<GroupRollType>(winner.roll_type), false,
      std::move(on_complete)));
}

void ResolveAndDisplayLootAllPassed(ObjectManager& objects, QueryCache& queries,
                                    Localization& localization,
                                    ui::game::CVarSystem& cvars,
                                    std::function<void(std::string)> display,
                                    const data::dbc::DbcLoader* dbc,
                                    const GroupLootAllPassed& all_passed,
                                    std::function<void(bool)> on_complete) {
  BeginLootMessageResolution(MakeLootMessageResolution(
      objects, queries, localization, cvars, std::move(display), dbc,
      ResolvedLootMessageKind::kAllPassed, 0,
      all_passed.item_id,
      all_passed.random_suffix, all_passed.random_prop_id, 0,
      GroupRollType::kPass, false, std::move(on_complete)));
}

}
