#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/quest_manager.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/wdb_cache.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/quest_dialog_text.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/reputation_info.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace openwow::game {

using net::wotlk::Opcode;
using net::wotlk::WorldPacket;

QuestManager::QuestManager(
    openwow::data::DBCacheRuntime& db_cache_runtime,
    const data::dbc::DbcLoader& dbc_loader,
    DialogTextExpansionFn expand_dialog_text)
    : db_cache_runtime_(db_cache_runtime),
      dbc_(dbc_loader),
      tick_count_provider_(
          []() { return openwow::core::GameClock::GetTickCount32(); }),
      expand_dialog_text_(std::move(expand_dialog_text)) {}

namespace {

std::string NormalizeQuestFrameTitle(const std::string &title) {
  return title.empty() ? " " : title;
}

constexpr std::size_t kQuestTemplateWdbRecordSize = openwow::data::wdb_format::kRecordSize_Quest;
constexpr std::size_t kQuestTemplateTitleOffset = 0x0B4;
constexpr std::size_t kQuestTemplateTitleSize = 0x200;
constexpr std::size_t kQuestTemplateObjectivesOffset = 0x2B4;
constexpr std::size_t kQuestTemplateObjectivesSize = 0xBB8;
constexpr std::size_t kQuestTemplateDetailsOffset = 0xE6C;
constexpr std::size_t kQuestTemplateDetailsSize = 0xBB8;
constexpr std::size_t kQuestTemplateAreaDescriptionOffset = 0x1A24;
constexpr std::size_t kQuestTemplateAreaDescriptionSize = 0x200;
constexpr std::size_t kQuestTemplateObjectiveTextOffset = 0x1C94;
constexpr std::size_t kQuestTemplateObjectiveTextStride = 0x100;
constexpr std::size_t kQuestTemplateObjectiveTextSize = 0x100;
constexpr std::size_t kQuestTemplateCompletedTextOffset = 0x20A4;
constexpr std::size_t kQuestTemplateCompletedTextSize = 0x800;

void WriteU32(std::vector<std::uint8_t> &record,
              const std::size_t offset,
              const std::uint32_t value) {
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    record[offset + byte] =
        static_cast<std::uint8_t>(value >> (byte * 8));
  }
}

void WriteI32(std::vector<std::uint8_t> &record,
              const std::size_t offset,
              const std::int32_t value) {
  WriteU32(record, offset, static_cast<std::uint32_t>(value));
}

void WriteFloat(std::vector<std::uint8_t> &record,
                const std::size_t offset,
                const float value) {
  WriteU32(record, offset, std::bit_cast<std::uint32_t>(value));
}

void WriteCStringField(std::vector<std::uint8_t> &record,
                       const std::size_t offset,
                       const std::size_t capacity,
                       const std::string &value) {
  const auto copy_len = std::min(value.size(), capacity - 1);
  std::memcpy(record.data() + offset, value.data(), copy_len);
  record[offset + copy_len] = 0;
}

bool ReadU32Field(const std::vector<std::uint8_t> &record,
                  const std::size_t offset,
                  std::uint32_t &value) {
  if (offset > record.size() || sizeof(value) > record.size() - offset) {
    return false;
  }
  value = 0;
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    value |= static_cast<std::uint32_t>(record[offset + byte]) <<
             (byte * 8);
  }
  return true;
}

bool ReadI32Field(const std::vector<std::uint8_t> &record,
                  const std::size_t offset,
                  std::int32_t &value) {
  std::uint32_t bits = 0;
  if (!ReadU32Field(record, offset, bits)) {
    return false;
  }
  value = std::bit_cast<std::int32_t>(bits);
  return true;
}

bool ReadFloatField(const std::vector<std::uint8_t> &record,
                    const std::size_t offset,
                    float &value) {
  std::uint32_t bits = 0;
  if (!ReadU32Field(record, offset, bits)) {
    return false;
  }
  value = std::bit_cast<float>(bits);
  return true;
}

bool ReadCStringField(const std::vector<std::uint8_t> &record,
                      const std::size_t offset,
                      const std::size_t capacity,
                      std::string &value) {
  if (capacity == 0 || offset > record.size() ||
      capacity > record.size() - offset) {
    return false;
  }
  const auto begin = record.begin() + static_cast<std::ptrdiff_t>(offset);
  const auto end = begin + static_cast<std::ptrdiff_t>(capacity);
  const auto terminator = std::find(begin, end, std::uint8_t{0});
  if (terminator == end) {
    return false;
  }
  value.assign(reinterpret_cast<const char *>(&*begin),
               static_cast<std::size_t>(terminator - begin));
  return true;
}

std::optional<QuestTemplate> DeserializeQuestTemplateWdbRecord(
    const std::vector<std::uint8_t> &record) {
  if (record.size() != kQuestTemplateWdbRecordSize) {
    return std::nullopt;
  }

  QuestTemplate quest_template{};
  std::uint32_t flags = 0;
  if (!ReadU32Field(record, 0x0000, quest_template.quest_id) ||
      !ReadU32Field(record, 0x0004, quest_template.quest_method) ||
      !ReadI32Field(record, 0x0008, quest_template.quest_level) ||
      !ReadU32Field(record, 0x000C, quest_template.min_level) ||
      !ReadU32Field(record, 0x0010, quest_template.zone_or_sort) ||
      !ReadU32Field(record, 0x0014, quest_template.type) ||
      !ReadU32Field(record, 0x0018, quest_template.suggested_players) ||
      !ReadU32Field(record, 0x001C,
                    quest_template.required_reputation_faction) ||
      !ReadI32Field(record, 0x0020,
                    quest_template.required_reputation_value) ||
      !ReadU32Field(record, 0x0024,
                    quest_template.required_reputation_faction_max) ||
      !ReadI32Field(record, 0x0028,
                    quest_template.required_reputation_value_max) ||
      !ReadU32Field(record, 0x002C, quest_template.next_quest_in_chain) ||
      !ReadU32Field(record, 0x0030, quest_template.xp_id) ||
      !ReadU32Field(record, 0x0034, quest_template.reward_money) ||
      !ReadU32Field(record, 0x0038, quest_template.rew_money_max_level) ||
      !ReadU32Field(record, 0x003C, quest_template.rew_spell) ||
      !ReadI32Field(record, 0x0040, quest_template.rew_spell_cast) ||
      !ReadU32Field(record, 0x0044, quest_template.rew_honor_addition) ||
      !ReadFloatField(record, 0x0048, quest_template.rew_honor_multiplier) ||
      !ReadU32Field(record, 0x004C, quest_template.src_item_id) ||
      !ReadU32Field(record, 0x0050, flags) ||
      !ReadU32Field(record, 0x00A4, quest_template.poi_continent) ||
      !ReadFloatField(record, 0x00A8, quest_template.poi_x) ||
      !ReadFloatField(record, 0x00AC, quest_template.poi_y) ||
      !ReadU32Field(record, 0x00B0, quest_template.poi_option) ||
      !ReadU32Field(record, 0x2094, quest_template.char_title_id) ||
      !ReadU32Field(record, 0x2098,
                    quest_template.required_player_kills) ||
      !ReadU32Field(record, 0x209C, quest_template.bonus_talents) ||
      !ReadU32Field(record, 0x20A0, quest_template.rew_arena_points) ||
      !ReadU32Field(record, 0x28E0,
                    quest_template.reward_faction_control)) {
    return std::nullopt;
  }
  quest_template.flags = static_cast<QuestFlags>(flags);

  for (int index = 0; index < kQuestRewardsCount; ++index) {
    if (!ReadU32Field(record, 0x0054 + index * 4,
                      quest_template.reward_items[index].item_id) ||
        !ReadU32Field(record, 0x0064 + index * 4,
                      quest_template.reward_items[index].count) ||
        !ReadU32Field(record, 0x1C74 + index * 4,
                      quest_template.item_drop_objectives[index].item_id) ||
        !ReadU32Field(
            record, 0x1C84 + index * 4,
            quest_template.item_drop_objectives[index].required_count)) {
      return std::nullopt;
    }
  }

  for (int index = 0; index < kQuestRewardChoicesCount; ++index) {
    if (!ReadU32Field(record, 0x0074 + index * 4,
                      quest_template.reward_choice_items[index].item_id) ||
        !ReadU32Field(record, 0x008C + index * 4,
                      quest_template.reward_choice_items[index].count) ||
        !ReadU32Field(record, 0x1C44 + index * 4,
                      quest_template.item_objectives[index].item_id) ||
        !ReadU32Field(record, 0x1C5C + index * 4,
                      quest_template.item_objectives[index].required_count)) {
      return std::nullopt;
    }
  }

  for (int index = 0; index < kQuestReputationsCount; ++index) {
    if (!ReadU32Field(record, 0x28A4 + index * 4,
                      quest_template.reward_faction_id[index]) ||
        !ReadI32Field(record, 0x28B8 + index * 4,
                      quest_template.reward_faction_value[index]) ||
        !ReadI32Field(record, 0x28CC + index * 4,
                      quest_template.reward_faction_value_override[index])) {
      return std::nullopt;
    }
  }

  for (int index = 0; index < kQuestObjectivesCount; ++index) {
    if (!ReadI32Field(
            record, 0x1C24 + index * 4,
            quest_template.npc_or_go_objectives[index].creature_or_go) ||
        !ReadU32Field(
            record, 0x1C34 + index * 4,
            quest_template.npc_or_go_objectives[index].required_count) ||
        !ReadCStringField(
            record,
            kQuestTemplateObjectiveTextOffset +
                index * kQuestTemplateObjectiveTextStride,
            kQuestTemplateObjectiveTextSize,
            quest_template.npc_or_go_objectives[index].text)) {
      return std::nullopt;
    }
  }

  if (!ReadCStringField(record, kQuestTemplateTitleOffset,
                        kQuestTemplateTitleSize, quest_template.title) ||
      !ReadCStringField(record, kQuestTemplateObjectivesOffset,
                        kQuestTemplateObjectivesSize,
                        quest_template.objectives) ||
      !ReadCStringField(record, kQuestTemplateDetailsOffset,
                        kQuestTemplateDetailsSize, quest_template.details) ||
      !ReadCStringField(record, kQuestTemplateAreaDescriptionOffset,
                        kQuestTemplateAreaDescriptionSize,
                        quest_template.area_description) ||
      !ReadCStringField(record, kQuestTemplateCompletedTextOffset,
                        kQuestTemplateCompletedTextSize,
                        quest_template.completed_text)) {
    return std::nullopt;
  }

  return quest_template;
}

std::vector<std::uint8_t> SerializeQuestTemplateWdbRecord(const QuestTemplate &quest_template) {
  std::vector<std::uint8_t> record(kQuestTemplateWdbRecordSize, 0);

  WriteU32(record, 0x0000, quest_template.quest_id);
  WriteU32(record, 0x0004, quest_template.quest_method);
  WriteI32(record, 0x0008, quest_template.quest_level);
  WriteU32(record, 0x000C, quest_template.min_level);
  WriteU32(record, 0x0010, quest_template.zone_or_sort);
  WriteU32(record, 0x0014, quest_template.type);
  WriteU32(record, 0x0018, quest_template.suggested_players);
  WriteU32(record, 0x001C, quest_template.required_reputation_faction);
  WriteI32(record, 0x0020, quest_template.required_reputation_value);
  WriteU32(record, 0x0024, quest_template.required_reputation_faction_max);
  WriteI32(record, 0x0028, quest_template.required_reputation_value_max);
  WriteU32(record, 0x002C, quest_template.next_quest_in_chain);
  WriteU32(record, 0x0030, quest_template.xp_id);
  WriteU32(record, 0x0034, quest_template.reward_money);
  WriteU32(record, 0x0038, quest_template.rew_money_max_level);
  WriteU32(record, 0x003C, quest_template.rew_spell);
  WriteI32(record, 0x0040, quest_template.rew_spell_cast);
  WriteU32(record, 0x0044, quest_template.rew_honor_addition);
  WriteFloat(record, 0x0048, quest_template.rew_honor_multiplier);
  WriteU32(record, 0x004C, quest_template.src_item_id);
  WriteU32(record, 0x0050, static_cast<std::uint32_t>(quest_template.flags));
  WriteU32(record, 0x00A4, quest_template.poi_continent);
  WriteFloat(record, 0x00A8, quest_template.poi_x);
  WriteFloat(record, 0x00AC, quest_template.poi_y);
  WriteU32(record, 0x00B0, quest_template.poi_option);
  WriteU32(record, 0x2094, quest_template.char_title_id);
  WriteU32(record, 0x2098, quest_template.required_player_kills);
  WriteU32(record, 0x209C, quest_template.bonus_talents);
  WriteU32(record, 0x20A0, quest_template.rew_arena_points);
  WriteU32(record, 0x28E0, quest_template.reward_faction_control);

  for (int index = 0; index < kQuestRewardsCount; ++index) {
    WriteU32(record, 0x0054 + index * 4, quest_template.reward_items[index].item_id);
    WriteU32(record, 0x0064 + index * 4, quest_template.reward_items[index].count);
    WriteU32(record, 0x1C74 + index * 4, quest_template.item_drop_objectives[index].item_id);
    WriteU32(record, 0x1C84 + index * 4, quest_template.item_drop_objectives[index].required_count);
  }

  for (int index = 0; index < kQuestRewardChoicesCount; ++index) {
    WriteU32(record, 0x0074 + index * 4, quest_template.reward_choice_items[index].item_id);
    WriteU32(record, 0x008C + index * 4, quest_template.reward_choice_items[index].count);
    WriteU32(record, 0x1C44 + index * 4, quest_template.item_objectives[index].item_id);
    WriteU32(record, 0x1C5C + index * 4, quest_template.item_objectives[index].required_count);
  }

  for (int index = 0; index < kQuestReputationsCount; ++index) {
    WriteU32(record, 0x28A4 + index * 4, quest_template.reward_faction_id[index]);
    WriteI32(record, 0x28B8 + index * 4, quest_template.reward_faction_value[index]);
    WriteI32(record, 0x28CC + index * 4, quest_template.reward_faction_value_override[index]);
  }

  for (int index = 0; index < kQuestObjectivesCount; ++index) {
    WriteI32(record, 0x1C24 + index * 4, quest_template.npc_or_go_objectives[index].creature_or_go);
    WriteU32(record, 0x1C34 + index * 4, quest_template.npc_or_go_objectives[index].required_count);
    WriteCStringField(record,
                      kQuestTemplateObjectiveTextOffset + index * kQuestTemplateObjectiveTextStride,
                      kQuestTemplateObjectiveTextSize,
                      quest_template.npc_or_go_objectives[index].text);
  }

  WriteCStringField(record,
                    kQuestTemplateTitleOffset,
                    kQuestTemplateTitleSize,
                    quest_template.title);
  WriteCStringField(record,
                    kQuestTemplateObjectivesOffset,
                    kQuestTemplateObjectivesSize,
                    quest_template.objectives);
  WriteCStringField(record,
                    kQuestTemplateDetailsOffset,
                    kQuestTemplateDetailsSize,
                    quest_template.details);
  WriteCStringField(record,
                    kQuestTemplateAreaDescriptionOffset,
                    kQuestTemplateAreaDescriptionSize,
                    quest_template.area_description);
  WriteCStringField(record,
                    kQuestTemplateCompletedTextOffset,
                    kQuestTemplateCompletedTextSize,
                    quest_template.completed_text);

  return record;
}

void UpdateQuestTemplateWdbEntry(
    openwow::data::DBCacheRuntime& runtime,
    const QuestTemplate &quest_template) {
  auto &cache = runtime.cache();
  cache.UpdateEntry(openwow::data::WDBCacheType::Quest,
                    quest_template.quest_id,
                    SerializeQuestTemplateWdbRecord(quest_template),
                    openwow::data::wdb_format::kVersion_Quest);
  runtime.persistence().SetDirty(openwow::data::WDBCacheType::Quest);
}

void InvalidateQuestTemplateWdbEntry(openwow::data::DBCacheRuntime& runtime,
                                     const std::uint32_t quest_id) {
  auto &cache = runtime.cache();
  if (cache.InvalidateEntry(openwow::data::WDBCacheType::Quest, quest_id)) {
    runtime.persistence().SetDirty(openwow::data::WDBCacheType::Quest);
  }
}

constexpr std::array<std::uint32_t, kQuestReputationsCount> kRewardFactionControlMasks = {
    1u, 2u, 4u, 8u, 16u};

std::int32_t ResolveQuestFactionRewardValue(const openwow::data::dbc::DbcLoader &dbc,
                                            const std::int32_t raw_value) {
  const auto *reward_table = dbc.quest_faction_reward().LookupEntry(raw_value < 0 ? 2u : 1u);
  if (reward_table == nullptr) {
    return 0;
  }

  const auto magnitude = raw_value < 0
                             ? static_cast<std::uint32_t>(-static_cast<std::int64_t>(raw_value))
                             : static_cast<std::uint32_t>(raw_value);
  if (magnitude >= reward_table->difficulty.size()) {
    return 0;
  }

  return reward_table->difficulty[magnitude];
}

std::int32_t GetPositiveSpilloverCap(const std::int32_t standing_index) {
  if (standing_index < 0 || static_cast<std::size_t>(standing_index + 1) >= kStandingMin.size()) {
    return 0;
  }
  return kStandingMin[static_cast<std::size_t>(standing_index + 1)] - 100;
}

std::int32_t GetNegativeSpilloverFloor(const std::int32_t standing_index) {
  if (standing_index < 0 || static_cast<std::size_t>(standing_index) >= kStandingMin.size()) {
    return 0;
  }
  return kStandingMin[static_cast<std::size_t>(standing_index)];
}

}

bool QuestManager::MarkQuestFailedInLog(const std::uint32_t quest_id,
                                        const bool timer_expiration_reported) {
  auto *entry = FindQuestLogEntry(quest_id);
  if (entry == nullptr) {
    return false;
  }

  if (entry->status == QuestStatus::kComplete || entry->status == QuestStatus::kRewarded) {
    return false;
  }

  entry->status = QuestStatus::kFailed;
  if (timer_expiration_reported) {
    entry->timer_expiration_reported = true;
  }
  return true;
}

void QuestManager::ClearActiveDialog() {
  active_details_.reset();
  active_reward_.reset();
  active_request_.reset();
  pending_reward_selection_ = {};
}

void QuestManager::BeginDialogResponse() {
  ClearActiveDialog();
  quest_frame_interaction_state_ = {};
  dialog_action_pending_ = false;
}

void QuestManager::SetQuestFrameInteractionState(const ObjectGuid &interaction_guid,
                                                 const ObjectGuid &secondary_guid,
                                                 const std::uint32_t quest_id) {
  quest_frame_interaction_state_ = {
      .interaction_guid = interaction_guid, .secondary_guid = secondary_guid, .quest_id = quest_id};
}

void QuestManager::ResetDialogState() {
  ClearActiveDialog();
  dialog_text_.Clear();
  quest_frame_interaction_state_ = {};
  dialog_action_pending_ = false;
}

std::string QuestManager::ExpandDialogText(
    const std::string_view text, const bool empty_as_space) const {
  return expand_dialog_text_
             ? expand_dialog_text_(text, empty_as_space)
             : ExpandQuestDialogText(text, empty_as_space);
}

void QuestManager::ShowQuestGreeting(const std::string &greeting) {
  ResetDialogState();
  dialog_text_.greeting_text = ExpandDialogText(greeting, true);
  dialog_text_.has_greeting_text = true;
}

void QuestManager::RecordPendingRewardSelection(const std::uint32_t quest_id,
                                                const std::uint32_t reward_index) {
  pending_reward_selection_ = {};
  if (!active_reward_.has_value() || active_reward_->quest_id != quest_id) {
    return;
  }
  if (reward_index >= active_reward_->reward_choice_items.size()) {
    return;
  }

  const auto &reward_item = active_reward_->reward_choice_items[reward_index];
  if (reward_item.item_id == 0) {
    return;
  }

  pending_reward_selection_ = {
      .quest_id = quest_id,
      .item_id = reward_item.item_id,
  };
}

std::uint32_t QuestManager::TakePendingRewardSelectionItem(const std::uint32_t quest_id) {
  if (pending_reward_selection_.quest_id != quest_id) {
    return 0;
  }

  const auto item_id = pending_reward_selection_.item_id;
  pending_reward_selection_ = {};
  return item_id;
}

void QuestManager::ClearRewardFactionPreview() {
  reward_faction_preview_count_ = 0;
  reward_faction_preview_.fill({});
}

std::int32_t QuestManager::AccumulateRewardFactionPreview(const std::int32_t faction_id,
                                                          const std::int32_t delta,
                                                          const std::int32_t positive_cap,
                                                          const std::int32_t negative_cap,
                                                          const std::int32_t source_faction_id,
                                                          const bool suppress_spillover) {
  const auto *dbc = &dbc_;

  const auto *faction = dbc->faction().LookupEntry(static_cast<std::uint32_t>(faction_id));
  if (faction == nullptr) {
    return 0;
  }

  auto &reputation = ReputationInfo::Get();
  reputation.BindDbc(dbc);

  const auto current = reputation.GetCurrentStanding(faction_id);
  auto target = current + delta;

  if (delta > 0) {
    if (positive_cap >= 0) {
      const auto upper_bound = GetPositiveSpilloverCap(positive_cap);
      if (current > upper_bound) {
        return current;
      }
      if (target >= std::max(current, upper_bound)) {
        target = upper_bound;
      }
    }
  } else {
    if (negative_cap >= 0) {
      const auto lower_bound = GetNegativeSpilloverFloor(negative_cap);
      if (current < lower_bound) {
        return current;
      }
      if (target <= std::min(current, lower_bound)) {
        target = lower_bound;
      }
    }
  }

  if (target == current) {
    return current;
  }

  if (reward_faction_preview_count_ >= reward_faction_preview_.size()) {
    return target;
  }

  reward_faction_preview_[reward_faction_preview_count_++] = {
      .faction_id = faction_id,
      .amount = delta,
  };

  if (suppress_spillover) {
    return target;
  }

  const auto current_faction_id = static_cast<std::int32_t>(faction->id);
  const auto parent_faction_id = static_cast<std::int32_t>(faction->parent_faction_id);
  if (parent_faction_id != 0 && parent_faction_id != source_faction_id) {
    const auto parent_delta =
        static_cast<std::int32_t>(static_cast<float>(delta) * faction->parent_faction_mod1);
    if (parent_delta != 0) {
      AccumulateRewardFactionPreview(parent_faction_id, parent_delta,
                                     static_cast<std::int32_t>(faction->parent_faction_cap1), -1,
                                     current_faction_id, false);
    }
  }

  const auto *child_spillover_factions =
      reputation.FindChildSpilloverFactionIds(current_faction_id);
  if (child_spillover_factions == nullptr) {
    return target;
  }

  for (const auto child_faction_id : *child_spillover_factions) {
    if (static_cast<std::int32_t>(child_faction_id) == source_faction_id) {
      continue;
    }

    const auto *child = dbc->faction().LookupEntry(child_faction_id);
    if (child == nullptr) {
      continue;
    }

    const auto child_delta =
        static_cast<std::int32_t>(static_cast<float>(delta) * child->parent_faction_mod0);
    if (child_delta == 0) {
      continue;
    }

    AccumulateRewardFactionPreview(static_cast<std::int32_t>(child->id), child_delta,
                                   static_cast<std::int32_t>(child->parent_faction_cap0), -1,
                                   current_faction_id, false);
  }

  return target;
}

void QuestManager::RebuildRewardFactionPreview(const std::uint32_t *faction_ids,
                                               const std::int32_t *reward_values,
                                               const std::int32_t *reward_value_overrides,
                                               const std::uint32_t reward_faction_control) {
  ClearRewardFactionPreview();

  const auto *dbc = &dbc_;

  for (std::size_t index = 0; index < kQuestReputationsCount; ++index) {
    const auto faction_id = static_cast<std::int32_t>(faction_ids[index]);
    if (dbc->faction().LookupEntry(faction_ids[index]) == nullptr) {
      continue;
    }

    const auto reward_value = reward_values[index];
    const auto base_reward = ResolveQuestFactionRewardValue(*dbc, reward_value);
    if (base_reward == 0) {
      continue;
    }

    auto delta = reward_value_overrides[index];
    if (delta == 0) {
      delta = base_reward * 100;
    }

    const auto suppress_spillover =
        (reward_faction_control & kRewardFactionControlMasks[index]) != 0;
    AccumulateRewardFactionPreview(faction_id, delta, -1, -1, 0, suppress_spillover);
  }
}

void QuestManager::BuildQuestLogRewardFactionPreview(const std::uint32_t quest_id) {
  if (quest_id == 0) {
    return;
  }

  const auto *tmpl = GetOrRequestTemplate(quest_id);
  if (tmpl == nullptr) {
    return;
  }

  RebuildRewardFactionPreview(tmpl->reward_faction_id, tmpl->reward_faction_value,
                              tmpl->reward_faction_value_override, tmpl->reward_faction_control);
}

void QuestManager::CommitQuestTemplateUpdate(QuestTemplate quest_template) {
  const auto quest_id = quest_template.quest_id;
  template_cache_[quest_id] = std::move(quest_template);
  UpdateQuestTemplateWdbEntry(db_cache_runtime_, template_cache_.at(quest_id));

  auto callbacks = quest_queries_.Resolve(quest_id);
  for (auto &callback : callbacks) {
    callback(true);
  }
}

void QuestManager::InvalidateQuestTemplate(const std::uint32_t quest_id) {
  template_cache_.erase(quest_id);
  InvalidateQuestTemplateWdbEntry(db_cache_runtime_, quest_id);

  auto callbacks = quest_queries_.Resolve(quest_id);
  for (auto &callback : callbacks) {
    callback(false);
  }
}

bool QuestManager::HandleQuestQueryResponse(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  QuestTemplate t;
  if (!r.ReadU32(t.quest_id))
    return false;
  if (!r.ReadU32(t.quest_method))
    return false;
  if (!r.ReadI32(t.quest_level))
    return false;
  std::uint32_t min_level;
  if (!r.ReadU32(min_level))
    return false;
  t.min_level = min_level;

  std::uint32_t zone_or_sort, type_, suggested, rep_fac, rep_val, rep_fac2, rep_val2, next_quest,
      xp_id, rew_money, rew_money_max_level, rew_spell;
  std::int32_t rew_spell_cast;
  std::uint32_t rew_honor;
  float rew_honor_mult;
  std::uint32_t src_item, flags_raw, char_title, players_slain, bonus_talents, rew_arena, unk0;

  if (!r.ReadU32(zone_or_sort) || !r.ReadU32(type_) || !r.ReadU32(suggested) ||
      !r.ReadU32(rep_fac) || !r.ReadU32(rep_val) || !r.ReadU32(rep_fac2) || !r.ReadU32(rep_val2) ||
      !r.ReadU32(next_quest) || !r.ReadU32(xp_id) || !r.ReadU32(rew_money) ||
      !r.ReadU32(rew_money_max_level) || !r.ReadU32(rew_spell) || !r.ReadI32(rew_spell_cast) ||
      !r.ReadU32(rew_honor) || !r.ReadFloat(rew_honor_mult) || !r.ReadU32(src_item) ||
      !r.ReadU32(flags_raw) || !r.ReadU32(char_title) || !r.ReadU32(players_slain) ||
      !r.ReadU32(bonus_talents) || !r.ReadU32(rew_arena) || !r.ReadU32(unk0))
    return false;

  t.zone_or_sort = zone_or_sort;
  t.type = type_;
  t.suggested_players = suggested;
  t.required_reputation_faction = rep_fac;
  t.required_reputation_value = static_cast<std::int32_t>(rep_val);
  t.required_reputation_faction_max = rep_fac2;
  t.required_reputation_value_max = static_cast<std::int32_t>(rep_val2);
  t.next_quest_in_chain = next_quest;
  t.xp_id = xp_id;
  t.reward_money = rew_money;
  t.rew_money_max_level = rew_money_max_level;
  t.rew_spell = rew_spell;
  t.rew_spell_cast = rew_spell_cast;
  t.rew_honor_addition = rew_honor;
  t.rew_honor_multiplier = rew_honor_mult;
  t.src_item_id = src_item;
  t.flags = static_cast<QuestFlags>(flags_raw);
  t.char_title_id = char_title;
  t.required_player_kills = players_slain;
  t.bonus_talents = bonus_talents;
  t.rew_arena_points = rew_arena;
  t.reward_faction_control = unk0;

  for (int i = 0; i < kQuestRewardsCount; ++i)
    if (!r.ReadU32(t.reward_items[i].item_id) || !r.ReadU32(t.reward_items[i].count))
      return false;

  for (int i = 0; i < kQuestRewardChoicesCount; ++i)
    if (!r.ReadU32(t.reward_choice_items[i].item_id) || !r.ReadU32(t.reward_choice_items[i].count))
      return false;

  for (int i = 0; i < kQuestReputationsCount; ++i)
    if (!r.ReadU32(t.reward_faction_id[i]))
      return false;
  for (int i = 0; i < kQuestReputationsCount; ++i)
    if (!r.ReadI32(t.reward_faction_value[i]))
      return false;
  for (int i = 0; i < kQuestReputationsCount; ++i)
    if (!r.ReadI32(t.reward_faction_value_override[i]))
      return false;

  if (!r.ReadU32(t.poi_continent) || !r.ReadFloat(t.poi_x) || !r.ReadFloat(t.poi_y))
    return false;
  if (!r.ReadU32(t.poi_option))
    return false;

  if (!r.ReadCString(t.title) || !r.ReadCString(t.objectives) || !r.ReadCString(t.details) ||
      !r.ReadCString(t.area_description) || !r.ReadCString(t.completed_text))
    return false;

  for (int i = 0; i < kQuestObjectivesCount; ++i) {
    std::int32_t entry;
    std::uint32_t count, item_drop, item_drop_count;
    if (!r.ReadI32(entry) || !r.ReadU32(count) || !r.ReadU32(item_drop) ||
        !r.ReadU32(item_drop_count))
      return false;
    t.npc_or_go_objectives[i].creature_or_go = entry;
    t.npc_or_go_objectives[i].required_count = count;
    t.item_drop_objectives[i].item_id = item_drop;
    t.item_drop_objectives[i].required_count = item_drop_count;
  }

  for (int i = 0; i < kQuestItemObjectivesCount; ++i)
    if (!r.ReadU32(t.item_objectives[i].item_id) || !r.ReadU32(t.item_objectives[i].required_count))
      return false;

  for (int i = 0; i < kQuestObjectivesCount; ++i)
    if (!r.ReadCString(t.npc_or_go_objectives[i].text))
      return false;

  if (t.title.empty()) {
    InvalidateQuestTemplate(t.quest_id);
    return true;
  }

  CommitQuestTemplateUpdate(std::move(t));
  return true;
}

bool QuestManager::HandleQuestGiverQuestDetails(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  QuestDetailsDialog d;
  if (!r.ReadGuid(d.npc_guid) || !r.ReadGuid(d.sharer_guid))
    return false;
  if (!r.ReadU32(d.quest_id))
    return false;
  if (!r.ReadCString(d.title, kQuestTemplateTitleSize) ||
      !r.ReadCString(d.details, kQuestTemplateDetailsSize) ||
      !r.ReadCString(d.objectives, kQuestTemplateObjectivesSize))
    return false;

  std::uint8_t auto_accept;
  std::uint32_t flags_raw, suggested;
  std::uint8_t accept_packet_value = 0;
  if (!r.ReadU8(auto_accept) || !r.ReadU32(flags_raw) || !r.ReadU32(suggested) ||
      !r.ReadU8(accept_packet_value))
    return false;

  d.auto_accept = auto_accept != 0;
  d.quest_flags = static_cast<QuestFlags>(flags_raw);
  d.suggested_players = suggested;
  d.accept_packet_value = accept_packet_value;

  std::uint32_t choice_count;
  if (!r.ReadU32(choice_count))
    return false;
  constexpr std::size_t kRewardItemWireBytes = 3u * sizeof(std::uint32_t);
  if (choice_count > static_cast<std::uint32_t>(kQuestRewardChoicesCount) ||
      choice_count > r.Remaining() / kRewardItemWireBytes) {
    return false;
  }
  d.reward_choice_items.resize(choice_count);
  for (std::uint32_t i = 0; i < choice_count; ++i) {
    if (!r.ReadU32(d.reward_choice_items[i].item_id) ||
        !r.ReadU32(d.reward_choice_items[i].count) ||
        !r.ReadU32(d.reward_choice_items[i].display_info_id))
      return false;
  }

  std::uint32_t rew_count;
  if (!r.ReadU32(rew_count))
    return false;
  if (rew_count > static_cast<std::uint32_t>(kQuestRewardsCount) ||
      rew_count > r.Remaining() / kRewardItemWireBytes) {
    return false;
  }
  d.reward_items.resize(rew_count);
  for (std::uint32_t i = 0; i < rew_count; ++i) {
    if (!r.ReadU32(d.reward_items[i].item_id) || !r.ReadU32(d.reward_items[i].count) ||
        !r.ReadU32(d.reward_items[i].display_info_id))
      return false;
  }

  if (!r.ReadU32(d.reward_money))
    return false;

  std::uint32_t xp, honor;
  float honor_mult;
  std::uint32_t rew_spell;
  std::int32_t rew_spell_cast;
  std::uint32_t char_title, bonus_talents, rew_arena, reward_faction_control;
  if (!r.ReadU32(xp) || !r.ReadU32(honor) || !r.ReadFloat(honor_mult) || !r.ReadU32(rew_spell) ||
      !r.ReadI32(rew_spell_cast) || !r.ReadU32(char_title) || !r.ReadU32(bonus_talents) ||
      !r.ReadU32(rew_arena) || !r.ReadU32(reward_faction_control))
    return false;

  d.reward_xp = xp;
  d.reward_honor = honor;
  d.rew_spell = rew_spell;
  d.rew_spell_cast = rew_spell_cast;
  d.char_title_id = char_title;
  d.bonus_talents = bonus_talents;
  d.rew_arena_points = rew_arena;
  d.reward_faction_control = reward_faction_control;

  for (int i = 0; i < kQuestReputationsCount; ++i)
    if (!r.ReadU32(d.reward_faction_id[i]))
      return false;
  for (int i = 0; i < kQuestReputationsCount; ++i)
    if (!r.ReadI32(d.reward_faction_value[i]))
      return false;
  for (int i = 0; i < kQuestReputationsCount; ++i)
    if (!r.ReadI32(d.reward_faction_value_override[i]))
      return false;

  std::uint32_t emote_count;
  if (!r.ReadU32(emote_count))
    return false;
  constexpr std::size_t kEmoteWireBytes = 2u * sizeof(std::uint32_t);
  if (emote_count > static_cast<std::uint32_t>(kQuestEmoteCount) ||
      emote_count > r.Remaining() / kEmoteWireBytes) {
    return false;
  }
  d.emotes.resize(emote_count);
  for (std::uint32_t i = 0; i < emote_count; ++i) {
    if (!r.ReadU32(d.emotes[i].emote_id))
      return false;
    if (!r.ReadU32(d.emotes[i].delay))
      return false;
  }

  if (r.Remaining() != 0) {
    return false;
  }

  BeginDialogResponse();
  SetQuestFrameInteractionState(d.npc_guid, d.sharer_guid, d.quest_id);
  dialog_text_.title_text = NormalizeQuestFrameTitle(d.title);
  dialog_text_.has_title_text = true;
  dialog_text_.quest_text = ExpandDialogText(d.details, true);
  dialog_text_.has_quest_text = true;
  dialog_text_.objective_text = ExpandDialogText(d.objectives, false);
  dialog_text_.has_objective_text = true;
  RebuildRewardFactionPreview(d.reward_faction_id, d.reward_faction_value,
                              d.reward_faction_value_override, d.reward_faction_control);
  active_details_ = std::move(d);
  return true;
}

bool QuestManager::HandleQuestGiverRequestItems(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  QuestRequestItemsDialog d;
  if (!r.ReadGuid(d.npc_guid))
    return false;
  if (!r.ReadU32(d.quest_id))
    return false;
  if (!r.ReadCString(d.title) || !r.ReadCString(d.request_text))
    return false;

  std::uint32_t emote_delay = 0;
  std::uint32_t emote_id = 0;
  std::uint32_t progress_state = 0;
  std::uint32_t flags_raw = 0;
  if (!r.ReadU32(emote_delay) || !r.ReadU32(emote_id) || !r.ReadU32(progress_state) ||
      !r.ReadU32(flags_raw) || !r.ReadU32(d.suggested_players) || !r.ReadU32(d.required_money))
    return false;
  d.quest_flags = static_cast<QuestFlags>(flags_raw);
  d.close_on_decline = progress_state != 0;

  if (emote_delay != 0 || emote_id != 0) {
    d.emotes.push_back({.delay = emote_delay, .emote_id = emote_id});
  }

  std::uint32_t required_count = 0;
  if (!r.ReadU32(required_count))
    return false;
  constexpr std::size_t kRequiredItemWireBytes = 3u * sizeof(std::uint32_t);
  if (required_count > static_cast<std::uint32_t>(kQuestItemObjectivesCount) ||
      required_count > r.Remaining() / kRequiredItemWireBytes) {
    return false;
  }
  d.required_items.resize(required_count);
  for (std::uint32_t i = 0; i < required_count; ++i) {
    if (!r.ReadU32(d.required_items[i].item_id) || !r.ReadU32(d.required_items[i].count) ||
        !r.ReadU32(d.required_items[i].display_info_id)) {
      return false;
    }
  }

  std::uint32_t completable_gate_a = 0;
  std::uint32_t completable_gate_b = 0;
  std::uint32_t completable_gate_c = 0;
  std::uint32_t completable_gate_d = 0;
  if (!r.ReadU32(completable_gate_a) || !r.ReadU32(completable_gate_b) ||
      !r.ReadU32(completable_gate_c) || !r.ReadU32(completable_gate_d)) {
    return false;
  }
  d.is_completable = completable_gate_a != 0 && completable_gate_b != 0 &&
                     completable_gate_c != 0 && completable_gate_d != 0;

  if (r.Remaining() != 0) {
    return false;
  }

  BeginDialogResponse();
  SetQuestFrameInteractionState(d.npc_guid, ObjectGuid(), d.quest_id);
  dialog_text_.title_text = NormalizeQuestFrameTitle(d.title);
  dialog_text_.has_title_text = true;
  dialog_text_.progress_text = ExpandDialogText(d.request_text, true);
  dialog_text_.has_progress_text = true;
  active_request_ = std::move(d);
  return true;
}

bool QuestManager::HandleQuestGiverOfferReward(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  QuestOfferRewardDialog d;
  if (!r.ReadGuid(d.npc_guid))
    return false;
  if (!r.ReadU32(d.quest_id))
    return false;
  if (!r.ReadCString(d.title) || !r.ReadCString(d.reward_text))
    return false;

  std::uint8_t dialog_state = 0;
  std::uint32_t flags_raw = 0;
  if (!r.ReadU8(dialog_state) || !r.ReadU32(flags_raw) || !r.ReadU32(d.suggested_players))
    return false;
  d.quest_flags = static_cast<QuestFlags>(flags_raw);
  d.close_on_decline = dialog_state != 0;

  std::uint32_t emote_count = 0;
  if (!r.ReadU32(emote_count))
    return false;
  constexpr std::size_t kEmoteWireBytes = 2u * sizeof(std::uint32_t);
  if (emote_count > static_cast<std::uint32_t>(kQuestEmoteCount) ||
      emote_count > r.Remaining() / kEmoteWireBytes) {
    return false;
  }
  d.emotes.resize(emote_count);
  for (std::uint32_t i = 0; i < emote_count; ++i) {
    if (!r.ReadU32(d.emotes[i].delay) || !r.ReadU32(d.emotes[i].emote_id))
      return false;
  }

  std::uint32_t choice_count = 0;
  if (!r.ReadU32(choice_count))
    return false;
  constexpr std::size_t kRewardItemWireBytes = 3u * sizeof(std::uint32_t);
  if (choice_count > static_cast<std::uint32_t>(kQuestRewardChoicesCount) ||
      choice_count > r.Remaining() / kRewardItemWireBytes) {
    return false;
  }
  d.reward_choice_items.resize(choice_count);
  for (std::uint32_t i = 0; i < choice_count; ++i) {
    if (!r.ReadU32(d.reward_choice_items[i].item_id) ||
        !r.ReadU32(d.reward_choice_items[i].count) ||
        !r.ReadU32(d.reward_choice_items[i].display_info_id))
      return false;
  }

  std::uint32_t reward_count = 0;
  if (!r.ReadU32(reward_count))
    return false;
  if (reward_count > static_cast<std::uint32_t>(kQuestRewardsCount) ||
      reward_count > r.Remaining() / kRewardItemWireBytes) {
    return false;
  }
  d.reward_items.resize(reward_count);
  for (std::uint32_t i = 0; i < reward_count; ++i) {
    if (!r.ReadU32(d.reward_items[i].item_id) || !r.ReadU32(d.reward_items[i].count) ||
        !r.ReadU32(d.reward_items[i].display_info_id))
      return false;
  }

  std::int32_t reward_money_or_required_money = 0;
  std::uint32_t reward_faction_control = 0;
  std::uint32_t reward_honor = 0;
  float reward_honor_multiplier = 0.0f;
  std::uint32_t reward_display_unknown = 0;
  if (!r.ReadI32(reward_money_or_required_money) || !r.ReadU32(d.reward_xp) ||
      !r.ReadU32(reward_honor) || !r.ReadFloat(reward_honor_multiplier) ||
      !r.ReadU32(reward_display_unknown) || !r.ReadU32(d.rew_spell) ||
      !r.ReadI32(d.rew_spell_cast) || !r.ReadU32(d.char_title_id) ||
      !r.ReadU32(d.bonus_talents) || !r.ReadU32(d.rew_arena_points) ||
      !r.ReadU32(reward_faction_control))
    return false;
  d.reward_honor = reward_honor;
  d.reward_faction_control = reward_faction_control;
  (void)reward_honor_multiplier;
  (void)reward_display_unknown;

  if (reward_money_or_required_money < 0) {
    d.required_money = static_cast<std::uint32_t>(-reward_money_or_required_money);
    d.reward_money = 0;
  } else {
    d.reward_money = static_cast<std::uint32_t>(reward_money_or_required_money);
    d.required_money = 0;
  }

  for (int i = 0; i < kQuestReputationsCount; ++i)
    if (!r.ReadU32(d.reward_faction_id[i]))
      return false;
  for (int i = 0; i < kQuestReputationsCount; ++i)
    if (!r.ReadI32(d.reward_faction_value[i]))
      return false;
  for (int i = 0; i < kQuestReputationsCount; ++i)
    if (!r.ReadI32(d.reward_faction_value_override[i]))
      return false;

  if (r.Remaining() != 0) {
    return false;
  }

  BeginDialogResponse();
  SetQuestFrameInteractionState(d.npc_guid, ObjectGuid(), d.quest_id);
  dialog_text_.title_text = NormalizeQuestFrameTitle(d.title);
  dialog_text_.has_title_text = true;
  dialog_text_.reward_text = ExpandDialogText(d.reward_text, true);
  dialog_text_.has_reward_text = true;
  RebuildRewardFactionPreview(d.reward_faction_id, d.reward_faction_value,
                              d.reward_faction_value_override, d.reward_faction_control);
  active_reward_ = std::move(d);
  return true;
}

bool QuestManager::HandleQuestGiverStatus(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  QuestGiverStatusEntry e;
  std::uint8_t status;
  if (!r.ReadGuid(e.guid) || !r.ReadU8(status) || r.Remaining() != 0)
    return false;
  e.status = static_cast<QuestGiverStatus>(status);

  for (auto &s : giver_statuses_) {
    if (s.guid == e.guid) {
      s.status = e.status;
      return true;
    }
  }
  giver_statuses_.push_back(e);
  return true;
}

bool QuestManager::HandleQuestGiverStatusMultiple(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t count;
  if (!r.ReadU32(count))
    return false;
  constexpr std::size_t kStatusWireBytes = sizeof(std::uint64_t) + sizeof(std::uint8_t);
  if (count > r.Remaining() / kStatusWireBytes) {
    return false;
  }
  std::vector<QuestGiverStatusEntry> statuses;
  statuses.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    QuestGiverStatusEntry e;
    std::uint8_t status;
    if (!r.ReadGuid(e.guid) || !r.ReadU8(status))
      return false;
    e.status = static_cast<QuestGiverStatus>(status);
    statuses.push_back(e);
  }
  if (r.Remaining() != 0) {
    return false;
  }
  giver_statuses_ = std::move(statuses);
  return true;
}

std::optional<QuestGiverStatus> QuestManager::FindQuestGiverStatus(
    const ObjectGuid &guid) const {
  for (const auto &entry : giver_statuses_) {
    if (entry.guid == guid) {
      return entry.status;
    }
  }

  return std::nullopt;
}

void QuestManager::EraseQuestGiverStatus(const ObjectGuid &guid) {
  giver_statuses_.erase(
      std::remove_if(giver_statuses_.begin(), giver_statuses_.end(),
                     [&guid](const QuestGiverStatusEntry &entry) { return entry.guid == guid; }),
      giver_statuses_.end());
}

bool QuestManager::HandleQuestUpdateComplete(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t quest_id;
  if (!r.ReadU32(quest_id) || r.Remaining() != 0)
    return false;
  if (auto *entry = FindQuestLogEntry(quest_id))
    entry->status = QuestStatus::kComplete;
  return true;
}

bool QuestManager::HandleQuestUpdateAddKill(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t quest_id, current, required;
  std::int32_t entry_id = 0;
  ObjectGuid guid;
  if (!r.ReadU32(quest_id) || !r.ReadI32(entry_id) || !r.ReadU32(current) || !r.ReadU32(required) ||
      !r.ReadGuid(guid) || r.Remaining() != 0)
    return false;

  if (auto *log = FindQuestLogEntry(quest_id)) {

    if (auto *tmpl = GetTemplate(quest_id)) {
      for (int i = 0; i < kQuestObjectivesCount; ++i) {
        if (tmpl->npc_or_go_objectives[i].creature_or_go == entry_id) {
          log->kill_counts[i] = current;
          break;
        }
      }
    }
  }
  return true;
}

bool QuestManager::HandleQuestConfirmAccept(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  QuestConfirmAcceptPrompt prompt;
  if (!r.ReadU32(prompt.quest_id) || !r.ReadCString(prompt.title) ||
      !r.ReadGuid(prompt.sharer_guid))
    return false;

  pending_confirm_accept_ = std::move(prompt);
  return true;
}

bool QuestManager::HandleQuestLogFull() {
  quest_log_full_ = true;
  return true;
}

bool QuestManager::HandleQuestPoiQueryResponse(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t quest_count;
  if (!r.ReadU32(quest_count))
    return false;

  if (quest_count > r.Remaining() / (2u * sizeof(std::uint32_t))) {
    return false;
  }
  std::vector<QuestPoiData> response;
  response.reserve(quest_count);

  for (std::uint32_t q = 0; q < quest_count; ++q) {
    QuestPoiData qd;
    if (!r.ReadU32(qd.quest_id))
      return false;
    std::uint32_t poi_count;
    if (!r.ReadU32(poi_count))
      return false;
    constexpr std::size_t kPoiFixedWireBytes = 8u * sizeof(std::uint32_t);
    if (poi_count > r.Remaining() / kPoiFixedWireBytes) {
      return false;
    }
    qd.pois.reserve(poi_count);

    for (std::uint32_t p = 0; p < poi_count; ++p) {
      QuestPoiEntry pe;
      if (!r.ReadU32(pe.poi_id) || !r.ReadI32(pe.objective_index) || !r.ReadU32(pe.map_id) ||
          !r.ReadU32(pe.area_id) || !r.ReadU32(pe.floor_id) || !r.ReadU32(pe.unk3) ||
          !r.ReadU32(pe.unk4))
        return false;
      std::uint32_t point_count;
      if (!r.ReadU32(point_count))
        return false;
      constexpr std::size_t kPointWireBytes = 2u * sizeof(std::int32_t);
      if (point_count > r.Remaining() / kPointWireBytes) {
        return false;
      }
      pe.points.resize(point_count);
      for (auto &pt : pe.points) {
        if (!r.ReadI32(pt.x) || !r.ReadI32(pt.y))
          return false;
      }
      qd.pois.push_back(std::move(pe));
    }
    response.push_back(std::move(qd));
  }
  if (r.Remaining() != 0) {
    return false;
  }
  last_poi_response_ = std::move(response);
  return true;
}

bool QuestManager::HandleQuestPushResult(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  QuestPushResultInfo pr;
  if (!r.ReadU64(pr.player_guid) || !r.ReadU8(pr.msg))
    return false;
  last_push_result_ = pr;
  return true;
}

bool QuestManager::HandleQuestGiverQuestFailed(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  QuestFailedInfo fi;
  if (!r.ReadU32(fi.quest_id) || !r.ReadU32(fi.reason))
    return false;
  last_quest_failed_ = fi;
  return true;
}

bool QuestManager::HandleQuestUpdateFailed(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_update_failed_quest_))
    return false;
  MarkQuestFailedInLog(last_update_failed_quest_, false);
  return true;
}

bool QuestManager::HandleQuestUpdateAddPvpKill(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  QuestPvpKillInfo pk;
  if (!r.ReadU32(pk.quest_id) || !r.ReadU32(pk.current_count) || !r.ReadU32(pk.required_count))
    return false;
  last_pvp_kill_ = pk;
  return true;
}

bool QuestManager::HandleQuestForceRemove(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(force_remove_quest_))
    return false;
  return true;
}

bool QuestManager::HandleQuestgiverQuestInvalid(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(quest_invalid_reason_))
    return false;
  return true;
}

bool QuestManager::HandleQuestUpdateAddItem(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  QuestAddItem item;
  if (!r.ReadU32(item.item_entry))
    return false;
  if (!r.ReadU32(item.count))
    return false;
  last_quest_add_item_ = item;
  return true;
}

bool QuestManager::HandleQuestUpdateFailedTimer(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(failed_timer_quest_))
    return false;
  MarkQuestFailedInLog(failed_timer_quest_, true);
  return true;
}

bool QuestManager::HandleQueryQuestsCompleted(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t count;
  if (!r.ReadU32(count))
    return false;

  completed_quests_.clear();
  completed_quests_.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    std::uint32_t qid;
    if (!r.ReadU32(qid))
      return false;
    completed_quests_.push_back(qid);
  }
  return true;
}

WorldPacket QuestManager::BuildQuestQuery(std::uint32_t quest_id) {
  WorldPacket pkt(Opcode::CMSG_QUEST_QUERY);
  pkt.AppendU32(quest_id);
  return pkt;
}

WorldPacket QuestManager::BuildQuestGiverHello(const ObjectGuid &npc) {
  WorldPacket pkt(Opcode::CMSG_QUESTGIVER_HELLO);
  pkt.AppendU64(npc.GetRawValue());
  return pkt;
}

WorldPacket QuestManager::BuildCompleteQuest(const ObjectGuid &npc, std::uint32_t quest_id) {
  WorldPacket pkt(net::wotlk::Opcode::CMSG_QUESTGIVER_COMPLETE_QUEST);
  pkt.AppendU64(npc.GetRawValue());
  pkt.AppendU32(quest_id);
  return pkt;
}

WorldPacket QuestManager::BuildChooseReward(const ObjectGuid &npc, std::uint32_t quest_id,
                                            std::uint32_t choice) {
  WorldPacket pkt(Opcode::CMSG_QUESTGIVER_CHOOSE_REWARD);
  pkt.AppendU64(npc.GetRawValue());
  pkt.AppendU32(quest_id);
  pkt.AppendU32(choice);
  return pkt;
}

void QuestManager::AddQuestToLog(std::uint32_t quest_id) {
  if (IsQuestInLog(quest_id))
    return;
  if (quest_log_.size() >= kMaxQuestLogEntries) {
    quest_log_full_ = true;
    return;
  }
  QuestLogEntry e;
  e.quest_id = quest_id;
  e.status = QuestStatus::kIncomplete;
  quest_log_.push_back(e);
}

void QuestManager::RemoveQuestFromLog(std::uint32_t quest_id) {
  quest_log_.erase(
      std::remove_if(quest_log_.begin(), quest_log_.end(),
                     [quest_id](const QuestLogEntry &e) { return e.quest_id == quest_id; }),
      quest_log_.end());
}

void QuestManager::SyncQuestLogFromPlayer(const ObjectManager& objects,
                                          const CGPlayer_C &player) {
  std::unordered_map<std::uint32_t, QuestLogEntry> previous_entries;
  previous_entries.reserve(quest_log_.size());
  for (const auto &entry : quest_log_) {
    previous_entries.emplace(entry.quest_id, entry);
  }

  auto &runtime_log = QuestLog::Get();
  std::vector<QuestLogSlot> runtime_entries;
  runtime_entries.reserve(kMaxQuestLogEntries);

  quest_log_.clear();
  quest_log_.reserve(kMaxQuestLogEntries);

  for (std::uint8_t slot = 0; slot < kMaxQuestLogEntries; ++slot) {
    const auto player_entry = player.GetQuestLog(slot);
    if (player_entry.quest_id == 0) {
      continue;
    }

    QuestLogEntry entry;
    entry.quest_id = player_entry.quest_id;
    entry.slot = slot;
    entry.is_timed = player_entry.timer != 0;
    entry.timer_ms = player_entry.timer;
    std::copy(std::begin(player_entry.counts), std::end(player_entry.counts),
              std::begin(entry.kill_counts));

    if (const auto it = previous_entries.find(entry.quest_id); it != previous_entries.end()) {
      entry.status = it->second.status;
      entry.timer_expiration_reported = it->second.timer_expiration_reported;
    }
    if ((player_entry.state & 0x10000u) != 0) {
      entry.status = QuestStatus::kComplete;
    }

    quest_log_.push_back(entry);

    QuestLogSlot runtime_entry;
    runtime_entry.quest_id = entry.quest_id;
    runtime_entry.has_cached_template = false;
    runtime_entry.is_complete = entry.status == QuestStatus::kComplete;
    runtime_entry.is_failed = entry.status == QuestStatus::kFailed;
    runtime_entry.is_tracked = runtime_log.IsTracked(entry.quest_id);
    runtime_entry.timer = player_entry.timer;

    if (const auto *tmpl = GetTemplate(entry.quest_id); tmpl != nullptr) {
      runtime_entry.has_cached_template = true;
      runtime_entry.title = tmpl->title;
      runtime_entry.description = tmpl->details;
      runtime_entry.objectives_text = tmpl->objectives;
      runtime_entry.level =
          tmpl->quest_level > 0 ? static_cast<std::uint32_t>(tmpl->quest_level) : 0;
      runtime_entry.suggested_group = tmpl->suggested_players;
      runtime_entry.zone_sort = tmpl->zone_or_sort;
      runtime_entry.is_daily = HasFlag(tmpl->flags, QuestFlags::kDaily);
      runtime_entry.is_group = tmpl->suggested_players > 0;
      runtime_entry.is_raid = HasFlag(tmpl->flags, QuestFlags::kRaid);
      runtime_entry.is_pvp = HasFlag(tmpl->flags, QuestFlags::kPvp);
      runtime_entry.reward_money = tmpl->reward_money;
      runtime_entry.reward_xp = tmpl->xp_id;
    }

    runtime_entries.push_back(std::move(runtime_entry));
  }

  runtime_log.SetQuestLog(runtime_entries);
  runtime_log.LoadTrackedQuestsFromCVarIfNeeded(objects);
}

bool QuestManager::IsQuestInLog(std::uint32_t quest_id) const {
  return FindQuestLogEntry(quest_id) != nullptr;
}

QuestLogEntry *QuestManager::FindQuestLogEntry(std::uint32_t quest_id) {
  for (auto &e : quest_log_)
    if (e.quest_id == quest_id)
      return &e;
  return nullptr;
}

const QuestLogEntry *QuestManager::FindQuestLogEntry(std::uint32_t quest_id) const {
  for (auto &e : quest_log_)
    if (e.quest_id == quest_id)
      return &e;
  return nullptr;
}

const QuestTemplate *QuestManager::GetTemplate(std::uint32_t quest_id) const {
  auto it = template_cache_.find(quest_id);
  return it != template_cache_.end() ? &it->second : nullptr;
}

bool QuestManager::HydrateRetailQuestCache(openwow::data::WDBCache &cache) {
  std::unordered_map<std::uint32_t, QuestTemplate> hydrated;
  const auto quest_ids = cache.GetKeysInPersistenceOrder(
      openwow::data::WDBCacheType::Quest);
  hydrated.reserve(quest_ids.size());

  for (const auto quest_id : quest_ids) {
    const auto record = cache.Get(openwow::data::WDBCacheType::Quest,
                                  quest_id);
    auto parsed = record.has_value()
                      ? DeserializeQuestTemplateWdbRecord(record->data)
                      : std::nullopt;
    if (quest_id == 0 || !parsed.has_value() ||
        parsed->quest_id != quest_id || parsed->title.empty()) {
      cache.ClearType(openwow::data::WDBCacheType::Quest);
      template_cache_.clear();
      quest_queries_.Clear();
      return false;
    }

    auto canonical = SerializeQuestTemplateWdbRecord(*parsed);
    if (canonical != record->data) {
      cache.Insert(openwow::data::WDBCacheType::Quest, quest_id,
                   std::move(canonical), record->version);
    }
    hydrated.insert_or_assign(quest_id, std::move(*parsed));
  }

  template_cache_ = std::move(hydrated);
  quest_queries_.Clear();
  return true;
}

const QuestTemplate *QuestManager::GetOrRequestTemplate(std::uint32_t quest_id) {
  return GetOrRequestTemplate(quest_id, QueryRequestOptions{});
}

const QuestTemplate *QuestManager::GetOrRequestTemplate(std::uint32_t quest_id,
                                                        QueryRequestOptions options) {
  if (quest_id == 0) {
    return nullptr;
  }

  auto it = template_cache_.find(quest_id);
  if (it != template_cache_.end()) {
    return &it->second;
  }

  if (!options.callback) {
    return nullptr;
  }

  quest_queries_.Request(quest_id, tick_count_provider_(), std::move(options));
  return nullptr;
}

const char *QuestManager::GetQuestCacheTitle(
    std::uint32_t quest_id,
    AsyncQueryChannel::Callback on_miss) {
  QueryRequestOptions options{};
  if (on_miss) {
    options.callback = std::move(on_miss);
  }
  const auto *tmpl = GetOrRequestTemplate(quest_id, std::move(options));
  return tmpl ? tmpl->title.c_str() : nullptr;
}

void QuestManager::SetQuestQueryDispatcher(QuestQueryDispatchFn dispatcher) {
  quest_queries_.SetDispatcher(
      [dispatcher = std::move(dispatcher)](std::uint32_t quest_id, std::uint64_t ) {
        if (dispatcher) {
          dispatcher(quest_id);
        }
      });
}

void QuestManager::SetTickCountProvider(std::function<std::uint32_t()> provider) {
  tick_count_provider_ = std::move(provider);
  if (!tick_count_provider_) {
    tick_count_provider_ =
        []() { return openwow::core::GameClock::GetTickCount32(); };
  }
}

void QuestManager::PumpDispatchQueues(std::uint32_t current_tick_ms) {
  quest_queries_.Pump(current_tick_ms);
}

void QuestManager::SetQuestQueryMaxInFlight(std::uint32_t max_in_flight) {
  quest_queries_.SetMaxInFlight(max_in_flight);
}

void QuestManager::MarkQuestQueryPending(std::uint32_t quest_id) {
  if (quest_id == 0)
    return;
  quest_queries_.MarkPending(quest_id, tick_count_provider_());
}

bool QuestManager::IsQuestQueryPending(std::uint32_t quest_id) const {
  return quest_queries_.IsPending(quest_id);
}

void QuestManager::ClearPendingQueriesOnLogout() {
  quest_queries_.Clear();
}

void QuestManager::HandleClientCacheVersionInvalidation() {
  template_cache_.clear();
  quest_queries_.Clear();
}

void QuestManager::Clear() {
  quest_log_.clear();
  template_cache_.clear();
  quest_queries_.Clear();
  ResetDialogState();
  pending_confirm_accept_.reset();
  ClearRewardFactionPreview();
  giver_statuses_.clear();
  quest_log_full_ = false;
  last_poi_response_.clear();
  last_push_result_.reset();
  last_quest_failed_.reset();
  last_update_failed_quest_ = 0;
  last_pvp_kill_.reset();

  force_remove_quest_ = 0;
  quest_invalid_reason_ = 0;
  last_quest_add_item_.reset();
  failed_timer_quest_ = 0;
  completed_quests_.clear();
}

}
