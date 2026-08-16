#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/tooltip_internal.h"
#include "openwow/ui/game/tooltip_runtime.h"
#include "openwow/ui/game/tooltip_types.h"
#include "openwow/core/storm_containers.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/client_config.h"
#include "openwow/game/container_slot_mapping.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/quest_dialog_text.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_query_bridge.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/shapeshift_form_resolver.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/commerce/trade/trade_item_location.h"
#include "openwow/game/unit_level_display.h"
#include "openwow/game/world_session.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_craft.h"
#include "openwow/ui/game/api/game_lua_api_quest.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_tradeskill_state.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/quest_special_item.h"
#include "openwow/ui/game/quest_leaderboard_builder.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/trade_cursor_utils.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/game/aura_lua_bridge.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace openwow::ui::game::tooltip_internal {

openwow::ui::game::TooltipSystem& ResolveTooltipState(void* const tooltip) {
  return tooltip != nullptr
             ? *static_cast<openwow::ui::game::TooltipSystem*>(tooltip)
             : openwow::ui::game::TooltipSystem::Get();
}
openwow::core::TSGrowableArray<openwow::ui::game::FrameStackInfo, 8>
    g_frameStackInfoArray;

class AchievementTooltipCriteriaIndex {
 public:
  using Entry = openwow::data::dbc::AchievementCriteriaEntry;

  const std::vector<const Entry*>& Get(
      const openwow::data::dbc::DbcLoader& dbc,
      const std::uint32_t achievement_id) {
    const auto& store = dbc.achievement_criteria();
    const auto& entries = store.entries();
    const Entry* const data = entries.empty() ? nullptr : entries.data();
    if (loader_ != &dbc || data_ != data || size_ != entries.size() ||
        max_id_ != store.max_id()) {
      Rebuild(dbc, data, entries.size(), store.max_id());
    }

    const auto found = by_achievement_.find(achievement_id);
    return found != by_achievement_.end() ? found->second : empty_;
  }

 private:
  void Rebuild(const openwow::data::dbc::DbcLoader& dbc,
               const Entry* const data,
               const std::size_t size,
               const std::uint32_t max_id) {
    by_achievement_.clear();
    for (const auto& criteria : dbc.achievement_criteria()) {
      by_achievement_[criteria.achievement_id].push_back(&criteria);
    }
    for (auto& [achievement_id, criteria] : by_achievement_) {
      (void)achievement_id;
      std::sort(criteria.begin(), criteria.end(),
                [](const Entry* const left, const Entry* const right) {
                  if (left->order != right->order) {
                    return left->order < right->order;
                  }
                  return left->id < right->id;
                });
    }

    loader_ = &dbc;
    data_ = data;
    size_ = size;
    max_id_ = max_id;
  }

  const openwow::data::dbc::DbcLoader* loader_ = nullptr;
  const Entry* data_ = nullptr;
  std::size_t size_ = 0;
  std::uint32_t max_id_ = 0;
  std::unordered_map<std::uint32_t, std::vector<const Entry*>> by_achievement_;
  std::vector<const Entry*> empty_;
};

AchievementTooltipCriteriaIndex& GetAchievementTooltipCriteriaIndex() {
  static AchievementTooltipCriteriaIndex index;
  return index;
}

const std::vector<const openwow::data::dbc::AchievementCriteriaEntry*>&
GetAchievementTooltipCriteria(const openwow::data::dbc::DbcLoader& dbc, std::uint32_t id) {
  return GetAchievementTooltipCriteriaIndex().Get(dbc, id);
}

const openwow::data::dbc::DbcLoader *ResolveTooltipDbcLoader() {
  const auto* const session = openwow::ui::game::TooltipSystem::Get().GetWorldSession();
  if (session != nullptr) {
    return session->GetDbcLoader();
  }

  return openwow::ui::game::TooltipSystem::Get().GetDbcLoader();
}

openwow::game::WorldSession *ResolveTooltipWorldSession() {
  return openwow::ui::game::TooltipSystem::Get().GetWorldSession();
}

constexpr std::size_t kTooltipComparisonStateSize = sizeof(openwow::game::ItemStatTable);
static_assert(kTooltipComparisonStateSize == 0x12Cu);

std::uint32_t ReadTooltipComparisonU32(const void* base, const std::size_t offset) {
  std::uint32_t value = 0;
  if (base != nullptr) {
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
  }
  return value;
}

std::int32_t ReadTooltipComparisonS32(const void* base, const std::size_t offset) {
  std::int32_t value = 0;
  if (base != nullptr) {
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
  }
  return value;
}

std::uint64_t ReadTooltipComparisonU64(const void* base, const std::size_t offset) {
  std::uint64_t value = 0;
  if (base != nullptr) {
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
  }
  return value;
}

std::uint32_t ResolveComparisonScalingLevelToken(const float scale) {
  return scale > 0.0f ? static_cast<std::uint32_t>(scale) : 0u;
}

bool BuildComparisonStatTableFromTemplate(openwow::game::ItemStatTable& stat_table,
                                          const openwow::game::ItemTemplate& item_template,
                                          const std::int32_t random_property_id,
                                          const std::uint32_t suffix_factor,
                                          const std::uint32_t scaling_level,
                                          const openwow::game::CGPlayer_C& active_player) {
  return openwow::game::BuildItemStatTableFromTemplateInfo(
      item_template, random_property_id, suffix_factor, ResolveTooltipDbcLoader(), scaling_level,
      active_player, stat_table);
}

bool BuildComparisonStatTableFromItemObject(openwow::game::ItemStatTable& stat_table,
                                            const openwow::game::CGItem_C& item,
                                            const std::uint32_t scaling_level) {
  const auto* item_template = item.GetOrRequestQueryItemTemplate();
  if (item_template == nullptr) {
    return false;
  }

  auto* session = ResolveTooltipWorldSession();
  const auto* player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (player == nullptr) {
    return false;
  }

  return BuildComparisonStatTableFromTemplate(stat_table, *item_template, item.GetRandomPropertiesId(),
                                              item.GetItemSuffixFactor(), scaling_level, *player);
}

bool ResolveTooltipComparisonCacheKey(const void* tooltip_data,
                                      TooltipComparisonCacheKey& out_key) {
  out_key = {};
  if (tooltip_data == nullptr) {
    return false;
  }

  const auto item_guid =
      openwow::game::ObjectGuid(ReadTooltipComparisonU64(tooltip_data, kTooltipCompareItemGuidOffset));
  if (!item_guid.IsEmpty()) {
    if (const auto* session = ResolveTooltipWorldSession(); session != nullptr) {
      if (const auto* item = session->objects().GetItem(item_guid); item != nullptr) {
        out_key.item_entry = item->GetEntry();
        out_key.random_property_id = item->GetRandomPropertiesId();
        out_key.suffix_factor = item->GetItemSuffixFactor();
        out_key.scaling_level =
            ReadTooltipComparisonU32(tooltip_data, kTooltipCompareLevelOffset);
        return out_key.item_entry != 0u;
      }
    }
  }

  out_key.item_entry = ReadTooltipComparisonU32(tooltip_data, kTooltipCompareItemEntryOffset);
  out_key.random_property_id =
      ReadTooltipComparisonS32(tooltip_data, kTooltipCompareRandomPropertyOffset);
  out_key.suffix_factor =
      ReadTooltipComparisonU32(tooltip_data, kTooltipCompareSuffixFactorOffset);
  out_key.scaling_level = ReadTooltipComparisonU32(tooltip_data, kTooltipCompareLevelOffset);
  return out_key.item_entry != 0u;
}

const openwow::game::CreatureTemplateInfo *ResolveTooltipCreatureTemplate(
    const openwow::game::CGUnit_C &unit) {
  const auto *session = ResolveTooltipWorldSession();
  if (session == nullptr || unit.IsPlayer()) {
    return nullptr;
  }

  const auto entry = unit.GetEntry();
  if (entry == 0) {
    return nullptr;
  }

  return session->query_cache().GetCreatureTemplate(entry);
}

const openwow::data::dbc::CreatureFamilyEntry *ResolveTooltipCreatureFamily(
    const openwow::game::CGUnit_C &unit,
    const openwow::data::dbc::DbcLoader *dbc,
    const openwow::game::CreatureTemplateInfo *creature_template) {
  if (dbc == nullptr || creature_template == nullptr || creature_template->creature_family == 0 ||
      unit.IsPlayer()) {
    return nullptr;
  }

  return dbc->creature_family().LookupEntry(creature_template->creature_family);
}

bool InventoryContainsEquipmentSetItem(const openwow::game::PlayerInventoryReplica& inventory,
                                       const std::uint64_t item_guid, const bool bank_open) {
  const auto absolute_slot = inventory.FindSlotByGuid(item_guid);
  if (absolute_slot >= openwow::game::InventorySlots::kBackpackStart &&
      absolute_slot < openwow::game::InventorySlots::kBackpackEnd) {
    return true;
  }

  if (bank_open && absolute_slot >= openwow::game::InventorySlots::kBankStart &&
      absolute_slot < openwow::game::InventorySlots::kBankEnd) {
    return true;
  }

  for (std::uint8_t bag_index = 1; bag_index <= openwow::game::PlayerInventoryReplica::kMaxBags;
       ++bag_index) {
    const auto* bag = inventory.GetBag(bag_index);
    if (bag == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag->num_slots; ++slot) {
      const auto* item = inventory.GetBagSlot(bag_index, slot);
      if (item != nullptr && item->guid == item_guid && !item->IsEmpty()) {
        return true;
      }
    }
  }

  if (!bank_open) {
    return false;
  }

  for (std::uint8_t bag_index = 0; bag_index < openwow::game::PlayerInventoryReplica::kMaxBankBags;
       ++bag_index) {
    const auto* bag = inventory.GetBankBag(bag_index);
    if (bag == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag->num_slots; ++slot) {
      const auto* item = inventory.GetBankBagSlot(bag_index, slot);
      if (item != nullptr && item->guid == item_guid && !item->IsEmpty()) {
        return true;
      }
    }
  }

  return false;
}

}
