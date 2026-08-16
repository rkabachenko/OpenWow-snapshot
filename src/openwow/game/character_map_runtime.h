#pragma once

#include "openwow/game/object_manager.h"

#include <cstdint>
#include <memory>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::render::m2 {
class M2System;
}
namespace openwow::audio { class SoundRuntime; }

namespace openwow::game {

class ItemDefinitions;
struct PlayerControlRuntime;
class PlayerInventoryReplica;
class QueryCache;
class TransportManager;

class CharacterMapRuntime final {
 public:
  CharacterMapRuntime(PlayerInventoryReplica& inventory,
                      PlayerControlRuntime& player_control,
                      ItemDefinitions& item_definitions,
                      openwow::render::m2::M2System& m2_system,
                       const openwow::data::dbc::DbcLoader& dbc_loader,
                       QueryCache& query_cache,
                       TransportManager& transport_manager,
                       openwow::audio::SoundRuntime& sound_runtime);
  ~CharacterMapRuntime() = default;

  CharacterMapRuntime(const CharacterMapRuntime&) = delete;
  CharacterMapRuntime& operator=(const CharacterMapRuntime&) = delete;

  [[nodiscard]] ObjectManager& objects() noexcept { return *object_manager_; }
  [[nodiscard]] const ObjectManager& objects() const noexcept {
    return *object_manager_;
  }

  void SetCallbacks(ObjectManagerCallbacks callbacks);
  void BindWorldFrame(openwow::render::WorldFrame* world_frame);
  void BindWorldEnvironmentState(WorldEnvironmentState* world_environment);
  void SetMapId(std::uint32_t map_id);
  void ReplaceMapGeneration(std::uint32_t map_id);

  [[nodiscard]] std::uint64_t generation() const noexcept {
    return generation_;
  }

 private:
  PlayerInventoryReplica& inventory_;
  PlayerControlRuntime& player_control_;
  ItemDefinitions& item_definitions_;
  openwow::render::m2::M2System& m2_system_;
  const openwow::data::dbc::DbcLoader& dbc_loader_;
  QueryCache& query_cache_;
  TransportManager& transport_manager_;
  openwow::audio::SoundRuntime& sound_runtime_;
  std::unique_ptr<ObjectManager> object_manager_;
  ObjectManagerCallbacks callbacks_;
  openwow::render::WorldFrame* world_frame_{nullptr};
  WorldEnvironmentState* world_environment_{nullptr};
  std::uint32_t map_id_{0};
  std::uint64_t generation_{1};

  void Configure(ObjectManager& objects);
};

}
