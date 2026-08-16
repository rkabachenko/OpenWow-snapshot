#pragma once

#include "openwow/game/descriptor_callback_registry.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cgdynamicobject.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/objects/object_factory.h"
#include "openwow/game/object_presentation_snapshot.h"
#include "openwow/game/update_field_event_mapper.h"
#include "openwow/game/update_object_parser.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::world {
class MovementSplineManager;
}
namespace openwow::render::m2 {
class M2System;
}
namespace openwow::render {
class WorldFrame;
}
namespace openwow::audio { class SoundRuntime; }

namespace openwow::game {

class WorldEnvironmentState;
class ObjectManager;
struct PlayerControlRuntime;
class QueryCache;
class TransportManager;
class WorldSession;

[[nodiscard]] WorldObject *CGObject_HasFlags(ObjectManager& objects,
                                             std::uint64_t guid_raw,
                                             std::uint32_t required_type_mask);
[[nodiscard]] const WorldObject *CGObject_HasFlags(
    const ObjectManager& objects,
    std::uint64_t guid_raw,
    std::uint32_t required_type_mask);

[[nodiscard]] bool Movement_C_IsGuidTransport(const ObjectManager& objects,
                                              std::uint64_t guid_raw);

struct ObjectManagerCallbacks {
  std::function<void()> on_update_object_batch_started;

  std::function<void(WorldObject &)> on_object_created;

  std::function<void(WorldObject &)> on_object_world_published;

  std::function<void(WorldObject &)> on_object_packet_promoted;
  std::function<void(WorldObject &)> on_object_updated;

  std::function<void(CGUnit_C &, const MovementUpdate &, std::uint32_t)>
      on_unit_authoritative_movement;

  std::function<void(CGUnit_C &, const MovementUpdate &)>
      on_unit_create_movement_metadata;

  std::function<void()> on_out_of_range_vehicle_transitions_ready;
  std::function<void(const WorldObject &)> on_object_out_of_range;

  std::function<void(const WorldObject &, bool destroy_packet_death_cleanup)>
      on_object_pre_destroyed;
  std::function<void(ObjectGuid)> on_object_destroyed;
  std::function<void(ObjectGuid, TypeID)> on_object_destroyed_typed;

  std::function<void(ObjectGuid transport_guid)>
      on_local_player_transport_destroyed;

  std::function<void(ObjectGuid transport_guid, ObjectGuid target_guid,
                     bool movement_attachment,
                     GameObjectAttachmentNode *attachment)>
      on_transport_attachment_destroyed;

  std::function<void(ObjectGuid transport_guid, ObjectGuid passenger_guid)>
      on_transport_passenger_destroyed;
  std::function<void(bool)> on_update_object_batch_finished;
  std::function<void(ObjectGuid)> on_player_self_created;
  std::function<void(ObjectGuid)> on_creature_entry_resolved;
  std::function<void(ObjectGuid)> on_transport_opened;
  std::function<void(std::uint32_t)> send_sheathed;

  std::function<void(const WorldObject &, const FieldUpdateBatch &, bool)> on_fields_changed;
};

class ObjectManager {
public:
  ObjectManager(PlayerInventoryReplica& inventory,
                 PlayerControlRuntime& player_control,
                 ItemDefinitions& item_definitions,
                 openwow::render::m2::M2System& m2_system,
                  const openwow::data::dbc::DbcLoader& dbc_loader,
                  QueryCache& query_cache, TransportManager& transport_manager,
                  openwow::audio::SoundRuntime& sound_runtime);
  ~ObjectManager() = default;

  void SetCallbacks(ObjectManagerCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
  }
  void NotifyTransportOpened(const ObjectGuid transport_guid) const {
    if (callbacks_.on_transport_opened) {
      callbacks_.on_transport_opened(transport_guid);
    }
  }
  void BindWorldFrame(openwow::render::WorldFrame* world_frame);
  void BindWorldEnvironmentState(WorldEnvironmentState* world_environment);
  [[nodiscard]] const openwow::data::dbc::DbcLoader& dbc_loader() const
      noexcept {
    return dbc_loader_;
  }
  [[nodiscard]] QueryCache& query_cache() noexcept { return query_cache_; }
  [[nodiscard]] PlayerControlRuntime& player_control() noexcept {
    return player_control_;
  }
  [[nodiscard]] const PlayerControlRuntime& player_control() const noexcept {
    return player_control_;
  }
  [[nodiscard]] openwow::audio::SoundRuntime& sound_runtime() const noexcept {
    return sound_runtime_;
  }
  [[nodiscard]] const QueryCache& query_cache() const noexcept {
    return query_cache_;
  }
  void NotifyCreatureEntryResolved(ObjectGuid guid) const {
    if (callbacks_.on_creature_entry_resolved) {
      callbacks_.on_creature_entry_resolved(guid);
    }
  }
  void SendSheathed(const std::uint32_t state) const {
    if (callbacks_.send_sheathed) {
      callbacks_.send_sheathed(state);
    }
  }
  [[nodiscard]] TransportManager& transport_manager() noexcept {
    return transport_manager_;
  }
  [[nodiscard]] const TransportManager& transport_manager() const noexcept {
    return transport_manager_;
  }

  [[nodiscard]] const WorldObject *Get(ObjectGuid guid) const;
  [[nodiscard]] WorldObject *GetMutable(ObjectGuid guid);

  [[nodiscard]] const WorldObject *GetLocalPlayer() const;
  [[nodiscard]] ObjectGuid GetLocalPlayerGuid() const {
    return local_player_guid_;
  }
  [[nodiscard]] ObjectGuid GetActivePlayerCorpseGuid() const {
    return active_player_corpse_guid_;
  }

  [[nodiscard]] std::size_t Count() const {
    return objects_.size() + pending_objects_.size();
  }
  [[nodiscard]] std::size_t GetPendingObjectCount() const {
    return pending_objects_.size();
  }

  [[nodiscard]] ObjectPresentationSnapshot PublishPresentationSnapshot(
      const WorldSession& session);
  [[nodiscard]] std::optional<ObjectHandle> GetObjectHandle(
      ObjectGuid guid) const;
  [[nodiscard]] const WorldObject* ResolveObjectHandle(
      ObjectHandle handle) const;

  void ApplyMovementUpdate(const MovementOnlyUpdate &upd);

  void SynchronizeUnitTransportPassengerMembership(
      const CGUnit_C &unit, const MovementInfo &previous_movement);

  void AdvanceSplineMovement(world::MovementSplineManager &spline_manager);
  void AdvanceMovementEvents(WorldSession &session,
                             std::uint32_t current_tick_ms);
  void AdvanceVisualState(std::uint32_t current_tick_ms, float elapsed_seconds);

  void AdvanceTransportPathStates();

  void AdvanceEmoteQueues();

  [[nodiscard]] bool AcquireObjectLifetimeHold(const ObjectGuid &guid);
  [[nodiscard]] bool ReleaseObjectLifetimeHold(const ObjectGuid &guid);

  [[nodiscard]] const CGUnit_C *GetUnit(ObjectGuid guid) const;
  [[nodiscard]] CGUnit_C *GetMutableUnit(ObjectGuid guid);

  [[nodiscard]] const CGPlayer_C *GetPlayer(ObjectGuid guid) const;
  [[nodiscard]] CGPlayer_C *GetMutablePlayer(ObjectGuid guid);

  [[nodiscard]] const CGItem_C *GetItem(ObjectGuid guid) const;

  [[nodiscard]] const CGGameObject_C *GetGameObject(ObjectGuid guid) const;
  [[nodiscard]] CGGameObject_C *GetMutableGameObject(ObjectGuid guid);

  [[nodiscard]] const CGDynamicObject_C *GetDynamicObject(ObjectGuid guid) const;

  [[nodiscard]] const CGCorpse_C *GetCorpse(ObjectGuid guid) const;

  [[nodiscard]] const CGContainer_C *GetContainer(ObjectGuid guid) const;

  [[nodiscard]] const CGPlayer_C *GetLocalPlayerTyped() const;

  void SetTarget(const ObjectGuid &guid);
  [[nodiscard]] ObjectGuid GetTargetGuid() const {
    return target_;
  }
  [[nodiscard]] CGUnit_C *GetTarget();
  [[nodiscard]] const CGUnit_C *GetTarget() const;

  void SetFocusTarget(const ObjectGuid &guid) {
    focus_target_ = guid;
  }
  [[nodiscard]] ObjectGuid GetFocusTargetGuid() const {
    return focus_target_;
  }
  [[nodiscard]] CGUnit_C *GetFocusTarget();
  [[nodiscard]] const CGUnit_C *GetFocusTarget() const;

  void SetMouseover(const ObjectGuid &guid);
  [[nodiscard]] ObjectGuid GetMouseoverGuid() const {
    return mouseover_;
  }

  void SetNpcGuid(const ObjectGuid &guid) { npc_guid_ = guid; }
  [[nodiscard]] ObjectGuid GetNpcGuid() const { return npc_guid_; }

  void SetNpcInteractionRangeSquared(const double range_squared) {
    npc_interaction_range_squared_ = range_squared;
  }
  [[nodiscard]] double GetNpcInteractionRangeSquared() const {
    return npc_interaction_range_squared_;
  }

  void SetActivePlayer(const ObjectGuid &guid) {
    if (local_player_guid_ != guid) {
      active_player_corpse_guid_ = ObjectGuid();
    }
    local_player_guid_ = guid;
    CGObject_C::SetActivePlayerGuid(guid);
  }

  [[nodiscard]] CGPlayer_C *GetActivePlayer();
  [[nodiscard]] const CGPlayer_C *GetActivePlayer() const;
  [[nodiscard]] ObjectGuid GetActivePlayerGuid() const {
    return CGObject_C::GetActivePlayerGuid();
  }

  [[nodiscard]] const WorldObject *GetObjectByGUID(ObjectGuid guid) const {
    return Get(guid);
  }

  template <typename Fn> bool EnumVisibleObjects(Fn &&fn) const {
    using CallbackResult = std::invoke_result_t<Fn &, const WorldObject &>;

    for (const auto &[guid, obj_ptr] : objects_) {
      if constexpr (std::is_void_v<CallbackResult>) {
        fn(*obj_ptr);
      } else {
        if (!static_cast<bool>(fn(*obj_ptr))) {
          return false;
        }
      }
    }

    return true;
  }

  template <typename Fn> bool EnumVisibleObjectsMutable(Fn &&fn) {
    using CallbackResult = std::invoke_result_t<Fn &, WorldObject &>;

    for (auto &[guid, obj_ptr] : objects_) {
      if constexpr (std::is_void_v<CallbackResult>) {
        fn(*obj_ptr);
      } else {
        if (!static_cast<bool>(fn(*obj_ptr))) {
          return false;
        }
      }
    }

    return true;
  }

  void SetMapId(std::uint32_t map_id) {
    map_id_ = map_id;
  }
  [[nodiscard]] std::uint32_t GetMapId() const {
    return map_id_;
  }

  void SetZoneId(std::uint32_t zone_id) {
    zone_id_ = zone_id;
  }
  [[nodiscard]] std::uint32_t GetZoneId() const {
    return zone_id_;
  }

  void SetAreaId(std::uint32_t area_id) {
    area_id_ = area_id;
  }
  [[nodiscard]] std::uint32_t GetAreaId() const {
    return area_id_;
  }

  [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> GetZoneAndAreaId() const {
    return {zone_id_, area_id_};
  }

  template <typename Fn> void ForEach(Fn &&fn) const {
    for (const auto &[guid, obj_ptr] : objects_) {
      fn(*obj_ptr);
    }
  }

  template <typename Pred> std::vector<const WorldObject *> FindAll(Pred &&pred) const {
    std::vector<const WorldObject *> result;
    for (const auto &[guid, obj_ptr] : objects_) {
      if (pred(*obj_ptr))
        result.push_back(obj_ptr.get());
    }
    for (const auto &[guid, pending] : pending_objects_) {
      if (pred(*pending.object))
        result.push_back(pending.object.get());
    }
    return result;
  }

  void ForEachObject(const std::function<void(const ObjectGuid &, CGObject_C &)> &fn);

  void ForEachUnit(const std::function<void(const ObjectGuid &, CGUnit_C &)> &fn);

  void ForEachPlayer(const std::function<void(const ObjectGuid &, CGPlayer_C &)> &fn);

  [[nodiscard]] std::size_t GetObjectCount() const {
    return Count();
  }
  [[nodiscard]] std::size_t GetUnitCount() const;
  [[nodiscard]] std::size_t GetPlayerCount() const;

  [[nodiscard]] UpdateObjectHandler MakeHandler();

  bool HandleUpdateObject(const std::uint8_t *data, std::size_t len);

  bool HandleCompressedUpdateObject(const std::uint8_t *data, std::size_t len);

  void HandleDestroyObject(const std::uint8_t *data, std::size_t len);

  struct NameCacheEntry {
    std::string name;
    std::uint8_t race = 0;
    std::uint8_t gender = 0;
    std::uint8_t class_id = 0;
  };

  void CachePlayerName(const ObjectGuid &guid, const std::string &name, std::uint8_t race,
                       std::uint8_t gender, std::uint8_t cls);
  bool InvalidatePlayerName(const ObjectGuid &guid);
  [[nodiscard]] std::string GetPlayerName(const ObjectGuid &guid) const;
  [[nodiscard]] const NameCacheEntry *GetNameEntry(const ObjectGuid &guid) const;
  [[nodiscard]] std::optional<ObjectGuid> FindPlayerGuidByName(std::string_view name) const;

  void CreateObject(const ObjectGuid &guid, TypeID type_id);
  void DestroyObject(const ObjectGuid &guid);
  void DestroyAllObjects();

  void Clear();

  void Reset();

  void DumpState(const std::string &filepath = "Logs/ObjectDump.txt") const;

private:
  PlayerInventoryReplica& inventory_;
  PlayerControlRuntime& player_control_;
  openwow::audio::SoundRuntime& sound_runtime_;
  ItemDefinitions& item_definitions_;
  openwow::render::m2::M2System& m2_system_;
  openwow::render::WorldFrame* world_frame_{nullptr};
  WorldEnvironmentState* world_environment_{nullptr};
  const openwow::data::dbc::DbcLoader& dbc_loader_;
  QueryCache& query_cache_;
  TransportManager& transport_manager_;
  struct PendingObjectEntry {
    std::unique_ptr<CGObject_C> object;
    TypeID type_id{TypeID::kObject};
    std::uint32_t created_at_ms{0};
    std::list<ObjectGuid>::iterator type_order_it;
  };

  std::unordered_map<ObjectGuid, std::unique_ptr<CGObject_C>, ObjectGuid::Hash> objects_;
  std::unordered_map<ObjectGuid, PendingObjectEntry, ObjectGuid::Hash> pending_objects_;

  struct PublishedPresentationEntry {
    ObjectPresentationRecord record;
    std::uint64_t presentation_generation{0};
    std::uint64_t seen_publication_generation{0};

    std::uint32_t presentation_slot{kNoPresentationSlot};
  };
  std::unordered_map<ObjectGuid, PublishedPresentationEntry, ObjectGuid::Hash>
      presentation_state_;

  std::vector<std::uint32_t> presentation_slot_free_list_;
  std::uint32_t presentation_slot_count_{0};

  static constexpr std::size_t kPresentationStateBucketFloor = 2048u;

  struct PresentationSortKey {
    std::uint64_t raw_guid{0};
    std::uint64_t generation{0};
    std::uint32_t walk_index{0};
  };

  void SortActiveRecords(std::vector<ObjectPresentationRecord>& walk_records,
                         std::vector<ObjectPresentationRecord>& sorted_records);
  std::vector<ObjectPresentationRecord> presentation_walk_scratch_;
  std::vector<PresentationSortKey> presentation_sort_keys_;
  std::vector<PresentationSortKey> presentation_sort_seeded_keys_;
  std::vector<std::uint32_t> presentation_sort_order_;
  std::uint64_t next_object_presentation_generation_{1};
  std::uint64_t object_presentation_publication_generation_{0};

  std::list<ObjectGuid> world_publication_queue_;
  std::unordered_map<ObjectGuid, std::list<ObjectGuid>::iterator,
                     ObjectGuid::Hash>
      world_publication_entries_;
  std::array<std::list<ObjectGuid>, kNumClientObjectTypes> pending_by_type_;
  std::unordered_set<ObjectGuid, ObjectGuid::Hash> preallocated_create_objects_;
  struct DeferredPrepassValues {
    ObjectGuid guid;
    std::optional<FieldUpdateBatch> changed_fields;
  };

  std::vector<DeferredPrepassValues> deferred_prepass_values_;
  std::size_t deferred_prepass_values_cursor_{0};
  ObjectGuid local_player_guid_;
  ObjectGuid active_player_corpse_guid_;
  ObjectGuid target_;
  ObjectGuid focus_target_;
  ObjectGuid mouseover_;
  ObjectGuid npc_guid_;

  double npc_interaction_range_squared_{0.0};
  ObjectManagerCallbacks callbacks_;
  std::unordered_map<std::uint64_t, NameCacheEntry> name_cache_;
  std::uint32_t map_id_{0};
  std::uint32_t zone_id_{0};
  std::uint32_t area_id_{0};

  [[nodiscard]] PendingObjectEntry *FindPendingEntry(ObjectGuid guid);
  [[nodiscard]] const PendingObjectEntry *FindPendingEntry(ObjectGuid guid) const;
  [[nodiscard]] CGObject_C *
  FindMutableForPacketUpdate(ObjectGuid guid, bool *promoted_from_pending = nullptr);
  [[nodiscard]] CGObject_C *PromotePendingObject(ObjectGuid guid);
  [[nodiscard]] bool DestroyPendingObject(ObjectGuid guid);
  [[nodiscard]] bool StageActiveObjectForServerRemoval(ObjectGuid guid,
                                                       bool destroy_packet_death_cleanup);
  [[nodiscard]] bool FinalizeActiveObjectServerRemoval(ObjectGuid guid);
  void DestroyActiveObject(ObjectGuid guid, bool destroy_packet_death_cleanup);
  [[nodiscard]] std::optional<std::uint16_t>
  ResolveFieldCountForTrackedObject(ObjectGuid guid) const;
  [[nodiscard]] bool PreallocateCreateObjects(const std::uint8_t *data, std::size_t len,
                                              std::vector<ObjectGuid> &created_shells);
  void ClearPreallocatedCreateMarkers(const std::vector<ObjectGuid> &created_shells);
  void StagePendingObject(std::unique_ptr<CGObject_C> object, TypeID type_id);
  void EnqueueWorldPublication(ObjectGuid guid);
  void RemoveWorldPublication(ObjectGuid guid);
  void DrainWorldPublicationQueue();
  [[nodiscard]] bool ReapExpiredPendingObjectForType(TypeID type_id, std::uint32_t now_ms,
                                                     std::uint32_t expiration_ms);
  void SweepStalePendingObjects();

  void ReapExpiredPendingObjectBeforeCreate(TypeID type_id);
  void ClearAllTrackedReferences();
  void ClearTrackedReferences(ObjectGuid guid);
  void RefreshActivePlayerCorpseReference(const CGObject_C &object);
  void ApplyCreateFieldsToExistingObject(CGObject_C &object,
                                         const CreateObjectUpdate &upd,
                                         bool clear_missing_fields);
  [[nodiscard]] bool ApplyCreateBlockToExistingObject(
      CGObject_C &object, const CreateObjectUpdate &upd);

  void OnCreate(const CreateObjectUpdate &upd);
  void ApplyPrepassValues(const ValuesUpdate &upd);
  [[nodiscard]] std::optional<FieldUpdateBatch>
  ConsumeDeferredPrepassValues(ObjectGuid guid);
  void OnValues(const ValuesUpdate &upd);
  void OnMovement(const MovementOnlyUpdate &upd);
  void OnOutOfRange(const OutOfRangeUpdate &upd);
  void OnNearObjects(const NearObjectsUpdate &upd);
  friend struct ObjectManagerTestAccess;
};

}
