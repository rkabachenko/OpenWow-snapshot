#include "openwow/game/character_map_runtime.h"

#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/transport_manager.h"
#include "openwow/render/m2/m2_system.h"

#include <utility>

namespace openwow::game {

CharacterMapRuntime::CharacterMapRuntime(
    PlayerInventoryReplica& inventory, PlayerControlRuntime& player_control,
    ItemDefinitions& item_definitions,
    openwow::render::m2::M2System& m2_system,
    const openwow::data::dbc::DbcLoader& dbc_loader,
    QueryCache& query_cache, TransportManager& transport_manager,
    openwow::audio::SoundRuntime& sound_runtime)
    : inventory_(inventory),
      player_control_(player_control),
      item_definitions_(item_definitions),
      m2_system_(m2_system),
      dbc_loader_(dbc_loader),
      query_cache_(query_cache),
      transport_manager_(transport_manager),
      sound_runtime_(sound_runtime),
      object_manager_(std::make_unique<ObjectManager>(
          inventory_, player_control_, item_definitions_, m2_system_,
          dbc_loader_, query_cache_, transport_manager_, sound_runtime_)) {
  Configure(*object_manager_);
}

void CharacterMapRuntime::SetCallbacks(ObjectManagerCallbacks callbacks) {
  callbacks_ = std::move(callbacks);
  object_manager_->SetCallbacks(callbacks_);
}

void CharacterMapRuntime::BindWorldFrame(
    openwow::render::WorldFrame* world_frame) {
  world_frame_ = world_frame;
  object_manager_->BindWorldFrame(world_frame_);
}

void CharacterMapRuntime::BindWorldEnvironmentState(
    WorldEnvironmentState* world_environment) {
  world_environment_ = world_environment;
  object_manager_->BindWorldEnvironmentState(world_environment_);
}

void CharacterMapRuntime::SetMapId(const std::uint32_t map_id) {
  map_id_ = map_id;
  object_manager_->SetMapId(map_id_);
}

void CharacterMapRuntime::ReplaceMapGeneration(const std::uint32_t map_id) {
  auto next_object_manager = std::make_unique<ObjectManager>(
      inventory_, player_control_, item_definitions_, m2_system_, dbc_loader_,
      query_cache_, transport_manager_, sound_runtime_);
  map_id_ = map_id;
  object_manager_ = std::move(next_object_manager);
  Configure(*object_manager_);
  ++generation_;
}

void CharacterMapRuntime::Configure(ObjectManager& objects) {
  objects.SetCallbacks(callbacks_);
  objects.BindWorldFrame(world_frame_);
  objects.BindWorldEnvironmentState(world_environment_);
  objects.SetMapId(map_id_);
}

}
