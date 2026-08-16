#include "openwow/game/objects/cgunit.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/display_info_resolver.h"
#include "openwow/game/ceffect_c.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/missile_trajectory.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/game/spell_visual_attachment.h"
#include "openwow/game/localization.h"
#include "openwow/game/title_system.h"
#include "openwow/game/unit_tooltip_info.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/world_session.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/models/animation/m2_attachment_transform.h"
#include "openwow/render/models/animation/model_instance_transform.h"
#include "openwow/render/scene/world_frame.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace openwow::game {

render::RenderMatrix4x4 BuildUnitM2WorldTransform(const CGUnit_C &unit) {
  render::RenderMatrix4x4 matrix{};
  if (unit.GetVisualModelWorldTransform(matrix.data())) {
    return matrix;
  }
  const auto position = unit.GetPosition();

  return render::BuildM2ModelInstanceTransform(
      position.x, position.y, position.z,
      unit.Movement().SmoothBodyFacing(), unit.GetScale());
}

CalcGroundPosCallback g_calc_ground_pos_callback = nullptr;
void *g_calc_ground_pos_context = nullptr;
UnitCollisionAabbCallback g_unit_collision_aabb_callback = nullptr;
void *g_unit_collision_aabb_context = nullptr;

namespace {

constexpr float kNpcCameraTargetHeightCap = 2.027777671813965f;
constexpr float kCameraTargetFloorOffset = 0.1666666716337204f;
constexpr std::uint32_t kDefaultSpellAttachmentIndex = 38;
constexpr std::uint32_t kCreatureModelDataCharacterComponentFlag = 0x4u;
constexpr std::uint32_t kSpellStateCharacterVisualActive = 0x00020000u;
constexpr std::uint32_t kSpellStateSuppressCharacterVisualRefresh = 0x00400000u;
constexpr std::uint32_t kSpellStateCharacterVisualReleaseMask =
    kSpellStateCharacterVisualActive | kSpellStateSuppressCharacterVisualRefresh;
constexpr std::uint32_t kModelAnimSequencesReadyBit = 0x40000u;
constexpr std::uint32_t kModelIdleAnimLatchBit = 0x200000u;
constexpr float kGroundContactInterpolationMinZ = 0.35721236f;
constexpr float kGroundContactInterpolationDecayBase = 0.001799999969080091f;
constexpr float kRetailFloatEpsilon = 0.00000095367432f;
constexpr float kMinGroundDistance = 0.0013888889f;
constexpr float kWalkableNormalZ_Player = 0.64278764f;
constexpr float kWalkableNormalZ_NPC = 0.17364818f;
constexpr float kDefaultCollisionWidth = 0.66666669f;
constexpr float kDefaultCollisionHeight = 2.027777671813965f;

void InterpolateRetailGroundContactNormal(
    const CGUnit_C& unit, std::array<float, 3>& smoothed_normal,
    const float dt) {
  if (g_calc_ground_pos_callback == nullptr) {
    return;
  }

  const auto position = unit.GetPosition();
  const CalcGroundPosCollisionResult surface = g_calc_ground_pos_callback(
      unit, {position.x, position.y, position.z},
      unit.Presentation().CollisionHeight() * 2.0f,
      g_calc_ground_pos_context);
  if (!surface.hit || surface.normal_z < kGroundContactInterpolationMinZ) {
    return;
  }

  const std::array<float, 3> target{
      surface.normal_x, surface.normal_y, surface.normal_z};
  const float decay = std::pow(kGroundContactInterpolationDecayBase, dt);
  for (std::size_t component = 0; component < smoothed_normal.size();
       ++component) {
    smoothed_normal[component] =
        (smoothed_normal[component] - target[component]) * decay +
        target[component];
  }
}

[[nodiscard]] render::RenderMatrix4x4 BuildGroundAlignedUnitMatrix(
    const CGUnit_C& unit, const std::array<float, 3>& surface_normal) {
  const Position position = unit.GetPosition();
  const std::array<float, 3> world_position{
      position.x, position.y, position.z};

  return render::BuildM2AttachmentTransformMatrix(
      render::RenderVec3View{world_position},
      unit.Movement().SmoothBodyFacing(), unit.GetScale(),
      render::RenderVec3View{surface_normal},
      static_cast<render::AttachmentOrientationMode>(
          unit.Movement().GroundOrientationMode() & 0x3u));
}

[[nodiscard]] bool PrimaryM2HasAttachment(
    const CGUnit_C& unit,
    const std::uint32_t attachment_lookup_index) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0u) {
    return false;
  }

  auto* const m2_system = unit.m2_system();
  if (m2_system == nullptr) {
    return false;
  }

  const auto query = m2_system->QueryAttachmentInfo(
      instance_id, attachment_lookup_index);
  return query.status == render::m2::M2ResultStatus::kReady;
}

[[nodiscard]] bool QueryPrimaryM2AttachmentPosition(
    const CGUnit_C& unit,
    const std::uint32_t attachment_lookup_index,
    float* const out_position,
    const std::optional<render::RenderVec3> &local_offset = std::nullopt) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0u || out_position == nullptr) {
    return false;
  }

  auto* const m2_system = unit.m2_system();
  if (m2_system == nullptr) {
    return false;
  }

  const auto query = m2_system->QueryAttachmentPosition(
      instance_id, attachment_lookup_index, local_offset);
  if (query.status != render::m2::M2ResultStatus::kReady) {
    return false;
  }

  out_position[0] = query.position[0];
  out_position[1] = query.position[1];
  out_position[2] = query.position[2];
  return true;
}

[[nodiscard]] bool RenderVec3ValuesAreFinite(const render::RenderVec3 &values) {
  return std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]);
}

[[nodiscard]] bool Float3ValuesAreFinite(const float* const values) {
  return values == nullptr ||
         (std::isfinite(values[0]) &&
          std::isfinite(values[1]) &&
          std::isfinite(values[2]));
}

[[nodiscard]] std::optional<render::RenderVec3> ToOptionalRenderVec3(const float *const values) {
  if (values == nullptr) {
    return std::nullopt;
  }
  return render::RenderVec3{values[0], values[1], values[2]};
}

std::uint32_t BlendPackedChannel(const std::uint32_t destination,
                                 const std::uint32_t source,
                                 const std::uint32_t factor) {
  return (destination + (((source - destination) * factor) >> 8u)) & 0xFFu;
}

void BlendPackedRgb(std::uint32_t &destination, const std::uint32_t factor,
                    const std::uint32_t source) {
  if (factor >= 255u) {
    destination = (destination & 0xFF000000u) | (source & 0x00FFFFFFu);
    return;
  }
  const auto blue = BlendPackedChannel(destination & 0xFFu, source & 0xFFu,
                                       factor);
  const auto green = BlendPackedChannel((destination >> 8u) & 0xFFu,
                                        (source >> 8u) & 0xFFu, factor);
  const auto red = BlendPackedChannel((destination >> 16u) & 0xFFu,
                                      (source >> 16u) & 0xFFu, factor);
  destination = (destination & 0xFF000000u) | (red << 16u) |
                (green << 8u) | blue;
}

bool SetAttachmentSelectorFlag(
    render::m2::M2System &m2_system,
    UnitAttachmentVisualSelectorNode *const node,
    const std::uint16_t selector_id, const bool enabled) {
  if (node == nullptr) {
    return false;
  }
  bool updated = false;
  if (node->selector_id != 0u && node->selector_id == selector_id &&
      node->model_instance_id != 0u) {
    updated = m2_system.SetAttachedModelVisualSelectorFlag(
                  node->model_instance_id, enabled) ==
              render::m2::M2ResultStatus::kReady;
  }
  for (auto *const child : node->children) {
    updated = SetAttachmentSelectorFlag(m2_system, child, selector_id,
                                        enabled) ||
              updated;
  }
  return updated;
}

}

UnitTooltipInfo CGUnit_C::BuildTooltipInfo(const WorldSession &session) const {
  UnitTooltipInfo info;

  info.flags = UnitTooltipFlag::kAlive;
  if (IsPlayer()) {
    info.flags |= 0x08;
  }

  if ((State().GetPvPFlags() & 0x01) != 0) {
    info.flags |= UnitTooltipFlag::kPvP;
  }
  if ((State().GetPvPFlags() & 0x04) != 0) {
    info.flags |= UnitTooltipFlag::kTapped;
  }
  if (static_cast<std::int32_t>(State().GetHealth()) <= 0) {
    info.flags |= UnitTooltipFlag::kDead;
  }
  if ((State().GetUnitFlags() & 0x00000040) != 0) {
    info.flags |= UnitTooltipFlag::kAttackable;
  }
  if (GetGuid().IsPlayer() || GetGuid().IsVehicle()) {
    info.flags |= UnitTooltipFlag::kPlayerOrVehicleGuid;
  }

  info.health = static_cast<std::int32_t>(State().GetHealth());
  info.max_health = static_cast<std::int32_t>(State().GetMaxHealth());

  const std::uint8_t power_type = State().GetPowerType();
  info.power_type_display = power_type;
  if (power_type == 0xfeu) {
    info.power = static_cast<std::uint16_t>(State().GetHealth());
    info.max_power = static_cast<std::uint16_t>(State().GetMaxHealth());
  } else {
    info.power = static_cast<std::uint16_t>(State().GetPower(power_type));
    info.max_power = static_cast<std::uint16_t>(State().GetMaxPower(power_type));
  }

  info.level = static_cast<std::uint16_t>(State().GetLevel());
  if (GetTransportGUID().IsEmpty()) {
    if (session.has_current_map()) {
      info.map_id = static_cast<std::uint16_t>(session.current_map_id());
    }
    info.position_x = static_cast<std::int16_t>(GetX());
    info.position_y = static_cast<std::int16_t>(GetY());
  }

  std::uint32_t aura_count = 0;
  for (const auto &aura : aura_.All()) {
    if (aura_count >= UnitTooltipInfo::kMaxTooltipAuras)
      break;
    if (aura.spell_id != 0 && (aura.flags & 0x07) != 0) {
      info.aura_spell_ids[aura_count] = aura.spell_id;
      info.aura_flags[aura_count] = static_cast<std::uint8_t>(aura.flags);
      ++aura_count;
    }
  }

  info.target_guid = State().GetTarget();
  if ((info.flags & UnitTooltipFlag::kPlayerOrVehicleGuid) != 0u) {
    info.vehicle_seat_spell_id = ResolveUnitVehicleSeatRecordId(session, *this);
  }
  return info;
}

std::string CGUnit_C::ResolveRetailName(
    const WorldSession &session,
    std::string* const out_realm,
    const bool follow_name_override_aura) const {
  if (out_realm != nullptr) {
    out_realm->clear();
  }

  const auto* const dbc = dbc_loader();
  const auto* const objects = object_manager();
  if (follow_name_override_aura && dbc != nullptr) {
    for (const auto& aura : aura_.All()) {
      const auto* const spell = dbc->spell().LookupEntry(aura.spell_id);
      if (spell == nullptr) {
        continue;
      }

      for (std::size_t effect_index = 0;
           effect_index < spell->effect_apply_aura.size(); ++effect_index) {
        const std::uint32_t effect_bit = 1u << effect_index;
        if (spell->effect_apply_aura[effect_index] != 0x117u ||
            (aura.flags & effect_bit) == 0u) {
          continue;
        }

        if (const auto* const caster =
                objects != nullptr ? objects->GetUnit(aura.caster_guid) : nullptr;
            caster != nullptr) {
          return caster->ResolveRetailName(session, out_realm, false);
        }
        if (objects != nullptr) {
          if (const auto* const cached = objects->query_cache().GetPlayerName(
                  aura.caster_guid.GetRawValue());
              cached != nullptr && !cached->name.empty()) {
            if (out_realm != nullptr) {
              *out_realm = cached->realm_name;
            }
            return cached->name;
          }
        }
        break;
      }
    }
  }

  if (IsPlayer() || GetGuid().IsPlayer()) {
    if (objects != nullptr && GetGuid() == objects->GetActivePlayerGuid()) {
      const std::string local_name = GetName();
      if (!local_name.empty()) {
        return local_name;
      }
      const auto& identity = session.pending_character_identity();
      if (!identity.name.empty()) {
        return identity.name;
      }
    }
    if (objects != nullptr) {
      if (const auto* const cached =
              objects->query_cache().GetPlayerName(GetGuid().GetRawValue());
          cached != nullptr && !cached->name.empty()) {
        if (out_realm != nullptr) {
          *out_realm = cached->realm_name;
        }
        return cached->name;
      }
    }
  }

  const std::string object_name = GetName();
  if (!object_name.empty()) {
    return object_name;
  }
  if (objects != nullptr && GetGuid().HasEntry()) {
    if (const auto* const creature =
            objects->query_cache().GetCreatureTemplate(GetGuid().GetEntry());
        creature != nullptr && !creature->name.empty()) {
      return creature->name;
    }
  }

  lua_State* const lua =
      openwow::ui::game::ScriptEventDispatch::Get().GetLuaState();
  if (lua != nullptr) {
    if (std::string unknown =
            ResolveLocalizedGlobalString(lua, "UNKNOWNOBJECT", -1, 0);
        !unknown.empty()) {
      return unknown;
    }
  }
  if (std::string unknown =
          Localization::Get().GetString("UNKNOWNOBJECT", std::string{});
      !unknown.empty()) {
    return unknown;
  }
  return "Unknown Being";
}

std::uint32_t CGUnit_C::FormatNameWithPvpTitle(
    const WorldSession &session, const bool include_title, std::string &out) const {
  const std::string unit_name = ResolveRetailName(session);
  if (!IsPlayer() || !include_title) {
    out = unit_name;
    return 1;
  }

  const auto *player = static_cast<const CGPlayer_C *>(this);
  const std::uint32_t chosen_title_mask = player->GetChosenTitle();
  if (chosen_title_mask == 0) {
    out = unit_name;
    return 1;
  }

  const auto &titles = TitleSystem::Get();
  const auto entry = titles.GetTitleEntryByMaskIndex(chosen_title_mask);
  if (!entry.has_value()) {
    out = unit_name;
    return 1;
  }

  const bool is_female = (State().GetGender() == 1);
  const std::string *format_str = nullptr;
  if (is_female) {
    format_str = entry->name_female.empty() ? &entry->name_male
                                            : &entry->name_female;
  } else {
    format_str = entry->name_male.empty() ? &entry->name_female
                                          : &entry->name_male;
  }
  if (format_str == nullptr || format_str->empty()) {
    out = unit_name;
    return 1;
  }

  auto pos = format_str->find("%s");
  if (pos != std::string::npos) {
    out.clear();
    out.reserve(format_str->size() + unit_name.size());
    out.append(*format_str, 0, pos);
    out.append(unit_name);
    out.append(*format_str, pos + 2);
  } else {
    out = *format_str;
  }

  const std::uint8_t medal_rank = player->GetPvpMedalRank();
  if (medal_rank != 0) {
    char medal_key[32];
    std::snprintf(medal_key, sizeof(medal_key), "PVP_MEDAL%u",
                  static_cast<unsigned>(medal_rank));
    out += '\n';
    lua_State *L =
        openwow::ui::game::ScriptEventDispatch::Get().GetLuaState();
    if (L != nullptr) {
      std::string medal_text =
          ResolveLocalizedGlobalString(L, medal_key, -1, 0);
      if (!medal_text.empty()) {
        out += medal_text;
      } else {
        out += medal_key;
      }
    } else {
      out += medal_key;
    }
    return 2;
  }
  return 1;
}

std::uint32_t CGUnit_C::BuildTooltipNameText(const WorldSession &session,
                                             const std::uint32_t flags,
                                             std::string &out,
                                             const bool check_vehicle_auras) const {
  if (check_vehicle_auras) {
    const auto* const dbc = dbc_loader();
    const auto* const objects = object_manager();
    if (dbc != nullptr && objects != nullptr) {
      for (std::size_t i = 0; i < Auras().Count(); ++i) {
        const auto *aura = Auras().At(i);
        if (aura == nullptr || aura->spell_id == 0)
          continue;
        const auto* const spell = dbc->spell().LookupEntry(aura->spell_id);
        if (spell == nullptr) {
          continue;
        }
        bool redirects_name = false;
        for (std::size_t effect_index = 0;
             effect_index < spell->effect_apply_aura.size(); ++effect_index) {
          if (spell->effect_apply_aura[effect_index] == 0x117u &&
              (aura->flags & (1u << effect_index)) != 0u) {
            redirects_name = true;
            break;
          }
        }
        if (!redirects_name) {
          continue;
        }
        const auto *caster = objects->GetUnit(aura->caster_guid);
        if (caster != nullptr && caster->IsPlayer()) {
          return caster->BuildTooltipNameText(session, flags, out,
                                              false);
        }
      }
    }
  }

  std::uint32_t extra_lines = 0;
  out.clear();
  const bool include_pvp_title = (flags & 0x08) != 0;
  std::string name_text;
  const std::uint32_t name_lines =
      FormatNameWithPvpTitle(session, include_pvp_title, name_text);
  out += name_text;
  if (name_lines > 1) {
    extra_lines += (name_lines - 1);
  }

  if (IsPlayer() && (flags & 0x04) != 0) {
    const auto *player = static_cast<const CGPlayer_C *>(this);
    const std::uint32_t guild_id = player->GetGuildID();
    if (guild_id != 0) {
      const auto *guild_info = session.guild().FindCachedGuildInfo(guild_id);
      if (guild_info != nullptr && !guild_info->name.empty()) {
        out += "\n<";
        out += guild_info->name;
        out += '>';
        ++extra_lines;
      }
    }
  }

  std::string server_label;
  if (IsPlayer()) {
    std::string realm_name;
    (void)ResolveRetailName(session, &realm_name, true);
    if (!realm_name.empty() &&
        GetGuid() != CGObject_C::GetActivePlayerGuid()) {
      lua_State* const lua =
          openwow::ui::game::ScriptEventDispatch::Get().GetLuaState();
      if (lua != nullptr) {
        server_label = ResolveLocalizedGlobalString(
            lua, "FOREIGN_SERVER_LABEL", -1, 0);
      }
      if (server_label.empty()) {
        server_label = Localization::Get().GetString(
            "FOREIGN_SERVER_LABEL", std::string{});
      }
      if (server_label.size() > 7u) {
        server_label.resize(7u);
      }
    }
  }

  if (!IsPlayer()) {
    const char *sub_name = State().GetCreatureSubnameForDisplay();
    if (sub_name != nullptr && *sub_name != '\0') {
      out += "\n<";
      out += sub_name;
      out += '>';
      ++extra_lines;
    }
    const auto *dbc_loader = this->dbc_loader();
    const std::string summon_title =
        openwow::ui::game::BuildSummonTitleText(*this, dbc_loader, object_manager());
    if (!summon_title.empty()) {
      out += "\n<";
      out += summon_title;
      out += '>';
      ++extra_lines;
    }
  }
  if (!server_label.empty()) {
    out += server_label;
  }
  return extra_lines;
}

std::uint32_t CGUnit_C::GetDisplayId() const {
  return Presentation().DisplayId();
}

std::uint32_t UnitPresentationRuntime::DisplayId() const {
  return owner_.GetUInt32(UNIT_FIELD_DISPLAYID);
}

std::uint32_t UnitPresentationRuntime::NativeDisplayId() const {
  return owner_.GetUInt32(UNIT_FIELD_NATIVEDISPLAYID);
}

const char *CGUnit_C::GetPortraitTextureName() const {
  return Presentation().PortraitTextureName();
}

float CGUnit_C::GetModelOpacity() const {
  return Presentation().ModelOpacity();
}

bool CGUnit_C::ShouldFadeOnShow() const {
  return Presentation().ShouldFadeOnShow();
}

void UnitPresentationRuntime::OnDisplayIdChanged() {
  if (on_display_changed_) {
    on_display_changed_(NativeDisplayId(), CurrentDisplayId());
  }
}

bool UnitPresentationRuntime::IsTransformed() const {
  return DisplayId() != NativeDisplayId();
}

std::uint32_t UnitPresentationRuntime::CurrentDisplayId() const {
  const std::uint32_t mount = owner_.Mount().CachedDisplayForSpell();
  if (mount != 0u) {
    return mount;
  }
  return VisibleBodyDisplayId();
}

std::uint32_t UnitPresentationRuntime::VisibleBodyDisplayId() const {
  if (owner_.SpellVisuals().VisibleHumanoidDisplayId() != 0u &&
      DisplayId() == NativeDisplayId()) {
    return owner_.SpellVisuals().VisibleHumanoidDisplayId();
  }
  return DisplayId();
}

std::uint32_t UnitPresentationRuntime::CreatureModelLookupDisplayId() const {

  const auto current = VisibleBodyDisplayId();

  return cached_native_display_id_ != 0u && NativeDisplayId() == current
             ? cached_native_display_id_
             : current;
}

std::uint32_t UnitPresentationRuntime::DisplayGender() const {
  return cached_display_info_extra_rec_ != nullptr
             ? cached_display_info_extra_rec_->display_sex_id
             : owner_.State().GetGender();
}

std::uint32_t UnitPresentationRuntime::DisplayRace() const {
  return cached_display_info_extra_rec_ != nullptr
             ? cached_display_info_extra_rec_->display_race_id
             : owner_.State().GetRace();
}

const DisplayInfoEntry *UnitPresentationRuntime::ResolveCreatureModelDisplayInfo() const {
  const auto *const entry =
      DisplayInfoResolver::Get().GetDisplayInfo(CreatureModelLookupDisplayId());
  return entry != nullptr &&
                 (entry->model_id != 0u || !entry->model_path.empty())
             ? entry
             : nullptr;
}

UnitPresentationRuntime::CreatureDisplayRows
UnitPresentationRuntime::ResolveCreatureDisplayRowsFor(
    const std::uint32_t display_id) const {
  const auto *const dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    return {};
  }
  const auto *const display = dbc->creature_display_info().LookupEntry(display_id);
  if (display == nullptr) {
    return {};
  }
  return {.display = display,
          .model = dbc->creature_model_data().LookupEntry(display->model_id)};
}

UnitPresentationRuntime::CreatureDisplayRows
UnitPresentationRuntime::ResolveCreatureDisplayRows() const {
  return ResolveCreatureDisplayRowsFor(CreatureModelLookupDisplayId());
}

float UnitPresentationRuntime::AttachedEffectModelScale() const {
  const auto rows = ResolveCreatureDisplayRows();
  if (rows.model == nullptr) {
    return 1.0f;
  }
  const float x_span = rows.model->geo_box_max[0] - rows.model->geo_box_min[0];
  const float y_span = rows.model->geo_box_max[1] - rows.model->geo_box_min[1];
  const float geometry_scale = std::min(x_span, y_span) * 0.30000001f;
  const float model_scale = rows.model->scale > 0.0f ? rows.model->scale : 1.0f;
  return std::max(geometry_scale, 1.0f) * model_scale;
}

float UnitPresentationRuntime::ModelHeight() const {
  const auto rows = ResolveCreatureDisplayRows();
  return rows.model != nullptr
             ? owner_.GetScale() *
                   (rows.model->geo_box_max[2] - rows.model->geo_box_min[2])
             : 0.0f;
}

float UnitPresentationRuntime::CameraTargetHeight() const {
  const float height =
      owner_.IsPlayer() ? collision_height_
                 : std::min(collision_height_, kNpcCameraTargetHeightCap);
  return height - kCameraTargetFloorOffset;
}

void UnitPresentationRuntime::UpdateCameraTargetAndMissilePreview(
    UnitMissileTrajectory_C &missile_trajectory, CGUnit_C *unit) {
  const data::dbc::VehicleEntry* vehicle_entry = nullptr;

  if (unit != nullptr) {

    vehicle_entry = unit->Vehicle().GetVehicleEntry();

    ui::game::ScriptEventDispatch::Get().FirePlayerTargetChanged();
  }

  constexpr std::uint32_t kMissilePreviewFlagMask = 0x0F800000u;

  if (vehicle_entry != nullptr &&
      (vehicle_entry->flags & kMissilePreviewFlagMask) != 0) {

    MissileTrajectoryPreviewResourceConfig config;
    config.spell_visual_flags = vehicle_entry->flags;
    config.source_unit_guid = unit->GetGuid().GetRawValue();
    config.current_time_ms = core::GameClock::GetTickCount32();
    config.ribbon_texture_path = vehicle_entry->mssl_trgt_arc_texture;
    config.endpoint_texture_path = vehicle_entry->mssl_trgt_impact_texture;
    config.model_paths[0] = vehicle_entry->mssl_trgt_impact_model[0];
    config.model_paths[1] = vehicle_entry->mssl_trgt_impact_model[1];

    if (auto* const m2 = unit->m2_system(); m2 != nullptr) {
      missile_trajectory.LoadPreviewResources(config, *m2);
    }

    UnitVehicle_UpdateSeatUI(unit, vehicle_entry);
  } else {
    missile_trajectory.ClearPreviewResources();

    UnitVehicle_UpdateSeatUI(unit, nullptr);
  }
}

float UnitPresentationRuntime::ModelScale() const {
  return owner_.GetFloat(OBJECT_FIELD_SCALE_X);
}

float UnitPresentationRuntime::ModelOpacity() const {

  const auto rows = ResolveCreatureDisplayRows();
  return rows.display != nullptr
             ? static_cast<float>(rows.display->model_alpha) * (1.0f / 255.0f)
             : 1.0f;
}

float UnitPresentationRuntime::ResolveDisplayNativeScale() const {

  const auto *const dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    return 1.0f;
  }
  constexpr float kUnresolvedDisplayScale = 1.0f;
  const auto *const display =
      dbc->creature_display_info().LookupEntry(DisplayId());
  const auto *const model =
      display != nullptr ? dbc->creature_model_data().LookupEntry(display->model_id)
                         : nullptr;
  if (display == nullptr || model == nullptr) {
    return kUnresolvedDisplayScale;
  }
  const CreatureFamilySizeCurve *curve = nullptr;
  CreatureFamilySizeCurve resolved_curve{};
  if (const auto *const objects = owner_.object_manager();
      objects != nullptr && owner_.GetGuid().HasEntry()) {
    if (const auto *const creature =
            objects->query_cache().GetCreatureTemplate(owner_.GetGuid().GetEntry());
        creature != nullptr && creature->creature_family != 0u) {
      if (const auto *const family =
              dbc->creature_family().LookupEntry(creature->creature_family);
          family != nullptr) {
        resolved_curve = {.min_scale = family->min_scale,
                          .min_scale_level = family->min_scale_level,
                          .max_scale = family->max_scale,
                          .max_scale_level = family->max_scale_level};
        curve = &resolved_curve;
      }
    }
  }
  return CombineDisplayNativeScale(
      display->scale, model->scale, curve,
      static_cast<std::int32_t>(owner_.State().GetLevel()),
      owner_.GetUInt32(static_cast<std::uint16_t>(UNIT_FIELD_PETNUMBER)) != 0u);
}

float UnitPresentationRuntime::CombineDisplayNativeScale(
    const float display_scale, const float model_scale,
    const CreatureFamilySizeCurve *const family, const std::int32_t level,
    const bool is_numbered_pet) noexcept {

  float scale = display_scale * model_scale;
  if (!(scale > 0.0f)) {
    scale = 1.0f;
  }
  if (family == nullptr) {
    return scale;
  }

  const auto min_level = static_cast<std::int32_t>(family->min_scale_level);
  const std::int32_t level_span =
      static_cast<std::int32_t>(family->max_scale_level) - min_level;
  std::int32_t levels_above_min = level > min_level ? level - min_level : 0;
  if (level_span < levels_above_min) {
    levels_above_min = level_span;
  }
  const float family_scale =
      level_span == 0
          ? family->min_scale
          : (family->max_scale - family->min_scale) *
                    (static_cast<float>(levels_above_min) /
                     static_cast<float>(level_span)) +
                family->min_scale;

  return (family_scale > scale || is_numbered_pet) ? family_scale : scale;
}

void UnitPresentationRuntime::RefreshDisplayInfoScale(const bool reset_ratio) {

  static_cast<void>(reset_ratio);
  const float native_scale = ResolveDisplayNativeScale();
  owner_.SetDisplayScale(native_scale, nullptr);
  ComputeModelBoundingBox();
  for (auto *node = *owner_.GetEffectNodeListHeadSlot(); node != nullptr;) {
    auto *const next = node->GetNextAttachedEffect();
    if ((node->GetFlags() & CEffectFlags::kScaleFromOwner) != 0u) {
      node->RefreshOwnerScale();
    }
    node = next;
  }
}

void UnitPresentationRuntime::NotifyNameplateLevelChanged() {

}

bool UnitPresentationRuntime::ShouldFadeOnShow() const {
  constexpr std::uint32_t kInhibitUnitFlags = 0x2u;
  constexpr std::uint32_t kInhibitUnitFlags2 = 0x20u;
  constexpr std::uint8_t kInhibitVisibilityFlags = 0x2u;
  if ((owner_.State().GetUnitFlags() & kInhibitUnitFlags) != 0u ||
      (owner_.State().GetUnitFlags2() & kInhibitUnitFlags2) != 0u ||
      (owner_.State().GetVisFlags() & kInhibitVisibilityFlags) != 0u) {
    return false;
  }
  const auto transport_guid = owner_.GetTransportGUID();
  if (transport_guid.IsEmpty()) {
    return true;
  }
  const auto *const objects = owner_.object_manager();
  const auto *const vehicle =
      objects != nullptr ? objects->GetUnit(transport_guid) : nullptr;
  if (vehicle == nullptr) {
    return false;
  }
  const auto *const seat = owner_.Vehicle().GetVehiclePassengerSeatEntry();
  return seat != nullptr && seat->attachment_id < 0
             ? true
             : !vehicle->IsOpacityFading();
}

float UnitPresentationRuntime::AnimScaleFromDbc(
    const data::dbc::CreatureDisplayInfoEntry *const display) const {
  if (display == nullptr || display->extra_info == 0u ||
      owner_.dbc_loader() == nullptr) {
    return 1.0f;
  }
  const auto &dbc = *owner_.dbc_loader();
  const auto *const extra =
      dbc.creature_display_info_extra().LookupEntry(display->extra_info);
  if (extra == nullptr) {
    return 1.0f;
  }
  const auto *const race = dbc.chr_races().LookupEntry(extra->display_race_id);
  if (race == nullptr) {
    return 1.0f;
  }
  const auto race_display_id = extra->display_sex_id == 0u
                                   ? race->model_male
                               : extra->display_sex_id == 1u
                                   ? race->model_female
                                   : 0u;
  const auto *const race_display =
      race_display_id != 0u
          ? dbc.creature_display_info().LookupEntry(race_display_id)
          : nullptr;
  return race_display != nullptr ? race_display->scale : 1.0f;
}

float UnitPresentationRuntime::ScaledModelHeight(
    const data::dbc::CreatureDisplayInfoEntry *const display,
    const data::dbc::CreatureModelDataEntry *const model,
    float *const out_raw_scale) const {
  if (display == nullptr || model == nullptr) {
    if (out_raw_scale != nullptr) {
      *out_raw_scale = 1.0f;
    }
    return 1.0f;
  }
  float raw_scale = AnimScaleFromDbc(display) * display->scale * model->scale;
  if (raw_scale <= 0.0f) {
    raw_scale = 1.0f;
  }
  if (out_raw_scale != nullptr) {
    *out_raw_scale = raw_scale;
  }

  return raw_scale * std::max(1.0f, ModelScale());
}

void UnitMovementRuntime::InterpolateShadowBlobPosition(float dt) {
  InterpolateRetailGroundContactNormal(owner_, ground_contact_normal_, dt);

  const Position position = owner_.GetPosition();
  const std::array<float, 3> world_position{position.x, position.y,
                                            position.z};
  const float body_facing = SmoothBodyFacing();
  const float scale = owner_.GetScale();
  const std::uint8_t orientation_mode =
      static_cast<std::uint8_t>(ground_orientation_mode_ & 0x3u);
  auto &memo = ground_aligned_matrix_memo_;
  const bool memo_matches =
      memo.valid &&
      std::memcmp(memo.position.data(), world_position.data(),
                  sizeof(world_position)) == 0 &&
      std::memcmp(&memo.body_facing, &body_facing, sizeof(body_facing)) == 0 &&
      std::memcmp(&memo.scale, &scale, sizeof(scale)) == 0 &&
      std::memcmp(memo.normal.data(), ground_contact_normal_.data(),
                  sizeof(ground_contact_normal_)) == 0 &&
      memo.orientation_mode == orientation_mode;
  if (!memo_matches) {
    memo.valid = true;
    memo.position = world_position;
    memo.body_facing = body_facing;
    memo.scale = scale;
    memo.normal = ground_contact_normal_;
    memo.orientation_mode = orientation_mode;
    memo.matrix = BuildGroundAlignedUnitMatrix(owner_, ground_contact_normal_);
  }
  owner_.SetVisualModelWorldTransform(memo.matrix.data());
}

void UnitPresentationRuntime::UpdateMountTransitionNodeTransform() {
  if (!owner_.Mount().TransitionHandle().IsValid() ||
      owner_.Mount().TransitionNode() == nullptr)
    return;

  if (owner_.Mount().TransitionNode()->GetPrimaryModelInstanceId() == 0)
    return;

  float mto_position[3]{};
  float mto_rotation[3]{};
  float mto_facing = 0.0f;
  MountTransitionObject_GetTransformData(
      owner_.Mount().TransitionHandle(), mto_position, mto_rotation, &mto_facing);
  const render::RenderVec3 attachment_position{
      mto_position[0], mto_position[1], mto_position[2]};
  const render::RenderVec3 attachment_up{mto_rotation[0], mto_rotation[1], mto_rotation[2]};

  const render::RenderMatrix4x4 parent_matrix = BuildUnitM2WorldTransform(owner_);

  const float scale = owner_.GetScale();
  render::RenderMatrix4x4 inverse_matrix{};
  if (std::fabs(scale - 1.0f) < kRetailFloatEpsilon) {
    inverse_matrix[0]  = parent_matrix[0];
    inverse_matrix[1]  = parent_matrix[4];
    inverse_matrix[2]  = parent_matrix[8];
    inverse_matrix[3]  = 0.0f;
    inverse_matrix[4]  = parent_matrix[1];
    inverse_matrix[5]  = parent_matrix[5];
    inverse_matrix[6]  = parent_matrix[9];
    inverse_matrix[7]  = 0.0f;
    inverse_matrix[8]  = parent_matrix[2];
    inverse_matrix[9]  = parent_matrix[6];
    inverse_matrix[10] = parent_matrix[10];
    inverse_matrix[11] = 0.0f;
  } else {
    const float inv_s2 = 1.0f / (scale * scale);
    inverse_matrix[0]  = parent_matrix[0] * inv_s2;
    inverse_matrix[1]  = parent_matrix[4] * inv_s2;
    inverse_matrix[2]  = parent_matrix[8] * inv_s2;
    inverse_matrix[3]  = 0.0f;
    inverse_matrix[4]  = parent_matrix[1] * inv_s2;
    inverse_matrix[5]  = parent_matrix[5] * inv_s2;
    inverse_matrix[6]  = parent_matrix[9] * inv_s2;
    inverse_matrix[7]  = 0.0f;
    inverse_matrix[8]  = parent_matrix[2] * inv_s2;
    inverse_matrix[9]  = parent_matrix[6] * inv_s2;
    inverse_matrix[10] = parent_matrix[10] * inv_s2;
    inverse_matrix[11] = 0.0f;
  }
  const float tx = -parent_matrix[12];
  const float ty = -parent_matrix[13];
  const float tz = -parent_matrix[14];
  inverse_matrix[12] = inverse_matrix[0] * tx + inverse_matrix[4] * ty +
                         inverse_matrix[8] * tz;
  inverse_matrix[13] = inverse_matrix[1] * tx + inverse_matrix[5] * ty +
                         inverse_matrix[9] * tz;
  inverse_matrix[14] = inverse_matrix[2] * tx + inverse_matrix[6] * ty +
                         inverse_matrix[10] * tz;
  inverse_matrix[15] = 1.0f;

  const render::RenderMatrix4x4 missile_matrix = render::BuildM2AttachmentTransformMatrix(
      render::RenderVec3View{attachment_position}, mto_facing, scale,
      render::RenderVec3View{attachment_up},
      render::AttachmentOrientationMode::kNone);

  const auto result = openwow::render::MultiplyMatrix4x4(missile_matrix, inverse_matrix);

  (void)owner_.Mount().TransitionNode()->SetPrimaryModelWorldTransform(result);
  owner_.Mount().TransitionNode()->SetPrimaryModelAlpha(
      MountTransitionObject_GetScale(owner_.Mount().TransitionHandle()) *
      owner_.GetEffectiveRenderOpacity());
}

void UnitMovementRuntime::BlendMountTransitionPosition(
    float dt, const std::uint32_t current_tick_ms) {
  const float blend_factor =
      MountTransitionObject_GetBlendFactor(owner_.Mount().TransitionHandle());
  if (blend_factor == 0.0f) {
    owner_.CGObject_C::UpdateModelNodeTransform(dt, current_tick_ms);
    return;
  }

  InterpolateRetailGroundContactNormal(owner_, ground_contact_normal_, dt);

  if (owner_.Mount().TransitionNode() == nullptr)
    return;

  render::RenderMatrix4x4 matrix =
      BuildGroundAlignedUnitMatrix(owner_, ground_contact_normal_);

  float target_offset[3]{};
  auto* const objects = owner_.object_manager();
  if (objects == nullptr) {
    return;
  }
  MountTransitionObject_GetTargetPosition(*objects,
                                          owner_.Mount().TransitionHandle(),
                                          target_offset);

  matrix[12] += target_offset[0];
  matrix[13] += target_offset[1];
  matrix[14] += target_offset[2];

  owner_.SetVisualModelWorldTransform(matrix.data());

  owner_.Mount().SetPendingTransitionSpellId(
      owner_.Mount().TransitionNode()->GetSpellId());
}

bool UnitPresentationRuntime::CalcGroundPos(UnitGroundPositionResult &result) const {
  result = {};

  const float collision_height = CollisionHeight();
  const float search_dist = collision_height + collision_height;
  result.distance = search_dist;

  const auto pos = owner_.GetPosition();
  const auto collision = QueryGroundSurface(
      {pos.x, pos.y, pos.z}, search_dist);

  if (!collision.hit) {
    result.distance = 0.0f;
    return false;
  }

  float ground_distance = pos.z - collision.ground_z;

  if (collision.normal_z < kMinGroundDistance) {
    result.missed = true;
  }

  if (collision_height > ground_distance) {
    const float walkable_threshold =
        owner_.IsPlayer() ? kWalkableNormalZ_Player : kWalkableNormalZ_NPC;
    if (collision.normal_z <= walkable_threshold) {
      ground_distance = collision_height;
    }
  }

  if (ground_distance < kMinGroundDistance) {
    ground_distance = 0.0f;
  }

  result.distance = ground_distance;
  result.normal_x = collision.normal_x;
  result.normal_y = collision.normal_y;
  return true;
}

CalcGroundPosCollisionResult UnitPresentationRuntime::QueryGroundSurface(
    const std::array<float, 3> &origin, const float max_distance) const {
  if (g_calc_ground_pos_callback == nullptr || max_distance <= 0.0f) {
    return {};
  }
  return g_calc_ground_pos_callback(
      owner_, origin, max_distance, g_calc_ground_pos_context);
}

bool UnitPresentationRuntime::InitMountedCollisionBounds(const float mount_height, const bool forced) {
  const auto *dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    return true;
  }

  const std::uint32_t display_id = CurrentDisplayId();
  const auto *display_info = dbc->creature_display_info().LookupEntry(display_id);
  if (display_info == nullptr) {
    return true;
  }

  const auto *model_data = dbc->creature_model_data().LookupEntry(display_info->model_id);
  if (model_data == nullptr) {
    return true;
  }

  float raw_scale = 1.0f;
  const float effective_scale = ScaledModelHeight(display_info, model_data, &raw_scale);

  const float model_collision_height = model_data->collision_height;
  float mounted_height =
      owner_.Mount().DisplayScale() * mount_height + model_collision_height * 0.5f;

  if (mounted_height < model_collision_height) {
    mounted_height = model_collision_height;
  }

  if (!forced && owner_.IsActivePlayer()) {
    const auto &move_data = owner_.Movement().Data();
    const float existing_half_width = move_data.GetCollisionHalfWidth();
    const float existing_height_product = move_data.GetCollisionHeightProduct();

    const auto pos = owner_.GetPosition();
    const float aabb[6] = {
        pos.x - existing_half_width,
        pos.y - existing_half_width,
        pos.z + existing_height_product,
        pos.x + existing_half_width,
        pos.y + existing_half_width,
        pos.z + mounted_height * effective_scale,
    };
    if (g_unit_collision_aabb_callback != nullptr &&
        g_unit_collision_aabb_callback(aabb, g_unit_collision_aabb_context)) {
      return false;
    }
  }

  auto &move_data = owner_.Movement().Data();
  const float width_param = move_data.GetCollisionHalfWidth() + move_data.GetCollisionHalfWidth();
  move_data.InitCollisionBounds(width_param, mounted_height, effective_scale,
                                raw_scale, forced,
                                owner_.Movement().IsNavigableAsPlayer());
  return true;
}

void UnitPresentationRuntime::HandleScaleChange(const float new_scale) {
  const bool growing = (CollisionHeight() <= new_scale);

  const std::uint32_t mount_display_id = owner_.Mount().DisplayId(owner_);
  bool collision_ok = false;

  if (mount_display_id > 0u && owner_.IsPlayer()) {
    const auto *dbc = owner_.dbc_loader();
    if (dbc != nullptr) {
      const auto *cdi = dbc->creature_display_info().LookupEntry(mount_display_id);
      if (cdi != nullptr) {
        const auto *cmd = dbc->creature_model_data().LookupEntry(cdi->model_id);
        if (cmd != nullptr) {
          collision_ok = InitMountedCollisionBounds(cmd->mount_height, growing);
        }
      }
    }
  } else {
    collision_ok = InitDisplayCollisionBounds(growing, false);
  }

  if (!collision_ok) {
    (void)net::ClientServices__SendPacket(
        net::wotlk::WorldPacket(net::wotlk::Opcode::CMSG_CANCEL_GROWTH_AURA));
  }

  owner_.RefreshOverlayBoneScale();
  ComputeModelBoundingBox();
  RefreshObjectItemEffectTransforms();
}

bool UnitPresentationRuntime::InitDisplayCollisionBounds(const bool growing, const bool forced) {
  if (owner_.IsPlayer()) {
    return InitPlayerDisplayCollisionBounds(growing, forced);
  }

  const auto *dbc = owner_.dbc_loader();

  const std::uint32_t display_id = DisplayId();
  if (dbc != nullptr && display_id != 0u) {
    const auto *cdi = dbc->creature_display_info().LookupEntry(display_id);
    if (cdi != nullptr) {
      const auto *cmd = dbc->creature_model_data().LookupEntry(cdi->model_id);
      if (cmd != nullptr) {
        float raw_scale = 1.0f;
        (void)ScaledModelHeight(cdi, cmd, &raw_scale);
        const float collision_height_val = CollisionHeight();
        const float height_factor = std::max(1.0f, collision_height_val);
        const float effective_scale = raw_scale * height_factor;

        auto &move_data = owner_.Movement().Data();
        move_data.InitCollisionBounds(cmd->collision_width, cmd->collision_height,
                                      effective_scale, raw_scale, forced,
                                      owner_.Movement().IsNavigableAsPlayer());
        return true;
      }
    }
  }

  auto &move_data = owner_.Movement().Data();
  move_data.InitCollisionBounds(kDefaultCollisionWidth, kDefaultCollisionHeight,
                                1.0f, 1.0f, forced,
                                owner_.Movement().IsNavigableAsPlayer());
  return true;
}

bool UnitPresentationRuntime::InitPlayerDisplayCollisionBounds(const bool growing, const bool forced) {
  const auto *dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    return true;
  }

  const std::uint32_t display_id = DisplayId();
  const auto *cdi = dbc->creature_display_info().LookupEntry(display_id);
  if (cdi == nullptr) {
    return true;
  }

  const auto *cmd = dbc->creature_model_data().LookupEntry(cdi->model_id);
  if (cmd == nullptr) {
    return true;
  }

  float raw_scale = 1.0f;
  const float effective_scale = ScaledModelHeight(cdi, cmd, &raw_scale);

  if (std::fabs(cmd->collision_width) < kRetailFloatEpsilon ||
      std::fabs(cmd->collision_height) < kRetailFloatEpsilon) {
    return true;
  }

  if (effective_scale == 0.0f) {
    return true;
  }

  if (!growing && owner_.IsActivePlayer()) {
    const auto position = owner_.GetPosition();
    const float half_width = cmd->collision_width * effective_scale * 0.5f;
    const float height = cmd->collision_height * effective_scale;
    const float aabb[6] = {
        position.x - half_width,
        position.y - half_width,
        position.z,
        position.x + half_width,
        position.y + half_width,
        position.z + height,
    };
    if (g_unit_collision_aabb_callback != nullptr &&
        g_unit_collision_aabb_callback(aabb, g_unit_collision_aabb_context)) {
      return false;
    }
  }

  auto &move_data = owner_.Movement().Data();
  move_data.InitCollisionBounds(cmd->collision_width, cmd->collision_height,
                                effective_scale, raw_scale, forced,
                                owner_.Movement().IsNavigableAsPlayer());
  return true;
}

void SetCalcGroundPosCallback(CalcGroundPosCallback callback, void *context) {
  g_calc_ground_pos_callback = callback;
  g_calc_ground_pos_context = context;
}

void ClearCalcGroundPosCallback() {
  g_calc_ground_pos_callback = nullptr;
  g_calc_ground_pos_context = nullptr;
}

void SetUnitCollisionAabbCallback(UnitCollisionAabbCallback callback,
                                  void *context) {
  g_unit_collision_aabb_callback = callback;
  g_unit_collision_aabb_context = context;
}

void ClearUnitCollisionAabbCallback() {
  g_unit_collision_aabb_callback = nullptr;
  g_unit_collision_aabb_context = nullptr;
}

void UnitPresentationRuntime::RefreshActiveDisplayRuntimeState() {

  const auto rows = ResolveCreatureDisplayRowsFor(CurrentDisplayId());
  owner_.Sound().SetActiveCreatureSoundDataId(
      rows.display == nullptr
          ? 0u
          : rows.display->npc_sound_id != 0u
                ? rows.display->npc_sound_id
                : (rows.model != nullptr ? rows.model->sound_id : 0u));
  collision_height_ = rows.model != nullptr ? rows.model->collision_height : 0.0f;

  footprint_.SetTextureId(0xFFFFFFFFu);
  footprint_.SetWidth(UnitFootprintComponent::kDefaultScale);
  footprint_.SetLength(UnitFootprintComponent::kDefaultScale);
  footprint_.SetParticleScale(0.0f);
  footprint_.SetMountedTextureId(0u);
  footprint_.SetMountedWidth(0.0f);
  footprint_.SetMountedLength(0.0f);
  active_creature_model_flags_ = 0u;
  active_footstep_shake_size_ = 0u;
  has_active_creature_model_data_ = false;

  const auto *const dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    owner_.Sound().RefreshAmbientLoopSound(owner_);
    return;
  }
  const auto resolve_model = [dbc](const std::uint32_t display_id) {
    const auto *const display =
        dbc->creature_display_info().LookupEntry(display_id);
    return display != nullptr
               ? dbc->creature_model_data().LookupEntry(display->model_id)
               : nullptr;
  };
  auto unmounted_display_id = DisplayId();
  if (owner_.SpellVisuals().VisibleHumanoidDisplayId() != 0u &&
      DisplayId() == NativeDisplayId()) {
    unmounted_display_id = owner_.SpellVisuals().VisibleHumanoidDisplayId();
  } else if (cached_native_display_id_ != 0u &&
             NativeDisplayId() == unmounted_display_id) {
    unmounted_display_id = cached_native_display_id_;
  }
  constexpr float kFootprintModelUnitScale = 1.0f / 36.0f;
  const auto *const unmounted_model = resolve_model(unmounted_display_id);
  if (unmounted_model != nullptr) {
    footprint_.SetTextureId(unmounted_model->footprint_texture_id);
    footprint_.SetWidth(unmounted_model->footprint_texture_width *
                        kFootprintModelUnitScale);
    footprint_.SetLength(unmounted_model->footprint_texture_length *
                         kFootprintModelUnitScale);
    footprint_.SetParticleScale(unmounted_model->footprint_particle_scale);
  }
  const auto *active_model = unmounted_model;
  if (static_cast<std::int32_t>(owner_.Mount().CachedDisplayForSpell()) > 0) {
    active_model = resolve_model(owner_.Mount().CachedDisplayForSpell());
    if (active_model != nullptr) {
      footprint_.SetMountedTextureId(active_model->footprint_texture_id);
      footprint_.SetMountedWidth(active_model->footprint_texture_width *
                                 kFootprintModelUnitScale);
      footprint_.SetMountedLength(active_model->footprint_texture_length *
                                  kFootprintModelUnitScale);
    }
  }
  if (active_model != nullptr) {
    active_creature_model_flags_ = active_model->flags;
    active_footstep_shake_size_ = active_model->footstep_shake_size;
    has_active_creature_model_data_ = true;
  }
  owner_.Sound().RefreshAmbientLoopSound(owner_);
}

void UnitPresentationRuntime::ResetRuntimeState() {
  owner_.ClearObjectBoundingBox();
  footprint_.SetTextureId(0xFFFFFFFFu);
  footprint_.SetTerrainTypeId(0xFFFFFFFFu);
  footprint_.SetWidth(UnitFootprintComponent::kDefaultScale);
  footprint_.SetLength(UnitFootprintComponent::kDefaultScale);
  footprint_.SetParticleScale(0.0f);
  footprint_.SetMountedTextureId(0u);
  footprint_.SetMountedWidth(0.0f);
  footprint_.SetMountedLength(0.0f);
  active_creature_model_flags_ = 0u;
  active_footstep_shake_size_ = 0u;
  has_active_creature_model_data_ = false;
}

const char *UnitPresentationRuntime::PortraitTextureName() const {
  const auto *const dbc = owner_.dbc_loader();
  const auto *const display =
      dbc != nullptr ? dbc->creature_display_info().LookupEntry(
                           CreatureModelLookupDisplayId())
                     : nullptr;
  return display != nullptr ? display->portrait_texture_name.data() : nullptr;
}

std::uint32_t UnitPresentationRuntime::SizeClass() const {
  const auto *const dbc = owner_.dbc_loader();
  const auto *const display =
      dbc != nullptr ? dbc->creature_display_info().LookupEntry(
                           CreatureModelLookupDisplayId())
                     : nullptr;
  if (display == nullptr) {
    return 0u;
  }
  if (display->size_class != -1) {
    return static_cast<std::uint32_t>(display->size_class);
  }
  const auto *const model =
      dbc->creature_model_data().LookupEntry(display->model_id);
  return model != nullptr ? static_cast<std::uint32_t>(model->size_class) : 0u;
}

const UnitBodyEquipmentData *UnitPresentationRuntime::BodyArmorEquipmentData() const {
  return &cached_body_equipment_;
}

void UnitPresentationRuntime::SetBodyArmorEquipmentData(const UnitBodyEquipmentData &data) {
  cached_body_equipment_ = data;
}

void UnitPresentationRuntime::SetDisplayChangeCallback(
    std::function<void(std::uint32_t, std::uint32_t)> callback) {
  on_display_changed_ = std::move(callback);
}

void UnitPresentationRuntime::SetCharacterModelVisualHandleForTest(
    const CharacterModelVisualHandle value) {
  if (!value.IsValid()) {
    character_model_visual_state_.Clear();
    return;
  }
  character_model_visual_state_.handle = value;
}

CharacterModelVisualHandle
UnitPresentationRuntime::PendingCharSelectVisualHandleForTest() {
  return pending_char_select_visual_state_.handle;
}

void UnitPresentationRuntime::SetPendingCharSelectVisualHandleForTest(
    const CharacterModelVisualHandle value) {
  if (!value.IsValid()) {
    pending_char_select_visual_state_.Clear();
    return;
  }
  pending_char_select_visual_state_.handle = value;
}

std::uint32_t UnitPresentationRuntime::PendingCharSelectDisplayId() {
  return pending_char_select_display_id_;
}

void UnitPresentationRuntime::SetPendingCharSelectDisplayId(
    const std::uint32_t display_id) {
  pending_char_select_display_id_ = display_id;
}

std::optional<render::RenderVec3> UnitPresentationRuntime::ProjectPositionToScreen() const {
  const auto movement = owner_.GetMovementInfo();
  const render::RenderVec3 position{movement.x, movement.y,
                                    movement.z + 0.66666669f};
  const auto *const viewport = owner_.world_frame();
  if (viewport == nullptr) {
    return std::nullopt;
  }
  const auto projected =
      viewport->WorldToScreen(render::RenderVec3View{position});
  return projected.on_screen
             ? std::optional<render::RenderVec3>{projected.position}
             : std::nullopt;
}

void UnitPresentationRuntime::SetAttachmentVisualSelectorRoots(
    std::vector<UnitAttachmentVisualSelectorNode *> roots) {
  attachment_visual_selector_roots_.swap(roots);
}

bool UnitPresentationRuntime::SetAttachedModelVisualSelectorFlagForSelector(
    const std::uint16_t selector_id, const bool enabled) {
  auto *const system = owner_.m2_system();
  if (selector_id == 0u || system == nullptr) {
    return false;
  }
  bool updated = false;
  for (auto *const root : attachment_visual_selector_roots_) {
    updated = SetAttachmentSelectorFlag(*system, root, selector_id, enabled) ||
              updated;
  }
  return updated;
}

void UnitPresentationRuntime::SetTransientEquipmentDisplayOverride(
    const EquipmentSlot slot, const std::uint32_t display_id) {
  const auto index = static_cast<std::size_t>(slot);
  if (index >= transient_equipment_display_overrides_.size()) {
    return;
  }
  if (display_id == 0u) {
    ClearTransientEquipmentDisplayOverride(slot);
    return;
  }
  transient_equipment_display_overrides_[index] = display_id;
  transient_equipment_display_override_mask_ |= 1u << index;
}

void UnitPresentationRuntime::ClearTransientEquipmentDisplayOverride(
    const EquipmentSlot slot) {
  const auto index = static_cast<std::size_t>(slot);
  if (index >= transient_equipment_display_overrides_.size()) {
    return;
  }
  transient_equipment_display_overrides_[index] = 0u;
  transient_equipment_display_override_mask_ &= ~(1u << index);
}

std::optional<std::uint32_t>
UnitPresentationRuntime::TransientEquipmentDisplayOverride(const EquipmentSlot slot) const {
  const auto index = static_cast<std::size_t>(slot);
  return index < transient_equipment_display_overrides_.size() &&
                 (transient_equipment_display_override_mask_ & (1u << index)) !=
                     0u
             ? std::optional<std::uint32_t>{
                   transient_equipment_display_overrides_[index]}
             : std::nullopt;
}

void UnitPresentationRuntime::MarkCharacterVisualReleased() {
  owner_.State().ReplaceSpellStateFlags(
      kSpellStateCharacterVisualReleaseMask,
      kSpellStateSuppressCharacterVisualRefresh);
}

void UnitPresentationRuntime::ReleaseCharacterModelVisualHandle() {
  if (character_model_visual_state_.HasHandle()) {
    character_model_visual_state_.Clear();
    MarkCharacterVisualReleased();
  }
}

void UnitPresentationRuntime::TryReuseCharSelectModel() {
  const auto pending = pending_char_select_visual_state_;
  if (!pending.HasHandle()) {
    return;
  }
  const auto display_id = DisplayId();
  const auto *const dbc = owner_.dbc_loader();
  const auto *const display =
      dbc != nullptr ? dbc->creature_display_info().LookupEntry(display_id)
                     : nullptr;
  const auto *const model =
      display != nullptr
          ? dbc->creature_model_data().LookupEntry(display->model_id)
          : nullptr;
  if (model != nullptr &&
      (model->flags & kCreatureModelDataCharacterComponentFlag) != 0u &&
      pending_char_select_display_id_ == display_id) {
    character_model_visual_state_ = pending;
  }
  pending_char_select_visual_state_.Clear();
}

bool UnitPresentationRuntime::EnsureModelReady() const {
  return !owner_.State().HasSpellStateFlags(
      kSpellStateSuppressCharacterVisualRefresh);
}

void UnitPresentationRuntime::PreserveOrReleaseCharacterVisualOnCleanup() {
  if (owner_.IsActivePlayer()) {
    pending_char_select_visual_state_ = character_model_visual_state_;
    pending_char_select_display_id_ = DisplayId();
  } else if (character_model_visual_state_.HasHandle()) {
    character_model_visual_state_.Clear();
    MarkCharacterVisualReleased();
  }
}

void UnitPresentationRuntime::SetSpellVisualModelInstances(
    const std::uint32_t primary_instance_id,
    const std::uint32_t secondary_instance_id) noexcept {
  primary_spell_visual_model_instance_id_ = primary_instance_id;
  secondary_spell_visual_model_instance_id_ = secondary_instance_id;
}

std::uint32_t
UnitPresentationRuntime::PrimarySpellVisualModelInstanceId() const noexcept {
  return primary_spell_visual_model_instance_id_;
}

std::uint32_t
UnitPresentationRuntime::SecondarySpellVisualModelInstanceId() const noexcept {
  return secondary_spell_visual_model_instance_id_;
}

std::uint8_t UnitPresentationRuntime::UnitAlphaByte() const noexcept {
  return owner_.GetOpacityMaster();
}

void UnitPresentationRuntime::SetUnitAlpha(const float alpha) {

  owner_.SetOpacityMaster(
      static_cast<std::uint8_t>(std::lrintf(alpha * 255.0f) & 0xFF));
  if (auto *const vehicle_data = owner_.Vehicle().GetVehicleData(); vehicle_data != nullptr) {
    vehicle::Vehicle_C_ForEachPassengerUnit(
        vehicle_data, [alpha](CGUnit_C &child) {
          child.Presentation().SetUnitAlpha(alpha);
        });
  }
}

void UnitPresentationRuntime::StartBodyColorFade(
    const std::uint32_t start_time, const std::uint32_t target_color,
    const std::uint32_t delay, const std::uint32_t duration) noexcept {
  body_color_fade_start_time_ = start_time;
  body_color_fade_target_color_ = target_color;
  body_color_fade_delay_ = delay;
  body_color_fade_duration_ = duration;
}

bool UnitPresentationRuntime::TryGetInterpolatedBodyColor(
    const std::uint32_t now_ms, std::uint32_t &out_color) {
  if (body_color_fade_start_time_ == 0u) {
    return false;
  }
  const std::uint32_t fade_start =
      body_color_fade_start_time_ + body_color_fade_delay_;
  const std::int32_t elapsed = static_cast<std::int32_t>(now_ms - fade_start);
  if (elapsed < 0) {
    out_color = body_color_fade_target_color_;
    return true;
  }
  if (elapsed >= static_cast<std::int32_t>(body_color_fade_duration_)) {
    body_color_fade_start_time_ = 0u;
    return false;
  }
  const double progress =
      1.0 - static_cast<double>(elapsed) /
                static_cast<double>(body_color_fade_duration_);
  const auto factor = static_cast<std::uint32_t>(
      static_cast<std::int64_t>(progress * 255.0));
  if (factor != 0u) {
    BlendPackedRgb(out_color, factor, body_color_fade_target_color_);
  }
  return true;
}

void UnitPresentationRuntime::UpdateIdleAnimationLatch() {
  if ((model_misc_flags_ & kModelIdleAnimLatchBit) != 0u &&
      owner_.GetPrimaryM2InstanceId() != 0u) {
    model_misc_flags_ &= ~kModelIdleAnimLatchBit;
  }
}

bool UnitPresentationRuntime::ModelAnimationsReady() const noexcept {
  return (model_misc_flags_ & kModelAnimSequencesReadyBit) != 0u;
}

void UnitPresentationRuntime::UpdateEffectAttachments() {
  for (auto *node = *owner_.GetEffectNodeListHeadSlot(); node != nullptr;) {
    auto *const next = node->GetNextAttachedEffect();
    if ((node->GetFlags() & CEffectFlags::kObjectItemVisual) != 0u) {
      node->RefreshObjectItemVisualForOwnerState();
    }
    node = next;
  }
}

void UnitPresentationRuntime::RefreshObjectItemEffectTransforms() {
  const auto instance_id = owner_.GetPrimaryM2InstanceId();

  if (instance_id == 0u ||
      owner_.m2_system()->QueryInstanceModel(instance_id).status !=
          render::m2::M2ResultStatus::kReady) {
    return;
  }
  for (auto *node = *owner_.GetEffectNodeListHeadSlot(); node != nullptr;) {
    auto *const next = node->GetNextAttachedEffect();
    if ((node->GetFlags() & CEffectFlags::kObjectItemVisual) != 0u) {
      node->RefreshOwnerAttachmentTransform();
    }
    node = next;
  }
}

bool UnitPresentationRuntime::GetSpellVisualAttachmentPosition(
    float *out_position,
    const data::dbc::SpellVisualEntry &visual) const {
  if (out_position == nullptr) {
    return false;
  }

  const std::int32_t attachment_id = visual.missile_attachment_id;
  const float offset_x = visual.missile_cast_offset_x;
  const float offset_y = visual.missile_cast_offset_y;
  const float offset_z = visual.missile_cast_offset_z;

  const render::RenderVec3 converted_offset{offset_x, -offset_y, offset_z};
  if (!RenderVec3ValuesAreFinite(converted_offset)) {
    return false;
  }

  if (attachment_id == -1) {
    if (offset_x == 0.0f && offset_y == 0.0f && offset_z == 0.0f) {
      return false;
    }

    return QueryPrimaryM2AttachmentPosition(
        owner_, kDefaultSpellAttachmentIndex, out_position,
        std::optional<render::RenderVec3>{converted_offset});
  }

  const bool use_raw_index = (visual.flags & 0x200u) != 0;
  if (!HasModelAttachmentPoint(static_cast<std::uint32_t>(attachment_id),
                               use_raw_index)) {
    return false;
  }

  return GetMappedAttachmentPosition(out_position,
                                     static_cast<std::uint32_t>(attachment_id),
                                     converted_offset, use_raw_index);
}

bool UnitPresentationRuntime::HasModelAttachmentPoint(
    const std::uint32_t attachment_index, const bool use_raw_index) const {
  return PrimaryM2HasAttachment(
      owner_, ResolveSpellAttachmentLookupIndex(attachment_index, use_raw_index));
}

bool UnitPresentationRuntime::GetMappedAttachmentPosition(
    float *out_position, std::uint32_t attachment_index,
    const render::RenderVec3 &offset, bool use_raw_index) const {
  if (out_position == nullptr || !RenderVec3ValuesAreFinite(offset)) {
    return false;
  }

  return QueryPrimaryM2AttachmentPosition(
      owner_, ResolveSpellAttachmentLookupIndex(attachment_index, use_raw_index),
      out_position, std::optional<render::RenderVec3>{offset});
}

void UnitPresentationRuntime::GetBoneAttachmentWorldPosition(
    float *out_position, std::uint32_t lookup_index,
    const float *offset) const {
  if (out_position == nullptr) {
    return;
  }

  if (QueryPrimaryM2AttachmentPosition(owner_, lookup_index, out_position,
                                       ToOptionalRenderVec3(offset))) {
    return;
  }

  static constexpr std::array<float, 50> kFallbackAttachmentHeight = {
      1.0f, 1.0f, 1.0f, 1.5f, 1.5f, 1.8f, 1.8f, 0.5f, 0.5f, 1.0f,
      1.0f, 2.0f, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.5f, 0.0f,
      2.0f, 1.5f, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 3.0f,
      1.5f, 1.5f, 1.0f, 1.0f, 1.5f, 1.0f, 1.0f, 2.5f, 1.5f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
  };

  render::RenderVec3 local_point{};
  const bool has_finite_offset = offset != nullptr && Float3ValuesAreFinite(offset);
  if (has_finite_offset) {
    local_point = {offset[0], offset[1], offset[2]};
  }
  if (lookup_index < kFallbackAttachmentHeight.size()) {
    local_point[2] += kFallbackAttachmentHeight[lookup_index];
  }

  const bool has_authored_offset =
      has_finite_offset &&
      (offset[0] != 0.0f || offset[1] != 0.0f || offset[2] != 0.0f);
  const auto model_matrix = BuildUnitM2WorldTransform(owner_);
  if (has_authored_offset) {
    const auto world_point = render::TransformAffinePoint4x4(
        render::RenderVec3View{local_point},
        render::RenderMatrix4x4View{model_matrix});
    std::copy(world_point.begin(), world_point.end(), out_position);
    return;
  }

  out_position[0] = model_matrix[12] + local_point[0];
  out_position[1] = model_matrix[13] + local_point[1];
  out_position[2] = model_matrix[14] + local_point[2];
}

void UnitPresentationRuntime::RefreshModelBoundsAndEffects() {
  ComputeModelBoundingBox();
  RefreshObjectItemEffectTransforms();
  owner_.RefreshOverlayBoneScale();
  if (void *const vehicle_data = owner_.Vehicle().GetVehicleData();
      vehicle_data != nullptr && vehicle::Vehicle_C_HasDbcEntry(vehicle_data)) {
    vehicle::Vehicle_C_UpdateBoundingRadius(vehicle_data);
  }
}

void UnitPresentationRuntime::RefreshModelBoundsAndEffectsForced() {
  const auto instance_id = owner_.GetPrimaryM2InstanceId();
  if (instance_id != 0u) {
    (void)owner_.m2_system()->SetWorldTransformMatrix(
        instance_id, BuildUnitM2WorldTransform(owner_));
  }

  RefreshModelBoundsAndEffects();
}

float *UnitPresentationRuntime::ModelToWorldMatrix(float *const out_matrix) const {
  const auto instance_id = owner_.GetPrimaryM2InstanceId();
  if (instance_id != 0u && out_matrix != nullptr && owner_.m2_system() != nullptr) {
    const auto query = owner_.m2_system()->QueryRootBoneWorldMatrix(instance_id);
    if (query.status == render::m2::M2ResultStatus::kReady) {
      std::copy(query.matrix.begin(), query.matrix.end(), out_matrix);
      return out_matrix;
    }
  }
  owner_.GetWorldMatrix(out_matrix);
  return out_matrix;
}

void UnitMovementRuntime::BuildStaticBodyMatrix(float *out_matrix) const {
  if (!out_matrix)
    return;

  constexpr float kTwoPi = 6.2831855f;

  const float world_facing = WorldSmoothBodyFacing();

  const float ya = -(kTwoPi - world_facing);
  const float pa = kTwoPi - owner_.GetMovementInfo().pitch;

  const float cy = std::cos(ya), sy = std::sin(ya);
  const float cp = std::cos(pa), sp = std::sin(pa);

  const float r00 = cy * cp;
  const float r01 = -sy;
  const float r02 = cy * sp;
  const float r10 = sy * cp;
  const float r11 = cy;
  const float r12 = sy * sp;
  const float r20 = -sp;
  const float r21 = 0.0f;
  const float r22 = cp;

  out_matrix[0]  = r00;  out_matrix[1]  = r10;  out_matrix[2]  = r20;  out_matrix[3]  = 0.0f;
  out_matrix[4]  = r01;  out_matrix[5]  = r11;  out_matrix[6]  = r21;  out_matrix[7]  = 0.0f;
  out_matrix[8]  = r02;  out_matrix[9]  = r12;  out_matrix[10] = r22;  out_matrix[11] = 0.0f;
  out_matrix[12] = 0.0f; out_matrix[13] = 0.0f; out_matrix[14] = 0.0f; out_matrix[15] = 1.0f;

  const float scale = owner_.GetScale();
  for (int col = 0; col < 3; ++col) {
    out_matrix[col]     *= scale;
    out_matrix[4 + col] *= scale;
    out_matrix[8 + col] *= scale;
  }

  out_matrix[12] = owner_.GetX();
  out_matrix[13] = owner_.GetY();
  out_matrix[14] = owner_.GetZ();
}

void UnitMovementRuntime::ComputeBodyLeanMatrix(float *out_matrix, float dt,
                                                bool apply_lean) {
  if (!out_matrix)
    return;

  constexpr float kPi        = 3.1415927f;
  constexpr float kTwoPi     = 6.2831855f;
  constexpr float kHalfPi    = 1.5707964f;
  constexpr float kQuarterPi = 0.78539819f;
  constexpr float kMinDt     = 0.011111111f;
  constexpr float kMaxDt     = 0.050000001f;
  constexpr float kEpsilon   = 0.0099999998f;
  constexpr float kFourPi    = 12.566371f;

  if (dt < kMinDt) dt = kMinDt;
  if (dt > kMaxDt) dt = kMaxDt;

  const std::uint32_t flags = owner_.GetMovementInfo().flags;

  float target_yaw = 0.0f;
  if (flags & (kMoveFlagForward | kMoveFlagBackward)) {
    if (flags & kMoveFlagAscending)
      target_yaw = kQuarterPi;
    else if (flags & kMoveFlagDescending)
      target_yaw = -kQuarterPi;
    else
      target_yaw = owner_.GetMovementInfo().pitch;
  }

  float saved_yaw = 0.0f;
  float final_yaw = target_yaw;
  if (flags & (kMoveFlagStrafeLeft | kMoveFlagStrafeRight)) {
    final_yaw = 0.0f;
  } else {
    saved_yaw = target_yaw;
  }

  if ((flags & kMoveFlagBackward) &&
      (flags & (kMoveFlagAscending | kMoveFlagDescending))) {
    final_yaw = saved_yaw * -1.0f;
  }

  const float world_facing = WorldSmoothBodyFacing();
  const float cached_facing = world_facing;

  float facing_delta = world_facing - body_lean_last_facing_;
  body_lean_last_facing_ = world_facing;

  if (facing_delta > kPi)        facing_delta -= kTwoPi;
  else if (facing_delta < -kPi)  facing_delta += kTwoPi;

  float yaw_diff = final_yaw - body_lean_yaw_;

  if (std::fabs(yaw_diff) >= kEpsilon) {
    if (yaw_diff > kPi)        yaw_diff -= kTwoPi;
    else if (yaw_diff < -kPi)  yaw_diff += kTwoPi;

    float rate = dt * kFourPi;
    if (rate < 0.0f) rate = 0.0f;
    if (rate > 1.0f) rate = 1.0f;

    body_lean_yaw_ += yaw_diff * rate;

    if (body_lean_yaw_ > kPi)        body_lean_yaw_ -= kTwoPi;
    else if (body_lean_yaw_ < -kPi)  body_lean_yaw_ += kTwoPi;
  } else {
    body_lean_yaw_ = final_yaw;
  }

  float target_pitch = 0.0f;
  if (apply_lean) {
    target_pitch = facing_delta / dt * -0.15000001f;
    float pitch_diff = target_pitch - body_lean_pitch_;

    float pitch_rate = dt * kHalfPi;
    if ((flags & (kMoveFlagForward | kMoveFlagBackward)) == 0)
      pitch_rate *= 4.0f;
    if (pitch_rate < 0.0f) pitch_rate = 0.0f;
    if (pitch_rate > 1.0f) pitch_rate = 1.0f;

    float new_pitch = pitch_diff * pitch_rate + body_lean_pitch_;
    if (new_pitch < -kQuarterPi)
      body_lean_pitch_ = -kQuarterPi;
    else if (new_pitch >= kQuarterPi)
      body_lean_pitch_ = kQuarterPi;
    else
      body_lean_pitch_ = new_pitch;
  }

  if (std::fabs(facing_delta) > kHalfPi)
    body_lean_flags_ |= 4u;

  float two_pi = kTwoPi;
  if (body_lean_flags_ & 4u) {
    float heading_diff = cached_facing - body_lean_heading_;
    if (heading_diff > kPi)        heading_diff -= kTwoPi;
    else if (heading_diff < -kPi)  heading_diff += kTwoPi;

    float heading_rate = dt * kFourPi;
    if (heading_rate < 0.0f) heading_rate = 0.0f;
    if (heading_rate > 1.0f) heading_rate = 1.0f;

    body_lean_heading_ = heading_rate * heading_diff + body_lean_heading_;
    body_lean_heading_ = Movement_NormalizeFacing0ToTau(body_lean_heading_);
    two_pi = kTwoPi;
  } else {
    body_lean_heading_ = cached_facing;
  }

  const float ya = -(two_pi - body_lean_heading_);
  const float pa = two_pi - body_lean_yaw_;
  const float ra = body_lean_pitch_;

  const float cy = std::cos(ya), sy = std::sin(ya);
  const float cp = std::cos(pa), sp = std::sin(pa);
  const float cr = std::cos(ra), sr = std::sin(ra);

  const float r00 = cy * cp;
  const float r01 = cy * sp * sr - sy * cr;
  const float r02 = cy * sp * cr + sy * sr;
  const float r10 = sy * cp;
  const float r11 = sy * sp * sr + cy * cr;
  const float r12 = sy * sp * cr - cy * sr;
  const float r20 = -sp;
  const float r21 = cp * sr;
  const float r22 = cp * cr;

  out_matrix[0]  = r00;  out_matrix[1]  = r10;  out_matrix[2]  = r20;  out_matrix[3]  = 0.0f;
  out_matrix[4]  = r01;  out_matrix[5]  = r11;  out_matrix[6]  = r21;  out_matrix[7]  = 0.0f;
  out_matrix[8]  = r02;  out_matrix[9]  = r12;  out_matrix[10] = r22;  out_matrix[11] = 0.0f;
  out_matrix[12] = 0.0f; out_matrix[13] = 0.0f; out_matrix[14] = 0.0f; out_matrix[15] = 1.0f;

  const float scale = owner_.GetScale();
  for (int col = 0; col < 3; ++col) {
    out_matrix[col]     *= scale;
    out_matrix[4 + col] *= scale;
    out_matrix[8 + col] *= scale;
  }

  out_matrix[12] = owner_.GetX();
  out_matrix[13] = owner_.GetY();
  out_matrix[14] = owner_.GetZ();

  body_lean_flags_ &= ~0x7u;
  if (std::fabs(body_lean_yaw_ - final_yaw) > kEpsilon)
    body_lean_flags_ |= 1u;
  if (std::fabs(body_lean_pitch_ - target_pitch) > kEpsilon)
    body_lean_flags_ |= 2u;
  if (std::fabs(body_lean_heading_ - cached_facing) > kEpsilon)
    body_lean_flags_ |= 4u;
}

void UnitPresentationRuntime::ComputeModelBoundingBox() {
  float bounds[6]{};
  if (!owner_.HasObjectBoundingBox()) {
    model_bounding_radius_ = 1.2f;
    return;
  }
  owner_.GetObjectBoundingBox(bounds);
  const float dx = bounds[3] - bounds[0];
  const float dy = bounds[4] - bounds[1];
  const float dz = bounds[5] - bounds[2];
  owner_.SetModelBoundingBoxHeight(bounds[3] > bounds[0] && bounds[4] > bounds[1] &&
                                    bounds[5] > bounds[2]
                                ? dz
                                : 0.0f);
  constexpr float kNearZero = 0.00000023841858f;
  if (std::fabs(dx) < kNearZero && std::fabs(dy) < kNearZero) {
    model_bounding_radius_ = 1.2f;
    return;
  }
  const float half_extent = std::sqrt(dx * dx + dy * dy) * 0.5f;
  const float scaled = owner_.GetScale() * half_extent;
  float radius = std::sqrt(scaled);
  if (scaled > 5.0f) {
    const float excess = scaled - 5.0f;
    radius += excess * excess * 0.06f;
  }
  model_bounding_radius_ = std::min(radius, 10.0f);
}

void UnitPresentationRuntime::UpdateModelTransform(bool) {
  const auto instance_id = owner_.GetPrimaryM2InstanceId();
  if (instance_id == 0u || owner_.m2_system() == nullptr) {
    owner_.ClearObjectBoundingBox();
    return;
  }
  const auto spatial = owner_.m2_system()->QueryInstanceSpatialInfo(instance_id);
  const auto world_bounds = owner_.m2_system()->QueryWorldBoundingBox(instance_id);
  const auto &local = spatial.spatial.local_bounds;
  if (spatial.status != render::m2::M2ResultStatus::kReady ||
      world_bounds.status != render::m2::M2ResultStatus::kReady ||
      local[3] <= local[0] || local[4] <= local[1] || local[5] <= local[2]) {
    owner_.ClearObjectBoundingBox();
    return;
  }
  const bool had_bounds = owner_.HasObjectBoundingBox();
  owner_.SetObjectBoundingBox(world_bounds.box.data());
  if (!had_bounds) {

    ComputeModelBoundingBox();
  }
}

void UnitPresentationRuntime::OnModelLoaded(WorldSession &session,
                             std::uint32_t instance_id) {

  UpdateEffectAttachments();

  if (instance_id != 0 && instance_id == owner_.GetPrimaryM2InstanceId()) {
    owner_.Animation().SetAnimationBoneAvailability(false, false);

    auto &m2_system = *owner_.m2_system();
    const auto instance_model = m2_system.QueryInstanceModel(instance_id);
    if (instance_model.status == render::m2::M2ResultStatus::kReady) {
      const auto model_info = m2_system.QueryModelInfo(instance_model.model_id);
      if (model_info.status == render::m2::M2ResultStatus::kReady) {
        owner_.Movement().SetGroundOrientationMode(static_cast<std::uint8_t>(
            model_info.info.global_flags & 0x3u));
      }
    }
    const ObjectGuid unit_guid = owner_.GetGuid();
    auto* const objects = owner_.object_manager();
    const auto event_callback_status = m2_system.SetTriggeredEventCallback(
        instance_id,
        [&session, objects, unit_guid, instance_id](
            const render::m2::M2TriggeredEvent& event) {
          if (objects == nullptr) {
            return;
          }
          auto* const event_unit =
              objects->GetUnit(unit_guid);
          if (event_unit == nullptr ||
              event_unit->GetPrimaryM2InstanceId() != instance_id) {
            return;
          }

          const auto position = event.world_position;
          UnitAnimationRuntime::AnimationEventCallback(
              session, *objects,
              unit_guid.GetRawValue(), 0u, event.identifier,
              static_cast<std::int32_t>(event.data), position.data(),
              static_cast<std::int32_t>(event.bone));
        });
    if (render::m2::IsTerminalM2ResultStatus(event_callback_status)) {
      owner_.SetPrimaryM2InstanceId(0u);
      return;
    }
    (void)m2_system.ClearAnimationCompletionCallback(instance_id);

    const auto animation_bone_4 = m2_system.QueryKeyBone(instance_id, 4u);
    const auto animation_bone_6 = m2_system.QueryKeyBone(instance_id, 6u);
    owner_.Animation().SetAnimationBoneAvailability(
        animation_bone_4.status == openwow::render::m2::M2ResultStatus::kReady &&
            animation_bone_4.present,
        animation_bone_6.status == openwow::render::m2::M2ResultStatus::kReady &&
            animation_bone_6.present);
  }

  if (ModelAnimationsReady()) {
    owner_.Animation().RefreshSelectedStandAnimation(session, 1,
                                   0xFFFFFFFF);

    const auto anim_id = owner_.Animation().GetCurrentAnimationId();
    if (anim_id.has_value()) {
      const auto *dbc = owner_.dbc_loader();
      if (dbc) {
        const auto *anim_entry = dbc->animation_data().LookupEntry(anim_id.value());
        if (anim_entry && anim_entry->behavior_id == 127) {
          owner_.Animation().ClearSelectedStandAnimationState();
        }
      }
    }

  }

  const auto pending_aura_visual_id =
      owner_.SpellVisuals().PendingAuraVisualId();
  if (pending_aura_visual_id != 0u) {
    if (owner_.Auras().HasSpellId(pending_aura_visual_id)) {
      const auto *dbc = owner_.dbc_loader();
      if (dbc) {
        const auto *spell_entry =
            dbc->spell().LookupEntry(pending_aura_visual_id);
        if (spell_entry && spell_entry->spell_visual[0] != 0) {
          const auto *visual = dbc->spell_visual().LookupEntry(spell_entry->spell_visual[0]);
          if (visual && visual->precast_kit != 0) {
            (void)owner_.SpellVisuals().CreateFromKit(
                session, visual->precast_kit, 2);
          }
        }
      }
    }
    owner_.SpellVisuals().SetPendingAuraVisualId(0u);
  }

  if (void *const vehicle_data = owner_.Vehicle().GetVehicleData();
      vehicle_data != nullptr && vehicle::Vehicle_C_HasDbcEntry(vehicle_data)) {
    vehicle::Vehicle_C_UpdateBoundingRadius(vehicle_data);
  }
  if (auto *const passenger = owner_.Vehicle().GetVehiclePassengerComponent();
      passenger != nullptr && passenger->HasFlag(VehiclePassengerFlag::kSeatAttached)) {
    passenger->AttachToSeat();
  }

  {
    auto &dispatch = ui::game::ScriptEventDispatch::Get();
    dispatch.FireUnitModel(owner_.GetGuid().GetRawValue());
  }
}

}
