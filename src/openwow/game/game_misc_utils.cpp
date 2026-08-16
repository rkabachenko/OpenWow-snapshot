
#include "openwow/game/game_misc_utils.h"
#include "openwow/net/serialization/cdatastore_vtable.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_error.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/achievements/application/tracked_achievement_state.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/minimap_terrain.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/targeting/application/target_selection_service.h"
#include "openwow/game/tracking_system.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace openwow::game {

namespace {

constexpr int kMirrorTimerSlotCount = 3;

constexpr std::uint32_t kMaxStablePetSlots = 5;
StablePetSlot g_stable_slots[kMaxStablePetSlots] = {};
std::uint32_t g_stable_slot_count = 0;

std::array<MirrorTimerScriptState, 3> g_mirror_timers = {};
MirrorTimerTickCountProvider g_mirror_timer_tick_count_provider = nullptr;

int   g_death_map_id = -1;
float g_death_x = 0.0f, g_death_y = 0.0f, g_death_z = 0.0f;

int g_hyperlink_compare_slot = 0;
int g_hyperlink_counter = 0;

std::mutex g_misc_mutex;

std::uint32_t GetMirrorTimerTickCount() {
  return g_mirror_timer_tick_count_provider != nullptr
             ? g_mirror_timer_tick_count_provider()
             : openwow::core::GameClock::GetTickCount32();
}

MirrorTimerScriptState* FindMirrorTimerStateLocked(const int timer_index) {
  if (timer_index < 0 ||
      timer_index >= static_cast<int>(g_mirror_timers.size())) {
    return nullptr;
  }
  return &g_mirror_timers[static_cast<std::size_t>(timer_index)];
}

bool ShouldRefreshSpiritHealerMarker(const ObjectManager& objects,
                                     const DeathReleasePosition& position) {
  if (objects.GetActivePlayer() == nullptr) {
    return false;
  }

  return position.map_id == -1 ||
         position.map_id == static_cast<int>(objects.GetMapId());
}

}

std::uint32_t ReadUInt32FromPacket(void* data_store) {
  if (!data_store) {
    return 0;
  }

  auto& store = *static_cast<openwow::net::CDataStore*>(data_store);
  std::uint32_t value = 0;
  openwow::net::CDataStore_GetUInt32(store, &value);
  return value;
}

const StablePetSlot* StablePet_GetSlotEntry(std::uint32_t slot_index) {
  std::lock_guard<std::mutex> lock(g_misc_mutex);
  if (slot_index >= g_stable_slot_count)
    return nullptr;
  return &g_stable_slots[slot_index];
}

double GetWidescreenAspectRatio() {
  int width = 4;
  int height = 3;
  char separator = 0;

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (cvars.GetCVarInt("widescreen") != 0) {
    const std::string resolution = cvars.GetCVar("gxResolution");
    std::sscanf(resolution.c_str(), "%d%c%d", &width, &separator, &height);
  }

  return static_cast<double>(width) / static_cast<double>(height);
}

bool IsAutoLootEnabled(const BindingProfiles* bindings) {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const bool auto_loot_default = cvars.GetCVarInt("autoLootDefault") != 0;

  const bool toggle_active =
      bindings != nullptr &&
      actions::bindings::adapters::retail::IsModifiedClickActiveNow(
          *bindings, "AUTOLOOTTOGGLE");
  return auto_loot_default != toggle_active;
}

namespace {

template <typename Predicate>
std::uint32_t CountCarriedItemTotal(
    const PlayerInventoryReplica& inventory, Predicate&& predicate) {
  std::uint32_t total = 0;

  for (std::uint8_t slot = InventorySlots::kEquipStart;
       slot < InventorySlots::kEquipEnd;
       ++slot) {
    if (const auto* item = inventory.GetEquipSlot(slot);
        item != nullptr && predicate(*item)) {
      total += item->count;
    }
  }

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (const auto* item = inventory.GetBackpackSlot(slot);
        item != nullptr && predicate(*item)) {
      total += item->count;
    }
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      if (const auto* item = inventory.GetBagSlot(bag, slot);
          item != nullptr && predicate(*item)) {
        total += item->count;
      }
    }
  }

  return total;
}

}

bool LootItemRequiresBindConfirm(const ItemTemplate& item_template,
                                 const LootSlotType slot_type) {
  return item_template.bonding == 1u &&
         static_cast<std::uint32_t>(item_template.quality) >= 2u &&
         (item_template.flags & 0x800u) == 0u &&
         slot_type != LootSlotType::kOwner;
}

bool CanPromptLootItemBindConfirm(WorldSession& session,
                                  const ItemTemplate& item_template,
                                  const std::uint32_t loot_count,
                                  const bool show_errors) {
  if (item_template.max_count != 0u) {
    const auto carried_count = CountCarriedItemTotal(
        session.inventory_replica(),
        [&item_template](const ItemInstance& item) {
          return item.entry == item_template.entry;
        });
    if (carried_count + loot_count > item_template.max_count) {
      if (show_errors) {
        ui::game::DisplaySystemMessage(20);
      }
      return false;
    }
  }

  if (item_template.item_limit_category != 0u) {
    const auto carried_count = CountCarriedItemTotal(
        session.inventory_replica(),
        [&session, &item_template](const ItemInstance& item) {
          const auto* carried_template =
              session.item_definitions().GetItem(item.entry);
          return carried_template != nullptr &&
                 carried_template->item_limit_category ==
                     item_template.item_limit_category;
        });

    if (const auto* dbc = session.GetDbcLoader(); dbc != nullptr) {
      const auto* limit_entry =
          dbc->item_limit_category().LookupEntry(item_template.item_limit_category);
      if (limit_entry != nullptr &&
          carried_count + loot_count > limit_entry->quantity) {
        if (show_errors) {
          const std::string category_name(limit_entry->name);
          ui::game::DisplaySystemMessage(
              626, limit_entry->quantity, category_name.c_str());
        }
        return false;
      }
    }
  }

  return true;
}

bool ApplyPendingAutoLoot(WorldSession& session,
                          const bool release_when_emptied) {

  const ObjectGuid loot_source_guid =
      session.loot().is_looting() ? session.loot().loot_window().source_guid
                                  : ObjectGuid();
  const auto plan = session.loot().TakePendingAutoLootPlan();
  if (plan.loot_money) {
    session.interaction().SendLootMoney();
  }
  for (const auto slot : plan.loot_slots) {
    session.interaction().SendAutoStoreLootItem(slot);
  }
  for (const auto& confirmation : plan.bind_confirmations) {
    const auto* item =
        session.item_definitions().GetItem(confirmation.item_id);
    if (item != nullptr &&
        !CanPromptLootItemBindConfirm(
            session, *item, confirmation.count, true)) {
      continue;
    }
    session.loot().state().SetPendingConfirmSlot(confirmation.ui_slot);
    ui::game::ScriptEventDispatch::Get().FireLootBindConfirm(
        confirmation.ui_slot);
    break;
  }

  if (release_when_emptied && !plan.remains_open &&
      !loot_source_guid.IsEmpty()) {
    CleanupClosingLootSourceState(
        session, LootWindow{.source_guid = loot_source_guid});
    ClearActivePlayerLootInteractionState(session);

    (void)session.loot().TakePendingReleaseGuid();
    session.interaction().SendLootRelease(loot_source_guid.GetRawValue());
    session.loot().state().ClearPendingConfirmSlot();
    ui::game::ScriptEventDispatch::Get().FireLootClosed();
  }
  return plan.remains_open;
}

void PrepareAutoLootInteraction(WorldSession& session, bool enabled) {
  session.loot().SetPendingAutoLoot(enabled);
  if (!enabled || !session.loot().is_looting()) {
    return;
  }

  const bool should_keep_window = ApplyPendingAutoLoot(session);
  if (should_keep_window) {
    ui::game::ScriptEventDispatch::Get().FireLootOpened(enabled);
  }
}

void ClearActivePlayerLootInteractionState(WorldSession& session) {
  if (auto* active_player = session.objects().GetActivePlayer();
      active_player != nullptr) {
    active_player->Animation().ClearStandSelectionInteractionTargetAndRefresh(session);
  }
}

void CleanupClosingLootSourceState(WorldSession& session, const LootWindow& loot_window) {
  if (loot_window.source_guid.IsEmpty()) {
    return;
  }

  if (auto* const game_object = session.objects().GetMutableGameObject(loot_window.source_guid);
      game_object != nullptr) {
    game_object->ApplyTransientGoStateByte(
        static_cast<std::uint8_t>(GOState::Ready));
    return;
  }

  if (session.objects().GetUnit(loot_window.source_guid) != nullptr) {
    ui::game::GameUI_OnMouseoverUnitLeave(loot_window.source_guid.GetRawValue());
  }
}

void CloseActiveLootWindow(WorldSession& session,
                           const CloseLootWindowOptions& options) {
  auto& loot = session.loot();
  if (!loot.is_looting()) {
    return;
  }

  const LootWindow loot_window = loot.loot_window();
  if (loot_window.source_guid.IsEmpty()) {
    return;
  }

  const bool has_loot_items = loot.HasLootItems();
  const bool should_auto_loot =
      !options.skip_item_check &&
      has_loot_items &&
      (loot_window.loot_type == LootType::kDisenchanting ||
       loot_window.loot_type == LootType::kProspecting ||
       loot_window.loot_type == LootType::kMilling);

  if (should_auto_loot) {
    loot.SetPendingAutoLoot(true);

    (void)ApplyPendingAutoLoot(session, false);
  }

  if (options.show_interrupted) {
    ui::game::DisplaySystemMessage(144);
  }

  if (!options.skip_item_check) {
    CleanupClosingLootSourceState(session, loot_window);
  }

  ClearActivePlayerLootInteractionState(session);

  const auto loot_source_guid = loot_window.source_guid.GetRawValue();
  if (options.send_release) {

    (void)session.loot().TakePendingReleaseGuid();
    session.interaction().SendLootRelease(loot_source_guid);
  }

  loot.CloseLootWindow();
  session.loot().state().ClearPendingConfirmSlot();
  ui::game::ScriptEventDispatch::Get().FireLootClosed();

  if (options.clear_dead_target) {
    if (const auto* loot_source_unit = session.objects().GetUnit(loot_window.source_guid);
        loot_source_unit != nullptr &&
        static_cast<std::int32_t>(loot_source_unit->State().GetHealth()) <= 0) {
      if (auto *targeting_system = session.targeting_system();
          targeting_system != nullptr) {
        targeting::TargetSelectionService target_selection(*targeting_system);
        (void)target_selection.ClearTarget(
            session, ObjectGuid(loot_source_guid),
            targeting::TargetClearMode::kNotifyServer);
      }
    }
  }

}

const char* GetMirrorTimerName(int timer_index) {
  switch (timer_index) {
    case 0: return "EXHAUSTION";
    case 1: return "BREATH";
    case 2: return "FEIGNDEATH";
    default: return "UNKNOWN";
  }
}

int GetMirrorTimerIndex(const char* timer_name) {
  if (!timer_name) return 3;
#ifdef _WIN32
  if (_stricmp(timer_name, "EXHAUSTION") == 0) return 0;
  if (_stricmp(timer_name, "BREATH") == 0) return 1;
  if (_stricmp(timer_name, "FEIGNDEATH") == 0) return 2;
#else
  if (strcasecmp(timer_name, "EXHAUSTION") == 0) return 0;
  if (strcasecmp(timer_name, "BREATH") == 0) return 1;
  if (strcasecmp(timer_name, "FEIGNDEATH") == 0) return 2;
#endif
  return 3;
}

std::optional<MirrorTimerScriptState> GetMirrorTimerState(
    const int timer_index) {
  std::lock_guard<std::mutex> lock(g_misc_mutex);
  const auto* state = FindMirrorTimerStateLocked(timer_index);
  if (state == nullptr) {
    return std::nullopt;
  }
  return *state;
}

bool HasNegativeMirrorTimerScale(const int timer_index) {
  std::lock_guard<std::mutex> lock(g_misc_mutex);
  const auto* state = FindMirrorTimerStateLocked(timer_index);
  return state != nullptr && state->scale < 0;
}

std::optional<std::int32_t> GetMirrorTimerProgressValue(
    const int timer_index) {
  std::lock_guard<std::mutex> lock(g_misc_mutex);
  const auto* state = FindMirrorTimerStateLocked(timer_index);
  if (state == nullptr) {
    return std::nullopt;
  }

  const std::uint32_t delta_bits =
      GetMirrorTimerTickCount() - state->base_tick_count;
  const std::uint32_t scaled_bits =
      delta_bits * static_cast<std::uint32_t>(state->scale);
  const std::uint32_t progress_bits = state->current_value + scaled_bits;
  return static_cast<std::int32_t>(progress_bits);
}

std::string ResolveMirrorTimerLabel(const int timer_type,
                                    const std::uint32_t spell_id) {
  if (spell_id != 0) {
    const std::string spell_name =
        SpellQueryBridge::Get().GetSpellName(spell_id);
    if (!spell_name.empty()) {
      return spell_name;
    }
  }

  std::string label_key = GetMirrorTimerName(timer_type);
  label_key += "_LABEL";
  return Localization::Get().GetString(label_key, label_key);
}

void StartMirrorTimer(const int timer_index, const std::uint32_t current_value,
                      const std::uint32_t max_value,
                      const std::int32_t scale, const bool paused,
                      const std::uint32_t spell_id) {
  std::lock_guard<std::mutex> lock(g_misc_mutex);
  auto* state = FindMirrorTimerStateLocked(timer_index);
  if (state == nullptr) {
    return;
  }

  state->type = timer_index;
  state->current_value = current_value;
  state->max_value = max_value;
  state->scale = scale;
  state->paused = paused ? 1 : 0;
  state->spell_id = spell_id;
  state->base_tick_count = GetMirrorTimerTickCount();
}

void StopMirrorTimer(const int timer_index) {
  std::lock_guard<std::mutex> lock(g_misc_mutex);
  auto* state = FindMirrorTimerStateLocked(timer_index);
  if (state == nullptr) {
    return;
  }

  *state = MirrorTimerScriptState{};
}

void SetMirrorTimerTickCountProvider(
    MirrorTimerTickCountProvider provider) {
  std::lock_guard<std::mutex> lock(g_misc_mutex);
  g_mirror_timer_tick_count_provider = provider;
}

void UseDefaultMirrorTimerTickCountProvider() {
  SetMirrorTimerTickCountProvider(nullptr);
}

void ResetMirrorTimersForEnterWorldInit() {
  std::lock_guard<std::mutex> lock(g_misc_mutex);

  for (auto& state : g_mirror_timers) {
    state = MirrorTimerScriptState{};
  }
}

void ResetAllMirrorTimers() {
  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  for (int i = 0; i < kMirrorTimerSlotCount; ++i) {
    dispatch.FireEventArgs(
        ui::game::events::MIRROR_TIMER_STOP,
        {std::string(GetMirrorTimerName(i))});
    StopMirrorTimer(i);
  }
}

void SetDeathReleasePosition(const ObjectManager& objects,
                             const DeathReleasePosition& position) {
  {
    std::lock_guard<std::mutex> lock(g_misc_mutex);
    g_death_map_id = position.map_id;
    g_death_x = position.x;
    g_death_y = position.y;
    g_death_z = position.z;
  }

  if (ShouldRefreshSpiritHealerMarker(objects, position)) {
    Minimap_SetSpiritHealerMarker(position.x, position.y);
  }
}

void ResetDeathReleasePosition() {
  std::lock_guard<std::mutex> lock(g_misc_mutex);
  g_death_map_id = -1;
  g_death_x = 0.0f;
  g_death_y = 0.0f;
  g_death_z = 0.0f;
}

DeathReleasePosition GetDeathReleasePosition() {
  std::lock_guard<std::mutex> lock(g_misc_mutex);
  return {g_death_map_id, g_death_x, g_death_y, g_death_z};
}

void LoadArchivedRuntimeStateFromCVars(ObjectManager& objects) {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (cvars.Exists("minimapTrackedInfo")) {
    static_cast<void>(TrackingSystem::Get().ApplyTrackedInfoCVarValue(
        objects, cvars.GetCVar("minimapTrackedInfo")));
  }

  TrackedAchievementState::Get().LoadTrackedAchievementsFromCVar();
  TutorialSystem::Instance().LoadFlaggedTutorials();
}

void LoadingScreen_PreInitFont() {
  openwow::ui::SetCachedUiAspectScaleState(
      static_cast<float>(GetWidescreenAspectRatio()));
}

int GetHyperlinkCompareSlot() {
  std::lock_guard<std::mutex> lock(g_misc_mutex);
  if (g_hyperlink_compare_slot != 0)
    return g_hyperlink_compare_slot;
  g_hyperlink_compare_slot = ++g_hyperlink_counter;
  return g_hyperlink_compare_slot;
}

}
