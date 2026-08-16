
#include "openwow/ui/game/api/game_lua_api_unit.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/aura_lua_bridge.h"
#include "openwow/game/action_validation_utils.h"
#include "openwow/game/aura_application.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/combat_rating.h"
#include "openwow/game/duel_system.h"
#include "openwow/game/group_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/instance_handler.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/pet_manager.h"
#include "openwow/game/skill_line_ability_lookup.h"
#include "openwow/game/player_chat_flags.h"
#include "openwow/game/power_display.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/targeting.h"
#include "openwow/game/targeting/adapters/ui/unit_selection_color_query.h"
#include "openwow/game/threat_system.h"
#include "openwow/game/tracked_unit_state_slice.h"
#include "openwow/game/trivial_level.h"
#include "openwow/game/unit_level_display.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/world_map_system.h"
#include "openwow/ui/lua_numeric.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace openwow::ui::game::detail {

namespace {

constexpr std::int32_t kDefaultLuaUnitPowerType = 7;
constexpr lua_Number kLuaColorByteScale = 1.0 / 255.0;
constexpr std::uint32_t kRetailMacroTargetNearestFlag = 1u << 5;
constexpr std::uint32_t kGetUnitNameAuraType = 279u;
constexpr std::array<std::uint32_t, 5> kThreatStatusColorTableArgb{
    0xFFFFFFFFu,
    0xFFB0B0B0u,
    0xFFFFFF77u,
    0xFFFF9900u,
    0xFFFF0000u,
};

constexpr std::array<float, 4> kLuaCheckInteractDistanceThresholdsSq{
    900.0f,
    123.45677947998047f,
    100.0f,
    900.0f,
};
constexpr std::uint32_t kTrackedPartySlotCount = 4;

openwow::game::MacroCatalog* FindMacroCatalog(lua_State* state) {
  auto* session = GetWorldSession(state);
  return session != nullptr ? &session->macros() : nullptr;
}

struct LuaUnitVehiclePowerDisplay {
  std::int32_t power_type = 0;
  std::string_view power_token;
  float red = 0.0f;
  float green = 0.0f;
  float blue = 0.0f;
};

struct LuaUnitClassInfo {
  std::string_view display_name;
  std::string_view base_name;
  std::string_view file_token;
};

struct LuaResolvedName {
  std::string name;
  std::optional<std::string> realm;
};

struct LuaFactionGroupStrings {
  std::string_view token;
  std::string_view localized_name;
  bool valid = false;
};

struct LuaUnitPair {
  const openwow::game::CGUnit_C *first = nullptr;
  const openwow::game::CGUnit_C *second = nullptr;
};

constexpr std::uint32_t kWeaponItemClass = 2u;
constexpr std::uint8_t kRangedEquipSlot = 17u;
constexpr std::uint8_t kRangedSkillBonusRating = 22u;
constexpr std::uint8_t kArmorPenetrationRating = 24u;

constexpr std::size_t kUnboundedStormStringCompare = 0x7FFFFFFFu;

[[nodiscard]] std::uint32_t ResolveRangedWeaponSkillLine(
    const openwow::game::CGPlayer_C &player,
    const openwow::data::dbc::DbcLoader &dbc) {
  const auto item_meta = player.GetVisibleItemTemplateMetadata(kRangedEquipSlot);
  std::uint32_t subclass_id = 0;
  bool have_subclass = false;

  if (item_meta.has_value()) {
    if (item_meta->item_class != kWeaponItemClass) return 0;
    subclass_id = item_meta->subclass;
    have_subclass = true;
  } else {

    for (const auto &entry : dbc.item_sub_class().entries()) {
      if (entry.class_id == kWeaponItemClass && (entry.flags & 0x4u) != 0) {
        subclass_id = entry.subclass_id;
        have_subclass = true;
        break;
      }
    }
  }
  if (!have_subclass) return 0;

  std::uint32_t spell_id = 0;
  const auto target_mask = 1u << subclass_id;
  for (auto it = dbc.spell().entries().rbegin();
       it != dbc.spell().entries().rend(); ++it) {
    if ((it->attributes & 0x40u) == 0 || it->equipped_item_class != 2)
      continue;
    auto sub_mask = static_cast<std::uint32_t>(it->equipped_item_sub_class_mask);
    if (sub_mask == target_mask) {
      spell_id = it->id;
      break;
    }
  }
  if (spell_id == 0) return 0;

  const auto *ability = openwow::game::FindSkillLineAbilityForRaceClassSpell(
      dbc.skill_line_ability().entries(),
      dbc.skill_race_class_info().entries(),
      player.State().GetRace(), player.State().GetClass(), spell_id);
  if (!ability || ability->spell_id != spell_id) return 0;
  return ability->skill_id;
}

[[nodiscard]] bool ActivePlayerHasFullControl(const openwow::game::WorldSession &session,
                                              const openwow::game::CGPlayer_C &player) {

  return session.IsInWorld() && player.State().GetHealth() > 0u;
}

[[nodiscard]] std::uint32_t MakeFactionGroupMask(const std::uint32_t mask_id) {
  if (mask_id == 0u) {
    return 0u;
  }

  return 1u << (mask_id & 31u);
}

[[nodiscard]] const openwow::game::CGUnit_C *
ResolveLiveScriptUnit(openwow::game::WorldSession *session, const std::string_view unit_id) {
  if (session == nullptr) {
    return nullptr;
  }

  return ResolveUnitObject(ResolveUnit(session, std::string(unit_id)));
}

int LuaUnitDynamicFlagPredicate(lua_State *L, const char *usage,
                                const std::uint32_t mask) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, usage);
  const auto *const unit = ResolveLiveScriptUnit(call.world_session(), uid);
  return call.wow_bool(unit != nullptr && (unit->GetUInt32(UNIT_DYNAMIC_FLAGS) & mask) != 0u);
}

[[nodiscard]] LuaUnitPair ResolveLiveScriptUnitPair(const LuaCallFrame &call,
                                                    const char *usage) {
  const auto first_id = call.require_string(1, usage);
  const auto second_id = call.require_string(2, usage);
  auto *const session = call.world_session();
  return {
      .first = ResolveLiveScriptUnit(session, first_id),
      .second = ResolveLiveScriptUnit(session, second_id),
  };
}

[[nodiscard]] const openwow::data::dbc::FactionTemplateEntry *
ResolveFactionTemplateForRace(const openwow::data::dbc::DbcLoader &dbc,
                              const std::uint8_t race_id) {
  if (race_id == 0u) {
    return nullptr;
  }

  const auto *const race_entry = dbc.chr_races().LookupEntry(race_id);
  if (race_entry == nullptr || race_entry->faction_id == 0u) {
    return nullptr;
  }

  return dbc.faction_template().LookupEntry(race_entry->faction_id);
}

[[nodiscard]] const openwow::data::dbc::FactionTemplateEntry *
ResolveFactionTemplateForUnit(const openwow::data::dbc::DbcLoader &dbc,
                              openwow::game::WorldSession &session,
                              const std::string_view unit_id) {
  if (openwow::text::EqualsIgnoreCaseAscii(unit_id, "player")) {
    const auto *const active_player = session.objects().GetActivePlayer();
    if (active_player != nullptr) {
      return ResolveFactionTemplateForRace(dbc, active_player->State().GetRace());
    }

    const auto &identity = session.pending_character_identity();
    return identity.is_available()
               ? ResolveFactionTemplateForRace(dbc, identity.race_id)
               : nullptr;
  }

  const auto guid = openwow::game::UnitQueryBridge::Get().ResolveToGuid(&session, unit_id);
  if (guid.IsEmpty()) {
    return nullptr;
  }

  if (const auto *const object = session.objects().Get(guid); object != nullptr) {
    if (object->IsPlayer()) {
      const auto *const unit = static_cast<const openwow::game::CGUnit_C *>(object);
      return ResolveFactionTemplateForRace(dbc, unit->State().GetRace());
    }

    if (!object->IsUnit()) {
      return nullptr;
    }

    const auto *const unit = static_cast<const openwow::game::CGUnit_C *>(object);
    return dbc.faction_template().LookupEntry(unit->State().GetFactionTemplate());
  }

  if (const auto *const name_entry = session.objects().GetNameEntry(guid); name_entry != nullptr) {
    return ResolveFactionTemplateForRace(dbc, name_entry->race);
  }

  return nullptr;
}

[[nodiscard]] LuaFactionGroupStrings
ResolveFactionGroupStrings(const openwow::data::dbc::DbcLoader &dbc,
                           const openwow::data::dbc::FactionTemplateEntry &faction_template) {
  for (const auto &entry : dbc.faction_group()) {
    const std::uint32_t faction_group_mask = MakeFactionGroupMask(entry.mask_id);
    if (faction_group_mask == 0u) {
      continue;
    }
    if ((faction_template.faction_group & faction_group_mask) == 0u) {
      continue;
    }
    if (entry.name.empty()) {
      continue;
    }

    return {
        .token = entry.internal_name,
        .localized_name = entry.name,
        .valid = true,
    };
  }

  return {};
}

[[nodiscard]] std::string ResolveUnknownUnitName() {
  auto localized = openwow::game::Localization::Get().GetString("UNKNOWNOBJECT", "");
  if (!localized.empty()) {
    return localized;
  }

  return "Unknown Being";
}

[[nodiscard]] const openwow::game::PendingCharacterIdentity *
ResolvePendingPlayerIdentity(openwow::game::WorldSession *session,
                             const std::string_view unit_id) {
  if (session == nullptr || !openwow::text::EqualsIgnoreCaseAscii(unit_id, "player")) {
    return nullptr;
  }

  const auto &identity = session->pending_character_identity();
  return identity.is_available() ? &identity : nullptr;
}

[[nodiscard]] std::optional<std::string>
ResolveCachedRealmName(openwow::game::WorldSession &session, const openwow::game::ObjectGuid guid) {
  if (const auto *player_name = session.query_cache().GetPlayerName(guid.GetRawValue());
      player_name != nullptr && !player_name->realm_name.empty()) {
    return player_name->realm_name;
  }

  return std::nullopt;
}

[[nodiscard]] std::uint32_t ExtractLegacyPetNameCacheKey(const openwow::game::ObjectGuid guid) {
  return static_cast<std::uint32_t>((guid.GetRawValue() >> 24) & 0x0FFFFFFFu);
}

[[nodiscard]] std::optional<LuaResolvedName>
ResolvePlayerNameCacheEntry(openwow::game::WorldSession &session,
                            const openwow::game::ObjectGuid guid, const bool request_on_miss) {
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  if (const auto *player_name = session.query_cache().GetPlayerName(guid.GetRawValue());
      player_name != nullptr && !player_name->name.empty()) {
    return LuaResolvedName{
        .name = player_name->name,
        .realm = player_name->realm_name.empty()
                     ? std::nullopt
                     : std::optional<std::string>(player_name->realm_name),
    };
  }

  if (const auto *name_entry = session.objects().GetNameEntry(guid);
      name_entry != nullptr && !name_entry->name.empty()) {
    return LuaResolvedName{
        .name = name_entry->name,
        .realm = std::nullopt,
    };
  }

  if (request_on_miss) {
    (void)session.query_cache().RequestNameQuery(guid.GetRawValue());
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<LuaResolvedName>
ResolvePetNameCacheEntry(openwow::game::WorldSession &session, const std::uint32_t pet_number,
                         const openwow::game::ObjectGuid guid,
                         const std::optional<std::uint32_t> required_timestamp,
                         const bool request_on_miss) {
  if (guid.IsEmpty() || pet_number == 0) {
    return std::nullopt;
  }

  if (const auto *pet_name = session.pet().GetPetName(pet_number); pet_name != nullptr) {
    if (!pet_name->found || pet_name->name.empty()) {
      return std::nullopt;
    }
    if (required_timestamp.has_value() && pet_name->name_timestamp != *required_timestamp) {
      if (request_on_miss) {
        session.Send(openwow::game::PetManager::BuildPetNameQuery(pet_number, guid));
      }
      return std::nullopt;
    }

    return LuaResolvedName{
        .name = pet_name->name,
        .realm = std::nullopt,
    };
  }

  if (request_on_miss) {
    session.Send(openwow::game::PetManager::BuildPetNameQuery(pet_number, guid));
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<openwow::game::ObjectGuid>
ResolveGetUnitNameAuraSourceGuid(openwow::game::WorldSession &session,
                                 const openwow::game::CGUnit_C &unit) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return std::nullopt;
  }

  for (const auto &aura : unit.Auras().All()) {
    if (aura.spell_id == 0 || aura.caster_guid.IsEmpty()) {
      continue;
    }

    const auto *spell = dbc->spell().LookupEntry(aura.spell_id);
    if (spell == nullptr) {
      continue;
    }

    for (std::size_t effect_index = 0; effect_index < spell->effect_apply_aura.size();
         ++effect_index) {
      const auto effect_mask = static_cast<std::uint32_t>(1u << effect_index);
      if (spell->effect_apply_aura[effect_index] == kGetUnitNameAuraType &&
          (aura.flags & effect_mask) != 0u) {
        return aura.caster_guid;
      }
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<LuaResolvedName>
ResolveLiveObjectName(openwow::game::WorldSession &session,
                      const openwow::game::WorldObject &object,
                      const bool allow_aura_name_override);

[[nodiscard]] bool CanExposeUnitName(const openwow::game::CGUnit_C &unit) {
  return !unit.State().IsNotSelectable() || unit.IsPlayer() ||
         unit.Interaction().ResolveControllingPlayer() != nullptr;
}

[[nodiscard]] std::optional<LuaResolvedName>
ResolveCachedUnitNameByGuid(openwow::game::WorldSession &session,
                            const openwow::game::ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  if (const auto snapshot =
          openwow::game::UnitQueryBridge::Get().GetPlayerInfoByGUID(&session, guid.GetRawValue());
      snapshot.has_value() && !snapshot->name.empty()) {
    return LuaResolvedName{
        .name = snapshot->name,
        .realm = ResolveCachedRealmName(session, guid),
    };
  }

  if (guid.IsPet()) {
    if (const auto pet_name =
            ResolvePetNameCacheEntry(session, ExtractLegacyPetNameCacheKey(guid), guid,
                                     std::nullopt, true);
        pet_name.has_value()) {
      return pet_name;
    }
  }

  if (guid.IsCreature() || guid.IsVehicle()) {
    if (const auto *creature_template =
            session.query_cache().GetOrRequestCreatureTemplate(guid.GetEntry(), guid.GetRawValue());
        creature_template != nullptr && !creature_template->name.empty()) {
      return LuaResolvedName{
          .name = creature_template->name,
          .realm = std::nullopt,
      };
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<LuaResolvedName>
ResolveLiveUnitName(openwow::game::WorldSession &session, const openwow::game::CGUnit_C &unit,
                    const bool allow_aura_name_override) {
  if (!CanExposeUnitName(unit)) {
    return std::nullopt;
  }

  if (allow_aura_name_override) {
    if (const auto source_guid = ResolveGetUnitNameAuraSourceGuid(session, unit);
        source_guid.has_value()) {
      if (const auto *source_object = session.objects().Get(*source_guid);
          source_object != nullptr && source_object->IsUnit()) {
        return ResolveLiveObjectName(session, *source_object, false);
      }

      if (const auto resolved = ResolveCachedUnitNameByGuid(session, *source_guid);
          resolved.has_value()) {
        auto proxy_name = *resolved;
        proxy_name.realm.reset();
        return proxy_name;
      }
    }
  }

  if (unit.IsPlayer()) {
    return ResolvePlayerNameCacheEntry(session, unit.GetGuid(), true);
  }

  if (unit.State().GetPetNumber() != 0) {
    return ResolvePetNameCacheEntry(session, unit.State().GetPetNumber(), unit.GetGuid(),
                                    unit.State().GetPetNameTimestamp(), true);
  }

  std::string name = unit.GetName();
  if (name.empty()) {
    if (const auto *creature_template = session.query_cache().GetOrRequestCreatureTemplate(
            unit.GetEntry(), unit.GetGuid().GetRawValue());
        creature_template != nullptr) {
      name = creature_template->name;
    }
  }

  if (name.empty()) {
    return std::nullopt;
  }

  return LuaResolvedName{
      .name = std::move(name),
      .realm = std::nullopt,
  };
}

[[nodiscard]] std::optional<LuaResolvedName>
ResolveLiveObjectName(openwow::game::WorldSession &session,
                      const openwow::game::WorldObject &object,
                      const bool allow_aura_name_override) {
  if (const auto *unit = ResolveUnitObject(&object); unit != nullptr) {
    return ResolveLiveUnitName(session, *unit, allow_aura_name_override);
  }

  std::string name = object.GetName();
  if (name.empty() && object.IsGameObject()) {
    if (const auto *gameobject_template = session.query_cache().GetOrRequestGameObjectTemplate(
            object.GetEntry(), object.GetGuid().GetRawValue());
        gameobject_template != nullptr) {
      name = gameobject_template->name;
    }
  } else if (name.empty() && object.IsItem()) {
    if (const auto *item_template =
            session.query_cache().GetOrRequestItemTemplate(object.GetEntry());
        item_template != nullptr) {
      name = item_template->name;
    }
  }

  if (name.empty()) {
    return std::nullopt;
  }

  return LuaResolvedName{
      .name = std::move(name),
      .realm = std::nullopt,
  };
}

bool IsActivePlayerOrTrackedPartyMemberGuid(const openwow::game::WorldSession &session,
                                            const openwow::game::ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return false;
  }

  if (guid == session.objects().GetActivePlayerGuid()) {
    return true;
  }

  auto &group_system = openwow::game::GroupSystem::Get();
  const auto raw_guid = guid.GetRawValue();
  for (std::uint32_t slot = 0; slot < kTrackedPartySlotCount; ++slot) {
    if (group_system.GetTrackedPartyMemberGuid(slot) == raw_guid) {
      return true;
    }
  }

  return false;
}

bool IsTrackedPartyPlayerOrPetGuid(const openwow::game::ObjectManager& objects,
                                   const openwow::game::ObjectGuid guid) {
  return !guid.IsEmpty() &&
         openwow::game::GroupSystem::Get().IsPartyUnitGuid(
             objects, guid.GetRawValue());
}

std::optional<int> ResolveTrackedRaidRosterIndex(openwow::game::WorldSession *session,
                                                 const std::string &unit_id) {
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto guid = ResolveUnitId(session, unit_id);
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  const auto raid_index =
      openwow::game::GroupSystem::Get().FindRaidRosterIndexByGuid(guid.GetRawValue());
  if (raid_index < 0) {
    return std::nullopt;
  }

  return raid_index;
}

bool IsTrackedRaidMemberGuid(const openwow::game::ObjectGuid guid) {
  return !guid.IsEmpty() &&
         openwow::game::GroupSystem::Get().FindRaidRosterIndexByGuid(guid.GetRawValue()) >= 0;
}

bool IsTrackedRaidPlayerOrPetGuid(const openwow::game::ObjectManager& objects,
                                  const openwow::game::ObjectGuid guid) {
  return !guid.IsEmpty() &&
         openwow::game::GroupSystem::Get().IsRaidUnitGuid(
             objects, guid.GetRawValue());
}

bool CanQueryLegacyPlayerMapPosition(const openwow::game::WorldSession &session,
                                     const openwow::game::ObjectGuid guid) {
  return IsActivePlayerOrTrackedPartyMemberGuid(session, guid) || IsTrackedRaidMemberGuid(guid);
}

openwow::ui::WorldMapSystem::MapCoord ApplyUnitMapPositionSuppression(
    openwow::ui::WorldMapSystem::MapCoord coord) {
  if (coord.suppress_unit_position) {
    coord.x = 0.0f;
    coord.y = 0.0f;
  }
  return coord;
}

std::optional<openwow::ui::WorldMapSystem::MapCoord>
ResolveLivePlayerMapPosition(lua_State *L,
                             const openwow::game::WorldSession &session,
                             const openwow::game::ObjectGuid guid) {
  auto *world_map = WorldMapStateOrNull(L);
  if (world_map == nullptr || !world_map->IsDbcInitialized()) {
    return std::nullopt;
  }

  const auto *unit = session.objects().GetUnit(guid);
  if (unit == nullptr) {
    return std::nullopt;
  }

  const auto map_id = session.objects().GetMapId();
  const auto unit_position = unit->GetPosition();
  return ApplyUnitMapPositionSuppression(world_map->WorldToMapForCurrentSelection(
      map_id, unit_position.x, unit_position.y, unit_position.z));
}

std::optional<openwow::ui::WorldMapSystem::MapCoord>
ResolveCachedPlayerMapPosition(lua_State *L,
                               const openwow::game::WorldSession &session,
                               const openwow::game::ObjectGuid guid) {
  auto *world_map = WorldMapStateOrNull(L);
  if (world_map == nullptr || !world_map->IsDbcInitialized()) {
    return std::nullopt;
  }

  const auto cached = session.party_stats().GetCachedMember(guid.GetRawValue());
  if (!cached.has_value()) {
    return std::nullopt;
  }

  constexpr std::uint32_t kRequiredMask =
      openwow::game::GroupUpdateFlag::kZone | openwow::game::GroupUpdateFlag::kPosition;
  if ((cached->available_mask & kRequiredMask) != kRequiredMask) {
    return std::nullopt;
  }

  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return std::nullopt;
  }

  const auto *area_entry = dbc->area_table().LookupEntry(cached->stats.zone_id);
  if (area_entry == nullptr) {
    return std::nullopt;
  }

  return ApplyUnitMapPositionSuppression(world_map->WorldToMapForCurrentSelection(
      area_entry->map_id, static_cast<float>(cached->stats.pos_x),
      static_cast<float>(cached->stats.pos_y), 0.0f));
}

std::optional<LuaUnitClassInfo> ResolveLuaUnitClassInfo(lua_State *L,
                                                        openwow::game::WorldSession *session,
                                                        const std::string &unit_id) {
  if (session == nullptr) {
    return std::nullopt;
  }

  std::uint8_t class_id = 0;
  std::uint8_t gender_id = 2;

  if (openwow::text::EqualsIgnoreCaseAscii(unit_id, "player")) {
    if (const auto *identity = ResolvePendingPlayerIdentity(session, unit_id)) {
      class_id = identity->class_id;
      gender_id = identity->gender;
    } else {
      const auto *player = session->objects().GetLocalPlayerTyped();
      if (player == nullptr) {
        return std::nullopt;
      }
      class_id = player->State().GetClass();
      gender_id = player->State().GetGender();
    }
  } else {
    const auto guid = ResolveUnitId(session, unit_id);
    if (guid.IsEmpty()) {
      return std::nullopt;
    }

    if (const auto *unit = session->objects().GetUnit(guid); unit != nullptr) {
      class_id = unit->State().GetClass();
      gender_id = unit->State().GetGender();
    } else if (const auto *name_entry = session->objects().GetNameEntry(guid);
               name_entry != nullptr) {
      class_id = name_entry->class_id;
      gender_id = name_entry->gender;
    } else {
      return std::nullopt;
    }
  }

  const auto display_name = LookupClassDisplayName(L, class_id, gender_id);
  const auto base_name = LookupClassBaseName(L, class_id);
  const auto file_token = LookupClassFileToken(L, class_id);
  if (display_name.empty() || base_name.empty() || file_token.empty()) {
    return std::nullopt;
  }

  return LuaUnitClassInfo{display_name, base_name, file_token};
}

std::int32_t ParseLuaUnitPowerDisplayIndex(lua_State *L) {
  if (!lua_isnumber(L, 2)) {
    return 0;
  }

  return openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(openwow::ui::TruncateLuaNumberToI32(
          lua_tonumber(L, 2))) -
      1u);
}

int LuaTargetNearestWithFilter(lua_State *L, const openwow::game::TargetFilter filter,
                               const bool uses_macro_protected_flag) {
  auto* macro_catalog = FindMacroCatalog(L);
  if (macro_catalog == nullptr) {
    return 0;
  }
  const bool running_macro = macro_catalog->IsRunningMacro();
  if (uses_macro_protected_flag && running_macro &&
      !macro_catalog->HasRetailProtectedActionFlag(
          kRetailMacroTargetNearestFlag)) {
    return 0;
  }

  const bool reverse = ScriptReadBoolArgOrDefault(L, 1, false);
  auto *targeting = GetTargetingSystem(L);

  if (targeting != nullptr && GameUI_CanPerformTaintForbiddenAction()) {
    targeting->TargetNearest(filter, reverse);
  }

  if (uses_macro_protected_flag && running_macro) {
    macro_catalog->ConsumeRetailProtectedActionFlag(
        kRetailMacroTargetNearestFlag);
  }

  return 0;
}

int LuaTargetDirectionWithFilter(lua_State *L, const openwow::game::TargetFilter filter,
                                 const char *usage) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, usage);
  }

  auto* macro_catalog = FindMacroCatalog(L);
  if (macro_catalog == nullptr ||
      !macro_catalog->HasRetailProtectedActionFlag(
          kRetailMacroTargetNearestFlag)) {
    return 0;
  }

  const float facing = static_cast<float>(lua_tonumber(L, 1));
  const float cone = lua_isnumber(L, 2) ? static_cast<float>(lua_tonumber(L, 2)) : 0.0f;

  if (auto *targeting = GetTargetingSystem(L);
      targeting != nullptr && GameUI_CanPerformTaintForbiddenAction()) {
    targeting->TargetDirection(facing, cone, filter);
  }

  macro_catalog->ConsumeRetailProtectedActionFlag(
      kRetailMacroTargetNearestFlag);
  return 0;
}

std::uint32_t ResolveLuaUnitVehicleRecordId(openwow::game::WorldSession *session,
                                            const openwow::game::CGUnit_C &unit) {
  if (const auto *vehicle_entry = unit.Vehicle().GetVehicleEntry(); vehicle_entry != nullptr) {
    return vehicle_entry->id;
  }

  const auto &movement = unit.GetMovementUpdate();
  if (movement.HasUpdateFlag(openwow::game::kUpdateFlagVehicle) && movement.vehicle_id != 0) {
    return movement.vehicle_id;
  }

  if (session == nullptr) {
    return 0;
  }

  const auto &forced_vehicle = session->vehicle().last_force_vehicle();
  if (forced_vehicle.unit_guid == unit.GetGuid() && forced_vehicle.vehicle_rec_id > 0) {
    return static_cast<std::uint32_t>(forced_vehicle.vehicle_rec_id);
  }

  const auto &player_vehicle = session->vehicle().last_vehicle_data();
  if (unit.GetGuid() == openwow::game::CGObject_C::GetActivePlayerGuid() &&
      player_vehicle.player_guid == unit.GetGuid()) {
    return player_vehicle.vehicle_id;
  }

  return 0;
}

const openwow::data::dbc::VehicleSeatEntry *
ResolveLuaUnitVehicleSeatEntryImpl(openwow::game::WorldSession *session,
                                   const std::string &unit_id) {
  if (session == nullptr) {
    return nullptr;
  }

  const auto guid = ResolveUnitId(session, unit_id);
  if (guid.IsEmpty()) {
    return nullptr;
  }

  if (const auto *unit = session->objects().GetUnit(guid); unit != nullptr) {
    return openwow::game::ResolveUnitVehicleSeatEntry(*session, *unit);
  }

  return openwow::game::LookupCachedUnitVehicleSeatEntry(*session, guid);
}

struct LuaVehicleSeatInfoResult {
  std::string_view label;
  std::optional<LuaResolvedName> passenger_name;
  bool transition_usable = false;
  bool can_switch = false;
};

[[nodiscard]] std::optional<LuaVehicleSeatInfoResult>
ResolveLuaVehicleSeatInfo(openwow::game::WorldSession *session, const openwow::game::CGUnit_C &unit,
                          const int seat_ordinal) {
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto *root_vehicle = openwow::game::ResolveRootVehicleUnit(unit);
  if (root_vehicle == nullptr) {
    return std::nullopt;
  }

  const openwow::game::CGUnit_C *seat_vehicle = nullptr;
  std::uint8_t seat_index = 0;
  if (!openwow::game::FindExpandedVehicleSeat(*root_vehicle, seat_ordinal, seat_vehicle,
                                              seat_index) ||
      seat_vehicle == nullptr) {
    return std::nullopt;
  }

  const auto *seat_entry =
      openwow::game::LookupVehicleSeatEntryForVehicleSeat(*session, *seat_vehicle, seat_index);
  if (seat_entry == nullptr) {
    return std::nullopt;
  }

  std::string_view label = "None";
  if ((seat_entry->flags & 0x800u) != 0u) {
    label = seat_vehicle == root_vehicle ? "Root" : "Child";
  }

  std::optional<LuaResolvedName> passenger_name;
  if (const auto *passenger =
          openwow::game::FindDirectVehiclePassengerBySeatIndex(*seat_vehicle, seat_index);
      passenger != nullptr) {
    if (const auto resolved =
            ResolveLiveUnitName(*session, *passenger, true);
        resolved.has_value()) {
      passenger_name = *resolved;
    } else if (const auto cached = ResolveCachedUnitNameByGuid(*session, passenger->GetGuid());
               cached.has_value()) {
      passenger_name = *cached;
    }
  }

  return LuaVehicleSeatInfoResult{
      .label = label,
      .passenger_name = std::move(passenger_name),
      .transition_usable = (seat_entry->transition_flags & 0x20u) != 0u,
      .can_switch = (seat_entry->flags & 0x04000000u) != 0u,
  };
}

enum class LuaLegacyVehicleQuery {
  kAttachedSeat,
  kUsingVehicle,
};

[[nodiscard]] bool EvaluateLuaLegacyVehicleQuery(openwow::game::WorldSession *session,
                                                 const std::string &unit_id,
                                                 const LuaLegacyVehicleQuery query) {
  if (session == nullptr) {
    return false;
  }

  const auto guid = ResolveUnitId(session, unit_id);
  if (guid.IsEmpty()) {
    return false;
  }

  if (const auto *unit = session->objects().GetUnit(guid); unit != nullptr) {
    if (query == LuaLegacyVehicleQuery::kAttachedSeat) {
      return unit->Vehicle().HasAttachedVehiclePassenger();
    }

    return unit->Vehicle().IsUsingVehicle();
  }

  return openwow::game::LookupCachedUnitVehicleSeatEntry(*session, guid) != nullptr;
}

std::optional<LuaUnitVehiclePowerDisplay>
LookupLuaUnitVehiclePowerDisplay(lua_State *L, openwow::game::WorldSession *session,
                                 const openwow::game::CGUnit_C &unit,
                                 const std::int32_t display_index) {
  if (display_index < 0 || display_index >= 3) {
    return std::nullopt;
  }

  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return std::nullopt;
  }

  const auto vehicle_record_id = ResolveLuaUnitVehicleRecordId(session, unit);
  if (vehicle_record_id == 0) {
    return std::nullopt;
  }

  const auto *vehicle_entry = dbc->vehicle().LookupEntry(vehicle_record_id);
  if (vehicle_entry == nullptr) {
    return std::nullopt;
  }

  const auto power_display_id = vehicle_entry->power_display_id[display_index];
  if (power_display_id == 0) {
    return std::nullopt;
  }

  const auto *power_display = dbc->power_display().LookupEntry(power_display_id);
  if (power_display == nullptr) {
    return std::nullopt;
  }

  if (power_display->actual_type != unit.State().GetPowerType()) {
    return std::nullopt;
  }

  return LuaUnitVehiclePowerDisplay{
      .power_type = static_cast<std::int32_t>(power_display->actual_type),
      .power_token = power_display->tag,
      .red = static_cast<float>(
          static_cast<lua_Number>(power_display->red) * kLuaColorByteScale),
      .green = static_cast<float>(
          static_cast<lua_Number>(power_display->green) * kLuaColorByteScale),
      .blue = static_cast<float>(
          static_cast<lua_Number>(power_display->blue) * kLuaColorByteScale),
  };
}

int PushLuaUnitPowerTypeResult(lua_State *L, const std::int32_t power_type) {
  lua_pushnumber(L, static_cast<lua_Number>(power_type));
  lua_pushstring(L, openwow::game::PowerTypeToString(static_cast<std::uint32_t>(power_type)));
  return 2;
}

int PushLuaUnitVehiclePowerDisplayResult(lua_State *L, const LuaUnitVehiclePowerDisplay &display) {
  lua_pushnumber(L, static_cast<lua_Number>(display.power_type));
  lua_pushlstring(L, display.power_token.data(), display.power_token.size());
  lua_pushnumber(L, static_cast<lua_Number>(display.red));
  lua_pushnumber(L, static_cast<lua_Number>(display.green));
  lua_pushnumber(L, static_cast<lua_Number>(display.blue));
  return 5;
}

int PushLuaUnitPowerTypeEmptyResult(lua_State *L) {
  lua_pushnumber(L, 0.0);
  lua_pushstring(L, "");
  return 2;
}

struct LegacyTrackedUnitCacheView {
  openwow::game::CachedPartyMemberStats cached;
  std::optional<openwow::game::TrackedControlledUnitStateSlice> controlled;

  [[nodiscard]] bool IsControlledUnit() const {
    return controlled.has_value();
  }

  [[nodiscard]] std::string_view CachedName() const {
    return controlled.has_value() ? std::string_view(controlled->name)
                                  : std::string_view{};
  }

  [[nodiscard]] std::uint32_t CachedCurrentHealth() const {
    return controlled.has_value() ? controlled->cur_hp : cached.stats.cur_hp;
  }

  [[nodiscard]] std::uint32_t CachedMaxHealth() const {
    return controlled.has_value() ? controlled->max_hp : cached.stats.max_hp;
  }

  [[nodiscard]] std::uint8_t CachedPowerType() const {
    return controlled.has_value() ? controlled->power_type
                                  : cached.stats.power_type;
  }

  [[nodiscard]] std::uint16_t CachedCurrentPower() const {
    return controlled.has_value() ? controlled->cur_power
                                  : cached.stats.cur_power;
  }

  [[nodiscard]] std::uint16_t CachedMaxPower() const {
    return controlled.has_value() ? controlled->max_power
                                  : cached.stats.max_power;
  }

  [[nodiscard]] std::uint16_t CachedStatus() const {
    return controlled.has_value() ? controlled->status : cached.stats.status;
  }
};

std::optional<openwow::game::CachedPartyMemberStats>
LookupCachedGroupMemberStats(openwow::game::WorldSession *session, const std::string &unit_id,
                             openwow::game::ObjectGuid *out_guid = nullptr) {
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto guid = ResolveUnitId(session, unit_id);
  if (out_guid != nullptr) {
    *out_guid = guid;
  }
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  return session->party_stats().GetCachedMember(guid.GetRawValue());
}

struct LuaDeadState {
  bool found = false;
  bool dead = false;
  bool ghost = false;
  bool feign_death = false;
  bool is_party_or_raid_unit = false;
};

[[nodiscard]] bool IsLuaPartyOrRaidUnit(
    const openwow::game::ObjectManager& objects,
    const openwow::game::CGUnit_C &unit) {
  return openwow::game::GroupSystem::Get()
      .IsActivePlayerPartyOrRaidUnitGuid(objects, unit.GetGuid().GetRawValue());
}

[[nodiscard]] LuaDeadState ResolveLuaDeadState(
    openwow::game::WorldSession *session,
    const openwow::game::CGUnit_C *unit) {
  LuaDeadState state;
  if (unit == nullptr) {
    return state;
  }

  state.found = true;
  state.dead = unit->State().GetHealth() == 0;
  state.ghost =
      unit->IsPlayer() && (unit->GetUInt32(PLAYER_FLAGS) & 0x10u) != 0;
  state.feign_death =
      (unit->GetUInt32(UNIT_DYNAMIC_FLAGS) & kUnitDynFlagDead) != 0;

  if (state.feign_death && session != nullptr) {
    state.is_party_or_raid_unit = IsLuaPartyOrRaidUnit(session->objects(), *unit);
  }

  return state;
}

[[nodiscard]] LuaDeadState ResolveLuaDeadState(
    openwow::game::WorldSession *session, const std::string &unit_id) {
  const auto *const object = ResolveUnit(session, unit_id);
  if (object != nullptr && object->IsUnit()) {
    return ResolveLuaDeadState(
        session, static_cast<const openwow::game::CGUnit_C *>(object));
  }

  LuaDeadState state;
  if (const auto cached = LookupCachedGroupMemberStats(session, unit_id);
      cached.has_value()
      && (cached->available_mask & openwow::game::GroupUpdateFlag::kStatus)
             != 0) {
    state.found = true;
    const auto status = cached->stats.status;
    state.dead =
        (status & openwow::game::GroupMemberStatus::kDead) != 0;
    state.ghost =
        (status & openwow::game::GroupMemberStatus::kGhost) != 0;
  }
  return state;
}

std::optional<LegacyTrackedUnitCacheView>
LookupCachedControlledUnitStats(openwow::game::WorldSession *session,
                                const openwow::game::ObjectGuid guid) {
  if (session == nullptr || guid.IsEmpty()) {
    return std::nullopt;
  }

  auto slice = openwow::game::FindTrackedUnitStateSliceByGuid(
      session->objects(), guid, openwow::game::GroupSystem::Get(),
      session->party_stats(), openwow::game::BattlefieldInfo::Get());
  if (!slice.has_value()) {
    return std::nullopt;
  }

  return LegacyTrackedUnitCacheView{.controlled = std::move(*slice)};
}

std::optional<LegacyTrackedUnitCacheView>
LookupLegacyTrackedUnitCacheSlice(openwow::game::WorldSession *session,
                                  const openwow::game::ObjectGuid guid) {
  if (session == nullptr || guid.IsEmpty()) {
    return std::nullopt;
  }

  if (const auto cached = session->party_stats().GetCachedMember(guid.GetRawValue());
      cached.has_value()) {
    return LegacyTrackedUnitCacheView{
        .cached = *cached,
    };
  }

  return LookupCachedControlledUnitStats(session, guid);
}

std::optional<LegacyTrackedUnitCacheView>
LookupLegacyTrackedUnitCacheSlice(openwow::game::WorldSession *session, const std::string &unit_id,
                                  openwow::game::ObjectGuid *out_guid = nullptr) {
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto guid = ResolveUnitId(session, unit_id);
  if (out_guid != nullptr) {
    *out_guid = guid;
  }
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  return LookupLegacyTrackedUnitCacheSlice(session, guid);
}

bool IsTrackedGroupUnitGuid(const openwow::game::ObjectManager& objects,
                            const openwow::game::ObjectGuid &guid) {
  return !guid.IsEmpty() &&
         openwow::game::GroupSystem::Get().IsGroupUnitGuid(
             objects, guid.GetRawValue());
}

bool IsTrackedGroupMemberGuid(const openwow::game::ObjectGuid &guid) {
  if (guid.IsEmpty()) {
    return false;
  }

  for (const auto &member : openwow::game::GroupSystem::Get().GetMembers()) {
    if (member.guid == guid) {
      return true;
    }
  }
  return false;
}

enum class LuaUnitStatusFlag : std::uint16_t {
  kAfk = openwow::game::GroupMemberStatus::kAfk,
  kDnd = openwow::game::GroupMemberStatus::kDnd,
};

[[nodiscard]] std::uint32_t GetLivePlayerStatusMask(const LuaUnitStatusFlag flag) {
  switch (flag) {
  case LuaUnitStatusFlag::kAfk:
    return openwow::game::PlayerFlagBits::kAFK;
  case LuaUnitStatusFlag::kDnd:
    return openwow::game::PlayerFlagBits::kDND;
  }

  return 0;
}

[[nodiscard]] bool LivePlayerStatusIsVisible(openwow::game::WorldSession *session,
                                             const openwow::game::CGPlayer_C &player,
                                             const LuaUnitStatusFlag flag) {
  if (flag == LuaUnitStatusFlag::kAfk) {
    const auto *active_player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
    if (active_player == nullptr ||
        active_player->Interaction().CanAttackSpellTarget(player)) {
      return false;
    }

    if (player.GetGuid() == active_player->GetGuid() &&
        session->chat_sender().IsLocalAfkDisplayed()) {
      return true;
    }
  }

  return (player.GetPlayerFlags() & GetLivePlayerStatusMask(flag)) != 0;
}

[[nodiscard]] bool CachedUnitHasStatus(const LegacyTrackedUnitCacheView &cached,
                                       const LuaUnitStatusFlag flag) {
  return (cached.CachedStatus() & static_cast<std::uint16_t>(flag)) != 0;
}

int LuaUnitHasStatus(lua_State *L, const char *usage, const LuaUnitStatusFlag flag) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, usage);
  const auto guid = ResolveUnitId(session, uid);

  if (session != nullptr && !guid.IsEmpty()) {
    if (const auto *player = session->objects().GetPlayer(guid); player != nullptr) {
      return call.wow_bool(LivePlayerStatusIsVisible(session, *player, flag));
    }

    if (const auto cached = LookupLegacyTrackedUnitCacheSlice(session, guid)) {
      return call.wow_bool(CachedUnitHasStatus(*cached, flag));
    }
  }

  return call.nil();
}

std::optional<std::int32_t> ParseLuaUnitPowerType(lua_State *L) {
  if (!lua_isnumber(L, 2)) {
    return kDefaultLuaUnitPowerType;
  }

  const auto parsed =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 2));
  if (parsed < 0 || parsed > kDefaultLuaUnitPowerType) {
    return std::nullopt;
  }

  return parsed;
}

std::optional<std::uint32_t>
LookupCachedPowerValue(const openwow::game::CachedPartyMemberStats &cached,
                       const std::int32_t requested_power_type, const bool max_value) {
  if ((cached.available_mask & openwow::game::GroupUpdateFlag::kPowerType) == 0) {
    return std::nullopt;
  }

  const auto actual_power_type = static_cast<std::int32_t>(cached.stats.power_type);
  const auto resolved_power_type =
      requested_power_type == kDefaultLuaUnitPowerType ? actual_power_type : requested_power_type;
  if (resolved_power_type != actual_power_type) {
    return std::uint32_t{0};
  }

  const auto required_mask = max_value ? openwow::game::GroupUpdateFlag::kMaxPower
                                       : openwow::game::GroupUpdateFlag::kCurPower;
  if ((cached.available_mask & required_mask) == 0) {
    return std::nullopt;
  }

  const auto raw_value = max_value ? cached.stats.max_power : cached.stats.cur_power;
  return openwow::game::NormalizePowerDisplayValue(raw_value, actual_power_type);
}

std::optional<std::uint32_t>
LookupLegacyTrackedUnitPowerValue(const LegacyTrackedUnitCacheView &cached,
                                  const std::int32_t requested_power_type, const bool max_value) {
  if (!cached.IsControlledUnit()) {
    return LookupCachedPowerValue(cached.cached, requested_power_type, max_value);
  }

  const auto actual_power_type = static_cast<std::int32_t>(cached.CachedPowerType());
  const auto resolved_power_type =
      requested_power_type == kDefaultLuaUnitPowerType ? actual_power_type : requested_power_type;
  if (resolved_power_type != actual_power_type) {
    return std::uint32_t{0};
  }

  const auto raw_value = max_value ? cached.CachedMaxPower() : cached.CachedCurrentPower();
  return openwow::game::NormalizePowerDisplayValue(raw_value, actual_power_type);
}

std::uint32_t QueryLiveUnitPower(const openwow::game::ObjectManager& objects,
                                 const openwow::game::CGUnit_C &unit,
                                 const std::int32_t requested_power_type, const bool max_value) {
  const auto actual_power_type = static_cast<std::int32_t>(unit.State().GetPowerType());
  const auto resolved_power_type =
      requested_power_type == kDefaultLuaUnitPowerType ? actual_power_type : requested_power_type;
  if (resolved_power_type < 0 || resolved_power_type > 6) {
    return 0;
  }

  if (!max_value) {
    const bool is_hidden = (unit.State().GetDynamicFlags() & kUnitDynFlagDead) != 0 &&
                           !IsLuaPartyOrRaidUnit(objects, unit);
    if (is_hidden) {
      return 0;
    }

    const bool is_active_player =
        unit.GetGuid() == openwow::game::CGObject_C::GetActivePlayerGuid();
    if (!is_active_player && resolved_power_type != actual_power_type) {
      return 0;
    }

    return openwow::game::NormalizePowerDisplayValue(unit.GetPowerOrHealth(resolved_power_type),
                                                     resolved_power_type);
  }

  return openwow::game::NormalizePowerDisplayValue(
      unit.State().GetMaxPower(static_cast<std::uint8_t>(resolved_power_type)), resolved_power_type);
}

int LuaUnitPowerCommon(lua_State *L, const char *usage, const bool max_value) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, usage);

  const auto requested_power_type = ParseLuaUnitPowerType(L);
  if (!requested_power_type.has_value()) {
    return call.number(0.0);
  }

  auto *session = call.world_session();
  const auto *object = ResolveUnit(session, uid);
  if (session != nullptr && object && object->IsUnit()) {
    const auto &unit = static_cast<const openwow::game::CGUnit_C &>(*object);
    return call.number(static_cast<lua_Number>(
        QueryLiveUnitPower(session->objects(), unit, *requested_power_type, max_value)));
  }

  if (const auto cached = LookupLegacyTrackedUnitCacheSlice(session, uid)) {
    if (const auto value =
            LookupLegacyTrackedUnitPowerValue(*cached, *requested_power_type, max_value)) {
      return call.number(static_cast<lua_Number>(*value));
    }
  }

  return call.number(0.0);
}

[[nodiscard]] std::uint32_t ResolveLuaThreatStatusColorArgb(const lua_Number raw_status) {
  const double status = static_cast<double>(raw_status);
  if (!std::isfinite(status)) {
    return kThreatStatusColorTableArgb[1];
  }

  const auto table_index = static_cast<long long>(std::trunc(status + 1.0));
  if (table_index < 0 ||
      table_index >= static_cast<long long>(kThreatStatusColorTableArgb.size())) {
    return kThreatStatusColorTableArgb[1];
  }

  return kThreatStatusColorTableArgb[static_cast<std::size_t>(table_index)];
}

void PushLuaThreatStatusColor(lua_State *L, const std::uint32_t argb) {
  FrameScript_PushNumber(L, static_cast<lua_Number>((argb >> 16) & 0xFFu) * kLuaColorByteScale);
  FrameScript_PushNumber(L, static_cast<lua_Number>((argb >> 8) & 0xFFu) * kLuaColorByteScale);
  FrameScript_PushNumber(L, static_cast<lua_Number>(argb & 0xFFu) * kLuaColorByteScale);
}

}

const openwow::data::dbc::VehicleSeatEntry *
ResolveLuaUnitVehicleSeatEntry(openwow::game::WorldSession *session,
                               const std::string &unit_id) {
  return ResolveLuaUnitVehicleSeatEntryImpl(session, unit_id);
}

int LuaUnitName(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitName(\"unit\")");

  if (const auto *identity = ResolvePendingPlayerIdentity(session, uid)) {
    return call.string_nil_pair(identity->name);
  }
  const auto unknown_name = ResolveUnknownUnitName();
  const auto *object = ResolveUnit(session, uid);
  if (!object) {
    if (const auto cached = LookupCachedControlledUnitStats(session, ResolveUnitId(session, uid));
        cached.has_value()) {
      const auto name = cached->CachedName();
      return call.string_nil_pair(name);
    }

    const auto guid = ResolveUnitId(session, uid);
    if (guid.IsEmpty() || session == nullptr) {
      return call.nil_pair();
    }

    if (const auto resolved = ResolveCachedUnitNameByGuid(*session, guid); resolved.has_value()) {
      call.string(resolved->name);
      call.optional_string(resolved->realm.has_value()
                               ? std::optional<std::string_view>{*resolved->realm}
                               : std::nullopt);
      return 2;
    }

    return call.string_nil_pair(unknown_name);
  }

  if (const auto resolved =
          ResolveLiveObjectName(*session, *object, true);
      resolved.has_value()) {
    call.string(resolved->name);
    call.optional_string(resolved->realm.has_value()
                             ? std::optional<std::string_view>{*resolved->realm}
                             : std::nullopt);
    return 2;
  }

  return call.string_nil_pair(unknown_name);
}

int LuaUnitLevel(lua_State *L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitLevel(\"unit\")");
  auto *session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto *unit = ResolveUnit(session, uid);
  if (unit) {
    int level = static_cast<int>(unit->GetUInt32(UNIT_FIELD_LEVEL));
    const auto *player = session ? session->objects().GetLocalPlayer() : nullptr;
    if (player && player != unit) {
      auto reaction = GetUnitReaction(player, unit, GetDbcLoader(L));
      bool skull = false;
      if (reaction <= openwow::game::ReactionType::kHostile) {
        int player_level = static_cast<int>(player->GetUInt32(UNIT_FIELD_LEVEL));
        if (player_level <= level - 10)
          skull = true;
      }
      if (!skull && !unit->IsPlayer() && session) {
        auto entry = unit->GetEntry();
        if (entry != 0) {
          const auto *tmpl = session->query_cache().GetCreatureTemplate(entry);
          if (tmpl && (tmpl->type_flags & 0x04) != 0)
            skull = true;
        }
      }
      if (skull) {
        lua_pushnumber(L, -1.0);
        return 1;
      }

      if (level > 0) {
        level = openwow::game::ApplyHostileDrunkLevelMask(
            static_cast<const ::openwow::game::CGPlayer_C&>(*player),
            static_cast<const ::openwow::game::CGUnit_C&>(*unit), level);
      }
    }
    lua_pushnumber(L, static_cast<lua_Number>(level));
    return 1;
  }
  if (const auto cached = LookupCachedGroupMemberStats(session, uid)) {
    if ((cached->available_mask & openwow::game::GroupUpdateFlag::kLevel) != 0) {
      lua_pushnumber(L, static_cast<lua_Number>(cached->stats.level));
      return 1;
    }

    lua_pushnumber(L, 0);
    return 1;
  }
  if (session) {
    openwow::game::ObjectGuid guid;
    LookupCachedGroupMemberStats(session, uid, &guid);
    if (IsTrackedGroupMemberGuid(guid)) {
      lua_pushnumber(L, 0);
      return 1;
    }
  }
  lua_pushnumber(L, 0);
  return 1;
}

int LuaUnitClass(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitClass(\"unit\")");
  const auto info = ResolveLuaUnitClassInfo(L, session, uid);
  if (!info.has_value()) {
    return call.nil_pair();
  }

  return call.string_pair(info->display_name, info->file_token);
}

int LuaUnitHasRelicSlot(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid =
      call.require_string(1, "Usage: UnitHasRelicSlot(\"unit\")");
  if (session == nullptr) {
    return call.nil();
  }

  const auto guid = ResolveUnitId(session, uid);
  const auto *unit = session->objects().GetUnit(guid);
  if (unit == nullptr || !unit->IsPlayer()) {
    return call.nil();
  }

  const auto *dbc = call.dbc();
  const auto *class_entry =
      dbc != nullptr
          ? dbc->chr_classes().LookupEntry(GetUnitClass(unit))
          : nullptr;
  constexpr std::uint32_t kChrClassRelicSlotFlag = 0x8u;
  return call.wow_bool(
      class_entry != nullptr &&
      (class_entry->class_flags & kChrClassRelicSlotFlag) != 0u);
}

int LuaUnitRace(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitRace(\"unit\")");

  std::uint8_t race_id = 0;
  std::uint8_t gender_id = 2;

  if (openwow::text::EqualsIgnoreCaseAscii(uid, "player")) {
    if (const auto *identity = ResolvePendingPlayerIdentity(session, uid)) {
      race_id = identity->race_id;
      gender_id = identity->gender;
    } else {
      const auto *player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
      if (player == nullptr) {
        return call.nil_pair();
      }
      race_id = player->State().GetRace();
      gender_id = player->State().GetGender();
    }
  } else {
    if (session == nullptr) {
      return call.nil_pair();
    }

    const auto guid = ResolveUnitId(session, uid);
    if (guid.IsEmpty()) {
      return call.nil_pair();
    }

    if (const auto *unit = session->objects().GetUnit(guid); unit != nullptr) {
      race_id = GetUnitRace(unit);
      gender_id = GetUnitGender(unit);
    } else if (const auto *name_entry = session->objects().GetNameEntry(guid);
               name_entry != nullptr) {
      race_id = name_entry->race;
      gender_id = name_entry->gender;
    } else {
      return call.nil_pair();
    }
  }

  const auto display_name = LookupRaceDisplayName(L, race_id, gender_id);
  const auto file_token = LookupRaceFileToken(L, race_id);

  if (display_name.empty() || file_token.empty()) {
    return call.nil_pair();
  }

  return call.string_pair(display_name, file_token);
}

int LuaUnitSex(lua_State *L) {
  const LuaCallFrame call{L};
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: UnitSex(\"unit\")");
  }
  auto *session = call.world_session();

  const auto uid = UnitIdArg(L, 1);

  static constexpr int kGenderToLuaSex[] = {2, 3, 1};
  auto mapGender = [](std::uint8_t g) -> lua_Number {
    return static_cast<lua_Number>(g < 3 ? kGenderToLuaSex[g] : 1);
  };

  if (const auto *identity = ResolvePendingPlayerIdentity(session, uid)) {
    return call.number(mapGender(identity->gender));
  }

  auto guid = ResolveUnitId(session, uid);

  if (!guid.IsEmpty() && session) {
    if (const auto *unit = session->objects().GetUnit(guid)) {
      return call.number(mapGender(GetUnitGender(unit)));
    }
    if (const auto *entry = session->objects().GetNameEntry(guid)) {
      return call.number(mapGender(entry->gender));
    }
  }

  return call.number(1);
}

int LuaUnitHealth(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitHealth(\"unit\")");
  const auto *object = ResolveUnit(session, uid);
  if (object != nullptr && object->IsUnit()) {
    const auto &unit = static_cast<const openwow::game::CGUnit_C &>(*object);
    const auto dead_state = ResolveLuaDeadState(session, &unit);
    if (dead_state.feign_death && !dead_state.is_party_or_raid_unit) {
      return call.number(0);
    }
    return call.number(static_cast<lua_Number>(unit.State().GetHealth()));
  }
  if (const auto cached = LookupLegacyTrackedUnitCacheSlice(session, uid)) {
    if (cached->IsControlledUnit()) {
      return call.number(static_cast<lua_Number>(cached->CachedCurrentHealth()));
    }

    if ((cached->cached.available_mask & openwow::game::GroupUpdateFlag::kCurHp) != 0) {
      return call.number(static_cast<lua_Number>(cached->CachedCurrentHealth()));
    }
    if ((cached->cached.available_mask & openwow::game::GroupUpdateFlag::kStatus) != 0) {
      const bool dead =
          (cached->cached.stats.status & openwow::game::GroupMemberStatus::kDead) != 0;
      return call.number(dead ? 0.0 : 1.0);
    }
  }
  if (session) {
    openwow::game::ObjectGuid guid;
    LookupCachedGroupMemberStats(session, uid, &guid);
    if (IsTrackedGroupMemberGuid(guid)) {
      return call.number(1.0);
    }
  }
  return call.number(0);
}

int LuaUnitHealthMax(lua_State *L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitHealthMax(\"unit\")");
  auto *session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto *unit = ResolveLiveScriptUnit(session, uid);
  if (unit) {
    lua_pushnumber(L, static_cast<lua_Number>(unit->State().GetMaxHealth()));
    return 1;
  }
  if (const auto cached = LookupLegacyTrackedUnitCacheSlice(session, uid)) {
    if (cached->IsControlledUnit()) {
      lua_pushnumber(L, static_cast<lua_Number>(cached->CachedMaxHealth()));
      return 1;
    }

    if ((cached->cached.available_mask & openwow::game::GroupUpdateFlag::kMaxHp) != 0) {
      lua_pushnumber(L, static_cast<lua_Number>(cached->CachedMaxHealth()));
      return 1;
    }

    lua_pushnumber(L, 1.0);
    return 1;
  }
  if (session) {
    openwow::game::ObjectGuid guid;
    LookupCachedGroupMemberStats(session, uid, &guid);
    if (IsTrackedGroupMemberGuid(guid)) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }
  lua_pushnumber(L, 0);
  return 1;
}

int LuaUnitPower(lua_State *L) {
  return LuaUnitPowerCommon(L, "Usage: UnitPower(\"unit\"[, type])", false);
}

int LuaUnitPowerMax(lua_State *L) {
  return LuaUnitPowerCommon(L, "Usage: UnitPowerMax(\"unit\"[, type])", true);
}

int LuaUnitMana(lua_State *L) {
  return LuaUnitPowerCommon(L, "Usage: UnitPower(\"unit\"[, type])", false);
}

int LuaUnitManaMax(lua_State *L) {
  return LuaUnitPowerCommon(L, "Usage: UnitPowerMax(\"unit\"[, type])", true);
}

int LuaUnitPowerType(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: UnitPowerType(\"unit\"[, index])");
  }

  const auto display_index = ParseLuaUnitPowerDisplayIndex(L);
  auto *session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto *object = ResolveUnit(session, uid);
  if (object != nullptr && object->IsUnit()) {
    const auto &unit = static_cast<const openwow::game::CGUnit_C &>(*object);
    if (const auto display = LookupLuaUnitVehiclePowerDisplay(L, session, unit, display_index)) {
      return PushLuaUnitVehiclePowerDisplayResult(L, *display);
    }

    if (display_index == 0) {
      return PushLuaUnitPowerTypeResult(L, static_cast<std::int32_t>(GetUnitPowerType(&unit)));
    }
  }

  if (display_index == 0) {
    if (const auto cached = LookupLegacyTrackedUnitCacheSlice(session, uid)) {
      if (cached->IsControlledUnit()) {
        return PushLuaUnitPowerTypeResult(L, static_cast<std::int32_t>(cached->CachedPowerType()));
      }

      if ((cached->cached.available_mask & openwow::game::GroupUpdateFlag::kPowerType) != 0) {
        return PushLuaUnitPowerTypeResult(L, static_cast<std::int32_t>(cached->CachedPowerType()));
      }
    }
  }

  return PushLuaUnitPowerTypeEmptyResult(L);
}

int LuaUnitExists(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  const auto *unit = ResolveUnit(session, uid);
  if (unit) {
    return call.number(1);
  }

  openwow::game::ObjectGuid guid;
  LookupCachedGroupMemberStats(session, uid, &guid);
  return session != nullptr && IsTrackedGroupUnitGuid(session->objects(), guid)
             ? call.number(1)
             : call.nil();
}

int LuaUnitIsDead(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitIsDead(\"unit\")");
  const auto state = ResolveLuaDeadState(session, uid);
  if (!state.found) {
    return call.nil();
  }

  return call.wow_bool(state.dead ||
                       (state.feign_death && !state.is_party_or_raid_unit));
}

int LuaUnitIsDeadOrGhost(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitIsDeadOrGhost(\"unit\")");
  const auto state = ResolveLuaDeadState(session, uid);
  if (!state.found) {
    return call.nil();
  }
  return call.wow_bool(state.dead || state.ghost ||
                       (state.feign_death && !state.is_party_or_raid_unit));
}

int LuaUnitIsPlayer(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  const auto *unit = ResolveUnit(session, uid);
  if (unit) {
    return call.wow_bool(UnitIsPlayer(unit));
  }

  openwow::game::ObjectGuid guid;
  LookupCachedGroupMemberStats(session, uid, &guid);
  return call.wow_bool(IsTrackedGroupMemberGuid(guid));
}

int LuaUnitIsEnemy(lua_State *L) {
  const LuaCallFrame call{L};
  const auto units = ResolveLiveScriptUnitPair(call, "Usage: UnitIsEnemy(\"unit\", \"otherUnit\")");
  return call.wow_bool(units.first != nullptr && units.second != nullptr &&
                       UnitIsEnemy(units.first, units.second, GetDbcLoader(L)));
}

int LuaUnitIsFriend(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid1 = call.require_string(1, "Usage: UnitIsFriend(\"unit\", \"otherUnit\")");
  const auto uid2 = call.require_string(2, "Usage: UnitIsFriend(\"unit\", \"otherUnit\")");
  auto *session = call.world_session();
  const auto *u1 = ResolveUnit(session, uid1);
  const auto *u2 = ResolveUnit(session, uid2);
  bool is_friend = UnitIsFriend(u1, u2, GetDbcLoader(L));
  if (!is_friend && session != nullptr) {
    const auto guid1 = ResolveUnitId(session, uid1);
    const auto guid2 = ResolveUnitId(session, uid2);
    auto &group_system = openwow::game::GroupSystem::Get();
    if (openwow::text::EqualsIgnoreCaseAscii(uid1, "player")) {
      is_friend = group_system.IsActivePlayerPartyOrRaidUnitGuid(
          session->objects(), guid2.GetRawValue());
    } else if (openwow::text::EqualsIgnoreCaseAscii(uid2, "player")) {
      is_friend = group_system.IsActivePlayerPartyOrRaidUnitGuid(
          session->objects(), guid1.GetRawValue());
    }
  }
  return call.wow_bool(is_friend);
}

int LuaUnitIsUnit(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid1 = call.require_string(1, "Usage: UnitIsUnit(\"unit\", \"otherUnit\")");
  const auto uid2 = call.require_string(2, "Usage: UnitIsUnit(\"unit\", \"otherUnit\")");
  if (openwow::text::EqualsIgnoreCaseAscii(uid1, uid2)) {
    return call.number(1.0);
  }

  auto *session = call.world_session();
  const auto g1 = ResolveUnitId(session, uid1);
  const auto g2 = ResolveUnitId(session, uid2);
  return call.wow_bool(!g1.IsEmpty() && g1 == g2);
}

int LuaUnitIsConnected(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitIsConnected(\"unit\")");
  const auto *unit = ResolveUnit(session, uid);
  if (unit) {
    return call.number(1.0);
  }
  if (const auto cached = LookupCachedControlledUnitStats(session, ResolveUnitId(session, uid));
      cached.has_value()) {
    return call.number(1.0);
  }

  if (session) {
    auto guid = ResolveUnitId(session, uid);
    if (!guid.IsEmpty()) {
      auto member = session->group().GetMember(guid);
      if (member && (member->online_status & 0x01) != 0) {
        return call.number(1.0);
      }
    }
  }
  return call.nil();
}

int LuaUnitIsAFK(lua_State *L) {
  return LuaUnitHasStatus(L, "Usage: UnitIsAFK(\"unit\")", LuaUnitStatusFlag::kAfk);
}

int LuaUnitIsDND(lua_State *L) {
  return LuaUnitHasStatus(L, "Usage: UnitIsDND(\"unit\")", LuaUnitStatusFlag::kDnd);
}

int LuaUnitIsCorpse(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  const auto guid = ResolveUnitId(session, uid);
  if (session != nullptr && !guid.IsEmpty()) {
    const auto *obj = session->objects().GetObjectByGUID(guid);
    return call.wow_bool(obj != nullptr && obj->IsCorpse());
  }
  return call.nil();
}

int LuaUnitIsGhost(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitIsGhost(\"unit\")");
  const auto state = ResolveLuaDeadState(session, uid);
  if (!state.found) {
    return call.nil();
  }

  return call.wow_bool(state.ghost);
}

int LuaUnitIsPVP(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitIsPVP(\"unit\")");
  const auto guid = ResolveUnitId(session, uid);
  if (session != nullptr && !guid.IsEmpty()) {
    if (const auto *unit = session->objects().GetUnit(guid); unit != nullptr) {
      return call.wow_bool(unit->State().IsPvP());
    }

    if (const auto cached = session->party_stats().GetCachedMember(guid.GetRawValue());
        cached.has_value() &&
        (cached->available_mask & openwow::game::GroupUpdateFlag::kStatus) != 0) {
      return call.wow_bool((cached->stats.status & openwow::game::GroupMemberStatus::kPvp) != 0);
    }
  }

  return call.nil();
}

int LuaUnitIsPVPFreeForAll(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitIsPVPFreeForAll(\"unit\")");
  const auto *unit = ResolveUnitObject(ResolveUnit(session, uid));
  if (!unit) {
    return call.nil();
  }
  return call.wow_bool(unit->State().IsPvPFreeForAll());
}

int LuaUnitPlayerControlled(lua_State *L) {
  const LuaCallFrame call{L};
  const auto *const unit = ResolveLiveScriptUnit(call.world_session(), UnitIdArg(L, 1));
  return call.wow_bool(
      unit != nullptr && unit->Interaction().IsPlayerControlled());
}

int LuaUnitCanAttack(lua_State *L) {
  const LuaCallFrame call{L};
  const auto units =
      ResolveLiveScriptUnitPair(call, "Usage: UnitCanAttack(\"unit\", \"otherUnit\")");
  return call.wow_bool(units.first != nullptr && units.second != nullptr &&
                        units.first->Interaction().CanAttackSpellTarget(*units.second));
}

int LuaUnitCanCooperate(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid1 = call.require_string(1, "Usage: UnitCanCooperate(\"unit\", \"otherUnit\")");
  const auto uid2 = call.require_string(2, "Usage: UnitCanCooperate(\"unit\", \"otherUnit\")");
  auto *session = call.world_session();
  const auto *u1 = ResolveUnit(session, uid1);
  const auto *u2 = ResolveUnit(session, uid2);
  bool can_cooperate = UnitCanCooperate(u1, u2, GetDbcLoader(L));

  if (!can_cooperate && session != nullptr) {
    const auto guid1 = ResolveUnitId(session, uid1);
    const auto guid2 = ResolveUnitId(session, uid2);
    auto &group_system = openwow::game::GroupSystem::Get();
    if (openwow::text::EqualsIgnoreCaseAscii(uid1, "player")) {
      can_cooperate = group_system.IsActivePlayerPartyOrRaidUnitGuid(
          session->objects(), guid2.GetRawValue());
    } else if (openwow::text::EqualsIgnoreCaseAscii(uid2, "player")) {
      can_cooperate = group_system.IsActivePlayerPartyOrRaidUnitGuid(
          session->objects(), guid1.GetRawValue());
    }
  }
  return call.wow_bool(can_cooperate);
}

namespace {

int LuaUnitAuraCommon(lua_State* L, const char* usage,
                      const std::uint8_t forced_type) {
  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "%s", usage);
  }

  openwow::game::AuraFilter filter;
  if (!openwow::game::ParseAuraFilter(L, 2, &filter)) {
    return luaL_error(L, "%s", usage);
  }

  if (forced_type != 0u) {
    filter.flags = static_cast<std::uint8_t>((filter.flags & 0xfcU) |
                                             forced_type);
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto guid = ResolveUnitId(session, UnitIdArg(L, 1));
  return openwow::game::PushUnitAuraQueryResult(
      *session, guid.GetRawValue(), filter, L);
}

}

int LuaUnitBuff(lua_State *L) {
  return LuaUnitAuraCommon(
      L,
      "Usage: UnitBuff(\"unit\", [index] or [\"name\", \"rank\"][, \"filter\"])",
      0x01u);
}

int LuaUnitDebuff(lua_State *L) {
  return LuaUnitAuraCommon(
      L,
      "Usage: UnitDebuff(\"unit\", [index] or [\"name\", \"rank\"][, \"filter\"])",
      0x02u);
}

int LuaUnitAura(lua_State *L) {
  return LuaUnitAuraCommon(
      L,
      "Usage: UnitAura(\"unit\", [index] or [\"name\", \"rank\"][, \"filter\"])",
      0u);
}

int LuaGetPlayerMapPosition(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetPlayerMapPosition(\"player\")");
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }

  const auto uid = UnitIdArg(L, 1);
  const auto guid = ResolveUnitId(session, uid);
  if (!CanQueryLegacyPlayerMapPosition(*session, guid)) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }

  if (const auto coord = ResolveLivePlayerMapPosition(L, *session, guid);
      coord.has_value() && coord->valid) {
    lua_pushnumber(L, static_cast<lua_Number>(coord->x));
    lua_pushnumber(L, static_cast<lua_Number>(coord->y));
    return 2;
  }

  if (const auto coord = ResolveCachedPlayerMapPosition(L, *session, guid);
      coord.has_value() && coord->valid) {
    lua_pushnumber(L, static_cast<lua_Number>(coord->x));
    lua_pushnumber(L, static_cast<lua_Number>(coord->y));
    return 2;
  }

  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  return 2;
}

int LuaUnitGUID(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitGUID(\"unit\")");
  const auto guid = ResolveUnitId(session, uid);

  if (guid.IsEmpty() || guid.GetRawValue() == 0xFFFFFFFFFFFFFFFEull) {
    return call.nil();
  }

  auto out_guid = guid;
  if ((guid.IsCreature() || guid.IsVehicle()) && session != nullptr) {
    const auto *tmpl =
        session->query_cache().GetCreatureTemplate(guid.GetEntry());
    if (tmpl != nullptr && (tmpl->type_flags & 0x4000) != 0) {
      out_guid = openwow::game::ObjectGuid(
          guid.GetRawValue() & 0xFFFFFFFFFF000000ull);
    }
  }

  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%016llX",
                static_cast<unsigned long long>(out_guid.GetRawValue()));
  return call.string(buf);
}

int LuaUnitFactionGroup(lua_State *L) {
  const LuaCallFrame call{L};
  const auto unit_id = call.require_string(1, "Usage: UnitFactionGroup(\"unit\")");

  auto *const session = call.world_session();
  const auto *const dbc = call.dbc();
  if (session == nullptr || dbc == nullptr) {
    return call.nil_pair();
  }

  const auto *const faction_template = ResolveFactionTemplateForUnit(*dbc, *session, unit_id);
  if (faction_template == nullptr) {
    return call.nil_pair();
  }

  const auto faction_group = ResolveFactionGroupStrings(*dbc, *faction_template);
  if (!faction_group.valid) {
    return call.nil_pair();
  }

  return call.string_pair(faction_group.token, faction_group.localized_name);
}

int LuaUnitClassification(lua_State *L) {
  const LuaCallFrame call{L};
  const auto *unit = ResolveLiveScriptUnit(call.world_session(), UnitIdArg(L, 1));
  if (!unit || unit->IsPlayer()) {
    return call.string("normal");
  }

  static constexpr const char *kClassificationStrings[] = {
      "normal", "elite", "rareelite", "worldboss", "rare", "trivial",
  };
  const auto rank = unit->State().GetClassificationRank();
  const auto index = static_cast<std::uint32_t>(rank);
  if (index < std::size(kClassificationStrings)) {
    return call.string(kClassificationStrings[index]);
  }
  return call.string("normal");
}

[[nodiscard]] const openwow::data::dbc::CreatureTypeEntry *
ResolveLuaUnitCreatureTypeEntry(lua_State *L,
                                const openwow::game::WorldSession *session,
                                const openwow::game::CGUnit_C &unit) {
  const auto *dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
  if (dbc == nullptr) {
    dbc = GetDbcLoader(L);
  }
  if (dbc == nullptr) {
    return nullptr;
  }

  auto creature_type_id =
      unit.IsPlayer()
          ? static_cast<std::uint32_t>(openwow::game::CreatureTypeId::kHumanoid)
          : openwow::game::ResolveSpellTargetCreatureTypeId(session, unit);
  if (creature_type_id == 0u && !unit.IsPlayer()) {
    creature_type_id = static_cast<std::uint32_t>(unit.State().GetCreatureType());
  }
  if (creature_type_id == 0u) {
    return nullptr;
  }

  return dbc->creature_type().LookupEntry(creature_type_id);
}

int LuaUnitCreatureType(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: UnitCreatureType(\"unit\")");
  auto *const session = call.world_session();
  const auto *const unit = ResolveLiveScriptUnit(session, uid);
  if (!unit) {
    return call.nil();
  }

  if (const auto *entry = ResolveLuaUnitCreatureTypeEntry(L, session, *unit);
      entry != nullptr) {
    return call.string(entry->name);
  }

  return call.nil();
}

int LuaUnitXP(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: UnitXP(\"unit\")");
  auto *const session = call.world_session();
  const auto *const unit = ResolveLiveScriptUnit(session, uid);
  const auto *const player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (unit == nullptr || player == nullptr || unit->GetGuid() != player->GetGuid()) {
    return call.number(0);
  }
  return call.number(static_cast<lua_Number>(player->GetUInt32(PLAYER_XP)));
}

int LuaUnitXPMax(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: UnitXPMax(\"unit\")");
  auto *const session = call.world_session();
  const auto *const unit = ResolveLiveScriptUnit(session, uid);
  const auto *const player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (unit == nullptr || player == nullptr || unit->GetGuid() != player->GetGuid()) {
    return call.number(0);
  }
  return call.number(static_cast<lua_Number>(player->GetUInt32(PLAYER_NEXT_LEVEL_XP)));
}

int LuaUnitReaction(lua_State *L) {
  const LuaCallFrame call{L};
  const auto units = ResolveLiveScriptUnitPair(call, "Usage: UnitReaction(\"unit\", \"otherUnit\")");

  if (units.first == nullptr || units.second == nullptr) {
    return call.nil();
  }

  const auto reaction = GetUnitReaction(units.first, units.second, GetDbcLoader(L));
  return call.number(static_cast<lua_Number>(static_cast<int>(reaction) + 1));
}

int LuaUnitThreatSituation(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto unit_token = UnitIdArg(L, 1);
  if (ParseUnitId(unit_token).kind == UnitIdKind::kUnknown) {
    return luaL_error(L, "Usage: UnitThreatSituation(\"unit\" [, \"mob\"])");
  }

  const auto unit_guid = ResolveUnitId(session, unit_token);
  if (unit_guid.IsEmpty()) {
    return 0;
  }

  const auto member_guid = unit_guid.GetRawValue();
  std::uint8_t status_plus_one = 0;

  if (lua_isstring(L, 2) != 0) {
    const auto mob_token = UnitIdArg(L, 2);
    if (ParseUnitId(mob_token).kind == UnitIdKind::kUnknown) {
      return luaL_error(L, "Usage: UnitThreatSituation(\"unit\" [, \"mob\"])");
    }

    const auto mob_guid = ResolveUnitId(session, mob_token);
    const auto *const mob = session != nullptr ? session->objects().GetUnit(mob_guid) : nullptr;
    if (mob == nullptr) {
      return 0;
    }

    mob->GetGroupMemberStatus(&member_guid, &status_plus_one, nullptr, nullptr, nullptr);
  } else {
    auto &threat_system = openwow::game::ThreatSystem::Get();
    for (const auto &target_guid : threat_system.GetTargetsForUnit(unit_guid)) {
      const auto *const target =
          session != nullptr ? session->objects().GetUnit(target_guid) : nullptr;
      if (target == nullptr) {
        threat_system.RemoveThreatTargetForUnit(unit_guid, target_guid);
        continue;
      }

      std::uint8_t target_status_plus_one = 0;
      target->GetGroupMemberStatus(&member_guid, &target_status_plus_one, nullptr, nullptr,
                                   nullptr);
      if (target_status_plus_one == 0) {
        continue;
      }

      if (status_plus_one <= 2) {
        status_plus_one = std::max(status_plus_one, target_status_plus_one);
      } else if (status_plus_one == 4 && target_status_plus_one == 3) {
        status_plus_one = 3;
      }
    }
  }

  if (status_plus_one == 0) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(static_cast<int>(status_plus_one) - 1));
  return 1;
}

int LuaUnitDetailedThreatSituation(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto unit_token = UnitIdArg(L, 1);
  const auto mob_token = UnitIdArg(L, 2);
  if (ParseUnitId(unit_token).kind == UnitIdKind::kUnknown ||
      ParseUnitId(mob_token).kind == UnitIdKind::kUnknown) {
    return luaL_error(L, "Usage: UnitDetailedThreatSituation(\"unit\" [, \"mob\"])");
  }

  const auto unit_guid = ResolveUnitId(session, unit_token);
  const auto mob_guid = ResolveUnitId(session, mob_token);
  const auto *const mob = session != nullptr ? session->objects().GetUnit(mob_guid) : nullptr;
  if (unit_guid.IsEmpty() || mob == nullptr) {
    return 0;
  }

  const auto member_guid = unit_guid.GetRawValue();
  std::uint8_t status_plus_one = 0;
  std::uint8_t raw_percent = 0;
  float scaled_percent = 0.0f;
  std::uint32_t threat_value = 0;
  mob->GetGroupMemberStatus(&member_guid, &status_plus_one, &raw_percent, &scaled_percent,
                            &threat_value);
  if (status_plus_one == 0) {
    return 0;
  }

  if (openwow::game::ThreatSystem::Get().GetHighestThreatGuid(mob_guid) == unit_guid) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<int>(status_plus_one) - 1));
  lua_pushnumber(L, static_cast<lua_Number>(scaled_percent));
  lua_pushnumber(L, static_cast<lua_Number>(raw_percent));
  lua_pushnumber(L, static_cast<lua_Number>(threat_value));
  return 5;
}

int LuaUnitGroupRolesAssigned(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  std::uint32_t role_flags = 0;

  if (session != nullptr) {
    const auto guid = ResolveUnitId(session, uid);
    if (!guid.IsEmpty()) {
      role_flags = openwow::game::GroupSystem::Get().GetRoleFlags(guid.GetRawValue());
    }
  }

  lua_pushboolean(L, (role_flags & openwow::game::kGroupRoleFlagDamager) != 0);
  lua_pushboolean(L, (role_flags & openwow::game::kGroupRoleFlagTank) != 0);
  lua_pushboolean(L, (role_flags & openwow::game::kGroupRoleFlagHealer) != 0);
  return 3;
}

int LuaUnitHasVehicleUI(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  const auto *seat_entry = ResolveLuaUnitVehicleSeatEntry(session, uid);
  return call.boolean(seat_entry != nullptr && (seat_entry->flags & 0x20000000u) != 0u);
}

int LuaUnitTargetsVehicleInRaidUI(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  const auto *seat_entry = ResolveLuaUnitVehicleSeatEntry(session, uid);
  return call.boolean(seat_entry != nullptr && (seat_entry->transition_flags & 0x8u) != 0u);
}

int LuaUnitInVehicle(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: UnitInVehicle(\"unit\")");

  return call.wow_bool(EvaluateLuaLegacyVehicleQuery(
      call.world_session(), uid, LuaLegacyVehicleQuery::kAttachedSeat));
}

int LuaUnitInVehicleControlSeat(lua_State *L) {
  const LuaCallFrame call{L};
  const auto *const seat_entry =
      ResolveLuaUnitVehicleSeatEntry(call.world_session(), UnitIdArg(L, 1));
  return call.boolean(seat_entry != nullptr && (seat_entry->flags & 0x800u) != 0u);
}

int LuaUnitUsingVehicle(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: UnitUsingVehicle(\"unit\")");

  return call.wow_bool(EvaluateLuaLegacyVehicleQuery(
      call.world_session(), uid, LuaLegacyVehicleQuery::kUsingVehicle));
}

int LuaUnitIsCharmed(lua_State *L) {
  const LuaCallFrame call{L};
  const auto *const unit = ResolveLiveScriptUnit(call.world_session(), UnitIdArg(L, 1));
  return call.wow_bool(unit != nullptr && !unit->GetGuidField(UNIT_FIELD_CHARMEDBY).IsEmpty());
}

int LuaUnitIsPossessed(lua_State *L) {
  const LuaCallFrame call{L};
  const auto *const unit = ResolveLiveScriptUnit(call.world_session(), UnitIdArg(L, 1));
  return call.wow_bool(unit != nullptr && unit->State().IsPossessed());
}

int LuaUnitOnTaxi(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: UnitOnTaxi(\"unit\")");
  const auto *const unit = ResolveLiveScriptUnit(call.world_session(), uid);
  return call.wow_bool(unit != nullptr && unit->State().IsTaxiFlight());
}

int LuaUnitInParty(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  if (session == nullptr) {
    return call.nil();
  }
  const auto guid = ResolveUnitId(session, uid);
  return call.wow_bool(IsActivePlayerOrTrackedPartyMemberGuid(*session, guid));
}

int LuaUnitInRaid(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  const auto raid_index = ResolveTrackedRaidRosterIndex(session, uid);
  if (raid_index.has_value()) {
    return call.number(static_cast<lua_Number>(*raid_index));
  }
  return call.nil();
}

int LuaUnitIsPartyLeader(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  if (session == nullptr) {
    return call.nil();
  }

  const auto guid = ResolveUnitId(session, uid);
  if (guid.IsEmpty()) {
    return call.nil();
  }

  constexpr std::uint32_t kPlayerFlagGroupLeader = 0x01u;

  bool has_group_leader_flag = false;
  if (const auto *unit = session->objects().GetUnit(guid); unit != nullptr && unit->IsPlayer()) {
    has_group_leader_flag = (unit->GetUInt32(PLAYER_FLAGS) & kPlayerFlagGroupLeader) != 0;
  }

  const bool is_cached_leader =
      guid.GetRawValue() == openwow::game::GroupSystem::Get().GetLeaderGuid();
  return call.wow_bool(has_group_leader_flag || is_cached_leader);
}

int LuaGetUnitSpeed(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: GetUnitSpeed(\"unit\")");
  const auto *unit = ResolveUnit(session, uid);
  if (!unit) {
    return call.number(0);
  }
  return call.number(unit->GetSpeed(kSpeedRun));
}

int LuaUnitResistance(lua_State *L) {
  const LuaCallFrame call{L};
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: UnitResistance(\"unit\", resistanceIndex)");
  }

  const auto uid = UnitIdArg(L, 1);

  auto *session = call.world_session();

  const auto school = static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(lua_tonumber(L, 2)));

  constexpr std::uint32_t kHighestResistanceSchool = 6u;
  if (school > kHighestResistanceSchool)
    return luaL_error(L, "Invalid resistance index in UnitResistance");

  const auto *unit = ResolveLiveScriptUnit(session, uid);

  std::int32_t base_value = 0;
  std::int32_t effective = 0;
  std::int32_t pos_buff = 0;
  std::int32_t neg_buff = 0;

  if (unit) {
    const bool is_active_player =
        (unit->GetGuid() == openwow::game::CGObject_C::GetActivePlayerGuid());
    if (is_active_player) {
      const auto rv = unit->State().GetResistanceDisplayValues(
          static_cast<std::uint8_t>(school));
      base_value = rv.base_value;
      effective = rv.clamped_total;
      pos_buff = rv.positive_modifier;
      neg_buff = rv.negative_modifier;
    } else {
      const std::int32_t total = unit->State().GetResistance(
          static_cast<std::uint8_t>(school));
      const std::int32_t clamped = total > 0 ? total : 0;
      base_value = clamped;
      effective = clamped;
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(base_value));
  lua_pushnumber(L, static_cast<lua_Number>(effective));
  lua_pushnumber(L, static_cast<lua_Number>(pos_buff));
  lua_pushnumber(L, static_cast<lua_Number>(neg_buff));
  return 4;
}

int LuaGetUnitMaxHealthModifier(lua_State *L) {
  const LuaCallFrame call{L};
  const auto token = call.require_string(1, "Usage: GetUnitMaxHealthModifier(\"unit\")");
  return call.number(LuaDerivedStatQuery(call.state(), token).max_health_modifier());
}

int LuaCheckInteractDistance(lua_State *L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: CheckInteractDistance(\"unit\", distIndex)");
  }

  auto *session = GetWorldSession(L);
  const auto target_guid = ResolveUnitId(session, UnitIdArg(L, 1));
  const auto *unit = session != nullptr ? session->objects().GetUnit(target_guid) : nullptr;
  const auto *player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  const auto distance_index = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 2));
  if (player != nullptr && unit != nullptr && distance_index >= 1u &&
      distance_index <= kLuaCheckInteractDistanceThresholdsSq.size()) {

    const float dx = unit->GetX() - player->GetX();
    const float dy = unit->GetY() - player->GetY();
    const float dz = unit->GetZ() - player->GetZ();
    const float distance_sq = dx * dx + dy * dy + dz * dz;
    if (distance_sq < kLuaCheckInteractDistanceThresholdsSq[distance_index - 1u]) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

int LuaUnitIsTappedByPlayer(lua_State *L) {
  constexpr std::uint32_t UNIT_DYNFLAG_TAPPED_BY_PLAYER = 0x08;
  return LuaUnitDynamicFlagPredicate(L, "Usage: UnitIsTappedByPlayer(\"unit\")",
                                     UNIT_DYNFLAG_TAPPED_BY_PLAYER);
}

int LuaUnitIsTappedByAllThreatList(lua_State *L) {
  constexpr std::uint32_t UNIT_DYNFLAG_TAPPED_BY_ALL_THREAT = 0x80;
  return LuaUnitDynamicFlagPredicate(L, "Usage: UnitIsTappedByAllThreatList(\"unit\")",
                                     UNIT_DYNFLAG_TAPPED_BY_ALL_THREAT);
}

int LuaUnitPlayerOrPetInParty(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  if (session == nullptr) {
    return call.nil();
  }

  const auto guid = ResolveUnitId(session, uid);
  return call.wow_bool(IsTrackedPartyPlayerOrPetGuid(session->objects(), guid));
}

int LuaUnitPlayerOrPetInRaid(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  if (session == nullptr) {
    return call.nil();
  }

  const auto guid = ResolveUnitId(session, uid);
  return call.wow_bool(IsTrackedRaidPlayerOrPetGuid(session->objects(), guid));
}

int LuaUnitIsVisible(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto guid = ResolveUnitId(session, uid);
  const auto *unit = session ? session->objects().GetUnit(guid) : nullptr;
  lua_pushwowbool(L, unit);
  return 1;
}

int LuaGetThreatStatusColor(lua_State *L) {
  PushLuaThreatStatusColor(L, ResolveLuaThreatStatusColorArgb(lua_tonumber(L, 1)));
  return 3;
}

int LuaGetPlayerFacing(lua_State *L) {
  auto* session = GetWorldSession(L);
  lua_pushnumber(L, session != nullptr
                        ? static_cast<lua_Number>(
                              openwow::game::Movement_QueryScriptPlayerFacing(*session))
                        : 0.0);
  return 1;
}

int LuaGetAttackPowerForStat(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(
        L, "Usage: Script_GetAttackPowerForStat(stat, value)");
  }

  auto *session = GetWorldSession(L);
  const auto *player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (player == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto stat_index = openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(openwow::ui::TruncateLuaNumberToI32(
          lua_tonumber(L, 1))) -
      1u);
  const auto stat_value =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 2));

  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto class_id = player->State().GetClass();
  const auto *class_entry =
      dbc->chr_classes().LookupEntry(static_cast<std::uint32_t>(class_id));
  if (class_entry == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const bool is_agility_class = (class_entry->primary_stat_type == 1);
  const int effective = std::max(stat_value - 10, 0);
  int result = 0;

  if (stat_index == 0) {

    result = is_agility_class ? effective : 2 * effective;
  } else if (stat_index == 1) {

    if (is_agility_class) {
      result = effective;
    }

    const std::uint8_t form_id = player->State().SuppressesCurrentFormSpellQueries()
                                     ? 0
                                     : player->Animation().GetShapeshiftForm();

    if (form_id != 0) {
      const auto *form_entry =
          dbc->spell_shapeshift_form().LookupEntry(
              static_cast<std::uint32_t>(form_id));
      if (form_entry != nullptr &&
          (form_entry->flags &
           openwow::data::dbc::kShapeshiftFormFlagApFromAgility) != 0) {
        result += stat_value;
      }
    }
  }

  lua_pushnumber(L, static_cast<double>(result));
  return 1;
}

int LuaGetPowerRegen(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 2;
  }
  const auto *player = session->objects().GetLocalPlayerTyped();
  if (!player) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 2;
  }

  constexpr float kLuaRegenBias = 0.001f;
  const auto power_type = player->State().GetPowerType();
  lua_pushnumber(L, static_cast<lua_Number>(
                        player->GetPowerRegenRate(power_type) + kLuaRegenBias));
  lua_pushnumber(L, static_cast<lua_Number>(
                        player->GetPowerRegenRateInterrupted(power_type) +
                        kLuaRegenBias));
  return 2;
}

int LuaGetResSicknessDuration(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player =
      session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  const auto *dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
  if (player == nullptr || dbc == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto *race = dbc->chr_races().LookupEntry(player->State().GetRace());
  const auto *spell = race != nullptr
                          ? dbc->spell().LookupEntry(
                                race->resurrection_sickness_spell_id)
                          : nullptr;
  const auto *duration_entry = spell != nullptr
                                   ? dbc->spell_duration().LookupEntry(
                                         spell->duration_index)
                                   : nullptr;
  if (duration_entry == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const std::int64_t scaled_duration =
      static_cast<std::int64_t>(duration_entry->duration) +
      static_cast<std::int64_t>(player->State().GetLevel()) *
          static_cast<std::int64_t>(duration_entry->duration_per_level);
  std::int32_t duration_ms = static_cast<std::int32_t>(std::clamp<std::int64_t>(
      scaled_duration, std::numeric_limits<std::int32_t>::min(),
      std::numeric_limits<std::int32_t>::max()));
  if (duration_entry->max_duration <= duration_ms) {
    duration_ms = duration_entry->max_duration;
  }

  const auto spell_family =
      ::openwow::game::detail::ResolveSpellModifierFamily(*player, dbc);
  (void)session->aura().ApplySpellModifierDeltas(
      spell_family, *spell, openwow::game::SpellModOp::kDuration,
      &duration_ms);

  if (duration_ms > 999) {
    const auto text = openwow::game::FormatRoundedGeneralDurationText(
        static_cast<std::uint32_t>(duration_ms));
    lua_pushlstring(L, text.data(), text.size());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaTargetLastEnemy([[maybe_unused]] lua_State *L) {
  auto *ts = GetTargetingSystem(L);

  if (ts && GameUI_CanPerformTaintForbiddenAction())
    ts->TargetLastEnemy();
  return 0;
}

int LuaTargetLastFriend([[maybe_unused]] lua_State *L) {
  auto *ts = GetTargetingSystem(L);
  if (ts && GameUI_CanPerformTaintForbiddenAction())
    ts->TargetLastFriend();
  return 0;
}

int LuaTargetLastTarget([[maybe_unused]] lua_State *L) {
  auto *ts = GetTargetingSystem(L);
  if (ts && GameUI_CanPerformTaintForbiddenAction())
    ts->TargetLastTarget();
  return 0;
}

int LuaTargetNearest([[maybe_unused]] lua_State *L) {
  return LuaTargetNearestWithFilter(L, openwow::game::TargetFilter::kAny, true);
}

int LuaTargetNearestEnemy([[maybe_unused]] lua_State *L) {
  return LuaTargetNearestWithFilter(L, openwow::game::TargetFilter::kEnemy, true);
}

int LuaTargetNearestEnemyPlayer([[maybe_unused]] lua_State *L) {
  return LuaTargetNearestWithFilter(L, openwow::game::TargetFilter::kEnemyPlayer, true);
}

int LuaTargetNearestFriend([[maybe_unused]] lua_State *L) {
  return LuaTargetNearestWithFilter(L, openwow::game::TargetFilter::kFriend, true);
}

int LuaTargetNearestFriendPlayer([[maybe_unused]] lua_State *L) {
  return LuaTargetNearestWithFilter(L, openwow::game::TargetFilter::kFriendPlayer, true);
}

int LuaTargetNearestPartyMember([[maybe_unused]] lua_State *L) {
  return LuaTargetNearestWithFilter(L, openwow::game::TargetFilter::kParty, false);
}

int LuaTargetNearestRaidMember([[maybe_unused]] lua_State *L) {
  return LuaTargetNearestWithFilter(L, openwow::game::TargetFilter::kRaid, false);
}

int LuaUnitCanAssist(lua_State *L) {
  const LuaCallFrame call{L};
  const auto units =
      ResolveLiveScriptUnitPair(call, "Usage: UnitCanAssist(\"unit\", \"otherUnit\")");
  return call.wow_bool(units.first != nullptr && units.second != nullptr &&
                        units.first->Interaction().CanAssistSpellTarget(
                            *units.second, false));
}

int LuaUnitCharacterPoints(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: UnitCharacterPoints(\"unit\")");
  auto *const session = call.world_session();
  const auto *const unit = ResolveLiveScriptUnit(session, uid);
  const auto *const player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (unit == nullptr || player == nullptr || unit->GetGuid() != player->GetGuid()) {
    return call.number_pair(0, 0);
  }
  return call.number_pair(static_cast<lua_Number>(player->GetUInt32(PLAYER_CHARACTER_POINTS1)),
                          static_cast<lua_Number>(player->GetUInt32(PLAYER_CHARACTER_POINTS2)));
}

int LuaUnitInBattleground(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  const auto raid_index = ResolveTrackedRaidRosterIndex(session, uid);
  if (session != nullptr && raid_index.has_value() && session->group().IsBattlegroundGroup()) {
    return call.number(static_cast<lua_Number>(*raid_index));
  }
  return call.nil();
}

int LuaUnitIsFeignDeath(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitIsFeignDeath(\"unit\")");
  const auto state = ResolveLuaDeadState(session, uid);
  if (!state.found) {
    return call.nil();
  }
  return call.wow_bool(state.feign_death && state.is_party_or_raid_unit);
}

int LuaUnitIsTapped(lua_State *L) {
  constexpr std::uint32_t UNIT_DYNFLAG_OTHER_TAGGER = 0x04;
  return LuaUnitDynamicFlagPredicate(L, "Usage: UnitIsTapped(\"unit\")",
                                     UNIT_DYNFLAG_OTHER_TAGGER);
}

int LuaUnitPVPName(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  const auto *unit = ResolveUnit(session, uid);
  if (session == nullptr || unit == nullptr) {
    return call.nil();
  }

  std::string formatted;
  (void)static_cast<const ::openwow::game::CGUnit_C&>(*unit)
      .FormatNameWithPvpTitle(*session, true, formatted);
  return call.string(formatted);
}

int LuaUnitVehicleSeatInfo(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = UnitIdArg(L, 1);
  const auto *unit = ResolveUnitObject(ResolveUnit(session, uid));
  if (unit == nullptr) {
    return call.none();
  }

  const int seat_ordinal = static_cast<int>(call.number_arg_or(2, 0)) - 1;
  const auto seat_info = ResolveLuaVehicleSeatInfo(session, *unit, seat_ordinal);
  if (!seat_info.has_value()) {
    return call.none();
  }

  call.string(seat_info->label);
  call.optional_string(seat_info->passenger_name.has_value()
                           ? std::optional<std::string_view>{seat_info->passenger_name->name}
                           : std::nullopt);
  call.optional_string(seat_info->passenger_name.has_value() &&
                               seat_info->passenger_name->realm.has_value()
                           ? std::optional<std::string_view>{*seat_info->passenger_name->realm}
                           : std::nullopt);
  call.boolean(seat_info->transition_usable);
  call.boolean(seat_info->can_switch);
  return 5;
}

int LuaUnitClassBase(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitClassBase(\"unit\")");
  const auto info = ResolveLuaUnitClassInfo(L, session, uid);
  if (!info.has_value()) {
    return call.nil_pair();
  }

  return call.string_pair(info->base_name, info->file_token);
}

int LuaUnitIsSameServer(lua_State *L) {
  const LuaCallFrame call{L};
  const auto token1 = call.require_string(1, "Usage: UnitIsSameServer(\"unit\", \"otherUnit\")");
  const auto token2 = call.require_string(2, "Usage: UnitIsSameServer(\"unit\", \"otherUnit\")");

  if (openwow::core::SStrCmpNoCase(token1.c_str(), token2.c_str(),
                                kUnboundedStormStringCompare) == 0) {
    return call.number(1.0);
  }

  auto *session = call.world_session();

  const auto guid1 = ResolveUnitId(session, token1);
  const auto guid2 = ResolveUnitId(session, token2);

  if (guid1 == guid2) {
    return call.number(1.0);
  }

  std::string_view realm1, realm2;
  if (session) {
    const auto &cache = session->query_cache();
    if (const auto *info1 = cache.GetPlayerName(guid1.GetRawValue()))
      realm1 = info1->realm_name;
    if (const auto *info2 = cache.GetPlayerName(guid2.GetRawValue()))
      realm2 = info2->realm_name;
  }

  const bool has_realm1 = !realm1.empty();
  const bool has_realm2 = !realm2.empty();

  if (has_realm1 != has_realm2 ||
      (has_realm1 && has_realm2 && realm1 != realm2)) {
    return call.nil();
  }

  return call.number(1.0);
}

int LuaUnitIsTrivial(lua_State *L) {
  const LuaCallFrame call{L};
  auto *const session = call.world_session();
  const auto uid = call.require_string(1, "Usage: UnitIsTrivial(\"unit\")");
  const auto *const unit = ResolveLiveScriptUnit(session, uid);
  if (!unit) {
    return call.nil();
  }

  if (unit->IsPlayer()) {
    return call.nil();
  }

  const auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (!player) {
    return call.nil();
  }

  return call.wow_bool(openwow::game::IsLevelTrivial(
      player->State().GetLevel(), unit->State().GetLevel()));
}

int LuaUnitPVPRank(lua_State *L) {
  const LuaCallFrame call{L};
  (void)call.require_string(1, "Usage: UnitPVPRank(\"unit\")");
  return call.number(0);
}

int LuaUnitRangedAttack(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: UnitRangedAttack(\"unit\")");
  auto *session = call.world_session();
  const auto *unit = ResolveLiveScriptUnit(session, uid);
  if (!unit) {
    return call.number_pair(0, 0);
  }

  if (!unit->IsPlayer()) {
    return call.number_pair(0, 0);
  }

  const auto *player =
      session ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (!player || player->GetGuid() != unit->GetGuid()) {
    return call.number_pair(0, 0);
  }

  const auto *dbc = GetDbcLoader(L);
  int base_skill = 0;
  int modifier = 0;

  if (dbc) {
    const auto skill_line_id = ResolveRangedWeaponSkillLine(*player, *dbc);

    if (skill_line_id != 0) {
      if (auto slot = player->FindActiveSkillSlot(
              static_cast<std::uint16_t>(skill_line_id))) {
        auto skill = player->GetSkill(*slot);
        base_skill = skill.value;
        if (base_skill != 0) {
          base_skill += static_cast<std::uint16_t>(skill.step_modifier);
        }
        modifier = skill.modifier;
      }
    }

    modifier += static_cast<int>(openwow::game::ComputeCombatRatingBonus(*player, *dbc, 0));
    modifier += static_cast<int>(
        openwow::game::ComputeCombatRatingBonus(*player, *dbc, kRangedSkillBonusRating));
  }

  return call.number_pair(static_cast<lua_Number>(base_skill),
                          static_cast<lua_Number>(modifier));
}

namespace {

void PushLuaSelectionColor(lua_State *L, const std::uint32_t argb) {
  FrameScript_PushNumber(L, static_cast<lua_Number>((argb >> 16) & 0xFFu) * kLuaColorByteScale);
  FrameScript_PushNumber(L, static_cast<lua_Number>((argb >> 8) & 0xFFu) * kLuaColorByteScale);
  FrameScript_PushNumber(L, static_cast<lua_Number>(argb & 0xFFu) * kLuaColorByteScale);
  FrameScript_PushNumber(L, static_cast<lua_Number>((argb >> 24) & 0xFFu) * kLuaColorByteScale);
}

}

int LuaUnitSelectionColor(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: UnitSelectionColor(\"unit\")");
  }

  auto *session = GetWorldSession(L);
  const auto uid = UnitIdArg(L, 1);
  const auto target = ResolveUnitId(session, uid);
  using ::openwow::game::targeting::ui::DefaultUnitSelectionColor;
  using ::openwow::game::targeting::ui::ResolveUnitSelectionColor;
  using ::openwow::game::targeting::ui::UnitSelectionColorVariant;
  const auto color =
      session != nullptr
          ? ResolveUnitSelectionColor(
                *session, target,
                UnitSelectionColorVariant::kSelection)
          : DefaultUnitSelectionColor(UnitSelectionColorVariant::kSelection);
  PushLuaSelectionColor(L, color.packed_argb());
  return 4;
}

int LuaGetPlayerInfoByGUID(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetPlayerInfoByGUID(\"playerGUID\")");
  }

  const std::string guid_str = SafeLuaString(L, 1);
  std::uint64_t raw_guid = 0;
  char *end = nullptr;
  if (guid_str.size() > 2 && guid_str[0] == '0' && (guid_str[1] == 'x' || guid_str[1] == 'X')) {
    raw_guid = std::strtoull(guid_str.c_str() + 2, &end, 16);
  } else {
    raw_guid = std::strtoull(guid_str.c_str(), &end, 10);
  }

  if (guid_str.empty() || end == nullptr || *end != '\0' || raw_guid == 0 ||
      (raw_guid & 0xF000000000000000ull) != 0) {
    return luaL_error(L, "Usage: GetPlayerInfoByGUID(\"playerGUID\")");
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  const auto *cached = session->query_cache().GetOrRequestPlayerName(raw_guid);
  if (cached == nullptr) {
    return 0;
  }
  const auto player_info = *cached;

  const auto push_optional_string = [L](const std::string_view value) {
    if (value.empty()) {
      lua_pushnil(L);
    } else {
      lua_pushlstring(L, value.data(), value.size());
    }
  };

  push_optional_string(LookupClassDisplayName(
      L, player_info.class_id, player_info.sex));
  push_optional_string(LookupClassFileToken(L, player_info.class_id));
  push_optional_string(LookupRaceDisplayName(
      L, player_info.race, player_info.sex));
  push_optional_string(LookupRaceFileToken(L, player_info.race));

  static constexpr std::array<lua_Number, 3> kGenderToLuaSex{2, 3, 1};
  lua_pushnumber(L, player_info.sex < kGenderToLuaSex.size()
                        ? kGenderToLuaSex[player_info.sex]
                        : 1);
  lua_pushlstring(L, player_info.name.data(), player_info.name.size());
  lua_pushlstring(L, player_info.realm_name.data(),
                  player_info.realm_name.size());

  return 7;
}

int LuaGetArmorPenetration(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  const auto *dbc = GetDbcLoader(L);
  if (player == nullptr || dbc == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const float armor_penetration =
      std::min(100.0f,
               openwow::game::ComputeCombatRatingBonus(*player, *dbc,
                                                        kArmorPenetrationRating));
  lua_pushnumber(L, static_cast<lua_Number>(armor_penetration));
  return 1;
}

int LuaUnitInRange(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  const auto token = call.require_string(1, "Usage: UnitInRange(\"unit\")");
  const auto guid = ResolveUnitId(session, token);
  const auto *unit = session != nullptr ? session->objects().GetUnit(guid) : nullptr;
  const auto *player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (session == nullptr || unit == nullptr || player == nullptr ||
      (!IsTrackedPartyPlayerOrPetGuid(session->objects(), guid) &&
       !IsTrackedRaidPlayerOrPetGuid(session->objects(), guid))) {
    return call.nil();
  }

  constexpr float kRangeYards = 40.0f;
  const float dx = unit->GetX() - player->GetX();
  const float dy = unit->GetY() - player->GetY();
  const float dz = unit->GetZ() - player->GetZ();
  return call.wow_bool(std::sqrt(dx * dx + dy * dy + dz * dz) <= kRangeYards);
}

int LuaUnitAffectingCombat(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: UnitAffectingCombat(\"unit\")");
  const auto *const unit = ResolveLiveScriptUnit(call.world_session(), uid);
  return call.wow_bool(unit != nullptr &&
                       (unit->GetUInt32(::openwow::game::UNIT_FIELD_FLAGS) & 0x00080000u) != 0);
}

int LuaHasFullControl(lua_State *L) {
  const auto *const session = GetWorldSession(L);
  const auto *const player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  lua_pushwowbool(L, player != nullptr && ActivePlayerHasFullControl(*session, *player));
  return 1;
}

int LuaGetMaxCombatRatingBonus(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetMaxCombatRatingBonus(ratingIndex)");
  }

  const auto rating_id =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  if (rating_id < 1u || rating_id > 25u) {
    return luaL_error(L, "ratingIndex is in the range 1 .. %d", 25);
  }

  if (rating_id >= 15 && rating_id <= 17) {
    lua_pushnumber(L, 33.0);
  } else {
    lua_pushnumber(L, -1.0);
  }
  return 1;
}

int LuaTargetDirectionEnemy(lua_State *L) {
  return LuaTargetDirectionWithFilter(L, openwow::game::TargetFilter::kEnemy,
                                      "Usage: TargetDirectionEnemy(facing)");
}

int LuaTargetDirectionFriend(lua_State *L) {
  return LuaTargetDirectionWithFilter(L, openwow::game::TargetFilter::kFriend,
                                      "Usage: TargetDirectionFriend(facing)");
}

int LuaTargetDirectionFinished(lua_State *L) {
  if (auto *targeting = GetTargetingSystem(L); targeting != nullptr) {
    targeting->FinishDirectionTarget();
  }
  return 0;
}

int LuaTargetTotem(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: TargetTotem(slot)");
  }

  const int slot = static_cast<int>(lua_tonumber(L, 1));
  if (static_cast<unsigned>(slot - 1) > 3) {
    return luaL_error(L, "Totem slot must be in range of 1 to %d", 4);
  }

  auto *session = GetWorldSession(L);
  auto *targeting = GetTargetingSystem(L);
  if (!session || !targeting) {
    return 0;
  }

  const auto slot_state = session->spell_book().GetTotemSlot(static_cast<std::uint8_t>(slot - 1));
  if (!slot_state.has_value() || !slot_state->has_totem()) {
    return 0;
  }

  if (!GameUI_CanPerformTaintForbiddenAction()) {
    return 0;
  }

  targeting->SetTarget(slot_state->totem_guid);
  return 0;
}

int LuaGetUnitPitch(lua_State *L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: GetUnitPitch(\"unit\")");
  auto *session = call.world_session();
  if (!session) {
    return call.number(0.0);
  }
  const auto *unit = ResolveUnitObject(ResolveUnit(session, uid));
  if (!unit) {
    return call.number(0.0);
  }
  return call.number(
      static_cast<lua_Number>(unit->Movement().Data().GetRuntimePitch()));
}

int LuaInteractUnit(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: InteractUnit(unitToken[, exactMatch])");
  }

  if (!GameUI_CanPerformTaintForbiddenAction()) {
    return 0;
  }

  auto *targeting = GetTargetingSystem(L);
  if (!targeting) {
    lua_pushnil(L);
    return 1;
  }

  const std::string unit_id = SafeLuaString(L, 1);
  if (unit_id.empty()) {
    lua_pushnil(L);
    return 1;
  }

  auto guid = openwow::game::ObjectGuid();
  if (openwow::text::EqualsIgnoreCaseAscii(unit_id, "mouseover")) {
    guid = session->objects().GetMouseoverGuid();
  } else {
    const bool exact_match = ReadClientBoolArgOrDefault(L, 2, false);
    guid = ResolveGameUiLookup(session, unit_id, openwow::game::kTypeMaskObject, 0, exact_match,
                               false);
  }
  if (targeting->InteractWith(guid.GetRawValue())) {
    lua_pushwowbool(L, true);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetUnitManaRegenRateFromSpirit(lua_State* L) {
  const LuaCallFrame call{L};
  const auto token = call.require_string(1, "Usage: GetUnitManaRegenRateFromSpirit(\"unit\")");
  return call.number(LuaDerivedStatQuery(call.state(), token).mana_regen_from_spirit());
}

}
