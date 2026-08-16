#include "openwow/game/character_world_runtime.h"

#include "openwow/data/db_cache_instances.h"
#include "openwow/game/character_map_runtime.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/pvp_info.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/realm_runtime.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/transport_manager.h"
#include "openwow/game/world_session.h"
#include "openwow/net/transport/packet_queue.h"
#include "openwow/render/m2/m2_system.h"

#include <span>
#include <utility>

namespace openwow::game {

CharacterWorldRuntime::CharacterWorldRuntime(
    openwow::data::DBCacheRuntime& db_cache_runtime,
    RealmRuntime& realm_runtime,
    ItemDefinitions& item_definitions,
    openwow::render::m2::M2System& m2_system,
    const openwow::data::dbc::DbcLoader& dbc_loader,
    const SpellbookSystem& spellbook,
    const PvPInfo& pvp,
    const ReputationInfo& reputation,
    openwow::audio::SoundRuntime& sound_runtime)
    : db_cache_runtime_(db_cache_runtime),
      realm_runtime_(realm_runtime),
      item_definitions_(item_definitions),
      m2_system_(m2_system),
      dbc_loader_(dbc_loader),
      spellbook_(spellbook),
      pvp_(pvp),
      reputation_(reputation),
      sound_runtime_(sound_runtime) {}

CharacterWorldRuntime::~CharacterWorldRuntime() {
  Destroy();
}

WorldSession& CharacterWorldRuntime::CreateSession(
    std::function<void(std::uint32_t)> client_cache_version_callback) {
  Destroy();
  spell_cast_runtime_ = std::make_unique<SpellCastRuntime>();
  player_control_runtime_ = std::make_unique<PlayerControlRuntime>();
  inventory_replica_ = std::make_unique<PlayerInventoryReplica>();
  query_cache_ = std::make_unique<QueryCache>(db_cache_runtime_,
                                              item_definitions_);
  transport_manager_ = std::make_unique<TransportManager>();
  map_runtime_ = std::make_unique<CharacterMapRuntime>(
      *inventory_replica_, *player_control_runtime_, item_definitions_,
      m2_system_, dbc_loader_, *query_cache_, *transport_manager_,
      sound_runtime_);
  map_generation_ = 1;
  missile_trajectory_.Initialize();
  session_ = std::make_unique<WorldSession>(
      db_cache_runtime_, *inventory_replica_, *map_runtime_, *query_cache_,
      *transport_manager_, item_definitions_,
      dbc_loader_, missile_trajectory_, realm_runtime_.packet_dispatcher,
      *player_control_runtime_,
      [this]() {
        return realm_runtime_.warden.ConsumeLegacyTokenSeedVerification();
      },
      [this](const std::span<const std::uint8_t> probe) {
        return realm_runtime_.BuildBotDetectedDigest(probe);
      },
       spellbook_, pvp_, reputation_, *spell_cast_runtime_, sound_runtime_,
      &game_time_);
  session_->SetClientCacheVersionCallback(
      std::move(client_cache_version_callback));
  session_->SetMapGenerationReplacementCallback(
      [this](const std::uint32_t map_id) { ReplaceMapGeneration(map_id); });
  return *session_;
}

void CharacterWorldRuntime::CreatePacketQueue() {
  packet_queue_ = std::make_unique<net::PacketQueue>("CharacterWorldRuntime");
  packet_queue_->SetMaxSize(kCharacterWorldPacketQueueCapacity);
}

void CharacterWorldRuntime::ReplaceMapGeneration(const std::uint32_t map_id) {
  if (session_ == nullptr || inventory_replica_ == nullptr) {
    return;
  }

  map_runtime_->ReplaceMapGeneration(map_id);
  ++map_generation_;
}

void CharacterWorldRuntime::StopReceivingAndDestroy(
    std::function<void()> stop_receiving) {
  if (stop_receiving) {
    stop_receiving();
  }
  Destroy();
}

void CharacterWorldRuntime::Destroy() {

  packet_queue_.reset();
  session_.reset();
  missile_trajectory_.Cleanup();
  map_runtime_.reset();
  transport_manager_.reset();
  query_cache_.reset();
  inventory_replica_.reset();
  player_control_runtime_.reset();
  if (spell_cast_runtime_ != nullptr) {
    spell_cast_runtime_->Reset();
  }
  spell_cast_runtime_.reset();
  map_generation_ = 0;
}

}
