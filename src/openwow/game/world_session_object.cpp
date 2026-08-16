
#include "openwow/game/world_session.h"
#include "openwow/game/commerce/mail/adapters/protocol/mail_packet_codec.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/arena_system.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/chat_cache.h"
#include "openwow/game/combat/adapters/ui/auto_attack_activity_presenter.h"
#include "openwow/game/combat/application/client_control_transition.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/descriptor_callback_registry.h"
#include "openwow/game/group_system.h"
#include "openwow/game/inventory/adapters/ui/inventory_highlight_presenter.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/object_types.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/wdb_cache.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/game/player_descriptor_callbacks.h"
#include "openwow/game/profession_system.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/spellbook_frame.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/talent_info.h"
#include "openwow/net/client_services.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/autocomplete.h"
#include "openwow/ui/game/capture_point_ui_manager.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_companion.h"
#include "openwow/game/inventory/adapters/lua/container_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_tradeskill_state.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/world_map_system.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/world/camera/world_camera.h"

#include "openwow/game/update_field_event_mapper.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace openwow::game {

namespace {

constexpr std::uint32_t kShapeshiftFormFlagSuppressCreateUiRefresh = 0x1u;

[[nodiscard]] std::optional<DancePlayerClass> ToDancePlayerClass(
    const std::uint8_t external_class) {
  switch (external_class) {
    case 1:
      return DancePlayerClass::kWarrior;
    case 2:
      return DancePlayerClass::kPaladin;
    case 3:
      return DancePlayerClass::kHunter;
    case 4:
      return DancePlayerClass::kRogue;
    case 5:
      return DancePlayerClass::kPriest;
    case 6:
      return DancePlayerClass::kDeathKnight;
    case 7:
      return DancePlayerClass::kShaman;
    case 8:
      return DancePlayerClass::kMage;
    case 9:
      return DancePlayerClass::kWarlock;
    case 11:
      return DancePlayerClass::kDruid;
    default:
      return std::nullopt;
  }
}

constexpr std::uint16_t kPlayerCharacterPoints1RelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_CHARACTER_POINTS1, UNIT_END) *
                               sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerCharacterPointsSizeBytes = sizeof(std::uint32_t);

constexpr std::array<std::uint16_t, 2> kPlayerCurrencyRegistrationOffsets{{
    static_cast<std::uint16_t>(
        DescriptorWordDistance(PLAYER_FIELD_HONOR_CURRENCY, UNIT_END) *
        sizeof(std::uint32_t)),
    static_cast<std::uint16_t>(
        DescriptorWordDistance(PLAYER_FIELD_ARENA_CURRENCY, UNIT_END) *
        sizeof(std::uint32_t)),
}};
constexpr std::uint16_t kPlayerCurrencySizeBytes = sizeof(std::uint32_t);

constexpr std::uint16_t kPlayerNoReagentCostRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_NO_REAGENT_COST_1, UNIT_END) *
                               sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerNoReagentCostSizeBytes =
    static_cast<std::uint16_t>(3u * sizeof(std::uint32_t));
constexpr std::uint16_t kUnitFlagsRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(UNIT_FIELD_FLAGS, OBJECT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerAmmoFieldRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_AMMO_ID, UNIT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerBuybackFieldRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_FIELD_BUYBACK_PRICE_1, UNIT_END) *
                               sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerBuybackFieldSizeBytes =
    static_cast<std::uint16_t>(24u * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerBankBagSlotCountOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_BYTES_2, UNIT_END) * sizeof(std::uint32_t) + 2u);
constexpr std::uint16_t kPlayerBankBagSlotCountSizeBytes = 1u;
constexpr std::uint16_t kPlayerArenaTeamInfoRelativeOffsetBytes = static_cast<std::uint16_t>(
    DescriptorWordDistance(PLAYER_FIELD_ARENA_TEAM_INFO_1_1, UNIT_END) * sizeof(std::uint32_t));

constexpr std::uint16_t kPlayerArenaTeamIdSizeBytes = sizeof(std::uint32_t);
constexpr std::uint32_t kActivePlayerArenaTeamQueryThrottleMs = 10'000u;
constexpr std::uint16_t kPlayerDailyQuestsRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_FIELD_DAILY_QUESTS_1, UNIT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerDailyQuestSlotCount = 25u;
constexpr std::uint16_t kPlayerDailyQuestSlotSizeBytes = sizeof(std::uint32_t);
constexpr std::uint16_t kPlayerGlyphsRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_FIELD_GLYPHS_1, UNIT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerGlyphsEnabledRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_GLYPHS_ENABLED, UNIT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerGlyphSlotCount = 6u;
constexpr std::uint16_t kPlayerGlyphSlotSizeBytes = sizeof(std::uint32_t);
constexpr std::uint16_t kPlayerPetSpellPowerRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_PET_SPELL_POWER, UNIT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerPetSpellPowerSizeBytes = sizeof(std::uint32_t);
struct CombatRatingRegistration {
  std::uint16_t offset_bytes;
  std::uint16_t size_bytes;
};

constexpr std::array kPlayerCombatRatingRegistrations{
    CombatRatingRegistration{
        static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_FIELD_COMBAT_RATING_1, UNIT_END) *
                                   sizeof(std::uint32_t)),
        static_cast<std::uint16_t>(25u * sizeof(std::uint32_t))},
    CombatRatingRegistration{
        static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_SHIELD_BLOCK, UNIT_END) *
                                   sizeof(std::uint32_t)),
        static_cast<std::uint16_t>(sizeof(std::uint32_t))},
    CombatRatingRegistration{
        static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_FIELD_MOD_TARGET_RESISTANCE, UNIT_END) *
                                   sizeof(std::uint32_t)),
        static_cast<std::uint16_t>(sizeof(std::uint32_t))},
};
struct PushPlayerEventRegistration {
  TypeID section_type;
  std::uint16_t offset_bytes;
  std::uint16_t size_bytes;
  const char* event_name;
};

constexpr std::array kPushPlayerEventRegistrations{
    PushPlayerEventRegistration{TypeID::kPlayer, 0x778, 8,
                                ui::game::events::KNOWN_TITLES_UPDATE},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x780, 8,
                                ui::game::events::KNOWN_TITLES_UPDATE},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x788, 8,
                                ui::game::events::KNOWN_TITLES_UPDATE},
    PushPlayerEventRegistration{TypeID::kPlayer, 0xFF4, 4,
                                ui::game::events::UPDATE_EXHAUSTION},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x798, 4,
                                ui::game::events::PLAYER_XP_UPDATE},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x79C, 4,
                                ui::game::events::PLAYER_XP_UPDATE},
    PushPlayerEventRegistration{TypeID::kPlayer, 0xFFC, 0x1C,
                                ui::game::events::PLAYER_DAMAGE_DONE_MODS},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x1018, 0x1C,
                                ui::game::events::PLAYER_DAMAGE_DONE_MODS},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x1034, 0x1C,
                                ui::game::events::PLAYER_DAMAGE_DONE_MODS},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x1050, 4,
                                ui::game::events::PLAYER_DAMAGE_DONE_MODS},
    PushPlayerEventRegistration{TypeID::kPlayer, 0xDB0, 0x34,
                                ui::game::events::PLAYER_DAMAGE_DONE_MODS},
    PushPlayerEventRegistration{TypeID::kUnit, 0x88, 0x38,
                                ui::game::events::PLAYER_DAMAGE_DONE_MODS},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x1060, 4,
                                ui::game::events::PLAYER_DAMAGE_DONE_MODS},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x10D4, 2,
                                ui::game::events::PLAYER_PVP_KILLS_CHANGED},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x10E0, 4,
                                ui::game::events::PLAYER_PVP_KILLS_CHANGED},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x10D8, 4,
                                ui::game::events::PLAYER_PVP_KILLS_CHANGED},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x1C, 4,
                                ui::game::events::PLAYER_PVP_RANK_CHANGED},
    PushPlayerEventRegistration{TypeID::kPlayer, 0x10E8, 4,
                                ui::game::events::UPDATE_FACTION},
};
constexpr std::uint16_t kPlayerFieldBytes2RelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_FIELD_BYTES2, UNIT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerFieldBytes2OverrideSpellSize = 2;

constexpr std::uint16_t kPlayerFieldBytes2RestStateOffsetBytes =
    static_cast<std::uint16_t>(kPlayerFieldBytes2RelativeOffsetBytes + 3u);
constexpr std::uint16_t kPlayerFieldBytes2RestStateSize = 1;

constexpr std::uint16_t kUnitFieldBytes2ShapeshiftFormOffsetBytes = 0x1D3;
constexpr std::uint16_t kUnitFieldBytes2ShapeshiftFormSize = 1;
constexpr std::uint16_t kUnitCharmRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(UNIT_FIELD_CHARM, OBJECT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kUnitCharmSizeBytes =
    static_cast<std::uint16_t>(2u * sizeof(std::uint32_t));

constexpr std::uint16_t kUnitCritterRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(UNIT_FIELD_CRITTER, OBJECT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kUnitCritterSizeBytes =
    static_cast<std::uint16_t>(2u * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerCoinageRelativeOffsetBytes =
    static_cast<std::uint16_t>(DescriptorWordDistance(PLAYER_FIELD_COINAGE, UNIT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerCoinageSizeBytes =
    static_cast<std::uint16_t>(sizeof(std::uint32_t));

constexpr std::uint16_t kPlayerSkillInfoRelativeOffsetBytes = static_cast<std::uint16_t>(
    DescriptorWordDistance(PLAYER_SKILL_INFO_1_1, UNIT_END) * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerSkillValueRelativeOffsetBytes =
    static_cast<std::uint16_t>(kPlayerSkillInfoRelativeOffsetBytes + 4u);
constexpr std::uint16_t kPlayerSkillMaximumValueRelativeOffsetBytes =
    static_cast<std::uint16_t>(kPlayerSkillInfoRelativeOffsetBytes + 6u);
constexpr std::uint16_t kPlayerSkillModifierRelativeOffsetBytes =
    static_cast<std::uint16_t>(kPlayerSkillInfoRelativeOffsetBytes + 8u);
constexpr std::uint16_t kPlayerSkillStepModifierRelativeOffsetBytes =
    static_cast<std::uint16_t>(kPlayerSkillInfoRelativeOffsetBytes + 10u);
constexpr std::uint16_t kPlayerSkillSlotStrideBytes =
    static_cast<std::uint16_t>(3u * sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerSkillSlotCount = 128u;
constexpr std::uint16_t kPlayerSkillHalfwordSizeBytes = sizeof(std::uint16_t);
constexpr int kMaxDailyQuests = 25;
constexpr int kMasterLooterSystemMessageId = 251;
constexpr std::uint32_t kUnitFlagInCombat = 0x00080000u;

void RequestGuildPermissionsAndWithdrawRefresh(InteractionSender &interaction) {
  interaction.SendGuildPermissionsQuery();
  interaction.SendGuildBankMoneyWithdrawnQuery();
}

void RefreshLocalPlayerCombatUsability(WorldSession &session) {
  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireEvent(ui::game::events::SPELL_UPDATE_USABLE);
  if (ui::game::detail::RefreshAllActionSlotValidation(session)) {
    dispatch.FireActionbarUpdateUsable();
  }
  dispatch.FirePetBarUpdateUsable();
}

bool HasPowerDescriptorChange(const FieldUpdateBatch &updates) {
  for (const auto field : updates.updated_fields) {
    if ((field >= UNIT_FIELD_POWER1 && field <= UNIT_FIELD_POWER7) ||
        (field >= UNIT_FIELD_MAXPOWER1 && field <= UNIT_FIELD_MAXPOWER7) ||
        field == UNIT_FIELD_BYTES_0) {
      return true;
    }
  }
  return false;
}

std::string FormatAutoCompletePlayerName(const PlayerNameInfo &name_info) {
  if (name_info.name.empty()) {
    return {};
  }
  if (name_info.realm_name.empty()) {
    return name_info.name;
  }
  return name_info.name + "-" + name_info.realm_name;
}

struct TradeSkillLinkedPlayerInfo {
  std::string name;
  std::uint8_t race{0};
  std::uint8_t class_id{0};
};

std::optional<TradeSkillLinkedPlayerInfo>
ResolveTradeSkillLinkedPlayerInfo(WorldSession &session, const std::uint64_t guid) {
  if (guid == 0) {
    return std::nullopt;
  }

  if (const auto *cached_name = session.query_cache().GetPlayerName(guid);
      cached_name != nullptr && !cached_name->name.empty()) {
    return TradeSkillLinkedPlayerInfo{cached_name->name, cached_name->race, cached_name->class_id};
  }

  if (const auto *cached_name = session.objects().GetNameEntry(ObjectGuid(guid));
      cached_name != nullptr && !cached_name->name.empty()) {
    return TradeSkillLinkedPlayerInfo{cached_name->name, cached_name->race, cached_name->class_id};
  }

  return std::nullopt;
}

std::optional<std::uint32_t>
ResolveTradeSkillLinkSkillLineId(WorldSession &session,
                                 const TradeSkillLinkedPlayerInfo &linked_player,
                                 const std::uint32_t spell_id) {
  return ResolveSpellSkillLineId(session.objects(), session.GetDbcLoader(), linked_player.race,
                                 linked_player.class_id, spell_id);
}

std::string ResolveTradeSkillLineName(WorldSession &session, const std::uint32_t skill_line_id) {
  if (const auto *dbc = session.GetDbcLoader(); dbc != nullptr) {
    if (const auto *entry = dbc->skill_line().LookupEntry(skill_line_id);
        entry != nullptr && !entry->name.empty()) {
      return std::string(entry->name);
    }
  }

  return {};
}

struct ClearedInventorySlotGuid {
  std::uint64_t guid = 0;
  std::uint8_t abs_slot = 0;
};

struct ContainerFrameTrackedSlotChangeSummary {
  bool has_slot_guid_change = false;
  bool has_trade_skill_refresh = false;
  bool has_currency_display_refresh = false;
  std::vector<ClearedInventorySlotGuid> old_item_guids;
};

struct ContainerFrameTrackedBagSlotChangeSummary {
  bool has_slot_guid_change = false;
  bool has_trade_skill_refresh = false;
  std::uint8_t changed_slot_count = 0;
  std::uint8_t bag_id = 0;
  std::vector<ClearedInventorySlotGuid> old_item_guids;
};

bool IsContainerFrameFunc3Slot(const std::uint8_t abs_slot) {
  using namespace InventorySlots;
  return (abs_slot >= kBackpackStart && abs_slot < kBackpackEnd) ||
         (abs_slot >= kKeyringStart && abs_slot < kKeyringEnd) ||
         (abs_slot >= kCurrencyStart && abs_slot < kCurrencyEnd);
}

bool IsContainerFrameFunc5Slot(const std::uint8_t abs_slot) {
  using namespace InventorySlots;
  return (abs_slot >= kBagSlotsStart && abs_slot < kBagSlotsEnd) ||
         (abs_slot >= kBankBagStart && abs_slot < kBankBagEnd);
}

bool IsContainerFramePlayerSlot(const std::uint8_t abs_slot) {
  return IsContainerFrameFunc3Slot(abs_slot) || IsContainerFrameFunc5Slot(abs_slot);
}

bool IsPlayerInventoryActionUsabilitySlot(const std::uint8_t abs_slot) {
  using namespace InventorySlots;
  return abs_slot < kBagSlotsEnd ||
         (abs_slot >= kBankStart && abs_slot < kBankBagEnd) ||
         (abs_slot >= kKeyringStart && abs_slot < kCurrencyEnd);
}

void FirePlayerRootItemLockEvent(ui::game::ScriptEventDispatch& dispatch,
                                 const char* const event_name,
                                 const std::uint8_t abs_slot) {
  using namespace InventorySlots;
  if (abs_slot < kBagSlotsEnd) {
    dispatch.FireEventArgs(event_name, {static_cast<int>(abs_slot) + 1});
  } else if (abs_slot < kBackpackEnd) {
    dispatch.FireEventArgs(event_name,
                            {0, static_cast<int>(abs_slot - kBackpackStart) + 1});
  } else if (abs_slot >= kBankStart && abs_slot < kBankBagEnd) {
    dispatch.FireEventArgs(event_name,
                            {-1, static_cast<int>(abs_slot - kBankStart) + 1});
  } else if (abs_slot >= kKeyringStart && abs_slot < kKeyringEnd) {
    dispatch.FireEventArgs(event_name,
                            {-2, static_cast<int>(abs_slot - kKeyringStart) + 1});
  } else if (abs_slot >= kCurrencyStart && abs_slot < kCurrencyEnd) {
    dispatch.FireEventArgs(event_name,
                            {-4, static_cast<int>(abs_slot - kCurrencyStart) + 1});
  }
}

bool IsCurrencyTokenSlot(const std::uint8_t abs_slot) {
  return abs_slot >= InventorySlots::kCurrencyStart && abs_slot < InventorySlots::kCurrencyEnd;
}

bool IsMoneyItemEntry(const ItemDefinitions& cache, const std::uint32_t entry) {
  if (entry == 0) {
    return false;
  }

  const auto *item_template = cache.GetItem(entry);
  return item_template != nullptr && item_template->item_class == ItemClass::Money;
}

const FieldValueChange *FindValueChange(const FieldUpdateBatch &updates,
                                        const std::uint16_t field_index) {
  for (const auto &change : updates.value_changes) {
    if (change.field_index == field_index) {
      return &change;
    }
  }

  return nullptr;
}

std::uint64_t BuildGuidFromWords(const std::uint32_t low, const std::uint32_t high) {
  return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32u);
}

std::vector<std::uint8_t> CollectChangedPlayerInventorySlots(
    const CGPlayer_C &player, const FieldUpdateBatch &updates) {
  std::array<bool, InventorySlots::kTotalSlots> seen{};
  std::vector<std::uint8_t> changed_slots;

  for (const auto field_index : updates.updated_fields) {
    if (field_index < PLAYER_FIELD_INV_SLOT_HEAD ||
        field_index >= PLAYER_FIELD_CURRENCYTOKEN_SLOT_1 + 64u) {
      continue;
    }

    const auto slot = static_cast<std::uint8_t>(
        (field_index - PLAYER_FIELD_INV_SLOT_HEAD) / 2u);
    if (slot >= seen.size() || seen[slot]) {
      continue;
    }
    seen[slot] = true;

    const auto base_field = static_cast<std::uint16_t>(
        PLAYER_FIELD_INV_SLOT_HEAD + static_cast<std::uint16_t>(slot) * 2u);
    const auto new_low = player.GetUInt32(base_field);
    const auto new_high = player.GetUInt32(static_cast<std::uint16_t>(base_field + 1u));
    const auto *low_change = FindValueChange(updates, base_field);
    const auto *high_change =
        FindValueChange(updates, static_cast<std::uint16_t>(base_field + 1u));
    const auto old_low = low_change != nullptr ? low_change->old_value : new_low;
    const auto old_high = high_change != nullptr ? high_change->old_value : new_high;
    if (BuildGuidFromWords(old_low, old_high) != BuildGuidFromWords(new_low, new_high)) {
      changed_slots.push_back(slot);
    }
  }

  return changed_slots;
}

std::optional<std::uint8_t> ResolveTrackedBagId(
    const PlayerInventoryReplica& inventory,
    const std::uint64_t bag_guid) {
  if (bag_guid == 0) {
    return std::nullopt;
  }

  for (std::uint8_t bag_id = 1; bag_id <= PlayerInventoryReplica::kMaxBags; ++bag_id) {
    if (const auto *bag = inventory.GetBag(bag_id); bag != nullptr && bag->guid == bag_guid) {
      return bag_id;
    }
  }

  for (std::uint8_t bag_index = 0; bag_index < PlayerInventoryReplica::kMaxBankBags; ++bag_index) {
    if (const auto *bag = inventory.GetBankBag(bag_index);
        bag != nullptr && bag->guid == bag_guid) {
      return static_cast<std::uint8_t>(bag_index + 5u);
    }
  }

  return std::nullopt;
}

ContainerFrameTrackedBagSlotChangeSummary
SummarizeContainerFrameFunc2SlotChanges(const PlayerInventoryReplica& inventory,
                                        const CGContainer_C &container,
                                        const FieldUpdateBatch &updates) {
  ContainerFrameTrackedBagSlotChangeSummary summary;
  const auto bag_id =
      ResolveTrackedBagId(inventory, container.GetGuid().GetRawValue());
  if (!bag_id.has_value()) {
    return summary;
  }

  summary.bag_id = *bag_id;
  std::array<bool, 36> seen_slots{};

  for (const auto field_index : updates.updated_fields) {
    if (field_index < CONTAINER_FIELD_SLOT_1 || field_index >= CONTAINER_FIELD_SLOT_1 + 72u) {
      continue;
    }

    const auto base_field =
        static_cast<std::uint16_t>(field_index - ((field_index - CONTAINER_FIELD_SLOT_1) & 1u));
    const auto slot_index = static_cast<std::uint8_t>((base_field - CONTAINER_FIELD_SLOT_1) / 2u);
    if (slot_index >= seen_slots.size() || seen_slots[slot_index]) {
      continue;
    }
    seen_slots[slot_index] = true;

    const auto new_low = container.GetUInt32(base_field);
    const auto new_high = container.GetUInt32(static_cast<std::uint16_t>(base_field + 1u));
    const auto *old_low_change = FindValueChange(updates, base_field);
    const auto *old_high_change =
        FindValueChange(updates, static_cast<std::uint16_t>(base_field + 1u));
    const auto old_low = old_low_change != nullptr ? old_low_change->old_value : new_low;
    const auto old_high = old_high_change != nullptr ? old_high_change->old_value : new_high;
    const auto old_guid = BuildGuidFromWords(old_low, old_high);
    const auto new_guid = BuildGuidFromWords(new_low, new_high);
    if (old_guid == new_guid) {
      continue;
    }

    summary.has_slot_guid_change = true;
    summary.has_trade_skill_refresh = true;
    ++summary.changed_slot_count;
    if (old_guid != 0) {
      summary.old_item_guids.push_back({old_guid, slot_index});
    }
  }

  return summary;
}

ContainerFrameTrackedSlotChangeSummary
SummarizeContainerFrameFunc3SlotChanges(WorldSession &session, const CGPlayer_C &player,
                                        const FieldUpdateBatch &updates) {
  ContainerFrameTrackedSlotChangeSummary summary;
  std::array<bool, InventorySlots::kTotalSlots> seen_slots{};
  auto &inventory = session.inventory_replica();

  for (const auto field_index : updates.updated_fields) {
    if (field_index < PLAYER_FIELD_INV_SLOT_HEAD ||
        field_index >= PLAYER_FIELD_CURRENCYTOKEN_SLOT_1 + 64) {
      continue;
    }

    const auto base_field =
        static_cast<std::uint16_t>(field_index - ((field_index - PLAYER_FIELD_INV_SLOT_HEAD) & 1u));
    const auto abs_slot = static_cast<std::uint8_t>((base_field - PLAYER_FIELD_INV_SLOT_HEAD) / 2u);
    if (!IsContainerFrameFunc3Slot(abs_slot) || seen_slots[abs_slot]) {
      continue;
    }
    seen_slots[abs_slot] = true;

    const auto old_guid = inventory.GetSlotGuid(abs_slot);
    const auto new_guid = player.GetGuidField(base_field).GetRawValue();
    if (old_guid == new_guid) {
      continue;
    }

    summary.has_slot_guid_change = true;
    summary.has_trade_skill_refresh = true;
    if (old_guid != 0) {
      summary.old_item_guids.push_back({old_guid, abs_slot});
    }

    std::uint32_t old_entry = 0;
    if (const auto *previous_item = inventory.GetItemInSlot(abs_slot); previous_item != nullptr) {
      old_entry = previous_item->entry;
    }

    std::uint32_t new_entry = 0;
    if (new_guid != 0) {
      if (const auto *new_item = session.objects().GetItem(ObjectGuid(new_guid));
          new_item != nullptr) {
        new_entry = new_item->GetEntry();
      }
    }

    if (IsCurrencyTokenSlot(abs_slot) ||
        IsMoneyItemEntry(session.item_definitions(), old_entry) ||
        IsMoneyItemEntry(session.item_definitions(), new_entry)) {
      summary.has_currency_display_refresh = true;
    }
  }

  return summary;
}

}

namespace {

bool DidPlayerAppearanceByteChange(const std::uint32_t old_value, const std::uint32_t new_value,
                                   const std::uint32_t shift) {
  return ((old_value >> shift) & 0xFFu) != ((new_value >> shift) & 0xFFu);
}

bool HasPlayerAppearanceDescriptorChange(const FieldUpdateBatch &updates) {
  for (const auto &change : updates.value_changes) {
    switch (change.field_index) {
    case PLAYER_BYTES:
      if (DidPlayerAppearanceByteChange(change.old_value, change.new_value, 0) ||
          DidPlayerAppearanceByteChange(change.old_value, change.new_value, 16) ||
          DidPlayerAppearanceByteChange(change.old_value, change.new_value, 24)) {
        return true;
      }
      break;
    case PLAYER_BYTES_2:
      if (DidPlayerAppearanceByteChange(change.old_value, change.new_value, 0)) {
        return true;
      }
      break;
    default:
      break;
    }
  }
  return false;
}

bool HasUpdatedField(const FieldUpdateBatch &updates, const std::uint16_t field_index) {
  return std::find(updates.updated_fields.begin(), updates.updated_fields.end(), field_index) !=
         updates.updated_fields.end();
}

bool TryGetVisibleItemEntrySlot(const std::uint16_t field_index, std::uint8_t &slot_index) {
  if (field_index < PLAYER_VISIBLE_ITEM_1_ENTRYID ||
      field_index >= PLAYER_VISIBLE_ITEM_1_ENTRYID + 19u * 2u) {
    return false;
  }

  const auto relative_index =
      static_cast<std::uint16_t>(field_index - PLAYER_VISIBLE_ITEM_1_ENTRYID);
  if ((relative_index & 1u) != 0u) {
    return false;
  }

  slot_index = static_cast<std::uint8_t>(relative_index / 2u);
  return true;
}

void DispatchLocalPlayerVisibleItemCombatEvents(const CGObject_C &object,
                                                const FieldUpdateBatch &updates,
                                                const ObjectGuid local_player_guid,
                                                ui::game::ScriptEventDispatch &dispatch) {
  if (object.GetTypeId() != TypeID::kPlayer || object.GetGuid() != local_player_guid) {
    return;
  }

  const auto guid = object.GetGuid().GetRawValue();
  for (const auto &change : updates.value_changes) {
    std::uint8_t slot_index = 0;
    if (!TryGetVisibleItemEntrySlot(change.field_index, slot_index)) {
      continue;
    }

    if (slot_index == 15u || slot_index == 16u) {
      dispatch.FireUnitAttack(guid);
    } else if (slot_index == 17u) {
      dispatch.FireUnitRangedAttackPower(guid);
    }
  }
}

std::string ResolveCalendarInviteeName(const WorldSession &session,
                                       const std::uint64_t guid_value) {
  if (guid_value == 0) {
    return {};
  }

  const ObjectGuid guid(guid_value);
  if (const auto *player = session.objects().GetPlayer(guid)) {

    if (auto name = player->ResolveRetailName(session); !name.empty()) {
      return name;
    }
  }
  if (const auto *name_info = session.query_cache().GetPlayerName(guid_value)) {
    return name_info->name;
  }
  if (const auto *name_entry = session.objects().GetNameEntry(guid)) {
    return name_entry->name;
  }
  return {};
}

}

void WorldSession::HandleLoginVerifyWorld(const net::wotlk::WorldPacket &pkt) {
  LoginVerifyWorld parsed;
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadU32(parsed.map_id) || !reader.ReadFloat(parsed.x) ||
      !reader.ReadFloat(parsed.y) || !reader.ReadFloat(parsed.z) ||
      !reader.ReadFloat(parsed.orientation)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Malformed SMSG_LOGIN_VERIFY_WORLD");
    return;
  }

  (void)ApplyLoginVerifyWorld(parsed);
}

bool WorldSession::ApplyLoginVerifyWorld(const LoginVerifyWorld &verify) {
  const bool needs_world_entry_dispatch = !has_current_map_ || verify.map_id != current_map_id_;

  if (needs_world_entry_dispatch) {
    pending_quest_poi_queries_.clear();
    std::string map_internal_name;
    if (!ResolveWorldTransferMap(verify.map_id, &map_internal_name)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Bad SMSG_NEW_WORLD zoneID");
      return false;
    }

    DispatchWorldTransfer(WorldTransferRequest{
        .map_id = verify.map_id,
        .x = verify.x,
        .y = verify.y,
        .z = verify.z,
        .orientation = verify.orientation,
        .map_internal_name = std::move(map_internal_name),
        .send_worldport_ack = false,
        .state_after_dispatch = WorldState::kLoadingScreen,
    });
  }

  return true;
}

bool WorldSession::HandleUpdateObject(const net::wotlk::WorldPacket &pkt) {
  if (pkt.payload.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Malformed SMSG_UPDATE_OBJECT: empty payload");
    return false;
  }

  if (!objects().HandleUpdateObject(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Malformed SMSG_UPDATE_OBJECT: object publication failed");
    return false;
  }

  return true;
}

bool WorldSession::HandleCompressedUpdateObject(const net::wotlk::WorldPacket &pkt) {
  if (pkt.payload.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Malformed SMSG_COMPRESSED_UPDATE_OBJECT: empty payload");
    return false;
  }

  if (!objects().HandleCompressedUpdateObject(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "Malformed SMSG_COMPRESSED_UPDATE_OBJECT: object publication failed");
    return false;
  }

  return true;
}

void WorldSession::NotifyTrackedGuidInvalidated(const std::uint64_t guid) {
  if (tracked_guid_invalidation_callback_) {
    tracked_guid_invalidation_callback_(guid);
  }
}

bool WorldSession::ShouldInvalidateTrackedGuidForDestroy(const ObjectGuid guid) const {
  if (!GroupSystem::Get().IsActivePlayerPartyOrRaidUnitGuid(objects(), guid.GetRawValue())) {
    return true;
  }

  const auto *unit = objects().GetUnit(guid);
  const auto *active_player = objects().GetActivePlayer();
  if (unit == nullptr || active_player == nullptr) {
    return false;
  }

  return unit->Interaction().IsInSamePartyOrControlledParty(*active_player) ||
         unit->Interaction().IsInSameRaidOrControlledRaid(*active_player);
}

void WorldSession::HandleDestroyObject(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  ObjectGuid guid;
  if (reader.ReadGuid(guid)) {
    const bool destroyed_active_pet =
        guid.GetRawValue() != 0u &&
        guid.GetRawValue() == pet_.pet_bar().guid.GetRawValue();
    std::uint8_t destroy_flag = 0;
    if (reader.ReadU8(destroy_flag) && ShouldInvalidateTrackedGuidForDestroy(guid)) {
      NotifyTrackedGuidInvalidated(guid.GetRawValue());
    }
    if (destroyed_active_pet) {

      ui::game::ScriptEventDispatch::Get().FireEvent(
          ui::game::events::PET_UI_CLOSE);
    }
  }

  objects().HandleDestroyObject(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleTimeSyncReq(const net::wotlk::WorldPacket &pkt) {
  if (objects().GetLocalPlayer() == nullptr) {
    return;
  }

  std::uint32_t counter = 0;
  if (pkt.payload.size() >= sizeof(counter)) {
    std::memcpy(&counter, pkt.payload.data(), sizeof(counter));
    const std::uint32_t client_time_ms =
        client_time_fn_ ? client_time_fn_() : core::GameClock::GetTickCount32();
    time_sync_.RecordServerRequest(counter, client_time_ms);
    if (auto *const mover = ResolveEffectiveMovingUnit(*this);
        mover != nullptr) {
      mover->Movement().Data().QueueTimeSync(client_time_ms, counter);
    }
  }
}

void WorldSession::OnLocalPlayerCreated(const ObjectGuid &guid) {
  active_player_ammo_attachment_id_ = kAmmoProjectileAttachmentId;
  active_player_ammo_attachment_selection_pending_ = true;

  if (const auto *player = objects().GetActivePlayer()) {
    chat_sender_.SyncLocalAfkDisplayState(player->GetPlayerFlags());
  }
  ResumeIncomingChatDelivery();
  if (const auto *const player = objects().GetLocalPlayerTyped(); player != nullptr) {

    movement_.ApplyAuthoritativeMovementInfo(player->GetMovementInfo());
    for (std::size_t index = 0; index < kMaxSpeeds; ++index) {
      const auto speed_type = static_cast<SpeedType>(index);
      movement_.SetSpeed(speed_type, player->GetSpeed(speed_type));
    }
  }
  if (world_camera_ != nullptr) {
    world_camera_->SetBoundObject(guid.GetRawValue());
    if (const auto* const player = objects().GetLocalPlayerTyped();
        player != nullptr) {

      const auto player_position = player->GetPosition();
      world_camera_->SetTarget(player_position.x, player_position.y,
                               player_position.z);
    }
  }

  if (auto *const local_unit = objects().GetMutableUnit(guid);
      local_unit != nullptr) {
    const auto auto_attack_change = combat::ApplyClientControlPermission(
        *this, *local_unit, combat::ClientControlPermission::Granted);
    combat::ui::PresentAutoAttackActivityChange(*this, auto_attack_change);
  }
  player_control_runtime_.InstallInitialActiveMover(
      *this, objects(), missile_trajectory(), guid.GetRawValue());
  runes_.ResetToDefault();
  spell_book_.ApplyPendingInitialSpellSideEffects();
  if (world_map_ != nullptr) {
    world_map_->RegisterActivePlayerExplorationRefresh(guid);
  }
  RebindActivePlayerDescriptorCallbacks(guid);

  if (dbc_ != nullptr) {
    if (const auto *const player = objects().GetLocalPlayerTyped();
        player != nullptr) {
      auto &talent_store = TalentInfoStore::Get();
      talent_store.LoadFromDbc(*dbc_);
      talent_store.InitFromPlayer(player->State().GetClass(),
                                  player->State().GetRace());
    }
  }
  if (const auto *player = objects().GetLocalPlayerTyped(); player != nullptr) {

    inventory_bridge_.OnPlayerInventoryFieldsChanged(*player);
    (void)inventory_bridge_.ConsumeChangedContainers();
    QueueEquipmentPresentation();
    for (const auto entry : inventory_bridge_.ConsumeChangedEntries()) {
      (void)ui::game::detail::RefreshActionSlotsForChangedItemEntry(*this, entry);
      SpellBookFrame::HandleTrackedMultiCastTotemItemEntry(*this, entry);
    }
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "WorldSession: Local player created: " + guid.ToString());

  pet_.ResetStableListState();
  pet_.ResetStablePetSelection();

  pet_.ResetPetBarForEnterWorld();
  if (const auto *player_unit = objects().GetLocalPlayerTyped();
      player_unit != nullptr) {
    const auto controlled_guid = player_unit->State().GetPrimaryControlledUnitGUID();
    if (!controlled_guid.IsEmpty()) {
      Send(net::wotlk::PacketSender::BuildRequestPetInfo());
    }
    RefreshPossessSpellIdFromPlayerAuras(*this, *player_unit);
  }

  petition_.ResetActivePetitionState();

  petition_.ResetGuildRegistrarCharterOffer();

  mail_.ResetForPlayerEnterWorld();
  if (mail_.ConsumeNextMailTimeQueryRequest()) {
    Send(mail_protocol::EncodeQueryNextMailTime());
  }

  battleground_.SetBattlefieldListBattlemasterGuid(0);
  battleground_.SetSelectedBattlefieldListIndex(0);
  BattlefieldInfo::Get().ResetForPlayerEnterWorld();
  interaction_.SendBattlefieldStatus();

  (void)equipment_.apply_use_result(0);
  gm_ticket_.ClearActiveTicketState();
  GuildSystem::Get().ResetGuildBankRuntimeStateOnPlayerEnterWorld();
  GuildSystem::Get().ResetGuildTextRuntimeStateOnPlayerEnterWorld();
  guild_bank_.ResetStateOnPlayerEnterWorld();

  if (const auto *local_player = objects().GetLocalPlayerTyped(); local_player != nullptr) {
    dance_studio().SetActivePlayerClass(
        ToDancePlayerClass(local_player->State().GetClass()));
    if (local_player->GetGuildID() != 0) {
      RequestGuildPermissionsAndWithdrawRefresh(interaction_);
    } else {
      interaction_.SendGuildBankMoneyWithdrawnQuery();
    }
  } else {
    interaction_.SendGuildBankMoneyWithdrawnQuery();
  }

  SyncActivePlayerArenaTeams(false);
  lfg_.ResetProposalEventGateForPlayerEnterWorld();
  interaction_.SendLfgGetStatus();
  interaction_.SendLfdPlayerLockInfoRequest();

  inventory_bridge_.FullResync();
  (void)inventory_bridge_.ConsumeChangedContainers();
  QueueEquipmentPresentation();
  if (group_.IsRaid()) {
    const bool had_pending_raid_self_resolution = pending_raid_roster_local_player_resolution_;
    SyncObservedGroupStateToGroupSystem();
    if (had_pending_raid_self_resolution) {
      const auto *local_player = objects().GetPlayer(guid);
      const auto *cached_name = query_cache_.GetPlayerName(guid.GetRawValue());
      const auto *name_entry = objects().GetNameEntry(guid);

      const bool raid_self_name_resolved =
          (local_player != nullptr && !local_player->ResolveRetailName(*this).empty()) ||
          (cached_name != nullptr && !cached_name->name.empty()) ||
          (name_entry != nullptr && !name_entry->name.empty());
      if (!raid_self_name_resolved) {
        pending_raid_roster_name_queries_.insert(guid.GetRawValue());
        (void)query_cache_.RequestNameQuery(guid.GetRawValue());
      } else if (pending_raid_roster_name_queries_.empty()) {
        ui::game::ScriptEventDispatch::Get().FireRaidRosterUpdate();
      }
    }
  }
  CurrencySystem::Get().OnCurrencyDisplayUpdate(objects());
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::CURRENCY_DISPLAY_UPDATE);
  ui::game::detail::RefreshActionBarBootstrapState(*this);
  if (const auto* const local_player = objects().GetLocalPlayerTyped();
      local_player != nullptr) {
    const auto form_id =
        static_cast<std::uint32_t>(local_player->Animation().GetShapeshiftForm());
    const auto* const form =
        form_id != 0u ? dbc_->spell_shapeshift_form().LookupEntry(form_id)
                      : nullptr;
    if (form != nullptr &&
        (form->flags & kShapeshiftFormFlagSuppressCreateUiRefresh) == 0u) {
      RefreshPlayerShapeshiftUiState(*this);
    }
  }
  RefreshQuestRuntimeFromPlayer(true);
  RequestVisibleQuestgiverStatusRefresh();

  if (const auto *local_player = objects().GetLocalPlayerTyped();
      local_player != nullptr && local_player->State().IsDead()) {
    interaction_.SendRepopRequest(true);

    if (auto *const mover = objects().GetMutablePlayer(objects().GetActivePlayerGuid());
        mover != nullptr) {
      mover->Movement().ArmDeferredAutoRelease();
    }
  }

  BootstrapCommentatorEnterWorld();

  ApplyStoredClientControl();

  openwow::net::ClientServices::Instance().CompleteCharacterLoginTransition(true);
  DispatchPendingTriggerCinematicIfReady();

  state_ = WorldState::kInWorld;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "WorldSession: Entered world state (local player created)");
}

void WorldSession::RebindActivePlayerDescriptorCallbacks(const ObjectGuid &guid) {

  RegisterActivePlayerArenaTeamRefresh(guid);
  RegisterActivePlayerCharacterPointsRefresh(guid);
  RegisterActivePlayerNoReagentCostRefresh(guid);
  RegisterActivePlayerAmmoInventoryRefresh(guid);
  RegisterActivePlayerBuybackRefresh(guid);
  RegisterActivePlayerBankBagSlotCountRefresh(guid);
  RegisterActivePlayerRegenRefresh(guid);
  RegisterActivePlayerGlyphRefresh(guid);
  RegisterActivePlayerPetSpellPowerRefresh(guid);
  RegisterActivePlayerCombatRatingRefresh(guid);
  RegisterActivePlayerDailyQuestRefresh(guid);
  RegisterActivePlayerFieldBytes2Refresh(guid);
  RegisterActivePlayerRestStateRefresh(guid);
  RegisterActivePlayerShapeshiftFormRefresh(guid);
  RegisterActivePlayerControlGuidRefresh(guid);
  RegisterActivePlayerCritterRefresh(guid);
  RegisterActivePlayerCoinageRefresh(guid);
  RegisterActivePlayerCurrencyRefresh(guid);
  RegisterActivePlayerPushPlayerEvents(guid);
  RegisterActivePlayerSkillRefresh(guid);
}

void WorldSession::SyncActivePlayerArenaTeams(const bool fire_update_event) {
  const auto *player = objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return;
  }

  auto &arena_system = ArenaSystem::Get();
  for (std::uint8_t slot = 0; slot < 3; ++slot) {
    const auto previous_team_id = arena_system.GetTeamId(slot);
    const auto team_info = player->GetArenaTeamInfo(slot);
    arena_system.UpdateLocalPlayerTeam(slot, team_info.team_id, team_info.weekly_games_played,
                                       team_info.weekly_games_won, team_info.personal_rating,
                                       team_info.IsLocalPlayerCaptain());
    if (previous_team_id != 0 && team_info.team_id == 0) {
      arena_.EraseArenaTeamQuery(previous_team_id);
    }

    if (team_info.team_id == 0u) {
      continue;
    }
    const auto now_tick_ms = openwow::core::GameClock::GetTickCount32();
    auto& next_query_tick_ms = active_player_arena_team_query_deadlines_ms_[slot];
    if (next_query_tick_ms == 0u ||
        static_cast<std::int32_t>(now_tick_ms - next_query_tick_ms) >= 0) {
      interaction_.SendArenaTeamQuery(team_info.team_id);
      next_query_tick_ms = now_tick_ms + kActivePlayerArenaTeamQueryThrottleMs;
    }
  }

  if (fire_update_event) {
    openwow::ui::game::ScriptEventDispatch::Get().FireGlobalEvent(
        openwow::ui::game::events::ARENA_TEAM_UPDATE);
  }
}

bool WorldSession::RequestActivePlayerArenaRoster(const std::uint8_t slot) {
  if (slot >= 3) {
    return false;
  }

  const auto *player = objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return false;
  }

  const auto team_info = player->GetArenaTeamInfo(slot);
  if (team_info.team_id == 0) {
    return false;
  }

  const auto now_tick_ms = openwow::core::GameClock::GetTickCount32();
  if (!battleground_.BeginArenaRosterRequest(slot, now_tick_ms)) {
    return false;
  }

  interaction_.SendArenaTeamRoster(team_info.team_id);
  return true;
}

void WorldSession::RegisterActivePlayerArenaTeamRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerArenaTeamRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerArenaTeamInfoRelativeOffsetBytes,
      kPlayerArenaTeamIdSizeBytes,
      [this](const DescriptorFieldChangeView &) { SyncActivePlayerArenaTeams(true); });
  active_player_arena_team_callback_handle_ = handle.value;
}

void WorldSession::UnregisterActivePlayerArenaTeamRefresh() {
  if (active_player_arena_team_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_arena_team_callback_handle_});
  active_player_arena_team_callback_handle_ = 0;
}

void WorldSession::RegisterActivePlayerCharacterPointsRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerCharacterPointsRefresh();
  if (!guid) {
    return;
  }

  const auto *active_player_at_register = objects().GetLocalPlayerTyped();

  const auto last_points2 = std::make_shared<std::uint32_t>(
      active_player_at_register != nullptr
          ? active_player_at_register->GetUInt32(PLAYER_CHARACTER_POINTS2)
          : 0u);

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerCharacterPoints1RelativeOffsetBytes,
      kPlayerCharacterPointsSizeBytes,
      [this, last_points2](const DescriptorFieldChangeView &change) {
        const auto *active_player = objects().GetLocalPlayerTyped();
        if (active_player == nullptr || change.old_words.empty() ||
            change.new_words.empty()) {
          return;
        }

        const std::int32_t old_points1 = static_cast<std::int32_t>(change.old_words.front());
        const std::int32_t new_points1 = static_cast<std::int32_t>(change.new_words.front());
        const std::uint32_t new_points2 = active_player->GetUInt32(PLAYER_CHARACTER_POINTS2);
        const std::int32_t delta1 = new_points1 - old_points1;
        const std::int32_t delta2 =
            static_cast<std::int32_t>(new_points2) - static_cast<std::int32_t>(*last_points2);
        *last_points2 = new_points2;

        openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
            openwow::ui::game::events::CHARACTER_POINTS_CHANGED,
            {static_cast<int>(delta1), static_cast<int>(delta2)});
      });
  if (handle) {
    active_player_character_points_callback_handles_.push_back(handle.value);
  }
}

void WorldSession::UnregisterActivePlayerCharacterPointsRefresh() {
  for (const auto handle : active_player_character_points_callback_handles_) {
    DescriptorCallbackRegistry::Get().Unregister(
        DescriptorCallbackRegistry::Handle{handle});
  }
  active_player_character_points_callback_handles_.clear();
}

void WorldSession::RegisterActivePlayerNoReagentCostRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerNoReagentCostRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerNoReagentCostRelativeOffsetBytes,
      kPlayerNoReagentCostSizeBytes, [this](const DescriptorFieldChangeView &) {
        if (objects().GetLocalPlayerTyped() == nullptr) {
          return;
        }

        spellbook_private_usability().Refresh(*this);
      });
  active_player_no_reagent_cost_callback_handle_ = handle.value;
}

void WorldSession::UnregisterActivePlayerNoReagentCostRefresh() {
  if (active_player_no_reagent_cost_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_no_reagent_cost_callback_handle_});
  active_player_no_reagent_cost_callback_handle_ = 0;
}

void WorldSession::RegisterActivePlayerAmmoInventoryRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerAmmoInventoryRefresh();
  if (!guid) {
    return;
  }

  const auto raw_guid = guid.GetRawValue();
  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerAmmoFieldRelativeOffsetBytes, sizeof(std::uint32_t),
      [this, raw_guid](const DescriptorFieldChangeView &) {
        auto &dispatch = openwow::ui::game::ScriptEventDispatch::Get();
        if (openwow::ui::game::detail::RefreshAllActionSlotValidation(*this)) {
          dispatch.FireActionbarUpdateUsable();
        }

        active_player_ammo_attachment_id_ = kAmmoProjectileAttachmentId;
        active_player_ammo_attachment_selection_pending_ = false;
        QueueEquipmentPresentation();
        dispatch.FireUnitInventoryChanged(raw_guid);
      });
  active_player_ammo_callback_handle_ = handle.value;
}

void WorldSession::UnregisterActivePlayerAmmoInventoryRefresh() {
  if (active_player_ammo_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_ammo_callback_handle_});
  active_player_ammo_callback_handle_ = 0;
}

void WorldSession::RegisterActivePlayerBuybackRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerBuybackRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerBuybackFieldRelativeOffsetBytes, kPlayerBuybackFieldSizeBytes,
      [](const DescriptorFieldChangeView &) {
        ui::game::ScriptEventDispatch::Get().FireMerchantUpdate();
      });
  active_player_buyback_callback_handle_ = handle.value;
}

void WorldSession::UnregisterActivePlayerBuybackRefresh() {
  if (active_player_buyback_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_buyback_callback_handle_});
  active_player_buyback_callback_handle_ = 0;
}

void WorldSession::RegisterActivePlayerBankBagSlotCountRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerBankBagSlotCountRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerBankBagSlotCountOffsetBytes,
      kPlayerBankBagSlotCountSizeBytes, [](const DescriptorFieldChangeView &change) {
        if (change.old_words.empty() || change.new_words.empty()) {
          return;
        }

        constexpr std::uint32_t kBankBagSlotCountMask = 0x00FF0000u;
        if (((change.old_words.front() ^ change.new_words.front()) &
             kBankBagSlotCountMask) != 0u) {
          ui::game::ScriptEventDispatch::Get().FirePlayerBankBagSlotsChanged();
        }
      });
  active_player_bank_bag_slot_callback_handle_ = handle.value;
}

void WorldSession::UnregisterActivePlayerBankBagSlotCountRefresh() {
  if (active_player_bank_bag_slot_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_bank_bag_slot_callback_handle_});
  active_player_bank_bag_slot_callback_handle_ = 0;
}

void WorldSession::RegisterActivePlayerRegenRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerRegenRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kUnit, kUnitFlagsRelativeOffsetBytes, sizeof(std::uint32_t),
      [this](const DescriptorFieldChangeView &change) {
        if (change.old_words.empty() || change.new_words.empty()) {
          return;
        }

        const auto old_flags = change.old_words.front();
        const auto new_flags = change.new_words.front();
        if (((old_flags ^ new_flags) & kUnitFlagInCombat) == 0u) {
          return;
        }

        const bool in_combat = (new_flags & kUnitFlagInCombat) != 0u;

        ui::game::SecureExecution::Get().SetInCombatLockdown(in_combat);

        RefreshLocalPlayerCombatUsability(*this);

        auto &dispatch = ui::game::ScriptEventDispatch::Get();
        if (in_combat) {
          dispatch.FirePlayerRegenDisabled();
        } else {
          dispatch.FirePlayerRegenEnabled();
        }

        if (local_player_combat_flag_changed_callback_) {
          local_player_combat_flag_changed_callback_();
        }
      });
  active_player_regen_callback_handle_ = handle.value;

  if (active_player_regen_callback_handle_ != 0) {
    const auto *player = objects().GetMutablePlayer(guid);
    ui::game::SecureExecution::Get().SetInCombatLockdown(
        player != nullptr &&
        (player->GetUInt32(UNIT_FIELD_FLAGS) & kUnitFlagInCombat) != 0u);
  }
}

void WorldSession::UnregisterActivePlayerRegenRefresh() {
  if (active_player_regen_callback_handle_ != 0) {
    DescriptorCallbackRegistry::Get().Unregister(
        DescriptorCallbackRegistry::Handle{active_player_regen_callback_handle_});
    active_player_regen_callback_handle_ = 0;
  }

  ui::game::SecureExecution::Get().SetInCombatLockdown(false);
}

void WorldSession::RegisterActivePlayerDailyQuestRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerDailyQuestRefresh();
  if (!guid) {
    return;
  }

  auto& registry = DescriptorCallbackRegistry::Get();
  active_player_daily_quest_callback_handles_.reserve(kPlayerDailyQuestSlotCount);
  for (std::uint16_t slot = 0; slot < kPlayerDailyQuestSlotCount; ++slot) {
    const auto offset = static_cast<std::uint16_t>(
        kPlayerDailyQuestsRelativeOffsetBytes + slot * kPlayerDailyQuestSlotSizeBytes);
    const auto handle = registry.RegisterObjectSectionCallback(
        guid, TypeID::kPlayer, offset, kPlayerDailyQuestSlotSizeBytes,
        [this](const DescriptorFieldChangeView &) {
          const auto *player = objects().GetLocalPlayerTyped();
          if (player == nullptr) {
            return;
          }

          RequestVisibleQuestgiverStatusRefresh();

          const int filled = static_cast<int>(player->GetDailyQuestCount());

          if (filled == 0 && daily_quests_were_empty_) {
            return;
          }

          char buf[3000];
          buf[0] = '\0';

          if (filled == kMaxDailyQuests) {
            const std::string fmt =
                Localization::Get().GetString("NO_DAILY_QUESTS_REMAINING");
            std::snprintf(buf, sizeof(buf), "%s", fmt.c_str());
            daily_quests_were_empty_ = false;
          } else {
            const std::string fmt =
                Localization::Get().GetString("DAILY_QUESTS_REMAINING");
            FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(),
                                            kMaxDailyQuests - filled);
            daily_quests_were_empty_ = (filled == 0);
          }

          if (buf[0] != '\0') {
            ChatFrame_DisplayMessage(objects(), buf, 0, nullptr, 0, nullptr, nullptr,
                                     nullptr, 0, 0, 0, 0, 0, nullptr);
          }
        });
    if (handle) {
      active_player_daily_quest_callback_handles_.push_back(handle.value);
    }
  }
}

void WorldSession::UnregisterActivePlayerDailyQuestRefresh() {
  auto& registry = DescriptorCallbackRegistry::Get();
  for (const auto handle : active_player_daily_quest_callback_handles_) {
    registry.Unregister(DescriptorCallbackRegistry::Handle{handle});
  }
  active_player_daily_quest_callback_handles_.clear();
}

void WorldSession::RegisterActivePlayerGlyphRefresh(const ObjectGuid& guid) {
  UnregisterActivePlayerGlyphRefresh();
  if (!guid) {
    return;
  }

  auto& registry = DescriptorCallbackRegistry::Get();
  active_player_glyph_callback_handles_.reserve(kPlayerGlyphSlotCount + 1u);

  for (std::uint16_t slot = 0; slot < kPlayerGlyphSlotCount; ++slot) {
    const auto offset = static_cast<std::uint16_t>(
        kPlayerGlyphsRelativeOffsetBytes + slot * kPlayerGlyphSlotSizeBytes);
    const auto handle = registry.RegisterObjectSectionCallback(
        guid, TypeID::kPlayer, offset, kPlayerGlyphSlotSizeBytes,
        [this, slot](const DescriptorFieldChangeView& change) {
          if (change.old_words.empty() || change.new_words.empty()) {
            return;
          }

          const auto* const dbc = GetDbcLoader();
          if (dbc == nullptr) {
            return;
          }

          const auto is_known_glyph = [dbc](const std::uint32_t glyph_id) {
            return glyph_id != 0u &&
                   dbc->glyph_properties().LookupEntry(glyph_id) != nullptr;
          };
          const bool had_glyph = is_known_glyph(change.old_words.front());
          const bool has_glyph = is_known_glyph(change.new_words.front());
          if (had_glyph == has_glyph && !had_glyph) {
            return;
          }

          const char* event_name = nullptr;
          if (!had_glyph) {
            event_name = ui::game::events::GLYPH_ADDED;
          } else if (!has_glyph) {
            event_name = ui::game::events::GLYPH_REMOVED;
          } else {
            event_name = ui::game::events::GLYPH_UPDATED;
          }
          ui::game::ScriptEventDispatch::Get().FireEventArgs(
              event_name, {std::to_string(static_cast<unsigned>(slot + 1u))});
        });
    if (handle) {
      active_player_glyph_callback_handles_.push_back(handle.value);
    }
  }

  const auto enabled_handle = registry.RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerGlyphsEnabledRelativeOffsetBytes,
      kPlayerGlyphSlotSizeBytes, [](const DescriptorFieldChangeView& change) {
        if (change.old_words.empty() || change.new_words.empty()) {
          return;
        }

        const auto old_mask = change.old_words.front();
        const auto new_mask = change.new_words.front();
        for (std::uint16_t slot = 0; slot < kPlayerGlyphSlotCount; ++slot) {
          const auto bit = std::uint32_t{1} << slot;
          if ((old_mask & bit) == (new_mask & bit)) {
            continue;
          }

          ui::game::ScriptEventDispatch::Get().FireEventArgs(
              (new_mask & bit) != 0u ? ui::game::events::GLYPH_ENABLED
                                     : ui::game::events::GLYPH_DISABLED,
              {std::to_string(static_cast<unsigned>(slot + 1u))});
        }
      });
  if (enabled_handle) {
    active_player_glyph_callback_handles_.push_back(enabled_handle.value);
  }
}

void WorldSession::UnregisterActivePlayerGlyphRefresh() {
  auto& registry = DescriptorCallbackRegistry::Get();
  for (const auto handle : active_player_glyph_callback_handles_) {
    registry.Unregister(DescriptorCallbackRegistry::Handle{handle});
  }
  active_player_glyph_callback_handles_.clear();
}

void WorldSession::RegisterActivePlayerPetSpellPowerRefresh(const ObjectGuid& guid) {
  UnregisterActivePlayerPetSpellPowerRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerPetSpellPowerRelativeOffsetBytes,
      kPlayerPetSpellPowerSizeBytes, [](const DescriptorFieldChangeView&) {
        ui::game::ScriptEventDispatch::Get().FirePetSpellPowerUpdate();
      });
  if (handle) {
    active_player_pet_spell_power_callback_handle_ = handle.value;
  }
}

void WorldSession::UnregisterActivePlayerPetSpellPowerRefresh() {
  if (active_player_pet_spell_power_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_pet_spell_power_callback_handle_});
  active_player_pet_spell_power_callback_handle_ = 0;
}

void WorldSession::RegisterActivePlayerCombatRatingRefresh(const ObjectGuid& guid) {
  UnregisterActivePlayerCombatRatingRefresh();
  if (!guid) {
    return;
  }

  auto& registry = DescriptorCallbackRegistry::Get();
  active_player_combat_rating_callback_handles_.reserve(
      kPlayerCombatRatingRegistrations.size());
  for (const auto& registration : kPlayerCombatRatingRegistrations) {
    const auto handle = registry.RegisterObjectSectionCallback(
        guid, TypeID::kPlayer, registration.offset_bytes, registration.size_bytes,
        [this](const DescriptorFieldChangeView&) { OnCombatRatingUpdate(*this); });
    if (handle) {
      active_player_combat_rating_callback_handles_.push_back(handle.value);
    }
  }
}

void WorldSession::UnregisterActivePlayerCombatRatingRefresh() {
  auto& registry = DescriptorCallbackRegistry::Get();
  for (const auto handle : active_player_combat_rating_callback_handles_) {
    registry.Unregister(DescriptorCallbackRegistry::Handle{handle});
  }
  active_player_combat_rating_callback_handles_.clear();
}

void WorldSession::RegisterActivePlayerPushPlayerEvents(const ObjectGuid &guid) {
  UnregisterActivePlayerPushPlayerEvents();
  if (!guid) {
    return;
  }

  auto &registry = DescriptorCallbackRegistry::Get();
  active_player_push_player_callback_handles_.reserve(kPushPlayerEventRegistrations.size());
  for (const auto &registration : kPushPlayerEventRegistrations) {
    const auto handle = registry.RegisterObjectSectionCallback(
        guid, registration.section_type, registration.offset_bytes,
        registration.size_bytes,
        [event_name = registration.event_name](const DescriptorFieldChangeView &) {
          PushPlayerUnitToken(event_name);
        });
    if (handle) {
      active_player_push_player_callback_handles_.push_back(handle.value);
    }
  }
}

void WorldSession::UnregisterActivePlayerPushPlayerEvents() {
  auto &registry = DescriptorCallbackRegistry::Get();
  for (const auto handle : active_player_push_player_callback_handles_) {
    registry.Unregister(DescriptorCallbackRegistry::Handle{handle});
  }
  active_player_push_player_callback_handles_.clear();
}

void WorldSession::RegisterActivePlayerFieldBytes2Refresh(const ObjectGuid &guid) {
  UnregisterActivePlayerFieldBytes2Refresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerFieldBytes2RelativeOffsetBytes,
      kPlayerFieldBytes2OverrideSpellSize,
      [this](const DescriptorFieldChangeView &) {
        if (objects().GetLocalPlayerTyped() == nullptr) {
          return;
        }
        HandlePlayerFieldBytes2Changed(*this);
      });
  active_player_field_bytes2_callback_handle_ = handle.value;
}

void WorldSession::RegisterActivePlayerRestStateRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerRestStateRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerFieldBytes2RestStateOffsetBytes,
      kPlayerFieldBytes2RestStateSize,
      [](const DescriptorFieldChangeView &change) {

        std::uint8_t rest_state = 0;
        if (!change.new_words.empty()) {
          rest_state = static_cast<std::uint8_t>((change.new_words[0] >> 24) & 0xFFu);
        }
        OnRestStateDescriptorChanged(rest_state);
      });
  active_player_rest_state_callback_handle_ = handle.value;
}

void WorldSession::UnregisterActivePlayerRestStateRefresh() {
  if (active_player_rest_state_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_rest_state_callback_handle_});
  active_player_rest_state_callback_handle_ = 0;
}

void WorldSession::UnregisterActivePlayerFieldBytes2Refresh() {
  if (active_player_field_bytes2_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_field_bytes2_callback_handle_});
  active_player_field_bytes2_callback_handle_ = 0;
}

void WorldSession::RegisterActivePlayerShapeshiftFormRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerShapeshiftFormRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kUnit, kUnitFieldBytes2ShapeshiftFormOffsetBytes,
      kUnitFieldBytes2ShapeshiftFormSize,
      [this](const DescriptorFieldChangeView &change) {
        if (objects().GetLocalPlayerTyped() == nullptr) {
          return;
        }

        std::uint8_t new_form = 0;
        if (!change.new_words.empty()) {
          new_form = static_cast<std::uint8_t>((change.new_words[0] >> 24) & 0xFF);
        }
        OnPlayerShapeshiftFormChanged(*this, new_form);
      });
  active_player_shapeshift_form_callback_handle_ = handle.value;
}

void WorldSession::UnregisterActivePlayerShapeshiftFormRefresh() {
  if (active_player_shapeshift_form_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_shapeshift_form_callback_handle_});
  active_player_shapeshift_form_callback_handle_ = 0;
}

void WorldSession::RegisterActivePlayerControlGuidRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerControlGuidRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kUnit, kUnitCharmRelativeOffsetBytes,
      kUnitCharmSizeBytes,
      [this](const DescriptorFieldChangeView &) {
        if (objects().GetLocalPlayerTyped() == nullptr) {
          return;
        }
        OnActiveControlGuidChanged(*this);
      });
  active_player_control_guid_callback_handle_ = handle.value;
}

void WorldSession::UnregisterActivePlayerControlGuidRefresh() {
  if (active_player_control_guid_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_control_guid_callback_handle_});
  active_player_control_guid_callback_handle_ = 0;
}

void WorldSession::RegisterActivePlayerCritterRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerCritterRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kUnit, kUnitCritterRelativeOffsetBytes, kUnitCritterSizeBytes,
      [this](const DescriptorFieldChangeView &change) {
        if (change.old_words.size() < 2u) {
          return;
        }

        const auto *const player = objects().GetLocalPlayerTyped();
        if (player == nullptr) {
          return;
        }

        const ObjectGuid previous_guid =
            ObjectGuid::FromHalves(change.old_words[0], change.old_words[1]);
        const ObjectGuid current_guid = player->GetGuidField(UNIT_FIELD_CRITTER);
        ui::game::detail::RefreshCritterActionBarForDescriptorChange(
            *this, previous_guid.GetEntry(), current_guid.GetEntry());
        ui::game::ScriptEventDispatch::Get().FireEventArgs(
            ui::game::events::COMPANION_UPDATE, {std::string("CRITTER")});
        active_player_critter_descriptor_refresh_pending_ = true;
      });
  active_player_critter_callback_handle_ = handle.value;
}

void WorldSession::UnregisterActivePlayerCritterRefresh() {
  if (active_player_critter_callback_handle_ != 0) {
    DescriptorCallbackRegistry::Get().Unregister(
        DescriptorCallbackRegistry::Handle{active_player_critter_callback_handle_});
    active_player_critter_callback_handle_ = 0;
  }
  active_player_critter_descriptor_refresh_pending_ = false;
}

void WorldSession::RegisterActivePlayerCoinageRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerCoinageRefresh();
  if (!guid) {
    return;
  }

  const auto handle = DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
      guid, TypeID::kPlayer, kPlayerCoinageRelativeOffsetBytes,
      kPlayerCoinageSizeBytes,
      [this](const DescriptorFieldChangeView &) {
        const auto *player = objects().GetLocalPlayerTyped();
        if (player == nullptr) {
          return;
        }
        const std::uint32_t current_money = player->GetUInt32(PLAYER_FIELD_COINAGE);
        OnCoinageDescriptorChanged(*this, current_money);
      });
  active_player_coinage_callback_handle_ = handle.value;
}

void WorldSession::RegisterActivePlayerCurrencyRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerCurrencyRefresh();
  if (!guid) {
    return;
  }

  auto &registry = DescriptorCallbackRegistry::Get();
  active_player_currency_callback_handles_.reserve(
      kPlayerCurrencyRegistrationOffsets.size());
  for (const auto offset : kPlayerCurrencyRegistrationOffsets) {
    const auto handle = registry.RegisterObjectSectionCallback(
        guid, TypeID::kPlayer, offset, kPlayerCurrencySizeBytes,
        [this](const DescriptorFieldChangeView &change) {
          if (!change.old_words.empty() && !change.new_words.empty() &&
              change.old_words.front() < change.new_words.front()) {
            sound_runtime().PlaySoundKitByName("LOOTWINDOWCOINSOUND");
          }
          auto &dispatch = openwow::ui::game::ScriptEventDispatch::Get();
          dispatch.FireEvent(openwow::ui::game::events::HONOR_CURRENCY_UPDATE);
          dispatch.FireEvent(openwow::ui::game::events::CURRENCY_DISPLAY_UPDATE);
        });
    if (handle) {
      active_player_currency_callback_handles_.push_back(handle.value);
    }
  }
}

void WorldSession::UnregisterActivePlayerCurrencyRefresh() {
  auto &registry = DescriptorCallbackRegistry::Get();
  for (const auto handle : active_player_currency_callback_handles_) {
    registry.Unregister(DescriptorCallbackRegistry::Handle{handle});
  }
  active_player_currency_callback_handles_.clear();
}

void WorldSession::UnregisterActivePlayerCoinageRefresh() {
  if (active_player_coinage_callback_handle_ == 0) {
    return;
  }

  DescriptorCallbackRegistry::Get().Unregister(
      DescriptorCallbackRegistry::Handle{active_player_coinage_callback_handle_});
  active_player_coinage_callback_handle_ = 0;
}

void WorldSession::RegisterActivePlayerSkillRefresh(const ObjectGuid &guid) {
  UnregisterActivePlayerSkillRefresh();
  if (!guid) {
    return;
  }

  auto &registry = DescriptorCallbackRegistry::Get();
  active_player_skill_callback_handles_.reserve(
      static_cast<std::size_t>(kPlayerSkillSlotCount) * 4u);

  for (std::uint16_t slot = 0; slot < kPlayerSkillSlotCount; ++slot) {
    const auto slot_offset = static_cast<std::uint16_t>(
        slot * kPlayerSkillSlotStrideBytes);
    const auto register_halfword =
        [this, &registry, guid, slot_offset](
            const std::uint16_t field_offset,
            DescriptorCallback callback) {
          const auto handle = registry.RegisterObjectSectionCallback(
              guid, TypeID::kPlayer,
              static_cast<std::uint16_t>(field_offset + slot_offset),
              kPlayerSkillHalfwordSizeBytes, std::move(callback));
          if (handle) {
            active_player_skill_callback_handles_.push_back(handle.value);
          }
        };

    register_halfword(
        kPlayerSkillValueRelativeOffsetBytes,
        [this, slot](const DescriptorFieldChangeView &change) {
          if (change.old_words.empty()) {
            return;
          }

          const auto *player = objects().GetLocalPlayerTyped();
          if (player == nullptr) {
            return;
          }

          const auto skill = player->GetSkill(slot);
          std::uint32_t new_adjusted_value = skill.value;
          if (new_adjusted_value != 0u) {
            new_adjusted_value += static_cast<std::uint16_t>(skill.step_modifier);
          }

          OnSkillValueDescriptorChanged(
              *this,
              {.slot_index = slot,
               .old_raw_value =
                   static_cast<std::uint16_t>(change.old_words.front()),
               .new_adjusted_value = new_adjusted_value,
               .skill_line_id = skill.skill_id,
               .race = player->State().GetRace(),
               .player_class = player->State().GetClass(),
               .is_active_player = player->IsActivePlayer()});
        });

    register_halfword(
        kPlayerSkillMaximumValueRelativeOffsetBytes,
        [this](const DescriptorFieldChangeView &) {
          OnSkillRangeDescriptorChanged(*this);
        });

    register_halfword(
        kPlayerSkillModifierRelativeOffsetBytes,
        [this](const DescriptorFieldChangeView &) {
          const auto *player = objects().GetLocalPlayerTyped();
          OnSkillModifierDescriptorChanged(
              *this, player != nullptr && player->IsActivePlayer());
        });

    register_halfword(
        kPlayerSkillStepModifierRelativeOffsetBytes,
        [this](const DescriptorFieldChangeView &) {
          OnSkillRangeDescriptorChanged(*this);
        });
  }
}

void WorldSession::UnregisterActivePlayerSkillRefresh() {
  auto &registry = DescriptorCallbackRegistry::Get();
  for (const auto handle : active_player_skill_callback_handles_) {
    registry.Unregister(DescriptorCallbackRegistry::Handle{handle});
  }
  active_player_skill_callback_handles_.clear();
}

void WorldSession::TryBindGameObjectTemplateInfo(WorldObject &obj) {
  if (!obj.IsGameObject()) {
    return;
  }

  auto *const game_object = static_cast<CGGameObject_C*>(&obj);

  if (game_object->GetTemplateInfo() != nullptr) {
    return;
  }

  const auto entry = game_object->GetEntry();
  const auto *template_info = query_cache_.GetOrRequestGameObjectTemplate(
      entry, {.context = game_object->GetGuid().GetRawValue(),
              .callback_key = AsyncQueryChannel::CallbackKey(
                  reinterpret_cast<std::uintptr_t>(this) ^ 0x474F5449u, entry),
              .dedupe_callbacks = true,
              .callback = [this, entry](const bool success) {
                if (!success) {
                  return;
                }
                BindGameObjectTemplateInfoForEntry(entry);
              }});
  if (template_info == nullptr) {
    return;
  }

  game_object->SetTemplateInfo(template_info);
  game_object->RefreshDifficultyVisibilityControlState(*this);
  ReloadReadableObjectAfterAsyncDependency(*this, game_object->GetGuid().GetRawValue());
}

void WorldSession::BindGameObjectTemplateInfoForEntry(const std::uint32_t entry) {
  const auto *template_info = query_cache_.GetGameObjectTemplate(entry);
  if (template_info == nullptr) {
    return;
  }

  objects().ForEach([&](const WorldObject &obj) {
    if (!obj.IsGameObject()) {
      return;
    }

    auto *const game_object = objects().GetMutableGameObject(obj.GetGuid());
    if (game_object == nullptr || game_object->GetEntry() != entry) {
      return;
    }

    if (game_object->GetTemplateInfo() != nullptr) {
      return;
    }

    game_object->SetTemplateInfo(template_info);
    game_object->RefreshDifficultyVisibilityControlState(*this);
    ReloadReadableObjectAfterAsyncDependency(*this, game_object->GetGuid().GetRawValue());
  });
}

void WorldSession::TryRegisterCapturePointObject(const WorldObject &obj) {
  if (!obj.IsGameObject()) {
    return;
  }

  const auto &game_object = static_cast<const CGGameObject_C &>(obj);
  if (!game_object.IsCapturePoint()) {
    return;
  }

  auto &manager = ui::game::GetCapturePointUIManagerState();
  if (manager.ContainsObjectGuid(game_object.GetGuid().GetRawValue())) {
    return;
  }

  const auto entry = game_object.GetEntry();
  const auto *template_info = query_cache_.GetOrRequestGameObjectTemplate(
      entry, {.context = game_object.GetGuid().GetRawValue(),
              .callback_key =
                  AsyncQueryChannel::CallbackKey(reinterpret_cast<std::uintptr_t>(this), entry),
              .dedupe_callbacks = true,
              .callback = [this, entry](const bool success) {
                if (!success) {
                  return;
                }
                RegisterCapturePointObjectsForEntry(entry);
              }});
  if (template_info == nullptr) {
    return;
  }

  ui::game::RegisterCapturePointGameObject(manager, game_object, *template_info);
}

void WorldSession::RegisterCapturePointObjectsForEntry(const std::uint32_t entry) {
  const auto *template_info = query_cache_.GetGameObjectTemplate(entry);
  if (template_info == nullptr) {
    return;
  }

  auto &manager = ui::game::GetCapturePointUIManagerState();
  objects().ForEach([&](const WorldObject &obj) {
    if (!obj.IsGameObject()) {
      return;
    }

    const auto &game_object = static_cast<const CGGameObject_C &>(obj);
    if (game_object.GetEntry() != entry || !game_object.IsCapturePoint()) {
      return;
    }

    ui::game::RegisterCapturePointGameObject(manager, game_object, *template_info);
  });
}

void WorldSession::HandleCapturePointObjectDestroyed(const ObjectGuid &guid) {
  ui::game::UnregisterCapturePointGameObject(ui::game::GetCapturePointUIManagerState(),
                                             guid.GetRawValue(), &world_states_);
}

void WorldSession::RequestVisibleQuestgiverStatusRefresh() {
  const auto *active_player = objects().GetActivePlayer();
  if (active_player == nullptr) {
    return;
  }

  constexpr std::uint32_t kNpcFlagFlightmaster = 0x00002000u;

  objects().EnumVisibleObjectsMutable([&](WorldObject &obj) {
    if (obj.IsUnit() && !obj.IsPlayer()) {
      auto &unit = static_cast<CGUnit_C &>(obj);
      const bool friendly =
          unit.State().GetNpcFlags() != 0 &&
          active_player->Interaction().GetReaction(unit) >= ReactionType::kNeutral &&
          unit.Interaction().GetReaction(*active_player) >= ReactionType::kNeutral;
      if (!friendly) {
        quests_.EraseQuestGiverStatus(unit.GetGuid());

        unit.ClearOverlayModelImmediate();
      } else if ((unit.State().GetNpcFlags() & kNpcFlagFlightmaster) != 0) {
        Send(net::wotlk::PacketSender::BuildQuestgiverStatusQuery(
            unit.GetGuid().GetRawValue()));

        interaction().SendTaxiNodeStatusQuery(unit.GetGuid().GetRawValue());
      }
    } else if (obj.IsGameObject()) {
      auto &go = static_cast<CGGameObject_C &>(obj);
      if (!active_player->Interaction().IsNeutralGameObjectTarget(go)) {
        quests_.EraseQuestGiverStatus(go.GetGuid());

        go.ClearOverlayModelImmediate();
      }
    }
  });

  Send(net::wotlk::PacketSender::BuildQuestgiverStatusMultipleQuery());
}

void WorldSession::RefreshCreatedGameObjectQuestgiverStatus(const WorldObject &obj) {
  if (!obj.IsGameObject()) {
    return;
  }

  const auto &game_object = static_cast<const CGGameObject_C &>(obj);

  if (!game_object.IsQuestGiverType() || objects().GetActivePlayer() == nullptr) {
    quests_.EraseQuestGiverStatus(game_object.GetGuid());
    return;
  }

  Send(net::wotlk::PacketSender::BuildQuestgiverStatusQuery(game_object.GetGuid().GetRawValue()));
}

void WorldSession::ClearDestroyedQuestgiverStatus(const ObjectGuid &guid) {
  quests_.EraseQuestGiverStatus(guid);
}

void WorldSession::HandleActivePlayerAliveTransition(
    const bool suppress_player_alive_event, const bool force_event_dispatch) {

  bool became_alive = true;
  if (death_callbacks_.on_alive) {
    became_alive =
        death_callbacks_.on_alive(suppress_player_alive_event, force_event_dispatch);
  }

  if ((!became_alive && !force_event_dispatch) || suppress_player_alive_event) {
    return;
  }

  RequestVisibleQuestgiverStatusRefresh();
  inventory::ui::ClearActivePlayerItemHighlights(objects());
  ui::game::ScriptEventDispatch::Get().FirePlayerAlive();
}

void WorldSession::HandleActivePlayerDeadTransition(const bool force_event_dispatch) {
  if (death_callbacks_.on_dead) {
    death_callbacks_.on_dead(force_event_dispatch);
  }
}

void WorldSession::EvaluateActivePlayerLifeLevel(const bool force_event_dispatch) {

  const auto *const active_player = objects().GetLocalPlayerTyped();
  if (active_player == nullptr) {
    return;
  }

  const auto current_health =
      static_cast<std::int32_t>(active_player->GetUInt32(UNIT_FIELD_HEALTH));
  if (current_health <= 0) {
    HandleActivePlayerDeadTransition(force_event_dispatch);
  } else {
    HandleActivePlayerAliveTransition(false, force_event_dispatch);
  }
}

void WorldSession::RefreshActivePlayerFactionDependentState() {
  const auto *active_player = objects().GetActivePlayer();
  if (active_player == nullptr) {
    return;
  }

  active_player->Interaction().RefreshFactionDependentState(*this, false);
}

void WorldSession::RefreshQuestRuntimeFromPlayer(bool request_query_time) {
  const auto *player = objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return;
  }

  std::unordered_set<std::uint32_t> previous_quest_ids;
  previous_quest_ids.reserve(quests_.quest_log_count());
  for (const auto &entry : quests_.quest_log()) {
    previous_quest_ids.insert(entry.quest_id);
  }
  quests_.SyncQuestLogFromPlayer(objects(), *player);
  if (!request_query_time) {
    for (const auto &entry : quests_.quest_log()) {
      const auto quest_id = entry.quest_id;
      if (quest_id != 0 && !previous_quest_ids.contains(quest_id)) {
        ui::game::ScriptEventDispatch::Get().FireQuestAccepted(
            static_cast<int>(entry.slot + 1), static_cast<int>(quest_id));
      }
    }
  }

  if (request_query_time) {
    interaction_.SendQueryTime();
  }

  bool waiting_for_template = false;
  std::vector<std::uint32_t> poi_query_ids;
  poi_query_ids.reserve(quests_.quest_log_count());

  for (const auto &entry : quests_.quest_log()) {
    if (entry.quest_id == 0) {
      continue;
    }

    const auto *tmpl = quests_.GetOrRequestTemplate(
        entry.quest_id, {.callback_key = AsyncQueryChannel::CallbackKey(
                             reinterpret_cast<std::uintptr_t>(this), entry.quest_id),
                         .dedupe_callbacks = true,
                         .callback = [this](bool success) {
                           if (!success) {

                             openwow::debug::DebugConsole::Get().Write(
                                 "Invalid quest log entry");
                             return;
                           }
                           ui::game::ScriptEventDispatch::Get().FireQuestQueryComplete();
                           RefreshQuestRuntimeFromPlayer(false);
                         }});
    if (tmpl == nullptr) {
      waiting_for_template = true;
      continue;
    }

    if (!QuestPOIData::Get().HasQuestQueryResult(entry.quest_id) &&
        !pending_quest_poi_queries_.contains(entry.quest_id)) {
      poi_query_ids.push_back(entry.quest_id);
    }
  }

  if (waiting_for_template || poi_query_ids.empty()) {
    return;
  }

  pending_quest_poi_queries_.insert(poi_query_ids.begin(), poi_query_ids.end());
  interaction_.SendQuestPoiQuery(poi_query_ids);
}

void WorldSession::OnFieldsChanged(const WorldObject &obj, const FieldUpdateBatch &updates,
                                   bool is_create) {
  auto &dispatch = openwow::ui::game::ScriptEventDispatch::Get();
  ContainerFrameTrackedSlotChangeSummary container_frame_func3_summary{};
  bool container_frame_func3_summary_pending = false;
  ContainerFrameTrackedBagSlotChangeSummary container_frame_func2_summary{};
  bool container_frame_func2_summary_pending = false;
  std::vector<std::uint8_t> changed_player_inventory_slots;

  if (!is_create && (obj.IsItem() || obj.IsContainer())) {
    const bool socket_enchantment_changed =
        HasUpdatedField(updates, static_cast<std::uint16_t>(
                                    ITEM_FIELD_ENCHANTMENT_1_1 +
                                    kEnchantSlotSocket1 * kFieldsPerEnchant)) ||
        HasUpdatedField(updates, static_cast<std::uint16_t>(
                                    ITEM_FIELD_ENCHANTMENT_1_1 +
                                    kEnchantSlotSocket2 * kFieldsPerEnchant)) ||
        HasUpdatedField(updates, static_cast<std::uint16_t>(
                                    ITEM_FIELD_ENCHANTMENT_1_1 +
                                    kEnchantSlotSocket3 * kFieldsPerEnchant));
    if (socket_enchantment_changed) {
      auto* const active_player =
          objects().GetMutablePlayer(objects().GetLocalPlayerGuid());
      if (active_player != nullptr) {
        for (std::uint8_t slot = 0; slot < InventorySlots::kEquipEnd; ++slot) {
          if (active_player->GetInventorySlotGuid(slot) == obj.GetGuid()) {
            active_player->RecountEquippedGemColorCounts();
            break;
          }
        }
      }
    }
  }

  if (obj.GetGuid() == objects().GetLocalPlayerGuid()) {
    if (const auto *local_player = objects().GetLocalPlayerTyped(); local_player != nullptr) {
      dance_studio().SetActivePlayerClass(
          ToDancePlayerClass(local_player->State().GetClass()));
      if (!is_create && obj.GetTypeId() == TypeID::kPlayer) {
        container_frame_func3_summary =
            SummarizeContainerFrameFunc3SlotChanges(*this, *local_player, updates);
        container_frame_func3_summary_pending = container_frame_func3_summary.has_slot_guid_change;
        changed_player_inventory_slots =
            CollectChangedPlayerInventorySlots(*local_player, updates);
      }
    }

    if (obj.GetTypeId() == TypeID::kPlayer &&
        (is_create || HasUpdatedField(updates, UNIT_FIELD_HEALTH))) {
      EvaluateActivePlayerLifeLevel(false);
    }

    if (obj.GetTypeId() == TypeID::kPlayer &&
        (is_create || HasUpdatedField(updates, PLAYER_FLAGS))) {
      constexpr std::uint32_t kPlayerFlagsGhost = 0x10u;
      const bool is_ghost_now =
          (obj.GetUInt32(PLAYER_FLAGS) & kPlayerFlagsGhost) != 0u;
      if (is_ghost_now != active_player_was_ghost_) {
        ResetAndRequeryCorpsePosition();
      }
      active_player_was_ghost_ = is_ghost_now;

      if (!is_create) {
        EvaluateActivePlayerLifeLevel(false);
      }
    }

    if (obj.GetTypeId() == TypeID::kPlayer &&
        (is_create || HasUpdatedField(updates, PLAYER_TRACK_RESOURCES))) {
      RefreshAllVisibleGameObjectLootArt(objects());
    }

    for (const auto &change : updates.value_changes) {
      if (change.field_index < PLAYER_RUNE_REGEN_1 ||
          change.field_index >= PLAYER_RUNE_REGEN_1 + 4) {
        continue;
      }

      float old_regen_rate = 0.0f;
      float new_regen_rate = 0.0f;
      std::memcpy(&old_regen_rate, &change.old_value, sizeof(old_regen_rate));
      std::memcpy(&new_regen_rate, &change.new_value, sizeof(new_regen_rate));
      const std::uint32_t adjusted_mask = runes_.HandleRuneRegenRateChanged(
          static_cast<std::uint8_t>(change.field_index - PLAYER_RUNE_REGEN_1), old_regen_rate,
          new_regen_rate);
      for (int i = 0; i < kClientTrackedRuneSlots; ++i) {
        if ((adjusted_mask & (1u << i)) != 0) {
          dispatch.FireActionbarUpdateCooldown();
        }
      }
    }
  }

  if (!is_create && HasPowerDescriptorChange(updates)) {
    if (obj.GetGuid() == objects().GetLocalPlayerGuid()) {

      spellbook_private_usability().RefreshPower(*this);
    } else if (const auto *local_player = objects().GetLocalPlayerTyped(); local_player != nullptr) {
      const auto pet_guid = local_player->State().GetPetGUID();
      if (!pet_guid.IsEmpty() && obj.GetGuid() == pet_guid) {
        spellbook_private_usability().RefreshPower(*this);
      }
    }
  }

  if (obj.GetGuid() == objects().GetLocalPlayerGuid() && obj.GetTypeId() == TypeID::kPlayer &&
      HasUpdatedField(updates, PLAYER_GUILDID)) {
    if (const auto *local_player = objects().GetLocalPlayerTyped(); local_player != nullptr) {

      if (SyncActivePlayerCalendarGuildState(*this, local_player->GetGuildID())) {
        ui::game::ScriptEventDispatch::Get().FireEvent(
            ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
      }
      TrySyncLoadedChatChannels(*this);
      if (!is_create) {

        RequestGuildPermissionsAndWithdrawRefresh(interaction_);
      }
      if (gossip().merchant().active()) {
        ui::game::ScriptEventDispatch::Get().FireMerchantUpdate();
      }
      GuildSystem::Get().ClearGuildBankTabCacheRuntimeState();
    }
  }

  if (!is_create && obj.GetGuid() == objects().GetLocalPlayerGuid() &&
      obj.GetTypeId() == TypeID::kPlayer && HasUpdatedField(updates, PLAYER_GUILDRANK)) {

    RequestGuildPermissionsAndWithdrawRefresh(interaction_);
  }

  if (!is_create && obj.GetTypeId() == TypeID::kPlayer &&
      HasUpdatedField(updates, PLAYER_DUEL_TEAM)) {
    if (const auto *unit = objects().GetUnit(obj.GetGuid()); unit != nullptr) {
      unit->Interaction().RefreshFactionDependentState(*this, true);
    }
  }

  if (!is_create && obj.GetTypeId() == TypeID::kPlayer &&
      (HasUpdatedField(updates, UNIT_FIELD_CHARMEDBY) ||
       HasUpdatedField(updates, UNIT_FIELD_CHARMEDBY + 1))) {
    if (const auto *player = objects().GetPlayer(obj.GetGuid()); player != nullptr) {
      player->Interaction().RefreshFactionDependentState(*this, false);
      if (obj.GetGuid() == objects().GetLocalPlayerGuid()) {
        dispatch.FirePetBarUpdate();
      }
    }
  }

  if (!is_create && obj.GetTypeId() == TypeID::kPlayer &&
      HasPlayerAppearanceDescriptorChange(updates)) {
    if (obj.GetGuid() == objects().GetLocalPlayerGuid()) {
      BarberShop::Get().MarkDeferredActivePlayerAppearanceRefresh();
      dispatch.FireBarberShopAppearanceApplied();
    } else {
      const auto guid = obj.GetGuid().GetRawValue();
      dispatch.FireUnitPortrait(guid);
      dispatch.FireUnitModel(guid);
    }
  }

  if (!is_create && obj.GetGuid() == objects().GetLocalPlayerGuid()) {
    const auto has_action_bar_driver_change =
        std::find(updates.updated_fields.begin(), updates.updated_fields.end(), UNIT_FIELD_CHARM) !=
            updates.updated_fields.end() ||
        std::find(updates.updated_fields.begin(), updates.updated_fields.end(),
                  UNIT_FIELD_CHARM + 1) != updates.updated_fields.end() ||
        std::find(updates.updated_fields.begin(), updates.updated_fields.end(),
                  UNIT_FIELD_BYTES_2) != updates.updated_fields.end() ||
        std::find(updates.updated_fields.begin(), updates.updated_fields.end(),
                  PLAYER_FIELD_BYTES2) != updates.updated_fields.end();
    if (has_action_bar_driver_change) {
      ui::game::detail::RefreshPetActionBarState(*this);
    }
  }

  if (!is_create && obj.GetTypeId() == TypeID::kContainer) {
    if (const auto *container = objects().GetContainer(obj.GetGuid()); container != nullptr) {
      container_frame_func2_summary =
          SummarizeContainerFrameFunc2SlotChanges(inventory_replica_, *container,
                                                  updates);
      container_frame_func2_summary_pending = container_frame_func2_summary.has_slot_guid_change;
    }
  }

  DescriptorCallbackRegistry::Get().Dispatch(obj, updates, is_create);

  if (!is_create) {
    DispatchLocalPlayerVisibleItemCombatEvents(obj, updates, objects().GetLocalPlayerGuid(),
                                               dispatch);
  }

  if (!changed_player_inventory_slots.empty()) {
    auto *player = objects().GetMutablePlayer(objects().GetLocalPlayerGuid());
    if (player != nullptr) {

      inventory_bridge_.OnPlayerInventoryFieldsChanged(*player);

      if (container_frame_func3_summary_pending) {
        if (container_frame_func3_summary.has_trade_skill_refresh &&
            ProfessionSystem::Get().HasTradeSkillWindow()) {
          dispatch.FireEvent(ui::game::events::TRADE_SKILL_UPDATE);
        }
        for (const auto &cleared : container_frame_func3_summary.old_item_guids) {
          ui::game::GameUI_OnMouseoverUnitLeave(cleared.guid);

          if (item_locks_.ClearForAuthoritativeInventoryPlacement(
                  ObjectGuid(cleared.guid))) {
            FirePlayerRootItemLockEvent(dispatch, ui::game::events::ITEM_LOCK_CHANGED,
                                        cleared.abs_slot);
            FirePlayerRootItemLockEvent(dispatch, ui::game::events::ITEM_UNLOCKED,
                                        cleared.abs_slot);
          }
        }
      }

      for (const auto slot : changed_player_inventory_slots) {
        if (!IsPlayerInventoryActionUsabilitySlot(slot)) {
          continue;
        }
        const auto item_guid = player->GetGuidField(static_cast<std::uint16_t>(
            PLAYER_FIELD_INV_SLOT_HEAD + static_cast<std::uint16_t>(slot) * 2u));
        if (item_guid.IsEmpty() ||
            !item_locks_.ClearForAuthoritativeInventoryPlacement(item_guid)) {
          continue;
        }
        FirePlayerRootItemLockEvent(dispatch, ui::game::events::ITEM_LOCK_CHANGED, slot);
        FirePlayerRootItemLockEvent(dispatch, ui::game::events::ITEM_UNLOCKED, slot);
      }

      const bool player_inventory_action_usability_changed = std::any_of(
          changed_player_inventory_slots.begin(), changed_player_inventory_slots.end(),
          [](const std::uint8_t slot) { return IsPlayerInventoryActionUsabilitySlot(slot); });
      if (player_inventory_action_usability_changed &&
          ui::game::detail::RefreshAllActionSlotValidation(*this)) {
        dispatch.FireActionbarUpdateUsable();
      }

      for (const auto slot : changed_player_inventory_slots) {

        if (slot == InventorySlots::kMainHand || slot == InventorySlots::kOffHand) {
          static_cast<void>(ui::game::detail::RefreshActionSlotsForAttackActions(*this));
        } else if (slot == InventorySlots::kRanged) {
          static_cast<void>(
              ui::game::detail::RefreshActionSlotsForRangedAttackActions(*this));
        }

        if (slot < InventorySlots::kEquipEnd) {
          const auto item_guid = player->GetGuidField(static_cast<std::uint16_t>(
              PLAYER_FIELD_INV_SLOT_HEAD + static_cast<std::uint16_t>(slot) * 2u));
          dispatch.FirePlayerEquipmentChanged(slot, !item_guid.IsEmpty());
        } else if (slot >= InventorySlots::kBankStart &&
                   slot < InventorySlots::kBankBagEnd) {
          dispatch.FirePlayerBankSlotsChanged(static_cast<std::uint8_t>(
              slot - InventorySlots::kBankStart + 1u));
        }

        if (!IsContainerFramePlayerSlot(slot) || IsPlayerInventoryActionUsabilitySlot(slot)) {
          continue;
        }

        if (ui::game::detail::RefreshAllActionSlotValidation(*this)) {
          dispatch.FireActionbarUpdateUsable();
        }

        if (IsContainerFrameFunc5Slot(slot)) {
        }
      }

      if (std::any_of(changed_player_inventory_slots.begin(),
                      changed_player_inventory_slots.end(),
                      [](const std::uint8_t slot) {
                        return slot < InventorySlots::kEquipEnd;
                      })) {
        player->RecountEquippedGemColorCounts();
      }

      if (container_frame_func3_summary_pending &&
          container_frame_func3_summary.has_currency_display_refresh) {
        CurrencySystem::Get().OnCurrencyDisplayUpdate(objects());
        dispatch.FireEvent(ui::game::events::CURRENCY_DISPLAY_UPDATE);
      }
    }
    container_frame_func3_summary_pending = false;
  }

  auto events = MapChangedFieldsToEvents(obj.GetTypeId(), obj.GetGuid().GetRawValue(),
                                         updates.updated_fields, is_create);

  if (container_frame_func2_summary_pending) {
    if (container_frame_func2_summary.has_trade_skill_refresh &&
        ProfessionSystem::Get().HasTradeSkillWindow()) {
      dispatch.FireEvent(ui::game::events::TRADE_SKILL_UPDATE);
    }
    for (const auto &cleared : container_frame_func2_summary.old_item_guids) {
      ui::game::GameUI_OnMouseoverUnitLeave(cleared.guid);

      if (item_locks_.ClearForAuthoritativeInventoryPlacement(
              ObjectGuid(cleared.guid))) {
        dispatch.FireEventArgs(ui::game::events::ITEM_LOCK_CHANGED,
                               {static_cast<int>(container_frame_func2_summary.bag_id),
                                static_cast<int>(cleared.abs_slot) + 1});
        dispatch.FireEventArgs(ui::game::events::ITEM_UNLOCKED,
                               {static_cast<int>(container_frame_func2_summary.bag_id),
                                static_cast<int>(cleared.abs_slot) + 1});
      }
    }
    if (ui::game::detail::RefreshAllActionSlotValidation(*this)) {
      dispatch.FireActionbarUpdateUsable();
    }

    container_frame_func2_summary_pending = false;
  }

  if (events.empty())
    return;

  for (const auto &evt : events) {
    if (!evt.event_name)
      continue;

    if (evt.needs_unit_token) {

      std::uint64_t guid = evt.guid_raw;

      if (std::strcmp(evt.event_name, "UNIT_HEALTH") == 0) {
        dispatch.FireUnitHealth(guid);
      } else if (std::strcmp(evt.event_name, "UNIT_MAXHEALTH") == 0) {
        dispatch.FireUnitMaxHealth(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_MAXMANA") == 0 ||
                 std::strcmp(evt.event_name, "UNIT_MAXRAGE") == 0 ||
                 std::strcmp(evt.event_name, "UNIT_MAXFOCUS") == 0 ||
                 std::strcmp(evt.event_name, "UNIT_MAXENERGY") == 0 ||
                 std::strcmp(evt.event_name, "UNIT_MAXHAPPINESS") == 0 ||
                 std::strcmp(evt.event_name, "UNIT_MAXRUNIC_POWER") == 0) {
        dispatch.FireUnitMaxPowerSpecific(guid, evt.power_type);
      }

      else if (std::strcmp(evt.event_name, "UNIT_MANA") == 0 ||
               std::strcmp(evt.event_name, "UNIT_RAGE") == 0 ||
               std::strcmp(evt.event_name, "UNIT_FOCUS") == 0 ||
               std::strcmp(evt.event_name, "UNIT_ENERGY") == 0 ||
               std::strcmp(evt.event_name, "UNIT_HAPPINESS") == 0 ||
               std::strcmp(evt.event_name, "UNIT_RUNIC_POWER") == 0) {
        dispatch.FireUnitPowerSpecific(guid, evt.power_type);
      }

      else if (std::strcmp(evt.event_name, "UNIT_LEVEL") == 0) {
        dispatch.FireUnitLevel(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_FLAGS") == 0) {
        dispatch.FireUnitFlags(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_TARGET") == 0) {
        dispatch.FireUnitTarget(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_MODEL_CHANGED") == 0) {
        dispatch.FireUnitModel(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_PORTRAIT_UPDATE") == 0) {
        dispatch.FireUnitPortrait(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_STATS") == 0) {
        dispatch.FireUnitStats(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_ATTACK_POWER") == 0) {
        dispatch.FireUnitAttackPower(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_ATTACK_SPEED") == 0) {
        dispatch.FireUnitAttackSpeed(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_DAMAGE") == 0) {
        dispatch.FireUnitAttack(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_RANGEDDAMAGE") == 0 ||
               std::strcmp(evt.event_name, "UNIT_RANGED_ATTACK_POWER") == 0) {
        dispatch.FireUnitRangedAttackPower(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_RESISTANCES") == 0) {
        dispatch.FireUnitResistances(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_DEFENSE") == 0) {
        dispatch.FireUnitDefense(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_FACTION") == 0) {
        dispatch.FireUnitFaction(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_DISPLAYPOWER") == 0) {
        dispatch.FireUnitDisplayPower(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_INVENTORY_CHANGED") == 0) {
        dispatch.FireUnitInventoryChanged(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_PET") == 0) {
        dispatch.FireUnitPet(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_AURA") == 0) {
        dispatch.FireUnitAura(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_DYNAMIC_FLAGS") == 0) {
        dispatch.FireUnitFlags(guid);
      }

      else if (std::strcmp(evt.event_name, "UNIT_COMBO_POINTS") == 0) {
        dispatch.FireUnitComboPoints(guid);
      }

      else if (std::strcmp(evt.event_name, "PLAYER_GUILD_UPDATE") == 0) {
        dispatch.FirePerUnitEvent(ui::game::events::PLAYER_GUILD_UPDATE, guid);
        dispatch.FireEvent(ui::game::events::TABARD_CANSAVE_CHANGED);
      }
    }

    else {
      if (std::strcmp(evt.event_name, "PLAYER_XP_UPDATE") == 0) {
        dispatch.FirePlayerXP();
      } else if (std::strcmp(evt.event_name, "PLAYER_MONEY") == 0) {
        dispatch.FirePlayerMoney();
      } else if (std::strcmp(evt.event_name, "PLAYER_FLAGS_CHANGED") == 0) {
        dispatch.FirePlayerFlags();
      } else if (std::strcmp(evt.event_name, "PLAYER_TALENT_UPDATE") == 0) {

      } else if (std::strcmp(evt.event_name, "UNIT_QUEST_LOG_CHANGED") == 0) {

        RequestVisibleQuestgiverStatusRefresh();
        RefreshQuestRuntimeFromPlayer(false);
        dispatch.FireQuestLogUpdate();
      } else if (std::strcmp(evt.event_name, "SPELLS_CHANGED") == 0) {
        dispatch.FireSpellsChanged();
      }
    }
  }
}

void WorldSession::BeginLevelGrantProposal(const std::uint64_t guid) {
  if (guid == 0 || refer_a_friend_runtime_.pending_level_grant_guid() != 0) {
    return;
  }

  refer_a_friend_runtime_.BeginLevelGrant(guid);

  if (const auto *cached_name = query_cache_.GetPlayerName(guid)) {
    ui::game::ScriptEventDispatch::Get().FireEventArgs(ui::game::events::LEVEL_GRANT_PROPOSED,
                                                       {cached_name->name});
    refer_a_friend_runtime_.MarkLevelGrantEventDispatched();
    return;
  }

  (void)query_cache_.RequestNameQuery(guid);
}

void WorldSession::BeginTradeSkillLinkOpen(const std::uint32_t spell_id,
                                           const std::uint32_t current_rank,
                                           const std::uint32_t max_rank,
                                           const std::uint64_t player_guid,
                                           std::string encoded_recipe_bits) {
  if (spell_id == 0 || player_guid == 0) {
    return;
  }

  if (const auto linked_player = ResolveTradeSkillLinkedPlayerInfo(*this, player_guid);
      linked_player.has_value()) {
    const auto skill_line_id = ResolveTradeSkillLinkSkillLineId(*this, *linked_player, spell_id);
    if (!skill_line_id.has_value()) {
      return;
    }

    ui::game::detail::OpenTradeSkillView(
        this, GetDbcLoader(), *skill_line_id, ResolveTradeSkillLineName(*this, *skill_line_id),
        current_rank, max_rank, linked_player->name, std::move(encoded_recipe_bits), spell_id);
    return;
  }

  PendingTradeSkillLinkOpen pending_request;
  pending_request.spell_id = spell_id;
  pending_request.current_rank = current_rank;
  pending_request.max_rank = max_rank;
  pending_request.player_guid = player_guid;
  pending_request.encoded_recipe_bits = std::move(encoded_recipe_bits);
  pending_trade_skill_link_opens_.push_back(std::move(pending_request));

  (void)query_cache_.RequestNameQuery(player_guid);
}

void WorldSession::ResolvePendingLevelGrantNameQuery(const std::uint64_t guid,
                                                     const bool name_unknown) {
  if (guid == 0 || refer_a_friend_runtime_.pending_level_grant_guid() != guid ||
      refer_a_friend_runtime_.level_grant_event_dispatched() ||
      name_unknown) {
    return;
  }

  const auto *cached_name = query_cache_.GetPlayerName(guid);
  if (cached_name == nullptr) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(ui::game::events::LEVEL_GRANT_PROPOSED,
                                                     {cached_name->name});
  refer_a_friend_runtime_.MarkLevelGrantEventDispatched();
}

void WorldSession::ResolvePendingTradeSkillLinkNameQuery(const std::uint64_t guid,
                                                         const bool name_unknown) {
  if (pending_trade_skill_link_opens_.empty()) {
    return;
  }

  const auto first_matching = std::find_if(
      pending_trade_skill_link_opens_.begin(), pending_trade_skill_link_opens_.end(),
      [guid](const PendingTradeSkillLinkOpen &pending) { return pending.player_guid == guid; });
  if (first_matching == pending_trade_skill_link_opens_.end()) {
    return;
  }

  auto remove_matching_requests = [this, guid]() {
    pending_trade_skill_link_opens_.erase(
        std::remove_if(pending_trade_skill_link_opens_.begin(),
                       pending_trade_skill_link_opens_.end(),
                       [guid](const PendingTradeSkillLinkOpen &pending) {
                         return pending.player_guid == guid;
                       }),
        pending_trade_skill_link_opens_.end());
  };

  if (name_unknown) {
    remove_matching_requests();
    return;
  }

  const auto linked_player = ResolveTradeSkillLinkedPlayerInfo(*this, guid);
  if (!linked_player.has_value()) {
    return;
  }

  std::vector<PendingTradeSkillLinkOpen> resolved_requests;
  resolved_requests.reserve(pending_trade_skill_link_opens_.size());
  for (auto it = pending_trade_skill_link_opens_.begin();
       it != pending_trade_skill_link_opens_.end();) {
    if (it->player_guid != guid) {
      ++it;
      continue;
    }

    resolved_requests.push_back(std::move(*it));
    it = pending_trade_skill_link_opens_.erase(it);
  }

  for (auto &pending : resolved_requests) {
    const auto skill_line_id =
        ResolveTradeSkillLinkSkillLineId(*this, *linked_player, pending.spell_id);
    if (!skill_line_id.has_value()) {
      continue;
    }

    ui::game::detail::OpenTradeSkillView(
        this, GetDbcLoader(), *skill_line_id, ResolveTradeSkillLineName(*this, *skill_line_id),
        pending.current_rank, pending.max_rank, linked_player->name,
        std::move(pending.encoded_recipe_bits), pending.spell_id);
  }
}

void WorldSession::HandleNameQueryResponse(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  ObjectGuid guid;
  if (!reader.ReadPackedGuid(guid)) {
    return;
  }
  std::uint8_t response_type = 0;
  if (!reader.ReadU8(response_type)) {
    return;
  }
  const bool name_query_requeued = response_type == 2;
  const bool name_lookup_failed = response_type != 0 && response_type != 2 && response_type != 3;
  const bool name_cache_updated = response_type == 0 || response_type == 3;
  const bool has_full_name_record = response_type == 0;
  const bool pending_raid_roster_name_query =
      pending_raid_roster_name_queries_.contains(guid.GetRawValue());

  if (!query_cache_.HandleNameQueryResponse(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (name_query_requeued) {
    return;
  }

  if (name_query_response_callback_) {
    name_query_response_callback_(guid.GetRawValue(), name_lookup_failed);
  }

  ResolvePendingReadyCheckNameQuery(guid.GetRawValue(), name_lookup_failed);
  ResolvePendingLevelGrantNameQuery(guid.GetRawValue(), name_lookup_failed);
  ResolvePendingTradeSkillLinkNameQuery(guid.GetRawValue(), name_lookup_failed);
  auction_packets_.ResolvePendingNameQuery(
      guid.GetRawValue(), name_cache_updated);
  if (pending_raid_roster_name_query) {
    SyncObservedGroupStateToGroupSystem();
  }
  if (pending_raid_roster_name_queries_.erase(guid.GetRawValue()) != 0 &&
      pending_raid_roster_name_queries_.empty() &&
      !pending_raid_roster_local_player_resolution_) {
    ui::game::ScriptEventDispatch::Get().FireRaidRosterUpdate();
  }
  ResolvePendingGroupLootMasterAnnouncements(guid.GetRawValue(), name_lookup_failed);

  if (guild_bank_.ResolveGuildBankLogNameQuery(guid.GetRawValue())) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::GUILDBANKLOG_UPDATE);
  }
  if (guild_.ResolveGuildEventLogNameQuery(guid.GetRawValue())) {
    ui::game::ScriptEventDispatch::Get().FireGuildEventLogUpdate();
  }

  pending_chat_name_queries_.erase(guid.GetRawValue());
  RetryPendingReferAFriendFailures(guid.GetRawValue(), name_lookup_failed);

  const auto battlefield_position_name_updates =
      BattlefieldInfo::Get().ConsumePendingBattlefieldPositionNameCallbacks(guid.GetRawValue());
  const auto *cached_name = query_cache_.GetPlayerName(guid.GetRawValue());
  if (has_full_name_record && cached_name != nullptr) {
    objects().CachePlayerName(guid, cached_name->name, cached_name->race, cached_name->sex,
                             cached_name->class_id);
    ui::game::AutoComplete::Get().UpdateRecentPlayerGuidName(
        guid.GetRawValue(), FormatAutoCompletePlayerName(*cached_name), false);
  }
  ResolvePendingSocialNameQueries(guid.GetRawValue(), name_lookup_failed);
  for (std::uint32_t i = 0; i < battlefield_position_name_updates; ++i) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::WORLD_MAP_NAME_UPDATE);
  }
  ResolvePendingMasterLootCandidateName(guid.GetRawValue(), name_lookup_failed);
  (void)BattlefieldInfo::Get().OnScoreNameResolved(guid.GetRawValue(), query_cache_, objects());
  (void)BattlefieldInfo::Get().OnBGPlayerStatusNameResolved(guid.GetRawValue(), query_cache_);
  ApplyPetitionUiTransition(
      petition_.OnPlayerNameResolved(guid.GetRawValue(), query_cache_, objects()));
  RetryPendingChatMessagesForGuid(guid, name_lookup_failed);
  RetryPendingTextEmotesForGuid(guid, name_lookup_failed);
  RetryPendingChannelListsForGuid(guid, name_lookup_failed);
  RetryPendingWatchedChannelRosterForGuid(guid, name_lookup_failed);
  if (lfg_.ResolvePendingSearchPlayerNameQuery(guid.GetRawValue())) {
    if (auto *ui = world_ui_runtime()) {
      ui->frame_events().dispatcher().FireEvent(ui::game::events::LFG_UPDATE);
    }
  }

  auto &calendar = CalendarSystem::Get();
  const bool refresh_calendar_event_list =
      calendar.ResolvePendingEventListNameQuery(guid.GetRawValue()) &&
      query_cache_.GetPlayerName(guid.GetRawValue()) != nullptr;

  const auto resolution = calendar.ResolvePendingInviteListNameQuery(guid.GetRawValue());
  if (resolution) {
    const std::string resolved_name = ResolveCalendarInviteeName(*this, guid.GetRawValue());
    if (!resolved_name.empty()) {
      calendar.SetEventInviteeName(resolution->event_id, guid.GetRawValue(), resolved_name);
    }

    if (resolution->completed) {
      FinalizeCalendarInviteLookupCompletion(resolution->event_id, resolution->completion_action);
    }
  }
  if (refresh_calendar_event_list) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  }
  TryDisplayPendingChannelInvite(guid.GetRawValue());
}

void WorldSession::RequestMasterLootCandidateName(const std::uint64_t guid) {
  if (guid == 0) {
    return;
  }

  if (query_cache_.GetPlayerName(guid) != nullptr ||
      !objects().GetPlayerName(ObjectGuid(guid)).empty()) {
    return;
  }

  pending_master_loot_candidate_name_queries_.insert(guid);
  (void)query_cache_.RequestNameQuery(guid);
}

void WorldSession::ResolvePendingMasterLootCandidateName(const std::uint64_t guid,
                                                         const bool name_unknown) {
  if (guid == 0 || pending_master_loot_candidate_name_queries_.erase(guid) == 0 || name_unknown) {
    return;
  }

  if (query_cache_.GetPlayerName(guid) == nullptr) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireUpdateMasterLootList();
}

void WorldSession::QueueGroupLootMasterAnnouncement(const std::uint64_t guid) {
  if (guid == 0) {
    return;
  }

  if (const auto *cached_name = query_cache_.GetPlayerName(guid)) {
    ui::game::DisplaySystemMessage(kMasterLooterSystemMessageId, cached_name->name.c_str());
    return;
  }

  pending_group_loot_master_announcements_.push_back(guid);
  (void)query_cache_.RequestNameQuery(guid);
}

void WorldSession::ResolvePendingGroupLootMasterAnnouncements(const std::uint64_t guid,
                                                              const bool name_unknown) {
  if (guid == 0 || pending_group_loot_master_announcements_.empty()) {
    return;
  }

  std::string resolved_name;
  if (!name_unknown) {
    if (const auto *cached_name = query_cache_.GetPlayerName(guid)) {
      resolved_name = cached_name->name;
    }
  }

  auto it = pending_group_loot_master_announcements_.begin();
  while (it != pending_group_loot_master_announcements_.end()) {
    if (*it != guid) {
      ++it;
      continue;
    }

    if (!resolved_name.empty()) {
      ui::game::DisplaySystemMessage(kMasterLooterSystemMessageId, resolved_name.c_str());
    }
    it = pending_group_loot_master_announcements_.erase(it);
  }
}

void WorldSession::HandleCreatureQueryResponse(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::uint32_t entry = 0;
  if (!reader.ReadU32(entry)) {
    return;
  }

  if (!query_cache_.HandleCreatureQueryResponse(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  RetryPendingChatMessagesForCreatureEntry(entry & 0x7FFFFFFFu, (entry & 0x80000000u) != 0);
  RetryPendingTextEmotesForCreatureEntry(entry & 0x7FFFFFFFu, (entry & 0x80000000u) != 0);
}

void WorldSession::HandleGameObjectQueryResponse(const net::wotlk::WorldPacket &pkt) {
  query_cache_.HandleGameObjectQueryResponse(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleItemQueryResponseSideEffects(
    const std::uint32_t response_entry) {
  const auto entry = response_entry & 0x7FFFFFFFu;
  const bool found = (response_entry & 0x80000000u) == 0;

  if (guild_bank_.ResolveGuildBankLogItemQuery(entry)) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::GUILDBANKLOG_UPDATE);
  }
  if (guild_bank_.ResolveGuildBankListItemQuery(entry)) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::GUILDBANKBAGSLOTS_CHANGED);
  }
  if (!found) {
    loot_.state().NotifyItemTemplateFailed(entry);
    ResolveInventoryTemplateContainers();
    return;
  }

  for (const auto &start_event :
       loot_.state().NotifyItemTemplateReady(entry)) {
    ui::game::ScriptEventDispatch::Get().FireStartLootRoll(
        static_cast<int>(start_event.roll_id),
        static_cast<int>(start_event.countdown_ms));
  }
  (void)ui::game::detail::RefreshActionSlotsForChangedItemEntry(*this, entry);
  SpellBookFrame::HandleTrackedMultiCastTotemItemEntry(*this, entry);
  QueueEquipmentPresentation();
}

void WorldSession::HandleItemQuerySingleResponse(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::uint32_t entry = 0;
  const bool have_entry = reader.ReadU32(entry);
  if (!query_cache_.HandleItemQuerySingleResponse(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  if (have_entry) {
    HandleItemQueryResponseSideEffects(entry);
  }
}

void WorldSession::HandlePageTextQueryResponse(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandlePageTextQueryResponse(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  LoadCurrentReadableTextPage(*this, true);
}

void WorldSession::HandleItemNameQueryResponse(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload);
  std::uint32_t raw_entry = 0;
  if (!reader.ReadU32(raw_entry)) {
    return;
  }
  const auto entry = raw_entry & 0x7fffffffu;
  auto& cache = db_cache_runtime_.cache();
  auto& persistence = db_cache_runtime_.persistence();
  if ((raw_entry & 0x80000000u) != 0) {
    if (reader.Remaining() != 0 || entry == 0) {
      return;
    }
    item_definitions_.InvalidateItemName(entry);
    if (cache.InvalidateEntry(data::WDBCacheType::ItemName, entry)) {
      persistence.SetDirty(data::WDBCacheType::ItemName);
    }
    return;
  }
  std::string name;
  std::uint32_t inventory_type = 0;
  if (entry == 0 || !reader.ReadCString(name, 1024) ||
      !reader.ReadU32(inventory_type) || reader.Remaining() != 0) {
    return;
  }
  item_definitions_.CacheItemName(entry, name, inventory_type);
  cache.UpdateEntry(data::WDBCacheType::ItemName, entry,
                    data::SerializeItemNameWdbPayload(name, inventory_type),
                    data::wdb_format::kVersion_ItemName);
  persistence.SetDirty(data::WDBCacheType::ItemName);
}

void WorldSession::HandleItemQueryMultipleResponse(const net::wotlk::WorldPacket &pkt) {
  (void)query_cache_.HandleItemQueryMultipleResponse(
      pkt.payload.data(), pkt.payload.size(),
      [this](const std::uint32_t response_entry) {
        HandleItemQueryResponseSideEffects(response_entry);
      });
}

}
