#pragma once

#include "openwow/game/inventory/equipment_presentation.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/world_session.h"
#include "openwow/render/scene/nameplate_renderer.h"
#include "openwow/render/scene/object_renderer.h"
#include "openwow/render/scene/unit_name_renderer.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class WorldPresentationPublisher {
public:

  explicit WorldPresentationPublisher(render::EquipmentRenderer &equipment_renderer)
      : equipment_renderer_(equipment_renderer) {
    projection_cache_.rehash(kProjectionCacheBucketFloor);
  }

  void BindDbc(const data::dbc::DbcLoader *dbc) {
    if (dbc_ != dbc) {
      projection_cache_.clear();
      projection_cache_slots_.clear();
    }
    dbc_ = dbc;
  }
  void ApplyEquipmentPresentation(const EquipmentPresentation &presentation);

  void Reset() {
    projection_cache_.clear();
    projection_cache_slots_.clear();
  }

  [[nodiscard]] render::ObjectRenderPresentationSnapshot &
  PublishObjects(const ObjectManager &objects, const ObjectPresentationSnapshot &presentation,
                 const TransportManager &transports, const WorldSession &world_session);

  [[nodiscard]] render::NameplatePresentationSnapshot PublishNameplates(
      const ObjectManager &objects, WorldSession &world_session, std::uint64_t target_guid,
      std::uint64_t mouseover_guid, bool show_world_nameplates,
      bool show_class_color_in_nameplate, float player_x, float player_y, float player_z);

  struct OverheadTextPresentation {
    render::NameplatePresentationSnapshot nameplates;
    render::UnitNamePresentationSnapshot unit_names;
  };
  [[nodiscard]] OverheadTextPresentation PublishOverheadText(
      const ObjectManager &objects, WorldSession &world_session, std::uint64_t target_guid,
      std::uint64_t mouseover_guid, bool show_world_nameplates,
      bool show_class_color_in_nameplate, float player_x, float player_y, float player_z);

  [[nodiscard]] render::UnitNamePresentationSnapshot PublishUnitNames(
      const ObjectManager &objects, WorldSession &world_session,
      std::uint64_t target_guid, bool ui_visible,
      const std::vector<render::NameplateInfo> &visible_nameplates,
      std::uint32_t now_ms);

  void PublishSpellVisuals(ObjectManager &objects, ObjectPresentationSnapshot &presentation);

  enum class EquipmentSource : std::uint8_t {

    kNone,

    kInventoryReplica,

    kVisibleItemDescriptors,

    kVirtualItemDescriptors,

    kCorpse,
  };
  [[nodiscard]] static EquipmentSource SelectEquipmentSource(
      TypeID type_id, bool has_inventory_replica_block) noexcept;

private:

  enum class CharacterComponentSource : std::uint8_t {

    kNone,

    kPlayer,

    kNpc,

    kCorpse,
  };

  struct AssembledEquipment {
    std::array<render::EquipmentItemVisual, render::kMaxEquipSlot> items{};
    std::uint8_t race{0};
    std::uint8_t gender{0};
    std::uint8_t sheathe_state{0};
    std::uint8_t padding{0};

    friend bool operator==(const AssembledEquipment &lhs,
                           const AssembledEquipment &rhs) noexcept {
      return std::memcmp(&lhs, &rhs, sizeof(AssembledEquipment)) == 0;
    }
  };
  static_assert(std::has_unique_object_representations_v<AssembledEquipment>,
                "AssembledEquipment is compared bytewise and must be "
                "padding-free");

  struct CachedProjection {

    bool equipment_present{false};
    EquipmentPresentation equipment;

    bool virtual_item_weapons_valid{false};
    std::array<std::uint32_t, 3> virtual_item_entries{};
    std::array<render::EquipmentItemVisual, render::kMaxEquipSlot>
        virtual_item_weapons{};

    bool visible_item_equipment_valid{false};
    bool visible_item_equipment_resolved{false};
    std::array<std::uint32_t, render::kMaxEquipSlot> visible_item_entries{};
    std::array<render::EquipmentItemVisual, render::kMaxEquipSlot>
        visible_item_equipment{};

    bool display_facts_valid{false};
    std::uint32_t display_id{0};
    bool is_player{false};
    CharacterComponentSource component_source{CharacterComponentSource::kNone};

    render::CharacterAppearanceSelection npc_selection{};
    std::string npc_prebaked_body_texture;

    bool appearance_valid{false};
    render::CharacterAppearanceSelection selection{};
    std::optional<GuildEmblem> guild_tabard_emblem;

    std::shared_ptr<const render::CharacterAppearanceTextureSources>
        texture_sources;
    render::CharacterAppearanceGeosetState geosets;

    std::shared_ptr<const std::string> appearance_key;

    bool equipment_visuals_valid{false};
    AssembledEquipment equipment_visual_inputs{};
    render::EquipmentVisuals equipment_visuals;

    std::vector<render::ModelAttachmentSpec> equipment_attachment_specs;

    std::uint64_t equipment_sync_serial{0};
    AssembledEquipment published_equipment{};

    bool art_kit_valid{false};
    std::uint8_t art_kit{0};
    std::array<std::string, 3> art_kit_texture_paths{};

    bool dynamic_object_valid{false};
    std::uint32_t dynamic_object_spell_id{0};
    std::int32_t dynamic_object_violence_level{0};
    DynamicObjectVisualState dynamic_object_visual{};
  };

  void ProjectObject(ObjectHandle handle, const WorldObject &object,
                     const TransportManager &transports, const WorldSession &world_session,
                     render::ObjectProjection &instance);

  void ProjectObject(ObjectHandle handle, const WorldObject &object,
                     const TransportManager &transports, const WorldSession &world_session,
                     CachedProjection &cache, std::int32_t violence_level,
                     render::ObjectProjection &instance);
  void SyncTransform(render::ObjectProjection &instance, const WorldObject &object,
                     const TransportManager &transports) const;
  void SyncGameObjectAnimation(render::ObjectProjection &instance, const CGGameObject_C &game_object);
  void SyncGameObjectArtKit(render::ObjectProjection &instance, const CGGameObject_C &game_object,
                            CachedProjection &cache);

  void SyncEquipment(const WorldObject &object, CachedProjection &cache);

  void SyncVisibleItemEquipment(const CGPlayer_C &player, CachedProjection &cache);

  void SyncVirtualItemWeapons(const CGUnit_C &unit, CachedProjection &cache);

  void SyncCorpseEquipment(const CGCorpse_C &corpse);

  void SyncCorpseCharacterAppearance(render::ObjectProjection &instance,
                                     const CGCorpse_C &corpse, CachedProjection &cache);

  void PublishCharacterAppearanceSelection(
      render::ObjectProjection &instance, CachedProjection &cache,
      const render::CharacterAppearanceSelection &selection,
      const std::optional<GuildEmblem> &guild_tabard_emblem,
      bool sources_invalidated);

  void BuildEquipmentAttachments(render::ObjectProjection &instance, CachedProjection &cache);

  void StampEquipmentSyncSerial(render::ObjectProjection &instance,
                                CachedProjection &cache);
  void SyncQuestOverlay(render::ObjectProjection &instance, const WorldObject &object) const;
  void SyncCharacterAppearance(render::ObjectProjection &instance, const WorldObject &object,
                               const WorldSession &world_session, CachedProjection &cache);
  void SelectAnimation(render::ObjectProjection &instance, const WorldObject &object);

  void GatherOverheadUnits(const ObjectManager &objects);
  [[nodiscard]] render::NameplatePresentationSnapshot PublishNameplatesFromUnits(
      std::span<const CGUnit_C *const> units, const ObjectManager &objects,
      WorldSession &world_session, std::uint64_t target_guid, std::uint64_t mouseover_guid,
      bool show_world_nameplates, bool show_class_color_in_nameplate, float player_x,
      float player_y, float player_z);
  [[nodiscard]] render::UnitNamePresentationSnapshot PublishUnitNamesFromUnits(
      std::span<const CGUnit_C *const> units, const ObjectManager &objects,
      WorldSession &world_session, std::uint64_t target_guid, bool ui_visible,
      const std::vector<render::NameplateInfo> &visible_nameplates, std::uint32_t now_ms);

  render::EquipmentRenderer &equipment_renderer_;
  const data::dbc::DbcLoader *dbc_{nullptr};

  static constexpr std::size_t kProjectionCacheBucketFloor = 2048u;
  std::unordered_map<ObjectHandle, CachedProjection, ObjectHandle::Hash> projection_cache_;

  struct ProjectionCacheSlot {
    ObjectHandle handle;
    CachedProjection *cache{nullptr};
  };
  std::vector<ProjectionCacheSlot> projection_cache_slots_;
  [[nodiscard]] CachedProjection &ResolveProjectionCache(ObjectHandle handle,
                                                         std::uint32_t presentation_slot);
  void ForgetProjectionCache(ObjectHandle handle, std::uint32_t presentation_slot);

  AssembledEquipment equipment_scratch_{};

  bool equipment_scratch_assembled_{false};

  std::uint64_t equipment_publication_serial_{0};

  render::ObjectRenderPresentationSnapshot scratch_snapshot_;

  struct PublishTarget {
    ObjectHandle handle;
    std::uint32_t presentation_slot{kNoPresentationSlot};
    const WorldObject *object{nullptr};
    CachedProjection *cache{nullptr};
  };

  std::vector<const WorldObject *> publish_gathered_objects_;
  std::vector<PublishTarget> publish_targets_;

  std::vector<const CGUnit_C *> overhead_units_scratch_;
};

}
