
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_unit_stats.h"
#include "openwow/game/combat_rating.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/objects/unit/unit_cast_runtime.h"
#include "openwow/game/skill_line_ability_lookup.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"

#include <array>
#include <cmath>
#include <optional>

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint8_t kOffHandVisibleItemSlot = 16;
constexpr std::uint8_t kRangedVisibleItemSlot = 17;

constexpr std::uint8_t kMainHandVisibleItemSlot = 15;
constexpr std::uint32_t kWeaponItemClassForSkill = 2u;
constexpr std::array<std::uint8_t, 2> kMeleeEquipSlots = {kMainHandVisibleItemSlot,
                                                          kOffHandVisibleItemSlot};
constexpr std::array<std::uint8_t, 2> kMeleeSkillBonusRatings = {20u, 21u};

[[nodiscard]] std::uint32_t ResolveMeleeWeaponSkillLine(
    const openwow::game::CGPlayer_C &player,
    const openwow::data::dbc::DbcLoader &dbc,
    std::uint8_t equip_slot) {
  const auto item_meta = player.GetVisibleItemTemplateMetadata(equip_slot);
  std::uint32_t subclass_id = 0;
  bool have_subclass = false;

  if (item_meta.has_value()) {
    if (item_meta->item_class != kWeaponItemClassForSkill) return 0;
    subclass_id = item_meta->subclass;
    have_subclass = true;
  } else {

    for (const auto &entry : dbc.item_sub_class().entries()) {
      if (entry.class_id == kWeaponItemClassForSkill && (entry.flags & 0x4u) != 0) {
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

  for (const auto &ability : dbc.skill_line_ability().entries()) {
    if (ability.spell_id != spell_id ||
        !::openwow::game::SkillLineAbilityMatchesRaceClass(
            ability, player.State().GetRace(), player.State().GetClass())) {
      continue;
    }
    return ability.skill_id;
  }
  return 0;
}

struct AttackPowerValues {
  std::int32_t base = 0;
  std::int32_t positive = 0;
  std::int32_t negative = 0;
};

struct DamageModifierValues {
  float positive = 0.0f;
  float negative = 0.0f;
  float percent = 0.0f;
};

[[nodiscard]] const openwow::game::CGUnit_C *
ResolveStatsUnit(openwow::game::WorldSession *session, const std::string_view unit_id) {
  if (session == nullptr) {
    return nullptr;
  }
  return ResolveUnitObject(ResolveUnit(session, std::string(unit_id)));
}

DamageModifierValues ResolvePhysicalDamageModifiers(
    const openwow::game::CGUnit_C& unit) {
  if (!unit.IsPlayer()) {
    return {.percent = 1.0f};
  }

  const auto* player = dynamic_cast<const openwow::game::CGPlayer_C*>(&unit);
  if (player == nullptr || !player->IsActivePlayer()) {
    return {};
  }

  return {
      .positive = static_cast<float>(player->GetModDamageDonePositive(0)),
      .negative = static_cast<float>(player->GetModDamageDoneNegative(0)),
      .percent = player->GetModDamageDonePercent(0),
  };
}

std::uint8_t ResolveRangedDamageSchool(
    lua_State* L, const openwow::game::CGPlayer_C& player) {
  const auto entry = player.GetVisibleItemTemplateEntry(kRangedVisibleItemSlot);
  if (!entry.has_value()) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  const auto* item =
      session->query_cache().GetOrRequestItemTemplate(*entry);
  if (item == nullptr) {
    return 0;
  }

  for (const auto& damage : item->damage) {
    if (damage.min_damage > 0.0f) {
      return static_cast<std::uint8_t>(damage.type);
    }
  }

  return 0;
}

std::optional<openwow::game::VisibleItemTemplateMetadata>
ResolveOffhandWeaponMetadata(lua_State* L,
                             const openwow::game::WorldSession& session,
                             const openwow::game::CGPlayer_C& player) {
  if (const auto item_guid = player.GetEquippedItem(kOffHandVisibleItemSlot);
      !item_guid.IsEmpty()) {
    if (const auto* item = session.objects().GetItem(item_guid);
        item != nullptr) {
      if (const auto* item_template =
              RequireItemDefinitions(L).GetItem(item->GetEntry());
          item_template != nullptr) {
        return openwow::game::VisibleItemTemplateMetadata{
            .entry = item_template->entry,
            .item_class = static_cast<std::uint32_t>(item_template->item_class),
            .subclass = item_template->subclass,
            .inventory_type =
                static_cast<std::uint32_t>(item_template->inventory_type),
        };
      }
    }
  }

  return player.GetVisibleItemTemplateMetadata(kOffHandVisibleItemSlot);
}

std::int32_t RoundAttackPowerComponent(const float multiplier_factor,
                                       const std::int32_t component) {
  const float stored_product = multiplier_factor * static_cast<float>(component);
  return static_cast<std::int32_t>(std::nearbyint(stored_product));
}

AttackPowerValues ResolveAttackPowerValues(const openwow::game::CGObject_C& unit,
                                           const std::uint16_t base_field,
                                           const std::uint16_t mods_field,
                                           const std::uint16_t multiplier_field) {
  const auto base = static_cast<std::int32_t>(unit.GetUInt32(base_field));
  const auto mods_raw = unit.GetUInt32(mods_field);
  const auto positive = static_cast<std::int16_t>(mods_raw & 0xFFFFu);
  const auto negative = static_cast<std::int16_t>((mods_raw >> 16) & 0xFFFFu);
  const auto multiplier_factor = 1.0f + unit.GetFloat(multiplier_field);

  return {
      .base = RoundAttackPowerComponent(multiplier_factor, base),
      .positive = RoundAttackPowerComponent(multiplier_factor, positive),
      .negative = RoundAttackPowerComponent(multiplier_factor, negative),
  };
}

}

int LuaUnitStat(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2))
    return luaL_error(L, "Usage: UnitStat(\"unit\", statIndex)");

  const auto stat_index =
      static_cast<std::uint32_t>(openwow::ui::TruncateLuaNumberToI32(
          lua_tonumber(L, 2))) -
      1u;
  if (stat_index >= 5u) {
    return luaL_error(L, "Invalid stat index in UnitStat");
  }

  auto* session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);

  const auto* unit = ResolveStatsUnit(session, uid);
  if (!unit) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 4;
  }

  const auto stat = static_cast<std::uint8_t>(stat_index);
  lua_pushnumber(L, static_cast<lua_Number>(unit->State().GetStat(stat)));
  lua_pushnumber(
      L, static_cast<lua_Number>(unit->State().GetNonNegativeStat(stat)));
  lua_pushnumber(L, static_cast<lua_Number>(unit->State().GetPosStat(stat)));
  lua_pushnumber(L, static_cast<lua_Number>(unit->State().GetNegStat(stat)));
  return 4;
}

int LuaUnitDamage(lua_State* L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitDamage(\"unit\")");
  auto* session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto* unit = ResolveStatsUnit(session, uid);

  if (!unit) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 7;
  }

  float min_dmg     = unit->GetFloat(UNIT_FIELD_MINDAMAGE);
  float max_dmg     = unit->GetFloat(UNIT_FIELD_MAXDAMAGE);
  float off_min_dmg = unit->GetFloat(UNIT_FIELD_MINOFFHANDDAMAGE);
  float off_max_dmg = unit->GetFloat(UNIT_FIELD_MAXOFFHANDDAMAGE);

  const auto modifiers = ResolvePhysicalDamageModifiers(
      *unit);

  lua_pushnumber(L, static_cast<lua_Number>(min_dmg));
  lua_pushnumber(L, static_cast<lua_Number>(max_dmg));
  lua_pushnumber(L, static_cast<lua_Number>(off_min_dmg));
  lua_pushnumber(L, static_cast<lua_Number>(off_max_dmg));
  lua_pushnumber(L, static_cast<lua_Number>(modifiers.positive));
  lua_pushnumber(L, static_cast<lua_Number>(modifiers.negative));
  lua_pushnumber(L, static_cast<lua_Number>(modifiers.percent));
  return 7;
}

int LuaUnitRangedDamage(lua_State* L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitRangedDamage(\"unit\")");
  auto* session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto* unit = ResolveStatsUnit(session, uid);

  if (!unit) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 6;
  }

  float ranged_speed = static_cast<float>(unit->GetUInt32(UNIT_FIELD_RANGEDATTACKTIME)) / 1000.0f;
  float min_ranged = unit->GetFloat(UNIT_FIELD_MINRANGEDDAMAGE);
  float max_ranged = unit->GetFloat(UNIT_FIELD_MAXRANGEDDAMAGE);

  lua_pushnumber(L, static_cast<lua_Number>(ranged_speed));
  lua_pushnumber(L, static_cast<lua_Number>(min_ranged));
  lua_pushnumber(L, static_cast<lua_Number>(max_ranged));

  if (!unit->IsPlayer()) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1);
    return 6;
  }

  float pos_buff = 0.0f;
  float neg_buff = 0.0f;
  float pct_mod = 0.0f;

  if (unit->GetGuid() == openwow::game::CGObject_C::GetActivePlayerGuid()) {
    if (const auto* player = dynamic_cast<const openwow::game::CGPlayer_C*>(unit);
        player != nullptr) {
      const auto school = ResolveRangedDamageSchool(L, *player);
      pos_buff = static_cast<float>(player->GetModDamageDonePositive(school));
      neg_buff = static_cast<float>(player->GetModDamageDoneNegative(school));
      pct_mod = player->GetModDamageDonePercent(school);
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(pos_buff));
  lua_pushnumber(L, static_cast<lua_Number>(neg_buff));
  lua_pushnumber(L, static_cast<lua_Number>(pct_mod));
  return 6;
}

int LuaUnitAttackSpeed(lua_State* L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitAttackSpeed(\"unit\")");
  auto* session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto* unit = ResolveStatsUnit(session, uid);

  if (!unit) {
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    return 2;
  }

  float main_speed = static_cast<float>(unit->GetUInt32(UNIT_FIELD_BASEATTACKTIME)) / 1000.0f;
  lua_pushnumber(L, static_cast<lua_Number>(main_speed));

  const auto* player = dynamic_cast<const openwow::game::CGPlayer_C*>(unit);
  if (player == nullptr) {
    lua_pushnil(L);
    return 2;
  }

  if (session == nullptr) {
    lua_pushnil(L);
    return 2;
  }

  const auto offhand_metadata =
      ResolveOffhandWeaponMetadata(L, *session, *player);
  const auto* active_player = session->objects().GetLocalPlayerTyped();
  if (!offhand_metadata.has_value() ||
      offhand_metadata->item_class !=
          static_cast<std::uint32_t>(openwow::game::ItemClass::Weapon) ||
      (player->IsActivePlayer() &&
       (active_player == nullptr ||
        !active_player->Casts().CanEquipWeaponInOffHand()))) {
    lua_pushnil(L);
    return 2;
  }

  float off_speed = static_cast<float>(unit->GetUInt32(
      static_cast<std::uint16_t>(UNIT_FIELD_BASEATTACKTIME + 1))) / 1000.0f;
  lua_pushnumber(L, static_cast<lua_Number>(off_speed));
  return 2;
}

int LuaUnitAttackPower(lua_State* L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitAttackPower(\"unit\")");
  auto* session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto* unit = ResolveStatsUnit(session, uid);

  if (unit == nullptr) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
  }

  const auto values =
      ResolveAttackPowerValues(*unit, UNIT_FIELD_ATTACK_POWER,
                               UNIT_FIELD_ATTACK_POWER_MODS,
                               UNIT_FIELD_ATTACK_POWER_MULTIPLIER);
  lua_pushnumber(L, static_cast<lua_Number>(values.base));
  lua_pushnumber(L, static_cast<lua_Number>(values.positive));
  lua_pushnumber(L, static_cast<lua_Number>(values.negative));
  return 3;
}

int LuaUnitRangedAttackPower(lua_State* L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitAttackPower(\"unit\")");
  auto* session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto* unit = ResolveStatsUnit(session, uid);

  if (unit == nullptr) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
  }

  const auto values =
      ResolveAttackPowerValues(*unit, UNIT_FIELD_RANGED_ATTACK_POWER,
                               UNIT_FIELD_RANGED_ATTACK_POWER_MODS,
                               UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER);
  lua_pushnumber(L, static_cast<lua_Number>(values.base));
  lua_pushnumber(L, static_cast<lua_Number>(values.positive));
  lua_pushnumber(L, static_cast<lua_Number>(values.negative));
  return 3;
}

int LuaUnitArmor(lua_State* L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitArmor(\"unit\")");
  auto* session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto* unit = ResolveStatsUnit(session, uid);

  if (!unit) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 5;
  }

  const auto* dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
  if (dbc == nullptr) {
    dbc = GetDbcLoader(L);
  }

  const auto armor_school = static_cast<std::uint8_t>(
      openwow::data::DBClient_GetArmorResistanceIndex(dbc));
  const auto armor = unit->State().GetResistanceDisplayValues(armor_school);

  lua_pushnumber(L, static_cast<lua_Number>(armor.base_value));
  lua_pushnumber(L, static_cast<lua_Number>(armor.clamped_total));
  lua_pushnumber(L, static_cast<lua_Number>(unit->State().GetResistance(armor_school)));
  lua_pushnumber(L, static_cast<lua_Number>(armor.positive_modifier));
  lua_pushnumber(L, static_cast<lua_Number>(armor.negative_modifier));
  return 5;
}

int LuaUnitDefense(lua_State* L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitDefense(\"unit\")");
  auto* session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto* unit = ResolveStatsUnit(session, uid);

  std::int32_t base = 0;
  std::int32_t modifier = 0;

  if (unit) {
    const auto player_guid = openwow::game::CGObject_C::GetActivePlayerGuid();
    const bool is_player = (unit->GetGuid() == player_guid);
    const bool is_player_pet = (!is_player &&
        unit->State().GetSummonedBy() == player_guid);

    if (is_player || is_player_pet) {
      base = static_cast<std::int32_t>(unit->State().GetLevel()) * 5;

      if (is_player) {
        if (const auto* player =
                dynamic_cast<const openwow::game::CGPlayer_C*>(unit);
            player != nullptr) {
          const auto* dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
          if (dbc == nullptr)
            dbc = GetDbcLoader(L);
          if (dbc != nullptr) {
            constexpr std::uint8_t kCRDefenseSkill = 1;
            modifier = static_cast<std::int32_t>(
                openwow::game::ComputeCombatRatingBonus(
                    *player, *dbc, kCRDefenseSkill));
          }
        }
      }

      if (base + modifier < 0)
        modifier = -base;
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(base));
  lua_pushnumber(L, static_cast<lua_Number>(modifier));
  return 2;
}

int LuaUnitAttackBothHands(lua_State* L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitAttackBothHands(\"unit\")");
  auto* session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto* unit = ResolveStatsUnit(session, uid);

  auto push_zeros = [&]() {
    for (int i = 0; i < 4; ++i) lua_pushnumber(L, 0);
    return 4;
  };

  if (!unit)
    return push_zeros();

  const auto active_guid = openwow::game::CGObject_C::GetActivePlayerGuid();
  bool accessible = (unit->GetGuid() == active_guid);
  if (!accessible)
    accessible = unit->State().GetSummonedBy() == active_guid;
  if (!accessible)
    return push_zeros();

  std::int32_t base[2] = {0, 0};
  std::int32_t modifier[2] = {0, 0};

  if (unit->IsPlayer()) {

    const auto* player =
        session ? session->objects().GetLocalPlayerTyped() : nullptr;
    const auto* dbc = GetDbcLoader(L);
    if (player && dbc && player->GetGuid() == unit->GetGuid()) {
      for (int slot = 0; slot < 2; ++slot) {
        auto skill_line_id =
            ResolveMeleeWeaponSkillLine(*player, *dbc, kMeleeEquipSlots[slot]);

        if (skill_line_id != 0) {
          if (auto skill_slot = player->FindActiveSkillSlot(
                  static_cast<std::uint16_t>(skill_line_id))) {
            auto skill = player->GetSkill(*skill_slot);
            base[slot] = skill.value;
            if (base[slot] != 0)
              base[slot] += static_cast<std::uint16_t>(skill.step_modifier);
            modifier[slot] = skill.modifier;
          }
        }

        modifier[slot] +=
            static_cast<std::int32_t>(
                openwow::game::ComputeCombatRatingBonus(*player, *dbc, 0));
        modifier[slot] += static_cast<std::int32_t>(
            openwow::game::ComputeCombatRatingBonus(
                *player, *dbc, kMeleeSkillBonusRatings[slot]));
      }
    }
  } else {

    auto level = static_cast<std::int32_t>(unit->GetUInt32(UNIT_FIELD_LEVEL));
    base[0] = base[1] = 5 * level;
  }

  for (int i = 0; i < 2; ++i) {
    if (base[i] + modifier[i] < 0)
      modifier[i] = -base[i];
  }

  for (int i = 0; i < 2; ++i) {
    lua_pushnumber(L, static_cast<lua_Number>(base[i]));
    lua_pushnumber(L, static_cast<lua_Number>(modifier[i]));
  }
  return 4;
}

int LuaGetCritChance(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }
  const auto* player = session->objects().GetLocalPlayer();
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }

  float crit = player->GetFloat(PLAYER_CRIT_PERCENTAGE);
  lua_pushnumber(L, static_cast<lua_Number>(crit));
  return 1;
}

int LuaGetSpellPenetration(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }
  const auto* player = session->objects().GetLocalPlayer();
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }

  auto value = static_cast<std::int32_t>(player->GetUInt32(PLAYER_FIELD_MOD_TARGET_RESISTANCE));
  lua_pushnumber(L, static_cast<lua_Number>(-value));
  return 1;
}

}
