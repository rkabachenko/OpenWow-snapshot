#include "openwow/game/world_session.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/items/item_protocol.h"
#include "openwow/game/session/handlers/commerce/trade_packets.h"
#include "openwow/game/session/handlers/commerce/mail_packets.h"
#include "openwow/game/session/handlers/commerce/merchant_packets.h"
#include "openwow/game/session/handlers/commerce/auction_packets.h"
#include "openwow/game/session/handlers/inventory/loot_roll_packets.h"
#include "openwow/game/session/handlers/inventory/item_packets.h"
#include "openwow/game/targeting.h"

#include "openwow/game/achievements/adapters/data/dbc_achievement_metadata_catalog.h"
#include "openwow/core/console.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/wdb_cache.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/game/account_data.h"
#include "openwow/game/account_data_runtime_sync.h"
#include "openwow/game/action_validation_utils.h"
#include "openwow/game/achievements/application/tracked_achievement_state.h"
#include "openwow/game/arena_system.h"
#include "openwow/game/arena_team.h"
#include "openwow/game/aura_tracker.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/commerce/trade/adapters/protocol/trade_packet_codec.h"
#include "openwow/game/chat_bubble.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/commentator_state.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/activities/dance/application/unit_dance_state.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/faction_system.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/group_system.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/inventory/replica_sync.h"
#include "openwow/game/knowledge_base.h"
#include "openwow/game/lfg_system.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/minigame_system.h"
#include "openwow/game/missile_trajectory.h"
#include "openwow/game/object_types.h"
#include "openwow/game/objects/cgdynamicobject.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/player_bag_family.h"
#include "openwow/game/quest_dialog_close.h"
#include "openwow/game/quest_dialog_text.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spellbook_frame.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/taint_system.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/taxi_system.h"
#include "openwow/game/trainer_frame.h"
#include "openwow/game/tracked_unit_state_slice.h"
#include "openwow/game/update_field_event_mapper.h"
#include "openwow/game/update_object_parser.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/voice_chat.h"
#include "openwow/net/adapters/diagnostics/packet_log.h"
#include "openwow/net/client_services.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/capture_point_ui_manager.h"
#include "openwow/ui/game/camera_lua_bindings.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/api/game_lua_api_gossip.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/game/inventory/adapters/lua/container_lua_api.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/world_map_system.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/game/reputation_info.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <limits>

namespace openwow::game {

void WorldSession::QueueSpellVisualPresentationEvent(
    SpellVisualPresentationEvent event, const std::uint32_t delay_ms) {
  std::vector<SpellVisualPresentationEvent> events;
  events.push_back(std::move(event));
  QueueSpellVisualPresentationEvents(std::move(events), delay_ms);
}

void WorldSession::QueueSpellVisualPresentationEvents(
    std::vector<SpellVisualPresentationEvent> events,
    const std::uint32_t delay_ms) {
  auto now = CurrentClientTimeMs();
  if (now == 0u) now = core::GameClock::GetTickCount32();
  pending_spell_visual_presentation_events_.reserve(
      pending_spell_visual_presentation_events_.size() + events.size());
  for (auto& event : events) {
    pending_spell_visual_presentation_events_.push_back({
        .ready_at_ms = now + delay_ms,
        .event = std::move(event),
    });
  }
}

std::vector<SpellVisualPresentationEvent>
WorldSession::TakeReadySpellVisualPresentationEvents() {
  auto now = CurrentClientTimeMs();
  if (now == 0u) now = core::GameClock::GetTickCount32();
  std::vector<SpellVisualPresentationEvent> ready;
  auto write = pending_spell_visual_presentation_events_.begin();
  for (auto read = pending_spell_visual_presentation_events_.begin();
       read != pending_spell_visual_presentation_events_.end(); ++read) {

    if (static_cast<std::int32_t>(now - read->ready_at_ms) >= 0) {
      ready.push_back(std::move(read->event));
      continue;
    }
    if (write != read) {
      *write = std::move(*read);
    }
    ++write;
  }
  pending_spell_visual_presentation_events_.erase(
      write, pending_spell_visual_presentation_events_.end());
  return ready;
}

void WorldSession::QueueEquipmentPresentation() {
  const auto player_guid = map_runtime_.objects().GetActivePlayerGuid();
  const auto owner = map_runtime_.objects().GetObjectHandle(player_guid);
  if (!owner.has_value()) {
    return;
  }

  EquipmentPresentation presentation{
      .owner = *owner,
      .inventory_revision = inventory_replica_.revision(),
  };
  for (std::size_t slot = 0; slot < presentation.slots.size(); ++slot) {
    const auto* item =
        inventory_replica_.GetEquipSlot(static_cast<std::uint8_t>(slot));
    if (item == nullptr || item->IsEmpty()) {
      continue;
    }
    auto& output = presentation.slots[slot];
    output.item_id = item->entry;
    output.permanent_enchantment = item->GetPermanentEnchant();
    output.temporary_enchantment = item->GetTemporaryEnchant();
    if (const auto* definition = query_cache_.GetItemTemplate(item->entry);
        definition != nullptr) {
      output.display_id = definition->display_id;
      output.inventory_type =
          static_cast<std::uint8_t>(definition->inventory_type);
      output.sheath_type = static_cast<std::uint8_t>(definition->sheath);
      if (dbc_ != nullptr) {
        if (const auto* display =
                dbc_->item_display_info().LookupEntry(definition->display_id);
            display != nullptr) {
          output.item_visual = display->item_visuals_id;
        }
      }
    }
  }

  if (active_player_ammo_attachment_selection_pending_) {
    const auto* const ranged_item =
        inventory_replica_.GetEquipSlot(InventorySlots::kRanged);
    if (ranged_item == nullptr || ranged_item->IsEmpty()) {
      active_player_ammo_attachment_id_ = kAmmoProjectileAttachmentId;
      active_player_ammo_attachment_selection_pending_ = false;
    } else if (const auto* const ranged_definition =
                   query_cache_.GetOrRequestItemTemplate(ranged_item->entry);
               ranged_definition != nullptr) {
      active_player_ammo_attachment_id_ =
          ranged_definition->inventory_type == InventoryType::Thrown
              ? kThrownProjectileAttachmentId
              : kAmmoProjectileAttachmentId;
      active_player_ammo_attachment_selection_pending_ = false;
    }
  }

  if (const auto* const player = map_runtime_.objects().GetLocalPlayerTyped();
      player != nullptr && dbc_ != nullptr) {
    const auto ammo_entry = player->GetUInt32(PLAYER_AMMO_ID);
    if (const auto* const ammo_row = dbc_->item().LookupEntry(ammo_entry);
        ammo_row != nullptr) {
      presentation.ammo_display_id = ammo_row->display_info_id;
    }
  }
  presentation.ammo_attachment_id = active_player_ammo_attachment_id_;
  inventory_presentation_changes_.equipment = std::move(presentation);
}

namespace {

constexpr std::int32_t kLegacyTokenSeedFollowUpWindow = 0x1A4;
constexpr std::uint32_t kLegacyTokenSeedFollowUpValue = 0x60231D69;
constexpr std::uint32_t kCommentatorSpectatorFlags2 = 0x00080000u;
constexpr std::uint32_t kCommentatorAdminFlags2 = 0x00400000u;

constexpr std::uint32_t kArenaMapType = 4u;

constexpr std::uint32_t kPlayerArenaFactionByteShift = 24u;

constexpr std::size_t kForceAnimStringReadBound = 0x200u;

constexpr std::size_t kWhoisResponseStringReadBound = 0x100u;

constexpr std::size_t kRWhoisAccountStringReadBound = 0x500u;
constexpr std::size_t kRWhoisCharacterStringReadBound = 0x30u;

constexpr std::int32_t kRWhoisFailureResult = -1;
constexpr char kRWhoisFailureText[] = "RWhoIs failed\n";
constexpr char kRWhoisNotFoundText[] = "Not found\n";
constexpr char kRWhoisAccountFormat[] = "Account: %s\n";
constexpr char kRWhoisCharacterFormat[] = "  %s\n";
constexpr std::uint32_t kNameQueryDispatchBudget = 128u;
constexpr std::uint32_t kItemTemplateQueryDispatchBudget = 256u;
constexpr std::uint32_t kShapeshiftAuraType = 36u;
constexpr int kCommentatorCameraView = 6;
constexpr int kDefaultWorldCameraView = 1;

void ClearPendingDbCacheQueriesOnWorldLeave(WorldSession &session) {

  session.query_cache().ClearPendingEntriesOnLogout();
  session.quests().ClearPendingQueriesOnLogout();
  session.misc().ClearPendingPageTextQueriesOnLogout();
  session.pet().ClearPendingNameQueriesOnLogout();
  session.petition().ClearPendingQueriesOnLogout();
  session.item_interactions().reset(0);
  session.arena().ClearPendingQueriesOnLogout();
  session.dance_studio().ClearPendingQueriesOnLogout();
}

void DispatchPaperDollInfoFrameItemEvents(WorldSession &session, const CGItem_C &item) {
  const auto *active_player = session.objects().GetLocalPlayerTyped();
  if (active_player == nullptr) {
    return;
  }

  auto absolute_slot =
      session.inventory_replica().FindSlotByGuid(
          item.GetGuid().GetRawValue());
  if (absolute_slot < 0) {
    const auto container_guid = item.GetContainedIn().GetRawValue();
    if (container_guid != 0 &&
        container_guid != active_player->GetGuid().GetRawValue()) {
      absolute_slot =
          session.inventory_replica().FindSlotByGuid(container_guid);
    }
  }

  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  if (absolute_slot >= InventorySlots::kBankStart &&
      absolute_slot < InventorySlots::kBankBagEnd) {
    dispatch.FireEventArgs(ui::game::events::PLAYERBANKSLOTS_CHANGED,
                           {static_cast<int>(
                               absolute_slot -
                               InventorySlots::kBankStart + 1)});
  }
  const auto owner_guid = item.GetOwner();
  if (!owner_guid.IsEmpty()) {
    dispatch.FireUnitInventoryChanged(owner_guid.GetRawValue());
  }
}

bool ContainerHasUnresolvedItemTemplate(const WorldSession& session,
                                        const std::int32_t container) {
  const auto unresolved = [&session](const ItemInstance* item) {
    return item != nullptr && item->entry != 0 &&
           session.query_cache().IsItemQueryPending(item->entry);
  };
  const auto& inventory = session.inventory_replica();
  if (container == -2 || container == -4 || container == 0) {
    const auto count = container == 0
                           ? PlayerInventoryReplica::kBackpackSize
                           : PlayerInventoryReplica::kKeyringSlots;
    for (std::uint8_t slot = 0; slot < count; ++slot) {
      const ItemInstance* item = nullptr;
      if (container == -2) {
        item = inventory.GetKeyringSlot(slot);
      } else if (container == -4) {
        item = inventory.GetItemInSlot(static_cast<std::uint8_t>(
            InventorySlots::kCurrencyStart + slot));
      } else {
        item = inventory.GetBackpackSlot(slot);
      }
      if (unresolved(item)) {
        return true;
      }
    }
    return false;
  }

  const BagInfo* bag = nullptr;
  if (container >= 1 && container <= PlayerInventoryReplica::kMaxBags) {
    bag = inventory.GetBag(static_cast<std::uint8_t>(container));
  } else if (container >= 5 && container <= 11) {
    bag = inventory.GetBankBag(static_cast<std::uint8_t>(container - 5));
  }
  if (bag == nullptr) {
    return false;
  }
  if (unresolved(&bag->item)) {
    return true;
  }
  return std::any_of(bag->slots.begin(), bag->slots.end(),
                     [&unresolved](const ItemInstance& item) {
                       return unresolved(&item);
                     });
}

void RefreshResolvedItemTemplateUi(WorldSession &session, const std::uint64_t item_guid,
                                   const std::uint32_t item_entry) {
  (void)ui::game::detail::RefreshActionSlotsForChangedItemEntry(session, item_entry);

  const ObjectGuid guid(item_guid);
  if (const auto *item = session.objects().GetItem(guid); item != nullptr) {
    DispatchPaperDollInfoFrameItemEvents(session, *item);
  } else if (const auto *container = session.objects().GetContainer(guid); container != nullptr) {
    DispatchPaperDollInfoFrameItemEvents(session, *container);
  }

  session.ResolveInventoryTemplateContainers();
}

QueryCache::CallbackKey ItemTemplateObjectCallbackKey(
    const ObjectHandle handle) {
  static const int kCallbackIdentity = 0;
  const auto folded_generation = static_cast<std::uint32_t>(
      handle.generation ^ (handle.generation >> 32u));
  return QueryCache::CallbackKey(
      reinterpret_cast<std::uintptr_t>(&kCallbackIdentity),
      folded_generation);
}

void RequestCreatedItemTemplateResolution(
    WorldSession& session, const CGItem_C& item) {
  const auto entry = item.GetEntry();
  const auto handle = session.objects().GetObjectHandle(item.GetGuid());
  if (entry == 0 || !handle.has_value()) {
    return;
  }

  QueryCache::QueryRequestOptions options{
      .context = item.GetGuid().GetRawValue(),
      .callback_key = ItemTemplateObjectCallbackKey(*handle),
      .dedupe_callbacks = true,
      .callback =
          [&session, handle = *handle, entry](const bool success) {
            if (!success) {
              return;
            }
            const auto* object =
                session.objects().ResolveObjectHandle(handle);
            if (object == nullptr ||
                (!object->IsItem() && !object->IsContainer()) ||
                static_cast<const CGItem_C*>(object)->GetEntry() != entry) {
              return;
            }
            RefreshResolvedItemTemplateUi(
                session, handle.guid.GetRawValue(), entry);
          },
  };
  if (session.query_cache().GetOrRequestItemTemplate(
          entry, std::move(options)) != nullptr) {
    RefreshResolvedItemTemplateUi(
        session, handle->guid.GetRawValue(), entry);
  }
}

void RequestChangedItemTemplateRefresh(WorldSession &session,
                                       const PlayerInventoryReplicaSync::ItemTemplateRefreshRequest &request) {
  if (request.item_guid == 0 || request.entry == 0) {
    return;
  }

  (void)session.query_cache().InvalidateItemTemplate(request.entry);
  const auto handle =
      session.objects().GetObjectHandle(ObjectGuid(request.item_guid));
  if (!handle.has_value()) {
    return;
  }
  auto options = QueryCache::QueryRequestOptions{
      .context = request.item_guid,
      .callback_key = ItemTemplateObjectCallbackKey(*handle),
      .dedupe_callbacks = true,
  };
  options.callback = [&session, request, handle = *handle](const bool success) {
    if (!success) {
      return;
    }
    const auto* object = session.objects().ResolveObjectHandle(handle);
    if (object == nullptr ||
        (!object->IsItem() && !object->IsContainer()) ||
        static_cast<const CGItem_C*>(object)->GetEntry() != request.entry) {
      return;
    }
    RefreshResolvedItemTemplateUi(session, request.item_guid, request.entry);
  };
  if (session.query_cache().GetOrRequestItemTemplate(request.entry, std::move(options)) != nullptr) {
    RefreshResolvedItemTemplateUi(session, request.item_guid, request.entry);
  }
}

ObjectGuid ResolveArenaOpponentPetOwner(const CGUnit_C &unit) {
  const auto charmed_by = unit.State().GetCharmedBy();
  if (!charmed_by.IsEmpty()) {
    return charmed_by;
  }

  const auto summoned_by = unit.State().GetSummonedBy();
  if (!summoned_by.IsEmpty()) {
    return summoned_by;
  }

  return unit.State().GetCreatedBy();
}

bool HasAuraType(const std::array<std::uint32_t, 3> &effect_apply_aura,
                 const std::uint32_t aura_type) {
  return std::find(effect_apply_aura.begin(), effect_apply_aura.end(), aura_type) !=
         effect_apply_aura.end();
}

[[nodiscard]] const openwow::data::WDBPersistence *GetConfiguredDbCachePersistence(
    const openwow::data::DBCacheRuntime& runtime) {
  const auto &persistence = runtime.persistence();
  return persistence.GetCacheDirectory().empty() ? nullptr : &persistence;
}

void FireReadyRunePowerUpdates(const std::uint32_t ready_mask_delta) {
  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  for (int i = 0; i < kClientTrackedRuneSlots; ++i) {
    if ((ready_mask_delta & (1u << i)) == 0) {
      continue;
    }
    dispatch.FireEventArgs(ui::game::events::RUNE_POWER_UPDATE, {i + 1, true});
  }
}

void RefreshRuneUsability(WorldSession &session) {
  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireEvent(ui::game::events::SPELL_UPDATE_USABLE);
  if (ui::game::detail::RefreshAllActionSlotValidation(session)) {
    dispatch.FireActionbarUpdateUsable();
  }
  dispatch.FirePetBarUpdateUsable();
}

void ResetSocketUiForWorldLeave(WorldSession& session,
                                const bool fire_close_event) {
  session.item_interactions().cancel_socket();
  if (fire_close_event) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::SOCKET_INFO_CLOSE);
  }
}

bool CanApplyCommentatorFollowCamera(const WorldSession &session, const CGPlayer_C &active_player) {
  const auto flags2 = active_player.State().GetUnitFlags2();
  if ((flags2 & kCommentatorSpectatorFlags2) == 0u) {
    return false;
  }

  if ((flags2 & kCommentatorAdminFlags2) != 0u) {
    return true;
  }

  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto *map_entry = dbc->map().LookupEntry(session.current_map_id());
  return map_entry != nullptr && map_entry->map_type == kArenaMapType;
}

void SyncCommentatorCameraToActivePlayer(CommentatorState &commentator,
                                         const CGPlayer_C &active_player, const float pitch) {

  const auto player_position = active_player.GetPosition();
  commentator.SetCamera(player_position.x, player_position.y, player_position.z + 1.5f,
                        active_player.GetWorldFacing(), pitch);
}

void SwitchCommentatorCameraView(openwow::world::WorldCamera* world_camera,
                                 const int view_index) {
  if (world_camera == nullptr) {
    return;
  }

  ui::game::detail::SyncCameraViewPreset(*world_camera, view_index);
  world_camera->ApplyViewPreset(view_index, true);
  ui::game::CVarSystem::Instance().SetCVar(
      "cameraView", std::to_string(view_index));
}

void CloseTrainerForWorldLogout(WorldSession &session) {
  if (session.gossip().has_trainer()) {
    ui::game::CloseTrainerInteraction(
        session, session.gossip().trainer().trainer_guid,
        ui::game::NpcInteractionClosureCause::UnitUnavailable);
  }

  Trainer_ResetFrameState();
}

}

void WorldSession::ResolveInventoryTemplateContainers() {
  std::vector<std::int32_t> ready;
  auto it = deferred_inventory_template_containers_.begin();
  while (it != deferred_inventory_template_containers_.end()) {
    if (ContainerHasUnresolvedItemTemplate(*this, *it)) {
      ++it;
      continue;
    }
    if (std::find(ready.begin(), ready.end(), *it) == ready.end()) {
      ready.push_back(*it);
    }
    it = deferred_inventory_template_containers_.erase(it);
  }
  for (const auto container : ready) {
    ui::game::ScriptEventDispatch::Get().FireBagUpdate(container);
  }
}

void WorldSession::FlushInventoryReplicaTransaction() {
  for (const auto& request : inventory_bridge_.ConsumeItemTemplateRefreshes()) {
    RequestChangedItemTemplateRefresh(*this, request);
  }

  QueueEquipmentPresentation();
  for (const auto entry : inventory_bridge_.ConsumeChangedEntries()) {
    (void)ui::game::detail::RefreshActionSlotsForChangedItemEntry(*this, entry);
    SpellBookFrame::HandleTrackedMultiCastTotemItemEntry(*this, entry);
  }

  for (const auto container : inventory_bridge_.ConsumeChangedContainers()) {
    const auto deferred = std::find(deferred_inventory_template_containers_.begin(),
                                    deferred_inventory_template_containers_.end(),
                                    container);
    if (ContainerHasUnresolvedItemTemplate(*this, container)) {
      if (deferred == deferred_inventory_template_containers_.end()) {
        deferred_inventory_template_containers_.push_back(container);
      }
      continue;
    }
    if (deferred != deferred_inventory_template_containers_.end()) {
      deferred_inventory_template_containers_.erase(deferred);
    }
    ui::game::ScriptEventDispatch::Get().FireBagUpdate(container);
  }
}

void WorldSession::BootstrapCommentatorEnterWorld() {
  if (const auto *local_player = map_runtime_.objects().GetLocalPlayerTyped(); local_player != nullptr) {
    if (CanApplyCommentatorFollowCamera(*this, *local_player)) {
      auto &commentator = CommentatorState::Get();
      commentator.SetActive(true);
      SwitchCommentatorCameraView(world_camera_, kCommentatorCameraView);
      SyncCommentatorCameraToActivePlayer(
          commentator, *local_player, local_player->GetMovementInfo().pitch);
    }
  }
  ui::game::ScriptEventDispatch::Get().FireCommentatorEnterWorld();
}

WorldSession::WorldSession(openwow::data::DBCacheRuntime& db_cache_runtime,
                           PlayerInventoryReplica& inventory_replica,
                           CharacterMapRuntime& map_runtime,
                           QueryCache& query_cache,
                           TransportManager& transport_manager,
                           ItemDefinitions& item_definitions,
                           const data::dbc::DbcLoader& dbc_loader,
                           UnitMissileTrajectory_C& missile_trajectory,
                           net::wotlk::MainThreadPacketDispatcher&
                               packet_dispatcher,
                           PlayerControlRuntime& player_control_runtime,
                           ConsumeLegacyTokenSeedVerificationFn
                               consume_legacy_token_seed_verification,
                           BuildBotDetectedDigestFn build_bot_detected_digest,
                             const SpellbookSystem& spellbook,
                             const PvPInfo& pvp,
                             const ReputationInfo& reputation,
                             SpellCastRuntime& spell_cast_runtime,
                              openwow::audio::SoundRuntime& sound_runtime,
                              openwow::core::ida::GameTimeData* const shared_game_time)
    : packet_dispatcher_(packet_dispatcher),
      player_control_runtime_(player_control_runtime),
      db_cache_runtime_(db_cache_runtime),
      sound_runtime_(sound_runtime),
      spell_cast_runtime_(spell_cast_runtime),
      dbc_(&dbc_loader),
      inventory_replica_(inventory_replica),
      item_definitions_(item_definitions),
      map_runtime_(map_runtime),
      missile_trajectory_(missile_trajectory),
      item_locks_(map_runtime),
      movement_(),
      action_assignments_(
          macro_catalog_,
          [this](const std::uint32_t item_id) {
            return ResolveItemUseSpellWithEquippedFallback(
                inventory_replica_, item_definitions_, item_id);
          }),
      quests_(
          db_cache_runtime_, dbc_loader,
          [this](const std::string_view text, const bool empty_as_space) {
            return ExpandQuestDialogText(*this, text, empty_as_space);
          }),
      loot_(inventory_replica_, item_definitions_, map_runtime),
      spell_book_(
          dbc_loader, map_runtime, spell_cast_runtime_, action_assignments_,
          SpellBookEffects{
              .held_cursor =
                  [this]() {
                    return held_cursor_;
                  },
              .spell_learned =
                  [this](const std::uint32_t spell_id,
                         const bool superseded,
                         const std::uint32_t old_spell_id) {

                    gossip_.MarkTrainerSpellKnown(
                        static_cast<std::int32_t>(spell_id));
                    SpellBookFrame::LearnSpell(
                        *this, spell_id, superseded, old_spell_id);
                  },
              .spell_forgotten =
                  [this](const std::uint32_t spell_id) {
                    SpellBookFrame::ForgetSpell(*this, spell_id);
                  },
              .multi_cast_slot_mask_changed =
                  [this](const std::uint32_t slot_mask) {
                    SpellBookFrame::HandleLearnedMultiCastTotemSlotMask(
                        *this, slot_mask);
                  },
              .finalize_initial_companion_catalog =
                  [this]() {
                    SpellBookFrame::FinalizeInitialCompanionCatalog(*this);
                  },
              .refresh_active_player_mutation_ui =
                  [this]() {
                    if (map_runtime_.objects().GetActivePlayer() == nullptr) {
                      return;
                    }
                    auto& dispatch =
                        ui::game::ScriptEventDispatch::Get();
                    if (gossip_.has_trainer()) {
                      dispatch.FireEvent(
                          ui::game::events::TRAINER_UPDATE);
                    }
                    dispatch.FireEvent(
                        ui::game::events::SPELL_UPDATE_USABLE);
                    if (ui::game::detail::RefreshAllActionSlotValidation(
                            *this)) {
                      dispatch.FireActionbarUpdateUsable();
                    }
                    dispatch.FirePetBarUpdateUsable();
                  },
              .trainer_spellbook_changed =
                  [this]() {
                    if (gossip_.has_trainer()) {
                      ui::game::ScriptEventDispatch::Get().FireEvent(
                          ui::game::events::TRAINER_UPDATE);
                    }
                  },
              .apply_spell_modifier =
                  [this](const data::dbc::SpellEntry& spell,
                         const SpellModOp op, const std::int32_t input) {
                    auto value = input;
                    const auto* const player =
                        map_runtime_.objects().GetActivePlayer();
                    if (player == nullptr) {
                      return value;
                    }
                    (void)aura_.ApplySpellModifierDeltas(
                        detail::ResolveSpellModifierFamily(*player, dbc_),
                        spell, op, &value);
                    return value;
                  },
              .main_hand_weapon_delay_ms =
                  [this]() -> std::uint32_t {

                    const auto* const equipped = inventory_replica_.GetEquipSlot(
                        InventorySlots::kMainHand);
                    if (equipped == nullptr || equipped->entry == 0u) {
                      return 0u;
                    }
                    const auto* const item_template =
                        item_definitions_.GetItem(equipped->entry);
                    return item_template != nullptr ? item_template->delay : 0u;
                  },
              .insert_pet_cooldown =
                  [this](const PetCooldown& cooldown) {
                    pet_.InsertCooldown(cooldown);
                  },
          }),
      guild_(db_cache_runtime_),
      dance_studio_(std::make_unique<DanceStudioSystem>()),
      achievement_metadata_(
          std::make_unique<DbcAchievementMetadataCatalog>()),
      achievements_(achievement_metadata_.get()),
      pet_(db_cache_runtime_),
      misc_(db_cache_runtime_),
      query_cache_(query_cache),
      session_(shared_game_time),
      petition_(db_cache_runtime_),
      item_use_requirement_sources_(spellbook, pvp, reputation),
      arena_(db_cache_runtime_),
      interaction_(*this),
      inventory_commands_(
          inventory_replica_, item_definitions_, map_runtime, interaction_, query_cache_,
          item_use_requirement_sources_, session_),
      inventory_bridge_(inventory_replica_, map_runtime),
      auction_packets_(
          auction_, mail_, query_cache_, interaction_,
          [this](const net::wotlk::WorldPacket& packet) {
            return Send(packet);
          }),
      transport_mgr_(transport_manager),
      consume_legacy_token_seed_verification_(
          std::move(consume_legacy_token_seed_verification)),
      build_bot_detected_digest_(std::move(build_bot_detected_digest)) {
  SpellbookSystem::Get().SetDbcLoader(
      &dbc_loader, map_runtime_.objects());
  combat_log_.SetObjectManagerProvider(
      [this]() -> ObjectManager* { return &map_runtime_.objects(); });
  auction_packets_.SetDbcLoader(&dbc_loader);
  loot_.BindDbc(&dbc_loader);
  achievement_metadata_->Bind(&dbc_loader);
  battleground_.SetDbcLoader(&dbc_loader);
  spell_log_.SetDbcLoader(&dbc_loader);
  dance_studio_->SetNameHasher([](const std::string_view name) {
    const std::string storage(name);
    return core::SStrHashCI(storage.c_str());
  });
  dance_studio_->SetNameEqual(
      [](const std::string_view left, const std::string_view right) {
        const std::string left_storage(left);
        const std::string right_storage(right);
        return core::SStrCmpNoCase(left_storage.c_str(), right_storage.c_str(),
                                   std::max(left.size(), right.size()) + 1) == 0;
      });
  dance_studio_->SetConsoleSink(
      [](const std::string& text, const DanceStudioSystem::ConsoleColor color) {
        const int console_color =
            color == DanceStudioSystem::ConsoleColor::kError
                ? core::ida::COLOR_ERROR
                : core::ida::COLOR_DEFAULT;
        core::ida::ConsoleAddLine(text, console_color);
      });
  dance_studio_->SetSystemMessageSink(
      [](const DanceSystemMessageId message_id) {
        ui::game::DisplaySystemMessage(static_cast<int>(message_id.value));
      });
  dance_studio_->SetUnitDanceStateProvider(
      [this](const DanceUnitGuid unit_guid)
          -> std::optional<DancePlaybackState> {
        const auto* unit =
            map_runtime_.objects().GetMutableUnit(ObjectGuid(unit_guid.value));
        return unit != nullptr
                   ? std::optional(ToDancePlaybackState(
                         unit->Animation().Dance().Get().IsActive()))
                   : std::nullopt;
      });
  dance_studio_->SetUnitDanceCanceller([this](const DanceUnitGuid unit_guid) {
    if (auto* unit =
            map_runtime_.objects().GetMutableUnit(ObjectGuid(unit_guid.value));
        unit != nullptr) {
      unit->Animation().Dance().Cancel();
    }
  });
  dance_studio_->SetUnitDanceStarter(
      [this](const DanceUnitGuid unit_guid,
             DanceSequence sequence,
             const DanceMoveCatalog& catalog) {
        if (auto* unit =
                map_runtime_.objects().GetMutableUnit(ObjectGuid(unit_guid.value));
            unit != nullptr) {
          unit->Animation().Dance().SetData(*unit, *this, std::move(sequence), catalog);
        }
      });
  SetOnMovementActivatedCallback(
      [this]() { HandleMovementActivatedSpellInterrupts(*this); });
  RegisterInstanceConsoleCommands();

  GroupSystem::Get().Reset();

  GuildSystem::Get().Reset();

  ReputationInfo::Get().Init();
  FactionSystem::Get().Reset();

  CommentatorState::Get().Reset();

  mail_.Initialize();

  auto &cvars = ui::game::CVarSystem::Instance();
  if (cvars.Exists("combatLogRetentionTime")) {
    combat_log_.SetRetentionTime(
        static_cast<float>(cvars.GetCVarInt("combatLogRetentionTime")));
  }

  MinigameSystem::Get().Reset();
  ui::game::detail::ResetActionBarRuntimeState(*this);

  ReputationInfo::Get().SetOnUpdate([]() {
    ui::game::ScriptEventDispatch::Get().FireGlobalEvent(ui::game::events::UPDATE_FACTION);
  });
  ReputationInfo::Get().SetFactionStateRefreshCallback(
      [this]() { RefreshActivePlayerFactionDependentState(); });
  duel_.SetOnCountdownMessage([this](const std::string &message) {
    ChatFrame_DisplayMessage(
        map_runtime_.objects(), message.c_str(), ChatDisplayType::kSystem,
        nullptr, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
  });

  ObjectManagerCallbacks& cbs = object_manager_callbacks_;
  cbs.on_update_object_batch_started = [this]() {
    update_object_batch_active_ = true;
  };
  cbs.on_update_object_batch_finished = [this](const bool committed) {
    update_object_batch_active_ = false;
    if (committed) {
      FlushInventoryReplicaTransaction();
      return;
    }
    (void)inventory_bridge_.ConsumeChangedContainers();
    (void)inventory_bridge_.ConsumeChangedEntries();
    (void)inventory_bridge_.ConsumeItemTemplateRefreshes();
  };
  cbs.send_sheathed = [this](const std::uint32_t state) {
    interaction_.SendSetSheathed(state);
  };
  cbs.on_object_created = [this](WorldObject &obj) {
    if (obj.IsUnit()) {
      auto& unit = static_cast<CGUnit_C&>(obj);

      unit.Mount().ApplyDisplayChange(
          unit, *this, unit.Mount().DisplayId(unit));

      unit.SpellVisuals().RefreshDescriptorChannelVisual(*this);
      if (!unit.ShouldFadeOnShow()) {
        unit.SetOpacityTarget(unit.GetModelOpacity(), 0u);
      }
      unit.Animation().RefreshSelectedStandAnimation(*this, 0u, ~0u);

      const auto tick = core::GameClock::GetTickCount32();
      UnitVehicle_RebuildCreatePassengerAttachment(
          *this, unit, static_cast<double>(tick));
      unit.Movement().RefreshCreateRootedGroundContact();

      unit.Movement().RunMovementPostUpdate(*this, tick,
                                            0u);
      missile_trajectory_.NotifyUnitCreated(&unit);
    }

    RetryPendingChatMessagesForGuid(obj.GetGuid(), false);
    RetryPendingTextEmotesForGuid(obj.GetGuid(), false);
    TryBindGameObjectTemplateInfo(obj);
    RefreshCreatedGameObjectQuestgiverStatus(obj);
    TryRegisterCapturePointObject(obj);
    BattlefieldInfo::Get().ObserveBattlefieldVehicle(obj, dbc_);

    if (auto *corpse = dynamic_cast<CGCorpse_C*>(&obj);
        corpse != nullptr) {
      corpse->UpdateGuildTabard(*this, true);
    }

    if (obj.IsDynamicObject()) {
      auto& dynamic_object = static_cast<CGDynamicObject_C&>(obj);
      auto* const active_player = objects().GetMutablePlayer(
          objects().GetLocalPlayerGuid());
      if (dynamic_object.IsFarsightFocus() && active_player != nullptr &&
          dynamic_object.GetCaster() == active_player->GetGuid() &&
          active_player->GetFarsightTarget() == dynamic_object.GetGuid()) {
        active_player->ActivateFarSightFocus(*this, dynamic_object);
      }
    }

    SyncTrackedGroupMemberSnapshot(obj);
    SyncArenaOpponentSnapshot(obj, true);
    if (obj.IsPlayer()) {
      auto& player = static_cast<CGPlayer_C&>(obj);
      player.Interaction().RefreshLinkedVisibleUnitFactionState();

      (void)query_cache_.GetOrRequestPlayerName(player.GetGuid().GetRawValue());

      auto& dispatch = ui::game::ScriptEventDispatch::Get();
      dispatch.FirePerUnitEvent(ui::game::events::PLAYER_GUILD_UPDATE,
                                player.GetGuid().GetRawValue());
      dispatch.FireEvent(ui::game::events::TABARD_CANSAVE_CHANGED);
      const auto guild_id = player.GetGuildID();
      if (guild_.BeginGuildQuery(guild_id)) {
        auto packet = GuildManager::BuildGuildQuery(guild_id);
        interaction_.SendRawPacket(packet);
      }

      const auto slot = GroupSystem::Get().FindPartySlotByGuid(obj.GetGuid().GetRawValue());
      if (slot >= 0) {
        ui::game::ScriptEventDispatch::Get().FirePartyMemberEnable(static_cast<std::uint8_t>(slot));
      }
    }

    if (obj.IsItem() || obj.IsContainer()) {
      inventory_bridge_.OnItemCreated(obj);

      RequestCreatedItemTemplateResolution(
          *this, static_cast<const CGItem_C&>(obj));
    }
  };
  cbs.on_object_world_published = [this](WorldObject& obj) {
    if (obj.IsUnit()) {
      auto& unit = static_cast<CGUnit_C&>(obj);

      unit.SpellVisuals().RefreshDescriptorChannelVisual(*this);
      if (!unit.ShouldFadeOnShow()) {
        unit.SetOpacityTarget(unit.GetModelOpacity(), 0u);
      }
      unit.Animation().RefreshSelectedStandAnimation(*this, 0u, ~0u);
      unit.Mount().ApplyDisplayChange(
          unit, *this, unit.Mount().DisplayId(unit));
      const auto tick = core::GameClock::GetTickCount32();
      UnitVehicle_RebuildCreatePassengerAttachment(
          *this, unit, static_cast<double>(tick));
      unit.Movement().RefreshCreateRootedGroundContact();

      SyncTrackedGroupMemberSnapshot(obj);
      SyncArenaOpponentSnapshot(obj, true);

      if (obj.IsPlayer()) {
        auto& player = static_cast<CGPlayer_C&>(obj);
        player.Interaction().RefreshLinkedVisibleUnitFactionState();

        if (player.IsActivePlayer()) {
          player.RecountEquippedGemColorCounts();
        }

        const auto slot =
            GroupSystem::Get().FindPartySlotByGuid(obj.GetGuid().GetRawValue());
        if (slot >= 0) {
          ui::game::ScriptEventDispatch::Get().FirePartyMemberEnable(
              static_cast<std::uint8_t>(slot));
        }
      }
    }

    if (obj.IsGameObject()) {
      auto& game_object = static_cast<CGGameObject_C&>(obj);

      TryBindGameObjectTemplateInfo(obj);
      if (game_object.GetTemplateInfo() != nullptr) {
        game_object.RefreshDifficultyVisibilityControlState(*this);
      }
    }

    if (!obj.IsItem() && !obj.IsContainer()) {
      return;
    }

    RequestCreatedItemTemplateResolution(
        *this, static_cast<const CGItem_C&>(obj));
  };
  cbs.on_object_packet_promoted = [this](WorldObject& obj) {
    if (obj.IsPlayer() && obj.IsActivePlayer()) {

      RebindActivePlayerDescriptorCallbacks(obj.GetGuid());
    }

    if (!obj.IsGameObject()) {
      return;
    }
    auto& game_object = static_cast<CGGameObject_C&>(obj);

    if (game_object.GetTemplateInfo() != nullptr) {
      game_object.RefreshDifficultyVisibilityControlState(*this);
    }
  };
  cbs.on_object_updated = [this](WorldObject &obj) {

    if (obj.IsItem() || obj.IsContainer()) {
      inventory_bridge_.OnItemUpdated(obj);
      DispatchPaperDollInfoFrameItemEvents(*this, static_cast<const CGItem_C &>(obj));
    }

    RetryPendingChatMessagesForGuid(obj.GetGuid(), false);
    RetryPendingTextEmotesForGuid(obj.GetGuid(), false);
    TryBindGameObjectTemplateInfo(obj);
    TryRegisterCapturePointObject(obj);
    SyncTrackedGroupMemberSnapshot(obj);
    SyncArenaOpponentSnapshot(obj, false);
  };
  cbs.on_unit_authoritative_movement =
      [this](CGUnit_C &unit, const MovementUpdate &update,
             const std::uint32_t client_receive_tick_ms) {
        unit.Movement().SynchronizeAuthoritativeState(
            *this, update, client_receive_tick_ms);
      };
  cbs.on_unit_create_movement_metadata =
      [this](CGUnit_C &unit, const MovementUpdate &update) {
        unit.Movement().ApplyCreateMovementMetadata(*this, update);
      };
  cbs.on_out_of_range_vehicle_transitions_ready = [this]() {
    VehiclePassengerC::ProcessQueuedTransitions(
        *this, static_cast<double>(openwow::core::GameClock::GetTickCount32()));
  };
  cbs.on_object_out_of_range = [this](const WorldObject &obj) {
    click_to_move().CancelInteraction(obj.GetGuid());
    if (tracked_guid_invalidation_callback_) {
      tracked_guid_invalidation_callback_(obj.GetGuid().GetRawValue());
    }
  };
  cbs.on_object_pre_destroyed = [this](const WorldObject &obj,
                                       const bool destroy_packet_death_cleanup) {
    if (obj.IsUnit()) {
      missile_trajectory_.RemoveCollisionTarget(
          static_cast<const CGUnit_C *>(&obj));
    }

    if (obj.IsItem() || obj.IsContainer()) {
      DispatchPaperDollInfoFrameItemEvents(*this, static_cast<const CGItem_C &>(obj));
    }

    if (!destroy_packet_death_cleanup) {
      return;
    }

    ui::game::HandleNpcInteractionLoss(
        *this, obj.GetGuid(),
        ui::game::NpcInteractionClosureCause::UnitDespawned);
  };
  cbs.on_object_destroyed_typed = [this](ObjectGuid guid, const TypeID type) {
    AuraTracker::Get().ClearAuras(guid);
    const bool was_item = type == TypeID::kItem || type == TypeID::kContainer;
    if (const auto* object = map_runtime_.objects().Get(guid);
        was_item && object != nullptr) {
      const auto& item = static_cast<const CGItem_C&>(*object);
      if (const auto handle = map_runtime_.objects().GetObjectHandle(guid);
          handle.has_value() && item.GetEntry() != 0) {
        query_cache_.CancelItemTemplateCallback(
            item.GetEntry(), ItemTemplateObjectCallbackKey(*handle));
      }
    }

    movement_spline_mgr_.RemoveEntity(guid.GetRawValue());

    if (guid == map_runtime_.objects().GetActivePlayerGuid()) {

      ResetAllMirrorTimers();
    }

    HandleCapturePointObjectDestroyed(guid);
    ClearDestroyedQuestgiverStatus(guid);
    BattlefieldInfo::Get().RemoveBattlefieldVehicle(guid);

    if (was_item) {
      inventory_bridge_.OnItemDestroyed(guid);
      QueueItemLifecycle(ItemLifecycleKind::kRemoved, guid.GetRawValue());
      item_interactions_.invalidate_item(guid);
      QueueItemLifecycle(ItemLifecycleKind::kDestroyed, guid.GetRawValue());
      if (const auto cleared_trade_slot = trade_.RemoveLocalPlayerTradeItemByGuid(guid.GetRawValue());
          cleared_trade_slot.has_value()) {
        Send(trade_protocol::EncodeClearItem(*cleared_trade_slot));
        if (*cleared_trade_slot == kTradeWillNotBeTradedSlot) {
          ui::game::ScriptEventDispatch::Get().FireTradePotentialBindEnchant(false);
        }
        ui::game::ScriptEventDispatch::Get().FireTradePlayerItemChanged(
            static_cast<int>(*cleared_trade_slot) + 1);
      }

      if (const auto slot = spell_book_.ClearTotemByGuid(guid.GetRawValue()); slot.has_value()) {
        ui::game::ScriptEventDispatch::Get().FirePlayerTotemUpdate(*slot + 1);
      }
      if (!update_object_batch_active_) {
        FlushInventoryReplicaTransaction();
      }
    }

    if (auto *const local_player = map_runtime_.objects().GetMutablePlayer(map_runtime_.objects().GetLocalPlayerGuid());
        local_player != nullptr && local_player->GetActiveFarSightFocusGuid() == guid) {
      local_player->ClearFarSightFocus(*this);
    }

    HandleTrackedGroupMemberDestroyed(guid);
  };
  cbs.on_player_self_created = [this](ObjectGuid guid) { OnLocalPlayerCreated(guid); };
  cbs.on_creature_entry_resolved = [this](const ObjectGuid guid) {
    if (const auto slot =
            spell_book_.FindTotemSlotByGuid(guid.GetRawValue());
        slot.has_value()) {
      ui::game::ScriptEventDispatch::Get().FirePlayerTotemUpdate(*slot + 1);
    }
  };
  cbs.on_transport_opened = [this](const ObjectGuid transport_guid) {
    if (world_camera_ != nullptr && world_camera_->bound_object() != 0u) {
      const auto *const camera_unit = map_runtime_.objects().GetUnit(
          ObjectGuid{world_camera_->bound_object()});
      if (camera_unit != nullptr &&
          camera_unit->Movement().Data().GetTransportGuid() ==
              transport_guid.GetRawValue()) {
        world_camera_->SetBoundObject(0u);
      }
    }

    CGPlayer_C *const active_player = map_runtime_.objects().GetActivePlayer();
    if (active_player == nullptr ||
        active_player->Movement().Data().GetTransportGuid() !=
            transport_guid.GetRawValue()) {
      return;
    }
    const std::uint32_t session_time = CurrentClientTimeMs();
    const std::uint32_t timestamp =
        session_time != 0u ? session_time
                           : openwow::core::GameClock::GetTickCount32();
    active_player->Movement().Data().QueueHeartbeat(timestamp);
    (void)active_player->Movement().ForceSetTransport(*this, 0u, 0xffu);
  };
  cbs.on_transport_attachment_destroyed =
      [this](const ObjectGuid transport_guid, const ObjectGuid target_guid,
             const bool movement_attachment,
             GameObjectAttachmentNode *const attachment) {

    if (world_camera_ != nullptr &&
        world_camera_->bound_object() == target_guid.GetRawValue()) {
      world_camera_->SetBoundObject(0u);
    }

    if (!movement_attachment) {

      if (attachment != nullptr) {
        attachment->ResetParentToWorld(map_runtime_.objects());
      }
      return;
    }

    CGUnit_C *const passenger =
        map_runtime_.objects().GetMutableUnit(target_guid);
    if (passenger == nullptr) {
      return;
    }

    auto &movement_data = passenger->Movement().Data();
    if (movement_data.GetTransportGuid() == 0u) {
      const auto &movement_info = passenger->GetMovementInfo();
      if (movement_info.IsOnTransport() &&
          !movement_info.transport.guid.IsEmpty()) {
        movement_data.SeedAuthoritativeTransportState(movement_info);
      }
    }

    const std::uint32_t session_time = CurrentClientTimeMs();
    const std::uint32_t timestamp =
        session_time != 0u ? session_time
                           : openwow::core::GameClock::GetTickCount32();
    if (passenger->IsActivePlayer()) {
      movement_data.QueueHeartbeat(timestamp);
    } else {
      (void)movement_data.TryInitRemoteMovement();
    }

    passenger->State().SetForcedVehicleTransition(true);
    (void)passenger->Movement().ForceSetTransport(
        *this, 0u, 0xffu, true);
    passenger->State().SetForcedVehicleTransition(false);

    (void)transport_guid;
  };
  cbs.on_fields_changed = [this](const WorldObject &obj, const FieldUpdateBatch &updates,
                                 bool is_create) { OnFieldsChanged(obj, updates, is_create); };
  map_runtime_.SetCallbacks(object_manager_callbacks_);
  runes_.SetRuneRegenRateFn([this](const RuneType rune_type) {
    const auto *local_player = map_runtime_.objects().GetLocalPlayerTyped();
    if (local_player == nullptr) {
      return 0.0f;
    }
    return local_player->GetRuneRegen(static_cast<std::uint8_t>(rune_type));
  });

  chat_sender_.BindRuntime(
      [this](const net::wotlk::WorldPacket& packet) {
        return Send(packet);
      },
      map_runtime_, chat_,
      [this]() {
        return CurrentClientTimeMs();
      });
  query_cache_.SetNameQueryDispatcher(
      [this](const std::uint64_t raw_guid) { interaction_.SendNameQuery(raw_guid); });
  query_cache_.SetCreatureQueryDispatcher([this](std::uint32_t entry, std::uint64_t context_guid) {
    interaction_.SendCreatureQuery(entry, context_guid);
  });
  query_cache_.SetGameObjectQueryDispatcher(
      [this](std::uint32_t entry, std::uint64_t context_guid) {
        interaction_.SendGameObjectQuery(entry, context_guid);
      });
  query_cache_.SetItemQueryDispatcher(
      [this](std::uint32_t entry, std::uint64_t ) { interaction_.SendItemQuery(entry); });
  query_cache_.SetTickCountProvider([this]() {
    const auto client_time_ms = CurrentClientTimeMs();
    return client_time_ms != 0 ? client_time_ms : openwow::core::GameClock::GetTickCount32();
  });

  combat_log_.SetTimestampFn(
      [this, epoch_base_s = 0.0, tick_base_s = 0.0, captured = false]() mutable {
        const auto client_time_ms = CurrentClientTimeMs();
        const auto effective_client_time_ms =
            client_time_ms != 0 ? client_time_ms
                                : openwow::core::GameClock::GetTickCount32();
        const double tick_s = static_cast<double>(effective_client_time_ms) / 1000.0;
        if (!captured) {
          captured = true;
          tick_base_s = tick_s;
          epoch_base_s = static_cast<double>(std::time(nullptr));
        }
        return epoch_base_s + (tick_s - tick_base_s);
      });

  query_cache_.SetNameQueryMaxInFlight(kNameQueryDispatchBudget);

  query_cache_.SetItemQueryMaxInFlight(kItemTemplateQueryDispatchBudget);

  (void)misc_.HydrateRetailPageTextCache(
      db_cache_runtime_.cache());
  (void)quests_.HydrateRetailQuestCache(
      db_cache_runtime_.cache());
  if (const auto *persistence =
          GetConfiguredDbCachePersistence(db_cache_runtime_);
      persistence != nullptr) {

    (void)query_cache_.HydrateRetailWdbCaches(
        db_cache_runtime_.cache());
    (void)query_cache_.LoadPlayerNameCacheWdb(persistence->GetCacheDirectory(),
                                              persistence->GetLocale());
    query_cache_.PopulateObjectManagerNameCache(map_runtime_.objects());
  }
  quests_.SetQuestQueryDispatcher(
      [this](std::uint32_t quest_id) { interaction_.SendQuestQuery(quest_id); });
  quests_.SetTickCountProvider([this]() {
    const auto client_time_ms = CurrentClientTimeMs();
    return client_time_ms != 0 ? client_time_ms : openwow::core::GameClock::GetTickCount32();
  });

  session_.BindWorldPacketHandlers(
      packet_dispatcher_,
      {
          .account_data_times =
              [](const AccountDataTimesInfo& account_data_times) {
                std::array<std::uint32_t, 8> times{};
                std::size_t timestamp_index = 0;
                for (std::size_t slot = 0;
                     slot < times.size() &&
                     timestamp_index < account_data_times.timestamps.size();
                     ++slot) {
                  if ((account_data_times.mask & (1u << slot)) != 0u) {
                    times[slot] =
                        account_data_times.timestamps[timestamp_index++];
                  }
                }
                auto& account_data = AccountData::Get();
                account_data.SetAccountDataTimes(times);
                account_data.SetNextUploadSequence(
                    account_data_times.server_time);
              },
          .logout_response =
              [](const LogoutResponseInfo& response) {
                openwow::net::ClientServices::Instance().OnLogoutResponse(
                    response.result, response.instant ? 1u : 0u);
              },
          .account_data_update =
              [this](const AccountDataUpdate& update) {
                if (update.type >= 8) {
                  return;
                }
                const auto type =
                    static_cast<AccountDataType>(update.type);
                if (!ShouldApplyAccountDataUpdate(
                        type, update.guid, pending_character_guid_)) {
                  return;
                }
                const auto resolution =
                    AccountData::Get().ResolveServerDownload(type,
                                                             update.time);
                if (!resolution.had_pending_request ||
                    !resolution.should_apply_payload) {
                  return;
                }
                auto decompressed = AccountData::Decompress(
                    update.compressed_data, update.decompressed_size);
                if (account_data_payload_consumer_) {
                  account_data_payload_consumer_(type, update.time,
                                                 decompressed);
                } else {
                  ApplyAccountDataPayload(*this, type, update.time,
                                          decompressed, binding_profiles_,
                                          &macro_catalog_);
                }
              },
          .client_cache_version =
              [this](const std::uint32_t version) {
                if (client_cache_version_callback_) {
                  client_cache_version_callback_(version);
                }
              },
      });
  character_.BindWorldPacketHandlers(packet_dispatcher_);
  BindDecomposedPacketHandlers();

}

WorldSession::~WorldSession() {
  SetOnMovementActivatedCallback(nullptr);
  spell_cast_runtime_.SetSendCastSpell({});
  AuraTracker::Get().Reset();
  lifetime_token_.reset();
}

const data::dbc::DbcLoader *WorldSession::GetDbcLoader() const {
  return dbc_;
}

DanceStudioSystem &WorldSession::dance_studio() {
  return *dance_studio_;
}

const DanceStudioSystem &WorldSession::dance_studio() const {
  return *dance_studio_;
}

void WorldSession::SyncTrackedGroupMemberSnapshot(
    const WorldObject &object) {
  if (object.IsPlayer() &&
      group_.GetMember(object.GetGuid()) != nullptr) {
    party_stats_.CaptureLiveMemberSnapshot(
        *this, static_cast<const CGUnit_C &>(object));
  }
}

bool WorldSession::IsActiveArenaBattlefield() const {

  return BattlefieldInfo::Get().GetActiveBGType() == kArenaMapType;
}

void WorldSession::SyncArenaOpponentSnapshot(
    const WorldObject &object, const bool is_create) {
  if (object.IsPlayer()) {
    const auto &player = static_cast<const CGPlayer_C &>(object);
    const auto raw_guid = object.GetGuid().GetRawValue();

    const auto player_arena_faction = static_cast<std::uint8_t>(
        player.GetUInt32(PLAYER_BYTES_3) >> kPlayerArenaFactionByteShift);
    if (is_create && IsActiveArenaBattlefield() &&
        player_arena_faction !=
            BattlefieldInfo::Get().GetBattlefieldArenaFactionRaw() &&
        !battleground_.FindArenaOpponentSlot(raw_guid).has_value()) {
      for (std::size_t slot = 0; slot < kMaxArenaOpponents; ++slot) {
        const auto candidate = battleground_.GetArenaOpponent(slot);
        if (!candidate.guid.IsEmpty() || !candidate.pet_guid.IsEmpty()) {
          continue;
        }

        ArenaOpponentSlot opponent{};
        opponent.guid = object.GetGuid();
        battleground_.SetArenaOpponent(objects(), slot, opponent);
        break;
      }
    }

    const auto &unit = static_cast<const CGUnit_C &>(player);
    const auto tooltip = unit.BuildTooltipInfo(*this);
    std::vector<std::uint32_t> aura_spell_ids;
    aura_spell_ids.reserve(tooltip.GetAuraCount());
    for (const auto spell_id : tooltip.aura_spell_ids) {
      if (spell_id == 0) {
        break;
      }
      aura_spell_ids.push_back(spell_id);
    }

    battleground_.SetArenaOpponentAuraSnapshot(
        raw_guid, std::move(aura_spell_ids));
    battleground_.SetArenaOpponentPvpFlag(
        raw_guid, unit.State().IsPvP());
    battleground_.SetArenaOpponentVehicleSeat(
        raw_guid, tooltip.vehicle_seat_spell_id);
  }

  if (!IsActiveArenaBattlefield() || !object.IsUnit()) {
    return;
  }

  const auto &unit = static_cast<const CGUnit_C &>(object);
  if (const auto owner_slot = battleground_.FindArenaOpponentSlot(
          object.GetGuid().GetRawValue());
      owner_slot.has_value()) {
    const auto controlled_guid = unit.State().GetPrimaryControlledUnitGUID();
    if (!controlled_guid.IsEmpty() &&
        battleground_.GetArenaOpponent(*owner_slot).pet_guid !=
            controlled_guid) {
      battleground_.SetArenaOpponentPet(
          objects(), *owner_slot, controlled_guid);
    }
  }

  const auto owner_guid = ResolveArenaOpponentPetOwner(unit);
  if (owner_guid.IsEmpty()) {
    return;
  }

  const auto owner_slot =
      battleground_.FindArenaOpponentSlot(owner_guid.GetRawValue());
  if (!owner_slot.has_value()) {
    return;
  }

  const auto tracked_pet_guid =
      battleground_.GetArenaOpponent(*owner_slot).pet_guid;
  if (is_create || tracked_pet_guid != object.GetGuid()) {
    battleground_.SetArenaOpponentPet(
        objects(), *owner_slot, object.GetGuid());
  }

  const auto power_type = unit.State().GetPowerType();
  const auto clamp_power = [](const std::uint32_t value) {
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(
        value, std::numeric_limits<std::uint16_t>::max()));
  };
  battleground_.SetArenaOpponentPetState(
      *owner_slot,
      TrackedControlledUnitStateSlice{
          .controlled_unit_guid = object.GetGuid(),
          .owner_guid = owner_guid,
          .display_id = unit.GetDisplayId(),
          .cur_hp = unit.State().GetHealth(),
          .max_hp = unit.State().GetMaxHealth(),
          .power_type = power_type,
          .cur_power = clamp_power(unit.State().GetPower(power_type)),
          .max_power = clamp_power(unit.State().GetMaxPower(power_type)),

          .name = unit.ResolveRetailName(*this),
      });
}

void WorldSession::HandleTrackedGroupMemberDestroyed(
    const ObjectGuid guid) {
  if (group_.GetMember(guid) == nullptr) {
    return;
  }

  RequestPartyMemberStats(guid);
  const auto slot =
      GroupSystem::Get().FindPartySlotByGuid(guid.GetRawValue());
  if (slot >= 0) {
    ui::game::ScriptEventDispatch::Get().FirePartyMemberDisable(
        static_cast<std::uint8_t>(slot));
  }
}

void WorldSession::RequestPartyMemberStats(const ObjectGuid guid) {
  if (send_fn_ && !guid.IsEmpty()) {
    (void)send_fn_(
        net::wotlk::PacketSender::BuildRequestPartyMemberStats(
            guid.GetRawValue()));
  }
}

void WorldSession::SetSendFn(WorldSendFn send) {
  send_fn_ = std::move(send);
  const auto send_packet = send_fn_;
  latency_tracker_.SetSendFn(
      [send_packet](const net::wotlk::WorldPacket &packet) {
        if (send_packet) {
          (void)send_packet(packet);
        }
      });
  latency_tracker_.SetAutoInterval(kKeepAliveInterval);

  dance_studio_->SetDanceQueryDispatcher(
      [send_packet](const DanceId dance_id) {
        if (!send_packet) {
          return;
        }
        net::wotlk::WorldPacket packet(
            net::wotlk::Opcode::CMSG_DANCE_QUERY);
        packet.AppendU32(dance_id.value);
        (void)send_packet(packet);
      });
  dance_studio_->SetActivePlayerDanceStateProvider(
      [this]() -> std::optional<DancePlaybackState> {
        const auto *active_player = objects().GetActivePlayer();
        return active_player != nullptr
                   ? std::optional(ToDancePlaybackState(
                         active_player->Animation().Dance().Get().IsActive()))
                   : std::nullopt;
      });
  dance_studio_->SetPlayDanceSender(
      [send_packet](const DanceId dance_id,
                    const DanceSequenceId sequence_id) {
        if (send_packet) {
          (void)send_packet(net::wotlk::PacketSender::BuildPlayDance(
              dance_id.value, sequence_id.value));
        }
      });

  const auto spell_callback_lifetime = lifetime_token();
  spell_cast_runtime_.SetSendCastSpell(
      [this, send_packet, spell_callback_lifetime](
          const SpellCastCommand& command) {
        if (spell_callback_lifetime.expired() || !send_packet) {
          return false;
        }

        const net::wotlk::SpellTargets targets{
            .target_mask = command.targets.target_mask,
            .unit_target = command.targets.unit_target,
            .go_target = command.targets.object_target,
            .item_target = command.targets.item_target,
            .src_transport = command.targets.source_transport,
            .src_x = command.targets.source_x,
            .src_y = command.targets.source_y,
            .src_z = command.targets.source_z,
            .dst_transport = command.targets.destination_transport,
            .dst_x = command.targets.destination_x,
            .dst_y = command.targets.destination_y,
            .dst_z = command.targets.destination_z,
            .str_target = command.targets.string_target,
            .trajectory_pitch = command.targets.trajectory_pitch,
            .trajectory_speed = command.targets.trajectory_speed,
        };

        const bool sent = send_packet(
            command.origin == SpellCastOrigin::kPet
                ? net::wotlk::PacketSender::BuildPetCastSpell(
                      command.caster_guid.GetRawValue(), command.cast_count,
                      command.spell_id, command.cast_flags, targets)
                : net::wotlk::PacketSender::BuildCastSpell(
                      command.cast_count, command.spell_id,
                      command.cast_flags, targets));
        if (!sent) {
          return false;
        }
        if (spell_callback_lifetime.expired()) {
          return true;
        }

        if (command.origin == SpellCastOrigin::kPet) {
          spell_book_.StartPetGlobalCooldown(command.spell_id,
                                             command.caster_guid);
        } else {
          spell_book_.StartGlobalCooldown(command.spell_id);
        }

        if ((command.cast_flags &
             net::wotlk::kClientSpellCastFlagHasTrajectory) != 0u) {
          if (auto *player = objects().GetActivePlayer();
              player != nullptr) {
            const auto client_time_ms = CurrentClientTimeMs();
            const auto trajectory_start_ms =
                client_time_ms != 0u
                    ? client_time_ms
                    : core::GameClock::GetTickCount32();
            player->Casts().BeginDelayedMissileTrajectory(
                command.spell_id, trajectory_start_ms);
          }
        }
        return true;
      });
}

void WorldSession::SetClientTimeFn(
    std::function<std::uint32_t()> clock) {
  client_time_fn_ = clock;
  time_sync_.SetClientTimeFn(clock);
  runes_.SetClientTimeFn(clock);
  latency_tracker_.SetClockFn(std::move(clock));
}

bool WorldSession::BeginLogin(const std::uint64_t character_guid) {
  pending_character_guid_ = character_guid;
  state_ = WorldState::kLoggingIn;
  return Send(
      net::wotlk::PacketSender::BuildPlayerLogin(character_guid));
}

bool WorldSession::AdoptLoginVerifyWorld(
    const std::uint64_t character_guid,
    const LoginVerifyWorld &verify) {
  if (character_guid == 0 ||
      state_ != WorldState::kDisconnected ||
      has_current_map_) {
    return false;
  }

  pending_character_guid_ = character_guid;
  state_ = WorldState::kLoggingIn;
  if (ApplyLoginVerifyWorld(verify)) {
    return true;
  }

  pending_character_guid_ = 0;
  state_ = WorldState::kDisconnected;
  return false;
}

void WorldSession::StartBotDetectedCountdown(
    const std::uint32_t client_time_ms) {
  bot_detected_world_active_ = true;
  bot_detected_enabled_ = true;
  bot_detected_last_tick_ms_ = client_time_ms;
  bot_detected_countdown_ = kBotDetectedInitialCountdown;
}

void WorldSession::StartBotDetectedCountdownFromInit(
    const std::uint32_t enter_world_init_time_ms,
    const std::uint32_t client_time_ms) {
  StartBotDetectedCountdown(enter_world_init_time_ms);

  if (client_time_ms <= enter_world_init_time_ms) {
    return;
  }

  const auto elapsed_ticks =
      (client_time_ms - enter_world_init_time_ms) / 1000u;
  if (elapsed_ticks == 0) {
    return;
  }

  bot_detected_last_tick_ms_ =
      enter_world_init_time_ms + elapsed_ticks * 1000u;
  if (elapsed_ticks >=
      static_cast<std::uint32_t>(bot_detected_countdown_)) {
    bot_detected_countdown_ = 1;
    return;
  }

  bot_detected_countdown_ -= static_cast<std::int32_t>(elapsed_ticks);
}

void WorldSession::ResetBotDetectedCountdown() {
  bot_detected_world_active_ = false;
  bot_detected_enabled_ = false;
  bot_detected_last_tick_ms_ = 0;
  bot_detected_countdown_ = 0;
}

void WorldSession::TrySendLegacyTokenSeedFollowUp() {
  if (!bot_detected_world_active_ || !bot_detected_enabled_ ||
      bot_detected_countdown_ <= 0 ||
      bot_detected_countdown_ > kLegacyTokenSeedFollowUpWindow ||
      !consume_legacy_token_seed_verification_ ||
      !consume_legacy_token_seed_verification_()) {
    return;
  }

  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_BOT_DETECTED2);
  packet.AppendU32(kLegacyTokenSeedFollowUpValue);
  Send(packet);
}

bool WorldSession::HasAvailableShapeshiftForms() const {
  if (objects().GetLocalPlayerTyped() == nullptr) {
    return false;
  }

  const auto *dbc = GetDbcLoader();
  for (const auto spell_id : spell_book_.spells()) {
    if (spell_id == 0) {
      continue;
    }

    if (const auto query = SpellQueryBridge::Get().Query(spell_id);
        query.has_value() &&
        HasAuraType(query->effectApplyAura, kShapeshiftAuraType)) {
      return true;
    }

    if (dbc == nullptr) {
      continue;
    }

    if (const auto *spell = dbc->spell().LookupEntry(spell_id);
        spell != nullptr &&
        HasAuraType(spell->effect_apply_aura, kShapeshiftAuraType)) {
      return true;
    }
  }

  return false;
}

void WorldSession::TickBotDetected(
    const std::uint32_t client_time_ms) {
  TrySendLegacyTokenSeedFollowUp();

  if (!bot_detected_world_active_ || !bot_detected_enabled_ ||
      client_time_ms - bot_detected_last_tick_ms_ < 1000u) {
    return;
  }

  bot_detected_last_tick_ms_ = client_time_ms;
  if (--bot_detected_countdown_ > 0) {
    return;
  }

  bot_detected_enabled_ = false;
  if (!bot_detected_probe_fn_ || !build_bot_detected_digest_) {
    return;
  }

  const auto probe = bot_detected_probe_fn_();
  if (!probe.has_value()) {
    return;
  }

  const auto digest = build_bot_detected_digest_(*probe);
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_BOT_DETECTED);
  packet.AppendBytes(probe->data(), probe->size());
  packet.AppendBytes(digest.data(), digest.size());
  Send(packet);
}

bool WorldSession::HandlePacket(const net::wotlk::WorldPacket &pkt) {

  if (net::PacketLog::Get().IsEnabled()) {
    net::PacketLog::Get().LogPacket(net::PacketDirection::kServerToClient,
                                    static_cast<std::uint16_t>(pkt.opcode), pkt.payload.data(),
                                    pkt.payload.size());
  }

  using Op = net::wotlk::Opcode;
  const auto op = pkt.GetOpcode();

  const bool world_session_owns_map_lifecycle =
      op == Op::SMSG_UPDATE_OBJECT ||
      op == Op::SMSG_COMPRESSED_UPDATE_OBJECT ||
      op == Op::SMSG_DESTROY_OBJECT;
  if (!world_session_owns_map_lifecycle &&
      packet_dispatcher_.HasHandler(op)) {
    return packet_dispatcher_.Dispatch(pkt);
  }

  switch (op) {
  case Op::SMSG_LOGIN_VERIFY_WORLD:
    HandleLoginVerifyWorld(pkt);
    return true;

  case Op::SMSG_UPDATE_OBJECT:
    return HandleUpdateObject(pkt);

  case Op::SMSG_COMPRESSED_UPDATE_OBJECT:
    return HandleCompressedUpdateObject(pkt);

  case Op::SMSG_DESTROY_OBJECT:
    HandleDestroyObject(pkt);
    return true;

  case Op::SMSG_TIME_SYNC_REQ:
    HandleTimeSyncReq(pkt);
    return true;

  case Op::SMSG_PONG: {
    if (pkt.payload.size() >= sizeof(std::uint32_t)) {
      std::uint32_t sequence = 0;
      std::memcpy(&sequence, pkt.payload.data(), sizeof(sequence));
      latency_tracker_.HandlePong(sequence);
    }
    return true;
  }

  case Op::SMSG_FORCE_WALK_SPEED_CHANGE:
    HandleForceSpeedChange(pkt, kSpeedWalk);
    return true;
  case Op::SMSG_FORCE_RUN_SPEED_CHANGE:
    HandleForceSpeedChange(pkt, kSpeedRun);
    return true;
  case Op::SMSG_FORCE_RUN_BACK_SPEED_CHANGE:
    HandleForceSpeedChange(pkt, kSpeedRunBack);
    return true;
  case Op::SMSG_FORCE_SWIM_SPEED_CHANGE:
    HandleForceSpeedChange(pkt, kSpeedSwim);
    return true;
  case Op::SMSG_FORCE_FLIGHT_SPEED_CHANGE:
    HandleForceSpeedChange(pkt, kSpeedFlight);
    return true;
  case Op::SMSG_FORCE_TURN_RATE_CHANGE:
    HandleForceSpeedChange(pkt, kSpeedTurnRate);
    return true;

  case Op::MSG_MOVE_START_FORWARD:
  case Op::MSG_MOVE_START_BACKWARD:
  case Op::MSG_MOVE_STOP:
  case Op::MSG_MOVE_START_STRAFE_LEFT:
  case Op::MSG_MOVE_START_STRAFE_RIGHT:
  case Op::MSG_MOVE_STOP_STRAFE:
  case Op::MSG_MOVE_JUMP:
  case Op::MSG_MOVE_START_TURN_LEFT:
  case Op::MSG_MOVE_START_TURN_RIGHT:
  case Op::MSG_MOVE_STOP_TURN:
  case Op::MSG_MOVE_SET_FACING:
  case Op::MSG_MOVE_HEARTBEAT:
  case Op::MSG_MOVE_FALL_LAND:
  case Op::MSG_MOVE_START_SWIM:
  case Op::MSG_MOVE_STOP_SWIM:
  case Op::MSG_MOVE_START_ASCEND:
  case Op::MSG_MOVE_STOP_ASCEND:
  case Op::MSG_MOVE_START_DESCEND:
  case Op::MSG_MOVE_START_SWIM_CHEAT:
  case Op::MSG_MOVE_STOP_SWIM_CHEAT:
    HandleMovement(pkt);
    return true;

  case Op::MSG_MOVE_TOGGLE_COLLISION_CHEAT:
    return true;

  case Op::SMSG_MESSAGECHAT:
    HandleChatMessage(pkt, false);
    return true;
  case Op::SMSG_GM_MESSAGECHAT:
    HandleChatMessage(pkt, true);
    return true;
  case Op::SMSG_CHANNEL_NOTIFY:
    HandleChannelNotify(pkt);
    return true;

  case Op::SMSG_INITIAL_SPELLS:
    HandleInitialSpells(pkt);
    return true;
  case Op::SMSG_LEARNED_SPELL:
    HandleLearnedSpell(pkt);
    return true;
  case Op::SMSG_REMOVED_SPELL:
    HandleRemovedSpell(pkt);
    return true;
  case Op::SMSG_SUPERCEDED_SPELL:
    HandleSupercededSpell(pkt);
    return true;
  case Op::SMSG_SPELL_COOLDOWN:
    HandleSpellCooldown(pkt);
    return true;
  case Op::SMSG_CAST_FAILED:
    HandleCastFailed(pkt);
    return true;
  case Op::SMSG_SPELL_START:
    HandleSpellStart(pkt);
    return true;
  case Op::SMSG_SPELL_GO:
    HandleSpellGo(pkt);
    return true;

  case Op::SMSG_ACTION_BUTTONS:
    HandleActionButtons(pkt);
    return true;

  case Op::SMSG_ITEM_PUSH_RESULT:
    if (auto result = HandleItemPushResultPacket(inventory_, pkt)) {
      QueueItemPush(std::move(*result));
    }
    return true;
  case Op::SMSG_INVENTORY_CHANGE_FAILURE:
    if (const auto failure =
            HandleInventoryChangeFailurePacket(inventory_, pkt)) {
      const int msg_id = InventoryResultCodeToSystemMessageId(
          static_cast<std::uint32_t>(failure->result));
      if (msg_id == 16) {
        (void)DisplayBagFamilyText(*this, failure->bag_type_subclass);
      }

      if (const auto& readable = item_interactions().readable();
          readable.has_value() && !failure->item1_guid.IsEmpty() &&
          readable->item == failure->item1_guid) {
        CloseReadableObjectInteraction(*this);
        if (auto* player = objects().GetActivePlayer(); player != nullptr) {
          player->Animation().RefreshSelectedStandAnimation(*this, 0u, ~0u);
        }
      }
    }
    return true;

  case Op::SMSG_CONTACT_LIST:
    HandleContactList(pkt);
    return true;
  case Op::SMSG_FRIEND_STATUS:
    HandleFriendStatus(pkt);
    return true;

  case Op::SMSG_GROUP_LIST:
    HandleGroupList(pkt);
    return true;
  case Op::SMSG_GROUP_INVITE:
    HandleGroupInvite(pkt);
    return true;
  case Op::SMSG_GROUP_DECLINE:
    HandleGroupDecline(pkt);
    return true;
  case Op::SMSG_GROUP_SET_LEADER:
    HandleGroupSetLeader(pkt);
    return true;
  case Op::SMSG_PARTY_COMMAND_RESULT:
    HandlePartyCommandResult(pkt);
    return true;

  case Op::SMSG_TALENTS_INFO:
    HandleTalentsInfo(pkt);
    return true;

  case Op::SMSG_ATTACKSTART:
    HandleAttackStart(pkt);
    return true;
  case Op::SMSG_ATTACKSTOP:
    HandleAttackStop(pkt);
    return true;
  case Op::SMSG_SPELLNONMELEEDAMAGELOG:
    HandleSpellNonMeleeDamageLog(pkt);
    return true;
  case Op::SMSG_SPELLHEALLOG:
    HandleSpellHealLog(pkt);
    return true;
  case Op::SMSG_SPELLENERGIZELOG:
    HandleSpellEnergizeLog(pkt);
    return true;
  case Op::SMSG_SEND_ALL_COMBAT_LOG:
    HandleSendAllCombatLog(pkt);
    return true;
  case Op::SMSG_LOG_XPGAIN:
    HandleLogXpGain(pkt);
    return true;
  case Op::SMSG_ATTACKERSTATEUPDATE:
    HandleAttackerStateUpdate(pkt);
    return true;
  case Op::SMSG_PERIODICAURALOG:
    HandlePeriodicAuraLog(pkt);
    return true;

  case Op::SMSG_GOSSIP_MESSAGE:
    HandleGossipMessagePacket(
        gossip_, query_cache_, interaction_,

        [this](const std::uint64_t) { ui::game::CloseGossipInteraction(*this); },
        [this]() {
          return ui::game::detail::PrepareCurrentGossipText(*this);
        }, pkt);
    return true;
  case Op::SMSG_NPC_TEXT_UPDATE:
    HandleNpcTextUpdatePacket(
        gossip_, query_cache_,
        [this]() {
          return ui::game::detail::PrepareCurrentGossipText(*this);
        }, pkt);
    return true;
  case Op::SMSG_TRAINER_LIST:
    HandleTrainerListPacket(
        gossip_, [this]() { Trainer_UpdateGreetingText(*this); }, pkt);
    return true;
  case Op::SMSG_LIST_INVENTORY:
    HandleMerchantListPacket(objects(), gossip_, query_cache_, pkt);
    return true;

  case Op::SMSG_GUILD_QUERY_RESPONSE:
    HandleGuildQueryResponse(pkt);
    return true;
  case Op::SMSG_GUILD_ROSTER:
    HandleGuildRoster(pkt);
    return true;
  case Op::SMSG_GUILD_EVENT:
    HandleGuildEvent(pkt);
    return true;
  case Op::SMSG_GUILD_COMMAND_RESULT:
    HandleGuildCommandResult(pkt);
    return true;
  case Op::SMSG_GUILD_INVITE:
    HandleGuildInvite(pkt);
    return true;
  case Op::MSG_GUILD_PERMISSIONS:
    HandleGuildPermissions(pkt);
    return true;
  case Op::MSG_GUILD_EVENT_LOG_QUERY:
    HandleGuildEventLogQuery(pkt);
    return true;

  case Op::SMSG_TRADE_STATUS:
    HandleTradeStatusPacket(
        trade_, interaction_, social_, inventory_replica_, held_cursor(),
        map_runtime_.objects(), query_cache_, spell_visual_.cinematic_active(),
        gossip_.has_gossip() || gossip_.merchant().active() ||
            gossip_.has_trainer() || GuildSystem::Get().IsBankFrameOpen() ||
            bank_npc_guid() != 0 || mail_.mailbox_guid() != 0 ||
            auction_.enabled() || taxi_.IsTaxiMapOpen() ||
            petition_.guild_registrar_guid() != 0 ||
            petition_.petition_vendor_guid() != 0 ||
            petition_.tabard_vendor_guid() != 0 ||
            pet_.stable_list().npc_guid.GetRawValue() != 0,
        pkt);
    return true;
  case Op::SMSG_TRADE_STATUS_EXTENDED:
    HandleTradeExtendedPacket(trade_, interaction_, pkt);
    return true;

  case Op::SMSG_MAIL_LIST_RESULT:
    HandleMailListPacket(
        mail_, social_, query_cache_,
        [this](const net::wotlk::WorldPacket& packet) {
          return Send(packet);
        },
        pkt);
    return true;
  case Op::SMSG_SEND_MAIL_RESULT:
    HandleSendMailResultPacket(
        mail_, map_runtime_.objects().GetLocalPlayer() != nullptr,
        [this](const net::wotlk::WorldPacket& packet) {
          return Send(packet);
        },
        pkt);
    return true;
  case Op::SMSG_RECEIVED_MAIL:
    HandleReceivedMailPacket(
        mail_,
        [this](const net::wotlk::WorldPacket& packet) {
          return Send(packet);
        },
        pkt);
    return true;
  case Op::SMSG_SHOW_MAILBOX:
    HandleShowMailboxPacket(mail_, pkt);
    return true;
  case Op::MSG_QUERY_NEXT_MAIL_TIME:
    HandleNextMailTimePacket(
        mail_,
        [this](const net::wotlk::WorldPacket& packet) {
          return Send(packet);
        },
        pkt);
    return true;

  case Op::MSG_AUCTION_HELLO:
    auction_packets_.HandleHello(pkt);
    return true;
  case Op::SMSG_AUCTION_LIST_RESULT:
    auction_packets_.HandleListResult(pkt);
    return true;
  case Op::SMSG_AUCTION_OWNER_LIST_RESULT:
    auction_packets_.HandleOwnerListResult(pkt);
    return true;
  case Op::SMSG_AUCTION_BIDDER_LIST_RESULT:
    auction_packets_.HandleBidderListResult(pkt);
    return true;
  case Op::SMSG_AUCTION_COMMAND_RESULT:
    auction_packets_.HandleCommandResult(pkt);
    return true;
  case Op::SMSG_AUCTION_BIDDER_NOTIFICATION:
    auction_packets_.HandleBidderNotification(pkt);
    return true;
  case Op::SMSG_AUCTION_OWNER_NOTIFICATION:
    auction_packets_.HandleOwnerNotification(pkt);
    return true;

  case Op::SMSG_LFG_JOIN_RESULT:
    HandleLfgJoinResult(pkt);
    return true;
  case Op::SMSG_LFG_QUEUE_STATUS:
    HandleLfgQueueStatus(pkt);
    return true;
  case Op::SMSG_LFG_UPDATE_PLAYER:
    HandleLfgUpdatePlayer(pkt);
    return true;
  case Op::SMSG_LFG_UPDATE_PARTY:
    HandleLfgUpdateParty(pkt);
    return true;
  case Op::SMSG_LFG_PROPOSAL_UPDATE:
    HandleLfgProposalUpdate(pkt);
    return true;
  case Op::SMSG_LFG_ROLE_CHECK_UPDATE:
    HandleLfgRoleCheckUpdate(pkt);
    return true;
  case Op::SMSG_LFG_BOOT_PROPOSAL_UPDATE:
    HandleLfgBootProposalUpdate(pkt);
    return true;
  case Op::SMSG_LFG_PLAYER_REWARD:
    HandleLfgPlayerReward(pkt);
    return true;
  case Op::SMSG_LFG_TELEPORT_DENIED:
    HandleLfgTeleportDenied(pkt);
    return true;
  case Op::SMSG_LFG_OFFER_CONTINUE:
    HandleLfgOfferContinue(pkt);
    return true;

  case Op::SMSG_AURA_UPDATE:
    HandleAuraUpdate(pkt);
    return true;
  case Op::SMSG_AURA_UPDATE_ALL:
    HandleAuraUpdateAll(pkt);
    return true;
  case Op::SMSG_SET_FLAT_SPELL_MODIFIER:
    HandleSetFlatSpellModifier(pkt);
    return true;
  case Op::SMSG_SET_PCT_SPELL_MODIFIER:
    HandleSetPctSpellModifier(pkt);
    return true;
  case Op::SMSG_COOLDOWN_EVENT:
    HandleCooldownEvent(pkt);
    return true;
  case Op::SMSG_CLEAR_COOLDOWN:
    HandleClearCooldown(pkt);
    return true;
  case Op::SMSG_SEND_UNLEARN_SPELLS:
    HandleSendUnlearnSpells(pkt);
    return true;
  case Op::SMSG_SPELL_FAILURE:
    HandleSpellFailure(pkt);
    return true;
  case Op::SMSG_SPELL_FAILED_OTHER:
    HandleSpellFailedOther(pkt);
    return true;
  case Op::SMSG_SPELL_DELAYED:
    HandleSpellDelayed(pkt);
    return true;
  case Op::MSG_CHANNEL_START:
    HandleChannelStart(pkt);
    return true;
  case Op::MSG_CHANNEL_UPDATE:
    HandleChannelUpdate(pkt);
    return true;

  case Op::SMSG_CANCEL_COMBAT:
    HandleCancelCombat(pkt);
    return true;
  case Op::SMSG_AI_REACTION:
    HandleAiReaction(pkt);
    return true;
  case Op::SMSG_PARTYKILLLOG:
    HandlePartyKillLog(pkt);
    return true;
  case Op::SMSG_ATTACKSWING_NOTINRANGE:
    HandleAttackSwingError(pkt, AttackSwingError::kNotInRange);
    return true;
  case Op::SMSG_ATTACKSWING_BADFACING:
    HandleAttackSwingError(pkt, AttackSwingError::kBadFacing);
    return true;
  case Op::SMSG_ATTACKSWING_DEADTARGET:
    HandleAttackSwingError(pkt, AttackSwingError::kDeadTarget);
    return true;
  case Op::SMSG_ATTACKSWING_CANT_ATTACK:
    HandleAttackSwingError(pkt, AttackSwingError::kCantAttack);
    return true;
  case Op::SMSG_HEALTH_UPDATE:
    HandleHealthUpdate(pkt);
    return true;
  case Op::SMSG_POWER_UPDATE:
    HandlePowerUpdate(pkt);
    return true;
  case Op::SMSG_THREAT_UPDATE:
    HandleThreatUpdate(pkt);
    return true;
  case Op::SMSG_HIGHEST_THREAT_UPDATE:
    HandleHighestThreatUpdate(pkt);
    return true;
  case Op::SMSG_THREAT_REMOVE:
    HandleThreatRemove(pkt);
    return true;
  case Op::SMSG_THREAT_CLEAR:
    HandleThreatClear(pkt);
    return true;
  case Op::SMSG_LEVELUP_INFO:
    HandleLevelUpInfo(pkt);
    return true;
  case Op::SMSG_ENVIRONMENTAL_DAMAGE_LOG:
    HandleEnvironmentalDamageLog(pkt);
    return true;

  case Op::SMSG_INIT_WORLD_STATES:
    HandleInitWorldStates(pkt);
    return true;
  case Op::SMSG_UPDATE_WORLD_STATE:
    HandleUpdateWorldState(pkt);
    return true;

  case Op::SMSG_BATTLEFIELD_STATUS:
    HandleBattlefieldStatus(pkt);
    return true;
  case Op::SMSG_BATTLEFIELD_LIST:
    HandleBattlefieldList(pkt);
    return true;
  case Op::MSG_BATTLEGROUND_PLAYER_POSITIONS:
    HandlePlayerPositions(pkt);
    return true;
  case Op::SMSG_BATTLEGROUND_PLAYER_JOINED:
    HandlePlayerJoined(pkt);
    return true;
  case Op::SMSG_BATTLEGROUND_PLAYER_LEFT:
    HandlePlayerLeft(pkt);
    return true;
  case Op::MSG_PVP_LOG_DATA:
    HandlePvpLogData(pkt);
    return true;
  case Op::SMSG_PVP_CREDIT:
    HandlePvpCredit(pkt);
    return true;
  case Op::SMSG_ARENA_TEAM_ROSTER:
    HandleArenaTeamRoster(pkt);
    return true;
  case Op::SMSG_ARENA_TEAM_COMMAND_RESULT:
    HandleArenaTeamCommandResult(pkt);
    return true;
  case Op::SMSG_ARENA_TEAM_STATS:
    HandleArenaTeamStats(pkt);
    return true;
  case Op::SMSG_ARENA_TEAM_INVITE:
    HandleArenaTeamInvite(pkt);
    return true;

  case Op::SMSG_NAME_QUERY_RESPONSE:
    HandleNameQueryResponse(pkt);
    return true;
  case Op::SMSG_CREATURE_QUERY_RESPONSE:
    HandleCreatureQueryResponse(pkt);
    return true;
  case Op::SMSG_GAMEOBJECT_QUERY_RESPONSE:
    HandleGameObjectQueryResponse(pkt);
    return true;
  case Op::SMSG_ITEM_QUERY_SINGLE_RESPONSE:
    HandleItemQuerySingleResponse(pkt);
    return true;

  case Op::SMSG_MONSTER_MOVE:
    HandleMonsterMove(pkt);
    return true;
  case Op::SMSG_MONSTER_MOVE_TRANSPORT:
    HandleMonsterMoveTransport(pkt);
    return true;
  case Op::SMSG_COMPRESSED_MOVES:
    HandleCompressedMoves(pkt);
    return true;

  case Op::SMSG_PARTY_MEMBER_STATS:
    HandlePartyMemberStats(pkt);
    return true;
  case Op::SMSG_PARTY_MEMBER_STATS_FULL:
    HandlePartyMemberStatsFull(pkt);
    return true;

  case Op::SMSG_BUY_ITEM:
    HandleMerchantBuyPacket(gossip_.merchant(), pkt);
    return true;
  case Op::SMSG_SELL_ITEM:
    HandleMerchantSellPacket(map_runtime_.objects(), pkt);
    return true;
  case Op::SMSG_BUY_FAILED:
    HandleMerchantBuyFailurePacket(gossip_.merchant(), pkt);
    return true;
  case Op::SMSG_LOOT_START_ROLL:
    HandleLootStartRollPacket(
        loot_, item_definitions_, query_cache_,
        has_current_map() ? current_map_id() : map_runtime_.objects().GetMapId(),
        core::GameClock::GetTickCount32(), pkt);
    return true;
  case Op::SMSG_LOOT_ROLL:
    HandleLootRollPacket(
        map_runtime_.objects(), query_cache_, Localization::Get(),
        ui::game::CVarSystem::Instance(), GetDbcLoader(), pkt);
    return true;
  case Op::SMSG_LOOT_ALL_PASSED:
    HandleLootAllPassedPacket(
        loot_, map_runtime_.objects(), query_cache_, Localization::Get(),
        ui::game::CVarSystem::Instance(), GetDbcLoader(), pkt);
    return true;

  case Op::SMSG_FEATURE_SYSTEM_STATUS:
    HandleFeatureSystemStatus(pkt);
    return true;
  case Op::MSG_MOVE_TELEPORT:
    HandleMoveTeleport(pkt);
    return true;
  case Op::SMSG_MOVE_WATER_WALK:
    HandleMoveWaterWalk(pkt);
    return true;
  case Op::SMSG_MOVE_LAND_WALK:
    HandleMoveLandWalk(pkt);
    return true;
  case Op::SMSG_MOVE_FEATHER_FALL:
    HandleMoveFeatherFall(pkt);
    return true;
  case Op::SMSG_MOVE_NORMAL_FALL:
    HandleMoveNormalFall(pkt);
    return true;
  case Op::SMSG_LOGOUT_CANCEL_ACK:
    HandleLogoutCancelAck(pkt);
    return true;
  case Op::MSG_MOVE_SET_RUN_SPEED:
    HandleMoveSetRunSpeed(pkt);
    return true;
  case Op::MSG_MOVE_SET_RUN_BACK_SPEED:
    HandleMoveSetRunBackSpeed(pkt);
    return true;
  case Op::MSG_MOVE_SET_WALK_SPEED:
    HandleMoveSetWalkSpeed(pkt);
    return true;
  case Op::MSG_MOVE_SET_SWIM_SPEED:
    HandleMoveSetSwimSpeed(pkt);
    return true;
  case Op::MSG_MOVE_SET_FLIGHT_SPEED:
    HandleMoveSetFlightSpeed(pkt);
    return true;

  case Op::SMSG_CONVERT_RUNE:
    HandleConvertRune(pkt);
    return true;
  case Op::SMSG_RESYNC_RUNES:
    HandleResyncRunes(pkt);
    return true;
  case Op::SMSG_ADD_RUNE_POWER:
    HandleAddRunePower(pkt);
    return true;

  case Op::SMSG_PLAY_SPELL_VISUAL:
    HandlePlaySpellVisual(pkt);
    return true;
  case Op::SMSG_PLAY_SPELL_IMPACT:
    HandlePlaySpellImpact(pkt);
    return true;
  case Op::SMSG_TRIGGER_CINEMATIC:
    HandleTriggerCinematic(pkt);
    return true;
  case Op::SMSG_TRIGGER_MOVIE:
    HandleTriggerMovie(pkt);
    return true;

  case Op::SMSG_SUMMON_REQUEST:
    HandleSummonRequest(pkt);
    return true;
  case Op::MSG_RAID_TARGET_UPDATE:
    HandleRaidTargetUpdate(pkt);
    return true;
  case Op::MSG_RAID_READY_CHECK:
    HandleRaidReadyCheck(pkt);
    return true;
  case Op::MSG_RAID_READY_CHECK_CONFIRM:
    HandleRaidReadyCheckConfirm(pkt);
    return true;
  case Op::MSG_RAID_READY_CHECK_FINISHED:
    HandleRaidReadyCheckFinished(pkt);
    return true;
  case Op::MSG_PARTY_ASSIGNMENT:
    HandlePartyAssignment(pkt);
    return true;

  case Op::SMSG_MOVE_SET_HOVER:
    HandleMoveSetHover(pkt);
    return true;
  case Op::SMSG_MOVE_UNSET_HOVER:
    HandleMoveUnsetHover(pkt);
    return true;
  case Op::SMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY:
    HandleMoveSetCanSwimFlyTransition(pkt);
    return true;
  case Op::SMSG_MOVE_UNSET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY:
    HandleMoveUnsetCanSwimFlyTransition(pkt);
    return true;
  case Op::SMSG_SPLINE_SET_RUN_SPEED:
    HandleSplineSetRunSpeed(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_ROOT:
    HandleSplineMoveRoot(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_UNROOT:
    HandleSplineMoveUnroot(pkt);
    return true;

  case Op::SMSG_SPELLDISPELLOG:
    HandleSpellDispelLog(pkt);
    return true;
  case Op::SMSG_SPELLSTEALLOG:
    HandleSpellStealLog(pkt);
    return true;
  case Op::SMSG_SPELLDAMAGESHIELD:
    HandleSpellDamageShield(pkt);
    return true;
  case Op::SMSG_SPELLLOGMISS:
    HandleSpellLogMiss(pkt);
    return true;
  case Op::SMSG_SPELLINSTAKILLLOG:
    HandleSpellInstaKillLog(pkt);
    return true;
  case Op::SMSG_SPELLORDAMAGE_IMMUNE:
    HandleSpellOrDamageImmune(pkt);
    return true;
  case Op::SMSG_DISPEL_FAILED:
    HandleDispelFailed(pkt);
    return true;
  case Op::SMSG_MODIFY_COOLDOWN:
    HandleModifyCooldown(pkt);
    return true;
  case Op::SMSG_SPELLLOGEXECUTE:
    HandleSpellLogExecute(pkt);
    return true;

  case Op::SMSG_GUILD_BANK_LIST:
    HandleGuildBankList(pkt);
    return true;
  case Op::MSG_GUILD_BANK_LOG_QUERY:
    HandleGuildBankLogQuery(pkt);
    return true;
  case Op::MSG_GUILD_BANK_MONEY_WITHDRAWN:
    HandleGuildBankMoneyWithdrawn(pkt);
    return true;
  case Op::MSG_QUERY_GUILD_BANK_TEXT:
    HandleQueryGuildBankText(pkt);
    return true;

  case Op::SMSG_TRAINER_BUY_SUCCEEDED:
    HandleTrainerBuySucceededPacket(petition_, pkt);
    return true;
  case Op::SMSG_TRAINER_BUY_FAILED:
    HandleTrainerBuyFailedPacket(petition_, pkt);
    return true;

  case Op::SMSG_PETITION_SHOWLIST:
    HandlePetitionShowList(pkt);
    return true;
  case Op::SMSG_PETITION_SHOW_SIGNATURES:
    HandlePetitionShowSignatures(pkt);
    return true;
  case Op::SMSG_PETITION_SIGN_RESULTS:
    HandlePetitionSignResults(pkt);
    return true;
  case Op::SMSG_PETITION_QUERY_RESPONSE:
    HandlePetitionQueryResponse(pkt);
    return true;
  case Op::SMSG_TURN_IN_PETITION_RESULTS:
    HandleTurnInPetitionResults(pkt);
    return true;

  case Op::SMSG_ENCHANTMENTLOG:
    HandleEnchantmentLogPacket(
        combat_log_, item_definitions_, query_cache_, pkt);
    return true;
  case Op::SMSG_ITEM_ENCHANT_TIME_UPDATE:
    HandleItemEnchantTimePacket(map_runtime_.objects(), pkt);
    return true;
  case Op::SMSG_ITEM_REFUND_INFO_RESPONSE:
    HandleItemRefundInfoPacket(item_interactions_, pkt);
    return true;
  case Op::SMSG_ITEM_REFUND_RESULT:
    HandleItemRefundResultPacket(
        item_interactions_, map_runtime_.objects(), query_cache_,
        Localization::Get(), pkt);
    return true;
  case Op::SMSG_ITEM_CHARGES_UPDATE:
    if (HandleItemChargesPacket(map_runtime_.objects(), inventory_bridge_, pkt)) {
      QueueEquipmentPresentation();
    }
    return true;
  case Op::SMSG_EQUIPMENT_SET_USE_RESULT:
    HandleEquipmentSetUseResultPacket(equipment_, Localization::Get(), pkt);
    return true;

  case Op::SMSG_ARENA_TEAM_QUERY_RESPONSE:
    HandleArenaTeamQueryResponse(pkt);
    return true;
  case Op::SMSG_ARENA_TEAM_EVENT:
    HandleArenaTeamEvent(pkt);
    return true;

  case Op::SMSG_FORCE_SWIM_BACK_SPEED_CHANGE:
    HandleForceSwimBackSpeedChange(pkt);
    return true;
  case Op::SMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE:
    HandleForceFlightBackSpeedChange(pkt);
    return true;
  case Op::SMSG_FORCE_PITCH_RATE_CHANGE:
    HandleForcePitchRateChange(pkt);
    return true;

  case Op::SMSG_SPLINE_SET_WALK_SPEED:
    HandleSplineSetWalkSpeed(pkt);
    return true;
  case Op::SMSG_SPLINE_SET_SWIM_SPEED:
    HandleSplineSetSwimSpeed(pkt);
    return true;
  case Op::SMSG_SPLINE_SET_FLIGHT_SPEED:
    HandleSplineSetFlightSpeed(pkt);
    return true;
  case Op::SMSG_SPLINE_SET_RUN_BACK_SPEED:
    HandleSplineSetRunBackSpeed(pkt);
    return true;
  case Op::SMSG_SPLINE_SET_SWIM_BACK_SPEED:
    HandleSplineSetSwimBackSpeed(pkt);
    return true;
  case Op::SMSG_SPLINE_SET_TURN_RATE:
    HandleSplineSetTurnRate(pkt);
    return true;
  case Op::SMSG_SPLINE_SET_FLIGHT_BACK_SPEED:
    HandleSplineSetFlightBackSpeed(pkt);
    return true;
  case Op::SMSG_SPLINE_SET_PITCH_RATE:
    HandleSplineSetPitchRate(pkt);
    return true;

  case Op::SMSG_FLIGHT_SPLINE_SYNC:
    HandleFlightSplineSync(pkt);
    return true;
  case Op::MSG_MOVE_TIME_SKIPPED:
    HandleMoveTimeSkipped(pkt);
    return true;
  case Op::MSG_MOVE_SET_PITCH:
    HandleMoveSetPitch(pkt);
    return true;
  case Op::MSG_MOVE_START_PITCH_UP:
    HandleMoveStartPitchUp(pkt);
    return true;
  case Op::MSG_MOVE_START_PITCH_DOWN:
    HandleMoveStartPitchDown(pkt);
    return true;
  case Op::MSG_MOVE_STOP_PITCH:
    HandleMoveStopPitch(pkt);
    return true;

  case Op::SMSG_LOOT_ROLL_WON:
    HandleLootRollWonPacket(
        loot_, map_runtime_.objects(), query_cache_, Localization::Get(),
        ui::game::CVarSystem::Instance(), GetDbcLoader(), pkt);
    return true;
  case Op::SMSG_GROUP_DESTROYED:
    HandleGroupDestroyed(pkt);
    return true;
  case Op::SMSG_GROUP_UNINVITE:
    HandleGroupUninvite(pkt);
    return true;
  case Op::SMSG_REAL_GROUP_UPDATE:
    HandleRealGroupUpdate(pkt);
    return true;
  case Op::SMSG_GROUPACTION_THROTTLED:
    HandleGroupActionThrottled(pkt);
    return true;

  case Op::SMSG_BATTLEFIELD_MGR_ENTRY_INVITE:
    HandleBattlefieldMgrEntryInvite(pkt);
    return true;
  case Op::SMSG_BATTLEFIELD_MGR_ENTERED:
    HandleBattlefieldMgrEntered(pkt);
    return true;
  case Op::SMSG_BATTLEFIELD_MGR_QUEUE_INVITE:
    HandleBattlefieldMgrQueueInvite(pkt);
    return true;
  case Op::SMSG_GROUP_JOINED_BATTLEGROUND:
    HandleGroupJoinedBattleground(pkt);
    return true;

  case Op::SMSG_ITEM_COOLDOWN:
    HandleItemCooldownPacket(
        inventory_replica_, map_runtime_.objects(), query_cache_, spell_book_,
        CooldownTracker::Get(), core::GameClock::GetTickCount32(),
        HasAvailableShapeshiftForms(), pkt);
    return true;
  case Op::SMSG_ITEM_TIME_UPDATE:
    HandleItemTimeUpdatePacket(map_runtime_.objects(), pkt);
    return true;
  case Op::SMSG_BUY_BANK_SLOT_RESULT:
    HandleBuyBankSlotResultPacket(pkt);
    return true;

  case Op::SMSG_EQUIPMENT_SET_SAVED:
    HandleEquipmentSetSavedPacket(equipment_, pkt);
    return true;
  case Op::SMSG_READ_ITEM_OK:
    if (const auto item = decode_read_ok(pkt.payload)) {
      openwow::game::HandleReadItemOk(*this, item->GetRawValue());
    }
    return true;
  case Op::SMSG_READ_ITEM_FAILED:
    if (const auto failure = decode_read_failure(pkt.payload)) {
      openwow::game::HandleReadItemFailed(
          *this, failure->item.GetRawValue(), failure->status, 0);
    }
    return true;
  case Op::SMSG_ITEM_TEXT_QUERY_RESPONSE:
    if (const auto item = HandleItemTextPacket(
            item_interactions_, db_cache_runtime_.persistence(), pkt)) {
      ReloadReadableObjectAfterAsyncDependency(*this, *item);
    }
    return true;
  case Op::SMSG_TOTEM_CREATED:
    HandleTotemCreated(pkt);
    return true;
  case Op::SMSG_RESUME_CAST_BAR:
    HandleResumeCastBar(pkt);
    return true;
  case Op::MSG_TALENT_WIPE_CONFIRM:
    HandleTalentWipeConfirm(pkt);
    return true;
  case Op::SMSG_SUMMON_CANCEL:
    HandleSummonCancel(pkt);
    return true;
  case Op::SMSG_SPELL_UPDATE_CHAIN_TARGETS:
    HandleSpellUpdateChainTargets(pkt);
    return true;
  case Op::SMSG_NOTIFY_DEST_LOC_SPELL_CAST:
    HandleNotifyDestLocSpellCast(pkt);
    return true;
  case Op::SMSG_PET_LEARNED_SPELL:
    HandlePetLearnedSpell(pkt);
    return true;
  case Op::SMSG_PET_UNLEARNED_SPELL:
    HandlePetUnlearnedSpell(pkt);
    return true;

  case Op::SMSG_CHAT_PLAYER_NOT_FOUND:
    HandleChatPlayerNotFound(pkt);
    return true;
  case Op::SMSG_CHANNEL_LIST:
    HandleChannelList(pkt);
    return true;

  case Op::SMSG_CHAT_WRONG_FACTION:
    HandleChatWrongFaction(pkt);
    return true;
  case Op::SMSG_CHAT_SERVER_MESSAGE:
    HandleChatServerMessage(pkt);
    return true;
  case Op::SMSG_CHAT_NOT_IN_PARTY:
    HandleChatNotInParty(pkt);
    return true;
  case Op::SMSG_CHAT_RESTRICTED:
    HandleChatRestricted(pkt);
    return true;
  case Op::SMSG_DEFENSE_MESSAGE:
    HandleDefenseMessage(pkt);
    return true;
  case Op::SMSG_CHAT_PLAYER_AMBIGUOUS:
    HandleChatPlayerAmbiguous(pkt);
    return true;
  case Op::SMSG_CHANNEL_MEMBER_COUNT:
    HandleChannelMemberCount(pkt);
    return true;

  case Op::MSG_MOVE_ROOT:
    HandleMsgMoveRoot(pkt);
    return true;
  case Op::MSG_MOVE_UNROOT:
    HandleMsgMoveUnroot(pkt);
    return true;
  case Op::MSG_MOVE_KNOCK_BACK:
    HandleMsgMoveKnockBack(pkt);
    return true;
  case Op::MSG_MOVE_TELEPORT_ACK:
    HandleMsgMoveTeleportAck(pkt);
    return true;
  case Op::MSG_MOVE_WORLDPORT_ACK:
    HandleMsgMoveWorldportAck(pkt);
    return true;

  case Op::MSG_MOVE_FEATHER_FALL:
    HandleMsgMoveFeatherFall(pkt);
    return true;
  case Op::MSG_MOVE_HOVER:
    HandleMsgMoveHover(pkt);
    return true;
  case Op::MSG_MOVE_WATER_WALK:
    HandleMsgMoveWaterWalk(pkt);
    return true;
  case Op::MSG_MOVE_UPDATE_CAN_FLY:
    HandleMsgMoveUpdateCanFly(pkt);
    return true;
  case Op::MSG_MOVE_SET_RUN_MODE:
    HandleMsgMoveSetRunMode(pkt);
    return true;
  case Op::MSG_MOVE_SET_WALK_MODE:
    HandleMsgMoveSetWalkMode(pkt);
    return true;

  case Op::MSG_MOVE_SET_SWIM_BACK_SPEED:
    HandleMsgMoveSetSwimBackSpeed(pkt);
    return true;
  case Op::MSG_MOVE_SET_TURN_RATE:
    HandleMsgMoveSetTurnRate(pkt);
    return true;
  case Op::MSG_MOVE_SET_FLIGHT_BACK_SPEED:
    HandleMsgMoveSetFlightBackSpeed(pkt);
    return true;
  case Op::MSG_MOVE_SET_PITCH_RATE:
    HandleMsgMoveSetPitchRate(pkt);
    return true;

  case Op::SMSG_SPLINE_MOVE_FEATHER_FALL:
    HandleSplineMoveFeatherFall(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_NORMAL_FALL:
    HandleSplineMoveNormalFall(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_SET_HOVER:
    HandleSplineMoveSetHover(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_UNSET_HOVER:
    HandleSplineMoveUnsetHover(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_WATER_WALK:
    HandleSplineMoveWaterWalk(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_LAND_WALK:
    HandleSplineMoveLandWalk(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_START_SWIM:
    HandleSplineMoveStartSwim(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_STOP_SWIM:
    HandleSplineMoveStopSwim(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_SET_RUN_MODE:
    HandleSplineMoveSetRunMode(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_SET_WALK_MODE:
    HandleSplineMoveSetWalkMode(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_SET_FLYING:
    HandleSplineMoveSetFlying(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_UNSET_FLYING:
    HandleSplineMoveUnsetFlying(pkt);
    return true;

  case Op::SMSG_BREAK_TARGET:
    HandleBreakTarget(pkt);
    return true;
  case Op::SMSG_CLEAR_TARGET:
    HandleClearTarget(pkt);
    return true;
  case Op::SMSG_FORCE_DISPLAY_UPDATE:
    HandleForceDisplayUpdate(pkt);
    return true;
  case Op::SMSG_RESURRECT_FAILED:
    HandleResurrectFailed(pkt);
    return true;
  case Op::SMSG_SPIRIT_HEALER_CONFIRM:
    HandleSpiritHealerConfirm(pkt);
    return true;
  case Op::SMSG_AREA_SPIRIT_HEALER_TIME:
    HandleAreaSpiritHealerTime(pkt);
    return true;
  case Op::SMSG_DESTRUCTIBLE_BUILDING_DAMAGE:
    HandleDestructibleBuildingDamage(pkt);
    return true;

  case Op::SMSG_PAGE_TEXT_QUERY_RESPONSE:
    HandlePageTextQueryResponse(pkt);
    return true;
  case Op::SMSG_COMBAT_EVENT_FAILED:
    HandleCombatEventFailed(pkt);
    return true;
  case Op::SMSG_PROCRESIST:
    HandleProcResist(pkt);
    return true;
  case Op::SMSG_SPELLBREAKLOG:
    HandleSpellBreakLog(pkt);
    return true;
  case Op::SMSG_AURACASTLOG:
    HandleAuraCastLog(pkt);
    return true;
  case Op::SMSG_RESET_RANGED_COMBAT_TIMER:
    HandleResetRangedCombatTimer(pkt);
    return true;
  case Op::SMSG_SET_PROJECTILE_POSITION:
    HandleSetProjectilePosition(pkt);
    return true;

  case Op::SMSG_ITEM_NAME_QUERY_RESPONSE:
    HandleItemNameQueryResponse(pkt);
    return true;
  case Op::SMSG_ITEM_QUERY_MULTIPLE_RESPONSE:
    HandleItemQueryMultipleResponse(pkt);
    return true;

  case Op::SMSG_MOVE_GRAVITY_DISABLE:
    HandleMoveGravityDisable(pkt);
    return true;
  case Op::SMSG_MOVE_GRAVITY_ENABLE:
    HandleMoveGravityEnable(pkt);
    return true;
  case Op::SMSG_MOVE_SET_COLLISION_HGT:
    HandleMoveSetCollisionHgt(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_GRAVITY_DISABLE:
    HandleSplineMoveGravityDisable(pkt);
    return true;
  case Op::SMSG_SPLINE_MOVE_GRAVITY_ENABLE:
    HandleSplineMoveGravityEnable(pkt);
    return true;
  case Op::MSG_MOVE_GRAVITY_CHNG:
    HandleMoveGravityChng(pkt);
    return true;
  case Op::MSG_MOVE_SET_COLLISION_HGT:
    HandleMoveSetCollisionHgtAck(pkt);
    return true;
  case Op::MSG_MOVE_UPDATE_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY:
    HandleMoveUpdateCanTransitionSwimFly(pkt);
    return true;
  case Op::SMSG_MULTIPLE_MOVES:
    HandleMultipleMoves(pkt);
    return true;

  case Op::SMSG_GUILD_INFO:
    HandleGuildInfoPacket(pkt);
    return true;
  case Op::SMSG_GUILD_DECLINE:
    HandleGuildDeclinePacket(pkt);
    return true;

  case Op::MSG_SAVE_GUILD_EMBLEM:
    HandleSaveGuildEmblem(pkt);
    return true;
  case Op::MSG_TABARDVENDOR_ACTIVATE:
    HandleTabardVendorActivate(pkt);
    return true;
  case Op::MSG_PETITION_DECLINE:
    HandlePetitionDecline(pkt);
    return true;
  case Op::MSG_PETITION_RENAME:
    HandlePetitionRename(pkt);
    return true;
  case Op::SMSG_OFFER_PETITION_ERROR:
    HandleOfferPetitionError(pkt);
    return true;

  case Op::SMSG_GROUP_CANCEL:
    HandleGroupCancel(pkt);
    return true;

  case Op::SMSG_BATTLEFIELD_MGR_EJECTED:
    HandleBattlefieldMgrEjected(pkt);
    return true;
  case Op::SMSG_BATTLEFIELD_MGR_EJECT_PENDING:
    HandleBattlefieldMgrEjectPending(pkt);
    return true;
  case Op::SMSG_BATTLEFIELD_MGR_QUEUE_REQUEST_RESPONSE:
    HandleBattlefieldMgrQueueRequestResponse(pkt);
    return true;
  case Op::SMSG_BATTLEFIELD_MGR_STATE_CHANGE:
    HandleBattlefieldMgrStateChange(pkt);
    return true;

  case Op::SMSG_INSPECT_RESULTS_UPDATE:
    HandleInspectResultsUpdate(pkt);
    return true;
  case Op::MSG_INSPECT_ARENA_TEAMS:
    HandleInspectArenaTeams(pkt);
    return true;
  case Op::SMSG_ARENA_ERROR:
    HandleArenaError(pkt);
    return true;
  case Op::SMSG_ARENA_TEAM_CHANGE_FAILED_QUEUED:
    HandleArenaTeamChangeFailedQueued(pkt);
    return true;
  case Op::SMSG_ARENA_UNIT_DESTROYED:
    HandleArenaUnitDestroyed(pkt);
    return true;
  case Op::SMSG_JOINED_BATTLEGROUND_QUEUE:
    HandleJoinedBattlegroundQueue(pkt);
    return true;
  case Op::SMSG_BATTLEFIELD_PORT_DENIED:
    HandleBattlefieldPortDenied(pkt);
    return true;
  case Op::SMSG_BATTLEGROUND_INFO_THROTTLED:
    HandleBattlegroundInfoThrottled(pkt);
    return true;
  case Op::SMSG_REMOVED_FROM_PVP_QUEUE:
    HandleRemovedFromPvpQueue(pkt);
    return true;
  case Op::SMSG_REPORT_PVP_AFK_RESULT:
    HandleReportPvpAfkResult(pkt);
    return true;

  case Op::SMSG_LFG_PLAYER_INFO:
    HandleLfgPlayerInfo(pkt);
    return true;
  case Op::SMSG_LFG_PARTY_INFO:
    HandleLfgPartyInfo(pkt);
    return true;
  case Op::SMSG_LFG_ROLE_CHOSEN:
    HandleLfgRoleChosen(pkt);
    return true;
  case Op::SMSG_LFG_UPDATE_SEARCH:
    HandleLfgUpdateSearch(pkt);
    return true;
  case Op::SMSG_LFG_DISABLED:
    HandleLfgDisabled(pkt);
    return true;
  case Op::SMSG_OPEN_LFG_DUNGEON_FINDER:
    HandleOpenLfgDungeonFinder(pkt);
    return true;
  case Op::SMSG_UPDATE_LFG_LIST:
    HandleUpdateLfgList(pkt);
    return true;

  case Op::SMSG_AUCTION_LIST_PENDING_SALES:
    auction_packets_.HandleListPendingSales(pkt);
    return true;
  case Op::SMSG_AUCTION_REMOVED_NOTIFICATION:
    auction_packets_.HandleRemovedNotification(pkt);
    return true;

  case Op::MSG_NOTIFY_PARTY_SQUELCH:
    HandleNotifyPartySquelch(pkt);
    return true;
  case Op::SMSG_ECHO_PARTY_SQUELCH:
    HandleEchoPartySquelch(pkt);
    return true;
  case Op::SMSG_COMPLAIN_RESULT:
    HandleComplainResult(pkt);
    return true;
  case Op::SMSG_USERLIST_ADD:
    HandleUserlistAdd(pkt);
    return true;
  case Op::SMSG_USERLIST_REMOVE:
    HandleUserlistRemove(pkt);
    return true;
  case Op::SMSG_USERLIST_UPDATE:
    HandleUserlistUpdate(pkt);
    return true;

  case Op::MSG_DEV_SHOWLABEL:
  case Op::MSG_GM_ACCOUNT_ONLINE:
  case Op::MSG_GM_BIND_OTHER:
  case Op::MSG_GM_CHANGE_ARENA_RATING:
  case Op::MSG_GM_GEARRATING:
  case Op::MSG_GM_SUMMON:
  case Op::SMSG_CHEAT_DUMP_ITEMS_DEBUG_ONLY_RESPONSE:
    HandleItemQuerySingleResponse(pkt);
    return true;

  case Op::MSG_GM_DESTROY_CORPSE:
  case Op::MSG_GM_RESETINSTANCELIMIT:
  case Op::MSG_GM_SHOWLABEL:
  case Op::MSG_MOVE_SET_ALL_SPEED_CHEAT:
  case Op::MSG_MOVE_SET_FLIGHT_BACK_SPEED_CHEAT:
  case Op::MSG_MOVE_SET_FLIGHT_SPEED_CHEAT:
  case Op::MSG_MOVE_SET_PITCH_RATE_CHEAT:
  case Op::MSG_MOVE_SET_RUN_BACK_SPEED_CHEAT:
  case Op::MSG_MOVE_SET_RUN_SPEED_CHEAT:
  case Op::MSG_MOVE_SET_SWIM_BACK_SPEED_CHEAT:
  case Op::MSG_MOVE_SET_SWIM_SPEED_CHEAT:
  case Op::MSG_MOVE_SET_TURN_RATE_CHEAT:
  case Op::MSG_MOVE_SET_WALK_SPEED_CHEAT:
  case Op::MSG_MOVE_TELEPORT_CHEAT:
  case Op::MSG_MOVE_TOGGLE_FALL_LOGGING:
  case Op::MSG_MOVE_TOGGLE_LOGGING:
    return false;

  case Op::SMSG_IGNORE_DIMINISHING_RETURNS_CHEAT:
  case Op::SMSG_IGNORE_REQUIREMENTS_CHEAT:
    return false;
  case Op::SMSG_CHEAT_PLAYER_LOOKUP:
  case Op::SMSG_COOLDOWN_CHEAT:
  case Op::SMSG_DAMAGE_CALC_LOG:
  case Op::SMSG_DBLOOKUP:
  case Op::SMSG_DEBUG_AISTATE:
  case Op::SMSG_DEBUGAURAPROC:
  case Op::SMSG_DEBUG_LIST_TARGETS:
  case Op::SMSG_DEBUG_SERVER_GEO:
  case Op::SMSG_DUMP_OBJECTS_DATA:
  case Op::CMSG_GM_REQUEST_PLAYER_INFO:
  case Op::SMSG_GM_PLAYER_INFO:
  case Op::SMSG_GODMODE:
  case Op::SMSG_PETGODMODE:
  case Op::SMSG_MOVE_CHARACTER_CHEAT:
    return true;

  case Op::SMSG_AUTH_CHALLENGE:
  case Op::SMSG_AUTH_RESPONSE:
  case Op::SMSG_AUTH_SRP6_RESPONSE:
    return true;

  case Op::SMSG_COMSAT_RECONNECT_TRY:
    DisplayVoiceChatSystemMessage(579);
    return true;
  case Op::SMSG_COMSAT_DISCONNECT:
    DisplayVoiceChatSystemMessage(578);
    return true;
  case Op::SMSG_COMSAT_CONNECT_FAIL:
    DisplayVoiceChatSystemMessage(580);
    return true;
  case Op::SMSG_VOICE_SESSION_ADJUST_PRIORITY:
  case Op::SMSG_VOICE_SESSION_ENABLE:
    return true;
  case Op::SMSG_VOICESESSION_FULL:
    DisplayVoiceChatSystemMessage(593);
    return true;

  case Op::SMSG_COMMENTATOR_GET_PLAYER_INFO:
    return true;

  case Op::SMSG_COMMENTATOR_MAP_INFO:

    if (CommentatorState::Get().HandleMapInfoPacket(pkt.payload.data(), pkt.payload.size())) {
      ui::game::ScriptEventDispatch::Get().FireCommentatorMapUpdate();
    }
    return true;

  case Op::SMSG_COMMENTATOR_PLAYER_INFO:

    if (CommentatorState::Get().HandlePlayerInfoPacket(pkt.payload.data(), pkt.payload.size()) ==
        CommentatorPlayerInfoPacketResult::Updated) {
      ui::game::ScriptEventDispatch::Get().FireCommentatorPlayerUpdate();
    }
    return true;

  case Op::SMSG_COMMENTATOR_SKIRMISH_QUEUE_RESULT1:

    if (CommentatorState::Get().HandleSkirmishQueueResult(pkt.payload.data(), pkt.payload.size())) {
      ui::game::ScriptEventDispatch::Get().FireCommentatorSkirmishQueueRequest();
    }
    return true;

  case Op::SMSG_COMMENTATOR_SKIRMISH_QUEUE_RESULT2:

    if (CommentatorState::Get().HandleSkirmishModePacket(pkt.payload.data(), pkt.payload.size())) {
      ui::game::ScriptEventDispatch::Get().FireCommentatorSkirmishQueueRequest();
    }
    return true;

  case Op::SMSG_COMMENTATOR_STATE_CHANGED:

    {
      PacketReader reader(pkt.payload.data(), pkt.payload.size());
      std::uint64_t active_player_guid = 0;
      std::uint8_t enabled = 0;
      if (!reader.ReadU64(active_player_guid) || !reader.ReadU8(enabled)) {
        return true;
      }

      const auto *active_player = map_runtime_.objects().GetActivePlayer();
      if (active_player == nullptr ||
          active_player->GetGuid().GetRawValue() != active_player_guid) {
        return true;
      }

      auto &commentator = CommentatorState::Get();
      if (enabled != 0 && CanApplyCommentatorFollowCamera(*this, *active_player)) {
        commentator.SetActive(true);
        SwitchCommentatorCameraView(world_camera_, kCommentatorCameraView);
        SyncCommentatorCameraToActivePlayer(
            commentator, *active_player,
            active_player->GetMovementInfo().pitch);
      } else {
        SwitchCommentatorCameraView(world_camera_, kDefaultWorldCameraView);
        commentator.SetActive(false);
      }
    }
    return true;

  case Op::SMSG_CLEAR_EXTRA_AURA_INFO_OBSOLETE:
  case Op::SMSG_GOGOGO_OBSOLETE:
  case Op::SMSG_INIT_EXTRA_AURA_INFO_OBSOLETE:
  case Op::SMSG_LOTTERY_RESULT_OBSOLETE:
  case Op::SMSG_SET_EXTRA_AURA_INFO_NEED_UPDATE_OBSOLETE:
  case Op::SMSG_SET_EXTRA_AURA_INFO_OBSOLETE:
  case Op::SMSG_FORCEACTIONSHOW:
  case Op::SMSG_CHECK_FOR_BOTS:
    return true;

  case Op::SMSG_FORCE_ANIM: {
    PacketReader reader(pkt.payload.data(), pkt.payload.size());
    std::uint64_t discarded_guid = 0;
    std::string discarded_animation;
    if (reader.ReadU64(discarded_guid)) {
      (void)reader.ReadCString(discarded_animation,
                               kForceAnimStringReadBound);
    }
    return true;
  }

  case Op::SMSG_MINIGAME_SETUP:

    MinigameSystem::Get().HandleSetupPayload(pkt.payload.data(), pkt.payload.size());
    ui::game::ScriptEventDispatch::Get().FireStartMinigame();
    ui::game::ScriptEventDispatch::Get().FireMinigameUpdate();
    return true;

  case Op::SMSG_MINIGAME_STATE:

    MinigameSystem::Get().HandleStatePayload(pkt.payload.data(), pkt.payload.size());
    ui::game::ScriptEventDispatch::Get().FireMinigameUpdate();
    return true;

  case Op::SMSG_REDIRECT_CLIENT:
  case Op::SMSG_SUSPEND_COMMS:
  case Op::SMSG_FORCE_SEND_QUEUED_PACKETS:

    return true;

  case Op::SMSG_AFK_MONITOR_INFO_RESPONSE:
  case Op::SMSG_CHARACTER_PROFILE:
  case Op::SMSG_CHARACTER_PROFILE_REALM_CONNECTED:
  case Op::SMSG_EXPECTED_SPAM_RECORDS:
  case Op::SMSG_MULTIPLE_PACKETS:
  case Op::SMSG_PROFILEDATA_RESPONSE:
  case Op::SMSG_QUERY_OBJECT_POSITION:
  case Op::SMSG_QUERY_OBJECT_ROTATION:
  case Op::SMSG_RESISTLOG:
  case Op::SMSG_SCRIPT_MESSAGE:
  case Op::SMSG_SERVER_BUCK_DATA:
  case Op::SMSG_SERVER_BUCK_DATA_START:
  case Op::SMSG_SERVERINFO:
  case Op::SMSG_SERVER_INFO_RESPONSE:
  case Op::SMSG_SERVERTIME:
  case Op::SMSG_SPELL_CHANCE_PROC_LOG:
  case Op::SMSG_SPELL_CHANCE_RESIST_PUSHBACK:
  case Op::SMSG_TEST_DROP_RATE_RESULT:
  case Op::SMSG_ZONE_MAP:
    return true;

  case Op::SMSG_WHOIS: {
    PacketReader reader(pkt.payload.data(), pkt.payload.size());
    std::string response;
    (void)reader.ReadCString(response, kWhoisResponseStringReadBound);
    core::ida::ConsoleAddLine(response, core::ida::COLOR_DEFAULT);
    return true;
  }

  case Op::SMSG_RWHOIS: {
    PacketReader reader(pkt.payload.data(), pkt.payload.size());
    std::int32_t account_count = 0;
    (void)reader.ReadI32(account_count);
    if (account_count == kRWhoisFailureResult) {
      core::ida::ConsoleAddLine(kRWhoisFailureText,
                                core::ida::COLOR_ERROR);
      return true;
    }
    if (account_count == 0) {
      core::ida::ConsoleAddLine(kRWhoisNotFoundText,
                                core::ida::COLOR_WARNING);
      return true;
    }
    if (account_count < 0) {
      return true;
    }

    for (std::int32_t account_index = 0; account_index < account_count;
         ++account_index) {
      std::string account;
      (void)reader.ReadCString(account, kRWhoisAccountStringReadBound);
      core::ida::ConsoleLog(kRWhoisAccountFormat, account.c_str());

      std::int32_t character_count = 0;
      std::int32_t selected_character_index = 0;
      (void)reader.ReadI32(character_count);
      (void)reader.ReadI32(selected_character_index);
      for (std::int32_t character_index = 0;
           character_index < character_count; ++character_index) {
        std::string character_name;
        (void)reader.ReadCString(character_name,
                                 kRWhoisCharacterStringReadBound);
        core::ida::ConsoleLogColored(
            kRWhoisCharacterFormat,
            character_index == selected_character_index
                ? core::ida::COLOR_WARNING
                : core::ida::COLOR_DEFAULT,
            character_name.c_str());
      }
    }
    return true;
  }

  default:
    return false;
  }
}

void WorldSession::RefreshActivePlayerReleaseTimerMode() {

  if (death_callbacks_.on_refresh_release_timer_mode) {
    death_callbacks_.on_refresh_release_timer_mode();
  }
}

void WorldSession::ClearWorldPacketState() {
  feature_status_ = {};
  phase_shift_ = {};
  instance_difficulty_ = {};
  world_transition_.transfer_aborted() = {};
  world_transition_.query_time() = {};
  last_poi_ = {};
  corpse_query_ = {};
  active_player_was_ghost_ = false;
  last_roll_ = {};
  last_quest_complete_ = {};
  last_quest_list_ = {};
  resurrect_request_ = {};
  bank_npc_guid_ = 0;
  client_control_ = {};
  cancel_auto_repeat_info_ = {};
  dismount_guid_ = ObjectGuid(0);
  realm_split_ = {};
  pending_spell_missile_corrections_.clear();
  pending_spell_visual_presentation_events_.clear();
}

void WorldSession::HandleFeatureSystemStatus(
    const net::wotlk::WorldPacket &packet) {
  PacketReader reader(packet.payload.data(), packet.payload.size());
  if (!reader.ReadU8(feature_status_.complaint_status) ||
      !reader.ReadU8(feature_status_.voice_chat_enabled)) {
    return;
  }

  social_.SetComplaintStatus(feature_status_.complaint_status);

  auto &voice_chat = VoiceChat::Get();
  const bool was_server_allowed = voice_chat.IsServerAllowed();
  const bool is_server_allowed = feature_status_.voice_chat_enabled != 0;
  voice_chat.SetServerAllowed(sound_runtime_, is_server_allowed);
  if (was_server_allowed != is_server_allowed) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::VOICE_CHAT_ENABLED_UPDATE);
  }
}

void WorldSession::Update(float dt_seconds, std::uint32_t client_time_ms) {
  TickBotDetected(client_time_ms);

  if (state_ != WorldState::kInWorld) {

    ScriptEvents_FlushPendingUnitEvents();
    return;
  }

  session_.AdvanceGameTime(dt_seconds);
  combat_log_.Update(combat_log_.TimestampWithOffsetMs(0));
  RefreshWorldSceneGameTime();

  const TransportManagerUpdate transport_update = transport_mgr_.Update(dt_seconds);

  for (const TransportSequenceChange &change : transport_update.sequence_changes) {
    CGGameObject_C *const transport_object =
        map_runtime_.objects().GetMutableGameObject(change.guid);
    if (transport_object != nullptr) {
      transport_object->ApplyTransportSequenceEffect(change.sequenceId);
    }
  }
  for (const TransportMotionStep &step : transport_update.motion_steps) {
    const Transport *const transport = transport_mgr_.GetTransport(step.guid);
    if (transport == nullptr) {
      continue;
    }

    const std::vector<std::uint64_t> passengers(
        transport->GetPassengers().begin(), transport->GetPassengers().end());
    for (const std::uint64_t raw_guid : passengers) {
      CGUnit_C *const passenger =
          map_runtime_.objects().GetMutableUnit(ObjectGuid(raw_guid));
      if (passenger != nullptr) {
        (void)passenger->Movement().ApplyTransportMotionCollision(
            *this, step, client_time_ms);
      }
    }
  }

  map_runtime_.objects().AdvanceMovementEvents(*this, client_time_ms);

  map_runtime_.objects().AdvanceEmoteQueues();
  movement_spline_mgr_.Update(client_time_ms);

  map_runtime_.objects().AdvanceSplineMovement(movement_spline_mgr_);

  ChatBubbleSystem::Get().Update(dt_seconds, map_runtime_.objects());
  chat_sender_.Update(client_time_ms);
  duel_.Update(dt_seconds);

  const auto current_client_time_ms = CurrentClientTimeMs();
  const auto dispatch_pump_tick_ms = current_client_time_ms != 0
                                         ? current_client_time_ms
                                         : openwow::core::GameClock::GetTickCount32();
  spell_book_.Update(dispatch_pump_tick_ms);
  query_cache_.PumpDispatchQueues(dispatch_pump_tick_ms);
  quests_.PumpDispatchQueues(dispatch_pump_tick_ms);
  pet_.PumpPetNameQueries(dispatch_pump_tick_ms);
  const auto ready_check_now_tick = dispatch_pump_tick_ms;
  if (!ready_check_finish_sent_ && active_ready_check_initiator_guid_ != 0 &&
      active_ready_check_initiator_guid_ == map_runtime_.objects().GetActivePlayerGuid().GetRawValue() &&
      active_ready_check_end_tick_ != 0 &&
      static_cast<std::int32_t>(ready_check_now_tick - active_ready_check_end_tick_) >= 0) {
    FinalizeLocalReadyCheck();
  }

  const std::uint32_t ready_mask_delta = UpdateLocalUnitRegenAndRunes(dispatch_pump_tick_ms);
  if (ready_mask_delta != 0) {
    FireReadyRunePowerUpdates(ready_mask_delta);
    RefreshRuneUsability(*this);
  }

  keep_alive_timer_ += dt_seconds;
  if (keep_alive_timer_ >= kKeepAliveInterval) {
    keep_alive_timer_ -= kKeepAliveInterval;
    Send(net::wotlk::PacketSender::BuildKeepAlive());
  }

  latency_tracker_.Update(dt_seconds);

  ScriptEvents_FlushPendingUnitEvents();
}

void WorldSession::PrepareForWorldLeave() {

  CloseActiveLootWindow(*this);
  CloseQuestDialogLikeIda58CA70(*this, GetActiveQuestDialogCloseState(quests_), false, true);

  ResetSocketUiForWorldLeave(*this, true);
  SuspendIncomingChatDelivery();
  ResetAllMirrorTimers();
  mirror_timers_reset_for_world_leave_ = true;
}

void WorldSession::Logout() {

  if (const auto *persistence =
          GetConfiguredDbCachePersistence(db_cache_runtime_);
      persistence != nullptr) {
    (void)query_cache_.SavePlayerNameCacheWdb(
        persistence->GetCacheDirectory(), persistence->GetLocale());
    db_cache_runtime_.Flush();
  }

  if (!mirror_timers_reset_for_world_leave_) {
    ResetAllMirrorTimers();
  }
  ClearCalendarEventAlarms();
  mirror_timers_reset_for_world_leave_ = false;
  ResetBotDetectedCountdown();
  local_unit_regen_last_tick_ms_ = 0;

  ClearPendingDbCacheQueriesOnWorldLeave(*this);
  ResetSocketUiForWorldLeave(*this, false);
  if (world_map_ != nullptr) {
    world_map_->UnregisterActivePlayerExplorationRefresh();
  }
  UnregisterActivePlayerArenaTeamRefresh();
  active_player_arena_team_query_deadlines_ms_.fill(0u);
  UnregisterActivePlayerCharacterPointsRefresh();
  UnregisterActivePlayerNoReagentCostRefresh();
  UnregisterActivePlayerAmmoInventoryRefresh();
  UnregisterActivePlayerBuybackRefresh();
  UnregisterActivePlayerBankBagSlotCountRefresh();
  UnregisterActivePlayerGlyphRefresh();
  UnregisterActivePlayerPetSpellPowerRefresh();
  UnregisterActivePlayerCombatRatingRefresh();
  UnregisterActivePlayerDailyQuestRefresh();
  UnregisterActivePlayerPushPlayerEvents();
  UnregisterActivePlayerFieldBytes2Refresh();
  UnregisterActivePlayerRestStateRefresh();
  UnregisterActivePlayerShapeshiftFormRefresh();
  UnregisterActivePlayerControlGuidRefresh();
  UnregisterActivePlayerCoinageRefresh();
  UnregisterActivePlayerCurrencyRefresh();
  UnregisterActivePlayerSkillRefresh();
  UnregisterActivePlayerCritterRefresh();
  UnregisterActivePlayerRegenRefresh();
  ChatBubbleSystem::Get().Clear();

  if (map_runtime_.objects().GetActivePlayer() != nullptr) {
    interaction_.SendGroupDecline();
  }
  CloseTrainerForWorldLogout(*this);
  ui::game::CloseStableInteraction(
      *this, ui::game::StableCloseEventPolicy::ActiveInteractionOnly);

  player_control_runtime_.SetActiveMover(
      *this, map_runtime_.objects(), missile_trajectory_, 0);
  map_runtime_.objects().Clear();

  ScriptEvents_ResetWorldEventFlag();

  ChatFrame_ResetWorldUiReady();
  spell_book_.Clear();
  spellbook_private_usability_.Reset();
  ui::game::detail::ResetActionBarRuntimeState(*this);
  chat_.Clear();
  ClearPendingChatMessages();
  chat_sender_.Reset();

  Chat_Shutdown(sound_runtime_);
  social_.Clear();
  group_.Clear();

  GroupSystem::Get().Reset();
  quests_.Clear();
  TalentInfoStore::Get().ResetForWorldLogout();
  loot_.Clear();
  loot_.state().Reset();
  combat_log_.Clear();
  gossip_.Clear();
  trainer_unlearn_spell_cache_.Clear();
  refer_a_friend_runtime_.ClearLevelGrant();
  auction_packets_.Clear();
  pending_master_loot_candidate_name_queries_.clear();
  pending_group_loot_master_announcements_.clear();
  pending_friend_name_queries_.clear();
  pending_ignore_name_queries_.clear();
  pending_mute_name_queries_.clear();
  pending_social_name_resolutions_.clear();

  ReputationInfo::Get().Cleanup();
  FactionSystem::Get().Reset();
  guild_.Clear();
  trade_.Clear();

  mail_.ResetCompose();

  mail_.Shutdown();
  auction_.Clear();

  interaction_.SendLfgSearchLeave();
  lfg_.Clear();
  LFGSystem::Get().Reset();
  aura_.Clear();
  AuraTracker::Get().Reset();
  combat_.Clear();
  achievements_.Clear();
  pet_.Clear();
  world_states_.Clear();
  battleground_.Clear();

  ArenaTeamSystem::Get().Reset();
  ArenaSystem::Get().Reset();
  misc_.Clear();
  equipment_.reset();
  duel_.Reset();
  active_ready_check_initiator_guid_ = 0;
  active_ready_check_end_tick_ = 0;
  ready_check_finish_sent_ = false;
  pending_ready_check_initiator_guid_ = 0;
  reputation_runtime_.Clear();
  session_.Clear();
  monster_move_.Clear();
  movement_spline_mgr_.Clear();
  movement_.ResetTransientState();
  ResetMovementCollisionSolver();
  party_stats_.Clear();
  auto &taxi_system = TaxiSystem::Get();
  const bool taxi_map_was_open = taxi_.GetFlightMasterGuid() != 0;

  taxi_system.ResetRouteDisplayState();
  taxi_system.CloseTaxiMap();
  taxi_.CloseTaxiMap();
  if (taxi_map_was_open) {
    ui::game::ScriptEventDispatch::Get().FireTaxiMapClosed();
  }
  taxi_.Clear();
  taxi_system.ResetSessionState();
  ClearWorldPacketState();
  movement_ext_.Clear();
  runes_.Clear();
  spell_visual_.Clear();
  character_.Clear();
  summon_.Clear();
  spell_log_.Clear();
  instance_.Clear();
  ui::game::UnitTokenRegistry::Get().ClearBossFrames();
  inspect_.Clear();
  BarberShop::Get().Reset();

  GuildSystem::Get().ShutdownGuildBankRuntimeState();
  guild_bank_.Clear();
  calendar_.Clear();
  calendar_runtime_.Reset();

  CalendarSystem::Get().Reset();
  KnowledgeBase::Get().ResetSystemMessages();
  petition_.Clear();
  item_interactions_.reset(0);
  vehicle_.Clear();
  arena_.Clear();
  battlefield_mgr_.Clear();
  pending_trigger_cinematic_sequence_id_ = 0;
  refer_a_friend_runtime_.Reset();
  world_transition_.CancelStaged();

  BattlefieldInfo::Get().Reset();
  pet_handler_.Clear();
  gm_ticket_.Clear();
  gm_survey_.Reset();
  area_spirit_healer_.Reset();
  combat_handler_.Clear();
  transport_mgr_.Clear();

  MinigameSystem::Get().Reset();
  ui::game::ResetCapturePointUIManagerState();
  current_map_id_ = 0;
  has_current_map_ = false;
  login_verify_ = {};

  ResetWorldTaintEventRegistry();
  RefreshWorldSceneGameTime();
  state_ = WorldState::kDisconnected;
}

bool WorldSession::Send(const net::wotlk::WorldPacket &pkt) {

  if (net::PacketLog::Get().IsEnabled()) {
    net::PacketLog::Get().LogPacket(net::PacketDirection::kClientToServer,
                                    static_cast<std::uint16_t>(pkt.opcode), pkt.payload.data(),
                                    pkt.payload.size());
  }

  if (send_fn_)
    return send_fn_(pkt);
  return false;
}

bool WorldSession::RequireActivePlayerForQuestDispatch() const {

  return map_runtime_.objects().GetActivePlayer() != nullptr;
}

}
