
#include "openwow/game/objects/cgplayer.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/display_settings.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/display_info_resolver.h"
#include "openwow/foundation/math/float_compare.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/async_query_channel.h"
#include "openwow/game/character_component_backend.h"
#include "openwow/game/group_system.h"
#include "openwow/game/inebriation.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/game/player_combat.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/socket_color_match.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/world_session.h"
#include "openwow/game/group_manager.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/scene/world_frame.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/world/camera/world_camera.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstring>

namespace openwow::game {

namespace {

constexpr std::int64_t kResetInstanceAnchorWindowExclusiveSeconds = 0x15f91;
constexpr std::uint32_t kEquipmentVisibilityFlags2Bit = 0x00200000u;
constexpr std::uint32_t kItemTemplateFlagHasDurability = 0x1000u;
constexpr std::array<std::uint8_t, 3> kVisibleWeaponEquipSlots = {15u, 16u, 17u};

[[nodiscard]] bool QueryPrimaryM2AttachmentPosition(
    const CGPlayer_C& player,
    const std::uint32_t attachment_lookup_index,
    float* const out_position) {
  const std::uint32_t instance_id = player.GetPrimaryM2InstanceId();
  if (instance_id == 0u || out_position == nullptr) {
    return false;
  }

  auto* const m2_system = player.m2_system();
  if (m2_system == nullptr) {
    return false;
  }

  const auto query = m2_system->QueryAttachmentPosition(
      instance_id, attachment_lookup_index);
  if (query.status != render::m2::M2ResultStatus::kReady) {
    return false;
  }

  out_position[0] = query.position[0];
  out_position[1] = query.position[1];
  out_position[2] = query.position[2];
  return true;
}

constexpr float kIndicatorMinRadius = 0.5f;

constexpr float kIndicatorMaxRadius = 0.75f;

constexpr std::uint32_t kIndicatorPulsePeriodMs = 500u;

constexpr float kTwoPi = 6.2831855f;

constexpr std::uint32_t kAutoAttackTypeMelee = 4u;
constexpr std::uint32_t kAutoAttackTypeRanged = 10u;

PlayerAnimationProgressCallback g_player_animation_progress_callback = nullptr;
void* g_player_animation_progress_context = nullptr;

bool IsTwoHandWeaponSubclass(const std::uint32_t subclass) {
  switch (subclass) {
    case 1:
    case 5:
    case 6:
    case 8:
    case 10:
    case 12:
    case 17:
    case 20:
      return true;
    default:
      return false;
  }
}

bool IsNearTrackedNpc(const CGPlayer_C &player, const ObjectManager &object_manager,
                      std::uint64_t npc_guid) {
  if (npc_guid == 0) {
    return false;
  }

  const auto *npc = object_manager.GetUnit(ObjectGuid(npc_guid));
  if (npc == nullptr) {
    return false;
  }

  const float dx = npc->GetX() - player.GetX();
  const float dy = npc->GetY() - player.GetY();
  const float dz = npc->GetZ() - player.GetZ();
  const float distance_sq = dx * dx + dy * dy + dz * dz;
  const float threshold = npc->State().GetBoundingRadius() + 4.0f;
  return distance_sq <= threshold * threshold;
}

bool IsDungeonOrRaidMap(const WorldSession& session,
                        const std::uint32_t map_id) {
  const auto* map_entry = session.LookupMapEntry(map_id);
  if (map_entry == nullptr) {
    return false;
  }

  return map_entry->map_type == 1 || map_entry->map_type == 2;
}

std::uint32_t GetControlFocusTimestamp(const WorldSession& session) {
  const auto current_time_ms = session.CurrentClientTimeMs();
  if (current_time_ms != 0) {
    return current_time_ms;
  }

  return openwow::core::GameClock::GetTickCount32();
}

void SendFarSightPacket(WorldSession& session, const bool enable) {
  auto pkt = net::wotlk::PacketSender::BuildFarSight(enable);
  session.interaction().SendRawPacket(pkt);
}

void ProcessControlFocusMovement(const std::uint32_t timestamp) {
  if (auto* const input = GetInputControlSingleton(); input != nullptr) {
    input->ProcessMovementNow(timestamp, true);
  }
}

void BindCameraToObject(openwow::world::WorldCamera& camera,
                        const CGObject_C& focus_object) {
  camera.SetBoundObject(focus_object.GetGuid().GetRawValue());

  const auto focus_position = focus_object.GetPosition();
  camera.SetTarget(focus_position.x, focus_position.y, focus_position.z);
}

const data::dbc::ItemEntry* LookupVisibleItemDbcFallback(
    const ObjectManager& objects, const std::uint32_t entry) {
  return objects.dbc_loader().item().LookupEntry(entry);
}

void PopulateVisibleItemTemplateMetadata(
    const ItemTemplate& item_template, VisibleItemTemplateMetadata& metadata) {
  metadata.item_class = static_cast<std::uint32_t>(item_template.item_class);
  metadata.subclass = item_template.subclass;
  metadata.sound_override = item_template.sound_override;
  metadata.material = item_template.material;
  metadata.inventory_type =
      static_cast<std::uint32_t>(item_template.inventory_type);
  metadata.sheath = item_template.sheath;
  metadata.display_id = item_template.display_id;
}

bool ResolveVisibleItemTemplateCacheMetadata(
    ObjectManager& objects, const std::uint32_t entry,
    VisibleItemTemplateMetadata& metadata) {
  if (const auto* const item_template =
          objects.query_cache().GetOrRequestItemTemplate(entry);
      item_template != nullptr) {
    PopulateVisibleItemTemplateMetadata(*item_template, metadata);
    return true;
  }

  return false;
}

const data::dbc::DbcLoader* LookupActiveDbcLoader(const CGObject_C& object) {
  const auto* const objects = object.object_manager();
  return objects != nullptr ? &objects->dbc_loader() : nullptr;
}

std::uint32_t ResolveVisibleItemAuraVisualId(
    const data::dbc::DbcLoader& dbc,
    const std::uint32_t display_id,
    const std::uint32_t packed_enchants) {
  if (display_id != 0) {
    const auto* display = dbc.item_display_info().LookupEntry(display_id);
    if (display != nullptr && display->item_visuals_id != 0 &&
        dbc.item_visuals().LookupEntry(display->item_visuals_id) != nullptr) {
      return 0;
    }
  }

  const auto resolve_enchant = [&](const std::uint16_t enchant_id) -> std::uint32_t {
    if (enchant_id == 0) {
      return 0;
    }

    const auto* enchant =
        dbc.spell_item_enchantment().LookupEntry(static_cast<std::uint32_t>(enchant_id));
    return enchant != nullptr ? enchant->aura_id : 0u;
  };

  const auto low_enchant =
      static_cast<std::uint16_t>(packed_enchants & 0xffffu);
  if (const auto aura_id = resolve_enchant(low_enchant); aura_id != 0) {
    return aura_id;
  }

  const auto high_enchant =
      static_cast<std::uint16_t>((packed_enchants >> 16) & 0xffffu);
  return resolve_enchant(high_enchant);
}

std::optional<std::uint8_t> ResolveVisibleWeaponEquipSlot(
    const std::uint8_t visible_weapon_index) {
  if (visible_weapon_index >= kVisibleWeaponEquipSlots.size()) {
    return std::nullopt;
  }

  return kVisibleWeaponEquipSlots[visible_weapon_index];
}

bool VisibleWeaponUsesWeaponClass(const CGPlayer_C& player,
                                  const std::uint8_t equip_slot) {
  const auto metadata = player.GetVisibleItemTemplateMetadata(equip_slot);
  return metadata.has_value() &&
         metadata->item_class ==
             static_cast<std::uint32_t>(ItemClass::Weapon);
}

bool OffhandVisibilitySuppressedByAuraState(const CGPlayer_C& player) {
  const auto aura_state_low_byte =
      static_cast<std::uint8_t>(player.State().GetAuraState() & 0xffu);
  return static_cast<std::int8_t>(aura_state_low_byte) < 0;
}

}

void SetPlayerAnimationProgressCallback(const PlayerAnimationProgressCallback callback,
                                        void* const context) {
  g_player_animation_progress_callback = callback;
  g_player_animation_progress_context = context;
}

void ClearPlayerAnimationProgressCallback() {
  g_player_animation_progress_callback = nullptr;
  g_player_animation_progress_context = nullptr;
}

CGPlayer_C::CGPlayer_C(ItemDefinitions& item_definitions,
                       const data::dbc::DbcLoader& dbc_loader)
    : CGUnit_C(item_definitions, dbc_loader, TypeID::kPlayer) {}

CGPlayer_C::CGPlayer_C(ItemDefinitions& item_definitions,
                       const data::dbc::DbcLoader& dbc_loader,
                       ObjectGuid guid)
    : CGUnit_C(item_definitions, dbc_loader, guid, TypeID::kPlayer) {}

CGPlayer_C::CGPlayer_C(ObjectManager& objects,
                       ItemDefinitions& item_definitions,
                       const data::dbc::DbcLoader& dbc_loader,
                       ObjectGuid guid)
    : CGUnit_C(objects, item_definitions, dbc_loader, guid, TypeID::kPlayer) {}

CGPlayer_C::~CGPlayer_C() {

  if (IsActivePlayer()) {
    CleanupActivePlayerState();
  }

  CleanupUnitResources();
}

std::vector<std::uint16_t> CGPlayer_C::ApplyCreateUpdate(const CreateObjectUpdate& upd) {
  auto updated_fields = CGUnit_C::ApplyCreateUpdate(upd);
  if (!upd.defer_post_init) {
    RunPostCreateInitialization();
  }
  return updated_fields;
}

void CGPlayer_C::FinalizeCreateUpdate(const CreateObjectUpdate& upd) {
  CGUnit_C::FinalizeCreateUpdate(upd);
  RunPostCreateInitialization();
}

void CGPlayer_C::FinalizePacketUpdatePromotion() {

  cached_stand_state_ =
      static_cast<std::uint8_t>(GetUInt32(UNIT_FIELD_BYTES_1) & 0xFFu);
  CGUnit_C::FinalizePacketUpdatePromotion();
  visual_model_state_ = 0u;
}

void CGPlayer_C::RunPostCreateInitialization() {

  cached_stand_state_ = static_cast<std::uint8_t>(GetUInt32(UNIT_FIELD_BYTES_1) & 0xFFu);
  visual_model_state_ = 0;

  if (IsActivePlayer()) {
    if (static_cast<std::int32_t>(GetUInt32(UNIT_FIELD_HEALTH)) <= 0) {
      State().AddSpellStateFlags(kSpellStateDeadOnInit);
    }

    Presentation().TryReuseCharSelectModel();
  }
}

std::uint32_t CGPlayer_C::GetXP() const {

  return predicted_xp_current_ != 0 ? predicted_xp_current_ : GetUInt32(PLAYER_XP);
}

std::uint32_t CGPlayer_C::GetNextLevelXP() const {
  return predicted_xp_max_ != 0 ? predicted_xp_max_ : GetUInt32(PLAYER_NEXT_LEVEL_XP);
}

std::uint32_t CGPlayer_C::GetMoney() const {

  if (predicted_money_ >= 0) {
    return static_cast<std::uint32_t>(predicted_money_);
  }
  return GetUInt32(PLAYER_FIELD_COINAGE);
}

float CGPlayer_C::GetBlockPercentage() const {
  return GetFloat(PLAYER_BLOCK_PERCENTAGE);
}

float CGPlayer_C::GetDodgePercentage() const {
  return GetFloat(PLAYER_DODGE_PERCENTAGE);
}

float CGPlayer_C::GetParryPercentage() const {
  return GetFloat(PLAYER_PARRY_PERCENTAGE);
}

float CGPlayer_C::GetCritPercentage() const {
  return GetFloat(PLAYER_CRIT_PERCENTAGE);
}

float CGPlayer_C::GetRangedCritPercentage() const {
  return GetFloat(PLAYER_RANGED_CRIT_PERCENTAGE);
}

float CGPlayer_C::GetSpellCritPercentage(std::uint8_t school) const {
  if (school > 6)
    return 0.0f;
  return GetFloat(static_cast<std::uint16_t>(PLAYER_SPELL_CRIT_PERCENTAGE1 + school));
}

CGPlayer_C::SkillInfo CGPlayer_C::GetSkill(std::uint16_t index) const {
  SkillInfo info{};
  if (index >= 128)
    return info;

  std::uint16_t base = static_cast<std::uint16_t>(PLAYER_SKILL_INFO_1_1 + index * 3);

  std::uint32_t v0 = GetUInt32(base);
  std::uint32_t v1 = GetUInt32(static_cast<std::uint16_t>(base + 1));
  std::uint32_t v2 = GetUInt32(static_cast<std::uint16_t>(base + 2));

  info.skill_id = static_cast<std::uint16_t>(v0 & 0xFFFF);
  info.step = static_cast<std::uint16_t>((v0 >> 16) & 0xFFFF);
  info.value = static_cast<std::uint16_t>(v1 & 0xFFFF);
  info.max_value = static_cast<std::uint16_t>((v1 >> 16) & 0xFFFF);
  std::int16_t mod, step_mod;
  std::memcpy(&mod, reinterpret_cast<const char *>(&v2), 2);
  std::memcpy(&step_mod, reinterpret_cast<const char *>(&v2) + 2, 2);
  info.modifier = mod;
  info.step_modifier = step_mod;

  return info;
}

std::optional<std::uint16_t> CGPlayer_C::FindSkillSlot(
    const std::uint16_t skill_id) const {
  if (skill_id == 0) {
    return std::nullopt;
  }

  for (std::uint16_t index = 0; index < 128; ++index) {
    if (GetSkill(index).skill_id == skill_id) {
      return index;
    }
  }

  return std::nullopt;
}

std::optional<std::uint16_t> CGPlayer_C::FindActiveSkillSlot(
    const std::uint16_t skill_id) const {
  if (!IsActivePlayer()) {
    return std::nullopt;
  }

  for (std::uint16_t index = 0; index < 128; ++index) {
    if (GetSkill(index).skill_id == skill_id) {
      return index;
    }
  }

  return std::nullopt;
}

std::optional<CGPlayer_C::SkillInfo> CGPlayer_C::FindSkill(
    const std::uint16_t skill_id) const {
  if (const auto slot = FindSkillSlot(skill_id); slot.has_value()) {
    return GetSkill(*slot);
  }

  return std::nullopt;
}

std::optional<CGPlayer_C::ActiveSkillValues> CGPlayer_C::FindActiveSkillValues(
    const std::uint16_t skill_id) const {
  const auto slot = FindActiveSkillSlot(skill_id);
  if (!slot.has_value()) {
    return std::nullopt;
  }

  const auto skill = GetSkill(*slot);
  ActiveSkillValues values;
  values.raw_value = skill.value;
  values.adjusted_value = skill.value;
  if (values.adjusted_value != 0) {
    values.adjusted_value += static_cast<std::uint16_t>(skill.step_modifier);
  }

  return values;
}

std::uint16_t CGPlayer_C::GetSkillValue(std::uint16_t skill_id) const {
  if (const auto skill = FindSkill(skill_id); skill.has_value()) {
    return skill->value;
  }
  return 0;
}

std::uint16_t CGPlayer_C::GetSkillMaxValue(std::uint16_t skill_id) const {
  if (const auto skill = FindSkill(skill_id); skill.has_value()) {
    return skill->max_value;
  }
  return 0;
}

std::uint16_t CGPlayer_C::GetSkillBonusValue(std::uint16_t skill_id) const {
  if (const auto skill = FindSkill(skill_id); skill.has_value()) {
    return (skill->modifier > 0) ? static_cast<std::uint16_t>(skill->modifier)
                                 : 0;
  }
  return 0;
}

std::uint32_t CGPlayer_C::GetSkillValueWithStepModifier(
    const std::uint16_t skill_id) const {
  const auto values = FindActiveSkillValues(skill_id);
  if (!values.has_value()) {
    return 0;
  }

  return values->adjusted_value;
}

ObjectGuid CGPlayer_C::GetInventorySlotGuid(std::uint8_t slot) const {
  if (slot >= 23)
    return ObjectGuid();
  return GetGuidField(static_cast<std::uint16_t>(PLAYER_FIELD_INV_SLOT_HEAD + slot * 2));
}

std::uint32_t CGPlayer_C::GetVisibleItemEntry(std::uint8_t slot) const {
  if (slot >= 19)
    return 0;
  return GetUInt32(static_cast<std::uint16_t>(PLAYER_VISIBLE_ITEM_1_ENTRYID + slot * 2));
}

std::uint16_t CGPlayer_C::GetVisibleItemEnchant(std::uint8_t slot) const {
  if (slot >= 19)
    return 0;
  std::uint32_t val =
      GetUInt32(static_cast<std::uint16_t>(PLAYER_VISIBLE_ITEM_1_ENTRYID + slot * 2 + 1));
  return static_cast<std::uint16_t>(val & 0xFFFF);
}

std::optional<EquipmentSlotInfo>
CGPlayer_C::GetVisibleEquipSlotInfo(const std::uint8_t slot) const {
  if (slot >= 19)
    return std::nullopt;

  const auto signed_entry = static_cast<std::int32_t>(
      GetUInt32(static_cast<std::uint16_t>(PLAYER_VISIBLE_ITEM_1_ENTRYID + slot * 2)));
  if (signed_entry == 0)
    return std::nullopt;

  const std::uint32_t packed_enchant =
      GetUInt32(static_cast<std::uint16_t>(PLAYER_VISIBLE_ITEM_1_ENTRYID + slot * 2 + 1));

  EquipmentSlotInfo info{};
  info.item_id = static_cast<std::uint32_t>(signed_entry < 0 ? -signed_entry : signed_entry);
  info.enchant_id = packed_enchant & 0xFFFFu;
  info.suffix_factor = static_cast<std::uint16_t>((packed_enchant >> 16) & 0xFFFFu);
  info.is_broken = signed_entry < 0;
  return info;
}

std::uint32_t CGPlayer_C::GetVisibleItemAuraVisual(const std::uint8_t slot) const {
  if (slot >= 19) {
    return 0;
  }

  const auto metadata = GetVisibleItemTemplateMetadata(slot);
  if (!metadata.has_value()) {
    return 0;
  }

  const auto* dbc = LookupActiveDbcLoader(*this);
  if (dbc == nullptr) {
    return 0;
  }

  const auto packed_enchants = GetUInt32(
      static_cast<std::uint16_t>(PLAYER_VISIBLE_ITEM_1_ENTRYID + slot * 2 + 1));
  return ResolveVisibleItemAuraVisualId(*dbc, metadata->display_id, packed_enchants);
}

std::optional<std::uint32_t>
CGPlayer_C::GetVisibleWeaponDisplayIdRaw(
    const std::uint8_t visible_weapon_index) const {
  const auto equip_slot = ResolveVisibleWeaponEquipSlot(visible_weapon_index);
  if (!equip_slot.has_value()) {
    return std::nullopt;
  }

  const auto metadata = GetVisibleItemTemplateMetadata(*equip_slot);
  if (!metadata.has_value() || metadata->display_id == 0) {
    return std::nullopt;
  }

  return metadata->display_id;
}

std::optional<std::uint32_t>
CGPlayer_C::GetVisibleWeaponDisplayId(const std::uint8_t visible_weapon_index) const {
  if (IsVisibleWeaponDisplaySuppressed(visible_weapon_index)) {
    return std::nullopt;
  }

  return GetVisibleWeaponDisplayIdRaw(visible_weapon_index);
}

const data::dbc::ItemDisplayInfoEntry*
CGPlayer_C::GetVisibleWeaponDisplayInfo(
    const std::uint8_t visible_weapon_index) const {
  const auto display_id = GetVisibleWeaponDisplayId(visible_weapon_index);
  if (!display_id.has_value()) {
    return nullptr;
  }

  const auto* dbc = LookupActiveDbcLoader(*this);
  if (dbc == nullptr) {
    return nullptr;
  }

  return dbc->item_display_info().LookupEntry(*display_id);
}

const data::dbc::ItemDisplayInfoEntry*
CGPlayer_C::GetEquipSlotItemDisplayRecord(
    const std::uint8_t slot_index) const {
  if (slot_index >= kMaxEquipSlots) {
    return nullptr;
  }

  const auto metadata = GetVisibleItemTemplateMetadata(slot_index);
  if (!metadata.has_value() || metadata->display_id == 0) {
    return nullptr;
  }

  const auto* dbc = LookupActiveDbcLoader(*this);
  if (dbc == nullptr) {
    return nullptr;
  }

  return dbc->item_display_info().LookupEntry(metadata->display_id);
}

bool CGPlayer_C::IsVisibleWeaponDisplaySuppressed(
    const std::uint8_t visible_weapon_index) const {
  if (visible_weapon_index == 2u) {
    return false;
  }

  const bool equipment_visibility_suppressed =
      (State().GetUnitFlags2() & kEquipmentVisibilityFlags2Bit) != 0u;
  if (!equipment_visibility_suppressed) {
    return visible_weapon_index == 1u &&
           OffhandVisibilitySuppressedByAuraState(*this);
  }

  const bool mainhand_suppressed =
      VisibleWeaponUsesWeaponClass(*this, kVisibleWeaponEquipSlots[0]);
  if (visible_weapon_index == 0u) {
    return mainhand_suppressed;
  }

  if (visible_weapon_index == 1u) {
    if (!mainhand_suppressed &&
        VisibleWeaponUsesWeaponClass(*this, kVisibleWeaponEquipSlots[1])) {
      return true;
    }

    return OffhandVisibilitySuppressedByAuraState(*this);
  }

  return false;
}

std::optional<VisibleItemTemplateMetadata>
CGPlayer_C::GetVisibleWeaponSlotMetadata(
    const std::uint8_t visible_weapon_index,
    const bool force_visible) const {

  if (!GetVisibleWeaponDisplayIdRaw(visible_weapon_index).has_value()) {
    return std::nullopt;
  }

  const auto equip_slot = ResolveVisibleWeaponEquipSlot(visible_weapon_index);
  if (!equip_slot.has_value()) {
    return std::nullopt;
  }

  if (force_visible) {
    return GetVisibleItemTemplateMetadata(*equip_slot);
  }

  if (visible_weapon_index == 0u) {

    if ((State().GetUnitFlags2() & kEquipmentVisibilityFlags2Bit) != 0u) {
      const auto meta = GetVisibleItemTemplateMetadata(*equip_slot);
      if (meta.has_value()) {
        if (meta->item_class ==
            static_cast<std::uint32_t>(ItemClass::Weapon)) {
          return std::nullopt;
        }
        return meta;
      }

    }
  } else if (visible_weapon_index == 1u) {

    if ((State().GetUnitFlags2() & kEquipmentVisibilityFlags2Bit) != 0u) {
      if (!IsVisibleWeaponDisplaySuppressed(0u)) {
        const auto off_meta = GetVisibleItemTemplateMetadata(*equip_slot);
        if (off_meta.has_value() &&
            off_meta->item_class ==
                static_cast<std::uint32_t>(ItemClass::Weapon)) {
          return std::nullopt;
        }
      }
    }

    if (OffhandVisibilitySuppressedByAuraState(*this)) {
      return std::nullopt;
    }
  } else if (visible_weapon_index == 2u) {

    if ((State().GetAuraState() & 0x400u) != 0u) {
      return std::nullopt;
    }
  }

  return GetVisibleItemTemplateMetadata(*equip_slot);
}

std::optional<std::uint32_t>
CGPlayer_C::GetVisibleItemTemplateEntry(const std::uint8_t slot) const {
  const auto raw_entry = GetVisibleItemEntry(slot);
  if (raw_entry == 0) {
    return std::nullopt;
  }

  const auto signed_entry = static_cast<std::int32_t>(raw_entry);
  if (signed_entry >= 0) {
    return raw_entry;
  }

  return static_cast<std::uint32_t>(-static_cast<std::int64_t>(signed_entry));
}

std::optional<VisibleItemTemplateMetadata>
CGPlayer_C::GetVisibleItemTemplateMetadata(const std::uint8_t slot) const {
  const auto entry = GetVisibleItemTemplateEntry(slot);
  if (!entry.has_value()) {
    return std::nullopt;
  }

  VisibleItemTemplateMetadata metadata;
  metadata.entry = *entry;

  auto* const objects = object_manager();
  if (objects != nullptr &&
      ResolveVisibleItemTemplateCacheMetadata(*objects, *entry, metadata)) {
    return metadata;
  }

  if (objects == nullptr) {
    return std::nullopt;
  }
  const auto* dbc_item = LookupVisibleItemDbcFallback(*objects, *entry);
  if (dbc_item == nullptr) {
    return std::nullopt;
  }

  metadata.item_class = dbc_item->class_id;
  metadata.subclass = dbc_item->subclass_id;
  metadata.display_id = dbc_item->display_info_id;
  metadata.sound_override = dbc_item->sound_override_subclass;
  metadata.material = static_cast<std::int32_t>(dbc_item->material);
  metadata.inventory_type = dbc_item->inventory_type;
  metadata.sheath = dbc_item->sheathe_type;
  return metadata;
}

bool CGPlayer_C::IsVisibleWeaponSlotTwoHandWeapon(const std::uint8_t slot) const {
  const auto metadata = GetVisibleItemTemplateMetadata(slot);
  return metadata.has_value() &&
         metadata->item_class == static_cast<std::uint32_t>(ItemClass::Weapon) &&
         IsTwoHandWeaponSubclass(metadata->subclass);
}

void CGPlayer_C::RefreshWeaponAttachmentVisual(
    const std::uint32_t attachment_id,
    const std::uint8_t weapon_slot) {

  std::uint8_t equip_slot;
  if (weapon_slot == 0u) {
    equip_slot = 15u;
  } else if (weapon_slot == 1u) {
    equip_slot = 16u;
  } else {
    return;
  }

  const auto entry = GetVisibleItemEntry(equip_slot);
  const auto signed_entry = static_cast<std::int32_t>(entry);
  if (signed_entry == 0) {
    return;
  }

  SetItemVisual(equip_slot, attachment_id);
}

bool CGPlayer_C::SetItemVisual(
    const std::uint8_t visible_item_slot,
    const std::uint32_t attachment_id) {

  const auto aura_visual = GetVisibleItemAuraVisual(visible_item_slot);
  if (aura_visual == 0) {
    return false;
  }

  last_item_visual_attachment_id_ = attachment_id;
  last_item_visual_aura_id_ = aura_visual;
  last_item_visual_dirty_ = true;

  return true;
}

bool CGPlayer_C::ConsumeItemVisualUpdate(
    std::uint32_t& out_attachment_id,
    std::uint32_t& out_aura_visual_id) {
  if (!last_item_visual_dirty_) {
    return false;
  }
  out_attachment_id = last_item_visual_attachment_id_;
  out_aura_visual_id = last_item_visual_aura_id_;
  last_item_visual_dirty_ = false;
  return true;
}

ObjectGuid CGPlayer_C::GetEquippedItem(std::uint8_t slot) const {

  if (slot >= 19)
    return ObjectGuid();
  return GetGuidField(static_cast<std::uint16_t>(PLAYER_FIELD_INV_SLOT_HEAD + slot * 2));
}

ObjectGuid CGPlayer_C::GetBackpackItem(std::uint8_t slot) const {

  if (slot >= 16)
    return ObjectGuid();
  return GetGuidField(static_cast<std::uint16_t>(PLAYER_FIELD_PACK_SLOT_1 + slot * 2));
}

ObjectGuid CGPlayer_C::GetBagItem(std::uint8_t bag_slot, std::uint8_t item_slot) const {

  if (bag_slot >= 4)
    return ObjectGuid();

  ObjectGuid bag_guid =
      GetGuidField(static_cast<std::uint16_t>(PLAYER_FIELD_INV_SLOT_HEAD + (19 + bag_slot) * 2));
  if (bag_guid.IsEmpty())
    return ObjectGuid();

  const auto* const objects = object_manager();
  const auto* const container =
      objects != nullptr ? objects->GetContainer(bag_guid) : nullptr;
  if (!container)
    return ObjectGuid();

  std::uint32_t num_slots = container->GetUInt32(CONTAINER_FIELD_NUM_SLOTS);
  if (item_slot >= num_slots)
    return ObjectGuid();

  return container->GetGuidField(
      static_cast<std::uint16_t>(CONTAINER_FIELD_SLOT_1 + item_slot * 2));
}

ObjectGuid CGPlayer_C::GetBagSlotGuid(std::uint8_t bag, std::uint8_t slot) const {
  return GetBagItem(bag, slot);
}

void CGPlayer_C::QueuePendingItemEnchantTimeUpdate(const ObjectGuid& item_guid,
                                                   const std::uint32_t enchant_slot,
                                                   const std::int32_t duration_seconds) {
  pending_item_enchant_time_updates_.push_back(
      {.item_guid = item_guid,
       .enchant_slot = enchant_slot,
       .duration_seconds = duration_seconds});
}

void CGPlayer_C::ApplyPendingItemEnchantTimeUpdates(CGItem_C& item) {
  if (pending_item_enchant_time_updates_.empty()) {
    return;
  }

  const auto item_guid = item.GetGuid();
  bool has_match = false;
  for (const auto& update : pending_item_enchant_time_updates_) {
    if (update.item_guid != item_guid) {
      continue;
    }

    has_match = true;
    item.SetEnchantTimeRemainingSeconds(
        static_cast<std::uint8_t>(update.enchant_slot), update.duration_seconds);
  }

  if (!has_match) {
    return;
  }

  pending_item_enchant_time_updates_.erase(
      std::remove_if(pending_item_enchant_time_updates_.begin(),
                     pending_item_enchant_time_updates_.end(),
                     [item_guid](const PendingItemEnchantTimeUpdate& update) {
                       return update.item_guid == item_guid;
                     }),
      pending_item_enchant_time_updates_.end());
}

std::uint32_t CGPlayer_C::GetGuildID() const {
  return GetUInt32(PLAYER_GUILDID);
}

std::uint32_t CGPlayer_C::GetGuildRank() const {
  return GetUInt32(PLAYER_GUILDRANK);
}

CGPlayer_C::QuestLogEntry CGPlayer_C::GetQuestLog(std::uint8_t slot) const {
  QuestLogEntry entry{};
  if (slot >= 25)
    return entry;

  std::uint16_t base = static_cast<std::uint16_t>(PLAYER_QUEST_LOG_1_1 + slot * 5);

  entry.quest_id = GetUInt32(base);
  entry.state = GetUInt32(static_cast<std::uint16_t>(base + 1));

  std::uint32_t counts_low = GetUInt32(static_cast<std::uint16_t>(base + 2));
  std::uint32_t counts_high = GetUInt32(static_cast<std::uint16_t>(base + 3));
  entry.counts[0] = counts_low & 0xFFFF;
  entry.counts[1] = (counts_low >> 16) & 0xFFFF;
  entry.counts[2] = counts_high & 0xFFFF;
  entry.counts[3] = (counts_high >> 16) & 0xFFFF;

  entry.timer = GetUInt32(static_cast<std::uint16_t>(base + 4));

  return entry;
}

std::uint32_t CGPlayer_C::GetRestStateExperience() const {
  return GetUInt32(PLAYER_REST_STATE_EXPERIENCE);
}

bool CGPlayer_C::IsRested() const {
  return GetRestStateExperience() > 0;
}

bool CGPlayer_C::IsResting() const {

  constexpr std::uint32_t kPlayerFlagsResting = 0x20u;
  return (GetPlayerFlags() & kPlayerFlagsResting) != 0;
}

bool CGPlayer_C::IsGroupLeader(const WorldSession& session) const {
  const auto& gm = session.group();
  if (!gm.IsInGroup()) return false;

  return gm.leader_guid() == GetGuid();
}

bool CGPlayer_C::IsGroupAssistant(const WorldSession& session) const {
  const auto& gm = session.group();
  if (!gm.IsInGroup()) return false;

  return (gm.my_flags() & static_cast<std::uint8_t>(GroupMemberFlag::kAssistant)) != 0;
}

std::uint32_t CGPlayer_C::GetFreeTalentPoints() const {
  return GetUInt32(PLAYER_CHARACTER_POINTS1);
}

std::uint32_t CGPlayer_C::GetWatchedFactionIndex() const {
  return watched_faction_index_;
}

void CGPlayer_C::SetWatchedFactionIndex(std::uint32_t index) {
  watched_faction_index_ = index;
}

std::uint32_t CGPlayer_C::GetPlayerFlags() const {
  return GetUInt32(PLAYER_FLAGS);
}

std::uint16_t CGPlayer_C::GetOverrideSpellDataId() const {
  return static_cast<std::uint16_t>(GetUInt32(PLAYER_FIELD_BYTES2) & 0xFFFFu);
}

float CGPlayer_C::GetMeleeCritChance() const {
  return GetCritPercentage();
}

float CGPlayer_C::GetRangedCritChance() const {
  return GetRangedCritPercentage();
}

float CGPlayer_C::GetSpellCritChance(std::uint8_t school) const {
  return GetSpellCritPercentage(school);
}

float CGPlayer_C::GetDodgeChance() const {
  return GetDodgePercentage();
}

float CGPlayer_C::GetParryChance() const {
  return GetParryPercentage();
}

float CGPlayer_C::GetBlockChance() const {
  return GetBlockPercentage();
}

std::int32_t CGPlayer_C::GetSpellBonusDamage(std::uint8_t school) const {
  if (school > 6)
    return 0;
  return GetModDamageDonePositive(school) + GetModDamageDoneNegative(school);
}

std::int32_t CGPlayer_C::GetSpellBonusHealing() const {
  return static_cast<std::int32_t>(GetUInt32(PLAYER_FIELD_MOD_HEALING_DONE_POS));
}

void CGPlayer_C::SetEquipment(std::uint8_t slot, const EquipmentSlotInfo &item) {
  if (slot < kMaxEquipSlots) {
    equipment_[slot] = item;
  }
}

const EquipmentSlotInfo *CGPlayer_C::GetEquipment(std::uint8_t slot) const {
  if (slot >= kMaxEquipSlots)
    return nullptr;
  return &equipment_[slot];
}

namespace detail {

void OnGemItemTemplateResolved_RecountColors(ObjectManager& objects,
                                             const bool success) {
  if (!success) {
    return;
  }
  auto *player = objects.GetActivePlayer();
  if (player) {
    player->RecountEquippedGemColorCounts();
  }
}

AsyncQueryChannel::CallbackKey BuildGemColorRecountCallbackKey(
    std::uint32_t item_entry) {
  return AsyncQueryChannel::CallbackKey(
      reinterpret_cast<std::uintptr_t>(&OnGemItemTemplateResolved_RecountColors),
      item_entry);
}

}

void CGPlayer_C::RecountEquippedGemColorCounts() {
  equipped_gem_color_counts_.Reset();

  const auto* const dbc = dbc_loader();
  if (dbc == nullptr) {
    return;
  }

  auto* const objects = object_manager();
  if (objects == nullptr) {
    return;
  }

  static constexpr std::array<std::uint8_t, 3> kSocketSlots = {
      kEnchantSlotSocket1, kEnchantSlotSocket2, kEnchantSlotSocket3};
  for (std::uint8_t equip_slot = 0; equip_slot < kMaxEquipSlots; ++equip_slot) {
    const auto item_guid = GetInventorySlotGuid(equip_slot);
    if (item_guid.IsEmpty()) {
      continue;
    }

    const auto* item = objects->GetItem(item_guid);
    if (item == nullptr) {
      continue;
    }

    for (const auto socket_slot : kSocketSlots) {
      const auto enchant_id = item->GetEnchantId(socket_slot);
      if (enchant_id == 0u) {
        continue;
      }

      const auto* enchant =
          dbc->spell_item_enchantment().LookupEntry(enchant_id);
      if (enchant == nullptr || enchant->gem_id == 0u) {
        continue;
      }

      auto request_options = QueryCache::QueryRequestOptions{
          .callback_key =
              detail::BuildGemColorRecountCallbackKey(enchant->gem_id),
          .dedupe_callbacks = true,
          .callback = [objects](const bool success) {
            detail::OnGemItemTemplateResolved_RecountColors(*objects, success);
          },
      };
      const auto* gem_item = objects->query_cache().GetOrRequestItemTemplate(
          enchant->gem_id, std::move(request_options));
      if (gem_item == nullptr || gem_item->gem_properties == 0u) {
        continue;
      }

      const auto* gem_props =
          dbc->gem_properties().LookupEntry(gem_item->gem_properties);
      if (gem_props == nullptr) {
        continue;
      }

      const auto color_mask = gem_props->type;
      if ((color_mask & kGemColorMaskMeta) != 0u) {
        ++equipped_gem_color_counts_.meta;
      }
      if ((color_mask & kGemColorMaskRed) != 0u) {
        ++equipped_gem_color_counts_.red;
      }
      if ((color_mask & kGemColorMaskYellow) != 0u) {
        ++equipped_gem_color_counts_.yellow;
      }
      if ((color_mask & kGemColorMaskBlue) != 0u) {
        ++equipped_gem_color_counts_.blue;
      }
    }
  }
}

void CGPlayer_C::SetMoney(std::int32_t copper) {

  predicted_money_ = copper;
}

void CGPlayer_C::SetXP(std::uint32_t current, std::uint32_t max) {

  predicted_xp_current_ = current;
  predicted_xp_max_ = max;
}

std::uint32_t CGPlayer_C::GetHonorableKills() const {
  return GetUInt32(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS);
}

std::uint32_t CGPlayer_C::GetHonorPoints() const {
  return GetUInt32(PLAYER_FIELD_HONOR_CURRENCY);
}

std::uint32_t CGPlayer_C::GetArenaPoints() const {
  return GetUInt32(PLAYER_FIELD_ARENA_CURRENCY);
}

std::uint8_t CGPlayer_C::GetPlayerStandState() const {
  if (GetGuid() == CGObject_C::GetActivePlayerGuid()) {
    return cached_stand_state_;
  }
  return Animation().GetStandState();
}

std::uint8_t CGPlayer_C::GetComboPoints() const {
  if (GetGuid() == CGObject_C::GetActivePlayerGuid()) {
    return combo_points_;
  }

  return static_cast<std::uint8_t>(GetUInt32(UNIT_FIELD_BYTES_1) & 0xFFu);
}

void CGPlayer_C::SetComboPoints(std::uint8_t points) {
  combo_points_ = points;
}

bool CGPlayer_C::IsGhost() const {

  return (GetPlayerFlags() & 0x10) != 0;
}

bool CGPlayer_C::AutoInteractSuppressesInteractionRange() const {

  return static_cast<std::int32_t>(State().GetHealth()) > 0 &&
         IsActivePlayer() &&
         openwow::ui::game::CVarSystem::Instance().GetCVarBool("autoInteract");
}

bool CGPlayer_C::IsAlive() const {
  return State().GetHealth() > 0 && !State().IsDead();
}

bool CGPlayer_C::IsLooting() const {
  return (internal_flags_ >> 25) & 1;
}

std::string CGPlayer_C::GetPlayerName() const {
  return GetName();
}

std::string CGPlayer_C::GetDisplayName() const {
  if (!display_name_override_.empty()) {
    return display_name_override_;
  }
  return GetName();
}

ObjectGuid CGPlayer_C::GetPlayerTarget() const {
  return State().GetTarget();
}

std::string CGPlayer_C::GetModelPath() const {
  const auto* display_info = Presentation().ResolveCreatureModelDisplayInfo();
  if (display_info == nullptr) {
    return "Spells\\ErrorCube.mdx";
  }

  return display_info->model_path;
}

bool CGPlayer_C::HasNoPetFamily() const {
  return true;
}

const CreatureTemplateInfo *CGPlayer_C::GetSummonedUnitCreatureData() const {
  const auto summon_guid = State().GetPetGUID();
  if (summon_guid.IsEmpty()) {
    return nullptr;
  }

  const auto* const objects = object_manager();
  const auto* const unit =
      objects != nullptr ? objects->GetUnit(summon_guid) : nullptr;
  if (unit == nullptr) {
    return nullptr;
  }

  const auto entry = unit->GetEntry();
  if (entry == 0u) {
    return nullptr;
  }

  if (objects == nullptr) {
    return nullptr;
  }

  return objects->query_cache().GetCreatureTemplate(entry);
}

std::uint32_t CGPlayer_C::GetSummonedUnitArmorMaterialSoundCategory() const {
  const auto summon_guid = State().GetPetGUID();
  if (summon_guid.IsEmpty()) {
    return 0;
  }

  const auto* const objects = object_manager();
  const auto* const unit =
      objects != nullptr ? objects->GetUnit(summon_guid) : nullptr;
  if (unit == nullptr) {
    return 0;
  }

  const auto *body_equip = unit->Presentation().BodyArmorEquipmentData();
  if (body_equip == nullptr) {
    return 0;
  }

  if (body_equip->item_class != 4) {
    return 0;
  }

  return data::DBClient_MaterialGetArmorSoundCategory(body_equip->material_id);
}

void CGPlayer_C::RenderAutoAttackInteractionIndicator() {
  const auto* const objects = object_manager();
  if (objects == nullptr) {
    return;
  }

  if (GetGuid().GetRawValue() !=
      objects->player_control().active_mover_guid) {
    return;
  }

  const auto auto_attack_type = Interaction().AutoAttackType();
  if (auto_attack_type == kAutoAttackTypeIdle) {
    return;
  }
  if (auto_attack_type != kAutoAttackTypeMelee &&
      auto_attack_type != kAutoAttackTypeRanged) {
    return;
  }

  const std::uint32_t tick = core::GameClock::GetTickCount32();
  const std::uint32_t remainder = tick % kIndicatorPulsePeriodMs;
  const float phase = std::sin(
      static_cast<float>(remainder) * kTwoPi /
      static_cast<float>(kIndicatorPulsePeriodMs));
  const float mapped = (phase + 1.0f) * 0.5f;
  const float radius =
      kIndicatorMinRadius +
      (kIndicatorMaxRadius - kIndicatorMinRadius) * mapped;

  float world_pos[3];
  Passenger_TransformLocalToWorldPosition(
      *objects, GetTransportGUID().GetRawValue(),
      world_pos,
      Interaction().AutoAttackTargetPosition().data());

  const float aabb_min[3] = {
      world_pos[0] - radius,
      world_pos[1] - radius,
      world_pos[2] - radius,
  };
  const float aabb_max[3] = {
      world_pos[0] + radius,
      world_pos[1] + radius,
      world_pos[2] + radius,
  };

  float billboard_matrix[16];
  openwow::math::row_major_mat4x4::SetIdentity(billboard_matrix);

  (void)aabb_min;
  (void)aabb_max;
  (void)billboard_matrix;
}

Position CGPlayer_C::GetNamePlatePosition() const {
  const bool use_mounted_name =
      Mount().CachedDisplayForSpell() > 0u &&
      !State().HasSpellStateFlags(kSpellStateSuppressMountFootprint);

  float attachment_pos[3]{};
  if (use_mounted_name &&
      QueryPrimaryM2AttachmentPosition(
          *this,
          openwow::render::m2::kM2AttachmentLookupPlayerNameMounted,
          attachment_pos)) {
    return {attachment_pos[0], attachment_pos[1], attachment_pos[2], GetOrientation()};
  }

  if (QueryPrimaryM2AttachmentPosition(
          *this,
          openwow::render::m2::kM2AttachmentLookupPlayerName,
          attachment_pos)) {
    return {attachment_pos[0], attachment_pos[1], attachment_pos[2], GetOrientation()};
  }

  auto pos = GetPosition();

  pos.z += GetModelBoundingBoxHeight() * GetNativeScale() * 1.25f;
  return pos;
}

void CGPlayer_C::UpdateModelTintColor(const std::uint32_t now_ms) {
  constexpr float kByteToFloat = 1.0f / 255.0f;

  std::uint32_t packed = 0x00FFFFFFu;

  if (!Presentation().TryGetInterpolatedBodyColor(now_ms, packed)) {
    packed = SpellVisuals().BodyTintEffects().empty()
                 ? Presentation().DefaultBodyColor()
                 : SpellVisuals().BodyTintEffects().front().packed_argb;
  }

  const auto *bytes = reinterpret_cast<const std::uint8_t *>(&packed);
  Presentation().SetModelTintColor({
      static_cast<float>(bytes[2]) * kByteToFloat,
      static_cast<float>(bytes[1]) * kByteToFloat,
      static_cast<float>(bytes[0]) * kByteToFloat});
}

CGObject_C::ModelTintColor CGPlayer_C::GetModelTintColor() const {
  return Presentation().ModelTintColor();
}

float CGPlayer_C::GetWalkAnimSpeed() const {
  return walk_anim_speed_;
}

float CGPlayer_C::GetPlayerCombatReach() const {
  return State().GetCombatReach();
}

float CGPlayer_C::GetWorldFacing() const {
  const auto* const objects = object_manager();

  return objects == nullptr
             ? GetOrientation()
             : Movement_TransformLocalFacingToWorld(
                   *objects, Movement().Data().GetTransportGuid(),
                   Movement().Data().GetScalarFacing());
}

std::uint32_t CGPlayer_C::BuildPlayerTooltipNameText(
    const WorldSession& session, const std::uint32_t flags,
                                                     std::string &out) const {
  return BuildTooltipNameText(session, flags, out,
                              true);
}

bool CGPlayer_C::CanShowResetInstances(const WorldSession* const session) {
  if (session == nullptr) {
    return false;
  }

  const auto& instance = session->instance();
  if (instance.instance_save_count() == 0) {
    return false;
  }

  const auto current_map_id = session->current_map_id();
  if (!IsDungeonOrRaidMap(*session, current_map_id)) {
    return false;
  }

  const auto difficulty =
      static_cast<std::uint8_t>(GroupSystem::Get().GetDungeonDifficulty());
  if (session->HasMapDifficultyRaidDuration(current_map_id, difficulty)) {
    return false;
  }

  const auto& visibility_state = instance.reset_instance_visibility_state();
  if (visibility_state.anchor_map_id == 0 ||
      session->LookupMapEntry(visibility_state.anchor_map_id) == nullptr) {
    return false;
  }

  const auto elapsed_seconds =
      static_cast<std::int64_t>(std::time(nullptr)) -
      static_cast<std::int64_t>(visibility_state.anchor_unix_time);
  return elapsed_seconds < kResetInstanceAnchorWindowExclusiveSeconds;
}

std::uint32_t CGPlayer_C::GetProfessionPoints() const {
  return GetUInt32(PLAYER_CHARACTER_POINTS2);
}

std::uint32_t CGPlayer_C::GetGlyphSlot(std::uint8_t slot) const {
  if (slot >= 6)
    return 0;
  return GetUInt32(static_cast<std::uint16_t>(PLAYER_FIELD_GLYPH_SLOTS_1 + slot));
}

std::uint32_t CGPlayer_C::GetGlyph(std::uint8_t slot) const {
  if (slot >= 6)
    return 0;
  return GetUInt32(static_cast<std::uint16_t>(PLAYER_FIELD_GLYPHS_1 + slot));
}

std::uint32_t CGPlayer_C::GetGlyphsEnabled() const {
  return GetUInt32(PLAYER_GLYPHS_ENABLED);
}

float CGPlayer_C::GetRuneRegen(std::uint8_t rune) const {
  if (rune >= 4)
    return 0.0f;
  return GetFloat(static_cast<std::uint16_t>(PLAYER_RUNE_REGEN_1 + rune));
}

bool CGPlayer_C::HasExploredZone(std::uint32_t zone_index) const {

  if (zone_index >= 4096)
    return true;
  std::uint32_t block = zone_index / 32;
  std::uint32_t bit = zone_index % 32;
  std::uint32_t val = GetUInt32(static_cast<std::uint16_t>(PLAYER_EXPLORED_ZONES_1 + block));
  return (val & (1u << bit)) != 0;
}

std::uint32_t CGPlayer_C::GetTrackCreatures() const {
  return GetUInt32(PLAYER_TRACK_CREATURES);
}

std::uint32_t CGPlayer_C::GetTrackResources() const {
  return GetUInt32(PLAYER_TRACK_RESOURCES);
}

std::int32_t CGPlayer_C::GetCombatRating(std::uint8_t rating) const {
  if (rating >= 25)
    return 0;
  std::uint32_t raw = GetUInt32(static_cast<std::uint16_t>(PLAYER_FIELD_COMBAT_RATING_1 + rating));
  std::int32_t val;
  std::memcpy(&val, &raw, 4);
  return val;
}

CGPlayer_C::ArenaTeamInfo CGPlayer_C::GetArenaTeamInfo(std::uint8_t team_index) const {
  ArenaTeamInfo info{};
  if (team_index >= 3)
    return info;

  std::uint16_t base =
      static_cast<std::uint16_t>(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + team_index * 7);

  info.team_id = GetUInt32(base);
  info.unknown_1 = GetUInt32(static_cast<std::uint16_t>(base + 1));
  info.captain_state = GetUInt32(static_cast<std::uint16_t>(base + 2));
  info.weekly_games_played = GetUInt32(static_cast<std::uint16_t>(base + 3));
  info.weekly_games_won = GetUInt32(static_cast<std::uint16_t>(base + 4));
  info.unknown_5 = GetUInt32(static_cast<std::uint16_t>(base + 5));
  info.personal_rating = GetUInt32(static_cast<std::uint16_t>(base + 6));

  return info;
}

bool CGPlayer_C::HasTitle(std::uint32_t title_bit) const {

  if (title_bit >= 192)
    return false;

  std::uint16_t field_base;
  std::uint32_t local_bit;
  if (title_bit < 64) {
    field_base = PLAYER_FIELD_KNOWN_TITLES;
    local_bit = title_bit;
  } else if (title_bit < 128) {
    field_base = PLAYER_FIELD_KNOWN_TITLES1;
    local_bit = title_bit - 64;
  } else {
    field_base = PLAYER_FIELD_KNOWN_TITLES2;
    local_bit = title_bit - 128;
  }

  std::uint32_t block = local_bit / 32;
  std::uint32_t bit = local_bit % 32;
  std::uint32_t val = GetUInt32(static_cast<std::uint16_t>(field_base + block));
  return (val & (1u << bit)) != 0;
}

std::uint32_t CGPlayer_C::GetChosenTitle() const {
  return GetUInt32(PLAYER_CHOSEN_TITLE);
}

std::uint32_t CGPlayer_C::GetDailyQuestId(std::uint8_t slot) const {
  if (slot >= 25)
    return 0;
  return GetUInt32(static_cast<std::uint16_t>(PLAYER_FIELD_DAILY_QUESTS_1 + slot));
}

std::uint8_t CGPlayer_C::GetDailyQuestCount() const {
  std::uint8_t count = 0;
  for (std::uint8_t i = 0; i < 25; ++i) {
    if (GetDailyQuestId(i) != 0)
      ++count;
  }
  return count;
}

ObjectGuid CGPlayer_C::GetFarsightTarget() const {
  return GetGuidField(PLAYER_FARSIGHT);
}

void CGPlayer_C::ActivateFarSightFocus(WorldSession& session,
                                       const CGObject_C& focus_object) {
  if (!IsActivePlayer()) {
    return;
  }

  if (!far_sight_view_active_) {
    SendFarSightPacket(session, true);
    far_sight_view_active_ = true;
  }

  far_sight_focus_guid_ = focus_object.GetGuid();

  Interaction().CompleteAutoAttackInteraction(false, true);
  auto& spell_client = session.spells();
  if (spell_client.GetAutoRepeatSpellId() != 0) {
    spell_client.CancelSpell(session,
                             SpellSlotType::kAutoRepeat);
  }

  const ObjectGuid new_mover_guid =
      Movement().CanControlCharacter() ? GetGuid() : ObjectGuid();
  session.player_control_runtime().SetActiveMover(
      session, session.objects(), session.missile_trajectory(),
      new_mover_guid.GetRawValue());

  const auto timestamp = GetControlFocusTimestamp(session);
  ProcessControlFocusMovement(timestamp);
  if (auto* const camera = session.world_camera(); camera != nullptr) {
    BindCameraToObject(*camera, focus_object);
  }
  ProcessControlFocusMovement(timestamp);

  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::PLAYER_FARSIGHT_FOCUS_CHANGED);
}

void CGPlayer_C::ClearFarSightFocus(WorldSession& session) {
  if (!far_sight_view_active_ && far_sight_focus_guid_.IsEmpty()) {
    return;
  }

  if (far_sight_view_active_) {
    SendFarSightPacket(session, false);
  }

  far_sight_view_active_ = false;
  far_sight_focus_guid_ = ObjectGuid();

  auto& spell_client = session.spells();
  if (spell_client.GetTargeting().IsTargeting()) {
    spell_client.GetTargeting().CancelTargeting();
  }

  const ObjectGuid new_mover_guid =
      Movement().CanControlCharacter() ? GetGuid() : ObjectGuid();
  session.player_control_runtime().SetActiveMover(
      session, session.objects(), session.missile_trajectory(),
      new_mover_guid.GetRawValue());

  const auto timestamp = GetControlFocusTimestamp(session);
  ProcessControlFocusMovement(timestamp);
  if (auto* const camera = session.world_camera(); camera != nullptr) {
    BindCameraToObject(*camera, *this);
  }
  ProcessControlFocusMovement(timestamp);

  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::PLAYER_FARSIGHT_FOCUS_CHANGED);
}

std::uint32_t CGPlayer_C::GetMaxLevel() const {
  return GetUInt32(PLAYER_FIELD_MAX_LEVEL);
}

std::uint8_t CGPlayer_C::GetSkinColor() const {
  return static_cast<std::uint8_t>(GetUInt32(PLAYER_BYTES) & 0xFF);
}

std::uint8_t CGPlayer_C::GetFace() const {
  return static_cast<std::uint8_t>((GetUInt32(PLAYER_BYTES) >> 8) & 0xFF);
}

std::uint8_t CGPlayer_C::GetHairStyle() const {
  return static_cast<std::uint8_t>((GetUInt32(PLAYER_BYTES) >> 16) & 0xFF);
}

std::uint8_t CGPlayer_C::GetHairColor() const {
  return static_cast<std::uint8_t>((GetUInt32(PLAYER_BYTES) >> 24) & 0xFF);
}

std::uint8_t CGPlayer_C::GetFacialHair() const {
  return static_cast<std::uint8_t>(GetUInt32(PLAYER_BYTES_2) & 0xFF);
}

std::uint8_t CGPlayer_C::GetBankBagSlotCount() const {
  return static_cast<std::uint8_t>((GetUInt32(PLAYER_BYTES_2) >> 16) & 0xFF);
}

std::uint8_t CGPlayer_C::GetRestState() const {
  return static_cast<std::uint8_t>((GetUInt32(PLAYER_BYTES_2) >> 24) & 0xFF);
}

std::uint8_t CGPlayer_C::GetActionBarToggles() const {
  return static_cast<std::uint8_t>(
      (GetUInt32(PLAYER_FIELD_ACTION_BAR_TOGGLES) >> 16) & 0xFF);
}

std::uint8_t CGPlayer_C::GetDrunkState() const {
  return static_cast<std::uint8_t>(GetUInt32(PLAYER_BYTES_3) & 0xFF);
}

std::uint8_t CGPlayer_C::GetGenderFromBytes() const {
  return static_cast<std::uint8_t>((GetUInt32(PLAYER_BYTES_3) >> 8) & 0xFF);
}

std::uint8_t CGPlayer_C::GetPvpMedalRank() const {
  return static_cast<std::uint8_t>((GetUInt32(PLAYER_BYTES_3) >> 16) & 0xFF);
}

std::uint32_t CGPlayer_C::GetExpertise() const {
  return GetUInt32(PLAYER_EXPERTISE);
}

std::uint32_t CGPlayer_C::GetOffhandExpertise() const {
  return GetUInt32(PLAYER_OFFHAND_EXPERTISE);
}

bool CGPlayer_C::HasActiveInebriation() const {
  return openwow::game::HasActiveInebriation(
      GetDrunkState(), GetFakeInebriation());
}

float CGPlayer_C::GetNormalizedInebriation() const {
  return openwow::game::ComputeNormalizedInebriation(
      GetDrunkState(), GetFakeInebriation());
}

std::uint32_t CGPlayer_C::GetFakeInebriation() const {
  return GetUInt32(PLAYER_FAKE_INEBRIATION);
}

std::uint32_t CGPlayer_C::GetPetSpellPower() const {
  return GetUInt32(PLAYER_PET_SPELL_POWER);
}

std::int32_t CGPlayer_C::GetModDamageDonePositive(std::uint8_t school) const {
  if (school > 6)
    return 0;
  std::uint32_t raw =
      GetUInt32(static_cast<std::uint16_t>(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + school));
  std::int32_t val;
  std::memcpy(&val, &raw, 4);
  return val;
}

std::int32_t CGPlayer_C::GetModDamageDoneNegative(std::uint8_t school) const {
  if (school > 6)
    return 0;
  std::uint32_t raw =
      GetUInt32(static_cast<std::uint16_t>(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + school));
  std::int32_t val;
  std::memcpy(&val, &raw, 4);
  return val;
}

float CGPlayer_C::GetModDamageDonePercent(std::uint8_t school) const {
  if (school > 6)
    return 0.0f;
  return GetFloat(static_cast<std::uint16_t>(PLAYER_FIELD_MOD_DAMAGE_DONE_PCT + school));
}

std::int32_t CGPlayer_C::GetModHealingDonePositive() const {
  std::uint32_t raw = GetUInt32(PLAYER_FIELD_MOD_HEALING_DONE_POS);
  std::int32_t val;
  std::memcpy(&val, &raw, 4);
  return val;
}

bool CGPlayer_C::HasKnownCurrency(std::uint32_t bit_index) const {

  if (bit_index >= 64)
    return false;
  std::uint32_t block = bit_index / 32;
  std::uint32_t bit = bit_index % 32;
  std::uint32_t val = GetUInt32(static_cast<std::uint16_t>(PLAYER_FIELD_KNOWN_CURRENCIES + block));
  return (val & (1u << bit)) != 0;
}

bool CGPlayer_C::CheckAllItemsNeedRepair() const {
  auto* const objects = object_manager();
  if (objects == nullptr) {
    return false;
  }

  auto needs_repair = [&](ObjectGuid item_guid) -> bool {
    if (item_guid.IsEmpty())
      return false;
    const auto* item = objects->GetItem(item_guid);
    if (!item)
      return false;
    if (item->GetDurability() != 0)
      return false;
    if (item->HasItemFlag(0x04))
      return false;

    const auto *item_template = item->GetOrRequestQueryItemTemplate();
    return item_template != nullptr &&
           (item_template->flags & kItemTemplateFlagHasDurability) != 0u;
  };

  for (std::uint8_t slot = 0; slot <= 18; ++slot) {
    ObjectGuid guid = GetInventorySlotGuid(slot);
    if (needs_repair(guid))
      return true;
  }

  for (std::uint8_t slot = 23; slot <= 38; ++slot) {
    ObjectGuid guid = GetInventorySlotGuid(slot);
    if (needs_repair(guid))
      return true;
  }

  for (std::uint8_t bag_slot = 19; bag_slot <= 22; ++bag_slot) {
    ObjectGuid bag_guid = GetInventorySlotGuid(bag_slot);
    if (bag_guid.IsEmpty())
      continue;

    const auto *bag_obj = objects->GetItem(bag_guid);
    if (!bag_obj || !bag_obj->IsContainer())
      continue;
    const auto *container = static_cast<const CGContainer_C *>(bag_obj);

    std::uint32_t num_slots = container->GetNumSlots();
    for (std::uint32_t i = 0; i < num_slots; ++i) {
      ObjectGuid item_guid = container->GetSlot(static_cast<std::uint8_t>(i));
      if (needs_repair(item_guid))
        return true;
    }
  }

  return false;
}

void CGPlayer_C::EngageTarget(WorldSession& session,
                              const ObjectGuid& target_guid,
                              const bool suppress_range_error) {

  if (!IsActivePlayer()) {
    return;
  }

  auto* const objects = object_manager();
  if (objects == nullptr) {
    return;
  }
  const auto* const target = objects->GetUnit(target_guid);
  if (target == nullptr) {
    return;
  }

  if (State().GetHealth() == 0 || State().IsStunned() || State().IsPacified()) {
    return;
  }

  if (!Interaction().CanInitiateAutoAttack(*target)) {
    return;
  }

  float range_sq = 0.0f;
  (void)Interaction().GetInteractionRangeSquared(
      session, target_guid, 5, &range_sq);

  const float dx = GetX() - target->GetX();
  const float dy = GetY() - target->GetY();
  const float dz = GetZ() - target->GetZ();
  const float dist_sq = dx * dx + dy * dy + dz * dz;

  if (dist_sq > range_sq) {

    if (!suppress_range_error) {

    }
  }

  auto pkt = net::wotlk::PacketSender::BuildAttackSwing(
      target_guid.GetRawValue());
  session.interaction().SendRawPacket(pkt);

  Interaction().SetCachedUpdateTarget(target_guid);
  Animation().ChangeSheatheStateAndNotifyServer(1, true, false);
  Animation().RefreshSelectedStandAnimation(session, 0x40u, ~0u);

  const auto attack_target_position = target->GetPosition();
  const auto attacker_position = GetPosition();
  Interaction().BeginAutoAttack(
      kAutoAttackTypeMelee, target_guid,
      {attack_target_position.x, attack_target_position.y, attack_target_position.z},
      std::atan2(attack_target_position.y - attacker_position.y,
                 attack_target_position.x - attacker_position.x));

  auto& control = objects->player_control();
  if (GetGuid().GetRawValue() == control.active_mover_guid) {
    control.movement_interaction_flags |= 2u;
  }

  CombatLog &log = session.combat_log();
  CombatEvent evt;
  evt.type = CombatEventType::kAttackStart;
  evt.source = GetGuid();
  evt.target = target_guid;
  log.AddEvent(std::move(evt));
}

std::uint64_t CGPlayer_C::s_gossip_npc_guid_ = 0;
std::uint64_t CGPlayer_C::s_talent_master_npc_guid_ = 0;
std::uint64_t CGPlayer_C::s_binder_npc_guid_ = 0;

void CGPlayer_C::SetGossipNpcGuid(std::uint64_t guid) {
  s_gossip_npc_guid_ = guid;
}

std::uint64_t CGPlayer_C::GetGossipNpcGuid() {
  return s_gossip_npc_guid_;
}

void CGPlayer_C::SetTalentMasterNpcGuid(std::uint64_t guid) {
  s_talent_master_npc_guid_ = guid;
}

std::uint64_t CGPlayer_C::GetTalentMasterNpcGuid() {
  return s_talent_master_npc_guid_;
}

void CGPlayer_C::SetBinderNpcGuid(std::uint64_t guid) {
  s_binder_npc_guid_ = guid;
}

std::uint64_t CGPlayer_C::GetBinderNpcGuid() {
  return s_binder_npc_guid_;
}

bool CGPlayer_C::CanInteractFromVehicleSeat() const {

  if (!Vehicle().HasValidVehicleUnitGuid())
    return true;

  const auto *vehicle_unit = Vehicle().GetVehicleUnit();
  if (vehicle_unit == nullptr)
    return true;

  if (vehicle_unit->Vehicle().GetVehicleData() == nullptr)
    return false;

  const auto *seat_entry = Vehicle().GetVehiclePassengerSeatEntry();
  if (seat_entry == nullptr)
    return false;

  return (seat_entry->flags & 0x80000000u) != 0u;
}

bool CGPlayer_C::IsNearGossipNpc() const {
  const auto* const objects = object_manager();
  return objects != nullptr && IsNearGossipNpc(*objects);
}

bool CGPlayer_C::IsNearGossipNpc(const ObjectManager &object_manager) const {
  return IsNearTrackedNpc(*this, object_manager, s_gossip_npc_guid_);
}

bool CGPlayer_C::IsNearTalentMasterNpc() const {
  const auto* const objects = object_manager();
  return objects != nullptr && IsNearTalentMasterNpc(*objects);
}

bool CGPlayer_C::IsNearTalentMasterNpc(const ObjectManager &object_manager) const {
  return IsNearTrackedNpc(*this, object_manager, s_talent_master_npc_guid_);
}

bool CGPlayer_C::IsNearBinderNpc() const {
  const auto* const objects = object_manager();
  return objects != nullptr && IsNearBinderNpc(*objects);
}

bool CGPlayer_C::IsNearBinderNpc(const ObjectManager &object_manager) const {
  return IsNearTrackedNpc(*this, object_manager, s_binder_npc_guid_);
}

void CGPlayer_C::InteractWithBinder(const ObjectGuid &binder_guid) {
  std::uint64_t guid_raw = binder_guid.IsEmpty() ? s_binder_npc_guid_ : binder_guid.GetRawValue();

  if (guid_raw == 0)
    return;
  auto* const objects = object_manager();
  if (objects == nullptr) {
    return;
  }
  const auto* const npc = objects->GetUnit(ObjectGuid(guid_raw));
  if (!npc)
    return;

  float dx = npc->GetX() - GetX();
  float dy = npc->GetY() - GetY();
  float dz = npc->GetZ() - GetZ();
  float distance_sq = dx * dx + dy * dy + dz * dz;
  float threshold = npc->State().GetBoundingRadius() + 4.0f;
  if (distance_sq > threshold * threshold)
    return;

  if (!binder_guid.IsEmpty()) {

    s_binder_npc_guid_ = guid_raw;

  } else {

  }
}

std::uint8_t CGPlayer_C::FindContainerSlotByGuid(const ObjectGuid &guid) const {

  constexpr std::uint8_t kFirstBagSlot = 19;
  constexpr std::uint8_t kLastBagSlot = 22;
  for (std::uint8_t i = kFirstBagSlot; i <= kLastBagSlot; ++i) {
    if (GetInventorySlotGuid(i) == guid) {
      return i;
    }
  }
  return 0xFF;
}

ObjectGuid CGPlayer_C::GetActiveControlGuid() const {
  if (!IsActivePlayer())
    return ObjectGuid();

  return GetGuidField(UNIT_FIELD_CHARM);
}

const CGUnit_C* CGPlayer_C::GetActiveControlUnit() const {
  const auto controlled_guid = GetActiveControlGuid();
  if (controlled_guid.IsEmpty()) {
    return nullptr;
  }

  const auto* const objects = object_manager();
  const auto* controlled_unit =
      objects != nullptr ? objects->GetUnit(controlled_guid) : nullptr;
  if (controlled_unit == nullptr) {
    return nullptr;
  }

  auto owner_guid = controlled_unit->State().GetCharmedBy();
  if (owner_guid.IsEmpty()) {
    owner_guid = controlled_unit->State().GetCreatedBy();
  }

  if (owner_guid != GetGuid()) {
    return nullptr;
  }

  return controlled_unit;
}

void CGPlayer_C::RefreshCharacterModelAndQueuePortraitEvents() {
  Presentation().AddCharacterVisualRefreshFlags(
      kCharacterModelFlagGeosetsDirty |
      kCharacterModelFlagForceEquipmentRefresh);

  const auto raw_guid = GetGuid().GetRawValue();
  if (raw_guid != 0) {
    auto &dispatch = ui::game::ScriptEventDispatch::Get();
    dispatch.FireUnitPortrait(raw_guid);
    dispatch.FireUnitModel(raw_guid);
  }
}

void CGPlayer_C::ProcessVisualInitGate(std::uint8_t construct_flags,
                                       std::uint32_t *out_error_count,
                                       std::uint32_t *out_pending) {

  if ((construct_flags & 0x01) == 0) {
    if (out_error_count) {
      *out_error_count = 1;
    }
    return;
  }

  constexpr std::uint32_t kEquipAppearanceDirty = 0x00400000u;
  if (State().HasSpellStateFlags(kEquipAppearanceDirty)) {

    State().ClearSpellStateFlags(kEquipAppearanceDirty);
  }

  const bool has_prior_error =
      (out_error_count && *out_error_count != 0) ||
      (out_pending && *out_pending != 0);

  if (has_prior_error) {

  } else {

  }

  const bool has_mount = Mount().DisplayId(*this) != 0;
  if (has_mount) {

    if (out_pending) {
      *out_pending = 0;
    }
  }
}

std::int32_t CGPlayer_C::GetSpellScalingLevelX5(
    const data::dbc::SpellEntry *spell) const {
  auto unit_level = static_cast<std::int32_t>(GetUInt32(UNIT_FIELD_LEVEL));
  std::int32_t result = 5 * unit_level;

  auto spell_max = static_cast<std::int32_t>(spell->max_level);
  if (spell_max > 0) {
    std::int32_t cap = 5 * spell_max;
    if (result >= cap)
      result = cap;
  }

  return result < 0 ? 0 : result;
}

std::int32_t CGPlayer_C::CalcSpellDuration(
    const WorldSession& session,
    const data::dbc::SpellEntry *spell) const {
  if (spell == nullptr) {
    return 0;
  }

  const auto* dbc = LookupActiveDbcLoader(*this);
  if (dbc == nullptr) {
    return 0;
  }

  const auto* ct = dbc->spell_cast_times().LookupEntry(spell->casting_time_index);
  if (ct == nullptr) {
    return 0;
  }

  const auto effective_level =
      static_cast<std::uint32_t>(GetSpellScalingLevelX5(spell)) / 5u;
  auto duration = ct->base_cast_time
      + ct->per_level
        * static_cast<std::int32_t>(effective_level - spell->base_level);

  if (duration < ct->minimum) {
    duration = ct->minimum;
  }

  if (IsActivePlayer()) {
    const auto* chr_class = dbc->chr_classes().LookupEntry(State().GetClass());
    const auto spell_family =
        chr_class != nullptr ? chr_class->spell_family : 0u;
    (void)session.aura().ApplySpellModifierDeltas(
        spell_family, *spell, SpellModOp::kCastingTime, &duration);
  }

  constexpr std::uint32_t kSkipHasteAttr0   = 0x30u;
  constexpr std::uint32_t kSkipHasteAttrEx3 = 0x20000000u;

  if ((spell->attributes & kSkipHasteAttr0) == 0
      && (spell->attributes_ex3 & kSkipHasteAttrEx3) == 0
      && duration > 0) {
    const float haste = State().GetSpellHaste();
    if (openwow::math::float_compare::OutsideClientEpsilon(haste, 1.0f)) {
      duration = static_cast<std::int32_t>(
          static_cast<double>(duration) * static_cast<double>(haste));
    }
  }

  constexpr std::uint32_t kAttr0UsesRangedSlot = 0x02u;
  if ((spell->attributes & kAttr0UsesRangedSlot) != 0) {
    duration += 500;
  }

  return duration > 0 ? duration : 0;
}

std::int32_t CGPlayer_C::CalcRawCastDuration(
    const data::dbc::SpellEntry *spell) const {
  if (spell == nullptr) {
    return 0;
  }

  const auto* dbc = LookupActiveDbcLoader(*this);
  if (dbc == nullptr) {
    return 0;
  }

  const auto* ct = dbc->spell_cast_times().LookupEntry(spell->casting_time_index);
  if (ct == nullptr) {
    return 0;
  }

  const auto effective_level =
      static_cast<std::uint32_t>(GetSpellScalingLevelX5(spell)) / 5u;
  auto duration = ct->base_cast_time
      + ct->per_level
        * static_cast<std::int32_t>(effective_level - spell->base_level);

  if (duration < ct->minimum) {
    duration = ct->minimum;
  }

  constexpr std::uint32_t kSkipHasteAttr0   = 0x30u;
  constexpr std::uint32_t kSkipHasteAttrEx3 = 0x20000000u;

  if ((spell->attributes & kSkipHasteAttr0) == 0
      && (spell->attributes_ex3 & kSkipHasteAttrEx3) == 0
      && duration > 0) {
    const float haste = State().GetSpellHaste();
    if (openwow::math::float_compare::OutsideClientEpsilon(haste, 1.0f)) {
      duration = static_cast<std::int32_t>(
          static_cast<double>(duration) * static_cast<double>(haste));
    }
  }

  constexpr std::uint32_t kAttr0UsesRangedSlot = 0x02u;
  if ((spell->attributes & kAttr0UsesRangedSlot) != 0) {
    duration = 0x7FFFFFFF;
  }

  return duration > 0 ? duration : 0;
}

void CGPlayer_C::GetXPRange(std::uint32_t *out_xp, std::uint32_t *out_zero) const {
  if (out_xp)
    *out_xp = 5 * GetUInt32(UNIT_FIELD_LEVEL);
  if (out_zero)
    *out_zero = 0;
}

void CGPlayer_C::GetXPRangeForLevel(std::int32_t , std::uint32_t *out_xp,
                                    std::uint32_t *out_zero) const {
  if (out_xp)
    *out_xp = 5 * GetUInt32(UNIT_FIELD_LEVEL);
  if (out_zero)
    *out_zero = 0;
}

void CGPlayer_C::PrepareForWorldRemoval() {
  CGObject_C::PrepareForWorldRemoval();
  CleanupPlayer();
}

void CGPlayer_C::DestroyPlayer(WorldSession& session) {

  const auto guid = GetGuid();
  const auto guid_raw = guid.GetRawValue();

  CGObject_C::PrepareForWorldRemoval();

  UnitVehicle_ClearVehicleData(session, *this, true);

  State().ClearSpellStateFlags(0x2u);

  Movement().Cleanup();

  auto& control = session.player_control_runtime();
  if (guid_raw == control.active_mover_guid) {

    if (Interaction().IsAutoAttacking()) {
      Interaction().CompleteAutoAttackInteraction(false,
                                                   true);
    }
    control.ClearDestroyedActiveMover(guid_raw);
  }

  CancelPendingCastsByGuid(session, guid_raw);

  Animation().ClearEmoteInternalFlags(0x200u);

  if (guid_raw == control.active_mover_guid) {
    if (Interaction().IsAutoAttacking()) {
      const auto now = openwow::core::GameClock::GetTickCount32();
      InputControl_MoveForwardStop(now);
      InputControl_ApplyControlFlagChange(0x1000000, false, now, 0);
      InputControl_ApplyControlFlagChange(0x400000, false, now, 0);
    }
  }

  Animation().ClearEmoteInternalFlags(0x8000u);

  State().ClearSpellStateFlags(0x01000000u);

  {
    const auto *ctrl = Interaction().ResolveControllingPlayer();
    if (ctrl != nullptr && ctrl->IsActivePlayer()) {
      SpellAction_CancelPeriodicClientSpell(guid, 0);
    }
  }

  {
    auto *passenger = Vehicle().GetVehiclePassengerComponent();
    if (passenger != nullptr) {
      passenger->DetachFromSeat();
      UnitVehicle_ReleasePassengerForUnit(*this);
    }
  }

  CleanupUnitResources();
}

void CGPlayer_C::CleanupPlayer() {

  CleanupUnitResources();
}

void CGPlayer_C::CleanupActivePlayerState() {

  SpellVisuals().ClearDispatches();

  SpellVisual_ResetVisibleHumanoidState();
}

void CGPlayer_C::CreateWeaponSpellVisualEffects(
    const WorldSession& session,
    const std::uint32_t spell_id,
    const data::dbc::SpellVisualKitEntry& kit,
    const float* const position,
    const std::uint64_t source_guid,
    const std::uint32_t group_param,
    std::uint32_t& dispatch_flags,
    const std::uintptr_t callback,
    bool& out_created,
    const bool has_aura_visual_flag) {
  const auto* const dbc = dbc_loader();
  if (dbc == nullptr) {
    return;
  }

  static constexpr std::uint32_t kCreatureModelBlockWeaponEffects = 0x10u;

  const auto resolve_creature_model_data =
      [this, dbc]() -> const data::dbc::CreatureModelDataEntry* {
    const auto display_id = Presentation().CreatureModelLookupDisplayId();
    const auto* display_info =
        dbc->creature_display_info().LookupEntry(display_id);
    if (display_info == nullptr) {
      return nullptr;
    }
    return dbc->creature_model_data().LookupEntry(display_info->model_id);
  };

  const auto try_create = [&](const std::uint32_t sven_id,
                              const std::uint32_t attachment_point) {
    if (sven_id == 0u) {
      return;
    }

    const auto* const sven =
        dbc->spell_visual_effect_name().LookupEntry(sven_id);
    if (sven == nullptr) {
      return;
    }

    const auto* const model_data = resolve_creature_model_data();
    if (model_data != nullptr &&
        (model_data->flags & kCreatureModelBlockWeaponEffects) != 0u) {
      return;
    }

    if (!has_aura_visual_flag) {
      Animation().ChangeSheatheStateAndNotifyServer(0, true, false);
    }

    CreateSpellVisualEffectNode(
        session,
        attachment_point,
        0u,
        spell_id,
        &kit,
        sven,
        dispatch_flags,
        callback,
        position,
        source_guid,
        group_param,
        0u);

    out_created = true;
  };

  try_create(kit.left_weapon_effect, 2u);

  try_create(kit.right_weapon_effect, 1u);
}

void CGPlayer_C::OnDestroyEffectNode(const WorldSession& session,
    const UnitSpellVisualRuntime::AttachedEffectNode& node) {

  const bool had_active_animation =
      node.spell_record_id != 0 &&
      (node.flags &
       UnitSpellVisualRuntime::AttachedEffectNode::kFlagPlayingAnimation) != 0;

  CGUnit_C::OnDestroyEffectNode(session, node);

  if (had_active_animation) {
    Animation().RefreshSelectedStandAnimation(session, 0u, ~0u);
  }
}

const data::dbc::SpellVisualEntry* CGPlayer_C::ResolveSpellVisualRecord(
    const data::dbc::SpellEntry& spell,
    data::dbc::SpellVisualEntry& out,
    std::uint32_t kit_visual_id,
    std::uint32_t kit_visual_id_fallback) const {
  const auto* dbc = LookupActiveDbcLoader(*this);
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto quality_level = static_cast<std::int32_t>(
      openwow::core::DisplaySettingsController::Instance().GetQualityLevel());

  const data::dbc::SpellVisualEntry* kit_visual = nullptr;
  const data::dbc::SpellVisualEntry* spell_visual = nullptr;

  if (kit_visual_id != 0u) {

    if (quality_level < 2 && kit_visual_id_fallback != 0u) {
      kit_visual_id = kit_visual_id_fallback;
    }
    kit_visual = dbc->spell_visual().LookupEntry(kit_visual_id);

    auto own_id = spell.spell_visual[0];
    if (quality_level < 2 && spell.spell_visual[1] != 0u) {
      own_id = spell.spell_visual[1];
    }
    spell_visual = own_id != 0u
        ? dbc->spell_visual().LookupEntry(own_id)
        : nullptr;
  } else {

    auto own_id = spell.spell_visual[0];
    if (quality_level < 2 && spell.spell_visual[1] != 0u) {
      own_id = spell.spell_visual[1];
    }
    kit_visual = own_id != 0u
        ? dbc->spell_visual().LookupEntry(own_id)
        : nullptr;

  }

  const data::dbc::SpellVisualEntry* item_visual = nullptr;

  constexpr std::uint32_t kSpellAttrUsesRangedSlot = 0x02u;
  if ((spell.attributes & kSpellAttrUsesRangedSlot) != 0u) {
    constexpr std::uint8_t kRangedWeaponSlot = 2;
    const auto display_id = GetVisibleWeaponDisplayId(kRangedWeaponSlot);
    if (display_id.has_value()) {
      const auto* item_display =
          dbc->item_display_info().LookupEntry(*display_id);
      if (item_display != nullptr && item_display->spell_visual_id != 0u) {
        item_visual =
            dbc->spell_visual().LookupEntry(item_display->spell_visual_id);
      }
    }
  }

  if (kit_visual != nullptr) {

    out = *kit_visual;

    if (spell_visual != nullptr) {
      out.precast_kit       = spell_visual->precast_kit;
      out.cast_kit          = spell_visual->cast_kit;
      out.impact_kit        = spell_visual->impact_kit;
      out.caster_impact_kit = spell_visual->caster_impact_kit;
      out.target_impact_kit = spell_visual->target_impact_kit;
    }
  } else {
    if (item_visual == nullptr) {
      return nullptr;
    }

    out = data::dbc::SpellVisualEntry{};
  }

  if (item_visual != nullptr) {
    auto merge = [](std::uint32_t& dst, std::uint32_t src) {
      if (dst == 0u) dst = src;
    };
    auto merge_float = [](float& dst, float src) {

      std::uint32_t d;
      std::memcpy(&d, &dst, sizeof(d));
      if (d == 0u) dst = src;
    };
    auto merge_int32 = [](std::int32_t& dst, std::int32_t src) {
      if (dst == 0) dst = src;
    };

    merge(out.precast_kit, item_visual->precast_kit);
    merge(out.cast_kit, item_visual->cast_kit);
    merge(out.impact_kit, item_visual->impact_kit);

    if (out.has_missile == 0u && item_visual->has_missile != 0u) {
      out.has_missile = 1u;
      out.missile_model = item_visual->missile_model;
      out.missile_path_type = item_visual->missile_path_type;
      out.missile_destination_attachment =
          item_visual->missile_destination_attachment;
      out.missile_sound_id = item_visual->missile_sound_id;
      out.anim_event_sound_id = item_visual->anim_event_sound_id;
      out.flags |= item_visual->flags;
      out.missile_attachment_id = item_visual->missile_attachment_id;
      out.missile_follow_ground_height =
          item_visual->missile_follow_ground_height;
      out.missile_follow_ground_drop_speed =
          item_visual->missile_follow_ground_drop_speed;
      out.missile_follow_ground_approach =
          item_visual->missile_follow_ground_approach;
      out.missile_follow_ground_flags =
          item_visual->missile_follow_ground_flags;
      out.missile_motion_id = item_visual->missile_motion_id;
      out.missile_targeting_kit = item_visual->missile_targeting_kit;
      out.instant_area_kit = item_visual->instant_area_kit;
      out.impact_area_kit = item_visual->impact_area_kit;
      out.missile_cast_offset_x = item_visual->missile_cast_offset_x;
      out.missile_cast_offset_y = item_visual->missile_cast_offset_y;
      out.missile_cast_offset_z = item_visual->missile_cast_offset_z;
      out.missile_impact_offset_x = item_visual->missile_impact_offset_x;
      out.missile_impact_offset_y = item_visual->missile_impact_offset_y;
      out.missile_impact_offset_z = item_visual->missile_impact_offset_z;
    }

    merge_int32(out.missile_destination_attachment,
                item_visual->missile_destination_attachment);
    merge(out.missile_sound_id, item_visual->missile_sound_id);
    merge(out.anim_event_sound_id, item_visual->anim_event_sound_id);
    merge(out.caster_impact_kit, item_visual->caster_impact_kit);
    merge(out.target_impact_kit, item_visual->target_impact_kit);
    merge_int32(out.missile_attachment_id, item_visual->missile_attachment_id);
    merge(out.missile_motion_id, item_visual->missile_motion_id);
    merge_float(out.missile_impact_offset_x,
                item_visual->missile_impact_offset_x);
    merge_float(out.missile_impact_offset_y,
                item_visual->missile_impact_offset_y);
    merge_float(out.missile_impact_offset_z,
                item_visual->missile_impact_offset_z);
  }

  return &out;
}

void CGPlayer_C::PlayerUpdateTick(float elapsed) {

  (void)elapsed;

  UpdateModelTintColor(core::GameClock::GetTickCount32());
}

float CGPlayer_C::GetPlayerAnimationProgress() const {
  if (g_player_animation_progress_callback == nullptr) {
    return 0.0f;
  }

  return g_player_animation_progress_callback(*this, g_player_animation_progress_context);
}

void CGPlayer_C::PlayArmorFoleySoundForOthers() {
  if (!ui::game::CVarSystem::Instance().GetCVarBool(
          "Sound_EnableArmorFoleySoundForOthers")) {
    return;
  }

  const auto* dbc = LookupActiveDbcLoader(*this);
  if (dbc == nullptr) {
    return;
  }

  const std::uint32_t display_id =
      Presentation().CreatureModelLookupDisplayId();
  const auto* display_entry = dbc->creature_display_info().LookupEntry(display_id);
  if (display_entry == nullptr) {
    return;
  }

  const auto* model_data = dbc->creature_model_data().LookupEntry(display_entry->model_id);
  if (model_data == nullptr || model_data->foley_material_id == 0) {
    return;
  }

  const auto position = GetPosition();
  const float pos[3] = {position.x, position.y, position.z};

  audio::PlayMaterialFoleySound(sound_runtime(), model_data->foley_material_id, pos, false);
}

void CGPlayer_C::ResetMatchingSpellVisualNodes(
    const WorldSession& session, const std::uint32_t spell_id,
    const std::uint32_t visual_kit_param) {

  SpellVisuals().ResetMatchingNodes(session, spell_id, visual_kit_param);

  if (pending_aura_visual_entries_.empty()) {
    return;
  }

  const auto* const dbc = LookupActiveDbcLoader(*this);
  if (dbc == nullptr) {
    return;
  }

  const data::dbc::SpellEntry* spell_record = nullptr;
  bool spell_lookup_attempted = false;

  for (auto& entry : pending_aura_visual_entries_) {
    if ((entry.flags & PendingAuraVisualEntry::kFlagPendingInit) == 0u ||
        entry.status != 0u ||
        entry.spell_id != spell_id ||
        entry.visual_kit_param != visual_kit_param) {
      continue;
    }

    if (!spell_lookup_attempted) {
      spell_record = dbc->spell().LookupEntry(spell_id);
      spell_lookup_attempted = true;
    }

    if (spell_record != nullptr && entry.visual_kit_id != 0u) {

      const std::array<float, 3>* const position =
          (entry.flags & PendingAuraVisualEntry::kFlagHasPosition) != 0u
              ? &entry.world_position
              : nullptr;
      (void)SpellVisuals().CreateFromKit(
          session, entry.visual_kit_id, entry.dispatch_type, position);
    }

    entry.flags &= ~PendingAuraVisualEntry::kFlagPendingInit;
  }
}

void CGPlayer_C::SetIdleAnimation() {
  if (GetActiveOverlayModelIndex() == 0) {
    return;
  }

  if (HasRenderObjectSleepFlag() || Presentation().HasNameplateFrame()) {
    overlay_animation_id_ = 190;
  } else {
    overlay_animation_id_ = 0;
  }
}

void CGPlayer_C::ReapplyAllAuraSpellVisuals(const WorldSession& session) {
  const auto* dbc = LookupActiveDbcLoader(*this);
  if (dbc == nullptr) {
    return;
  }

  const auto& auras = Auras().All();
  const auto count = static_cast<int>(auras.size());
  if (count == 0) {
    return;
  }

  for (int i = count - 1; i >= 0; --i) {
    const auto& aura = auras[static_cast<std::size_t>(i)];
    if (aura.spell_id == 0) {
      continue;
    }

    const auto* spell = dbc->spell().LookupEntry(aura.spell_id);
    if (spell == nullptr) {
      continue;
    }

    const auto visual_id = spell->spell_visual[0];
    if (visual_id != 0) {
      const auto* visual = dbc->spell_visual().LookupEntry(visual_id);
      if (visual != nullptr && visual->persistent_area_kit != 0) {

        constexpr std::uint32_t kDispatchTypeAura = 0u;
        (void)SpellVisuals().CreateFromKit(
            session, visual->persistent_area_kit, kDispatchTypeAura);
      }
    }

  }
}

}
