
#include "openwow/ui/glue/glue_charselect_scene.h"

#include "openwow/render/api/math/render_matrix_math.h"

#include "openwow/render/models/characters/character_model_path.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/ui/glue/character_customization_randomizer.h"
#include "openwow/ui/glue/glue_game_state.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace openwow::ui::glue {

namespace {

std::string NormalizeM2Path(std::string p) {

  std::replace(p.begin(), p.end(), '\\', '/');
  if (!p.empty() && p.front() != '/') {
    p.insert(p.begin(), '/');
  }
  return p;
}

openwow::ui::glue::detail::CharacterCustomizationState MakeCustomizationState(
    const openwow::render::CharacterAppearance &appearance) {
  return {
      .race_id = appearance.race,
      .sex_id = appearance.gender,
      .skin = appearance.skin_color,
      .face = appearance.face,
      .hair_style = appearance.hair_style,
      .hair_color = appearance.hair_color,
      .facial_hair = appearance.facial_hair,
  };
}

openwow::render::CharacterAppearanceSelection MakeAppearanceSelection(
    const openwow::render::CharacterAppearance &appearance,
    const std::uint8_t class_id) {
  openwow::render::CharacterAppearanceSelection selection{
      .race = appearance.race,
      .gender = appearance.gender,
      .class_id = class_id,
      .skin_color = appearance.skin_color,
      .face = appearance.face,
      .hair_style = appearance.hair_style,
      .hair_color = appearance.hair_color,
      .facial_hair = appearance.facial_hair,
  };
  for (std::size_t slot = 0u;
       slot < selection.equipment_display_ids.size(); ++slot) {
    selection.equipment_display_ids[slot] =
        appearance.equipment[slot].display_id;
  }
  return selection;
}

openwow::render::CharacterAppearance MakeCharacterAppearance(
    const openwow::net::wotlk::CharacterSummary &character) {
  openwow::render::CharacterAppearance appearance;
  appearance.race = character.race_id;
  appearance.gender = character.gender;
  appearance.skin_color = character.skin;
  appearance.face = character.face;
  appearance.hair_style = character.hair_style;
  appearance.hair_color = character.hair_color;
  appearance.facial_hair = character.facial_hair;
  for (std::size_t slot = 0; slot < appearance.equipment.size(); ++slot) {
    appearance.equipment[slot].display_id = character.equipment[slot].display_id;
    appearance.equipment[slot].inventory_type = character.equipment[slot].inv_type;
    appearance.equipment[slot].enchant_visual = character.equipment[slot].enchant_id;
  }
  return appearance;
}

bool CharacterAppearancesEqual(const openwow::render::CharacterAppearance &lhs,
                               const openwow::render::CharacterAppearance &rhs) {
  if (lhs.race != rhs.race || lhs.gender != rhs.gender ||
      lhs.skin_color != rhs.skin_color || lhs.face != rhs.face ||
      lhs.hair_style != rhs.hair_style || lhs.hair_color != rhs.hair_color ||
      lhs.facial_hair != rhs.facial_hair) {
    return false;
  }

  for (std::size_t slot = 0; slot < lhs.equipment.size(); ++slot) {
    const auto &left = lhs.equipment[slot];
    const auto &right = rhs.equipment[slot];
    if (left.display_id != right.display_id ||
        left.inventory_type != right.inventory_type ||
        left.enchant_visual != right.enchant_visual) {
      return false;
    }
  }
  return true;
}

constexpr std::uint32_t kCharacterFlagGhost = 0x2000u;
constexpr std::uint32_t kGhostSpellVisualKitId = 989u;
constexpr std::uint8_t kInventoryTypeShield = 14u;
constexpr std::uint8_t kInventoryTypeRanged = 15u;
constexpr std::uint8_t kInventoryTypeTwoHand = 17u;
constexpr std::uint8_t kInventoryTypeTabard = 19u;
constexpr std::uint8_t kInventoryTypeRobe = 20u;
constexpr std::uint8_t kInventoryTypeMainHand = 21u;
constexpr std::uint8_t kInventoryTypeOffHand = 22u;
constexpr std::uint8_t kInventoryTypeRangedRight = 26u;
constexpr std::uint8_t kInventoryTypeQuiver = 27u;
constexpr std::uint8_t kHunterClassId = 3u;
constexpr std::array<std::uint32_t, 3> kStoredItemSlots{{15u, 16u, 17u}};
constexpr std::uint32_t kHandsClosedAnimationId = 15u;
constexpr std::string_view kHeadTexturePathPrefix = "Item\\ObjectComponents\\Head\\";
constexpr std::string_view kQuiverModelPathPrefix = "Item\\ObjectComponents\\Quiver\\";

struct GhostAttachmentSpec {
  std::uint32_t openwow::data::dbc::SpellVisualKitEntry::*effect_id;
  std::uint32_t attachment_index;
};

constexpr std::array<GhostAttachmentSpec, 9> kGhostAttachmentSpecs{{
    {&openwow::data::dbc::SpellVisualKitEntry::head_effect, 20u},
    {&openwow::data::dbc::SpellVisualKitEntry::chest_effect, 34u},
    {&openwow::data::dbc::SpellVisualKitEntry::base_effect, 19u},
    {&openwow::data::dbc::SpellVisualKitEntry::left_hand_effect, 21u},
    {&openwow::data::dbc::SpellVisualKitEntry::right_hand_effect, 22u},
    {&openwow::data::dbc::SpellVisualKitEntry::breath_effect, 17u},
    {&openwow::data::dbc::SpellVisualKitEntry::special1_effect, 23u},
    {&openwow::data::dbc::SpellVisualKitEntry::special2_effect, 24u},
    {&openwow::data::dbc::SpellVisualKitEntry::special3_effect, 25u},
}};

std::string BuildObjectComponentModelPath(const std::string_view prefix,
                                          const std::string_view model_name) {
  if (model_name.empty()) {
    return {};
  }

  std::string path(prefix);
  path.append(model_name);
  return path;
}

std::string BuildObjectComponentTexturePath(const std::string_view prefix,
                                            const std::string_view texture_stem) {
  if (texture_stem.empty()) {
    return {};
  }

  std::string path(prefix);
  path.append(texture_stem);
  path.append(".blp");
  return path;
}

std::string BuildDisplayTextureOverridePath(const std::string_view model_name,
                                            const std::string_view texture_name) {
  if (model_name.empty() || texture_name.empty()) {
    return {};
  }

  const auto separator = model_name.find_last_of('\\');
  if (separator == std::string_view::npos) {
    return std::string(texture_name);
  }

  std::string path(model_name.substr(0, separator + 1));
  path.append(texture_name);
  return path;
}

double ResolvePetFamilyScaleMultiplierImpl(
    const openwow::data::dbc::CreatureFamilyEntry &family,
    const std::uint32_t pet_level) {
  const auto min_level = static_cast<int>(family.min_scale_level);
  const auto max_level = static_cast<int>(family.max_scale_level);
  if (max_level < min_level) {
    return static_cast<double>(family.min_scale);
  }
  if (max_level == min_level) {
    return static_cast<int>(pet_level) < min_level
               ? static_cast<double>(family.min_scale)
               : static_cast<double>(family.max_scale);
  }
  const auto level_delta =
      static_cast<int>(pet_level) < min_level ? 0 : static_cast<int>(pet_level) - min_level;
  const auto clamped_delta = std::min(level_delta, max_level - min_level);
  const auto level_range = static_cast<double>(max_level - min_level);
  return (static_cast<double>(clamped_delta) / level_range) *
             static_cast<double>(family.max_scale - family.min_scale) +
         static_cast<double>(family.min_scale);
}

std::optional<std::uint32_t> ResolveCreatePreviewEquipmentSlot(const std::uint8_t class_id,
                                                               const std::uint32_t inventory_type) {

  switch (inventory_type) {
  case 1:
    return 0u;
  case 3:
    return 2u;
  case 4:
    return 3u;
  case 5:
  case kInventoryTypeRobe:
    return 4u;
  case 6:
    return 5u;
  case 7:
    return 6u;
  case 8:
    return 7u;
  case 9:
    return 8u;
  case 10:
    return 9u;
  case 16:
    return 14u;
  case kInventoryTypeTabard:
    return 18u;
  case 13:
  case kInventoryTypeTwoHand:
  case kInventoryTypeMainHand:
    return class_id == kHunterClassId ? std::nullopt : std::optional<std::uint32_t>{15u};
  case kInventoryTypeShield:
    return 16u;
  case kInventoryTypeOffHand:
    return class_id == kHunterClassId ? std::nullopt : std::optional<std::uint32_t>{16u};
  case kInventoryTypeRanged:
  case kInventoryTypeRangedRight:
    return class_id == kHunterClassId ? std::optional<std::uint32_t>{17u} : std::nullopt;
  default:
    return std::nullopt;
  }
}

}

double detail::ResolvePetFamilyScaleMultiplier(
    const openwow::data::dbc::CreatureFamilyEntry& family,
    const std::uint32_t pet_level) {
  return ResolvePetFamilyScaleMultiplierImpl(family, pet_level);
}

bool GlueCharSelectScene::SelectedCharacterSourcesEqual(
    const SelectedCharacterSource &lhs,
    const SelectedCharacterSource &rhs) {
  return SameStableCharacterDisplayOwner(lhs.owner, rhs.owner) &&
         lhs.model_path == rhs.model_path && lhs.class_id == rhs.class_id &&
         lhs.ghost == rhs.ghost && lhs.pet_display_id == rhs.pet_display_id &&
         lhs.pet_level == rhs.pet_level && lhs.pet_family == rhs.pet_family &&
         CharacterAppearancesEqual(lhs.appearance, rhs.appearance);
}

GlueCharSelectScene::GlueCharSelectScene() {
  Reset();
}

void GlueCharSelectScene::SetContentReleaseCallback(std::function<void()> callback) {
  content_release_callback_ = std::move(callback);
}

void GlueCharSelectScene::NotifyContentRelease() {
  if (content_release_callback_) {
    content_release_callback_();
  }
}

void GlueCharSelectScene::ResetSelectedCharacterAppearance() {
  selected_character_appearance_ = {};
  appearance_texture_sources_ = {};
  selected_character_class_id_ = 0u;
  character_equipment_pose_ = {};
  selected_character_appearance_.race = 0;

  appearance_geosets_ = {};
}

void GlueCharSelectScene::ResetCharacterEquipmentModels() {
  for (auto &equipment : character_equipment_models_) {
    equipment.equipment_slot = 0;
    equipment.attachment_index = 0;
    equipment.model_path.clear();
    equipment.texture_path.clear();
    equipment.selection_triangle_candidate = false;
    equipment.active = false;
    if (equipment.node_id != 0) {
      graph_.SetPosition(equipment.node_id, 0.0f, 0.0f, 0.0f);
      graph_.SetRotation(equipment.node_id, {});
      graph_.SetScale(equipment.node_id, 1.0f, 1.0f, 1.0f);
    }
    for (auto &child : equipment.child_models) {
      child.attachment_index = 0;
      child.model_path.clear();
      child.active = false;
      if (child.node_id != 0) {
        graph_.SetPosition(child.node_id, 0.0f, 0.0f, 0.0f);
        graph_.SetRotation(child.node_id, {});
        graph_.SetScale(child.node_id, 1.0f, 1.0f, 1.0f);
      }
    }
  }
}

void GlueCharSelectScene::ResetCharacterVisualEffects() {
  character_model_alpha_ = 1.0f;
  character_model_tint_ = {1.0f, 1.0f, 1.0f};
  for (auto &effect : character_effect_models_) {
    effect.attachment_index = 0;
    effect.model_path.clear();
    effect.scale = 1.0f;
    effect.active = false;
    if (effect.node_id != 0) {
      graph_.SetPosition(effect.node_id, 0.0f, 0.0f, 0.0f);
      graph_.SetRotation(effect.node_id, {});
      graph_.SetScale(effect.node_id, 1.0f, 1.0f, 1.0f);
    }
  }
}

void GlueCharSelectScene::ResetCurrentDisplayContent() {
  current_display_owner_ = {};
  selected_character_source_.reset();
  selected_character_model_path_.clear();
  selected_character_display_initialized_ = false;
  selected_character_is_ghost_ = false;
  prop_model_path_.reset();
  prop_texture_paths_.fill({});
  prop_model_alpha_ = 1.0f;
  graph_.SetPosition(prop_node_, 0.0f, 0.0f, 0.0f);
  graph_.SetRotation(prop_node_, {});
  graph_.SetScale(prop_node_, 1.0f, 1.0f, 1.0f);
  ResetSelectedCharacterAppearance();
  ResetCharacterEquipmentModels();
  ResetCharacterVisualEffects();
}

void GlueCharSelectScene::ReleaseContent() {
  NotifyContentRelease();
  ResetCurrentDisplayContent();
  RebuildAttachments();
  ApplySelectFacing(0.0f);
}

void GlueCharSelectScene::Reset() {
  NotifyContentRelease();
  graph_.Clear();
  attachments_ = openwow::render::AttachmentGraph(&graph_);

  background_root_node_ =
      graph_.CreateNode(openwow::render::RenderableType::Unknown, 0.0f, 0.0f, 0.0f);
  character_node_ = graph_.CreateNode(openwow::render::RenderableType::M2, 0.0f, 0.0f, 0.0f);
  prop_node_ = graph_.CreateNode(openwow::render::RenderableType::M2, 0.0f, 0.0f, 0.0f);
  for (auto &effect : character_effect_models_) {
    effect.node_id = graph_.CreateNode(openwow::render::RenderableType::M2, 0.0f, 0.0f, 0.0f);
  }
  for (auto &equipment : character_equipment_models_) {
    equipment.node_id = graph_.CreateNode(openwow::render::RenderableType::M2, 0.0f, 0.0f, 0.0f);
    for (auto &child : equipment.child_models) {
      child.node_id = graph_.CreateNode(openwow::render::RenderableType::M2, 0.0f, 0.0f, 0.0f);
    }
  }

  ResetCurrentDisplayContent();
  prop_model_path_.reset();
  selected_character_display_initialized_ = false;
  character_display_preloads_.clear();

  RebuildAttachments();
  ApplySelectFacing(0.0f);
}

void GlueCharSelectScene::RebuildAttachments() {

  attachments_.Detach(character_node_);
  attachments_.Detach(prop_node_);
  for (const auto &effect : character_effect_models_) {
    attachments_.Detach(effect.node_id);
  }
  for (const auto &equipment : character_equipment_models_) {
    for (const auto &child : equipment.child_models) {
      attachments_.Detach(child.node_id);
    }
    attachments_.Detach(equipment.node_id);
  }

  (void)attachments_.Attach(character_node_, background_root_node_, 0u);
  if (prop_model_path_.has_value() && !prop_model_path_->empty()) {
    (void)attachments_.Attach(prop_node_, background_root_node_, 1u);
  }
  for (const auto &effect : character_effect_models_) {
    if (!effect.active || effect.model_path.empty()) {
      continue;
    }
    (void)attachments_.Attach(effect.node_id, character_node_, effect.attachment_index);
  }
  for (const auto &equipment : character_equipment_models_) {
    if (!equipment.active || equipment.model_path.empty()) {
      continue;
    }
    (void)attachments_.Attach(equipment.node_id, character_node_, equipment.attachment_index);
    for (const auto &child : equipment.child_models) {
      if (!child.active || child.model_path.empty()) {
        continue;
      }
      (void)attachments_.Attach(child.node_id, equipment.node_id, child.attachment_index);
    }
  }
}

void GlueCharSelectScene::BindAppearanceDbcStores(
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharHairGeosetsEntry> *hair_geosets,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>
        *facial_hair_styles,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ChrRacesEntry> *chr_races,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemDisplayInfoEntry> *item_display_info,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::HelmetGeosetVisDataEntry>
        *helmet_geoset_vis_data,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> *char_sections,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellVisualKitEntry> *spell_visual_kit,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellVisualEffectNameEntry>
        *spell_visual_effect_name,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharStartOutfitEntry>
        *char_start_outfit,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemVisualEffectsEntry>
        *item_visual_effects,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemVisualsEntry> *item_visuals,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ParticleColorEntry> *particle_color,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CreatureDisplayInfoEntry>
        *creature_display_info,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CreatureModelDataEntry>
        *creature_model_data,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CreatureFamilyEntry> *creature_family) {
  char_sections_ = char_sections;
  hair_geosets_ = hair_geosets;
  facial_hair_styles_ = facial_hair_styles;
  chr_races_ = chr_races;
  item_display_info_ = item_display_info;
  helmet_geoset_vis_data_ = helmet_geoset_vis_data;
  spell_visual_kit_ = spell_visual_kit;
  spell_visual_effect_name_ = spell_visual_effect_name;
  char_start_outfit_ = char_start_outfit;
  item_visual_effects_ = item_visual_effects;
  item_visuals_ = item_visuals;
  particle_color_ = particle_color;
  creature_display_info_ = creature_display_info;
  creature_model_data_ = creature_model_data;
  creature_family_ = creature_family;
  character_display_preloads_.clear();
  RefreshCharacterAppearanceTextureSources();
  RefreshAppearanceGeosets();
  RefreshCharacterEquipmentModels();
  RebuildAttachments();
}

void GlueCharSelectScene::RefreshCharacterAppearanceTextureSources() {
  const auto &appearance = selected_character_appearance_;
  const auto selection =
      MakeAppearanceSelection(appearance, selected_character_class_id_);

  appearance_texture_sources_ =
      openwow::render::BuildCharacterAppearanceTextureSources(
          selection, char_sections_, item_display_info_, chr_races_);
}

void GlueCharSelectScene::RefreshAppearanceGeosets() {
  const auto &appearance = selected_character_appearance_;
  const auto selection =
      MakeAppearanceSelection(appearance, selected_character_class_id_);

  appearance_geosets_ =
      openwow::render::BuildCharacterAppearanceGeosetState(
          selection,
          {
              .hair_geosets = hair_geosets_,
              .facial_hair_styles = facial_hair_styles_,
              .item_display_info = item_display_info_,
              .helmet_geoset_vis_data = helmet_geoset_vis_data_,
              .char_sections = char_sections_,
          });
}

void GlueCharSelectScene::RefreshCharacterVisualEffects(bool ghost_character) {
  ResetCharacterVisualEffects();
  if (!ghost_character || spell_visual_kit_ == nullptr || spell_visual_effect_name_ == nullptr) {
    return;
  }

  const auto *kit = spell_visual_kit_->LookupEntry(kGhostSpellVisualKitId);
  if (kit == nullptr) {
    return;
  }

  std::size_t effect_index = 0;
  for (const auto &spec : kGhostAttachmentSpecs) {
    if (effect_index >= character_effect_models_.size()) {
      break;
    }
    const std::uint32_t effect_id = kit->*spec.effect_id;
    if (effect_id == 0) {
      continue;
    }
    const auto *effect_name = spell_visual_effect_name_->LookupEntry(effect_id);
    if (effect_name == nullptr || effect_name->file_path.empty()) {
      continue;
    }

    auto &effect = character_effect_models_[effect_index++];
    effect.attachment_index = spec.attachment_index;
    effect.model_path = NormalizeM2Path(std::string(effect_name->file_path));
    effect.scale = effect_name->scale;
    effect.active = !effect.model_path.empty();
    graph_.SetPosition(effect.node_id, 0.0f, 0.0f, 0.0f);
    graph_.SetRotation(effect.node_id, {});
    graph_.SetScale(effect.node_id, effect.scale, effect.scale, effect.scale);
  }

  for (std::size_t i = 0; i < 4; ++i) {
    const auto proc_type = kit->proc_type[i];
    const float proc_value = kit->proc_param_zero[i];
    if (proc_type == 1u) {
      const auto color = static_cast<std::uint32_t>(proc_value);
      character_model_tint_[0] = static_cast<float>((color >> 16) & 0xFFu) / 255.0f;
      character_model_tint_[1] = static_cast<float>((color >> 8) & 0xFFu) / 255.0f;
      character_model_tint_[2] = static_cast<float>(color & 0xFFu) / 255.0f;
    } else if (proc_type == 14u) {
      character_model_alpha_ = std::clamp(proc_value, 0.0f, 1.0f);
    }
  }
}

void GlueCharSelectScene::RefreshCharacterEquipmentModels() {
  ResetCharacterEquipmentModels();
  character_equipment_pose_ = {};
  if (item_display_info_ == nullptr) {
    return;
  }

  std::size_t next_model = 0u;
  const auto append_model = [&](const std::uint32_t equipment_slot,
                                const std::uint32_t attachment_index,
                                std::string model_path, std::string texture_path,
                                const openwow::data::dbc::ItemDisplayInfoEntry &display,
                                const bool selection_triangle_candidate = false,
                                const std::uint32_t item_visuals_override = 0u) {
    if (model_path.empty() || next_model >= character_equipment_models_.size()) {
      return;
    }

    auto &target = character_equipment_models_[next_model++];
    target.equipment_slot = equipment_slot;
    target.attachment_index = attachment_index;
    target.model_path = NormalizeM2Path(std::move(model_path));
    target.texture_path = std::move(texture_path);
    target.selection_triangle_candidate = selection_triangle_candidate;
    target.active = !target.model_path.empty();

    if (attachment_index == openwow::render::m2::kM2AttachmentLookupRightPalm) {
      for (std::uint32_t slot = kRightFingerAnimationSlotBegin;
           slot < kRightFingerAnimationSlotEnd; ++slot) {
        character_equipment_pose_.key_bone_animation_ids[slot] =
            kHandsClosedAnimationId;
      }
    } else if (attachment_index == openwow::render::m2::kM2AttachmentLookupLeftPalm) {
      for (std::uint32_t slot = kLeftFingerAnimationSlotBegin;
           slot < kLeftFingerAnimationSlotEnd; ++slot) {
        character_equipment_pose_.key_bone_animation_ids[slot] =
            kHandsClosedAnimationId;
      }
    }

    const auto item_visuals_id =
        item_visuals_override != 0u ? item_visuals_override : display.item_visuals_id;
    if (item_visuals_ == nullptr || item_visual_effects_ == nullptr || item_visuals_id == 0u) {
      return;
    }
    const auto *visuals = item_visuals_->LookupEntry(item_visuals_id);
    if (visuals == nullptr) {
      return;
    }
    for (std::size_t index = 0; index < visuals->slot.size(); ++index) {
      const auto effect_id = visuals->slot[index];
      if (effect_id == 0u) {
        continue;
      }
      const auto *effect = item_visual_effects_->LookupEntry(effect_id);
      if (effect == nullptr || effect->model.empty()) {
        continue;
      }
      auto &child = target.child_models[index];
      child.attachment_index = static_cast<std::uint32_t>(index);
      child.model_path = NormalizeM2Path(std::string(effect->model));
      child.active = !child.model_path.empty();
    }
  };

  const auto resolve_display = [&](const std::size_t slot) {
    const auto display_id = selected_character_appearance_.equipment[slot].display_id;
    return display_id == 0u ? nullptr : item_display_info_->LookupEntry(display_id);
  };

  if (const auto *display = resolve_display(0u); display != nullptr) {
    const auto *race = chr_races_ != nullptr
                           ? chr_races_->LookupEntry(selected_character_appearance_.race)
                           : nullptr;
    if (race != nullptr && !race->model_client_prefix.empty()) {
      append_model(0u, openwow::render::m2::kM2AttachmentLookupHelm,
                   openwow::game::EquipmentVisualSystem::BuildHeadModelPath(
                       display->model_name_left, race->model_client_prefix,
                       selected_character_appearance_.gender),
                   BuildObjectComponentTexturePath(kHeadTexturePathPrefix,
                                                   display->texture_name_left),
                   *display);
    }
  }

  if (const auto *display = resolve_display(2u); display != nullptr) {
    append_model(2u, openwow::render::m2::kM2AttachmentLookupLeftShoulder,
                 openwow::game::EquipmentVisualSystem::BuildShoulderModelPath(
                     display->model_name_left),
                 openwow::game::EquipmentVisualSystem::BuildShoulderTexturePath(
                     display->texture_name_left),
                 *display);
    append_model(2u, openwow::render::m2::kM2AttachmentLookupRightShoulder,
                 openwow::game::EquipmentVisualSystem::BuildShoulderModelPath(
                     display->model_name_right),
                 openwow::game::EquipmentVisualSystem::BuildShoulderTexturePath(
                     display->texture_name_right),
                 *display);
  }

  for (const auto equipment_slot : kStoredItemSlots) {

    if (equipment_slot == 17u && selected_character_class_id_ != kHunterClassId) {
      continue;
    }
    const auto &source = selected_character_appearance_.equipment[equipment_slot];
    const auto *display = resolve_display(equipment_slot);
    if (display == nullptr) {
      continue;
    }

    if (equipment_slot == 17u && source.inventory_type == kInventoryTypeQuiver) {
      append_model(equipment_slot, openwow::render::m2::kM2AttachmentLookupSheathMainHand,
                   BuildObjectComponentModelPath(kQuiverModelPathPrefix,
                                                 display->model_name_left),
                   BuildObjectComponentTexturePath(kQuiverModelPathPrefix,
                                                   display->texture_name_left),
                   *display, false, source.enchant_visual);
      continue;
    }

    const auto slot = static_cast<openwow::game::EquipmentSlot>(equipment_slot);
    const auto dispatch = openwow::game::EquipmentVisualSystem::DispatchStoredItemModel(
        equipment_slot, 0u, false, source.inventory_type == kInventoryTypeShield,
        openwow::game::EquipmentVisualSystem::UsePrimarySheathedWeaponSide(
            slot, source.inventory_type),
        display->model_name_left, display->texture_name_left);
    if (dispatch.targetAttachmentId < 0) {
      continue;
    }
    append_model(equipment_slot, static_cast<std::uint32_t>(dispatch.targetAttachmentId),
                 dispatch.modelPath, dispatch.texturePath, *display,
                 equipment_slot == 17u && source.inventory_type == kInventoryTypeRanged,
                 source.enchant_visual);
  }
}

bool GlueCharSelectScene::HasValidBaseSkinSelection(
    const openwow::render::CharacterAppearance &appearance) const {
  if (char_sections_ == nullptr) {
    return true;
  }

  return detail::HasUnfilteredCharacterCustomizationRow(
      char_sections_->entries(), MakeCustomizationState(appearance), 0u, 0, appearance.skin_color);
}

void GlueCharSelectScene::PopulateCreatePreviewEquipment(openwow::render::CharacterAppearance &appearance,
                                                         const GlueGameState &gs) {
  for (auto &slot : appearance.equipment) {
    slot = {};
  }

  if (const auto *character = GetCustomizationSourceCharacter(gs); character != nullptr) {
    for (std::size_t slot = 0; slot < appearance.equipment.size(); ++slot) {
      appearance.equipment[slot].display_id = character->equipment[slot].display_id;
      appearance.equipment[slot].inventory_type = character->equipment[slot].inv_type;
      appearance.equipment[slot].enchant_visual = character->equipment[slot].enchant_id;
    }
    return;
  }

  if (char_start_outfit_ == nullptr) {
    return;
  }

  const auto race_id = static_cast<std::uint8_t>(std::max(gs.create_race, 0));
  const auto class_id = static_cast<std::uint8_t>(std::max(gs.create_class, 0));
  const auto gender = static_cast<std::uint8_t>(std::max(gs.create_sex, 0));

  for (const auto &entry : char_start_outfit_->entries()) {
    if (entry.race != race_id || entry.class_id != class_id || entry.gender != gender) {
      continue;
    }

    for (std::size_t index = 0; index < entry.display_id.size(); ++index) {
      if (entry.display_id[index] <= 0 || entry.inv_type[index] <= 0) {
        continue;
      }

      const auto slot = ResolveCreatePreviewEquipmentSlot(
          class_id, static_cast<std::uint32_t>(entry.inv_type[index]));
      if (!slot.has_value() || *slot >= appearance.equipment.size()) {
        continue;
      }

      auto &target = appearance.equipment[*slot];
      target.display_id = static_cast<std::uint32_t>(entry.display_id[index]);
      target.inventory_type = static_cast<std::uint8_t>(entry.inv_type[index]);
      target.enchant_visual = 0;
    }
    return;
  }
}

void GlueCharSelectScene::RefreshCharacterDisplayPreloads(const GlueGameState &gs) {
  bool unchanged = character_display_preloads_.size() == gs.characters.size();
  if (unchanged) {
    for (std::size_t index = 0; index < gs.characters.size(); ++index) {
      const auto &character = gs.characters[index];
      const CharacterDisplayOwner owner{
          .kind = CharacterDisplayOwnerKind::kCharacterListRow,
          .character_id = character.id,
          .row_index = index,
      };
      const auto &preload = character_display_preloads_[index];
      if (preload.owner != owner ||
          preload.class_id != character.class_id ||
          !CharacterAppearancesEqual(preload.appearance,
                                     MakeCharacterAppearance(character))) {
        unchanged = false;
        break;
      }
    }
  }
  if (unchanged) {
    return;
  }

  character_display_preloads_.clear();
  character_display_preloads_.reserve(gs.characters.size());
  const auto active_appearance = selected_character_appearance_;
  const auto active_model_path = selected_character_model_path_;
  const auto active_class_id = selected_character_class_id_;
  for (std::size_t index = 0; index < gs.characters.size(); ++index) {
    const auto &character = gs.characters[index];
    auto appearance = MakeCharacterAppearance(character);
    const std::string model_path = NormalizeM2Path(
        openwow::render::CharacterModelPath(appearance.race,
                                            appearance.gender));
    CharacterDisplayPreload preload{
        .owner = {
            .kind = CharacterDisplayOwnerKind::kCharacterListRow,
            .character_id = character.id,
            .row_index = index,
        },
        .appearance = appearance,
        .class_id = character.class_id,
        .model_path = HasValidBaseSkinSelection(appearance) ? model_path : std::string{},
        .appearance_sources =
            model_path.empty() || !HasValidBaseSkinSelection(appearance)
                ? openwow::render::CharacterAppearanceTextureSources{}
                : openwow::render::BuildCharacterAppearanceTextureSources(
                      MakeAppearanceSelection(appearance, character.class_id),
                      char_sections_, item_display_info_, chr_races_),
    };
    selected_character_appearance_ = appearance;
    selected_character_model_path_ = model_path;
    selected_character_class_id_ = character.class_id;
    RefreshCharacterEquipmentModels();
    for (const auto &equipment : character_equipment_models_) {
      if (!equipment.active) {
        continue;
      }
      preload.component_model_paths.push_back(equipment.model_path);
      for (const auto &child : equipment.child_models) {
        if (child.active) {
          preload.component_model_paths.push_back(child.model_path);
        }
      }
    }
    std::sort(preload.component_model_paths.begin(),
              preload.component_model_paths.end());
    preload.component_model_paths.erase(
        std::unique(preload.component_model_paths.begin(),
                    preload.component_model_paths.end()),
        preload.component_model_paths.end());
    character_display_preloads_.push_back(std::move(preload));
  }
  selected_character_appearance_ = active_appearance;
  selected_character_model_path_ = active_model_path;
  selected_character_class_id_ = active_class_id;
  RefreshCharacterEquipmentModels();
}

void GlueCharSelectScene::SyncSelectedCharacter(GlueGameState &gs) {
  RefreshCharacterDisplayPreloads(gs);
  const int idx = gs.selected_character_index;
  if (idx < 0 || idx >= static_cast<int>(gs.characters.size())) {
    if (current_display_owner_.valid() || selected_character_source_.has_value()) {
      ResetCurrentDisplayContent();
      RebuildAttachments();
    }
    return;
  }

  const auto &c = gs.characters[static_cast<std::size_t>(idx)];
  const auto appearance = MakeCharacterAppearance(c);

  const std::string model_path =
      NormalizeM2Path(openwow::render::CharacterModelPath(
          appearance.race, appearance.gender));
  if (model_path.empty() || !HasValidBaseSkinSelection(appearance)) {
    ResetCurrentDisplayContent();
    RebuildAttachments();
    return;
  }

  const SelectedCharacterSource source{
      .owner = {
          .kind = CharacterDisplayOwnerKind::kCharacterListRow,
          .character_id = c.id,
          .row_index = static_cast<std::size_t>(idx),
      },
      .appearance = appearance,
      .model_path = model_path,
      .class_id = c.class_id,
      .ghost = (c.char_flags & kCharacterFlagGhost) != 0u,
      .pet_display_id = c.pet_display_id,
      .pet_level = c.pet_level,
      .pet_family = c.pet_family,
  };
  if (selected_character_source_.has_value() &&
      SelectedCharacterSourcesEqual(*selected_character_source_, source)) {

    current_display_owner_ = source.owner;
    selected_character_source_->owner = source.owner;
    return;
  }
  const bool owner_changed =
      !SameStableCharacterDisplayOwner(current_display_owner_, source.owner);

  ResetCurrentDisplayContent();
  current_display_owner_ = source.owner;
  selected_character_source_ = source;
  selected_character_model_path_ = model_path;
  selected_character_display_initialized_ = true;
  has_constructed_selected_character_display_ = true;
  selected_character_is_ghost_ = source.ghost;
  if (owner_changed) {
    gs.select_facing = 0.0f;
  }
  selected_character_appearance_ = appearance;
  selected_character_class_id_ = c.class_id;
  RefreshCharacterAppearanceTextureSources();
  RefreshAppearanceGeosets();
  RefreshCharacterEquipmentModels();
  RefreshCharacterVisualEffects(selected_character_is_ghost_);
  prop_model_path_.reset();
  prop_texture_paths_.fill({});
  prop_model_alpha_ = 1.0f;
  graph_.SetPosition(prop_node_, 0.0f, 0.0f, 0.0f);
  graph_.SetRotation(prop_node_, {});
  graph_.SetScale(prop_node_, 1.0f, 1.0f, 1.0f);
  if (c.pet_display_id != 0 && creature_display_info_ != nullptr && creature_model_data_ != nullptr) {
    if (const auto *display_info = creature_display_info_->LookupEntry(c.pet_display_id);
        display_info != nullptr) {
      if (const auto *model_data = creature_model_data_->LookupEntry(display_info->model_id);
          model_data != nullptr) {
        const std::string prop_model_path = NormalizeM2Path(std::string(model_data->model_name));
        if (!prop_model_path.empty()) {
          prop_model_path_ = prop_model_path;
          for (std::size_t texture_index = 0; texture_index < prop_texture_paths_.size();
               ++texture_index) {
            prop_texture_paths_[texture_index] = BuildDisplayTextureOverridePath(
                model_data->model_name, display_info->texture_variation[texture_index]);
          }
          prop_model_alpha_ = static_cast<float>(display_info->model_alpha) / 255.0f;
          double prop_scale =
              static_cast<double>(model_data->scale) * static_cast<double>(display_info->scale);
          if (creature_family_ != nullptr) {
            if (const auto *family = creature_family_->LookupEntry(c.pet_family); family != nullptr) {
              prop_scale *= detail::ResolvePetFamilyScaleMultiplier(*family,
                                                                     c.pet_level);
            }
          }
          const float final_prop_scale = static_cast<float>(prop_scale);
          graph_.SetScale(prop_node_, final_prop_scale, final_prop_scale, final_prop_scale);
        }
      }
    }
  }
  ApplySelectFacing(gs.select_facing);
  RebuildAttachments();
}

void GlueCharSelectScene::SyncFromGameState(GlueGameState &gs) {
  SyncSelectedCharacter(gs);
}

void GlueCharSelectScene::RefreshFromGameState(GlueGameState &gs) {

  SyncSelectedCharacter(gs);
}

void GlueCharSelectScene::SyncCreateCharacter(const GlueGameState &gs) {
  openwow::render::CharacterAppearance appearance;
  appearance.race = static_cast<std::uint8_t>(std::max(gs.create_race, 0));
  appearance.gender = static_cast<std::uint8_t>(std::max(gs.create_sex, 0));
  appearance.skin_color =
      static_cast<std::uint8_t>(std::max(gs.create_skin, 0));
  appearance.face = static_cast<std::uint8_t>(std::max(gs.create_face, 0));
  appearance.hair_style =
      static_cast<std::uint8_t>(std::max(gs.create_hair_style, 0));
  appearance.hair_color =
      static_cast<std::uint8_t>(std::max(gs.create_hair_color, 0));
  appearance.facial_hair =
      static_cast<std::uint8_t>(std::max(gs.create_facial_hair, 0));
  PopulateCreatePreviewEquipment(appearance, gs);

  const std::string model_path =
      NormalizeM2Path(openwow::render::CharacterModelPath(
          appearance.race, appearance.gender));
  if (model_path.empty() || !HasValidBaseSkinSelection(appearance)) {
    if (selected_character_display_initialized_ || !selected_character_model_path_.empty()) {
      ResetCurrentDisplayContent();
      RebuildAttachments();
    }
    return;
  }

  const auto *source_character = GetCustomizationSourceCharacter(gs);
  const std::uint8_t class_id =
      source_character != nullptr
          ? source_character->class_id
          : static_cast<std::uint8_t>(std::max(gs.create_class, 0));
  if (selected_character_display_initialized_ &&
      current_display_owner_.kind == CharacterDisplayOwnerKind::kCreatePreview &&
      selected_character_model_path_ == model_path &&
      selected_character_class_id_ == class_id &&
      CharacterAppearancesEqual(selected_character_appearance_, appearance)) {
    return;
  }

  if (!selected_character_display_initialized_ ||
      selected_character_model_path_ != model_path) {
    ResetCurrentDisplayContent();
  }
  current_display_owner_ = {
      .kind = CharacterDisplayOwnerKind::kCreatePreview,
  };
  selected_character_source_.reset();
  selected_character_model_path_ = model_path;
  selected_character_display_initialized_ = true;
  selected_character_is_ghost_ = false;
  selected_character_appearance_ = appearance;
  selected_character_class_id_ = class_id;
  RefreshCharacterAppearanceTextureSources();
  RefreshAppearanceGeosets();
  RefreshCharacterEquipmentModels();
  ResetCharacterVisualEffects();
  prop_model_path_.reset();
  prop_texture_paths_.fill({});
  prop_model_alpha_ = 1.0f;
  graph_.SetPosition(prop_node_, 0.0f, 0.0f, 0.0f);
  graph_.SetRotation(prop_node_, {});
  graph_.SetScale(prop_node_, 1.0f, 1.0f, 1.0f);
  ApplyCreateFacing(gs.create_facing);
  RebuildAttachments();
}

void GlueCharSelectScene::SyncCreateFromGameState(const GlueGameState &gs) {
  SyncCreateCharacter(gs);
}

void GlueCharSelectScene::RefreshCreateFromGameState(const GlueGameState &gs) {

  SyncCreateCharacter(gs);
}

void GlueCharSelectScene::ApplyFacing(float yaw_radians) {

  openwow::render::Mat4 facing{};
  const auto matrix = openwow::render::PrependRotationMatrix4x4Z(
      openwow::render::kRenderIdentityMatrix4x4, yaw_radians);
  std::copy(matrix.begin(), matrix.end(), facing.m);
  graph_.SetLocalAffineTransform(character_node_, facing);
}

void GlueCharSelectScene::ApplySelectFacing(float yaw_radians) {
  ApplyFacing(yaw_radians);
}

void GlueCharSelectScene::ApplyCreateFacing(float yaw_radians) {
  ApplyFacing(yaw_radians);
}

bool GlueCharSelectScene::HasInitializedSelectedCharacterDisplay() const {
  return selected_character_display_initialized_;
}

bool GlueCharSelectScene::HasConstructedSelectedCharacterDisplay() const {
  return has_constructed_selected_character_display_;
}

bool GlueCharSelectScene::HasActiveSelectedCharacterDisplay() const {
  return selected_character_display_initialized_ && !selected_character_model_path_.empty();
}

bool GlueCharSelectScene::SelectedCharacterIsGhost() const {
  return selected_character_is_ghost_;
}

const CharacterDisplayOwner &GlueCharSelectScene::current_display_owner() const noexcept {
  return current_display_owner_;
}

std::optional<float> GlueCharSelectScene::viewport_death_effect_alpha() const noexcept {
  if (!HasActiveSelectedCharacterDisplay() || !selected_character_is_ghost_) {
    return std::nullopt;
  }
  return kGhostDeathFfxAlpha;
}

void GlueCharSelectScene::SetSelectedCharacterModelPath(std::string m2_path) {
  selected_character_model_path_ = NormalizeM2Path(std::move(m2_path));
}

const std::string &GlueCharSelectScene::selected_character_model_path() const {
  return selected_character_model_path_;
}

const openwow::render::CharacterAppearance &
GlueCharSelectScene::selected_character_appearance() const {
  return selected_character_appearance_;
}

const openwow::render::CharacterAppearanceTextureSources &
GlueCharSelectScene::character_appearance_texture_sources() const {
  return appearance_texture_sources_;
}

const std::vector<GlueCharSelectScene::CharacterDisplayPreload> &
GlueCharSelectScene::character_display_preloads() const {
  return character_display_preloads_;
}

bool GlueCharSelectScene::IsAppearanceGeosetVisible(
    const std::uint16_t section_id) const {
  return appearance_geosets_.IsVisible(section_id);
}

void GlueCharSelectScene::SetPropModelPath(std::optional<std::string> m2_path) {
  if (m2_path.has_value()) {
    *m2_path = NormalizeM2Path(std::move(*m2_path));
    if (m2_path->empty()) {
      m2_path.reset();
    }
  }
  prop_model_path_ = std::move(m2_path);
  prop_texture_paths_.fill({});
  prop_model_alpha_ = 1.0f;
  graph_.SetPosition(prop_node_, 0.0f, 0.0f, 0.0f);
  graph_.SetRotation(prop_node_, {});
  graph_.SetScale(prop_node_, 1.0f, 1.0f, 1.0f);
  RebuildAttachments();
}

const std::optional<std::string> &GlueCharSelectScene::prop_model_path() const {
  return prop_model_path_;
}

const std::array<std::string, 3> &GlueCharSelectScene::prop_texture_paths() const {
  return prop_texture_paths_;
}

float GlueCharSelectScene::prop_model_alpha() const {
  return prop_model_alpha_;
}

const std::array<GlueCharSelectScene::AttachedEffectModel, 9> &
GlueCharSelectScene::character_effect_models() const {
  return character_effect_models_;
}

const std::array<GlueCharSelectScene::AttachedEquipmentModel,
                 GlueCharSelectScene::kMaxCharacterEquipmentModels> &
GlueCharSelectScene::character_equipment_models() const {
  return character_equipment_models_;
}

const GlueCharSelectScene::CharacterEquipmentPose &
GlueCharSelectScene::character_equipment_pose() const {
  return character_equipment_pose_;
}

float GlueCharSelectScene::character_model_alpha() const {
  return character_model_alpha_;
}

const std::array<float, 3> &GlueCharSelectScene::character_model_tint() const {
  return character_model_tint_;
}

std::array<float, 4> GlueCharSelectScene::character_hierarchy_color_multiplier() const {
  return {character_model_tint_[0], character_model_tint_[1],
          character_model_tint_[2], character_model_alpha_};
}

}
