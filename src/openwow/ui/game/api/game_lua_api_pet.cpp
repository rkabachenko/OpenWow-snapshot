#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/held_cursor_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_pet.h"
#include "openwow/game/actors/pets/adapters/ui/stable_pet_cursor_controller.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/aura_manager.h"
#include "openwow/game/name_declension.h"
#include "openwow/game/name_validation.h"
#include "openwow/game/pet_system.h"
#include "openwow/game/spell_cast_execution.h"
#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/spell_validation.h"
#include "openwow/data/archive_system.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/ui/lua_numeric.h"

#include <algorithm>
#include <optional>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint32_t kStableInteractionAuraType = 292;
constexpr std::uint32_t kStableInteractionInternalFlag = 0x10u;

constexpr std::uint8_t kRetailStableSlotPurchaseLimit = 4u;

bool NeedsDeclinedPetName(const char *name) {
  const int locale = openwow::data::GetCurrentLocaleInfo().locale_index;
  return locale == 8 && name != nullptr &&
         openwow::game::declension::StartsWithCyrillicCodeUnit(name);
}

int ValidatePetStablePaperdollFrame(lua_State* L) {
  if (lua_type(L, 1) != LUA_TTABLE) {
    return luaL_error(L, "Usage: SetPetStablePaperdoll(characterModel)");
  }

  if (!HasLuaScriptObjectThis(L, 1)) {
    return luaL_error(
        L, "SetPetStablePaperdoll(): Couldn't find 'this' in frame object");
  }

  if (!LuaScriptObjectIsKindOfCanonicalType(
          L, 1, openwow::ui::widgets::ScriptObjectType::Frame)) {
    return luaL_error(
        L, "SetPetStablePaperdoll(): Wrong object type, expected frame");
  }

  return lua_absindex(L, 1);
}

bool SendPetActionIfUsable(openwow::game::WorldSession& session,
                           const std::uint32_t action_data,
                           const std::uint64_t target_guid) {
  const auto pet_guid = GetPrimaryPetActionGuid(session);
  if (pet_guid.IsEmpty()) {
    return false;
  }

  const auto action_kind =
      static_cast<std::uint8_t>((action_data >> 24) & 0x3Fu);
  if (!CanUsePetActions(
          session,
          PetActionAvailabilityRequiresForceCheck(action_kind))) {
    return false;
  }

  session.interaction().SendPetAction(
      pet_guid.GetRawValue(), action_data, target_guid);
  return true;
}

void SendPetAbandonSequence(openwow::game::WorldSession& session) {
  session.interaction().SendPetAbandon(
      GetPrimaryPetActionGuid(session).GetRawValue());

  const auto stable_master_guid = session.pet().stable_list().npc_guid.GetRawValue();
  if (stable_master_guid != 0) {
    session.interaction().SendListStabledPets(stable_master_guid);
  }
}

static bool SpellHasAuraType(const openwow::data::dbc::SpellEntry &spell,
                             std::uint32_t aura_type) {
  return std::find(spell.effect_apply_aura.begin(),
                   spell.effect_apply_aura.end(), aura_type) !=
         spell.effect_apply_aura.end();
}

bool ActivePlayerHasAuraType(const openwow::game::WorldSession& session,
                             const openwow::data::dbc::DbcLoader* dbc,
                             const std::uint32_t aura_type) {
  if (dbc == nullptr) {
    return false;
  }

  const auto* player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return false;
  }

  const auto& auras = session.aura().GetAuras(player->GetGuid().GetRawValue());
  return std::any_of(
      auras.begin(), auras.end(),
      [dbc, aura_type](const openwow::game::AuraSlotInfo& aura) {
        if (aura.spell_id == 0) {
          return false;
        }

        const auto* spell = dbc->spell().LookupEntry(aura.spell_id);
        return spell != nullptr && SpellHasAuraType(*spell, aura_type);
      });
}

void CancelStableInteractionAura(openwow::game::WorldSession& session,
                                 const openwow::data::dbc::DbcLoader* dbc) {
  if (dbc == nullptr) {
    return;
  }

  const auto* player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr ||
      (player->GetInternalFlags() & kStableInteractionInternalFlag) == 0) {
    return;
  }

  const auto& auras = session.aura().GetAuras(player->GetGuid().GetRawValue());
  for (const openwow::game::AuraSlotInfo& aura : auras) {
    if (aura.spell_id == 0 ||
        !openwow::game::HasFlag(aura.flags, openwow::game::AuraFlag::kPositive)) {
      continue;
    }

    const auto* spell = dbc->spell().LookupEntry(aura.spell_id);
    if (spell == nullptr) {
      continue;
    }

    for (const std::uint32_t effect_aura_type : spell->effect_apply_aura) {
      if (effect_aura_type == kStableInteractionAuraType) {
        openwow::game::UseSpellAction(session, 0, aura.spell_id);
      }
    }
  }
}

}

int LuaHasPetUI(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto pet_guid = GetPrimaryPetActionGuid(*session);
  const auto* pet_unit = session->objects().GetUnit(pet_guid);

  if (pet_unit && !pet_unit->IsPlayer()
      && !pet_unit->State().GetSummonedBy().IsEmpty()) {
    lua_pushnumber(L, 1.0);
    if (pet_unit->State().IsHunterPet())
      lua_pushnumber(L, 1.0);
    else
      lua_pushnil(L);
    return 2;
  }

  lua_pushnil(L);
  lua_pushnil(L);
  return 2;
}

int LuaGetPetActionsUsable(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }

  if (CanUsePetActions(*session, false)) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetPetActionInfo(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetPetActionInfo(index)");
  }

  const int index = static_cast<int>(std::trunc(lua_tonumber(L, 1)));
  auto* session = GetWorldSession(L);
  if (!session || index <= 0 || index > 10) {
    for (int i = 0; i < 7; ++i) lua_pushnil(L);
    return 7;
  }

  const auto& pet_bar = session->pet().pet_bar();
  if (!pet_bar.active || pet_bar.guid.IsEmpty()) {
    for (int i = 0; i < 7; ++i) lua_pushnil(L);
    return 7;
  }

  const auto& action = pet_bar.action_bar[static_cast<std::size_t>(index - 1)];
  const auto action_id = action.ActionId();
  const auto action_kind = action.ActionKind();
  if (action.raw == 0) {
    for (int i = 0; i < 7; ++i) lua_pushnil(L);
    return 7;
  }

  if (action_kind == 6 || action_kind == 7) {
    const char* name_token = nullptr;
    const char* texture_token = nullptr;
    bool is_active = false;

    if (action_kind == 6) {
      switch (action_id) {
        case 0:
          name_token = "PET_MODE_PASSIVE";
          texture_token = "PET_PASSIVE_TEXTURE";
          break;
        case 1:
          name_token = "PET_MODE_DEFENSIVE";
          texture_token = "PET_DEFENSIVE_TEXTURE";
          break;
        case 2:
          name_token = "PET_MODE_AGGRESSIVE";
          texture_token = "PET_AGGRESSIVE_TEXTURE";
          break;
        default:
          break;
      }
      is_active =
          action_id == static_cast<std::uint32_t>(pet_bar.EffectiveReactState());
    } else {
      switch (action_id) {
        case 0:
          name_token = "PET_ACTION_WAIT";
          texture_token = "PET_WAIT_TEXTURE";
          break;
        case 1:
          name_token = "PET_ACTION_FOLLOW";
          texture_token = "PET_FOLLOW_TEXTURE";
          break;
        case 2:
          name_token = "PET_ACTION_ATTACK";
          texture_token = "PET_ATTACK_TEXTURE";
          break;
        case 3:
          name_token = "PET_ACTION_DISMISS";
          texture_token = "PET_DISMISS_TEXTURE";
          break;
        default:
          break;
      }
      is_active = action_id == 2
          ? session->pet().attack_command_active()
          : action_id == static_cast<std::uint32_t>(pet_bar.command);
    }

    if (!name_token || !texture_token) {
      for (int i = 0; i < 7; ++i) lua_pushnil(L);
      return 7;
    }

    lua_pushstring(L, name_token);
    lua_pushnil(L);
    lua_pushstring(L, texture_token);
    lua_pushnumber(L, 1.0);
    lua_pushwowbool(L, is_active);
    lua_pushnil(L);
    lua_pushnil(L);
  } else {

    std::string_view spell_name;
    std::string_view spell_rank;
    std::string_view texture_path;
    lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
    auto* dbc = static_cast<const openwow::data::dbc::DbcLoader*>(
        lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (dbc) {
      const auto* spell = dbc->spell().LookupEntry(
          action.ActionId());
      if (spell != nullptr) {
        spell_name = spell->spell_name;
        spell_rank = spell->rank;
        std::uint32_t icon_id = spell->spell_icon_id;
        const auto* pet_unit = session->objects().GetUnit(pet_bar.guid);
        if (pet_unit != nullptr &&
            openwow::game::IsSpellRecordCurrentForUnit(*spell, *dbc, pet_unit) &&
            spell->active_icon_id != 0) {
          icon_id = spell->active_icon_id;
        }

        if (icon_id != 0) {
          const auto* icon = dbc->spell_icon().LookupEntry(icon_id);
          if (icon != nullptr &&
              !std::string_view(icon->icon_path).empty()) {
            texture_path = icon->icon_path;
          }
        }
      }
    }
    if (spell_name.empty()) {
      for (int i = 0; i < 7; ++i) lua_pushnil(L);
      return 7;
    }
    lua_pushlstring(L, spell_name.data(), spell_name.size());
    lua_pushlstring(L, spell_rank.data(), spell_rank.size());
    if (texture_path.empty())
      lua_pushnil(L);
    else
      lua_pushlstring(L, texture_path.data(), texture_path.size());
    lua_pushnil(L);
    lua_pushnil(L);
    if (action.IsAutocastAllowed())
      lua_pushnumber(L, 1.0);
    else
      lua_pushnil(L);
    if (action.IsAutocastEnabled())
      lua_pushnumber(L, 1.0);
    else
      lua_pushnil(L);
  }
  return 7;
}

int LuaPetAttack(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;
  const auto* player = session->objects().GetLocalPlayer();
  if (!player) return 0;

  auto target_guid = session->objects().GetTargetGuid();
  if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
    const std::string unit_id = SafeLuaString(L, 1);
    if (!unit_id.empty()) {
      const bool exact_match = ReadClientBoolArgOrDefault(L, 2, false);
      const auto guid = ResolveGameUiLookup(session, unit_id,
                                            openwow::game::kTypeMaskUnit,
                                            1,
                                            exact_match,
                                            true);
      if (!guid.IsEmpty()) {
        target_guid = guid;
      }
    }
  }

  SendPetActionIfUsable(*session, 0x07000002, target_guid.GetRawValue());
  return 0;
}

int LuaPetFollow(lua_State* L) {
  auto* session = GetWorldSession(L);

  if (!session) return 0;
  SendPetActionIfUsable(*session, 0x07000001, 0);
  return 0;
}

int LuaPetPassiveMode(lua_State* L) {
  auto* session = GetWorldSession(L);

  if (!session) return 0;
  SendPetActionIfUsable(*session, 0x06000000, 0);
  return 0;
}

int LuaPetDefensiveMode(lua_State* L) {
  auto* session = GetWorldSession(L);

  if (!session) return 0;
  SendPetActionIfUsable(*session, 0x06000001, 0);
  return 0;
}

int LuaPetAggressiveMode(lua_State* L) {
  auto* session = GetWorldSession(L);

  if (!session) return 0;
  SendPetActionIfUsable(*session, 0x06000002, 0);
  return 0;
}

int LuaPetAbandon(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;
  SendPetAbandonSequence(*session);
  return 0;
}

int LuaPetRename(lua_State* L) {
  const char* name = lua_tostring(L, 1);

  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto pet_guid = GetPrimaryPetActionGuid(*session);
  const auto* pet_unit = session->objects().GetUnit(pet_guid);
  if (pet_unit == nullptr) {
    DisplaySystemMessage(258);
    return 0;
  }

  const auto* player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr ||
      pet_unit->State().GetSummonedBy() != player->GetGuid()) {
    DisplaySystemMessage(259);
    return 0;
  }

  if ((pet_unit->State().GetPetFlags() & 0x01) == 0) {
    DisplaySystemMessage(260);
    return 0;
  }

  if (name == nullptr || *name == '\0') {
    PetNameCache_HandlePetRenameResult(
        static_cast<int>(openwow::game::NameValidationResult::kNoName));
    return 0;
  }

  const auto validation = openwow::game::ValidatePetName(std::string(name), 0);
  if (validation != openwow::game::NameValidationResult::kOk) {
    PetNameCache_HandlePetRenameResult(static_cast<int>(validation));
    return 0;
  }

  if (NeedsDeclinedPetName(name)) {
    constexpr int kMaxDeclinedForms = 5;
    std::array<std::string, 5> declined_forms{};
    int collected = 0;
    for (int i = 0; i < kMaxDeclinedForms; ++i) {
      const char* form = lua_tostring(L, i + 2);
      if (form == nullptr || *form == '\0')
        break;
      declined_forms[static_cast<std::size_t>(i)] =
          std::string(form, std::min<std::size_t>(std::strlen(form), 95));
      ++collected;
    }
    if (collected >= kMaxDeclinedForms) {
      session->interaction().SendPetRename(
          pet_guid.GetRawValue(), std::string(name), &declined_forms);
    } else {
      ScriptEventDispatch::Get().FireEventArgs(
          events::PET_FORCE_NAME_DECLENSION,
          {std::string(name)});
    }
  } else {
    session->interaction().SendPetRename(pet_guid.GetRawValue(),
                                         std::string(name));
  }
  return 0;
}

int LuaPetDismiss(lua_State* L) {

  auto* session = GetWorldSession(L);
  if (!session) return 0;
  if (!SendPetActionIfUsable(*session, 0x07000003, 0)) {
    return 0;
  }

  constexpr int kPetDismissCastDurationMs = 10000;
  ScriptEventDispatch::Get().FireEventArgs(events::PET_DISMISS_START,
                                           {kPetDismissCastDurationMs});
  return 0;
}

int LuaPetCanBeAbandoned(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session) {
    const auto pet_guid = GetPrimaryPetActionGuid(*session);
    const auto* pet_unit = session->objects().GetUnit(pet_guid);
    if (pet_unit) {
      const auto* player = session->objects().GetLocalPlayer();
      if (player &&
          pet_unit->State().GetSummonedBy() == player->GetGuid() &&
          (pet_unit->State().GetPetFlags() & 0x02) != 0) {
        lua_pushnumber(L, 1.0);
        return 1;
      }
    }
  }
  lua_pushnil(L);
  return 1;
}

int LuaPetCanBeRenamed(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session) {
    const auto pet_guid = GetPrimaryPetActionGuid(*session);
    const auto* pet_unit = session->objects().GetUnit(pet_guid);
    if (pet_unit) {
      const auto* player = session->objects().GetLocalPlayer();
      if (player &&
          pet_unit->State().GetSummonedBy() == player->GetGuid() &&
          (pet_unit->State().GetPetFlags() & 0x01) != 0) {
        lua_pushnumber(L, 1.0);
        return 1;
      }
    }
  }
  lua_pushnil(L);
  return 1;
}

int LuaGetPetExperience(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session) {
    const auto pet_guid = GetPrimaryPetActionGuid(*session);
    const auto* pet_unit = session->objects().GetUnit(pet_guid);
    if (pet_unit && pet_unit->State().IsHunterPet()) {
      lua_pushnumber(L, static_cast<double>(pet_unit->State().GetPetExperience()));
      lua_pushnumber(L, static_cast<double>(pet_unit->State().GetPetNextLevelExp()));
      return 2;
    }
  }
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  return 2;
}

namespace {

constexpr std::uint8_t kHunterClassId = 3;
constexpr std::uint8_t kStablePetFlagStabled = 0x2;
constexpr int kPetActionBarSlotCount = 10;
constexpr std::uint8_t kPetActionCooldownKindSpell = 1;
constexpr std::uint8_t kPetActionCooldownKindMin = 8;
constexpr std::uint8_t kPetActionCooldownKindMax = 0x11;

void PushLuaStringView(lua_State* L, const std::string_view value) {
  lua_pushlstring(L, value.empty() ? "" : value.data(), value.size());
}

const openwow::data::dbc::CreatureFamilyEntry* ResolveCurrentHunterPetFamily(
    lua_State* L, const openwow::data::dbc::DbcLoader& dbc) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return nullptr;
  }

  const auto* player = session->objects().GetLocalPlayer();
  if (player == nullptr || GetUnitClass(player) != kHunterClassId) {
    return nullptr;
  }

  const auto& pet_bar = session->pet().pet_bar();
  if (!pet_bar.active || pet_bar.guid.IsEmpty() || pet_bar.creature_family == 0) {
    return nullptr;
  }

  return dbc.creature_family().LookupEntry(pet_bar.creature_family);
}

int TruncateLuaNumberArgument(lua_State* L, const int argument_index) {
  return TruncateLuaNumberToSseI32(lua_tonumber(L, argument_index));
}

bool PetActionSupportsCooldown(
    const openwow::game::PetActionButton& action_button) {
  if (action_button.raw == 0) {
    return false;
  }

  const auto action_kind = action_button.ActionKind();
  return action_kind == kPetActionCooldownKindSpell ||
         (action_kind >= kPetActionCooldownKindMin &&
          action_kind <= kPetActionCooldownKindMax);
}

std::optional<openwow::game::SpellCooldownState> ResolvePetActionCooldown(
    const openwow::game::PetBarState& pet_bar, const std::size_t slot_index,
    const openwow::data::dbc::DbcLoader* const dbc) {
  const auto& action_button = pet_bar.action_bar[slot_index];
  if (!PetActionSupportsCooldown(action_button)) {
    return std::nullopt;
  }

  return openwow::game::ResolvePetBarSpellCooldown(
      pet_bar, action_button.ActionId(), dbc);
}

bool IsPetActionSlotUsable(
    const openwow::game::WorldSession& session,
    const openwow::game::PetActionButton* action,
    const openwow::data::dbc::DbcLoader* dbc) {
  if (action == nullptr) {
    return true;
  }

  const auto action_kind = action->ActionKind();
  if (!PetActionAvailabilityRequiresForceCheck(action_kind)) {
    return CanUsePetActions(session, false);
  }

  if (!CanUsePetActions(session, true) || dbc == nullptr) {
    return false;
  }

  const auto* spell = dbc->spell().LookupEntry(action->ActionId());
  if (spell == nullptr) {
    return false;
  }

  std::vector<std::uint64_t> pet_guids = session.pet().pet_guids();
  if (pet_guids.empty()) {
    const auto primary_guid = GetPrimaryPetActionGuid(session);
    if (!primary_guid.IsEmpty()) {
      pet_guids.push_back(primary_guid.GetRawValue());
    }
  }

  for (const auto raw_guid : pet_guids) {
    const auto* pet = session.objects().GetUnit(openwow::game::ObjectGuid(raw_guid));
    if (pet == nullptr) {
      continue;
    }

    const auto target_guid = pet->State().GetTarget().GetRawValue();
    if (openwow::game::ValidateSpellRequirements(
            session,
            reinterpret_cast<std::uintptr_t>(pet),
            reinterpret_cast<std::uintptr_t>(spell),
            reinterpret_cast<std::uintptr_t>(&target_guid), 0, true)) {
      return true;
    }
  }

  return false;
}

const openwow::game::StablePetEntry* FindCurrentStablePetEntry(
    const openwow::game::StableListInfo& stable_list) {
  for (const auto& pet : stable_list.pets) {
    if ((pet.flags & kStablePetFlagStabled) == 0) {
      return &pet;
    }
  }

  return nullptr;
}

const openwow::game::StablePetEntry* FindStabledPetEntry(
    const openwow::game::StableListInfo& stable_list,
    const int visible_stable_slot) {
  if (visible_stable_slot <= 0) {
    return nullptr;
  }

  int visible_index = 0;
  for (const auto& pet : stable_list.pets) {
    if ((pet.flags & kStablePetFlagStabled) == 0) {
      continue;
    }

    ++visible_index;
    if (visible_index == visible_stable_slot) {
      return &pet;
    }
  }

  return nullptr;
}

const openwow::game::StablePetEntry* ResolveStablePetEntryForInfo(
    const openwow::game::StableListInfo& stable_list,
    const std::uint32_t zero_based_index) {
  if (SignedI32FromU32Bits(zero_based_index) < 0) {
    return FindCurrentStablePetEntry(stable_list);
  }

  return FindStabledPetEntry(
      stable_list, static_cast<int>(zero_based_index + 1u));
}

const openwow::game::StablePetEntry* ResolveStablePetEntryForFoodTypes(
    const openwow::game::StableListInfo& stable_list,
    const std::uint32_t zero_based_index) {
  if (zero_based_index == UINT32_MAX) {
    return FindCurrentStablePetEntry(stable_list);
  }

  if (zero_based_index >= stable_list.pets.size()) {
    return nullptr;
  }

  return FindStabledPetEntry(
      stable_list, static_cast<int>(zero_based_index + 1u));
}

std::uint64_t ResolveCreatureTemplateQueryContext(
    const openwow::game::WorldSession& session) {
  const auto* player = session.objects().GetLocalPlayerTyped();
  return player != nullptr ? player->GetGuid().GetRawValue() : 0;
}

void FirePetStableUpdateEvent(bool success) {
  if (!success) {
    return;
  }

  ScriptEventDispatch::Get().FireEvent(events::PET_STABLE_UPDATE);
}

const openwow::game::CreatureTemplateInfo* ResolveStablePetCreatureTemplate(
    openwow::game::WorldSession& session,
    const openwow::game::StablePetEntry& pet) {
  if (pet.creature_id == 0) {
    return nullptr;
  }

  return session.query_cache().GetOrRequestCreatureTemplate(
      pet.creature_id,
      openwow::game::QueryCache::QueryRequestOptions{
          .context = ResolveCreatureTemplateQueryContext(session),
          .callback = FirePetStableUpdateEvent,
      });
}

const openwow::data::dbc::CreatureFamilyEntry* ResolveStablePetFamily(
    openwow::game::WorldSession& session,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::StablePetEntry& pet) {
  const auto* creature_template = ResolveStablePetCreatureTemplate(session, pet);
  if (creature_template == nullptr || creature_template->creature_family == 0) {
    return nullptr;
  }

  return dbc.creature_family().LookupEntry(creature_template->creature_family);
}

bool CanIssueStableMasterCommand(const openwow::game::WorldSession& session,
                                 const openwow::data::dbc::DbcLoader* dbc) {
  if (!session.pet().stable_list().npc_guid.IsEmpty()) {
    return true;
  }

  return ActivePlayerHasAuraType(session, dbc, kStableInteractionAuraType);
}

const openwow::game::StableListInfo& ResolveStableListForUi(
    const openwow::game::WorldSession* session) {
  static const openwow::game::StableListInfo kEmptyStableList;
  return session != nullptr ? session->pet().stable_list() : kEmptyStableList;
}

void FirePetStablePaperdollUpdateEvent(bool success) {
  if (!success) {
    return;
  }

  ScriptEventDispatch::Get().FireEvent(events::PET_STABLE_UPDATE_PAPERDOLL);
}

bool ApplyLiveStablePetBindingToFrame(
    lua_State* L, const int frame_index,
    const openwow::game::CGUnit_C& pet) {
  const auto display_id = pet.Presentation().DisplayId();
  if (display_id == 0) {
    return false;
  }

  const auto raw_guid = pet.GetGuid().GetRawValue();
  lua_pushinteger(L, static_cast<lua_Integer>(raw_guid & 0xFFFFFFFFu));
  lua_setfield(L, frame_index, "__ow_model_unit_guid_lo");
  lua_pushinteger(L, static_cast<lua_Integer>(raw_guid >> 32));
  lua_setfield(L, frame_index, "__ow_model_unit_guid_hi");
  lua_pushinteger(L, 0);
  lua_setfield(L, frame_index, "__ow_model_creature");
  lua_pushinteger(L, static_cast<lua_Integer>(display_id));
  lua_setfield(L, frame_index, "__ow_model_display");

  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_model_sequence");
  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_model_sequence_time");
  lua_pushnumber(L, NormalizeFrameAlphaByte(pet.Presentation().UnitAlphaByte()));
  lua_setfield(L, frame_index, "__ow_alpha");
  return true;
}

std::uint32_t ResolveActiveStablePetCreatureEntry(
    const openwow::game::WorldSession& session) {
  if (const auto* active_stable_pet =
          FindCurrentStablePetEntry(session.pet().stable_list());
      active_stable_pet != nullptr && active_stable_pet->creature_id != 0) {
    return active_stable_pet->creature_id;
  }

  const auto pet_guid = GetPrimaryPetActionGuid(session);
  if (pet_guid.IsEmpty()) {
    return 0;
  }

  const auto* pet_unit = session.objects().GetUnit(pet_guid);
  return pet_unit != nullptr ? pet_unit->GetEntry() : 0;
}

static std::int32_t ResolveSelectedStablePetVisibleIndex(
    const openwow::game::PetManager& pets,
    const openwow::game::StableListInfo& stable_list);

std::uint32_t ResolveSelectedStablePetCreatureEntry(
    const openwow::game::WorldSession& session) {
  const auto& stable_list = session.pet().stable_list();
  const auto selected_visible_index =
      ResolveSelectedStablePetVisibleIndex(session.pet(), stable_list);
  if (selected_visible_index < 0) {
    return 0;
  }

  if (selected_visible_index == 0) {
    return ResolveActiveStablePetCreatureEntry(session);
  }

  const auto* stable_pet =
      FindStabledPetEntry(stable_list, selected_visible_index);
  return stable_pet != nullptr ? stable_pet->creature_id : 0;
}

const openwow::game::CreatureTemplateInfo* ResolveSelectedStablePetCreatureTemplate(
    openwow::game::WorldSession& session) {
  const auto creature_entry = ResolveSelectedStablePetCreatureEntry(session);
  if (creature_entry == 0) {
    return nullptr;
  }

  return session.query_cache().GetOrRequestCreatureTemplate(
      creature_entry,
      openwow::game::QueryCache::QueryRequestOptions{
          .context = ResolveCreatureTemplateQueryContext(session),
          .callback_key = openwow::game::AsyncQueryChannel::CallbackKey(
              reinterpret_cast<std::uintptr_t>(
                  &FirePetStablePaperdollUpdateEvent),
              0),
          .callback = FirePetStablePaperdollUpdateEvent,
      });
}

const openwow::game::StablePetEntry* FindStabledPetEntryByZeroBasedIndex(
    const openwow::game::StableListInfo& stable_list,
    const int zero_based_index) {
  if (zero_based_index < 0) {
    return nullptr;
  }

  return FindStabledPetEntry(stable_list, zero_based_index + 1);
}

bool HasPetNumber(const openwow::game::StablePetEntry* stable_pet) {
  return stable_pet != nullptr && stable_pet->pet_number != 0;
}

bool HasActivePetGuid(const openwow::game::WorldSession& session) {
  return !GetPrimaryPetActionGuid(session).IsEmpty();
}

void SendRetrieveOrSwapStablePet(openwow::game::WorldSession& session,
                                 const std::uint64_t stable_master_guid,
                                 const std::uint32_t pet_number) {
  if (HasActivePetGuid(session)) {
    session.interaction().SendStableSwapPet(stable_master_guid, pet_number);
    return;
  }

  session.interaction().SendUnstablePet(stable_master_guid, pet_number);
}

void SetSelectedStablePetFromZeroBasedIndex(
    openwow::game::PetManager& pets,
    const openwow::game::StableListInfo& stable_list,
    const std::uint32_t zero_based_index) {
  if (zero_based_index == UINT32_MAX) {
    pets.SetSelectedStablePetNumber(-1);
    return;
  }

  const auto* stable_pet = zero_based_index < stable_list.pets.size()
      ? FindStabledPetEntryByZeroBasedIndex(
            stable_list, static_cast<int>(zero_based_index))
      : nullptr;
  pets.SetSelectedStablePetNumber(
      HasPetNumber(stable_pet)
          ? static_cast<std::int32_t>(stable_pet->pet_number)
          : 0);
}

std::int32_t ResolveSelectedStablePetVisibleIndex(
    const openwow::game::PetManager& pets,
    const openwow::game::StableListInfo& stable_list) {
  const auto selected_pet_number = pets.selected_stable_pet_number();
  if (selected_pet_number == -1) {
    return 0;
  }

  if (selected_pet_number == 0) {
    return -1;
  }

  std::int32_t visible_index = 1;
  for (const auto& pet : stable_list.pets) {
    if ((pet.flags & kStablePetFlagStabled) == 0) {
      continue;
    }

    if (static_cast<std::int32_t>(pet.pet_number) == selected_pet_number) {
      return visible_index;
    }

    ++visible_index;
  }

  return -1;
}

int PushPetFoodTypes(lua_State* L,
                     const openwow::data::dbc::DbcLoader& dbc,
                     const openwow::data::dbc::CreatureFamilyEntry& family) {
  int return_count = 0;
  for (const auto& pet_food : dbc.item_pet_food().entries()) {
    if (pet_food.id == 0 || pet_food.id > 32) {
      continue;
    }

    const auto food_bit =
        static_cast<std::uint32_t>(1u << (pet_food.id - 1u));
    if ((family.pet_food_mask & food_bit) == 0) {
      continue;
    }

    PushLuaStringView(L, pet_food.name);
    ++return_count;
  }

  return return_count;
}

const openwow::data::dbc::TalentTabEntry* ResolvePetTalentTreeTab(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::CreatureFamilyEntry& family) {
  if (family.pet_talent_type < 0 || family.pet_talent_type >= 32) {
    return nullptr;
  }

  const auto pet_talent_mask =
      static_cast<std::uint32_t>(1u << family.pet_talent_type);
  for (const auto& tab : dbc.talent_tab().entries()) {
    if ((tab.pet_talent_mask & pet_talent_mask) != 0) {
      return &tab;
    }
  }

  return nullptr;
}

}

int LuaGetPetFoodTypes(lua_State* L) {
  const auto* dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return 0;
  }

  const auto* family = ResolveCurrentHunterPetFamily(L, *dbc);
  if (family == nullptr) {
    return 0;
  }

  return PushPetFoodTypes(L, *dbc, *family);
}

int LuaGetPetHappiness(lua_State* L) {
  auto& sys = ::openwow::game::PetSystem::Get();
  const auto* pet = sys.GetCurrentPet();
  if (!pet) {
    lua_pushnil(L);
    lua_pushnumber(L, 100.0);
    return 2;
  }

  lua_pushnumber(L, static_cast<lua_Number>(pet->happiness + 1u));

  static constexpr float kDamagePct[3] = { 75.0f, 100.0f, 125.0f };
  const float dpct = (pet->happiness < 3u)
      ? kDamagePct[pet->happiness]
      : 100.0f;
  lua_pushnumber(L, static_cast<lua_Number>(dpct));
  return 2;
}

int LuaUnitCreatureFamily(lua_State* L) {
  const LuaCallFrame call{L};
  const auto uid = call.require_string(1, "Usage: UnitCreatureFamily(\"unit\")");
  auto* const session = call.world_session();
  const auto* const unit =
      ResolveUnitObject(ResolveUnit(session, std::string(uid)));
  if (unit == nullptr || session == nullptr) {
    return call.nil();
  }

  const auto entry = unit->GetEntry();
  if (entry == 0) {
    return call.nil();
  }

  const auto* creature_template =
      session->query_cache().GetCreatureTemplate(entry);
  if (creature_template == nullptr || creature_template->creature_family == 0) {
    return call.nil();
  }

  const auto* dbc = session->GetDbcLoader();
  if (dbc == nullptr) {
    dbc = call.dbc();
  }
  if (dbc == nullptr) {
    return call.nil();
  }

  const auto* family_entry =
      dbc->creature_family().LookupEntry(creature_template->creature_family);
  if (family_entry == nullptr) {
    return call.nil();
  }

  return call.string(family_entry->name);
}

int LuaGetPetIcon(lua_State* L) {
  const auto* dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto* family = ResolveCurrentHunterPetFamily(L, *dbc);
  if (family == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  PushLuaStringView(L, family->icon_file);
  return 1;
}

int LuaGetPetTalentTree(lua_State* L) {
  const auto* dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto* family = ResolveCurrentHunterPetFamily(L, *dbc);
  if (family == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto* talent_tab = ResolvePetTalentTreeTab(*dbc, *family);
  if (talent_tab == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  PushLuaStringView(L, talent_tab->name);
  return 1;
}

int LuaGetPetTimeRemaining(lua_State* L) {
  const auto* session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto remaining_ms = session->pet().GetTimedPetRemainingMs();
  if (!remaining_ms.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(*remaining_ms));
  return 1;
}

int LuaGetStablePetInfo(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetStablePetInfo(index)");
  }

  auto* session = GetWorldSession(L);
  const auto* dbc = GetDbcLoader(L);
  if (session == nullptr || dbc == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 3;
  }

  const std::uint32_t zero_based_index =
      SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto* stable_pet =
      ResolveStablePetEntryForInfo(
          session->pet().stable_list(), zero_based_index);
  if (stable_pet == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 3;
  }

  const auto* family = ResolveStablePetFamily(*session, *dbc, *stable_pet);
  if (family == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 3;
  }

  PushLuaStringView(L, family->icon_file);
  PushLuaStringView(L, stable_pet->name);
  lua_pushnumber(L, static_cast<lua_Number>(stable_pet->level));
  PushLuaStringView(L, family->name);

  const auto* talent_tab = ResolvePetTalentTreeTab(*dbc, *family);
  if (talent_tab == nullptr) {
    return 4;
  }

  PushLuaStringView(L, talent_tab->name);
  return 5;
}

int LuaGetNumStableSlots(lua_State* L) {
  auto* session = GetWorldSession(L);
  lua_pushnumber(
      L,
      static_cast<lua_Number>(ResolveStableListForUi(session).max_slots));
  return 1;
}

int LuaSetPetStablePaperdoll(lua_State* L) {
  const int frame_index = ValidatePetStablePaperdollFrame(L);
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  if (ResolveSelectedStablePetVisibleIndex(
          session->pet(), session->pet().stable_list()) == 0) {
    const auto pet_guid = GetPrimaryPetActionGuid(*session);
    if (const auto* pet = session->objects().GetUnit(pet_guid);
        pet != nullptr && ApplyLiveStablePetBindingToFrame(L, frame_index, *pet)) {
      return 0;
    }
  }

  const auto* creature_template =
      ResolveSelectedStablePetCreatureTemplate(*session);
  if (creature_template == nullptr) {
    return 0;
  }

  ApplyCreatureTemplateBindingToFrame(L, frame_index, *creature_template);
  return 0;
}

int LuaGetStablePetFoodTypes(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetStablePetInfo(index)");
  }

  auto* session = GetWorldSession(L);
  const auto* dbc = GetDbcLoader(L);
  if (session == nullptr || dbc == nullptr) {
    return 0;
  }

  const std::uint32_t zero_based_index =
      SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto* stable_pet =
      ResolveStablePetEntryForFoodTypes(
          session->pet().stable_list(), zero_based_index);
  if (stable_pet == nullptr) {
    return 0;
  }

  const auto* family = ResolveStablePetFamily(*session, *dbc, *stable_pet);
  if (family == nullptr) {
    return 0;
  }

  return PushPetFoodTypes(L, *dbc, *family);
}

int LuaGetPetActionCooldown(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetPetActionCooldown(index)");
  }

  const int slot = TruncateLuaNumberArgument(L, 1);
  auto* session = GetWorldSession(L);
  if (!session || slot < 1 || slot > kPetActionBarSlotCount) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
  }

  const auto& pet_bar = session->pet().pet_bar();
  if (!pet_bar.active || pet_bar.guid.IsEmpty()) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
  }

  const auto cooldown = ResolvePetActionCooldown(
      pet_bar, static_cast<std::size_t>(slot - 1), session->GetDbcLoader());
  if (!cooldown.has_value()) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
  }

  lua_pushnumber(L, cooldown->start_time_s);
  lua_pushnumber(L, cooldown->duration_s);
  lua_pushnumber(L, cooldown->enabled);
  return 3;
}

int LuaStablePet(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  if (!CanIssueStableMasterCommand(*session, GetDbcLoader(L))) {
    return 0;
  }

  session->interaction().SendStablePet(
      session->pet().stable_list().npc_guid.GetRawValue());
  return 0;
}

int LuaClickStablePet(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: ClickStablePet(index)");
  }

  auto* session = GetWorldSession(L);
  const auto& stable_list = ResolveStableListForUi(session);
  const std::uint32_t zero_based_index =
      SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;

  auto* cursor = session != nullptr ? session->held_cursor() : nullptr;
  const auto* held_pet =
      cursor != nullptr
          ? cursor->get_if<::openwow::game::actions::held_cursor::StablePet>()
          : nullptr;
  if (held_pet != nullptr) {
    const int held_slot = held_pet->stable_index;
    const std::uint32_t held_slot_bits = static_cast<std::uint32_t>(held_slot);
    if (held_slot_bits == zero_based_index) {
      cursor->Clear();
      lua_pushnil(L);
      return 1;
    }

    if (session != nullptr) {
      const auto stable_master_guid = stable_list.npc_guid.GetRawValue();
      const auto* clicked_pet = zero_based_index < stable_list.pets.size()
          ? FindStabledPetEntryByZeroBasedIndex(
                stable_list, static_cast<int>(zero_based_index))
          : nullptr;
      if (held_slot != -1) {
        if (zero_based_index == UINT32_MAX) {
          const auto* held_slot_entry =
              FindStabledPetEntryByZeroBasedIndex(stable_list, held_slot);
          if (HasPetNumber(held_slot_entry)) {
            SendRetrieveOrSwapStablePet(
                *session, stable_master_guid, held_slot_entry->pet_number);
          }
        } else if (HasPetNumber(clicked_pet)) {
          session->interaction().SendStableSwapPet(
              stable_master_guid, clicked_pet->pet_number);
        } else if (zero_based_index < stable_list.max_slots) {
          session->interaction().SendStablePet(stable_master_guid);
        }
      } else {
        if (HasPetNumber(clicked_pet)) {
          SendRetrieveOrSwapStablePet(
              *session, stable_master_guid, clicked_pet->pet_number);
        } else if (zero_based_index < stable_list.max_slots) {
          session->interaction().SendStablePet(stable_master_guid);
        }
      }
    }

    cursor->Clear();
    lua_pushnil(L);
    return 1;
  }

  if (session != nullptr) {
    SetSelectedStablePetFromZeroBasedIndex(
        session->pet(), stable_list, zero_based_index);
  }
  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaBuyStableSlot(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto* player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return 0;
  }

  const auto stable_master_guid =
      session->pet().stable_list().npc_guid.GetRawValue();
  if (stable_master_guid == 0) {
    return 0;
  }

  if (session->pet().stable_list().max_slots ==
      kRetailStableSlotPurchaseLimit) {
    return 0;
  }

  const auto* next_slot_price =
      GetNextStableSlotPriceEntry(*session, GetDbcLoader(L));
  if (next_slot_price == nullptr || player->GetMoney() < next_slot_price->cost) {
    return 0;
  }

  session->interaction().SendBuyStableSlot(stable_master_guid);
  return 0;
}

int LuaCastPetAction(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CastPetAction(index, target)");
  }

  const int slot = TruncateLuaNumberArgument(L, 1);
  if (slot <= 0 || slot > 11) {
    return luaL_error(L, "Invalid slot in CastPetAction");
  }

  auto* session = GetWorldSession(L);
  if (!session || !GameUI_CanPerformProtectedAction(protected_action_kind::kSpellCast)) return 0;
  const auto* player = session->objects().GetLocalPlayer();
  if (!player) return 0;

  const auto pet_guid = GetPrimaryPetActionGuid(*session);
  if (pet_guid.IsEmpty()) return 0;

  const auto slot_index = static_cast<std::size_t>(slot - 1);
  if (TryPlaceHeldPetActionOnBar(
          L, *session, pet_guid.GetRawValue(), slot_index)) {
    return 0;
  }

  if (slot_index >= kPetActionBarSlotCount) return 0;
  const auto& pet_bar = session->pet().pet_bar();
  const auto& action = pet_bar.action_bar[slot_index];
  if (!pet_bar.active || action.raw == 0) return 0;

  auto target_guid = session->objects().GetTargetGuid();
  bool has_explicit_target = false;

  const char* const target_token = lua_tostring(L, 2);
  if (target_token != nullptr && *target_token != '\0') {
    const auto resolved = ResolveGameUiLookup(
        session, target_token, openwow::game::kTypeMaskUnit, 1,
        false, true);
    if (resolved.IsEmpty()) {
      return 0;
    }
    target_guid = resolved;
    has_explicit_target = true;
  }

  (void)ExecutePetAction(L, *session, action.raw, target_guid.GetRawValue(),
                         has_explicit_target);
  ScriptEventDispatch::Get().FireEvent("GAMEABILITYACTIVATE");
  return 0;
}

int LuaClosePetStables(lua_State* L) {
  if (auto* session = GetWorldSession(L); session != nullptr) {
    CancelStableInteractionAura(*session, GetDbcLoader(L));

    const auto stable_master_guid =
        session->pet().stable_list().npc_guid;
    if (!stable_master_guid.IsEmpty()) {
      HandleNpcInteractionLoss(
          *session, stable_master_guid,
          NpcInteractionClosureCause::UnitUnavailable);
    }
  }
  return 0;
}

int LuaPetStopAttack(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session != nullptr) {
    session->pet().StopAttackIfActive(session->interaction());
  }
  return 0;
}

int LuaPetWait(lua_State* L) {

  auto* session = GetWorldSession(L);
  if (!session) return 0;
  SendPetActionIfUsable(
      *session, 0x07000000, 0);
  return 0;
}

int LuaPickupStablePet(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: PickupStablePet(index)");
  }

  auto* session = GetWorldSession(L);
  const int stable_slot = TruncateLuaNumberArgument(L, 1) - 1;
  const auto cursor_slot =
      stable_slot < 0
          ? ::openwow::game::actors::pets::StablePetCursorSlot::CurrentPet()
          : ::openwow::game::actors::pets::StablePetCursorSlot::StabledPet(
                static_cast<std::uint32_t>(stable_slot));
  if (session == nullptr) {
    return 0;
  }
  auto* cursor = session->held_cursor();
  if (cursor != nullptr &&
      ::openwow::game::actors::pets::ui::HasStablePetForCursor(
          *session, cursor_slot)) {
    (void)::openwow::game::actors::pets::ui::PickupStablePetCursor(
        *cursor, *session, cursor_slot);
  }

  return 0;
}

int LuaGetPetSpellBonusDamage(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }
  const auto* player = session->objects().GetLocalPlayerTyped();
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(
      static_cast<std::int32_t>(player->GetPetSpellPower())));
  return 1;
}

int LuaGetSelectedStablePet(lua_State* L) {
  const auto* session = GetWorldSession(L);
  lua_pushnumber(
      L,
      static_cast<lua_Number>(
          session == nullptr
              ? -1
              : ResolveSelectedStablePetVisibleIndex(
                    session->pet(), ResolveStableListForUi(session))));
  return 1;
}

int LuaIsPetAttackAction(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: IsPetAttackAction(index)");
  }

  const int index = TruncateLuaNumberArgument(L, 1);
  auto* session = GetWorldSession(L);
  if (!session || index <= 0 || index > kPetActionBarSlotCount) {
    lua_pushwowbool(L, false);
    return 1;
  }

  const auto& pet_bar = session->pet().pet_bar();
  const auto& action = pet_bar.action_bar[static_cast<std::size_t>(index - 1)];
  const bool is_attack = pet_bar.active && !pet_bar.guid.IsEmpty() &&
                         action.ActionKind() == 7 && action.ActionId() == 2;
  lua_pushwowbool(L, is_attack);
  return 1;
}

int LuaIsPetAttackActive(lua_State* L) {
  const auto* session = GetWorldSession(L);
  lua_pushwowbool(L, session != nullptr && session->pet().attack_command_active());
  return 1;
}

int LuaPetCanBeDismissed(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session) {
    const auto pet_guid = GetPrimaryPetActionGuid(*session);
    const auto* pet_unit = session->objects().GetUnit(pet_guid);
    if (pet_unit) {
      const auto* player = session->objects().GetLocalPlayer();
      if (player &&
          pet_unit->State().GetSummonedBy() == player->GetGuid()) {
        const auto entry = pet_unit->GetEntry();
        const auto* templ =
            session->query_cache().GetCreatureTemplate(entry);
        if (templ && templ->creature_family != 0) {
          const auto* dbc = GetDbcLoader(L);
          if (dbc) {
            const auto* family =
                dbc->creature_family().LookupEntry(templ->creature_family);
            if (family && (family->skill_line_0 & 0x20) != 0) {
              lua_pushnil(L);
              return 1;
            }
          }
        }
      }
    }
  }
  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaPetHasActionBar(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushwowbool(L, session->pet().HasActionBar(*session));
  return 1;
}

int LuaTogglePetAutocast(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: TogglePetAutocast(index)");
  }

  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto slot_number = TruncateLuaNumberArgument(L, 1);
  if (slot_number <= 0 || slot_number > 11) {
    return luaL_error(L, "Invalid slot in TogglePetAutocast");
  }

  const auto pet_guid = GetPrimaryPetActionGuid(*session);
  if (pet_guid.IsEmpty()) return 0;

  const auto slot_index = static_cast<std::uint32_t>(slot_number - 1);
  if (TryPlaceHeldPetActionOnBar(L, *session, pet_guid.GetRawValue(), slot_index)) {
    return 0;
  }

  if (slot_index >= kPetActionBarSlotCount) return 0;

  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kSpellCast) || !CanUsePetAutocastActions(*session)) {
    return 0;
  }

  const auto raw_action = session->pet().SetActionBarAutocastState(slot_index, std::nullopt);
  if (!raw_action.has_value()) return 0;

  session->interaction().SendPetSetAction(
      pet_guid.GetRawValue(), std::nullopt, {slot_index, *raw_action});
  ScriptEventDispatch::Get().FirePetBarUpdate();
  return 0;
}

int LuaGetPetActionSlotUsable(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetPetActionSlotsUsable(index)");
  }

  const int index = TruncateLuaNumberArgument(L, 1);
  const auto* session = GetWorldSession(L);
  const openwow::game::PetActionButton* action = nullptr;
  if (session != nullptr && index >= 1 && index <= kPetActionBarSlotCount) {
    const auto& pet_bar = session->pet().pet_bar();
    if (pet_bar.active && !pet_bar.guid.IsEmpty()) {
      action = &pet_bar.action_bar[static_cast<std::size_t>(index - 1)];
    }
  }

  lua_pushwowbool(
      L, session == nullptr || IsPetActionSlotUsable(*session, action, GetDbcLoader(L)));
  return 1;
}

int LuaUnstablePet(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: UnstablePet(index)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto* player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr || !player->State().GetPrimaryControlledUnitGUID().IsEmpty()) {
    return 0;
  }

  const std::uint32_t zero_based_index =
      SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto* stable_pet =
      zero_based_index < session->pet().stable_list().pets.size()
          ? FindStabledPetEntry(
                session->pet().stable_list(),
                static_cast<int>(zero_based_index + 1u))
          : nullptr;
  if (stable_pet == nullptr) {
    return 0;
  }

  session->interaction().SendUnstablePet(
      session->pet().stable_list().npc_guid.GetRawValue(),
      stable_pet->pet_number);
  return 0;
}

}
