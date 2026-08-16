#pragma once

#include "openwow/core/client_crt_random.h"
#include "openwow/game/session/handlers/inventory/item_packets.h"
#include "openwow/game/session/handlers/commerce/auction_packets.h"

#include "openwow/game/actions/application/action_assignment_runtime.h"
#include "openwow/game/account_data.h"
#include "openwow/game/character_map_runtime.h"
#include "openwow/game/actions/model/action_page.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/achievements/application/achievement_state.h"
#include "openwow/game/arena_handler.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/aura_manager.h"
#include "openwow/game/battlefield_mgr.h"
#include "openwow/game/battleground_manager.h"
#include "openwow/game/bg/bg_instance.h"
#include "openwow/game/calendar/adapters/protocol/calendar_handler.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/character_handler.h"
#include "openwow/game/chat_manager.h"
#include "openwow/game/chat_sender.h"
#include "openwow/game/click_to_move.h"
#include "openwow/game/combat_handler.h"
#include "openwow/game/combat/death/area_spirit_healer_state.h"
#include "openwow/game/combat_log.h"
#include "openwow/game/combat_manager.h"
#include "openwow/game/duel_system.h"
#include "openwow/game/faction_manager.h"
#include "openwow/game/gm_survey.h"
#include "openwow/game/gm_ticket_handler.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/group_manager.h"
#include "openwow/game/group_system.h"
#include "openwow/game/commerce/banking/adapters/protocol/guild_bank_protocol_session_state.h"
#include "openwow/game/guild_manager.h"
#include "openwow/game/inspect_handler.h"
#include "openwow/game/instance_handler.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/adapters/protocol/inventory_messages.h"
#include "openwow/game/inventory/item_interaction_lease.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/replica_sync.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/inventory/equipment_presentation.h"
#include "openwow/game/inventory/operations/inventory_commands.h"
#include "openwow/game/lfg_manager.h"
#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/commerce/merchants/merchant_requirements.h"
#include "openwow/game/misc_handler.h"
#include "openwow/game/monster_move.h"
#include "openwow/game/movement_controller.h"
#include "openwow/game/movement_ext.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/party_stats.h"
#include "openwow/game/pet_handler.h"
#include "openwow/game/pet_manager.h"
#include "openwow/game/petition_handler.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/quest_manager.h"
#include "openwow/game/raid_handler.h"
#include "openwow/game/rune_handler.h"
#include "openwow/game/session_handler.h"
#include "openwow/game/session/world_transition_controller.h"
#include "openwow/game/session/refer_a_friend_runtime.h"
#include "openwow/game/session/reputation_runtime.h"
#include "openwow/game/session/session_observations.h"
#include "openwow/game/calendar/calendar_runtime.h"
#include "openwow/game/social_manager.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spellbook_private_usability.h"
#include "openwow/game/spell_log.h"
#include "openwow/game/spell_visual.h"
#include "openwow/game/taxi_handler.h"
#include "openwow/game/time_sync.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/trainer_unlearn_spell_cache.h"
#include "openwow/game/transport_manager.h"
#include "openwow/game/vehicle_handler.h"
#include "openwow/game/world_state_manager.h"
#include "openwow/net/wotlk/main_thread_packet_dispatcher.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/net/transport/latency_tracker.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/world/movement/movement_spline.h"
#include "openwow/data/db_cache_instances.h"

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
struct MapEntry;
struct MapDifficultyEntry;
}

namespace openwow::data {
class DBCacheRuntime;
}
namespace openwow::render::m2 {
class M2System;
}
namespace openwow::render {
class WorldFrame;
}
namespace openwow::ui {
class MinimapSystem;
class WorldMapSystem;
}
namespace openwow::world {
class WorldCamera;
}
namespace openwow::audio { class SoundRuntime; }

namespace openwow::ui::game::runtime {
class WorldUiRuntimeContext;
}

namespace openwow::game {

namespace actions::held_cursor {
class HeldCursor;
}

class BindingProfiles;
class MinimapPingSystem;
class WorldEnvironmentState;
class WorldSession;
class VehiclePassengerC;
class DanceStudioSystem;
class DbcAchievementMetadataCatalog;
class TargetingSystem;
class UnitMissileTrajectory_C;
class MovementCollisionSolver;
class SpellCastRuntime;
struct PlayerControlRuntime;
class ReputationInfo;

void DisplayPlayerDifficultyChangedMessage(std::uint8_t difficulty);

void PrimeReputationInfo(ReputationInfo& reputation,
                         const data::dbc::DbcLoader* dbc,
                         const ObjectManager& objects);

enum class CalendarInviteLookupCompletionAction : std::uint8_t;
enum class AuctionSelectionList : std::uint8_t;

using WorldSendFn = std::function<bool(const net::wotlk::WorldPacket &)>;
using AccountDataPayloadConsumer =
    std::function<void(AccountDataType, std::uint32_t, const std::string&)>;

struct SpellVisualCallbacks {
  std::function<void(std::uint64_t caster_guid, std::uint32_t spell_id)> on_spell_start;
  std::function<void(std::uint64_t caster_guid, std::uint32_t spell_id,
                     const SpellGoVisualData &visual_data)>
      on_spell_go;
  std::function<void(std::uint64_t target_guid, std::uint32_t kit_id)>
      on_play_visual;
  std::function<void(std::uint64_t target_guid, std::uint32_t kit_id)>
      on_play_impact;
};

struct CastBarCallbacks {

  std::function<void(std::uint32_t spell_id, std::int32_t cast_time_ms)> on_cast_start;

  std::function<void(std::uint32_t spell_id)> on_cast_complete;

  std::function<void(std::uint32_t spell_id)> on_cast_interrupt;

  std::function<void(std::uint32_t spell_id, std::int32_t duration_ms)> on_channel_start;

  std::function<void(std::int32_t time_remaining_ms)> on_channel_update;
};

struct DeathCallbacks {

  std::function<void(bool force_event_dispatch)> on_dead;

  std::function<void()> on_refresh_release_timer_mode;
  std::function<void()> on_death_release_loc;
  std::function<void()> on_corpse_reclaim_delay;

  std::function<void()> on_corpse_position_cleared;
  std::function<void()> on_resurrect_request;

  std::function<bool(bool suppress_player_alive_event, bool force_event_dispatch)>
      on_alive;
  std::function<void(std::int32_t xp_loss)> on_spirit_healer_confirm;
};

struct CinematicCallbacks {
  std::function<void(std::uint32_t cinematic_sequence_id)> on_trigger_cinematic;
  std::function<void()> on_stop_cinematic;
};

struct MovieCallbacks {
  std::function<void(std::uint32_t movie_id)> on_trigger_movie;
};

struct LoginVerifyWorld {
  std::uint32_t map_id{0};
  float x{0}, y{0}, z{0}, orientation{0};
};

struct PendingCharacterIdentity {
  std::string name;
  std::uint8_t race_id{0};
  std::uint8_t class_id{0};
  std::uint8_t gender{2};

  [[nodiscard]] bool is_available() const noexcept { return !name.empty(); }
};

using EnterWorldTransitionCallback =
    std::function<void(std::uint32_t map_id, float x, float y, float z, float orientation,
                       const std::string &map_internal_name)>;

using TransferPendingCallback = std::function<void(const TransferPendingInfo &)>;
using MapGenerationReplacementCallback = std::function<void(std::uint32_t map_id)>;
using WeatherPresentationCallback =
    std::function<void(std::uint32_t weather_id, float intensity, bool smooth)>;
using LootSourceTargetSelectionCallback = std::function<void(std::uint64_t)>;
using TrackedGuidInvalidationCallback = std::function<void(std::uint64_t)>;

using NameQueryResponseCallback = std::function<void(std::uint64_t guid, bool name_unknown)>;
using LocalPlayerCombatFlagChangedCallback = std::function<void()>;
using CancelCombatCallback = std::function<void()>;
enum class AutoAttackCombatEvent : std::uint8_t {
  AttackStart = 0,
  AttackStop = 1,
  AttackerStateUpdate = 2,
};
using AutoAttackCombatEventCallback = std::function<void(
    AutoAttackCombatEvent event, std::uint64_t attacker_guid, std::uint64_t victim_guid)>;

using BotDetectedProbeFn = std::function<std::optional<std::array<std::uint8_t, 3>>()>;
using ConsumeLegacyTokenSeedVerificationFn = std::function<bool()>;
using BuildBotDetectedDigestFn = std::function<std::array<std::uint8_t, 20>(
    std::span<const std::uint8_t>)>;

class WorldSession {
public:
  WorldSession(openwow::data::DBCacheRuntime& db_cache_runtime,
               PlayerInventoryReplica& inventory_replica,
               CharacterMapRuntime& map_runtime,
               QueryCache& query_cache,
               TransportManager& transport_manager,
               ItemDefinitions& item_definitions,
                const data::dbc::DbcLoader& dbc_loader,
                UnitMissileTrajectory_C& missile_trajectory,
                net::wotlk::MainThreadPacketDispatcher& packet_dispatcher,
                PlayerControlRuntime& player_control_runtime,
                ConsumeLegacyTokenSeedVerificationFn
                   consume_legacy_token_seed_verification,
               BuildBotDetectedDigestFn build_bot_detected_digest,
                 const SpellbookSystem& spellbook, const PvPInfo& pvp,
                 const ReputationInfo& reputation,
                 SpellCastRuntime& spell_cast_runtime,
                 openwow::audio::SoundRuntime& sound_runtime,
                 openwow::core::ida::GameTimeData* shared_game_time = nullptr);
  ~WorldSession();

  [[nodiscard]] openwow::audio::SoundRuntime& sound_runtime() const noexcept {
    return sound_runtime_;
  }
  [[nodiscard]] SpellCastRuntime& spells() const noexcept {
    return spell_cast_runtime_;
  }
  [[nodiscard]] PlayerControlRuntime& player_control_runtime() noexcept {
    return player_control_runtime_;
  }
  [[nodiscard]] const PlayerControlRuntime& player_control_runtime()
      const noexcept {
    return player_control_runtime_;
  }

  void SetSendFn(WorldSendFn fn);
  void BindHeldCursor(actions::held_cursor::HeldCursor* cursor) noexcept {
    held_cursor_ = cursor;
    inventory_commands_.BindHeldCursor(cursor);
  }
  [[nodiscard]] actions::held_cursor::HeldCursor* held_cursor() noexcept {
    return held_cursor_;
  }
  [[nodiscard]] const actions::held_cursor::HeldCursor* held_cursor()
      const noexcept {
    return held_cursor_;
  }
  void BindWorldUiRuntime(
      openwow::ui::game::runtime::WorldUiRuntimeContext* runtime) noexcept {
    world_ui_runtime_ = runtime;
  }
  [[nodiscard]] openwow::ui::game::runtime::WorldUiRuntimeContext*
  world_ui_runtime() const noexcept {
    return world_ui_runtime_;
  }

  void SetClientTimeFn(std::function<std::uint32_t()> fn);
  void SetClientCacheVersionCallback(
      std::function<void(std::uint32_t)> callback) {
    client_cache_version_callback_ = std::move(callback);
  }
  void InvalidateDecodedCaches(
      std::uint32_t version,
      const openwow::data::DBCacheVersionChanges& changes);

  void RequestMasterLootCandidateName(std::uint64_t guid);

  void ApplyLootOptOutState(bool opt_out, bool announce_change);

  void SetSpellVisualCallbacks(SpellVisualCallbacks cb) {
    spell_visual_callbacks_ = std::move(cb);
  }

  void SetCastBarCallbacks(CastBarCallbacks cb) {
    cast_bar_callbacks_ = std::move(cb);
  }

  void SetDeathCallbacks(DeathCallbacks cb) {
    death_callbacks_ = std::move(cb);
  }

  void SetCinematicCallbacks(CinematicCallbacks cb) {
    cinematic_callbacks_ = std::move(cb);
    DispatchPendingTriggerCinematicIfReady();
  }
  void SetMovieCallbacks(MovieCallbacks cb) {
    movie_callbacks_ = std::move(cb);
  }

  void StopCinematicFromScript() {
    if (cinematic_callbacks_.on_stop_cinematic) {
      cinematic_callbacks_.on_stop_cinematic();
    } else {
      spell_visual_.StopCinematic();
    }
  }

  void SetEnterWorldTransitionCallback(EnterWorldTransitionCallback cb) {
    enter_world_transition_callback_ = std::move(cb);
  }

  void SetTransferPendingCallback(TransferPendingCallback cb) {
    transfer_pending_callback_ = std::move(cb);
  }

  void SetMapGenerationReplacementCallback(
      MapGenerationReplacementCallback cb) {
    map_generation_replacement_callback_ = std::move(cb);
  }

  void SetWeatherPresentationCallback(WeatherPresentationCallback cb) {
    weather_presentation_callback_ = std::move(cb);
  }

  void SetLootSourceTargetSelectionCallback(LootSourceTargetSelectionCallback cb) {
    loot_source_target_selection_callback_ = std::move(cb);
  }

  void SetTrackedGuidInvalidationCallback(TrackedGuidInvalidationCallback cb) {
    tracked_guid_invalidation_callback_ = std::move(cb);
  }

  void SetNameQueryResponseCallback(NameQueryResponseCallback cb) {
    name_query_response_callback_ = std::move(cb);
  }

  void SetLocalPlayerCombatFlagChangedCallback(LocalPlayerCombatFlagChangedCallback cb) {
    local_player_combat_flag_changed_callback_ = std::move(cb);
  }

  void SetAutoAttackCombatEventCallback(AutoAttackCombatEventCallback cb) {
    auto_attack_combat_event_callback_ = std::move(cb);
  }

  void SetCancelCombatCallback(CancelCombatCallback cb) {
    cancel_combat_callback_ = std::move(cb);
  }

  void NotifyTrackedGuidInvalidated(std::uint64_t guid);

  void PrepareForWorldLeave();

  void SetBotDetectedProbeFn(BotDetectedProbeFn fn) {
    bot_detected_probe_fn_ = std::move(fn);
  }

  [[nodiscard]] const data::dbc::DbcLoader *GetDbcLoader() const;
  [[nodiscard]] std::uint32_t CurrentClientTimeMs() const {
    return client_time_fn_ ? client_time_fn_() : 0;
  }

  [[nodiscard]] WorldState state() const {
    return state_;
  }
  [[nodiscard]] bool IsInWorld() const {
    return state_ == WorldState::kInWorld;
  }
  [[nodiscard]] std::weak_ptr<void> lifetime_token() const noexcept {
    return lifetime_token_;
  }

  [[nodiscard]] bool HasPendingTriggerCinematic() const {
    return pending_trigger_cinematic_sequence_id_ != 0;
  }

  bool BeginLogin(std::uint64_t character_guid);

  void SetPendingCharacterIdentity(PendingCharacterIdentity identity) {
    pending_character_identity_ = std::move(identity);
  }
  [[nodiscard]] const PendingCharacterIdentity &pending_character_identity() const {
    return pending_character_identity_;
  }

  void SetPendingCharacterName(std::string name) {
    pending_character_identity_.name = std::move(name);
  }
  [[nodiscard]] const std::string &pending_character_name() const {
    return pending_character_identity_.name;
  }

  bool AdoptLoginVerifyWorld(std::uint64_t character_guid,
                             const LoginVerifyWorld &verify);

  [[nodiscard]] const LoginVerifyWorld &login_verify() const {
    return login_verify_;
  }
  [[nodiscard]] bool has_current_map() const {
    return has_current_map_;
  }
  [[nodiscard]] std::uint32_t current_map_id() const {
    return current_map_id_;
  }

  [[nodiscard]] const data::dbc::MapEntry *LookupMapEntry(std::uint32_t map_id) const;

  [[nodiscard]] bool HasAvailableShapeshiftForms() const;

  bool HandlePacket(const net::wotlk::WorldPacket &pkt);

  bool ConsumeLogoutComplete() { return session_.ConsumeLogoutComplete(); }

  [[nodiscard]] ObjectManager &objects() {
    return map_runtime_.objects();
  }
  [[nodiscard]] const ObjectManager &objects() const {
    return map_runtime_.objects();
  }

  bool ConsumeActivePlayerCritterDescriptorRefresh() {
    const bool pending = active_player_critter_descriptor_refresh_pending_;
    active_player_critter_descriptor_refresh_pending_ = false;
    return pending;
  }
  [[nodiscard]] UnitMissileTrajectory_C& missile_trajectory() noexcept {
    return missile_trajectory_;
  }
  [[nodiscard]] const UnitMissileTrajectory_C& missile_trajectory()
      const noexcept {
    return missile_trajectory_;
  }
  [[nodiscard]] WorldSceneState& scene_state() {
    return scene_state_;
  }
  [[nodiscard]] const WorldSceneState& scene_state() const {
    return scene_state_;
  }

  [[nodiscard]] MovementController &movement() {
    return movement_;
  }
  [[nodiscard]] const MovementController &movement() const {
    return movement_;
  }
  void SetMovementCollisionSolver(
      std::shared_ptr<MovementCollisionSolver> solver);
  void ResetMovementCollisionSolver();
  [[nodiscard]] std::shared_ptr<MovementCollisionSolver>
  GetMovementCollisionSolver() const;

  void SetTransportCollisionReadinessQuery(
      std::function<bool(std::uint64_t)> query) {
    transport_collision_readiness_query_ = std::move(query);
  }
  [[nodiscard]] bool IsTransportParentCollisionGeometryReady(
      const std::uint64_t transport_guid) const {
    return !transport_collision_readiness_query_ ||
           transport_collision_readiness_query_(transport_guid);
  }

  [[nodiscard]] ClickToMoveSystem &click_to_move() {
    return click_to_move_;
  }
  [[nodiscard]] const ClickToMoveSystem &click_to_move() const {
    return click_to_move_;
  }

  void BindTargetingSystem(TargetingSystem *targeting) {
    targeting_system_ = targeting;
  }
  void BindWorldMapSystem(openwow::ui::WorldMapSystem* world_map) {
    world_map_ = world_map;
  }
  void BindMinimapSystem(openwow::ui::MinimapSystem* minimap) {
    minimap_ = minimap;
  }
  void BindMinimapPingSystem(MinimapPingSystem* minimap_ping) {
    minimap_ping_ = minimap_ping;
  }
  void BindWorldFrame(openwow::render::WorldFrame* world_frame) {
    world_frame_ = world_frame;
    map_runtime_.BindWorldFrame(world_frame);
  }
  void BindWorldEnvironmentState(WorldEnvironmentState* world_environment) {
    world_environment_ = world_environment;
    map_runtime_.BindWorldEnvironmentState(world_environment);
  }
  [[nodiscard]] WorldEnvironmentState* world_environment() noexcept {
    return world_environment_;
  }
  [[nodiscard]] const WorldEnvironmentState* world_environment() const noexcept {
    return world_environment_;
  }
  void BindWorldCamera(openwow::world::WorldCamera* world_camera) {
    world_camera_ = world_camera;
  }
  [[nodiscard]] openwow::world::WorldCamera* world_camera() noexcept {
    return world_camera_;
  }
  [[nodiscard]] const openwow::world::WorldCamera* world_camera() const noexcept {
    return world_camera_;
  }
  [[nodiscard]] openwow::render::WorldFrame* world_frame() noexcept {
    return world_frame_;
  }
  [[nodiscard]] const openwow::render::WorldFrame* world_frame() const noexcept {
    return world_frame_;
  }
  [[nodiscard]] TargetingSystem *targeting_system() {
    return targeting_system_;
  }
  [[nodiscard]] const TargetingSystem *targeting_system() const {
    return targeting_system_;
  }

  [[nodiscard]] TimeSyncManager &time_sync() {
    return time_sync_;
  }

  [[nodiscard]] ChatManager &chat() {
    return chat_;
  }
  [[nodiscard]] const ChatManager &chat() const {
    return chat_;
  }

  [[nodiscard]] SpellBook &spell_book() {
    return spell_book_;
  }
  [[nodiscard]] const SpellBook &spell_book() const {
    return spell_book_;
  }

  [[nodiscard]] SpellbookPrivateUsability& spellbook_private_usability() {
    return spellbook_private_usability_;
  }
  [[nodiscard]] const SpellbookPrivateUsability& spellbook_private_usability()
      const {
    return spellbook_private_usability_;
  }

  [[nodiscard]] ActionAssignmentRuntime &action_assignments() {
    return action_assignments_;
  }
  [[nodiscard]] const ActionAssignmentRuntime &action_assignments() const {
    return action_assignments_;
  }
  [[nodiscard]] actions::ActionPageState& action_page_state() noexcept {
    return action_page_state_;
  }
  [[nodiscard]] const actions::ActionPageState& action_page_state()
      const noexcept {
    return action_page_state_;
  }
  [[nodiscard]] MacroCatalog& macros() { return macro_catalog_; }
  [[nodiscard]] const MacroCatalog& macros() const { return macro_catalog_; }
  void SetBindingProfiles(BindingProfiles* bindings) noexcept {
    binding_profiles_ = bindings;
  }
  [[nodiscard]] BindingProfiles* binding_profiles() const noexcept {
    return binding_profiles_;
  }
  void SetAccountDataPayloadConsumer(AccountDataPayloadConsumer consumer) {
    account_data_payload_consumer_ = std::move(consumer);
  }
  [[nodiscard]] std::uint32_t cached_bonus_action_bar_offset() const {
    return cached_bonus_action_bar_offset_;
  }
  void set_cached_bonus_action_bar_offset(const std::uint32_t offset) {
    cached_bonus_action_bar_offset_ = offset;
  }

  [[nodiscard]] InventoryMessageState &inventory() {
    return inventory_;
  }
  [[nodiscard]] PlayerInventoryReplica& inventory_replica() noexcept {
    return inventory_replica_;
  }
  [[nodiscard]] ItemDefinitions& item_definitions() noexcept { return item_definitions_; }
  [[nodiscard]] const ItemDefinitions& item_definitions() const noexcept {
    return item_definitions_;
  }
  [[nodiscard]] openwow::data::DBCacheRuntime& db_cache_runtime() noexcept {
    return db_cache_runtime_;
  }
  [[nodiscard]] EquipmentSets& equipment() noexcept { return equipment_; }
  [[nodiscard]] const EquipmentSets& equipment() const noexcept {
    return equipment_;
  }
  [[nodiscard]] InventoryCommands& inventory_commands() noexcept {
    return inventory_commands_;
  }
  [[nodiscard]] const InventoryCommands& inventory_commands() const noexcept {
    return inventory_commands_;
  }
  [[nodiscard]] const PlayerInventoryReplica& inventory_replica() const noexcept {
    return inventory_replica_;
  }
  [[nodiscard]] ItemLockRegistry& item_locks() noexcept {
    return item_locks_;
  }
  [[nodiscard]] const ItemLockRegistry& item_locks() const noexcept {
    return item_locks_;
  }
  [[nodiscard]] const InventoryMessageState &inventory() const {
    return inventory_;
  }

  [[nodiscard]] SocialManager &social() {
    return social_;
  }
  [[nodiscard]] const SocialManager &social() const {
    return social_;
  }

  [[nodiscard]] GroupManager &group() {
    return group_;
  }
  [[nodiscard]] const GroupManager &group() const {
    return group_;
  }

  [[nodiscard]] QuestManager &quests() {
    return quests_;
  }
  [[nodiscard]] const QuestManager &quests() const {
    return quests_;
  }

  [[nodiscard]] LootInteraction &loot() {
    return loot_;
  }
  [[nodiscard]] const LootInteraction &loot() const {
    return loot_;
  }

  [[nodiscard]] CombatLog &combat_log() {
    return combat_log_;
  }
  [[nodiscard]] const CombatLog &combat_log() const {
    return combat_log_;
  }

  [[nodiscard]] GossipManager &gossip() {
    return gossip_;
  }
  [[nodiscard]] const GossipManager &gossip() const {
    return gossip_;
  }

  [[nodiscard]] TrainerUnlearnSpellCache &trainer_unlearn_spell_cache() {
    return trainer_unlearn_spell_cache_;
  }
  [[nodiscard]] const TrainerUnlearnSpellCache &trainer_unlearn_spell_cache() const {
    return trainer_unlearn_spell_cache_;
  }

  [[nodiscard]] GuildManager &guild() {
    return guild_;
  }
  [[nodiscard]] const GuildManager &guild() const {
    return guild_;
  }

  [[nodiscard]] TradeInteraction &trade() {
    return trade_;
  }
  [[nodiscard]] const TradeInteraction &trade() const {
    return trade_;
  }

  [[nodiscard]] MailInteraction &mail() {
    return mail_;
  }
  [[nodiscard]] const MailInteraction &mail() const {
    return mail_;
  }

  [[nodiscard]] AuctionInteraction &auction() {
    return auction_;
  }
  [[nodiscard]] const AuctionInteraction &auction() const {
    return auction_;
  }
  [[nodiscard]] AuctionPacketHandler& auction_packets() {
    return auction_packets_;
  }
  [[nodiscard]] const AuctionPacketHandler& auction_packets() const {
    return auction_packets_;
  }

  [[nodiscard]] LfgManager &lfg() {
    return lfg_;
  }
  [[nodiscard]] const LfgManager &lfg() const {
    return lfg_;
  }

  [[nodiscard]] AuraManager &aura() {
    return aura_;
  }
  [[nodiscard]] const AuraManager &aura() const {
    return aura_;
  }

  [[nodiscard]] CombatManager &combat() {
    return combat_;
  }
  [[nodiscard]] const CombatManager &combat() const {
    return combat_;
  }

  [[nodiscard]] DanceStudioSystem &dance_studio();
  [[nodiscard]] const DanceStudioSystem &dance_studio() const;

  [[nodiscard]] AchievementState &achievements() {
    return achievements_;
  }
  [[nodiscard]] const AchievementState &achievements() const {
    return achievements_;
  }

  [[nodiscard]] PetManager &pet() {
    return pet_;
  }
  [[nodiscard]] const PetManager &pet() const {
    return pet_;
  }

  [[nodiscard]] WorldStateManager &world_states() {
    return world_states_;
  }
  [[nodiscard]] const WorldStateManager &world_states() const {
    return world_states_;
  }

  [[nodiscard]] BattlegroundManager &battleground() {
    return battleground_;
  }
  [[nodiscard]] const BattlegroundManager &battleground() const {
    return battleground_;
  }

  [[nodiscard]] BgInstance &bg_instance() {
    return bg_instance_;
  }
  [[nodiscard]] const BgInstance &bg_instance() const {
    return bg_instance_;
  }

  [[nodiscard]] MiscHandler &misc() {
    return misc_;
  }
  [[nodiscard]] const MiscHandler &misc() const {
    return misc_;
  }

  [[nodiscard]] DuelSystem &duel() {
    return duel_;
  }
  [[nodiscard]] const DuelSystem &duel() const {
    return duel_;
  }

  [[nodiscard]] QueryCache &query_cache() {
    return query_cache_;
  }
  [[nodiscard]] const QueryCache &query_cache() const {
    return query_cache_;
  }

  [[nodiscard]] FactionManager &factions() {
    return reputation_runtime_.factions();
  }
  [[nodiscard]] const FactionManager &factions() const {
    return reputation_runtime_.factions();
  }

  [[nodiscard]] SessionHandler &session() {
    return session_;
  }
  [[nodiscard]] const SessionHandler &session() const {
    return session_;
  }

  [[nodiscard]] MonsterMoveManager &monster_move() {
    return monster_move_;
  }
  [[nodiscard]] const MonsterMoveManager &monster_move() const {
    return monster_move_;
  }

  [[nodiscard]] world::MovementSplineManager &movement_spline_mgr() {
    return movement_spline_mgr_;
  }
  [[nodiscard]] const world::MovementSplineManager &movement_spline_mgr() const {
    return movement_spline_mgr_;
  }

  [[nodiscard]] PartyStatsManager &party_stats() {
    return party_stats_;
  }
  [[nodiscard]] const PartyStatsManager &party_stats() const {
    return party_stats_;
  }

  [[nodiscard]] TaxiHandler &taxi() {
    return taxi_;
  }
  [[nodiscard]] const TaxiHandler &taxi() const {
    return taxi_;
  }

  [[nodiscard]] const FeatureSystemStatus& feature_status() const {
    return feature_status_;
  }
  [[nodiscard]] const PhaseShiftInfo& phase_shift() const {
    return phase_shift_;
  }
  [[nodiscard]] const InstanceDifficultyInfo& instance_difficulty() const {
    return instance_difficulty_;
  }
  [[nodiscard]] const TransferAbortedInfo& transfer_aborted() const {
    return world_transition_.transfer_aborted();
  }
  [[nodiscard]] const QueryTimeResponse& query_time() const {
    return world_transition_.query_time();
  }
  [[nodiscard]] const GossipPointOfInterest& last_poi() const {
    return last_poi_;
  }
  [[nodiscard]] const CorpseQueryResult& corpse_query() const {
    return corpse_query_;
  }
  [[nodiscard]] const RandomRollResult& last_roll() const {
    return last_roll_;
  }
  [[nodiscard]] const QuestCompleteInfo& last_quest_complete() const {
    return last_quest_complete_;
  }
  [[nodiscard]] const QuestListInfo& last_quest_list() const {
    return last_quest_list_;
  }
  [[nodiscard]] const ResurrectRequest& resurrect_request() const {
    return resurrect_request_;
  }
  [[nodiscard]] std::uint64_t bank_npc_guid() const {
    return bank_npc_guid_;
  }
  void SetBankNpcGuid(std::uint64_t guid) {
    bank_npc_guid_ = guid;
  }
  void CloseBank() {
    bank_npc_guid_ = 0;
  }
  [[nodiscard]] const ClientControlUpdate& client_control() const {
    return client_control_;
  }
  [[nodiscard]] const CancelAutoRepeatInfo& cancel_auto_repeat_info() const {
    return cancel_auto_repeat_info_;
  }
  [[nodiscard]] const ObjectGuid& dismount_guid() const {
    return dismount_guid_;
  }
  [[nodiscard]] const RealmSplitInfo& realm_split() const {
    return realm_split_;
  }

  [[nodiscard]] MovementExtHandler &movement_ext() {
    return movement_ext_;
  }
  [[nodiscard]] const MovementExtHandler &movement_ext() const {
    return movement_ext_;
  }

  [[nodiscard]] RuneHandler &runes() {
    return runes_;
  }
  [[nodiscard]] const RuneHandler &runes() const {
    return runes_;
  }

  [[nodiscard]] SpellVisualHandler &spell_visual() {
    return spell_visual_;
  }
  [[nodiscard]] const SpellVisualHandler &spell_visual() const {
    return spell_visual_;
  }

  [[nodiscard]] CharacterHandler &character() {
    return character_;
  }
  [[nodiscard]] const CharacterHandler &character() const {
    return character_;
  }

  [[nodiscard]] SummonInteraction &summon() {
    return summon_;
  }
  [[nodiscard]] const SummonInteraction &summon() const {
    return summon_;
  }

  [[nodiscard]] SpellLogHandler &spell_log() {
    return spell_log_;
  }
  [[nodiscard]] const SpellLogHandler &spell_log() const {
    return spell_log_;
  }

  [[nodiscard]] InstanceHandler &instance() {
    return instance_;
  }
  [[nodiscard]] const InstanceHandler &instance() const {
    return instance_;
  }

  [[nodiscard]] InspectHandler &inspect() {
    return inspect_;
  }
  [[nodiscard]] const InspectHandler &inspect() const {
    return inspect_;
  }

  [[nodiscard]] GuildBankProtocolSessionState &guild_bank() {
    return guild_bank_;
  }
  [[nodiscard]] const GuildBankProtocolSessionState &guild_bank() const {
    return guild_bank_;
  }

  [[nodiscard]] CalendarHandler &calendar() {
    return calendar_;
  }
  [[nodiscard]] const CalendarHandler &calendar() const {
    return calendar_;
  }

  [[nodiscard]] PetitionHandler &petition() {
    return petition_;
  }
  [[nodiscard]] const PetitionHandler &petition() const {
    return petition_;
  }

  [[nodiscard]] ItemInteractionSession& item_interactions() {
    return item_interactions_;
  }
  [[nodiscard]] const ItemInteractionSession& item_interactions() const {
    return item_interactions_;
  }

  [[nodiscard]] VehicleHandler &vehicle() {
    return vehicle_;
  }
  [[nodiscard]] const VehicleHandler &vehicle() const {
    return vehicle_;
  }

  [[nodiscard]] ArenaHandler &arena() {
    return arena_;
  }
  [[nodiscard]] const ArenaHandler &arena() const {
    return arena_;
  }

  [[nodiscard]] BattlefieldMgrHandler &battlefield_mgr() {
    return battlefield_mgr_;
  }
  [[nodiscard]] const BattlefieldMgrHandler &battlefield_mgr() const {
    return battlefield_mgr_;
  }

  [[nodiscard]] PetHandler &pet_handler() {
    return pet_handler_;
  }
  [[nodiscard]] const PetHandler &pet_handler() const {
    return pet_handler_;
  }

  [[nodiscard]] GMTicketHandler &gm_ticket() {
    return gm_ticket_;
  }
  [[nodiscard]] const GMTicketHandler &gm_ticket() const {
    return gm_ticket_;
  }

  [[nodiscard]] GMSurveySystem &gm_survey() {
    return gm_survey_;
  }
  [[nodiscard]] const GMSurveySystem &gm_survey() const {
    return gm_survey_;
  }

  [[nodiscard]] CombatHandler &combat_handler() {
    return combat_handler_;
  }
  [[nodiscard]] const CombatHandler &combat_handler() const {
    return combat_handler_;
  }

  [[nodiscard]] combat::death::AreaSpiritHealerState &area_spirit_healer() {
    return area_spirit_healer_;
  }
  [[nodiscard]] const combat::death::AreaSpiritHealerState &
  area_spirit_healer() const {
    return area_spirit_healer_;
  }

  [[nodiscard]] InteractionSender &interaction() {
    return interaction_;
  }
  [[nodiscard]] const InteractionSender &interaction() const {
    return interaction_;
  }
  void DeleteFriendContactByName(const std::string &name);
  void DeleteIgnoredContactByName(const std::string &name);
  void DeleteMutedContactByName(const std::string &name);
  void DisplaySocialApiError(std::uint32_t message_id) const;

  [[nodiscard]] PlayerInventoryReplicaSync &inventory_bridge() {
    return inventory_bridge_;
  }
  [[nodiscard]] const ItemUseRequirementSources&
  item_use_requirement_sources() const {
    return item_use_requirement_sources_;
  }
  void SetMerchantArenaTeamQuery(MerchantArenaTeamQuery query) {
    merchant_arena_team_query_ = std::move(query);
  }
  [[nodiscard]] const MerchantArenaTeamQuery& merchant_arena_team_query() const {
    return merchant_arena_team_query_;
  }
  enum class ItemLifecycleKind : std::uint8_t {
    kRemoved,
    kDestroyed,
  };
  struct ItemLifecycleChange {
    ItemLifecycleKind kind;
    std::uint64_t item_guid;
  };
  struct InventoryPresentationChanges {
    std::vector<ItemPushResult> item_pushes;
    std::vector<ItemLifecycleChange> item_lifecycle;
    std::optional<EquipmentPresentation> equipment;
  };
  void QueueItemPush(ItemPushResult result) {
    inventory_presentation_changes_.item_pushes.push_back(std::move(result));
  }
  void QueueItemLifecycle(ItemLifecycleKind kind, std::uint64_t item_guid) {
    inventory_presentation_changes_.item_lifecycle.push_back({kind, item_guid});
  }
  void QueueEquipmentPresentation();
  void ResolveInventoryTemplateContainers();
  [[nodiscard]] InventoryPresentationChanges
  TakeInventoryPresentationChanges() {
    return std::exchange(inventory_presentation_changes_, {});
  }
  void QueueSpellMissileCorrection(SpellMissilePositionCorrection correction) {
    pending_spell_missile_corrections_.push_back(std::move(correction));
  }
  [[nodiscard]] std::vector<SpellMissilePositionCorrection>
  TakeSpellMissileCorrections() {
    return std::exchange(pending_spell_missile_corrections_, {});
  }
  void QueueSpellVisualPresentationEvent(
      SpellVisualPresentationEvent event, std::uint32_t delay_ms = 0u);
  void QueueSpellVisualPresentationEvents(
      std::vector<SpellVisualPresentationEvent> events,
      std::uint32_t delay_ms = 0u);
  [[nodiscard]] std::vector<SpellVisualPresentationEvent>
  TakeReadySpellVisualPresentationEvents();
  [[nodiscard]] const PlayerInventoryReplicaSync &inventory_bridge() const {
    return inventory_bridge_;
  }

  [[nodiscard]] ChatSender &chat_sender() {
    return chat_sender_;
  }
  [[nodiscard]] const ChatSender &chat_sender() const {
    return chat_sender_;
  }
  void DisplayReferAFriendFailure(std::uint32_t reason, std::uint64_t target_guid = 0);
  void DisplayReferAFriendFailure(std::uint32_t reason, const std::string &target_name);
  void BeginLevelGrantProposal(std::uint64_t guid);
  void AcceptLevelGrant();
  void DeclineLevelGrant();
  [[nodiscard]] std::uint64_t pending_level_grant_guid() const {
    return refer_a_friend_runtime_.pending_level_grant_guid();
  }

  [[nodiscard]] TransportManager &transport_mgr() {
    return transport_mgr_;
  }
  [[nodiscard]] const TransportManager &transport_mgr() const {
    return transport_mgr_;
  }

  [[nodiscard]] net::LatencyTracker &latency_tracker() {
    return latency_tracker_;
  }
  [[nodiscard]] const net::LatencyTracker &latency_tracker() const {
    return latency_tracker_;
  }
  [[nodiscard]] core::ClientCrtRandom& client_random() noexcept {
    return client_random_;
  }

  void StartBotDetectedCountdown(std::uint32_t client_time_ms);
  void StartBotDetectedCountdownFromInit(std::uint32_t enter_world_init_time_ms,
                                         std::uint32_t client_time_ms);
  void Update(float dt_seconds, std::uint32_t client_time_ms);
  void FlushDeferredWorldTransfer();
  void RequestVisibleQuestgiverStatusRefresh();
  void HandleActivePlayerDeadTransition(bool force_event_dispatch = false);

  void RefreshActivePlayerReleaseTimerMode();
  void HandleActivePlayerAliveTransition(bool suppress_player_alive_event,
                                         bool force_event_dispatch = false);

  void EvaluateActivePlayerLifeLevel(bool force_event_dispatch);
  void CloseGuildRegistrarInteraction();
  void ClosePetitionSignatureDisplay();
  void ClosePetitionVendorInteraction();
  void CloseTabardVendorInteraction();
  void BeginTradeSkillLinkOpen(std::uint32_t spell_id, std::uint32_t current_rank,
                               std::uint32_t max_rank, std::uint64_t player_guid,
                               std::string encoded_recipe_bits);

  void Logout();

private:
  enum class PendingSocialListKind : std::uint8_t {
    kFriend,
    kIgnore,
    kMute,
  };

  struct PendingSocialNameResolution {
    std::uint64_t guid{0};
    PendingSocialListKind list_kind{PendingSocialListKind::kFriend};
    std::uint32_t message_id{730};
    bool refresh_visible_list{false};
  };

  [[nodiscard]] bool IsActiveArenaBattlefield() const;
  void SyncTrackedGroupMemberSnapshot(const WorldObject &obj);
  void SyncArenaOpponentSnapshot(const WorldObject &obj, bool is_create = false);
  void HandleTrackedGroupMemberDestroyed(ObjectGuid guid);
  void RequestPartyMemberStats(ObjectGuid guid);
  void ApplyGroupLootSettingsUpdate(std::uint8_t previous_loot_method,
                                    std::uint64_t previous_master_looter,
                                    std::uint8_t previous_loot_threshold, bool force_announcements);
  void QueueGroupLootMasterAnnouncement(std::uint64_t guid);
  void DispatchReadyCheckStart(std::uint64_t initiator_guid);
  void ResolvePendingReadyCheckNameQuery(std::uint64_t guid, bool name_unknown);
  void CancelLocalReadyCheck(bool interrupted);
  void ReconcileReadyCheckAfterGroupListUpdate(std::size_t removed_member_count,
                                               std::uint64_t previous_leader_guid);
  void FinalizeLocalReadyCheck();
  void ResolvePendingLevelGrantNameQuery(std::uint64_t guid, bool name_unknown);
  void ResolvePendingTradeSkillLinkNameQuery(std::uint64_t guid, bool name_unknown);
  void ResolvePendingMasterLootCandidateName(std::uint64_t guid, bool name_unknown);
  void ResolvePendingGroupLootMasterAnnouncements(std::uint64_t guid, bool name_unknown);
  void ResolvePendingSocialNameQueries(std::uint64_t guid, bool name_unknown);
  void FlushInventoryReplicaTransaction();
  [[nodiscard]] bool ShouldInvalidateTrackedGuidForDestroy(ObjectGuid guid) const;
  [[nodiscard]] std::uint8_t ApplyLocalChannelRosterMuteFlag(const std::string &channel_name,
                                                             std::uint64_t raw_guid,
                                                             std::uint8_t raw_flags) const;
  [[nodiscard]] std::uint8_t
  DecorateWatchedChannelRosterVoiceFlags(const std::string &channel_name, std::uint64_t raw_guid,
                                         std::uint8_t base_flags, bool include_silenced_bit) const;
  void RefreshWatchedChannelRosterLocalMuteFlags();
  void RefreshSelectedJoinedChannelVoiceRoster(const std::string &channel_name);
  void DispatchPendingTriggerCinematicIfReady();
  net::wotlk::MainThreadPacketDispatcher& packet_dispatcher_;
  PlayerControlRuntime& player_control_runtime_;
  WorldState state_{WorldState::kDisconnected};
  bool mirror_timers_reset_for_world_leave_{false};
  WorldSendFn send_fn_;
  actions::held_cursor::HeldCursor* held_cursor_{nullptr};
  openwow::ui::game::runtime::WorldUiRuntimeContext* world_ui_runtime_{nullptr};
  std::function<std::uint32_t()> client_time_fn_;
  std::function<void(std::uint32_t)> client_cache_version_callback_;
  SpellVisualCallbacks spell_visual_callbacks_;
  CastBarCallbacks cast_bar_callbacks_;
  DeathCallbacks death_callbacks_;
  CinematicCallbacks cinematic_callbacks_;
  MovieCallbacks movie_callbacks_;
  EnterWorldTransitionCallback enter_world_transition_callback_;
  TransferPendingCallback transfer_pending_callback_;
  MapGenerationReplacementCallback map_generation_replacement_callback_;
  WeatherPresentationCallback weather_presentation_callback_;
  LootSourceTargetSelectionCallback loot_source_target_selection_callback_;
  TrackedGuidInvalidationCallback tracked_guid_invalidation_callback_;
  NameQueryResponseCallback name_query_response_callback_;
  LocalPlayerCombatFlagChangedCallback local_player_combat_flag_changed_callback_;
  AutoAttackCombatEventCallback auto_attack_combat_event_callback_;
  CancelCombatCallback cancel_combat_callback_;
  WorldTransitionController world_transition_;
  std::uint32_t pending_trigger_cinematic_sequence_id_{0};

  LoginVerifyWorld login_verify_;
  std::uint64_t pending_character_guid_{0};
  PendingCharacterIdentity pending_character_identity_;
  std::uint32_t current_map_id_{0};
  bool has_current_map_{false};

  openwow::data::DBCacheRuntime& db_cache_runtime_;
  openwow::audio::SoundRuntime& sound_runtime_;
  SpellCastRuntime& spell_cast_runtime_;
  const data::dbc::DbcLoader* const dbc_;
  PlayerInventoryReplica& inventory_replica_;
  ItemDefinitions& item_definitions_;
  WorldSceneState scene_state_;
  CharacterMapRuntime& map_runtime_;
  UnitMissileTrajectory_C& missile_trajectory_;
  ObjectManagerCallbacks object_manager_callbacks_;
  ItemLockRegistry item_locks_;
  MovementController movement_;
  std::shared_ptr<MovementCollisionSolver> movement_collision_solver_;
  std::function<bool(std::uint64_t)> transport_collision_readiness_query_;
  ClickToMoveSystem click_to_move_;
  TargetingSystem *targeting_system_{nullptr};
  openwow::ui::WorldMapSystem* world_map_{nullptr};
  openwow::ui::MinimapSystem* minimap_{nullptr};
  MinimapPingSystem* minimap_ping_{nullptr};
  openwow::render::WorldFrame* world_frame_{nullptr};
  WorldEnvironmentState* world_environment_{nullptr};
  openwow::world::WorldCamera* world_camera_{nullptr};
  TimeSyncManager time_sync_;
  ChatManager chat_;
  MacroCatalog macro_catalog_;
  BindingProfiles* binding_profiles_{nullptr};
  AccountDataPayloadConsumer account_data_payload_consumer_;
  ActionAssignmentRuntime action_assignments_;
  actions::ActionPageState action_page_state_;
  std::uint32_t cached_bonus_action_bar_offset_{0};
  InventoryMessageState inventory_;
  EquipmentSets equipment_;
  SocialManager social_;
  GroupManager group_;
  QuestManager quests_;
  LootInteraction loot_;
  CombatLog combat_log_;
  GossipManager gossip_;
  SpellBook spell_book_;
  SpellbookPrivateUsability spellbook_private_usability_;
  TrainerUnlearnSpellCache trainer_unlearn_spell_cache_;
  GuildManager guild_;
  TradeInteraction trade_;
  MailInteraction mail_;
  AuctionInteraction auction_;
  LfgManager lfg_;
  AuraManager aura_;
  CombatManager combat_;
  std::unique_ptr<DanceStudioSystem> dance_studio_;
  std::unique_ptr<DbcAchievementMetadataCatalog> achievement_metadata_;
  AchievementState achievements_;
  PetManager pet_;
  WorldStateManager world_states_;
  BattlegroundManager battleground_;
  BgInstance bg_instance_;
  MiscHandler misc_;
  DuelSystem duel_;
  QueryCache& query_cache_;
  ReputationRuntime reputation_runtime_;
  SessionHandler session_;
  MonsterMoveManager monster_move_;
  world::MovementSplineManager movement_spline_mgr_;
  PartyStatsManager party_stats_;
  TaxiHandler taxi_;
  FeatureSystemStatus feature_status_{};
  PhaseShiftInfo phase_shift_{};
  InstanceDifficultyInfo instance_difficulty_{};
  GossipPointOfInterest last_poi_{};
  CorpseQueryResult corpse_query_{};

  bool active_player_was_ghost_ = false;
  RandomRollResult last_roll_{};
  QuestCompleteInfo last_quest_complete_{};
  QuestListInfo last_quest_list_{};
  ResurrectRequest resurrect_request_{};
  std::uint64_t bank_npc_guid_{0};
  ClientControlUpdate client_control_{};
  CancelAutoRepeatInfo cancel_auto_repeat_info_{};
  ObjectGuid dismount_guid_{ObjectGuid(0)};
  RealmSplitInfo realm_split_{};
  MovementExtHandler movement_ext_;
  RuneHandler runes_;
  SpellVisualHandler spell_visual_;
  CharacterHandler character_;
  SummonInteraction summon_;
  SpellLogHandler spell_log_;
  InstanceHandler instance_;
  InspectHandler inspect_;
  GuildBankProtocolSessionState guild_bank_;
  CalendarHandler calendar_;
  PetitionHandler petition_;
  ItemUseRequirementSources item_use_requirement_sources_;
  MerchantArenaTeamQuery merchant_arena_team_query_;
  InventoryPresentationChanges inventory_presentation_changes_;
  std::vector<SpellMissilePositionCorrection>
      pending_spell_missile_corrections_;
  struct PendingSpellVisualPresentationEvent {
    std::uint32_t ready_at_ms{0};
    SpellVisualPresentationEvent event;
  };
  std::vector<PendingSpellVisualPresentationEvent>
      pending_spell_visual_presentation_events_;
  ItemInteractionSession item_interactions_;
  VehicleHandler vehicle_;
  ArenaHandler arena_;
  BattlefieldMgrHandler battlefield_mgr_;
  PetHandler pet_handler_;
  GMTicketHandler gm_ticket_;
  GMSurveySystem gm_survey_;
  CombatHandler combat_handler_;
  combat::death::AreaSpiritHealerState area_spirit_healer_;
  InteractionSender interaction_;
  InventoryCommands inventory_commands_;
  PlayerInventoryReplicaSync inventory_bridge_;
  std::vector<std::int32_t> deferred_inventory_template_containers_;
  bool update_object_batch_active_{false};
  AuctionPacketHandler auction_packets_;

  CalendarRuntime calendar_runtime_;
  std::vector<std::uint64_t> active_player_character_points_callback_handles_;
  std::uint64_t active_player_no_reagent_cost_callback_handle_{0};
  std::uint64_t active_player_ammo_callback_handle_{0};
  std::uint32_t active_player_ammo_attachment_id_{
      kAmmoProjectileAttachmentId};
  bool active_player_ammo_attachment_selection_pending_{true};
  std::uint64_t active_player_arena_team_callback_handle_{0};

  std::array<std::uint32_t, 3> active_player_arena_team_query_deadlines_ms_{};
  std::uint64_t active_player_buyback_callback_handle_{0};
  std::uint64_t active_player_bank_bag_slot_callback_handle_{0};
  std::uint64_t active_player_regen_callback_handle_{0};
  std::vector<std::uint64_t> active_player_glyph_callback_handles_;
  std::uint64_t active_player_pet_spell_power_callback_handle_{0};
  std::vector<std::uint64_t> active_player_combat_rating_callback_handles_;
  std::vector<std::uint64_t> active_player_daily_quest_callback_handles_;
  std::vector<std::uint64_t> active_player_push_player_callback_handles_;
  std::uint64_t active_player_field_bytes2_callback_handle_{0};
  std::uint64_t active_player_rest_state_callback_handle_{0};
  std::vector<std::uint64_t> active_player_currency_callback_handles_;
  std::uint64_t active_player_shapeshift_form_callback_handle_{0};
  std::uint64_t active_player_control_guid_callback_handle_{0};
  std::uint64_t active_player_critter_callback_handle_{0};
  bool active_player_critter_descriptor_refresh_pending_{false};
  std::uint64_t active_player_coinage_callback_handle_{0};
  std::vector<std::uint64_t> active_player_skill_callback_handles_;
  bool daily_quests_were_empty_{false};
  ChatSender chat_sender_;
  TransportManager& transport_mgr_;
  struct PendingChannelInvite {
    std::uint64_t inviter_guid{0};
    std::string channel_name;
  };
  std::optional<PendingChannelInvite> pending_channel_invite_;
  struct PendingIncomingChatMessage {
    ChatMessage message;
    bool dispatch_as_incoming{false};
  };
  struct PendingIncomingTextEmote {
    ObjectGuid source_guid;
    std::uint32_t text_emote_id{0};
    std::uint32_t sound_id{0};
    std::string target_name;
    std::string source_name;
    bool use_alternate_gender_variant{false};
  };
  struct PendingChannelListMember {
    std::uint64_t guid{0};
    std::uint8_t member_flags{0};
  };
  struct PendingChannelListDisplay {
    std::string channel_name;
    std::vector<PendingChannelListMember> members;
  };
  std::vector<PendingIncomingChatMessage> pending_chat_messages_;
  std::vector<PendingIncomingTextEmote> pending_text_emotes_;
  std::vector<PendingChannelListDisplay> pending_channel_lists_;
  std::unordered_set<std::uint64_t> pending_chat_name_queries_;
  bool incoming_chat_delivery_suspended_{false};
  std::uint64_t active_ready_check_initiator_guid_{0};
  std::uint32_t active_ready_check_end_tick_{0};
  bool ready_check_finish_sent_{false};
  std::uint64_t pending_ready_check_initiator_guid_{0};
  struct PendingTradeSkillLinkOpen {
    std::uint32_t spell_id{0};
    std::uint32_t current_rank{0};
    std::uint32_t max_rank{0};
    std::uint64_t player_guid{0};
    std::string encoded_recipe_bits;
  };
  std::vector<PendingTradeSkillLinkOpen> pending_trade_skill_link_opens_;
  std::unordered_set<std::uint64_t> pending_raid_roster_name_queries_;
  bool pending_raid_roster_local_player_resolution_{false};
  std::unordered_set<std::uint64_t> pending_master_loot_candidate_name_queries_;
  std::vector<std::uint64_t> pending_group_loot_master_announcements_;
  std::unordered_set<std::uint64_t> pending_friend_name_queries_;
  std::unordered_set<std::uint64_t> pending_ignore_name_queries_;
  std::unordered_set<std::uint64_t> pending_mute_name_queries_;
  std::vector<PendingSocialNameResolution> pending_social_name_resolutions_;
  ReferAFriendRuntime refer_a_friend_runtime_;
  net::LatencyTracker latency_tracker_;
  core::ClientCrtRandom client_random_;
  float keep_alive_timer_{0.0f};
  static constexpr float kKeepAliveInterval{30.0f};
  BotDetectedProbeFn bot_detected_probe_fn_;
  ConsumeLegacyTokenSeedVerificationFn
      consume_legacy_token_seed_verification_;
  BuildBotDetectedDigestFn build_bot_detected_digest_;
  std::unordered_set<std::uint32_t> pending_quest_poi_queries_;
  bool bot_detected_world_active_{false};
  bool bot_detected_enabled_{false};
  std::uint32_t bot_detected_last_tick_ms_{0};
  std::uint32_t local_unit_regen_last_tick_ms_{0};
  std::int32_t bot_detected_countdown_{0};
  static constexpr std::int32_t kBotDetectedInitialCountdown{0x4B0};

  void HandleLoginVerifyWorld(const net::wotlk::WorldPacket &pkt);
  void BindDecomposedPacketHandlers();
  bool ApplyLoginVerifyWorld(const LoginVerifyWorld &verify);
  bool HandleUpdateObject(const net::wotlk::WorldPacket &pkt);
  bool HandleCompressedUpdateObject(const net::wotlk::WorldPacket &pkt);
  void HandleDestroyObject(const net::wotlk::WorldPacket &pkt);
  void HandleTimeSyncReq(const net::wotlk::WorldPacket &pkt);
  void HandleForceSpeedChange(const net::wotlk::WorldPacket &pkt, SpeedType type);
  void HandleMovement(const net::wotlk::WorldPacket &pkt);

  void HandleChatMessage(const net::wotlk::WorldPacket &pkt, bool is_gm);
  void HandleChannelNotify(const net::wotlk::WorldPacket &pkt);
  void HandleSpamFilterResult(const net::wotlk::WorldPacket &pkt);

  void HandleInitialSpells(const net::wotlk::WorldPacket &pkt);
  void HandleLearnedSpell(const net::wotlk::WorldPacket &pkt);
  void HandleRemovedSpell(const net::wotlk::WorldPacket &pkt);
  void HandleSupercededSpell(const net::wotlk::WorldPacket &pkt);
  void HandleSpellCooldown(const net::wotlk::WorldPacket &pkt);
  void HandleCastFailed(const net::wotlk::WorldPacket &pkt);
  void HandleSpellStart(const net::wotlk::WorldPacket &pkt);
  void HandleSpellGo(const net::wotlk::WorldPacket &pkt);

  void HandleActionButtons(const net::wotlk::WorldPacket &pkt);

  void HandleContactList(const net::wotlk::WorldPacket &pkt);
  void HandleFriendStatus(const net::wotlk::WorldPacket &pkt);

  void HandleGroupList(const net::wotlk::WorldPacket &pkt);
  void HandleGroupInvite(const net::wotlk::WorldPacket &pkt);
  void HandleGroupDecline(const net::wotlk::WorldPacket &pkt);
  void HandleGroupSetLeader(const net::wotlk::WorldPacket &pkt);
  void HandlePartyCommandResult(const net::wotlk::WorldPacket &pkt);

  void HandleQuestQueryResponse(const net::wotlk::WorldPacket &pkt);
  bool HandleQuestGiverQuestDetails(const net::wotlk::WorldPacket &pkt);
  bool HandleQuestGiverOfferReward(const net::wotlk::WorldPacket &pkt);
  bool HandleQuestGiverRequestItems(const net::wotlk::WorldPacket &pkt);
  bool HandleQuestGiverStatus(const net::wotlk::WorldPacket &pkt);
  bool HandleQuestGiverStatusMultiple(const net::wotlk::WorldPacket &pkt);
  bool HandleQuestUpdateComplete(const net::wotlk::WorldPacket &pkt);
  bool HandleQuestUpdateAddKill(const net::wotlk::WorldPacket &pkt);

  [[nodiscard]] bool RequireActivePlayerForQuestDispatch() const;
  [[nodiscard]] std::uint32_t UpdateLocalUnitRegenAndRunes(std::uint32_t current_time_ms);
  void NotifyLocalUnitManaSpellcast(const ObjectGuid &caster_guid, std::uint32_t spell_id);
  void HandleQuestConfirmAccept(const net::wotlk::WorldPacket &pkt);
  void HandleQuestLogFull(const net::wotlk::WorldPacket &pkt);

  void ResetBotDetectedCountdown();
  void TrySendLegacyTokenSeedFollowUp();
  void TickBotDetected(std::uint32_t client_time_ms);
  void RefreshWorldSceneGameTime();
  void RefreshCreatedGameObjectQuestgiverStatus(const WorldObject &obj);
  void ClearDestroyedQuestgiverStatus(const ObjectGuid &guid);
  void RefreshActivePlayerFactionDependentState();
  void RefreshQuestRuntimeFromPlayer(bool request_query_time);
  void TryBindGameObjectTemplateInfo(WorldObject &obj);
  void BindGameObjectTemplateInfoForEntry(std::uint32_t entry);
  void TryRegisterCapturePointObject(const WorldObject &obj);
  void RegisterCapturePointObjectsForEntry(std::uint32_t entry);
  void HandleCapturePointObjectDestroyed(const ObjectGuid &guid);

  void HandleTalentsInfo(const net::wotlk::WorldPacket &pkt);

  void HandleLootResponse(const net::wotlk::WorldPacket &pkt);
  void HandleLootReleaseResponse(const net::wotlk::WorldPacket &pkt);
  void HandleLootRemoved(const net::wotlk::WorldPacket &pkt);
  void HandleLootMoneyNotify(const net::wotlk::WorldPacket &pkt);

  void HandleAttackStart(const net::wotlk::WorldPacket &pkt);
  void HandleAttackStop(const net::wotlk::WorldPacket &pkt);
  void HandleSpellNonMeleeDamageLog(const net::wotlk::WorldPacket &pkt);
  void HandleSpellHealLog(const net::wotlk::WorldPacket &pkt);
  void HandleSpellEnergizeLog(const net::wotlk::WorldPacket &pkt);
  void HandleLogXpGain(const net::wotlk::WorldPacket &pkt);
  void HandleAttackerStateUpdate(const net::wotlk::WorldPacket &pkt);
  void HandlePeriodicAuraLog(const net::wotlk::WorldPacket &pkt);

  void HandleGuildQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleGuildRoster(const net::wotlk::WorldPacket &pkt);
  void HandleGuildEvent(const net::wotlk::WorldPacket &pkt);
  void HandleGuildCommandResult(const net::wotlk::WorldPacket &pkt);
  void HandleGuildInvite(const net::wotlk::WorldPacket &pkt);
  void HandleGuildPermissions(const net::wotlk::WorldPacket &pkt);
  void HandleGuildEventLogQuery(const net::wotlk::WorldPacket &pkt);
  void HandleGuildInfoPacket(const net::wotlk::WorldPacket &pkt);
  void HandleGuildDeclinePacket(const net::wotlk::WorldPacket &pkt);

  void HandleLfgJoinResult(const net::wotlk::WorldPacket &pkt);
  void HandleLfgQueueStatus(const net::wotlk::WorldPacket &pkt);
  void HandleLfgUpdatePlayer(const net::wotlk::WorldPacket &pkt);
  void HandleLfgUpdateParty(const net::wotlk::WorldPacket &pkt);
  void HandleLfgProposalUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleLfgRoleCheckUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleLfgBootProposalUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleLfgPlayerReward(const net::wotlk::WorldPacket &pkt);
  void HandleLfgTeleportDenied(const net::wotlk::WorldPacket &pkt);
  void HandleLfgOfferContinue(const net::wotlk::WorldPacket &pkt);
  void HandleLfgPlayerInfo(const net::wotlk::WorldPacket &pkt);
  void HandleLfgPartyInfo(const net::wotlk::WorldPacket &pkt);
  void HandleLfgRoleChosen(const net::wotlk::WorldPacket &pkt);
  void HandleLfgUpdateSearch(const net::wotlk::WorldPacket &pkt);
  void HandleLfgDisabled(const net::wotlk::WorldPacket &pkt);
  void HandleOpenLfgDungeonFinder(const net::wotlk::WorldPacket &pkt);
  void HandleUpdateLfgList(const net::wotlk::WorldPacket &pkt);

  void HandleAuraUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleAuraUpdateAll(const net::wotlk::WorldPacket &pkt);
  void HandleSetFlatSpellModifier(const net::wotlk::WorldPacket &pkt);
  void HandleSetPctSpellModifier(const net::wotlk::WorldPacket &pkt);
  void HandleCooldownEvent(const net::wotlk::WorldPacket &pkt);
  void HandleClearCooldown(const net::wotlk::WorldPacket &pkt);
  void HandleSendUnlearnSpells(const net::wotlk::WorldPacket &pkt);
  void HandleSpellFailure(const net::wotlk::WorldPacket &pkt);
  void HandleSpellFailedOther(const net::wotlk::WorldPacket &pkt);
  void HandleSpellDelayed(const net::wotlk::WorldPacket &pkt);
  void HandleChannelStart(const net::wotlk::WorldPacket &pkt);
  void HandleChannelUpdate(const net::wotlk::WorldPacket &pkt);

  void HandleCancelCombat(const net::wotlk::WorldPacket &pkt);
  void HandleSendAllCombatLog(const net::wotlk::WorldPacket &pkt);
  void HandleAiReaction(const net::wotlk::WorldPacket &pkt);
  void HandlePartyKillLog(const net::wotlk::WorldPacket &pkt);
  void HandleAttackSwingError(const net::wotlk::WorldPacket &pkt, AttackSwingError error);
  void HandleHealthUpdate(const net::wotlk::WorldPacket &pkt);
  void HandlePowerUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleThreatUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleHighestThreatUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleThreatRemove(const net::wotlk::WorldPacket &pkt);
  void HandleThreatClear(const net::wotlk::WorldPacket &pkt);
  void HandleLevelUpInfo(const net::wotlk::WorldPacket &pkt);
  void HandleEnvironmentalDamageLog(const net::wotlk::WorldPacket &pkt);

  void HandleAllAchievementData(const net::wotlk::WorldPacket &pkt);
  void HandleAchievementEarned(const net::wotlk::WorldPacket &pkt);
  void HandleCriteriaUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleCriteriaDeleted(const net::wotlk::WorldPacket &pkt);
  void HandleServerFirstAchievement(const net::wotlk::WorldPacket &pkt);
  void HandleInspectAchievements(const net::wotlk::WorldPacket &pkt);

  void HandlePetSpells(const net::wotlk::WorldPacket &pkt);
  void HandlePetMode(const net::wotlk::WorldPacket &pkt);
  void HandlePetActionFeedback(const net::wotlk::WorldPacket &pkt);
  void HandlePetCastFailed(const net::wotlk::WorldPacket &pkt);
  void HandlePetNameQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleStabledPets(const net::wotlk::WorldPacket &pkt);
  void HandlePetGuids(const net::wotlk::WorldPacket &pkt);

  void HandleInitWorldStates(const net::wotlk::WorldPacket &pkt);
  void HandleUpdateWorldState(const net::wotlk::WorldPacket &pkt);

  void HandleBattlefieldStatus(const net::wotlk::WorldPacket &pkt);
  void HandleBattlefieldList(const net::wotlk::WorldPacket &pkt);
  void HandlePlayerPositions(const net::wotlk::WorldPacket &pkt);
  void HandlePlayerJoined(const net::wotlk::WorldPacket &pkt);
  void HandlePlayerLeft(const net::wotlk::WorldPacket &pkt);
  void HandlePvpLogData(const net::wotlk::WorldPacket &pkt);
  void HandlePvpCredit(const net::wotlk::WorldPacket &pkt);
  void HandleArenaTeamRoster(const net::wotlk::WorldPacket &pkt);
  void HandleArenaTeamCommandResult(const net::wotlk::WorldPacket &pkt);
  void HandleArenaTeamStats(const net::wotlk::WorldPacket &pkt);
  void HandleArenaTeamInvite(const net::wotlk::WorldPacket &pkt);

  void HandleWeather(const net::wotlk::WorldPacket &pkt);
  void HandleBindPointUpdate(const net::wotlk::WorldPacket &pkt);
  void HandlePlayerBound(const net::wotlk::WorldPacket &pkt);
  void HandlePlayedTime(const net::wotlk::WorldPacket &pkt);
  void HandleWho(const net::wotlk::WorldPacket &pkt);
  void HandleMotd(const net::wotlk::WorldPacket &pkt);
  void HandleTutorialFlags(const net::wotlk::WorldPacket &pkt);
  void HandleDuelRequested(const net::wotlk::WorldPacket &pkt);
  void HandleDuelWinner(const net::wotlk::WorldPacket &pkt);
  void HandleDuelComplete(const net::wotlk::WorldPacket &pkt);
  void HandleEmote(const net::wotlk::WorldPacket &pkt);
  void HandleTextEmote(const net::wotlk::WorldPacket &pkt);
  void HandleNotification(const net::wotlk::WorldPacket &pkt);
  void HandleExplorationExperience(const net::wotlk::WorldPacket &pkt);
  void HandleEquipmentSetList(const net::wotlk::WorldPacket &pkt);
  void HandleDeathReleaseLoc(const net::wotlk::WorldPacket &pkt);
  void HandleCorpseReclaimDelay(const net::wotlk::WorldPacket &pkt);

  void HandleNameQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleCreatureQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleGameObjectQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleItemQuerySingleResponse(const net::wotlk::WorldPacket &pkt);
  void HandleItemQueryResponseSideEffects(std::uint32_t response_entry);

  void HandleInitializeFactions(const net::wotlk::WorldPacket &pkt);
  void HandleSetFactionStanding(const net::wotlk::WorldPacket &pkt);
  void HandleSetFactionVisible(const net::wotlk::WorldPacket &pkt);

  void SyncFactionsToFactionSystem();

  void HandleLoginSetTimeSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleGameTimeSet(const net::wotlk::WorldPacket &pkt);
  void HandleGameSpeedSet(const net::wotlk::WorldPacket &pkt);
  void HandleTransferPending(const net::wotlk::WorldPacket &pkt);
  void HandleNewWorld(const net::wotlk::WorldPacket &pkt);
  void HandleForceMoveRoot(const net::wotlk::WorldPacket &pkt);
  void HandleForceMoveUnroot(const net::wotlk::WorldPacket &pkt);
  void HandleMoveKnockBack(const net::wotlk::WorldPacket &pkt);
  void HandleMoveSetCanFly(const net::wotlk::WorldPacket &pkt);
  void HandleMoveUnsetCanFly(const net::wotlk::WorldPacket &pkt);
  void HandleStartMirrorTimer(const net::wotlk::WorldPacket &pkt);
  void HandleStopMirrorTimer(const net::wotlk::WorldPacket &pkt);
  void HandleSetProficiency(const net::wotlk::WorldPacket &pkt);
  void HandleStandStateUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleUpdateComboPoints(const net::wotlk::WorldPacket &pkt);
  void HandlePlaySound(const net::wotlk::WorldPacket &pkt);

  [[nodiscard]] bool HasMapDifficultyRaidDuration(std::uint32_t map_id,
                                                   std::uint8_t difficulty) const;
  void UpdateResetInstanceVisibilityForMapTransition(std::uint32_t previous_map_id,
                                                     std::uint32_t new_map_id);
  [[nodiscard]] bool ResolveWorldTransferMap(std::uint32_t map_id,
                                             std::string *map_internal_name) const;
  void ResetRuntimeForWorldTransfer();
  void DispatchWorldTransfer(const WorldTransferRequest &request);

  void HandleMonsterMove(const net::wotlk::WorldPacket &pkt);
  void HandleMonsterMoveTransport(const net::wotlk::WorldPacket &pkt);
  void HandleCompressedMoves(const net::wotlk::WorldPacket &pkt);

  void FeedSplineManager(const MonsterMoveInfo &info);

  void HandlePartyMemberStats(const net::wotlk::WorldPacket &pkt);
  void HandlePartyMemberStatsFull(const net::wotlk::WorldPacket &pkt);

  void HandleShowTaxiNodes(const net::wotlk::WorldPacket &pkt);
  void HandleActivateTaxiReply(const net::wotlk::WorldPacket &pkt);
  void HandleNewTaxiPath(const net::wotlk::WorldPacket &pkt);
  void HandleTaxiNodeStatus(const net::wotlk::WorldPacket &pkt);

  void DisplayVoiceChatSystemMessage(int error_index);
  void HandleAvailableVoiceChannel(const net::wotlk::WorldPacket &pkt);
  void HandleVoiceParentalControls(const net::wotlk::WorldPacket &pkt);
  void HandleVoiceChatStatus(const net::wotlk::WorldPacket &pkt);
  void HandleVoiceSetTalkerMuted(const net::wotlk::WorldPacket &pkt);
  void HandleVoiceSessionRosterUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleVoiceSessionLeave(const net::wotlk::WorldPacket &pkt);
  void HandlePhaseShift(const net::wotlk::WorldPacket &pkt);
  void HandleInstanceDifficulty(const net::wotlk::WorldPacket &pkt);
  void HandleTransferAborted(const net::wotlk::WorldPacket &pkt);
  void HandleQueryTimeResponse(const net::wotlk::WorldPacket &pkt);
  void HandleGossipComplete(const net::wotlk::WorldPacket &pkt);
  void HandleGossipPoi(const net::wotlk::WorldPacket &pkt);
  void HandleCorpseQuery(const net::wotlk::WorldPacket &pkt);

  void ResetAndRequeryCorpsePosition();

  void RefreshActivePlayerCorpseMinimapMarker();

  void NotifyCorpsePositionCleared();
  void HandleRandomRoll(const net::wotlk::WorldPacket &pkt);
  bool HandleQuestGiverQuestComplete(const net::wotlk::WorldPacket &pkt);
  bool HandleQuestGiverQuestList(const net::wotlk::WorldPacket &pkt);
  void HandleResurrectRequest(const net::wotlk::WorldPacket &pkt);
  void HandleShowBank(const net::wotlk::WorldPacket &pkt);
  void HandleClientControlUpdate(const net::wotlk::WorldPacket &pkt);

  void ApplyStoredClientControl();
  void HandleCancelAutoRepeat(const net::wotlk::WorldPacket &pkt);
  void HandleDismount(const net::wotlk::WorldPacket &pkt);
  void HandleRealmSplit(const net::wotlk::WorldPacket &pkt);
  void HandleFeatureSystemStatus(const net::wotlk::WorldPacket &pkt);

  void HandleMoveTeleport(const net::wotlk::WorldPacket &pkt);
  void HandleMoveWaterWalk(const net::wotlk::WorldPacket &pkt);
  void HandleMoveLandWalk(const net::wotlk::WorldPacket &pkt);
  void HandleMoveFeatherFall(const net::wotlk::WorldPacket &pkt);
  void HandleMoveNormalFall(const net::wotlk::WorldPacket &pkt);
  void HandleLogoutCancelAck(const net::wotlk::WorldPacket &pkt);
  void HandleMoveSetRunSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleMoveSetRunBackSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleMoveSetWalkSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleMoveSetSwimSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleMoveSetFlightSpeed(const net::wotlk::WorldPacket &pkt);

  void HandleConvertRune(const net::wotlk::WorldPacket &pkt);
  void HandleResyncRunes(const net::wotlk::WorldPacket &pkt);
  void HandleAddRunePower(const net::wotlk::WorldPacket &pkt);

  void HandlePlaySpellVisual(const net::wotlk::WorldPacket &pkt);
  void HandlePlaySpellImpact(const net::wotlk::WorldPacket &pkt);
  void HandleTriggerCinematic(const net::wotlk::WorldPacket &pkt);
  void HandleTriggerMovie(const net::wotlk::WorldPacket &pkt);

  void HandleInvalidatePlayer(const net::wotlk::WorldPacket &pkt);

  void HandleSummonRequest(const net::wotlk::WorldPacket &pkt);
  void HandleRaidTargetUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleRaidReadyCheck(const net::wotlk::WorldPacket &pkt);
  void HandleRaidReadyCheckConfirm(const net::wotlk::WorldPacket &pkt);
  void HandleRaidReadyCheckFinished(const net::wotlk::WorldPacket &pkt);
  void HandlePartyAssignment(const net::wotlk::WorldPacket &pkt);

  void HandleMoveSetHover(const net::wotlk::WorldPacket &pkt);
  void HandleMoveUnsetHover(const net::wotlk::WorldPacket &pkt);
  void HandleMoveSetCanSwimFlyTransition(const net::wotlk::WorldPacket &pkt);
  void HandleMoveUnsetCanSwimFlyTransition(const net::wotlk::WorldPacket &pkt);
  void HandleSplineSetRunSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveRoot(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveUnroot(const net::wotlk::WorldPacket &pkt);

  void HandleSpellDispelLog(const net::wotlk::WorldPacket &pkt);
  void HandleSpellStealLog(const net::wotlk::WorldPacket &pkt);
  void HandleSpellDamageShield(const net::wotlk::WorldPacket &pkt);
  void HandleSpellLogMiss(const net::wotlk::WorldPacket &pkt);
  void HandleSpellInstaKillLog(const net::wotlk::WorldPacket &pkt);
  void HandleSpellOrDamageImmune(const net::wotlk::WorldPacket &pkt);
  void HandleDispelFailed(const net::wotlk::WorldPacket &pkt);
  void HandleModifyCooldown(const net::wotlk::WorldPacket &pkt);
  void HandleSpellLogExecute(const net::wotlk::WorldPacket &pkt);

  void HandleSetDungeonDifficulty(const net::wotlk::WorldPacket &pkt);
  void HandleSetRaidDifficulty(const net::wotlk::WorldPacket &pkt);
  void HandleRaidInstanceInfo(const net::wotlk::WorldPacket &pkt);
  void HandleInstanceReset(const net::wotlk::WorldPacket &pkt);
  void HandleInstanceResetFailed(const net::wotlk::WorldPacket &pkt);
  void HandleEncounterUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleRaidGroupOnly(const net::wotlk::WorldPacket &pkt);

  void HandleInspectTalent(const net::wotlk::WorldPacket &pkt);
  void HandleInspectHonorStats(const net::wotlk::WorldPacket &pkt);
  void HandleTitleEarned(const net::wotlk::WorldPacket &pkt);
  void HandleEnableBarberShop(const net::wotlk::WorldPacket &pkt);
  void HandleBarberShopResult(const net::wotlk::WorldPacket &pkt);
  void HandleMinimapPing(const net::wotlk::WorldPacket &pkt);

  void HandleGuildBankList(const net::wotlk::WorldPacket &pkt);
  void HandleGuildBankLogQuery(const net::wotlk::WorldPacket &pkt);
  void HandleGuildBankMoneyWithdrawn(const net::wotlk::WorldPacket &pkt);
  void HandleQueryGuildBankText(const net::wotlk::WorldPacket &pkt);

  void HandleCalendarSendCalendar(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarSendEvent(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarSendNumPending(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarCommandResult(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventInvite(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventInviteAlert(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventStatus(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarRaidLockoutAdded(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventInviteRemoved(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventInviteRemovedAlert(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventInviteStatusAlert(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventModeratorStatusAlert(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventRemovedAlert(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventUpdatedAlert(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarClearPendingAction(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarFilterGuild(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarArenaTeam(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarRaidLockoutRemoved(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarRaidLockoutUpdated(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventInviteNotes(const net::wotlk::WorldPacket &pkt);
  void HandleCalendarEventInviteNotesAlert(const net::wotlk::WorldPacket &pkt);
  void
  FinalizeCalendarInviteLookupCompletion(std::uint64_t event_id,
                                         CalendarInviteLookupCompletionAction completion_action);
  void ReplaceCalendarEventAlarms(const std::vector<CalendarSystemEvent> &events);
  void SyncCalendarEventAlarm(const CalendarSystemEvent &event);
  void RemoveCalendarEventAlarm(std::uint64_t event_id);
  void ClearCalendarEventAlarms();

  static bool DispatchCalendarEventAlarm(const GameTimeCallbackMoment &current_time, void *context);
  bool HandleCalendarEventAlarmCallback(std::uint64_t event_id,
                                        GameTimeCallbackRegistry::Handle expected_handle,
                                        const GameTimeCallbackMoment &current_time);

  void HandlePetitionShowList(const net::wotlk::WorldPacket &pkt);
  void HandlePetitionShowSignatures(const net::wotlk::WorldPacket &pkt);
  void HandlePetitionSignResults(const net::wotlk::WorldPacket &pkt);
  void HandlePetitionQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleTurnInPetitionResults(const net::wotlk::WorldPacket &pkt);
  void HandleSaveGuildEmblem(const net::wotlk::WorldPacket &pkt);
  void HandleTabardVendorActivate(const net::wotlk::WorldPacket &pkt);
  void HandlePetitionDecline(const net::wotlk::WorldPacket &pkt);
  void HandlePetitionRename(const net::wotlk::WorldPacket &pkt);
  void HandleOfferPetitionError(const net::wotlk::WorldPacket &pkt);

  void HandlePlayerVehicleData(const net::wotlk::WorldPacket &pkt);
  void HandleForceSetVehicleRecId(const net::wotlk::WorldPacket &pkt);
  void HandleCancelExpectedRideVehicleAura(const net::wotlk::WorldPacket &pkt);

  void HandleArenaTeamQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleArenaTeamEvent(const net::wotlk::WorldPacket &pkt);
  void HandleInspectResultsUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleInspectArenaTeams(const net::wotlk::WorldPacket &pkt);
  void HandleArenaError(const net::wotlk::WorldPacket &pkt);
  void HandleArenaTeamChangeFailedQueued(const net::wotlk::WorldPacket &pkt);
  void HandleArenaUnitDestroyed(const net::wotlk::WorldPacket &pkt);
  void HandleJoinedBattlegroundQueue(const net::wotlk::WorldPacket &pkt);
  void HandleBattlefieldPortDenied(const net::wotlk::WorldPacket &pkt);
  void HandleBattlegroundInfoThrottled(const net::wotlk::WorldPacket &pkt);
  void HandleRemovedFromPvpQueue(const net::wotlk::WorldPacket &pkt);
  void HandleReportPvpAfkResult(const net::wotlk::WorldPacket &pkt);

  void HandleForceSwimBackSpeedChange(const net::wotlk::WorldPacket &pkt);
  void HandleForceFlightBackSpeedChange(const net::wotlk::WorldPacket &pkt);
  void HandleForcePitchRateChange(const net::wotlk::WorldPacket &pkt);

  void HandleSplineSetWalkSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleSplineSetSwimSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleSplineSetFlightSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleSplineSetRunBackSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleSplineSetSwimBackSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleSplineSetTurnRate(const net::wotlk::WorldPacket &pkt);
  void HandleSplineSetFlightBackSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleSplineSetPitchRate(const net::wotlk::WorldPacket &pkt);

  void HandleFlightSplineSync(const net::wotlk::WorldPacket &pkt);
  void HandleMoveTimeSkipped(const net::wotlk::WorldPacket &pkt);
  void HandleMoveSetPitch(const net::wotlk::WorldPacket &pkt);
  void HandleMoveStartPitchUp(const net::wotlk::WorldPacket &pkt);
  void HandleMoveStartPitchDown(const net::wotlk::WorldPacket &pkt);
  void HandleMoveStopPitch(const net::wotlk::WorldPacket &pkt);

  void HandleMsgMoveRoot(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveUnroot(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveKnockBack(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveTeleportAck(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveWorldportAck(const net::wotlk::WorldPacket &pkt);

  void HandleMsgMoveFeatherFall(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveHover(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveWaterWalk(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveUpdateCanFly(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveSetRunMode(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveSetWalkMode(const net::wotlk::WorldPacket &pkt);

  void HandleMsgMoveSetSwimBackSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveSetTurnRate(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveSetFlightBackSpeed(const net::wotlk::WorldPacket &pkt);
  void HandleMsgMoveSetPitchRate(const net::wotlk::WorldPacket &pkt);

  void HandleSplineMoveFeatherFall(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveNormalFall(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveSetHover(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveUnsetHover(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveWaterWalk(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveLandWalk(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveStartSwim(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveStopSwim(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveSetRunMode(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveSetWalkMode(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveSetFlying(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveUnsetFlying(const net::wotlk::WorldPacket &pkt);

  void HandleLootItemNotify(const net::wotlk::WorldPacket &pkt);
  void HandleLootList(const net::wotlk::WorldPacket &pkt);
  void HandleLootMasterList(const net::wotlk::WorldPacket &pkt);
  void HandleLootSlotChanged(const net::wotlk::WorldPacket &pkt);

  bool HandleQuestPoiQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleQuestPushResult(const net::wotlk::WorldPacket &pkt);
  void HandleQuestGiverQuestFailed(const net::wotlk::WorldPacket &pkt);
  void HandleQuestUpdateFailed(const net::wotlk::WorldPacket &pkt);
  void HandleQuestUpdateAddPvpKill(const net::wotlk::WorldPacket &pkt);

  void HandleGroupDestroyed(const net::wotlk::WorldPacket &pkt);
  void HandleGroupUninvite(const net::wotlk::WorldPacket &pkt);
  void HandleRealGroupUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleGroupActionThrottled(const net::wotlk::WorldPacket &pkt);
  void HandleGroupCancel(const net::wotlk::WorldPacket &pkt);

  void HandleBattlefieldMgrEntryInvite(const net::wotlk::WorldPacket &pkt);
  void HandleBattlefieldMgrEntered(const net::wotlk::WorldPacket &pkt);
  void HandleBattlefieldMgrQueueInvite(const net::wotlk::WorldPacket &pkt);
  void HandleGroupJoinedBattleground(const net::wotlk::WorldPacket &pkt);
  void HandleBattlefieldMgrEjected(const net::wotlk::WorldPacket &pkt);
  void HandleBattlefieldMgrEjectPending(const net::wotlk::WorldPacket &pkt);
  void HandleBattlefieldMgrQueueRequestResponse(const net::wotlk::WorldPacket &pkt);
  void HandleBattlefieldMgrStateChange(const net::wotlk::WorldPacket &pkt);

  void HandleLootClearMoney(const net::wotlk::WorldPacket &pkt);

  void HandleDuelCountdown(const net::wotlk::WorldPacket &pkt);
  void HandleDuelOutOfBounds(const net::wotlk::WorldPacket &pkt);
  void HandleDuelInBounds(const net::wotlk::WorldPacket &pkt);
  void HandleDurabilityDamageDeath(const net::wotlk::WorldPacket &pkt);
  void HandlePlayMusic(const net::wotlk::WorldPacket &pkt);
  void HandlePlayObjectSound(const net::wotlk::WorldPacket &pkt);

  void HandleChatPlayerNotFound(const net::wotlk::WorldPacket &pkt);
  void HandleChannelList(const net::wotlk::WorldPacket &pkt);
  void TryDisplayPendingChannelInvite(std::uint64_t inviter_guid);
  void RetryPendingWatchedChannelRosterForGuid(const ObjectGuid &guid, bool drop_if_unresolved);

  void HandleChatWrongFaction(const net::wotlk::WorldPacket &pkt);
  void HandleChatServerMessage(const net::wotlk::WorldPacket &pkt);
  void HandleChatNotInParty(const net::wotlk::WorldPacket &pkt);
  void HandleChatRestricted(const net::wotlk::WorldPacket &pkt);
  void HandleDefenseMessage(const net::wotlk::WorldPacket &pkt);
  void HandleChatPlayerAmbiguous(const net::wotlk::WorldPacket &pkt);
  void HandleChannelMemberCount(const net::wotlk::WorldPacket &pkt);

  void HandleGameObjectCustomAnim(const net::wotlk::WorldPacket &pkt);
  void HandleGameObjectDespawnAnim(const net::wotlk::WorldPacket &pkt);
  void HandleGameObjectResetState(const net::wotlk::WorldPacket &pkt);
  void HandleGameObjectPageText(const net::wotlk::WorldPacket &pkt);
  void HandleAreaTriggerMessage(const net::wotlk::WorldPacket &pkt);
  void HandleZoneUnderAttack(const net::wotlk::WorldPacket &pkt);
  void HandleForcedDeathUpdate(const net::wotlk::WorldPacket &pkt);
  void HandlePreResurrect(const net::wotlk::WorldPacket &pkt);
  void HandleFeignDeathResisted(const net::wotlk::WorldPacket &pkt);
  void HandleCameraShake(const net::wotlk::WorldPacket &pkt);

  void HandleTotemCreated(const net::wotlk::WorldPacket &pkt);
  void HandleResumeCastBar(const net::wotlk::WorldPacket &pkt);
  void HandleTalentWipeConfirm(const net::wotlk::WorldPacket &pkt);
  void HandleSummonCancel(const net::wotlk::WorldPacket &pkt);
  void HandleSpellUpdateChainTargets(const net::wotlk::WorldPacket &pkt);
  void HandleNotifyDestLocSpellCast(const net::wotlk::WorldPacket &pkt);
  void HandlePetLearnedSpell(const net::wotlk::WorldPacket &pkt);
  void HandlePetUnlearnedSpell(const net::wotlk::WorldPacket &pkt);

  void HandleInstanceLockWarning(const net::wotlk::WorldPacket &pkt);
  void HandleInstanceSaveCreated(const net::wotlk::WorldPacket &pkt);
  void HandleUpdateLastInstance(const net::wotlk::WorldPacket &pkt);
  void HandleUpdateInstanceOwnership(const net::wotlk::WorldPacket &pkt);
  void HandleRaidReadyCheckError(const net::wotlk::WorldPacket &pkt);

  void HandlePetTameFailure(const net::wotlk::WorldPacket &pkt);
  void HandlePetNameInvalid(const net::wotlk::WorldPacket &pkt);
  void HandlePetBroken(const net::wotlk::WorldPacket &pkt);
  void HandlePetActionSound(const net::wotlk::WorldPacket &pkt);
  void HandlePetDismissSound(const net::wotlk::WorldPacket &pkt);

  void HandleBreakTarget(const net::wotlk::WorldPacket &pkt);
  void HandleClearTarget(const net::wotlk::WorldPacket &pkt);
  void HandleForceDisplayUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleResurrectFailed(const net::wotlk::WorldPacket &pkt);
  void HandleSpiritHealerConfirm(const net::wotlk::WorldPacket &pkt);
  void HandleAreaSpiritHealerTime(const net::wotlk::WorldPacket &pkt);
  void HandleDestructibleBuildingDamage(const net::wotlk::WorldPacket &pkt);

  void HandlePageTextQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleDanceManagement(const net::wotlk::WorldPacket &pkt);
  void HandlePlayDance(const net::wotlk::WorldPacket &pkt);
  void HandleStopDance(const net::wotlk::WorldPacket &pkt);
  void HandleDanceQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleInvalidateDance(const net::wotlk::WorldPacket &pkt);
  void HandleLearnedDanceMoves(const net::wotlk::WorldPacket &pkt);
  void HandlePauseMirrorTimer(const net::wotlk::WorldPacket &pkt);
  void HandleOverrideLight(const net::wotlk::WorldPacket &pkt);
  void HandleSetForcedReactions(const net::wotlk::WorldPacket &pkt);
  void HandleMirrorImageData(const net::wotlk::WorldPacket &pkt);

  void HandleMountResult(const net::wotlk::WorldPacket &pkt);
  void HandleDismountResult(const net::wotlk::WorldPacket &pkt);
  void HandleMountSpecialAnim(const net::wotlk::WorldPacket &pkt);
  void HandleFishEscaped(const net::wotlk::WorldPacket &pkt);
  void HandleFishNotHooked(const net::wotlk::WorldPacket &pkt);
  void HandleBinderConfirm(const net::wotlk::WorldPacket &pkt);
  void HandleBindZoneReply(const net::wotlk::WorldPacket &pkt);
  void HandlePlayerBindError(const net::wotlk::WorldPacket &pkt);
  void HandleCrossedInebriationThreshold(const net::wotlk::WorldPacket &pkt);
  void HandleSetFactionAtWar(const net::wotlk::WorldPacket &pkt);
  void HandlePlayerSkinned(const net::wotlk::WorldPacket &pkt);
  void HandleTalentsInvoluntarilyReset(const net::wotlk::WorldPacket &pkt);
  void HandleToggleXpGain(const net::wotlk::WorldPacket &pkt);

  void HandleQuestForceRemove(const net::wotlk::WorldPacket &pkt);
  void HandleQuestgiverQuestInvalid(const net::wotlk::WorldPacket &pkt);
  void HandleQuestUpdateAddItem(const net::wotlk::WorldPacket &pkt);
  void HandleQuestUpdateFailedTimer(const net::wotlk::WorldPacket &pkt);
  void HandleQueryQuestsCompleted(const net::wotlk::WorldPacket &pkt);

  void HandleCombatEventFailed(const net::wotlk::WorldPacket &pkt);
  void HandleProcResist(const net::wotlk::WorldPacket &pkt);
  void HandleSpellBreakLog(const net::wotlk::WorldPacket &pkt);
  void HandleAuraCastLog(const net::wotlk::WorldPacket &pkt);
  void HandleResetRangedCombatTimer(const net::wotlk::WorldPacket &pkt);
  void HandleSetProjectilePosition(const net::wotlk::WorldPacket &pkt);

  void HandlePetUnlearnConfirm(const net::wotlk::WorldPacket &pkt);
  void HandleStableResult(const net::wotlk::WorldPacket &pkt);
  void HandlePetRenameable(const net::wotlk::WorldPacket &pkt);
  void HandlePetUpdateComboPoints(const net::wotlk::WorldPacket &pkt);

  void HandleDynamicDropRollResult(const net::wotlk::WorldPacket &pkt);

  void HandleItemNameQueryResponse(const net::wotlk::WorldPacket &pkt);
  void HandleItemQueryMultipleResponse(const net::wotlk::WorldPacket &pkt);

  void HandleMoveGravityDisable(const net::wotlk::WorldPacket &pkt);
  void HandleMoveGravityEnable(const net::wotlk::WorldPacket &pkt);
  void HandleMoveSetCollisionHgt(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveGravityDisable(const net::wotlk::WorldPacket &pkt);
  void HandleSplineMoveGravityEnable(const net::wotlk::WorldPacket &pkt);
  void HandleMoveGravityChng(const net::wotlk::WorldPacket &pkt);
  void HandleMoveSetCollisionHgtAck(const net::wotlk::WorldPacket &pkt);
  void HandleMoveUpdateCanTransitionSwimFly(const net::wotlk::WorldPacket &pkt);
  void HandleMultipleMoves(const net::wotlk::WorldPacket &pkt);

  void HandleRaidInstanceMessage(const net::wotlk::WorldPacket &pkt);
  void HandleResetFailedNotify(const net::wotlk::WorldPacket &pkt);
  void HandleViewPhaseShift(const net::wotlk::WorldPacket &pkt);

  void HandleGMTicketSystemStatus(const net::wotlk::WorldPacket &pkt);
  void HandleGMTicketGetTicket(const net::wotlk::WorldPacket &pkt);
  void HandleGMTicketCreate(const net::wotlk::WorldPacket &pkt);
  void HandleGMTicketStatusUpdate(const net::wotlk::WorldPacket &pkt);
  void HandleGMResponseReceived(const net::wotlk::WorldPacket &pkt);
  void HandleGMResponseStatusUpdate(const net::wotlk::WorldPacket &pkt);

  void HandleGMResponseCreateTicket(const net::wotlk::WorldPacket &pkt);
  void HandleGMResponseDbError(const net::wotlk::WorldPacket &pkt);
  void HandleGMTicketDeleteTicket(const net::wotlk::WorldPacket &pkt);
  void HandleGMTicketUpdateText(const net::wotlk::WorldPacket &pkt);

  void HandleKickReason(const net::wotlk::WorldPacket &pkt);

  void ClearWorldPacketState();

  void HandleNpcWontTalk(const net::wotlk::WorldPacket &pkt);
  void HandleDelayGhostTeleport(const net::wotlk::WorldPacket &pkt);
  void HandleClearFarSightImmediate(const net::wotlk::WorldPacket &pkt);
  void HandleCorpseMapPositionResponse(const net::wotlk::WorldPacket &pkt);
  void HandleCorpseNotInInstance(const net::wotlk::WorldPacket &pkt);
  void HandleGhosteeGone(const net::wotlk::WorldPacket &pkt);
  void HandleOpenContainer(const net::wotlk::WorldPacket &pkt);
  void HandlePlayTimeWarning(const net::wotlk::WorldPacket &pkt);
  void HandleProposeLevelGrant(const net::wotlk::WorldPacket &pkt);
  void HandleReferAFriendExpired(const net::wotlk::WorldPacket &pkt);
  void HandleReferAFriendFailure(const net::wotlk::WorldPacket &pkt);
  void HandleInvalidPromotionCode(const net::wotlk::WorldPacket &pkt);
  void HandleWorldStateTimerUpdate(const net::wotlk::WorldPacket &pkt);

  void HandleAchievementDeleted(const net::wotlk::WorldPacket &pkt);

  void HandleChangeDifficultyResult(const net::wotlk::WorldPacket &pkt);
  void HandleDeclinedNamesResult(const net::wotlk::WorldPacket &pkt);
  void HandleGameTimeUpdate(const net::wotlk::WorldPacket &pkt);

  void HandleNotifyPartySquelch(const net::wotlk::WorldPacket &pkt);
  void HandleEchoPartySquelch(const net::wotlk::WorldPacket &pkt);
  void HandleComplainResult(const net::wotlk::WorldPacket &pkt);
  void HandleUserlistAdd(const net::wotlk::WorldPacket &pkt);
  void HandleUserlistRemove(const net::wotlk::WorldPacket &pkt);
  void HandleUserlistUpdate(const net::wotlk::WorldPacket &pkt);
  void DispatchIncomingChatMessage(ChatMessage msg);
  void EmitChannelMessage(ChatMsg type, const std::string &message, const std::string &channel_name,
                          const std::string &sender_name = {}, std::uint64_t sender_guid = 0,
                          std::uint64_t receiver_guid = 0);
  [[nodiscard]] std::string ResolveImmediateChatParticipantName(const ObjectGuid &guid) const;
  [[nodiscard]] bool TryResolveChatDisplayMessage(ChatMessage &msg, bool request_resolution);
  void QueueOrDispatchChatMessage(ChatMessage msg, bool dispatch_as_incoming);
  void DispatchIncomingTextEmote(const PendingIncomingTextEmote &emote);
  [[nodiscard]] bool TryResolveIncomingTextEmote(PendingIncomingTextEmote &emote,
                                                 bool request_resolution);
  void RetryPendingReferAFriendFailures(std::uint64_t guid, bool name_unknown);
  void RetryPendingChatMessagesForGuid(const ObjectGuid &guid, bool drop_if_unresolved);
  void RetryPendingChatMessagesForCreatureEntry(std::uint32_t entry, bool drop_if_unresolved);
  void RetryPendingTextEmotesForGuid(const ObjectGuid &guid, bool drop_if_unresolved);
  void RetryPendingTextEmotesForCreatureEntry(std::uint32_t entry, bool drop_if_unresolved);
  void RetryPendingChannelListsForGuid(const ObjectGuid &guid, bool drop_if_unresolved);
  void SuspendIncomingChatDelivery();
  void ResumeIncomingChatDelivery();
  void FlushResolvedPendingChatDelivery();
  void ClearPendingChatMessages();
  [[nodiscard]] std::string ResolveImmediateChannelListMemberName(std::uint64_t raw_guid) const;
  [[nodiscard]] bool TryDisplayPendingChannelList(const PendingChannelListDisplay &pending);
  [[nodiscard]] std::vector<GroupSystemMember> BuildObservedGroupSystemMembers(
      bool *has_deferred_local_raid_member = nullptr) const;
  void SyncObservedGroupStateToGroupSystem();
  void ApplyPetitionUiTransition(const PetitionUiTransition &transition);

public:

  bool Send(const net::wotlk::WorldPacket &pkt);

  [[nodiscard]] bool RequestActivePlayerArenaRoster(std::uint8_t slot);

  void RefreshGameObjectDifficultyVisibility();

  void RequestRaidDifficultyChange(std::uint32_t difficulty);

  [[nodiscard]] const data::dbc::MapDifficultyEntry *
  LookupMapDifficultyEntry(std::uint32_t map_id, std::uint8_t difficulty) const;

private:

  void OnLocalPlayerCreated(const ObjectGuid &guid);

  void RebindActivePlayerDescriptorCallbacks(const ObjectGuid &guid);
  void BootstrapCommentatorEnterWorld();
  void SyncActivePlayerArenaTeams(bool fire_update_event);
  void RegisterActivePlayerArenaTeamRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerArenaTeamRefresh();
  void RegisterActivePlayerCharacterPointsRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerCharacterPointsRefresh();
  void RegisterActivePlayerNoReagentCostRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerNoReagentCostRefresh();
  void RegisterActivePlayerAmmoInventoryRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerAmmoInventoryRefresh();
  void RegisterActivePlayerBuybackRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerBuybackRefresh();
  void RegisterActivePlayerRegenRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerRegenRefresh();
  void RegisterActivePlayerBankBagSlotCountRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerBankBagSlotCountRefresh();
  void RegisterActivePlayerGlyphRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerGlyphRefresh();
  void RegisterActivePlayerPetSpellPowerRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerPetSpellPowerRefresh();
  void RegisterActivePlayerCombatRatingRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerCombatRatingRefresh();
  void RegisterActivePlayerDailyQuestRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerDailyQuestRefresh();
  void RegisterActivePlayerPushPlayerEvents(const ObjectGuid &guid);
  void UnregisterActivePlayerPushPlayerEvents();
  void RegisterActivePlayerFieldBytes2Refresh(const ObjectGuid &guid);
  void UnregisterActivePlayerFieldBytes2Refresh();
  void RegisterActivePlayerRestStateRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerRestStateRefresh();
  void RegisterActivePlayerCurrencyRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerCurrencyRefresh();
  void RegisterActivePlayerShapeshiftFormRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerShapeshiftFormRefresh();
  void RegisterActivePlayerControlGuidRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerControlGuidRefresh();
  void RegisterActivePlayerCritterRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerCritterRefresh();
  void RegisterActivePlayerCoinageRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerCoinageRefresh();
  void RegisterActivePlayerSkillRefresh(const ObjectGuid &guid);
  void UnregisterActivePlayerSkillRefresh();

  void OnFieldsChanged(const WorldObject &obj, const FieldUpdateBatch &updates, bool is_create);

  friend class InteractionSender;
  friend class ChatSender;
  friend class CGUnit_C;
  friend class CGPlayer_C;

  friend class VehiclePassengerC;
  friend struct WorldSessionTestAccess;

  std::vector<net::wotlk::MainThreadPacketDispatcher::Registration>
      decomposed_packet_registrations_;
  std::shared_ptr<void> lifetime_token_{std::make_shared<int>(0)};
};

[[nodiscard]] inline bool CGPlayer_C_IsLoading(const WorldSession& session) {
  return session.HasPendingTriggerCinematic();
}

}
