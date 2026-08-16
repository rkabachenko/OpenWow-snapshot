#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "openwow/game/character_map_runtime.h"
#include "openwow/game/missile_trajectory.h"
#include "openwow/runtime/time/game_time.h"

namespace openwow::data {
class DBCacheRuntime;
namespace dbc {
class DbcLoader;
}
}

namespace openwow::net {
class PacketQueue;
}

namespace openwow::render::m2 {
class M2System;
}
namespace openwow::audio { class SoundRuntime; }

namespace openwow::game {

class ItemDefinitions;
class CharacterMapRuntime;
class ObjectManager;
struct PlayerControlRuntime;
class PlayerInventoryReplica;
class QueryCache;
class PvPInfo;
class ReputationInfo;
class RealmRuntime;
class SpellbookSystem;
class SpellCastRuntime;
class TransportManager;
class WorldSession;

inline constexpr std::uint32_t kCharacterWorldPacketQueueCapacity = 4096;

class CharacterWorldRuntime final {
 public:
  CharacterWorldRuntime(openwow::data::DBCacheRuntime& db_cache_runtime,
                        RealmRuntime& realm_runtime,
                        ItemDefinitions& item_definitions,
                        openwow::render::m2::M2System& m2_system,
                        const openwow::data::dbc::DbcLoader& dbc_loader,
                         const SpellbookSystem& spellbook,
                         const PvPInfo& pvp,
                         const ReputationInfo& reputation,
                         openwow::audio::SoundRuntime& sound_runtime);
  ~CharacterWorldRuntime();

  CharacterWorldRuntime(const CharacterWorldRuntime&) = delete;
  CharacterWorldRuntime& operator=(const CharacterWorldRuntime&) = delete;

  WorldSession& CreateSession(
      std::function<void(std::uint32_t)> client_cache_version_callback = {});
  void CreatePacketQueue();

  void ReplaceMapGeneration(std::uint32_t map_id);
  void StopReceivingAndDestroy(std::function<void()> stop_receiving);
  void Destroy();

  [[nodiscard]] WorldSession* session() noexcept { return session_.get(); }
  [[nodiscard]] const WorldSession* session() const noexcept {
    return session_.get();
  }
  [[nodiscard]] SpellCastRuntime* spell_cast_runtime() noexcept {
    return spell_cast_runtime_.get();
  }
  [[nodiscard]] const SpellCastRuntime* spell_cast_runtime() const noexcept {
    return spell_cast_runtime_.get();
  }
  [[nodiscard]] PlayerControlRuntime* player_control_runtime() noexcept {
    return player_control_runtime_.get();
  }
  [[nodiscard]] const PlayerControlRuntime* player_control_runtime()
      const noexcept {
    return player_control_runtime_.get();
  }
  [[nodiscard]] net::PacketQueue* packet_queue() noexcept {
    return packet_queue_.get();
  }
  [[nodiscard]] const net::PacketQueue* packet_queue() const noexcept {
    return packet_queue_.get();
  }
  [[nodiscard]] ObjectManager* object_manager() noexcept {
    return map_runtime_ != nullptr ? &map_runtime_->objects() : nullptr;
  }
  [[nodiscard]] const ObjectManager* object_manager() const noexcept {
    return map_runtime_ != nullptr ? &map_runtime_->objects() : nullptr;
  }
  [[nodiscard]] std::uint64_t map_generation() const noexcept {
    return map_generation_;
  }
  [[nodiscard]] openwow::core::ida::GameTimeData& game_time() noexcept {
    return game_time_;
  }
  [[nodiscard]] const openwow::core::ida::GameTimeData& game_time() const noexcept {
    return game_time_;
  }
  [[nodiscard]] PlayerInventoryReplica* inventory_replica() noexcept {
    return inventory_replica_.get();
  }
  [[nodiscard]] const PlayerInventoryReplica* inventory_replica() const noexcept {
    return inventory_replica_.get();
  }
  [[nodiscard]] UnitMissileTrajectory_C& missile_trajectory() noexcept {
    return missile_trajectory_;
  }
  [[nodiscard]] const UnitMissileTrajectory_C& missile_trajectory()
      const noexcept {
    return missile_trajectory_;
  }

 private:
  openwow::data::DBCacheRuntime& db_cache_runtime_;
  RealmRuntime& realm_runtime_;
  ItemDefinitions& item_definitions_;
  openwow::render::m2::M2System& m2_system_;
  const openwow::data::dbc::DbcLoader& dbc_loader_;
  const SpellbookSystem& spellbook_;
  const PvPInfo& pvp_;
  const ReputationInfo& reputation_;
  openwow::audio::SoundRuntime& sound_runtime_;

  openwow::core::ida::GameTimeData game_time_{};
  std::unique_ptr<SpellCastRuntime> spell_cast_runtime_;
  std::unique_ptr<PlayerControlRuntime> player_control_runtime_;
  std::unique_ptr<PlayerInventoryReplica> inventory_replica_;
  std::unique_ptr<QueryCache> query_cache_;
  std::unique_ptr<TransportManager> transport_manager_;
  std::unique_ptr<CharacterMapRuntime> map_runtime_;
  std::uint64_t map_generation_{0};
  UnitMissileTrajectory_C missile_trajectory_;
  std::unique_ptr<WorldSession> session_;
  std::unique_ptr<net::PacketQueue> packet_queue_;
};

}
