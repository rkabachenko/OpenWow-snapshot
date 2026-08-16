
#pragma once

#include <cstdint>
#include <vector>

namespace openwow::game {

class CGUnit_C;
class CGPlayer_C;
struct DescriptorFieldChangeView;
class ObjectManager;
class WorldSession;

namespace DescriptorEventId {
inline constexpr std::uint32_t kSheatheState = 120;
inline constexpr std::uint32_t kQuestLogUpdate = 624;
inline constexpr std::uint32_t kEquipSlotChanged = 634;
inline constexpr std::uint32_t kItemLost = 338;
inline constexpr std::uint32_t kGlyphAdded = 611;
inline constexpr std::uint32_t kGlyphRemoved = 612;
inline constexpr std::uint32_t kGlyphUpdated = 613;
inline constexpr std::uint32_t kGlyphEnabled = 614;
inline constexpr std::uint32_t kGlyphDisabled = 615;
inline constexpr std::uint32_t kPvPDesired = 397;

inline constexpr std::uint32_t kPlaytimeChanged = 467;
inline constexpr std::uint32_t kBarberShopOpen = 523;
inline constexpr std::uint32_t kBarberShopClose = 524;
inline constexpr std::uint32_t kRuneType = 642;
inline constexpr std::uint32_t kRuneTypeAlt = 643;
inline constexpr std::uint32_t kEnableLowLevelRaid = 667;
inline constexpr std::uint32_t kDisableLowLevelRaid = 668;
inline constexpr std::uint32_t kGuildChanged = 381;
inline constexpr std::uint32_t kTabardSaved = 459;
inline constexpr std::uint32_t kQuestComplete = 146;

inline constexpr std::uint32_t kPlayerFarsightFocusChanged = 161;
inline constexpr std::uint32_t kMailNotify = 303;
}

struct DescriptorCallbackInfo {
  std::uint8_t type;
  std::uint16_t offset;
  std::uint16_t size;
  const char *name;
};

const DescriptorCallbackInfo *GetDescriptorCallbackTable(std::uint32_t &out_count);

struct SkillValueChangeInfo {
  std::uint16_t slot_index;

  std::uint16_t old_raw_value;
  std::uint32_t new_adjusted_value;
  std::uint16_t skill_line_id;
  std::uint8_t race;
  std::uint8_t player_class;
  bool is_active_player;
};

void OnSkillValueDescriptorChanged(WorldSession& session,
                                   const SkillValueChangeInfo& info);

void OnSkillModifierDescriptorChanged(WorldSession& session,
                                      bool is_active_player);

void OnSkillRangeDescriptorChanged(WorldSession& session);

void OnCombatRatingUpdate(WorldSession& session);

void OnRestStateDescriptorChanged(std::uint8_t rest_state);

void OnInebriationDescriptorChanged(WorldSession& session,
                                    float normalized_inebriation,
                                    bool is_active_player,
                                    const CGUnit_C *player_unit,
                                    const CGUnit_C *target_unit);

struct ItemSwapState {
  std::uint32_t guild_appearance_count = 0;
  std::uint32_t initial_spells_count = 0;
  std::uint32_t pending_swap_count = 0;
  bool tabard_save_pending = false;

  void Reset();
};

struct GuildAppearanceEntry {
  std::uint32_t guild_id = 0;
  std::uint32_t guild_timestamp = 0;
};

struct GuildAppearanceChangeResult {

  bool needs_requery = false;
  std::uint32_t guild_id = 0;
};

GuildAppearanceChangeResult OnGuildAppearanceDescriptorChanged(
    std::uint32_t guild_id,
    std::uint32_t guild_timestamp,
    std::vector<GuildAppearanceEntry>& tracking_entries);

struct PlayerFlagsChangeResult {

  bool refresh_spells;
  bool update_pvp_state;
  bool dirty_portrait;
  bool fire_per_unit_399;

  bool latch_pvp_display;
  std::uint32_t pvp_display_value;

  bool update_model;
  bool fire_ffa_pvp_event;
  bool trigger_ffa_tutorials;
  bool fire_pvp_toggle;
  bool pvp_toggle_on;
  bool fire_talent_group;
  bool fire_barber_event;
  bool barber_open;
  bool refresh_rune_power;
  bool fire_rune_type_event;
  bool rune_type_set;
  bool fire_raid_toggle;
  bool raid_toggle_on;

  bool dispatch_visual_toggle_400;
  bool visual_400_on;
  bool dispatch_visual_toggle_800;
  bool visual_800_on;
};

bool OnPlayerFlagsDescriptorChanged(std::uint32_t new_flags,
                                    std::uint32_t old_flags,
                                    bool is_active_player,
                                    PlayerFlagsChangeResult& result);

struct PlayerFlagChangeInfo {
  std::uint32_t changed_bits;
  std::uint32_t new_flags;
  bool is_active_player;
};

struct FlagChangeEvent {
  std::uint32_t event_id;
  const char *format;
  int arg1;
};

std::uint32_t ProcessPlayerFlagChanges(const PlayerFlagChangeInfo &info, FlagChangeEvent *events,
                                       std::uint32_t max_events);

struct InventorySlotChangeInfo {
  std::uint32_t slot_index;
  std::uint64_t old_guid;
  std::uint64_t new_guid;
};

bool ProcessInventorySlotChange(const InventorySlotChangeInfo &info, FlagChangeEvent *events,
                                std::uint32_t max_events, std::uint32_t &out_count);

struct QuestLogChangeInfo {
  std::uint32_t slot_index;
  std::uint32_t old_quest_id;
  std::uint32_t new_quest_id;
  std::uint32_t old_state;
  std::uint32_t new_state;
  bool is_active_player = false;

  bool old_quest_template_available = false;
  std::uint32_t old_quest_type = 0;
  int old_quest_visible_index = -1;

  bool current_quest_template_available = false;
  std::uint32_t current_quest_flags = 0;
  bool current_quest_is_complete = false;
  int current_quest_visible_index = -1;

  bool tutorial_0x14_completed = false;
  std::uint32_t quest_completion_counter = 0;

  std::uint32_t selected_quest_id = 0;

  std::uint64_t object_guid = 0;
};

inline constexpr std::uint32_t kQuestLogStateBitComplete = 0x02;

inline constexpr std::uint32_t kQuestFlagAutoRewarded = 0x10000;

struct QuestLogChangeResult {

  bool send_questgiver_status_multiple_query = false;

  bool notify_quest_change = false;
  std::uint32_t notification_quest_id = 0;
  bool trigger_tutorial_0x28 = false;
  bool trigger_tutorial_0x26 = false;
  bool increment_completion_counter = false;
  bool trigger_tutorial_0x14 = false;
  std::uint32_t updated_completion_counter = 0;
  bool fire_quest_log_update = false;
  int quest_log_update_visible_index = -1;

  bool request_async_template_lookup = false;
  std::uint32_t async_lookup_quest_id = 0;

  bool send_auto_complete = false;
  std::uint64_t auto_complete_guid = 0;
  std::uint32_t auto_complete_quest_id = 0;

  bool reset_quest_tracking = false;
  std::uint32_t tracking_quest_id = 0;
};

bool ProcessQuestLogChange(const QuestLogChangeInfo &info,
                           QuestLogChangeResult &out);

struct EquipmentVisualChangeInfo {
  std::uint32_t slot_index;
  std::int32_t old_entry;
  std::int32_t new_entry;
  bool is_active_player;
  std::uint8_t sheathe_state;

  bool has_character_model;

  bool has_unit_model;

  std::uint8_t body_slot_inv_type;
};

struct EquipmentVisualChangeResult {
  bool early_return;

  bool rebuild_trade_skill;
  bool refresh_spell_ui;

  bool refresh_action_bar;
  std::uint32_t abs_new_entry;
  std::uint32_t abs_old_entry;
  bool fire_unit_attack_main;
  bool fire_unit_attack_off;
  bool fire_ranged_slot_event;
  bool rebuild_spellbook;

  bool clear_hand_attachment_0;
  bool clear_hand_attachment_1;

  bool update_character_model_component;
  bool dispatch_barber_shop;

  bool ranged_update_sheathe;
  std::uint8_t ranged_sheathe_arg;
  bool ranged_clear_char_model;
  bool ranged_cleanup_unit_model;
  bool change_sheathe_to_ranged;
  bool refresh_stand_animation;

  bool walk_child_attachments;
  bool queue_portrait_model_event;
};

bool ProcessEquipmentVisualChange(const EquipmentVisualChangeInfo &info,
                                  EquipmentVisualChangeResult &out);

struct EquipmentSlotDescriptorCallbackParams {
  std::uint16_t descriptor_offset;
  std::int32_t *new_value_ptr;
  bool player_resolved;
};

bool OnEquipmentSlotDescriptorChanged(
    const EquipmentSlotDescriptorCallbackParams &params,
    const EquipmentVisualChangeInfo &info,
    EquipmentVisualChangeResult &out);

struct MinimapTrackInfo {
  bool viewer_is_dead_or_ghost{false};

  std::uint8_t unit_vis_flags{0};
  std::uint32_t unit_dynamic_flags{0};
  std::uint32_t creature_type_id{0};
  std::uint32_t player_tracking_mask{0};
  bool viewer_has_creep_view_flag{false};
};

inline constexpr std::uint32_t kMinimapCreepViewPlayerFlag = 0x02u;

bool ShouldShowOnMinimap(const MinimapTrackInfo &info);

class WorldSession;

void HandlePlayerFieldBytes2Changed(WorldSession &session);

void RefreshPlayerShapeshiftUiState(WorldSession &session);

void OnPlayerShapeshiftFormChanged(WorldSession &session, std::uint8_t new_form_id);

void OnActiveControlGuidChanged(WorldSession &session);

void RefreshPossessSpellIdFromPlayerAuras(WorldSession& session,
                                          const CGPlayer_C& player);

void OnComboTargetDescriptorChanged(
    WorldSession& session, const DescriptorFieldChangeView& change);

void RefreshAllVisibleGameObjectLootArt(ObjectManager& objects);

void PushPlayerUnitToken(const char* event_name);

void OnCoinageDescriptorChanged(class WorldSession& session,
                                std::uint32_t current_money);

}
