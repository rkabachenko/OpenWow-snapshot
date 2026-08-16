#pragma once

#include "openwow/game/inventory/equipment/equipment_visual.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/models/animation/attachment_graph.h"
#include "openwow/render/models/characters/character_appearance_geosets.h"
#include "openwow/render/models/characters/character_appearance_texture_sources.h"
#include "openwow/render/models/characters/character_model_path.h"
#include "openwow/render/scene/scene_graph.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace openwow::ui::glue {

struct GlueGameState;

namespace detail {
[[nodiscard]] double ResolvePetFamilyScaleMultiplier(
    const openwow::data::dbc::CreatureFamilyEntry& family,
    std::uint32_t pet_level);
}

enum class CharacterDisplayOwnerKind : std::uint8_t {
  kNone,
  kCharacterListRow,
  kCreatePreview,
};

struct CharacterDisplayOwner {
  CharacterDisplayOwnerKind kind{CharacterDisplayOwnerKind::kNone};
  std::uint64_t character_id{0};
  std::size_t row_index{0};

  [[nodiscard]] bool valid() const noexcept {
    return kind != CharacterDisplayOwnerKind::kNone;
  }

  bool operator==(const CharacterDisplayOwner &) const = default;
};

[[nodiscard]] inline bool SameStableCharacterDisplayOwner(
    const CharacterDisplayOwner &lhs,
    const CharacterDisplayOwner &rhs) noexcept {
  if (lhs.kind != rhs.kind) {
    return false;
  }
  if (lhs.kind == CharacterDisplayOwnerKind::kCharacterListRow) {
    return lhs.character_id != 0u && rhs.character_id != 0u
               ? lhs.character_id == rhs.character_id
               : lhs.row_index == rhs.row_index;
  }
  return lhs.kind == CharacterDisplayOwnerKind::kCreatePreview;
}

class GlueCharSelectScene {
public:
  static constexpr std::size_t kMaxCharacterEquipmentModels = 6u;
  static constexpr std::size_t kMaxItemVisualModelsPerEquipment = 5u;
  static constexpr std::uint32_t kRightFingerAnimationSlotBegin = 8u;
  static constexpr std::uint32_t kRightFingerAnimationSlotEnd = 13u;
  static constexpr std::uint32_t kLeftFingerAnimationSlotBegin = 13u;
  static constexpr std::uint32_t kLeftFingerAnimationSlotEnd = 18u;

  struct CharacterEquipmentPose {
    std::uint32_t base_animation_id{0u};
    std::array<std::optional<std::uint32_t>,
               openwow::render::m2::kM2RetailAnimationSlotCount>
        key_bone_animation_ids{};

    [[nodiscard]] bool HasKeyBoneAnimation(const std::uint32_t slot) const noexcept {
      return slot < key_bone_animation_ids.size() && key_bone_animation_ids[slot].has_value();
    }
  };

  struct AttachedEffectModel {
    std::uint32_t node_id{0};
    std::uint32_t attachment_index{0};
    std::string model_path;
    float scale{1.0f};
    bool active{false};
  };

  struct AttachedEquipmentModel {
    struct ChildModel {
      std::uint32_t node_id{0};
      std::uint32_t attachment_index{0};
      std::string model_path;
      bool active{false};
    };

    std::uint32_t node_id{0};
    std::uint32_t equipment_slot{0};
    std::uint32_t attachment_index{0};
    std::string model_path;
    std::string texture_path;
    std::array<ChildModel, kMaxItemVisualModelsPerEquipment> child_models{};
    bool selection_triangle_candidate{false};
    bool active{false};
  };

  struct CharacterDisplayPreload {
    CharacterDisplayOwner owner;
    openwow::render::CharacterAppearance appearance;
    std::uint8_t class_id{0};
    std::string model_path;
    openwow::render::CharacterAppearanceTextureSources appearance_sources;
    std::vector<std::string> component_model_paths;
  };

  static constexpr float kGhostDeathFfxAlpha = 0.15f;

  GlueCharSelectScene();

  GlueCharSelectScene(const GlueCharSelectScene &) = delete;
  GlueCharSelectScene &operator=(const GlueCharSelectScene &) = delete;

  void Reset();

  void ReleaseContent();
  void SetContentReleaseCallback(std::function<void()> callback);

  void RebuildAttachments();

  void SyncFromGameState(GlueGameState &gs);

  void RefreshFromGameState(GlueGameState &gs);

  void SyncCreateFromGameState(const GlueGameState &gs);

  void RefreshCreateFromGameState(const GlueGameState &gs);

  void ApplySelectFacing(float yaw_radians);

  void ApplyCreateFacing(float yaw_radians);
  [[nodiscard]] bool HasInitializedSelectedCharacterDisplay() const;
  [[nodiscard]] bool HasConstructedSelectedCharacterDisplay() const;
  [[nodiscard]] bool HasActiveSelectedCharacterDisplay() const;
  [[nodiscard]] bool SelectedCharacterIsGhost() const;
  [[nodiscard]] const CharacterDisplayOwner &current_display_owner() const noexcept;
  [[nodiscard]] std::optional<float> viewport_death_effect_alpha() const noexcept;

  void BindAppearanceDbcStores(
      const openwow::data::dbc::DbcStore<openwow::data::dbc::CharHairGeosetsEntry> *hair_geosets,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>
          *facial_hair_styles,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::ChrRacesEntry> *chr_races,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemDisplayInfoEntry>
          *item_display_info,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::HelmetGeosetVisDataEntry>
          *helmet_geoset_vis_data,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> *char_sections =
          nullptr,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellVisualKitEntry>
          *spell_visual_kit = nullptr,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellVisualEffectNameEntry>
          *spell_visual_effect_name = nullptr,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::CharStartOutfitEntry>
          *char_start_outfit = nullptr,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemVisualEffectsEntry>
          *item_visual_effects = nullptr,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemVisualsEntry>
          *item_visuals = nullptr,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::ParticleColorEntry>
          *particle_color = nullptr,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::CreatureDisplayInfoEntry>
          *creature_display_info = nullptr,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::CreatureModelDataEntry>
          *creature_model_data = nullptr,
      const openwow::data::dbc::DbcStore<openwow::data::dbc::CreatureFamilyEntry>
          *creature_family = nullptr);

  void SetSelectedCharacterModelPath(std::string m2_path);
  [[nodiscard]] const std::string &selected_character_model_path() const;
  [[nodiscard]] const openwow::render::CharacterAppearance &selected_character_appearance() const;
  [[nodiscard]] const openwow::render::CharacterAppearanceTextureSources &
  character_appearance_texture_sources() const;
  [[nodiscard]] const std::vector<CharacterDisplayPreload> &character_display_preloads() const;
  [[nodiscard]] bool IsAppearanceGeosetVisible(std::uint16_t section_id) const;

  void SetPropModelPath(std::optional<std::string> m2_path);
  [[nodiscard]] const std::optional<std::string> &prop_model_path() const;
  [[nodiscard]] const std::array<std::string, 3> &prop_texture_paths() const;
  [[nodiscard]] float prop_model_alpha() const;
  [[nodiscard]] const std::array<AttachedEffectModel, 9> &character_effect_models() const;
  [[nodiscard]] const std::array<AttachedEquipmentModel, kMaxCharacterEquipmentModels> &
  character_equipment_models() const;
  [[nodiscard]] const CharacterEquipmentPose &character_equipment_pose() const;
  [[nodiscard]] float character_model_alpha() const;
  [[nodiscard]] const std::array<float, 3> &character_model_tint() const;
  [[nodiscard]] std::array<float, 4> character_hierarchy_color_multiplier() const;

  [[nodiscard]] openwow::render::SceneGraph &graph() {
    return graph_;
  }
  [[nodiscard]] const openwow::render::SceneGraph &graph() const {
    return graph_;
  }
  [[nodiscard]] openwow::render::AttachmentGraph &attachments() {
    return attachments_;
  }
  [[nodiscard]] const openwow::render::AttachmentGraph &attachments() const {
    return attachments_;
  }

  [[nodiscard]] std::uint32_t background_root_node() const {
    return background_root_node_;
  }
  [[nodiscard]] std::uint32_t character_node() const {
    return character_node_;
  }
  [[nodiscard]] std::uint32_t prop_node() const {
    return prop_node_;
  }

private:
  struct SelectedCharacterSource {
    CharacterDisplayOwner owner;
    openwow::render::CharacterAppearance appearance;
    std::string model_path;
    std::uint8_t class_id{0};
    bool ghost{false};
    std::uint32_t pet_display_id{0};
    std::uint32_t pet_level{0};
    std::uint32_t pet_family{0};
  };

  static bool SelectedCharacterSourcesEqual(const SelectedCharacterSource &lhs,
                                             const SelectedCharacterSource &rhs);
  void SyncSelectedCharacter(GlueGameState &gs);
  void RefreshCharacterDisplayPreloads(const GlueGameState &gs);
  void SyncCreateCharacter(const GlueGameState &gs);
  void ApplyFacing(float yaw_radians);
  void NotifyContentRelease();
  void ResetCurrentDisplayContent();
  void ResetSelectedCharacterAppearance();
  void ResetCharacterEquipmentModels();
  void ResetCharacterVisualEffects();
  [[nodiscard]] bool HasValidBaseSkinSelection(
      const openwow::render::CharacterAppearance &appearance) const;
  void RefreshCharacterAppearanceTextureSources();
  void RefreshAppearanceGeosets();
  void RefreshCharacterEquipmentModels();
  void RefreshCharacterVisualEffects(bool ghost_character);
  void PopulateCreatePreviewEquipment(openwow::render::CharacterAppearance &appearance,
                                      const GlueGameState &gs);

  openwow::render::SceneGraph graph_{};
  openwow::render::AttachmentGraph attachments_{&graph_};

  std::uint32_t background_root_node_{0};
  std::uint32_t character_node_{0};
  std::uint32_t prop_node_{0};

  const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> *char_sections_{
      nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::CharHairGeosetsEntry> *hair_geosets_{
      nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>
      *facial_hair_styles_{nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::ChrRacesEntry> *chr_races_{nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemDisplayInfoEntry> *item_display_info_{
      nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::HelmetGeosetVisDataEntry>
      *helmet_geoset_vis_data_{nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellVisualKitEntry> *spell_visual_kit_{
      nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellVisualEffectNameEntry>
      *spell_visual_effect_name_{nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::CharStartOutfitEntry> *char_start_outfit_{
      nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemVisualEffectsEntry>
      *item_visual_effects_{nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemVisualsEntry> *item_visuals_{nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::ParticleColorEntry> *particle_color_{
      nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::CreatureDisplayInfoEntry>
      *creature_display_info_{nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::CreatureModelDataEntry>
      *creature_model_data_{nullptr};
  const openwow::data::dbc::DbcStore<openwow::data::dbc::CreatureFamilyEntry>
      *creature_family_{nullptr};
  std::string selected_character_model_path_;
  openwow::render::CharacterAppearance selected_character_appearance_{};
  openwow::render::CharacterAppearanceTextureSources appearance_texture_sources_{};
  std::vector<CharacterDisplayPreload> character_display_preloads_;
  openwow::render::CharacterAppearanceGeosetState appearance_geosets_{};
  std::optional<std::string> prop_model_path_;
  std::array<std::string, 3> prop_texture_paths_{};
  float prop_model_alpha_{1.0f};
  std::array<AttachedEffectModel, 9> character_effect_models_{};
  std::array<AttachedEquipmentModel, kMaxCharacterEquipmentModels> character_equipment_models_{};
  CharacterEquipmentPose character_equipment_pose_{};
  std::uint8_t selected_character_class_id_{0};
  float character_model_alpha_{1.0f};
  std::array<float, 3> character_model_tint_{1.0f, 1.0f, 1.0f};
  CharacterDisplayOwner current_display_owner_{};
  std::optional<SelectedCharacterSource> selected_character_source_;
  bool selected_character_display_initialized_{false};

  bool has_constructed_selected_character_display_{false};
  bool selected_character_is_ghost_{false};
  std::function<void()> content_release_callback_;
};

}
