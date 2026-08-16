
#include "openwow/game/objects/cgcorpse.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/descriptor_callback_registry.h"
#include "openwow/game/guild_manager.h"
#include "openwow/game/object_effect_system.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_session.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <array>
#include <string>
#include <string_view>

namespace openwow::game {

namespace {

constexpr std::uint32_t kDescriptorWordByteSize = sizeof(std::uint32_t);
constexpr std::uint32_t kCorpseFlagsCallbackOffsetBytes =
    DescriptorWordDistance(CORPSE_FIELD_FLAGS, OBJECT_END) * kDescriptorWordByteSize;
constexpr std::uint32_t kCorpseDynamicFlagsCallbackOffsetBytes =
    DescriptorWordDistance(CORPSE_FIELD_DYNAMIC_FLAGS, OBJECT_END) * kDescriptorWordByteSize;
constexpr float kCorpseUndergroundHeightThreshold = 2.0f / 3.0f;
constexpr std::array<std::string_view, 2> kCorpseGenderNames{
    "Male", "Female"};

CorpseSupportSurfaceHeightCallback g_support_surface_height_callback = nullptr;
void* g_support_surface_height_context = nullptr;
CorpseCharacterAppearanceReadyCallback
    g_character_appearance_ready_callback = nullptr;
void* g_character_appearance_ready_context = nullptr;
char g_corpse_flags_callback_key = 0;
char g_corpse_dynamic_flags_callback_key = 0;

[[nodiscard]] DescriptorCallbackBinding CorpseFlagsCallbackBinding() noexcept {
  return {.callback_key =
              reinterpret_cast<std::uintptr_t>(&g_corpse_flags_callback_key)};
}

[[nodiscard]] DescriptorCallbackBinding
CorpseDynamicFlagsCallbackBinding() noexcept {
  return {
      .callback_key = reinterpret_cast<std::uintptr_t>(
          &g_corpse_dynamic_flags_callback_key)};
}

[[nodiscard]] bool GuildEmblemsEqual(const GuildEmblem& lhs,
                                     const GuildEmblem& rhs) noexcept {
  return lhs.style == rhs.style && lhs.color == rhs.color &&
         lhs.border_style == rhs.border_style &&
         lhs.border_color == rhs.border_color &&
         lhs.background_color == rhs.background_color;
}

[[nodiscard]] std::optional<float> ResolveSupportSurfaceHeight(
    const CGCorpse_C& corpse) {
  if (g_support_surface_height_callback == nullptr) {
    return std::nullopt;
  }
  return g_support_surface_height_callback(
      corpse, g_support_surface_height_context);
}

void LogInvalidPlayerDisplay(const std::uint32_t display_id,
                             const std::uint8_t race,
                             const std::uint8_t gender) {
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      "INVALIDPLAYERDISPLAYID|" + std::to_string(display_id) + "|" +
          std::to_string(race) + "|" + std::to_string(gender));
}

void LogInvalidPlayerModel(const std::uint32_t model_id,
                           const std::uint8_t race,
                           const std::uint8_t gender) {
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      "INVALIDPLAYERMODELRECORD|" + std::to_string(model_id) + "|" +
          std::to_string(race) + "|" + std::to_string(gender));
}

}

void SetCorpseSupportSurfaceHeightCallback(
    CorpseSupportSurfaceHeightCallback callback, void* context) {
  g_support_surface_height_callback = callback;
  g_support_surface_height_context = context;
}

void ClearCorpseSupportSurfaceHeightCallback() {
  g_support_surface_height_callback = nullptr;
  g_support_surface_height_context = nullptr;
}

void SetCorpseCharacterAppearanceReadyCallback(
    CorpseCharacterAppearanceReadyCallback callback, void* context) {
  g_character_appearance_ready_callback = callback;
  g_character_appearance_ready_context = context;
}

void ClearCorpseCharacterAppearanceReadyCallback() {
  g_character_appearance_ready_callback = nullptr;
  g_character_appearance_ready_context = nullptr;
}

CGCorpse_C::CGCorpse_C() : CGObject_C(TypeID::kCorpse) {}

CGCorpse_C::CGCorpse_C(ObjectGuid guid)
    : CGObject_C(guid, TypeID::kCorpse) {}

CGCorpse_C::CGCorpse_C(ObjectManager& objects)
    : CGObject_C(objects, TypeID::kCorpse) {}

CGCorpse_C::CGCorpse_C(ObjectManager& objects, ObjectGuid guid)
    : CGObject_C(objects, guid, TypeID::kCorpse) {}

ObjectGuid CGCorpse_C::GetOwner() const {
  return GetGuidField(CORPSE_FIELD_OWNER);
}

ObjectGuid CGCorpse_C::GetParty() const {
  return GetGuidField(CORPSE_FIELD_PARTY);
}

std::uint32_t CGCorpse_C::GetDisplayId() const {
  return GetUInt32(CORPSE_FIELD_DISPLAY_ID);
}

std::uint32_t CGCorpse_C::GetCorpseDisplayID() const {
  return GetDisplayId();
}

std::uint32_t CGCorpse_C::GetFactionTemplate() const {
  const auto* const objects = object_manager();
  const auto* const dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc == nullptr) {
    return 0u;
  }

  const auto* const race =
      dbc->chr_races().LookupEntry(static_cast<std::uint32_t>(GetRace()));
  return race != nullptr ? race->faction_id : 0u;
}

const char* CGCorpse_C::GetPortraitTextureName() const {
  const auto* const objects = object_manager();
  const auto* const dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto* const display =
      dbc->creature_display_info().LookupEntry(GetDisplayId());
  return display != nullptr ? display->portrait_texture_name.data() : nullptr;
}

std::uint32_t CGCorpse_C::GetItemDisplay(std::uint8_t slot) const {
  if (slot >= kCorpseEquipmentSlotCount) return 0;
  return GetUInt32(
      static_cast<std::uint16_t>(CORPSE_FIELD_ITEM + slot));
}

std::uint32_t CGCorpse_C::GetItemEntry(std::uint8_t slot) const {
  return GetItemDisplay(slot);
}

std::uint8_t CGCorpse_C::GetRace() const {
  return static_cast<std::uint8_t>(GetUInt32(CORPSE_FIELD_BYTES_1) & 0xFF);
}

std::uint8_t CGCorpse_C::GetGender() const {
  return static_cast<std::uint8_t>((GetUInt32(CORPSE_FIELD_BYTES_1) >> 8) & 0xFF);
}

std::uint8_t CGCorpse_C::GetSkinColor() const {
  return static_cast<std::uint8_t>((GetUInt32(CORPSE_FIELD_BYTES_1) >> 16) & 0xFF);
}

std::uint8_t CGCorpse_C::GetFace() const {
  return static_cast<std::uint8_t>((GetUInt32(CORPSE_FIELD_BYTES_1) >> 24) & 0xFF);
}

std::uint8_t CGCorpse_C::GetHairStyle() const {
  return static_cast<std::uint8_t>(GetUInt32(CORPSE_FIELD_BYTES_2) & 0xFF);
}

std::uint8_t CGCorpse_C::GetHairColor() const {
  return static_cast<std::uint8_t>((GetUInt32(CORPSE_FIELD_BYTES_2) >> 8) & 0xFF);
}

std::uint8_t CGCorpse_C::GetFacialHair() const {
  return static_cast<std::uint8_t>((GetUInt32(CORPSE_FIELD_BYTES_2) >> 16) & 0xFF);
}

std::uint32_t CGCorpse_C::GetGuildId() const {
  return GetUInt32(CORPSE_FIELD_GUILD);
}

std::uint32_t CGCorpse_C::GetGuildID() const {
  return GetGuildId();
}

std::uint32_t CGCorpse_C::GetCorpseFlags() const {
  return GetUInt32(CORPSE_FIELD_FLAGS);
}

std::uint32_t CGCorpse_C::GetDynamicFlags() const {
  return GetUInt32(CORPSE_FIELD_DYNAMIC_FLAGS);
}

bool CGCorpse_C::IsItemVisible() const {
  return (GetDynamicFlags() & kCorpseDynFlagLootable) != 0u;
}

void CGCorpse_C::QueryModelRebuildFlags(
    const std::uint8_t flags,
    std::uint32_t& out_needs_construct,
    std::uint32_t& out_needs_refresh) {
  CGObject_C::QueryModelRebuildFlags(
      flags, out_needs_construct, out_needs_refresh);

  if (out_needs_construct != 0u || out_needs_refresh != 0u) {
    return;
  }

  const bool underground_bones =
      (visual_state_.render_flags & kCorpseRenderFlagUnderground) != 0u &&
      (GetCorpseFlags() & kCorpseFlagBones) != 0u;
  const bool character_appearance_pending =
      HasCharacterModelVisual() &&
      (g_character_appearance_ready_callback == nullptr ||
       !g_character_appearance_ready_callback(
           *this, g_character_appearance_ready_context));
  if (underground_bones || character_appearance_pending) {
    out_needs_refresh = 1u;
  }
}

std::vector<std::uint16_t> CGCorpse_C::ApplyCreateUpdate(
    const CreateObjectUpdate& upd) {
  auto updated_fields = CGObject_C::ApplyCreateUpdate(upd);
  if (!upd.defer_post_init) {
    OnCreate();
  }
  return updated_fields;
}

void CGCorpse_C::FinalizeCreateUpdate(const CreateObjectUpdate& upd) {
  CGObject_C::FinalizeCreateUpdate(upd);
  OnCreate();
}

void CGCorpse_C::FinalizeWorldPublication() {

  CGObject_C::FinalizeWorldPublication();
}

void CGCorpse_C::ResetVisualState() {
  const std::uint32_t next_serial = visual_state_.sync_serial + 1u;
  visual_state_ = {};
  visual_state_.sync_serial = next_serial;
  visual_state_.death_animation_id = kCorpseAnimStandingDead;
}

void CGCorpse_C::SetLootSparkleVisualFromDbc(
    const openwow::data::dbc::DbcLoader& dbc,
    const bool active) {
  visual_state_.loot_sparkle_requested = active;
  visual_state_.loot_sparkle_active = false;
  visual_state_.loot_sparkle_effect_path.clear();
  if (!active) {
    return;
  }

  const std::uint32_t effect_id =
      HardcodedEffectIdTable::GetEffectId(HardcodedEffectId::kLootArt);
  if (effect_id == 0u) {
    return;
  }

  const auto* const effect_entry =
      dbc.spell_visual_effect_name().LookupEntry(effect_id);
  if (effect_entry == nullptr || effect_entry->file_path.empty()) {
    return;
  }

  visual_state_.loot_sparkle_active = true;
  visual_state_.loot_sparkle_effect_path =
      std::string(effect_entry->file_path);
}

void CGCorpse_C::PopulateEquipmentVisuals(
    const std::uint32_t corpse_flags,
    const openwow::data::dbc::DbcLoader& dbc) {
  visual_state_.equipment_count = 0u;

  for (std::uint8_t slot = 0; slot < kCorpseEquipmentSlotCount; ++slot) {
    const std::uint32_t packed_item = GetItemDisplay(slot);
    const std::uint32_t item_display_id =
        packed_item & kCorpseItemDisplayIdMask;
    if (item_display_id == 0u || slot == kCorpseEquipmentSlotRanged) {
      continue;
    }
    if (slot == kCorpseEquipmentSlotHead &&
        (corpse_flags & kCorpseFlagHideHelm) != 0u) {
      continue;
    }
    if (slot == kCorpseEquipmentSlotBack &&
        (corpse_flags & kCorpseFlagHideCloak) != 0u) {
      continue;
    }

    const auto* const item_display =
        dbc.item_display_info().LookupEntry(item_display_id);
    if (item_display == nullptr) {
      continue;
    }

    auto& visual =
        visual_state_.equipment[visual_state_.equipment_count++];
    visual.slot = slot;
    visual.item_display_id = item_display_id;
    visual.inventory_type = static_cast<std::uint8_t>(
        packed_item >> kCorpseItemInventoryTypeShift);
    visual.weapon_model = slot == kCorpseEquipmentSlotMainHand ||
                          slot == kCorpseEquipmentSlotOffHand;
    visual.updates_guild_tabard =
        slot == kCorpseEquipmentSlotTabard &&
        (item_display->flags & kCorpseTabardEmblemItemDisplayFlag) != 0u;

  }
}

void CGCorpse_C::RebuildVisualState(
    const std::uint32_t corpse_flags,
    const openwow::data::dbc::DbcLoader& dbc) {
  ResetVisualState();

  const bool is_bones = (corpse_flags & kCorpseFlagBones) != 0u;
  const std::uint8_t race = GetRace();
  const std::uint8_t gender = GetGender();
  if (is_bones) {
    if (gender < kCorpseGenderNames.size()) {
      if (const auto* const race_entry = dbc.chr_races().LookupEntry(race);
          race_entry != nullptr) {
        visual_state_.model_kind = CorpseVisualModelKind::kSkeleton;
        visual_state_.model_path =
            "World\\Generic\\PassiveDoodads\\DeathSkeletons\\" +
            std::string(race_entry->model_client_prefix) +
            std::string(kCorpseGenderNames[gender]) + "DeathSkeleton.mdx";
      }
    }
  } else {
    const std::uint32_t display_id = GetDisplayId();
    const auto* const display_info =
        dbc.creature_display_info().LookupEntry(display_id);
    if (display_info == nullptr) {
      LogInvalidPlayerDisplay(display_id, race, gender);
    } else {
      const auto* const model_data =
          dbc.creature_model_data().LookupEntry(display_info->model_id);
      if (model_data == nullptr) {
        LogInvalidPlayerModel(display_info->model_id, race, gender);
      } else if (!model_data->model_name.empty()) {
        visual_state_.model_path = std::string(model_data->model_name);
        const bool needs_full_character_model =
            (model_data->flags &
             kCorpseCreatureModelDataCharacterFlag) != 0u;
        visual_state_.model_kind =
            needs_full_character_model
                ? CorpseVisualModelKind::kCharacter
                : CorpseVisualModelKind::kCreatureTextureReplacement;
        if (needs_full_character_model) {
          PopulateEquipmentVisuals(corpse_flags, dbc);
        }
      }
    }
  }

  if (const auto support_surface_height = ResolveSupportSurfaceHeight(*this);
      support_surface_height.has_value() &&
      *support_surface_height - GetPosition().z >
          kCorpseUndergroundHeightThreshold) {
    visual_state_.render_flags |= kCorpseRenderFlagUnderground;
    visual_state_.death_animation_id = kCorpseAnimUndergroundDead;
  }

  const auto owner_guid = GetOwner();
  const auto active_player_guid = GetActivePlayerGuid();
  visual_state_.active_player_corpse =
      !is_bones && !active_player_guid.IsEmpty() &&
      owner_guid == active_player_guid;

  SetLootSparkleVisualFromDbc(
      dbc, (GetDynamicFlags() & kCorpseDynFlagLootable) != 0u);
}

void CGCorpse_C::OnCreate() {
  const auto flags = GetCorpseFlags();
  const auto* const objects = object_manager();
  if (objects == nullptr) {
    ResetVisualState();
    return;
  }
  const auto* const dbc = &objects->dbc_loader();

  RebuildVisualState(flags, *dbc);
}

void CGCorpse_C::UpdateGuildTabard(WorldSession& session,
                                   const bool request_async) {
  if (!HasCharacterModelVisual()) {
    return;
  }

  const std::uint32_t guild_id = GetGuildId();
  if (guild_id == 0u) {
    return;
  }

  const auto* cached_info = session.guild().FindCachedGuildInfo(guild_id);
  if (cached_info != nullptr &&
      HasResolvedGuildEmblem(cached_info->emblem)) {
    if (!visual_state_.guild_tabard_emblem.has_value() ||
        !GuildEmblemsEqual(*visual_state_.guild_tabard_emblem,
                           cached_info->emblem)) {
      visual_state_.guild_tabard_emblem = cached_info->emblem;
      ++visual_state_.sync_serial;
    }
  } else if (request_async && session.guild().BeginGuildQuery(guild_id)) {
    auto pkt = GuildManager::BuildGuildQuery(guild_id);
    session.interaction().SendRawPacket(pkt);
  }
}

void CGCorpse_C::RegisterFieldHandlers() {
  auto &registry = DescriptorCallbackRegistry::Get();
  const auto flags_binding = CorpseFlagsCallbackBinding();
  const auto dynamic_flags_binding = CorpseDynamicFlagsCallbackBinding();

  (void)registry.UnregisterTypeSectionCallback(
      TypeID::kCorpse, kCorpseFlagsCallbackOffsetBytes, flags_binding);
  (void)registry.UnregisterTypeSectionCallback(
      TypeID::kCorpse, kCorpseDynamicFlagsCallbackOffsetBytes,
      dynamic_flags_binding);

  (void)registry.RegisterTypeSectionCallback(
      TypeID::kCorpse, kCorpseFlagsCallbackOffsetBytes,
      kDescriptorWordByteSize,
      [](const DescriptorFieldChangeView &v) {
        auto *corpse = dynamic_cast<CGCorpse_C *>(
            const_cast<CGObject_C *>(&v.object));
        if (corpse) {
          const auto new_flags =
              v.new_words.empty() ? std::uint32_t{0} : v.new_words[0];
          corpse->OnFlagsChanged(new_flags);
        }
      },
      flags_binding);

  (void)registry.RegisterTypeSectionCallback(
      TypeID::kCorpse, kCorpseDynamicFlagsCallbackOffsetBytes,
      kDescriptorWordByteSize,
      [](const DescriptorFieldChangeView &v) {
        auto *corpse = dynamic_cast<CGCorpse_C *>(
            const_cast<CGObject_C *>(&v.object));
        if (corpse) {
          const auto new_val =
              v.new_words.empty() ? std::uint8_t{0}
                                  : static_cast<std::uint8_t>(v.new_words[0]);
          corpse->OnDynamicFlagsChanged(new_val);
        }
      },
      dynamic_flags_binding);
}

void CGCorpse_C::OnFlagsChanged(std::uint32_t new_flags) {
  const auto* const objects = object_manager();
  const auto* const dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc == nullptr) {
    ResetVisualState();
    return;
  }

  RebuildVisualState(new_flags, *dbc);
}

void CGCorpse_C::OnDynamicFlagsChanged(std::uint8_t new_dynamic_flags) {
  const bool now_lootable = (new_dynamic_flags & kCorpseDynFlagLootable) != 0;

  if (visual_state_.loot_sparkle_requested == now_lootable) {
    return;
  }

  const auto* const objects = object_manager();
  const auto* const dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc == nullptr) {
    visual_state_.loot_sparkle_requested = now_lootable;
    visual_state_.loot_sparkle_active = false;
    visual_state_.loot_sparkle_effect_path.clear();
    ++visual_state_.sync_serial;
    return;
  }

  SetLootSparkleVisualFromDbc(*dbc, now_lootable);
  ++visual_state_.sync_serial;
}

}
