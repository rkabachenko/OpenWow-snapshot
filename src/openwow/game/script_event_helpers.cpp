
#include "openwow/game/script_event_helpers.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/aura_lua_bridge.h"
#include "openwow/game/inventory/items/item_scaling.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/player_unit_field_event_callbacks.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/game/world_session.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/ui/game/tooltip_formatter.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_catalog.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/lua_numeric.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <lua.hpp>
}

namespace openwow::game {

namespace {

constexpr float kDpsEpsilon = 1.1920929e-7f;
constexpr std::size_t kModifierCount = 73;
constexpr std::size_t kModifierArrayOffset = 1;
constexpr std::size_t kSocketCount = 3;

constexpr std::array<int, 25> kAura189StatMap = {
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, -1, -1, -1, 37, 44,
};

constexpr std::array<std::uint32_t, 2> kMergeSpellPowerTargets = {53, 52};
constexpr std::array<std::uint32_t, 3> kMergeHitTargets = {27, 28, 29};
constexpr std::array<std::uint32_t, 3> kMergeCritTargets = {30, 31, 32};
constexpr std::array<std::uint32_t, 3> kMergeHitTakenTargets = {33, 34, 35};
constexpr std::array<std::uint32_t, 3> kMergeCritTakenTargets = {36, 37, 38};
constexpr std::array<std::uint32_t, 3> kMergeHasteTargets = {39, 40, 41};
constexpr std::array<std::uint32_t, 1> kMergeManaRegenTargets = {54};
constexpr std::array<std::uint32_t, 2> kMergeAttackPowerTargets = {8, 9};

constexpr std::size_t kStatTableFlagsIndex     = 74;
constexpr std::uint32_t kStatFlagHasWeaponDPS  = 0x04u;

constexpr std::size_t kStatTableMeleeAPIndex = 9;
constexpr std::size_t kStatTableFeralAPIndex = 11;

constexpr std::uint8_t kClassDruid = 11;

bool EqualsIgnoreCaseAscii(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }

  for (std::size_t i = 0; i < left.size(); ++i) {
    const auto lhs = static_cast<unsigned char>(left[i]);
    const auto rhs = static_cast<unsigned char>(right[i]);
    const char lhs_lower =
        static_cast<char>(lhs >= 'A' && lhs <= 'Z' ? lhs + ('a' - 'A') : lhs);
    const char rhs_lower =
        static_cast<char>(rhs >= 'A' && rhs <= 'Z' ? rhs + ('a' - 'A') : rhs);
    if (lhs_lower != rhs_lower) {
      return false;
    }
  }

  return true;
}

bool IsAuraFilterDelimiter(const char value) {
  return value == ' ' || value == '|';
}

void SetStatTableFlag(ItemStatTable& stat_values, const std::uint32_t flag_bits) {
  std::uint32_t current = 0;
  std::memcpy(&current, &stat_values[kStatTableFlagsIndex], sizeof(current));
  current |= flag_bits;
  std::memcpy(&stat_values[kStatTableFlagsIndex], &current, sizeof(current));
}

bool IsActivePlayerDruid(const CGPlayer_C& active_player) {
  return active_player.State().GetClass() == kClassDruid;
}

bool ResolveScriptEventUnitGuid(WorldSession& session,
                                const char* unit_id, const bool allow_empty,
                                ObjectGuid* out_guid) {
  if (out_guid == nullptr) {
    return false;
  }

  *out_guid = ObjectGuid();

  if (allow_empty && (unit_id == nullptr || *unit_id == '\0')) {
    *out_guid = session.objects().GetMouseoverGuid();
    return true;
  }

  if (unit_id == nullptr) {
    return false;
  }

  const std::string_view unit_token(unit_id);
  if (const auto parsed = ParseUnitId(unit_token); parsed.kind != UnitIdKind::kUnknown) {
    *out_guid = UnitQueryBridge::Get().ResolveToGuid(&session, unit_token);
    return true;
  }

  *out_guid = UnitQueryBridge::Get().ResolveToGuid(&session, unit_token);
  return !out_guid->IsEmpty();
}

void* ResolveLiveUnitObject(ObjectManager& objects, ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return nullptr;
  }

  return objects.GetMutableUnit(guid);
}

inline std::size_t ModifierValueIndex(const std::uint32_t mod_id) {
  return kModifierArrayOffset + mod_id;
}

float GetModifierValue(const ItemStatTable& stat_values, const std::uint32_t mod_id) {
  return stat_values[ModifierValueIndex(mod_id)];
}

void AddModifierValue(ItemStatTable& stat_values, const std::uint32_t mod_id, const float value) {
  stat_values[ModifierValueIndex(mod_id)] += value;
}

bool ParseItemLinkFields(const char* item_link, std::uint32_t* item_id,
                         std::int32_t* random_property_id, std::uint32_t* suffix_factor,
                         float* link_level) {
  if (item_link == nullptr || item_id == nullptr || random_property_id == nullptr ||
      suffix_factor == nullptr || link_level == nullptr) {
    return false;
  }

  const auto parsed_link = ItemLinkParser::Parse(item_link);
  if (!parsed_link.has_value() || parsed_link->itemId == 0) {
    return false;
  }

  *item_id = parsed_link->itemId;
  *random_property_id = parsed_link->randomPropertyId;
  *suffix_factor = static_cast<std::uint32_t>(parsed_link->suffixFactor > 0
                                                  ? parsed_link->suffixFactor
                                                  : 0);
  *link_level = static_cast<float>(parsed_link->linkLevel);
  return true;
}

void ApplyBaseItemStat(ItemStatTable& stat_values, const std::uint32_t stat_type,
                       const std::int32_t stat_value) {
  AddModifierValue(stat_values, stat_type + 11u, static_cast<float>(stat_value));
  if (stat_type == 38u) {
    AddModifierValue(stat_values, 8u, static_cast<float>(stat_value));
    AddModifierValue(stat_values, 9u, static_cast<float>(stat_value));
  } else if (stat_type == 39u) {
    AddModifierValue(stat_values, 9u, static_cast<float>(stat_value));
  }
}

void ApplySpellStatAura(ItemStatTable& stat_values, const openwow::data::dbc::SpellEntry& spell) {
  for (std::size_t effect_index = 0; effect_index < spell.effect.size(); ++effect_index) {
    if (spell.effect[effect_index] != 6u) {
      continue;
    }

    const auto amount = static_cast<float>(spell.effect_base_points[effect_index] + 1);
    const auto aura = spell.effect_apply_aura[effect_index];
    const auto misc = spell.effect_misc_value[effect_index];

    switch (aura) {
    case 0x0Du:
      AddModifierValue(stat_values, 53u, amount);
      break;
    case 0x16u:
      if ((misc & 0x1) != 0) AddModifierValue(stat_values, 0u, amount);
      if ((misc & 0x2) != 0) AddModifierValue(stat_values, 1u, amount);
      if ((misc & 0x4) != 0) AddModifierValue(stat_values, 2u, amount);
      if ((misc & 0x8) != 0) AddModifierValue(stat_values, 3u, amount);
      if ((misc & 0x10) != 0) AddModifierValue(stat_values, 4u, amount);
      if ((misc & 0x20) != 0) AddModifierValue(stat_values, 5u, amount);
      if ((misc & 0x40) != 0) AddModifierValue(stat_values, 6u, amount);
      break;
    case 0x55u:
      if (misc >= 0) {
        AddModifierValue(stat_values, static_cast<std::uint32_t>(misc) + 61u, amount);
      }
      break;
    case 0x63u:
      AddModifierValue(stat_values, 8u, amount);
      break;
    case 0x7Bu:
      AddModifierValue(stat_values, 58u, -amount);
      break;
    case 0x7Cu:
      AddModifierValue(stat_values, 9u, amount);
      break;
    case 0x87u:
      AddModifierValue(stat_values, 52u, amount);
      break;
    case 0x96u:
      AddModifierValue(stat_values, 26u, amount);
      break;
    case 0x9Eu:
      AddModifierValue(stat_values, 60u, amount);
      break;
    case 0xA1u:
      AddModifierValue(stat_values, 68u, amount);
      break;
    case 0xBDu:
      for (std::size_t bit_index = 0; bit_index < kAura189StatMap.size(); ++bit_index) {
        if ((misc & (1 << bit_index)) == 0) {
          continue;
        }
        const auto mapped_stat = kAura189StatMap[bit_index];
        if (mapped_stat >= 0) {
          AddModifierValue(stat_values, static_cast<std::uint32_t>(mapped_stat) + 11u, amount);
        }
      }
      break;
    default:
      break;
    }
  }
}

void ApplyItemTemplateArmorResistanceBlockStats(ItemStatTable& stat_values,
                                                const ItemTemplate& item_template) {
  AddModifierValue(stat_values, 0u, static_cast<float>(item_template.armor));
  const std::array resistances{
      item_template.holy_res, item_template.fire_res,
      item_template.nature_res, item_template.frost_res,
      item_template.shadow_res, item_template.arcane_res};
  for (std::size_t resistance_index = 0;
       resistance_index < resistances.size(); ++resistance_index) {
    AddModifierValue(stat_values, static_cast<std::uint32_t>(resistance_index) + 1u,
                     static_cast<float>(resistances[resistance_index]));
  }

  AddModifierValue(stat_values, 60u, static_cast<float>(item_template.block));
}

void ApplyItemTemplateSpellAndSocketStats(ItemStatTable& stat_values,
                                          const ItemTemplate& item_template,
                                          const openwow::data::dbc::DbcLoader* dbc) {

  for (const auto& spell_info : item_template.spells) {
    if (spell_info.spell_id == 0 || dbc == nullptr) {
      continue;
    }
    if (const auto* spell = dbc->spell().LookupEntry(spell_info.spell_id); spell != nullptr) {
      ApplySpellStatAura(stat_values, *spell);
    }
  }

  for (std::size_t socket_index = 0; socket_index < kSocketCount; ++socket_index) {
    const auto color = item_template.sockets[socket_index].color;
    if ((color & 0x1u) != 0) AddModifierValue(stat_values, 69u, 1.0f);
    if ((color & 0x2u) != 0) AddModifierValue(stat_values, 70u, 1.0f);
    if ((color & 0x4u) != 0) AddModifierValue(stat_values, 71u, 1.0f);
    if ((color & 0x8u) != 0) AddModifierValue(stat_values, 72u, 1.0f);
  }
}

void ApplyItemTemplateWeaponDps(ItemStatTable& stat_values,
                                const ItemTemplate& item_template,
                                const CGPlayer_C& active_player) {
  SetStatTableFlag(stat_values, kStatFlagHasWeaponDPS);

  if (item_template.delay == 0u) {
    return;
  }
  const auto speed_seconds = static_cast<float>(item_template.delay) * 0.001f;
  if (speed_seconds <= 0.0f) {
    return;
  }

  const auto damage0 =
      0.5f * (item_template.damage[0].min_damage + item_template.damage[0].max_damage);
  const auto damage1 =
      0.5f * (item_template.damage[1].min_damage + item_template.damage[1].max_damage);
  stat_values[0] += damage0 / speed_seconds;
  stat_values[0] += damage1 / speed_seconds;

  if (IsActivePlayerDruid(active_player)) {
    const auto total_avg_damage = damage0 + damage1;
    const auto feral_bonus = item_scaling::CalculateFeralAPBonusFromWeaponDPS(
        static_cast<std::uint32_t>(item_template.item_class),
        static_cast<std::uint32_t>(item_template.inventory_type),
        total_avg_damage, item_template.delay);
    if (feral_bonus != 0) {
      const auto melee_ap = stat_values[kStatTableMeleeAPIndex];
      stat_values[kStatTableFeralAPIndex] +=
          melee_ap + static_cast<float>(feral_bonus);
    }
  }
}

void ApplyBaseItemTemplateStats(ItemStatTable& stat_values,
                                const ItemTemplate& item_template,
                                const openwow::data::dbc::DbcLoader* dbc,
                                const CGPlayer_C& active_player) {
  for (const auto& stat : item_template.stats) {
    ApplyBaseItemStat(stat_values, stat.type, stat.value);
  }

  ApplyItemTemplateArmorResistanceBlockStats(stat_values, item_template);
  ApplyItemTemplateSpellAndSocketStats(stat_values, item_template, dbc);
  ApplyItemTemplateWeaponDps(stat_values, item_template, active_player);
}

std::uint32_t ResolveScalingTooltipLevel(const float link_level,
                                         const CGPlayer_C& active_player) {
  if (link_level > 0.0f) {
    return static_cast<std::uint32_t>(link_level);
  }

  return active_player.State().GetLevel();
}

bool ApplyScalingItemTemplateStats(ItemStatTable& stat_values,
                                   const ItemTemplate& item_template,
                                   const openwow::data::dbc::DbcLoader* dbc,
                                   const std::uint32_t requested_level,
                                   const CGPlayer_C& active_player) {
  const auto* distribution = item_scaling::FindScalingStatDistribution(item_template, dbc);
  if (distribution == nullptr) {
    return false;
  }

  const auto resolved_level =
      item_scaling::ResolveScalingItemLevel(item_template, *distribution, requested_level);
  const auto* values = item_scaling::FindScalingStatValuesByLevel(dbc, resolved_level);
  if (values == nullptr) {
    return false;
  }

  ApplyItemTemplateSpellAndSocketStats(stat_values, item_template, dbc);

  const auto scaling_mask = item_template.scaling_stat_value;
  const auto stat_budget = item_scaling::SelectScalingStatBudget(*values, scaling_mask);
  if (stat_budget != 0u) {
    for (std::size_t index = 0; index < distribution->stat_id.size(); ++index) {
      const auto stat_id = distribution->stat_id[index];
      if (stat_id < 0) {
        continue;
      }

      const auto value =
          item_scaling::ComputeScalingDistributionValue(stat_budget, distribution->bonus[index]);
      if (value == 0) {
        continue;
      }

      ApplyBaseItemStat(stat_values, static_cast<std::uint32_t>(stat_id), value);
    }
  } else {
    for (const auto& stat : item_template.stats) {
      ApplyBaseItemStat(stat_values, stat.type, stat.value);
    }
  }

  if ((scaling_mask & item_scaling::kScalingArmorMask) != 0u) {
    const auto scaled_armor = item_scaling::SelectScalingArmorValue(*values, scaling_mask);
    if (scaled_armor != 0u) {
      AddModifierValue(stat_values, 0u, static_cast<float>(scaled_armor));
    }
  } else {
    ApplyItemTemplateArmorResistanceBlockStats(stat_values, item_template);
  }

  if (const auto weapon_dps = item_scaling::SelectScalingWeaponDpsInfo(*values, scaling_mask);
      weapon_dps.has_value()) {
    stat_values[0] += weapon_dps->base_dps;

    if (IsActivePlayerDruid(active_player) && item_template.delay != 0u) {
      const auto total_avg_damage =
          (weapon_dps->min_dps + weapon_dps->max_dps) *
          static_cast<float>(item_template.delay) * 0.0005f;
      const auto feral_bonus = item_scaling::CalculateFeralAPBonusFromWeaponDPS(
          static_cast<std::uint32_t>(item_template.item_class),
          static_cast<std::uint32_t>(item_template.inventory_type),
          total_avg_damage, item_template.delay);
      if (feral_bonus != 0) {
        const auto melee_ap = stat_values[kStatTableMeleeAPIndex];
        stat_values[kStatTableFeralAPIndex] +=
            melee_ap + static_cast<float>(feral_bonus);
      }
    }
  } else {
    ApplyItemTemplateWeaponDps(stat_values, item_template, active_player);
  }

  if ((scaling_mask & item_scaling::kScalingSpellPowerFlag) != 0u) {
    const auto spell_power = values->GetField(16);
    if (spell_power != 0u) {
      AddModifierValue(stat_values, 56u, static_cast<float>(spell_power));
    }
  }

  return true;
}

float ComputeRandomSuffixScaledAmount(const std::uint32_t allocation_pct,
                                      const std::uint32_t suffix_factor) {
  const auto value = static_cast<double>(allocation_pct) * 0.0001 *
                     static_cast<double>(suffix_factor);
  return static_cast<float>(std::lround(value));
}

void ApplySpellItemEnchantment(ItemStatTable& stat_values,
                               const openwow::data::dbc::SpellItemEnchantmentEntry& enchantment,
                               const openwow::data::dbc::DbcLoader* dbc, const bool use_scaled_amount,
                               const float scaled_amount) {
  for (std::size_t effect_index = 0; effect_index < enchantment.type.size(); ++effect_index) {
    const auto amount = use_scaled_amount
                            ? scaled_amount
                            : static_cast<float>((enchantment.amount[effect_index] +
                                                  enchantment.amount_max[effect_index]) / 2);
    switch (enchantment.type[effect_index]) {
    case 3u:
      if (dbc == nullptr || enchantment.spell_id[effect_index] == 0u) {
        break;
      }
      if (const auto* spell = dbc->spell().LookupEntry(enchantment.spell_id[effect_index]);
          spell != nullptr) {
        ApplySpellStatAura(stat_values, *spell);
      }
      break;
    case 4u:
      AddModifierValue(stat_values, enchantment.spell_id[effect_index], amount);
      break;
    case 5u:
      AddModifierValue(stat_values, enchantment.spell_id[effect_index] + 11u, amount);
      if (enchantment.spell_id[effect_index] == 38u) {
        AddModifierValue(stat_values, 8u, amount);
        AddModifierValue(stat_values, 9u, amount);
      } else if (enchantment.spell_id[effect_index] == 39u) {
        AddModifierValue(stat_values, 9u, amount);
      }
      break;
    default:
      break;
    }
  }
}

void ApplyRandomPropertyStats(ItemStatTable& stat_values, const std::int32_t random_property_id,
                              const std::uint32_t suffix_factor,
                              const openwow::data::dbc::DbcLoader* dbc) {
  if (random_property_id == 0 || dbc == nullptr) {
    return;
  }

  if (random_property_id > 0) {
    const auto* property = dbc->item_random_properties().LookupEntry(
        static_cast<std::uint32_t>(random_property_id));
    if (property == nullptr) {
      return;
    }

    for (const auto enchant_id : property->enchantment) {
      if (enchant_id == 0) {
        continue;
      }
      const auto* enchantment = dbc->spell_item_enchantment().LookupEntry(enchant_id);
      if (enchantment != nullptr) {
        ApplySpellItemEnchantment(stat_values, *enchantment, dbc, false, 0.0f);
      }
    }
    return;
  }

  const auto* suffix = dbc->item_random_suffix().LookupEntry(
      static_cast<std::uint32_t>(-random_property_id));
  if (suffix == nullptr) {
    return;
  }

  for (std::size_t slot = 0; slot < suffix->enchantment.size(); ++slot) {
    const auto enchant_id = suffix->enchantment[slot];
    if (enchant_id == 0) {
      continue;
    }
    const auto* enchantment = dbc->spell_item_enchantment().LookupEntry(enchant_id);
    if (enchantment == nullptr) {
      continue;
    }
    ApplySpellItemEnchantment(stat_values, *enchantment, dbc, true,
                              ComputeRandomSuffixScaledAmount(suffix->allocation_pct[slot],
                                                              suffix_factor));
  }
}

template <std::size_t N>
void NormalizeCompositeModifier(ItemStatTable& stat_values, const std::uint32_t source_mod,
                                const std::array<std::uint32_t, N>& target_mods) {
  const auto source_value = GetModifierValue(stat_values, source_mod);
  bool keep_source = true;
  float first_target_value = 0.0f;

  for (std::size_t target_index = 0; target_index < target_mods.size(); ++target_index) {
    auto& target_value = stat_values[ModifierValueIndex(target_mods[target_index])];
    target_value += source_value;
    if (target_index == 0) {
      first_target_value = target_value;
    } else if (target_value != first_target_value) {
      keep_source = false;
    }
  }

  if (keep_source) {
    stat_values[ModifierValueIndex(source_mod)] = first_target_value;
    for (const auto target_mod : target_mods) {
      stat_values[ModifierValueIndex(target_mod)] = 0.0f;
    }
  } else {
    stat_values[ModifierValueIndex(source_mod)] = 0.0f;
  }
}

void NormalizeItemStatTable(ItemStatTable& stat_values) {
  if (GetModifierValue(stat_values, 8u) == GetModifierValue(stat_values, 10u)) {
    stat_values[ModifierValueIndex(10u)] = 0.0f;
  }

  NormalizeCompositeModifier(stat_values, 56u, kMergeSpellPowerTargets);
  NormalizeCompositeModifier(stat_values, 42u, kMergeHitTargets);
  NormalizeCompositeModifier(stat_values, 43u, kMergeCritTargets);
  NormalizeCompositeModifier(stat_values, 44u, kMergeHitTakenTargets);
  NormalizeCompositeModifier(stat_values, 45u, kMergeCritTakenTargets);
  NormalizeCompositeModifier(stat_values, 47u, kMergeHasteTargets);
  NormalizeCompositeModifier(stat_values, 61u, kMergeManaRegenTargets);
  NormalizeCompositeModifier(stat_values, 7u, kMergeAttackPowerTargets);
}

}

std::uint8_t ParseAuraFilterFlags(const std::string_view filter,
                                  const std::uint8_t initial_flags) {
  std::uint8_t flags = initial_flags;
  std::size_t offset = 0;

  while (offset < filter.size()) {
    while (offset < filter.size() && IsAuraFilterDelimiter(filter[offset])) {
      ++offset;
    }
    if (offset >= filter.size()) {
      break;
    }

    const std::size_t token_start = offset;
    while (offset < filter.size() && !IsAuraFilterDelimiter(filter[offset])) {
      ++offset;
    }

    const std::string_view token = filter.substr(token_start, offset - token_start);
    if (EqualsIgnoreCaseAscii(token, "HELPFUL")) {
      flags = static_cast<std::uint8_t>((flags & 0xfcU) | 0x01U);
    } else if (EqualsIgnoreCaseAscii(token, "HARMFUL")) {
      flags = static_cast<std::uint8_t>((flags & 0xfcU) | 0x02U);
    } else if (EqualsIgnoreCaseAscii(token, "PLAYER")) {
      flags = static_cast<std::uint8_t>(flags | 0x04U);
    } else if (EqualsIgnoreCaseAscii(token, "RAID")) {
      flags = static_cast<std::uint8_t>(flags | 0x08U);
    } else if (EqualsIgnoreCaseAscii(token, "CANCELABLE")) {
      flags = static_cast<std::uint8_t>(flags | 0x10U);
    } else if (EqualsIgnoreCaseAscii(token, "NOT_CANCELABLE")) {
      flags = static_cast<std::uint8_t>(flags | 0x20U);
    }
  }

  return flags;
}

std::string AuraFilterFlagsToFilterString(const std::uint8_t flags) {
  std::string filter;
  const auto append = [&filter](const char* token) {
    if (!filter.empty()) {
      filter.push_back('|');
    }
    filter.append(token);
  };

  if ((flags & 0x03U) == 0x01U) {
    append("HELPFUL");
  } else if ((flags & 0x03U) == 0x02U) {
    append("HARMFUL");
  }
  if ((flags & 0x04U) != 0U) {
    append("PLAYER");
  }
  if ((flags & 0x08U) != 0U) {
    append("RAID");
  }
  if ((flags & 0x10U) != 0U) {
    append("CANCELABLE");
  }
  if ((flags & 0x20U) != 0U) {
    append("NOT_CANCELABLE");
  }

  return filter;
}

bool ParseAuraFilter(void* lua_state, int stack_index, AuraFilter* out) {
  if (lua_state == nullptr || out == nullptr) {
    return false;
  }

  auto* L = static_cast<lua_State*>(lua_state);
  if (lua_isnoneornil(L, stack_index) != 0) {
    out->index = 0;
    out->name = nullptr;
    out->rank = nullptr;
    out->flags = 1;
    return true;
  }
  if (lua_isnumber(L, stack_index) == 0 && lua_isstring(L, stack_index) == 0) {
    return false;
  }

  out->index = -1;
  out->name = nullptr;
  out->rank = nullptr;
  out->flags = 1;

  int filter_index = stack_index + 1;
  if (lua_isnumber(L, stack_index) != 0) {
    const auto one_based = openwow::ui::TruncateLuaNumberToI32(
        lua_tonumber(L, stack_index));
    out->index = openwow::ui::SignedI32FromU32Bits(
        static_cast<std::uint32_t>(one_based) - 1u);
  } else {
    out->name = lua_tostring(L, stack_index);
    out->rank = lua_tostring(L, stack_index + 1);
    filter_index = stack_index + 2;
  }

  if (lua_isstring(L, filter_index) != 0) {
    if (const char* filter = lua_tostring(L, filter_index); filter != nullptr) {
      out->flags = ParseAuraFilterFlags(filter, out->flags);
    }
  }

  return true;
}

void ClearItemStatTable(ItemStatTable& stat_values) {
  stat_values.fill(0.0f);
}

bool BuildItemStatTableFromTemplateInfo(const ItemTemplate& item_template,
                                        const std::int32_t random_property_id,
                                        const std::uint32_t suffix_factor,
                                        const openwow::data::dbc::DbcLoader* dbc,
                                        const std::uint32_t scaling_level,
                                        const CGPlayer_C& active_player,
                                        ItemStatTable& stat_values) {
  ClearItemStatTable(stat_values);

  const auto* distribution = item_scaling::FindScalingStatDistribution(item_template, dbc);

  std::uint32_t resolved_level = scaling_level;
  if (resolved_level == 0u) {
    resolved_level = active_player.State().GetLevel();
  }

  resolved_level = std::max(resolved_level, item_template.required_level);
  if (distribution != nullptr && distribution->max_level != 0u) {
    resolved_level = std::min(resolved_level, distribution->max_level);
  }
  resolved_level = std::max(resolved_level, 1u);

  if (item_template.scaling_stat_value != 0u) {
    if (!ApplyScalingItemTemplateStats(stat_values, item_template, dbc, resolved_level,
                                       active_player)) {
      return false;
    }
  } else {
    ApplyBaseItemTemplateStats(stat_values, item_template, dbc, active_player);
  }

  if (random_property_id != 0) {
    ApplyRandomPropertyStats(stat_values, random_property_id, suffix_factor, dbc);
  }

  NormalizeItemStatTable(stat_values);
  return true;
}

bool BuildItemStatTableFromItemLink(const char* item_link, const QueryCache& query_cache,
                                    const openwow::data::dbc::DbcLoader* dbc,
                                    const CGPlayer_C& active_player,
                                    ItemStatTable& stat_values) {
  std::uint32_t item_id = 0;
  std::int32_t random_property_id = 0;
  std::uint32_t suffix_factor = 0;
  float link_level = 0.0f;
  if (!ParseItemLinkFields(item_link, &item_id, &random_property_id, &suffix_factor,
                           &link_level)) {
    return false;
  }

  const auto* item_template = query_cache.GetItemTemplate(item_id);
  if (item_template == nullptr) {
    return false;
  }

  return BuildItemStatTableFromTemplateInfo(*item_template, random_property_id, suffix_factor, dbc,
                                            ResolveScalingTooltipLevel(link_level, active_player),
                                            active_player, stat_values);
}

void BuildItemStatDelta(const ItemStatTable& left, const ItemStatTable& right,
                        ItemStatTable& out_delta) {
  ClearItemStatTable(out_delta);
  out_delta[0] = left[0] - right[0];
  for (std::size_t index = 0; index < kModifierCount; ++index) {
    out_delta[index + 1] = left[index + 1] - right[index + 1];
  }
  NormalizeItemStatTable(out_delta);
}

int PushItemStatFields(const ItemStatTable& stat_values, void* lua_state) {
  auto* const lua = static_cast<lua_State*>(lua_state);
  if (lua == nullptr) {
    return 0;
  }

  if (std::fabs(stat_values[0]) > kDpsEpsilon) {
    lua_pushstring(lua, "ITEM_MOD_DAMAGE_PER_SECOND_SHORT");
    lua_pushnumber(lua, stat_values[0]);
    lua_settable(lua, -3);
  }

  char modifier_name[1024] = {};
  for (std::uint32_t mod_id = 0; mod_id < kModifierCount; ++mod_id) {
    if (mod_id == 49u || mod_id == 50u) {
      continue;
    }

    const auto value = stat_values[ModifierValueIndex(mod_id)];
    if (value == 0.0f) {
      continue;
    }

    lua_pushstring(lua, openwow::ui::game::GetStatModifierGlobalStringName(
                            mod_id, modifier_name, sizeof(modifier_name)));
    lua_pushnumber(lua, static_cast<int>(value));
    lua_settable(lua, -3);
  }

  return 1;
}

static bool s_world_event_names_initialized = false;

void InitWorldEventNames() {
    if (s_world_event_names_initialized) {
        return;
    }

    for (std::uint32_t event_id = 0; event_id < kUnitFieldEventSlotCount; ++event_id) {
        if (const char* event_name = GetUnitFieldEventName(event_id); event_name != nullptr) {
            ui::frame_script_events::FrameScript_RegisterEventName(
                static_cast<int>(event_id), event_name);
        }
    }

    ui::frame_script_events::FrameScript_RegisterEventName(
        static_cast<int>(kEventInventoryChanged), "UNIT_INVENTORY_CHANGED");
    ui::frame_script_events::FrameScript_RegisterEventName(
        static_cast<int>(kEventQuestLogChanged), "UNIT_QUEST_LOG_CHANGED");

    s_world_event_names_initialized = true;
}

static char s_resolve_name_realm_buf[305];

static char s_resolve_name_buf[256];

bool ScriptEvents_ResolveUnitName(WorldSession& session,
                                  const char* unit_id, const char** out_name,
                                   bool allow_enemy, bool include_realm) {
    if (!unit_id || !out_name) return false;

    ObjectGuid guid;
    if (!ResolveScriptEventUnitGuid(session, unit_id, allow_enemy, &guid)) {
        return false;
    }

    const auto* unit = session.objects().GetUnit(guid);
    if (unit != nullptr) {

        std::string realm;
        const std::string name =
            unit->ResolveRetailName(session, include_realm ? &realm : nullptr);
        if (name.empty()) {
            *out_name = nullptr;
            return true;
        }

        if (include_realm && guid.IsPlayer() && !realm.empty()) {
            std::snprintf(s_resolve_name_realm_buf,
                          sizeof(s_resolve_name_realm_buf),
                          "%s-%s", name.c_str(), realm.c_str());
            *out_name = s_resolve_name_realm_buf;
            return true;
        }

        std::snprintf(s_resolve_name_buf, sizeof(s_resolve_name_buf),
                      "%s", name.c_str());
        *out_name = s_resolve_name_buf;
        return true;
    }

    const auto* player_info =
        session.query_cache().GetPlayerName(guid.GetRawValue());
    if (player_info == nullptr) {

        return true;
    }

    if (include_realm && !player_info->realm_name.empty()) {
        std::snprintf(s_resolve_name_realm_buf,
                      sizeof(s_resolve_name_realm_buf),
                      "%s-%s", player_info->name.c_str(),
                      player_info->realm_name.c_str());
        *out_name = s_resolve_name_realm_buf;
        return true;
    }

    *out_name = player_info->name.c_str();
    return true;
}

uint32_t ScriptEvents_GetUnitXP(void* unit_obj) {
    if (!unit_obj) return 0;
    auto* obj = static_cast<CGObject_C*>(unit_obj);
    if (!obj->IsActivePlayer()) return 0;
    return obj->GetUInt32(PLAYER_XP);
}

uint32_t ScriptEvents_GetUnitNextLevelXP(void* unit_obj) {
    if (!unit_obj) return 0;
    auto* obj = static_cast<CGObject_C*>(unit_obj);
    if (!obj->IsActivePlayer()) return 0;
    return obj->GetUInt32(PLAYER_NEXT_LEVEL_XP);
}

bool ScriptEvents_ResolveUnitObject(WorldSession& session,
                                    const char* unit_id, void** out_unit,
                                    bool allow_empty) {
  if (out_unit == nullptr) {
    return false;
  }

  ObjectGuid guid;
  if (!ResolveScriptEventUnitGuid(session, unit_id, allow_empty, &guid)) {
    return false;
  }

  *out_unit = ResolveLiveUnitObject(session.objects(), guid);
  return true;
}

void* ScriptEvents_GetUnit(WorldSession& session, const char* unit_id) {
  void* unit = nullptr;
  if (!ScriptEvents_ResolveUnitObject(session, unit_id, &unit, false)) {
    return nullptr;
  }

  return unit;
}

int ScriptEvents_GetGuildBankTabIcon(void* lua_state) {
    (void)lua_state;
    return 0;
}

const char *ResolveScriptEventName(const std::uint32_t event_id) {
  if (event_id < kUnitFieldEventSlotCount) {
    return GetUnitFieldEventName(event_id);
  }

  switch (event_id) {
    case 0x143:
      return ui::game::events::UNIT_SPELLCAST_SENT;
    case 0x144:
      return ui::game::events::UNIT_SPELLCAST_START;
    case 0x145:
      return ui::game::events::UNIT_SPELLCAST_STOP;
    case 0x146:
      return ui::game::events::UNIT_SPELLCAST_FAILED;
    case 0x147:
      return ui::game::events::UNIT_SPELLCAST_FAILED_QUIET;
    case 0x148:
      return ui::game::events::UNIT_SPELLCAST_INTERRUPTED;
    case 0x149:
      return ui::game::events::UNIT_SPELLCAST_DELAYED;
    case 0x14a:
      return ui::game::events::UNIT_SPELLCAST_SUCCEEDED;
    case 0x14b:
      return ui::game::events::UNIT_SPELLCAST_CHANNEL_START;
    case 0x14c:
      return ui::game::events::UNIT_SPELLCAST_CHANNEL_UPDATE;
    case 0x14d:
      return ui::game::events::UNIT_SPELLCAST_CHANNEL_STOP;
    case 0x14e:
      return ui::game::events::UNIT_SPELLCAST_INTERRUPTIBLE;
    case 0x14f:
      return ui::game::events::UNIT_SPELLCAST_NOT_INTERRUPTIBLE;
    case kEventInventoryChanged:
      return "UNIT_INVENTORY_CHANGED";
    case kEventQuestLogChanged:
      return "UNIT_QUEST_LOG_CHANGED";
    default:
      return nullptr;
  }
}

void ScriptEvents_FireUnitEvent(uint64_t guid, uint32_t event_id) {
    if (const char *event_name = ResolveScriptEventName(event_id);
        event_name != nullptr) {
        ui::game::ScriptEventDispatch::Get().FirePerUnitEvent(event_name, guid);
    }
}

void ScriptEvents_FireUnitSpellcastEvent(
    uint64_t guid,
    uint32_t event_id,
    const UnitSpellcastScriptEventPayload* payload) {
    const char *event_name = ResolveScriptEventName(event_id);
    if (event_name == nullptr) {
        return;
    }

    auto &dispatch = ui::game::ScriptEventDispatch::Get();
    if (payload == nullptr) {
        dispatch.FirePerUnitEvent(event_name, guid);
        return;
    }

    dispatch.FirePerUnitEventWithArgs(
        event_name,
        guid,
        {std::string(payload->spell_name),
         std::string(payload->spell_rank),
         static_cast<int>(payload->cast_id)});
}

void ScriptEvents_QueueUnitEvent(uint64_t guid, uint32_t event_id) {
    if (const char *event_name = ResolveScriptEventName(event_id);
        event_name != nullptr) {
        ui::game::ScriptEventDispatch::Get().QueuePerUnitEvent(event_name, guid);
    }
}

void ScriptEvents_QueueGlobalEvent(uint32_t event_id) {
    const auto *descriptor =
        ui::game::ScriptEventCatalog::Instance().FindById(static_cast<std::uint16_t>(event_id));
    if (descriptor != nullptr) {
        const std::string event_name{descriptor->name};
        ui::game::ScriptEventDispatch::Get().QueueGlobalEvent(event_name.c_str());
    }
}

void ScriptEvents_QueueEventForAllUnits(uint32_t event_id) {
    if (const char *event_name = ResolveScriptEventName(event_id);
        event_name != nullptr) {
        ui::game::ScriptEventDispatch::Get().QueueTrackedUnitEvent(event_name);
    }
}

void ScriptEvents_FlushPendingUnitEvents() {
    ui::game::ScriptEventDispatch::Get().FlushQueuedEvents();
}

void ScriptEvents_ResetWorldEventFlag() {
    ui::game::ScriptEventDispatch::Get().ClearQueuedEvents();
}

int PushUnitAuraQueryResult(WorldSession& session, const std::uint64_t guid_raw,
                            const AuraFilter& aura_filter,
                            void* lua_state) {
    if (guid_raw == 0 || lua_state == nullptr) return 0;

    auto* L = static_cast<lua_State*>(lua_state);
    const ObjectGuid guid(guid_raw);
    const std::string filter = AuraFilterFlagsToFilterString(aura_filter.flags);

    auto& bridge = AuraLuaBridge::Get();

    std::optional<AuraQueryResult> result;

    if (aura_filter.index >= 0) {

        result = bridge.GetUnitAura(session, guid,
                                    static_cast<std::uint32_t>(aura_filter.index + 1),
                                    filter);
    } else if (aura_filter.name != nullptr) {

        result = bridge.FindUnitAura(
            session, guid, aura_filter.name,
            aura_filter.rank != nullptr ? aura_filter.rank : "", filter);
    } else {
        return 0;
    }

    if (!result.has_value()) return 0;

    lua_pushstring(L, result->name.c_str());
    lua_pushstring(L, result->rank.c_str());
    lua_pushstring(L, result->icon.c_str());
    lua_pushnumber(L, static_cast<lua_Number>(result->count));

    if (!result->debuffType.empty())
        lua_pushstring(L, result->debuffType.c_str());
    else
        lua_pushnil(L);

    lua_pushnumber(L, static_cast<lua_Number>(result->duration));
    lua_pushnumber(L, static_cast<lua_Number>(result->expirationTime));

    if (!result->caster.empty())
        lua_pushstring(L, result->caster.c_str());
    else
        lua_pushnil(L);

    if (result->canStealOrPurge)
        lua_pushnumber(L, 1.0);
    else
        lua_pushnil(L);

    if (result->shouldConsolidate)
        lua_pushnumber(L, 1.0);
    else
        lua_pushnil(L);

    lua_pushnumber(L, static_cast<lua_Number>(result->spellId));

    return 11;
}

}
