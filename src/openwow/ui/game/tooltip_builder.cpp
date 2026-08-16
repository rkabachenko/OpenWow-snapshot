
#include "openwow/ui/game/tooltip_builder.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/inventory/items/item_scaling.h"
#include "openwow/game/inventory/items/item_trade_eligibility.h"
#include "openwow/game/localization.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/tooltip_formatter.h"
#include "openwow/ui/game/cvar_system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::ui {
namespace {

constexpr std::string_view kWhite = "ffffffff";
constexpr std::string_view kGold = "ffffd100";
constexpr std::string_view kGreen = "ff00ff00";
constexpr std::string_view kRed = "ffff2020";
constexpr std::string_view kGray = "ff808080";

constexpr std::uint32_t kItemFlagConjured = 0x00000002u;
constexpr std::uint32_t kItemFlagHeroic = 0x00000008u;
constexpr std::uint32_t kItemFlagProspectable = 0x00040000u;
constexpr std::uint32_t kItemFlagUniqueEquippable = 0x00080000u;
constexpr std::uint32_t kItemFlagAccountBound = 0x08000000u;
constexpr std::uint32_t kItemFlagMillable = 0x20000000u;

TooltipLine MakeLine(std::string text,
                     const std::string_view color = kWhite,
                     const bool wrap = false) {
  TooltipLine line;
  line.text = std::move(text);
  line.color.assign(color);
  line.wrap = wrap;
  return line;
}

TooltipLine MakeWrappedLine(std::string text,
                            const std::string_view color = kWhite) {
  return MakeLine(std::move(text), color, true);
}

TooltipLine MakeDoubleLine(std::string left, std::string right,
                           const std::string_view left_color = kWhite,
                           const std::string_view right_color = kWhite) {
  TooltipLine line;
  line.text = std::move(left);
  line.color.assign(left_color);
  line.right_text = std::move(right);
  line.right_color.assign(right_color);
  line.is_left = true;
  return line;
}

std::string Localized(const std::string_view key,
                      const std::string_view fallback) {
  return openwow::game::Localization::Get().GetString(
      std::string(key), std::string(fallback));
}

std::string FormatLocalized(const std::string_view key,
                            const std::string_view fallback,
                            std::vector<std::string> arguments) {
  auto& localization = openwow::game::Localization::Get();
  return localization.FormatString(
      localization.GetString(std::string(key), std::string(fallback)),
      arguments);
}

std::string JoinNames(const std::vector<std::string>& names) {
  std::string joined;
  for (const auto& name : names) {
    if (name.empty()) {
      continue;
    }
    if (!joined.empty()) {
      joined += ", ";
    }
    joined += name;
  }
  return joined;
}

std::string FormatDuration(const std::uint64_t duration_seconds,
                           const std::string_view wrapper_key,
                           const std::string_view wrapper_fallback) {
  constexpr std::uint64_t kDay = 24u * 60u * 60u;
  constexpr std::uint64_t kHour = 60u * 60u;
  constexpr std::uint64_t kMinute = 60u;

  std::uint64_t remaining = duration_seconds;
  std::vector<std::string> units;
  units.reserve(2);
  const auto append_unit = [&](const std::uint64_t divisor,
                               const std::string_view key,
                               const std::string_view fallback) {
    if (units.size() == 2u || remaining < divisor) {
      return;
    }
    const auto count = remaining / divisor;
    remaining %= divisor;
    units.push_back(FormatLocalized(key, fallback,
                                    {std::to_string(count)}));
  };

  append_unit(kDay, "DAYS_ABBR", "%s Day(s)");
  append_unit(kHour, "HOURS_ABBR", "%s Hour(s)");
  append_unit(kMinute, "MINUTES_ABBR", "%s Minute(s)");
  if (units.size() < 2u && (remaining != 0u || units.empty())) {
    units.push_back(FormatLocalized("SECONDS_ABBR", "%s Second(s)",
                                    {std::to_string(remaining)}));
  }

  const auto delimiter = Localized("TIME_UNIT_DELIMITER", " ");
  std::string value;
  for (const auto& unit : units) {
    if (!value.empty()) {
      value += delimiter;
    }
    value += unit;
  }
  return FormatLocalized(wrapper_key, wrapper_fallback, {std::move(value)});
}

std::string DamageSchoolName(const std::uint32_t school) {
  static constexpr std::array<std::string_view, 7> kFallback = {
      "Physical", "Holy", "Fire", "Nature", "Frost", "Shadow", "Arcane"};
  if (school >= kFallback.size()) {
    return {};
  }
  const auto key = "SPELL_SCHOOL" + std::to_string(school) + "_CAP";
  return Localized(key, kFallback[school]);
}

std::string InventoryTypeName(const openwow::game::InventoryType type) {
  struct InventoryName {
    openwow::game::InventoryType type;
    std::string_view key;
    std::string_view fallback;
  };
  static constexpr std::array kNames = {
      InventoryName{openwow::game::InventoryType::Head, "INVTYPE_HEAD", "Head"},
      InventoryName{openwow::game::InventoryType::Neck, "INVTYPE_NECK", "Neck"},
      InventoryName{openwow::game::InventoryType::Shoulders, "INVTYPE_SHOULDER", "Shoulder"},
      InventoryName{openwow::game::InventoryType::Body, "INVTYPE_BODY", "Shirt"},
      InventoryName{openwow::game::InventoryType::Chest, "INVTYPE_CHEST", "Chest"},
      InventoryName{openwow::game::InventoryType::Waist, "INVTYPE_WAIST", "Waist"},
      InventoryName{openwow::game::InventoryType::Legs, "INVTYPE_LEGS", "Legs"},
      InventoryName{openwow::game::InventoryType::Feet, "INVTYPE_FEET", "Feet"},
      InventoryName{openwow::game::InventoryType::Wrists, "INVTYPE_WRIST", "Wrist"},
      InventoryName{openwow::game::InventoryType::Hands, "INVTYPE_HAND", "Hands"},
      InventoryName{openwow::game::InventoryType::Finger, "INVTYPE_FINGER", "Finger"},
      InventoryName{openwow::game::InventoryType::Trinket, "INVTYPE_TRINKET", "Trinket"},
      InventoryName{openwow::game::InventoryType::Weapon, "INVTYPE_WEAPON", "One-Hand"},
      InventoryName{openwow::game::InventoryType::Shield, "INVTYPE_SHIELD", "Off Hand"},
      InventoryName{openwow::game::InventoryType::Ranged, "INVTYPE_RANGED", "Ranged"},
      InventoryName{openwow::game::InventoryType::Cloak, "INVTYPE_CLOAK", "Back"},
      InventoryName{openwow::game::InventoryType::TwoHand, "INVTYPE_2HWEAPON", "Two-Hand"},
      InventoryName{openwow::game::InventoryType::Bag, "INVTYPE_BAG", "Bag"},
      InventoryName{openwow::game::InventoryType::Tabard, "INVTYPE_TABARD", "Tabard"},
      InventoryName{openwow::game::InventoryType::Robe, "INVTYPE_ROBE", "Chest"},
      InventoryName{openwow::game::InventoryType::MainHand, "INVTYPE_WEAPONMAINHAND", "Main Hand"},
      InventoryName{openwow::game::InventoryType::OffHand, "INVTYPE_WEAPONOFFHAND", "Off Hand"},
      InventoryName{openwow::game::InventoryType::Holdable, "INVTYPE_HOLDABLE", "Held In Off-Hand"},
      InventoryName{openwow::game::InventoryType::Ammo, "INVTYPE_AMMO", "Ammo"},
      InventoryName{openwow::game::InventoryType::Thrown, "INVTYPE_THROWN", "Thrown"},
      InventoryName{openwow::game::InventoryType::RangedRight, "INVTYPE_RANGEDRIGHT", "Ranged"},
      InventoryName{openwow::game::InventoryType::Quiver, "INVTYPE_QUIVER", "Quiver"},
      InventoryName{openwow::game::InventoryType::Relic, "INVTYPE_RELIC", "Relic"},
  };
  const auto it = std::find_if(kNames.begin(), kNames.end(),
                               [type](const InventoryName& value) {
                                 return value.type == type;
                               });
  return it == kNames.end() ? std::string{}
                            : Localized(it->key, it->fallback);
}

std::string ItemSubclassName(const openwow::game::ItemTemplate& item,
                             const openwow::data::dbc::DbcLoader* const dbc) {
  if (dbc != nullptr) {
    const auto composite_id =
        static_cast<std::uint32_t>(item.item_class) * 256u + item.subclass;
    if (const auto* const subclass =
            dbc->item_sub_class().LookupEntry(composite_id);
        subclass != nullptr) {
      const auto name = subclass->display_name.empty()
                            ? subclass->verbose_name
                            : subclass->display_name;
      if (!name.empty()) {
        return std::string(name);
      }
    }
  }

  if (item.item_class == openwow::game::ItemClass::Weapon) {
    static constexpr std::array<std::string_view, 21> kWeapon = {
        "Axe", "Axe", "Bow", "Gun", "Mace", "Mace", "Polearm",
        "Sword", "Sword", "Obsolete", "Staff", "Exotic", "Exotic",
        "Fist Weapon", "Miscellaneous", "Dagger", "Thrown", "Spear",
        "Crossbow", "Wand", "Fishing Pole"};
    return item.subclass < kWeapon.size() ? std::string(kWeapon[item.subclass])
                                          : std::string{};
  }
  if (item.item_class == openwow::game::ItemClass::Armor) {
    static constexpr std::array<std::string_view, 11> kArmor = {
        "Miscellaneous", "Cloth", "Leather", "Mail", "Plate", "Buckler",
        "Shield", "Libram", "Idol", "Totem", "Sigil"};
    return item.subclass < kArmor.size() ? std::string(kArmor[item.subclass])
                                         : std::string{};
  }
  if (item.item_class == openwow::game::ItemClass::Container) {
    static constexpr std::array<std::string_view, 9> kContainer = {
        "Bag", "Soul Bag", "Herb Bag", "Enchanting Bag", "Engineering Bag",
        "Gem Bag", "Mining Bag", "Leatherworking Bag", "Inscription Bag"};
    return item.subclass < kContainer.size()
               ? std::string(kContainer[item.subclass])
               : std::string{};
  }
  return {};
}

std::string StatFallbackName(const std::uint32_t stat_type) {
  switch (stat_type) {
    case 0: return "Mana";
    case 1: return "Health";
    case 3: return "Agility";
    case 4: return "Strength";
    case 5: return "Intellect";
    case 6: return "Spirit";
    case 7: return "Stamina";
    case 12: return "Defense Rating";
    case 13: return "Dodge Rating";
    case 14: return "Parry Rating";
    case 15: return "Block Rating";
    case 16: return "Hit Melee Rating";
    case 17: return "Hit Ranged Rating";
    case 18: return "Hit Spell Rating";
    case 19: return "Crit Melee Rating";
    case 20: return "Crit Ranged Rating";
    case 21: return "Crit Spell Rating";
    case 22: return "Hit Taken Melee Rating";
    case 23: return "Hit Taken Ranged Rating";
    case 24: return "Hit Taken Spell Rating";
    case 25: return "Crit Taken Melee Rating";
    case 26: return "Crit Taken Ranged Rating";
    case 27: return "Crit Taken Spell Rating";
    case 28: return "Haste Melee Rating";
    case 29: return "Haste Ranged Rating";
    case 30: return "Haste Spell Rating";
    case 31: return "Hit Rating";
    case 32: return "Critical Strike Rating";
    case 35: return "Resilience Rating";
    case 36: return "Haste Rating";
    case 37: return "Expertise Rating";
    case 38: return "Attack Power";
    case 39: return "Ranged Attack Power";
    case 40: return "Feral Attack Power";
    case 41: return "Spell Healing";
    case 42: return "Spell Damage";
    case 43: return "Mana Regeneration";
    case 44: return "Armor Penetration Rating";
    case 45: return "Spell Power";
    case 46: return "Health Regeneration";
    case 47: return "Spell Penetration";
    case 48: return "Block Value";
    default: return {};
  }
}

std::string FormatStat(const std::uint32_t stat_type, const std::int32_t value) {
  std::array<char, 128> key{};
  const char* const resolved_key = openwow::ui::game::GetStatModifierGlobalStringName(
      stat_type + 11u, key.data(), key.size());
  if (resolved_key != nullptr && resolved_key[0] != '\0') {
    const auto format = openwow::game::Localization::Get().GetString(
        resolved_key, "");
    if (!format.empty()) {
      return openwow::game::Localization::Get().FormatString(
          format, {std::to_string(value)});
    }
  }

  const auto name = StatFallbackName(stat_type);
  if (name.empty()) {
    return {};
  }
  return (value >= 0 ? "+" : "") + std::to_string(value) + " " + name;
}

struct ScaledTooltipStatLine {
  std::uint32_t stat_type = 0;
  std::int32_t value = 0;
};

struct ScaledTooltipData {
  bool has_scaled_damage = false;
  openwow::game::item_scaling::ScalingWeaponDpsInfo weapon_dps{};
  bool has_scaled_armor = false;
  std::uint32_t armor = 0;
  std::vector<ScaledTooltipStatLine> stats;
  std::int32_t spell_power = 0;
};

std::optional<ScaledTooltipData> BuildScaledTooltipData(
    const openwow::game::ItemTemplate& item,
    const openwow::data::dbc::DbcLoader* const dbc,
    const std::uint32_t scaling_level) {
  const auto* const distribution =
      openwow::game::item_scaling::FindScalingStatDistribution(item, dbc);
  if (distribution == nullptr) {
    return std::nullopt;
  }
  const auto resolved_level = openwow::game::item_scaling::ResolveScalingItemLevel(
      item, *distribution, scaling_level);
  const auto* const values =
      openwow::game::item_scaling::FindScalingStatValuesByLevel(dbc, resolved_level);
  if (values == nullptr) {
    return std::nullopt;
  }

  ScaledTooltipData result;
  const auto mask = item.scaling_stat_value;
  const auto stat_budget =
      openwow::game::item_scaling::SelectScalingStatBudget(*values, mask);
  if (stat_budget != 0u) {
    for (std::size_t index = 0; index < distribution->stat_id.size(); ++index) {
      if (distribution->stat_id[index] < 0) {
        continue;
      }
      const auto value = openwow::game::item_scaling::ComputeScalingDistributionValue(
          stat_budget, distribution->bonus[index]);
      if (value != 0) {
        result.stats.push_back(
            {static_cast<std::uint32_t>(distribution->stat_id[index]), value});
      }
    }
  }
  if ((mask & openwow::game::item_scaling::kScalingArmorMask) != 0u) {
    result.armor = openwow::game::item_scaling::SelectScalingArmorValue(*values, mask);
    result.has_scaled_armor = result.armor != 0u;
  }
  if (const auto weapon =
          openwow::game::item_scaling::SelectScalingWeaponDpsInfo(*values, mask);
      weapon.has_value()) {
    result.has_scaled_damage = true;
    result.weapon_dps = *weapon;
  }
  if ((mask & openwow::game::item_scaling::kScalingSpellPowerFlag) != 0u) {
    result.spell_power = static_cast<std::int32_t>(values->GetField(16));
  }
  return result;
}

void AddDamageLine(std::vector<TooltipLine>& lines,
                   const float minimum_damage,
                   const float maximum_damage,
                   const std::uint32_t school,
                   const std::uint32_t delay,
                   const float displayed_dps) {
  if (minimum_damage <= 0.0f && maximum_damage <= 0.0f) {
    return;
  }
  const auto minimum = std::to_string(static_cast<std::uint32_t>(minimum_damage));
  const auto maximum = std::to_string(static_cast<std::uint32_t>(maximum_damage));
  std::string damage;
  if (school == 0u) {
    damage = minimum_damage == maximum_damage
                 ? FormatLocalized("SINGLE_DAMAGE_TEMPLATE", "%s Damage", {minimum})
                 : FormatLocalized("DAMAGE_TEMPLATE", "%s - %s Damage",
                                   {minimum, maximum});
  } else {
    const auto school_name = DamageSchoolName(school);
    damage = minimum_damage == maximum_damage
                 ? FormatLocalized("SINGLE_DAMAGE_TEMPLATE_WITH_SCHOOL",
                                   "%s %s Damage", {minimum, school_name})
                 : FormatLocalized("DAMAGE_TEMPLATE_WITH_SCHOOL",
                                   "%s - %s %s Damage",
                                   {minimum, maximum, school_name});
  }

  std::string speed;
  if (delay != 0u) {
    std::array<char, 32> value{};
    std::snprintf(value.data(), value.size(), "%.2f",
                  static_cast<float>(delay) / 1000.0f);
    speed = FormatLocalized("SPEED", "Speed %s", {value.data()});
  }
  lines.push_back(MakeDoubleLine(std::move(damage), std::move(speed)));

  if (displayed_dps > 0.0f) {
    std::array<char, 32> value{};
    std::snprintf(value.data(), value.size(), "%.1f", displayed_dps);
    lines.push_back(MakeLine(
        FormatLocalized("DPS_TEMPLATE", "(%s damage per second)",
                        {value.data()})));
  }
}

void AddBaseDamage(std::vector<TooltipLine>& lines,
                   const openwow::game::ItemTemplate& item) {
  if (!item.IsWeapon()) {
    return;
  }
  const auto& damage = item.damage[0];
  const auto average = (damage.min_damage + damage.max_damage) * 0.5f;
  const auto dps = item.delay == 0u
                       ? 0.0f
                       : average / (static_cast<float>(item.delay) / 1000.0f);
  AddDamageLine(lines, damage.min_damage, damage.max_damage, damage.type,
                item.delay, dps);
  const auto& secondary = item.damage[1];
  if (secondary.min_damage > 0.0f || secondary.max_damage > 0.0f) {
    AddDamageLine(lines, secondary.min_damage, secondary.max_damage,
                  secondary.type, 0u, 0.0f);
  }
}

void AddScaledDamage(std::vector<TooltipLine>& lines,
                     const openwow::game::ItemTemplate& item,
                     const openwow::game::item_scaling::ScalingWeaponDpsInfo& data) {
  if (!item.IsWeapon() || item.delay == 0u) {
    return;
  }
  const auto speed = static_cast<float>(item.delay) / 1000.0f;
  AddDamageLine(lines, data.min_dps * speed, data.max_dps * speed,
                item.damage[0].type, item.delay, data.base_dps);
}

void AddArmorAndBlock(std::vector<TooltipLine>& lines,
                      const std::uint32_t armor,
                      const std::uint32_t block) {
  if (armor != 0u) {
    lines.push_back(MakeLine(FormatLocalized("ARMOR_TEMPLATE", "%s Armor",
                                             {std::to_string(armor)})));
  }
  if (block != 0u) {
    lines.push_back(MakeLine(FormatLocalized("SHIELD_BLOCK_TEMPLATE", "%s Block",
                                             {std::to_string(block)})));
  }
}

void AddBaseStats(std::vector<TooltipLine>& lines,
                  const openwow::game::ItemTemplate& item) {
  for (const auto& stat : item.stats) {
    if (stat.value == 0) {
      continue;
    }
    if (auto text = FormatStat(stat.type, stat.value); !text.empty()) {
      lines.push_back(MakeLine(std::move(text)));
    }
  }

  struct Resistance {
    std::int32_t value;
    std::uint32_t school;
  };
  const std::array resistances = {
      Resistance{item.holy_res, 1u}, Resistance{item.fire_res, 2u},
      Resistance{item.nature_res, 3u}, Resistance{item.frost_res, 4u},
      Resistance{item.shadow_res, 5u}, Resistance{item.arcane_res, 6u},
  };
  for (const auto& resistance : resistances) {
    if (resistance.value <= 0) {
      continue;
    }
    lines.push_back(MakeLine(FormatLocalized(
        "ITEM_RESIST_SINGLE", "+%s %s Resistance",
        {std::to_string(resistance.value), DamageSchoolName(resistance.school)})));
  }
}

void AddScaledStats(std::vector<TooltipLine>& lines,
                    const ScaledTooltipData& data) {
  const auto equip_prefix =
      Localized("ITEM_SPELL_TRIGGER_ONEQUIP", "Equip: ");
  for (const auto& stat : data.stats) {
    if (auto text = FormatStat(stat.stat_type, stat.value); !text.empty()) {
      lines.push_back(MakeLine(equip_prefix + text, kGreen));
    }
  }
  if (data.spell_power != 0) {
    if (auto text = FormatStat(45u, data.spell_power); !text.empty()) {
      lines.push_back(MakeLine(equip_prefix + text, kGreen));
    }
  }
}

void AddItemIdentityMetadata(std::vector<TooltipLine>& lines,
                             const openwow::game::ItemTemplate& item,
                             const TooltipItemInstanceData* const instance) {
  lines.push_back(MakeLine(
      item.name, openwow::game::ItemTemplate::GetQualityColorCode(item.quality)));

  if ((item.flags & kItemFlagHeroic) != 0u) {
    const auto key = item.quality == openwow::game::ItemQuality::Epic
                         ? "ITEM_HEROIC_EPIC"
                         : "ITEM_HEROIC";
    lines.push_back(MakeLine(Localized(key, "Heroic"), kGreen));
  }
  if ((item.flags & kItemFlagConjured) != 0u ||
      (instance != nullptr && instance->live_item &&
       (instance->runtime_flags & openwow::game::ItemFlags::kConjured) != 0u)) {
    lines.push_back(MakeLine(Localized("ITEM_CONJURED", "Conjured Item")));
  }

  const bool soulbound = instance != nullptr && instance->live_item &&
                         (instance->runtime_flags &
                          openwow::game::ItemFlags::kSoulbound) != 0u;
  if (soulbound) {
    if (item.bonding == 4u) {
      lines.push_back(MakeLine(Localized("ITEM_BIND_QUEST", "Quest Item")));
    } else if ((item.flags & kItemFlagAccountBound) != 0u) {
      lines.push_back(MakeLine(Localized("ITEM_ACCOUNTBOUND", "Account Bound")));
    } else if (item.item_class != openwow::game::ItemClass::Money) {
      lines.push_back(MakeLine(Localized("ITEM_SOULBOUND", "Soulbound")));
    }
  } else if ((item.flags & kItemFlagAccountBound) != 0u) {
    lines.push_back(
        MakeLine(Localized("ITEM_BIND_TO_ACCOUNT", "Binds to account")));
  } else {
    switch (item.bonding) {
      case 1u:
        lines.push_back(MakeLine(
            Localized("ITEM_BIND_ON_PICKUP", "Binds when picked up")));
        break;
      case 2u:
        lines.push_back(MakeLine(
            Localized("ITEM_BIND_ON_EQUIP", "Binds when equipped")));
        break;
      case 3u:
        lines.push_back(
            MakeLine(Localized("ITEM_BIND_ON_USE", "Binds when used")));
        break;
      case 4u:
        lines.push_back(MakeLine(Localized("ITEM_BIND_QUEST", "Quest Item")));
        break;
      default:
        break;
    }
  }

  if ((item.flags & kItemFlagUniqueEquippable) != 0u) {
    lines.push_back(
        MakeLine(Localized("ITEM_UNIQUE_EQUIPPABLE", "Unique-Equipped")));
  } else if (item.max_count == 1u) {
    lines.push_back(MakeLine(Localized("ITEM_UNIQUE", "Unique")));
  } else if (item.max_count > 1u) {
    lines.push_back(MakeLine(FormatLocalized(
        "ITEM_UNIQUE_MULTIPLE", "Unique (%s)",
        {std::to_string(item.max_count)})));
  }
}

void AddLimitCategory(std::vector<TooltipLine>& lines,
                      const openwow::game::ItemTemplate& item,
                      const openwow::data::dbc::DbcLoader* const dbc) {
  if (dbc == nullptr || item.item_limit_category == 0u) {
    return;
  }
  const auto* const category =
      dbc->item_limit_category().LookupEntry(item.item_limit_category);
  if (category == nullptr || category->name.empty()) {
    return;
  }
  const auto key = category->flags == 0u ? "ITEM_LIMIT_CATEGORY"
                                         : "ITEM_LIMIT_CATEGORY_MULTIPLE";
  const auto fallback = category->flags == 0u ? "Unique-Equipped: %s (%s)"
                                               : "Unique: %s (%s)";
  lines.push_back(MakeLine(FormatLocalized(
      key, fallback,
      {std::string(category->name), std::to_string(category->quantity)})));
}

void AddSlotAndContainer(std::vector<TooltipLine>& lines,
                         const openwow::game::ItemTemplate& item,
                         const openwow::data::dbc::DbcLoader* const dbc) {
  if (item.item_class == openwow::game::ItemClass::Container &&
      item.container_slots != 0u) {
    lines.push_back(MakeLine(FormatLocalized(
        "CONTAINER_SLOTS", "%s Slot Bag",
        {std::to_string(item.container_slots)})));
  }
  if (item.inventory_type == openwow::game::InventoryType::NonEquip &&
      item.item_class != openwow::game::ItemClass::Container) {
    return;
  }
  const auto slot = InventoryTypeName(item.inventory_type);
  const auto subclass = ItemSubclassName(item, dbc);
  if (!slot.empty() || !subclass.empty()) {
    lines.push_back(MakeDoubleLine(slot, subclass));
  }
}

struct GemPresentation {
  std::string description;
  std::uint32_t color = 0;
};

std::optional<GemPresentation> ResolveGemPresentation(
    const openwow::game::ItemDefinitions& item_definitions,
    const std::uint32_t item_id,
    const openwow::data::dbc::DbcLoader* const dbc) {
  if (item_id == 0u || dbc == nullptr) {
    return std::nullopt;
  }
  const auto gem_item = item_definitions.GetItemSnapshot(item_id);
  if (!gem_item.has_value() || gem_item->gem_properties == 0u) {
    return std::nullopt;
  }
  const auto* const properties =
      dbc->gem_properties().LookupEntry(gem_item->gem_properties);
  if (properties == nullptr) {
    return std::nullopt;
  }
  GemPresentation presentation;
  presentation.color = properties->type;
  if (const auto* const enchant =
          dbc->spell_item_enchantment().LookupEntry(properties->enchant_id);
      enchant != nullptr) {
    presentation.description.assign(enchant->description);
  }
  if (presentation.description.empty()) {
    presentation.description = gem_item->name;
  }
  return presentation;
}

std::pair<std::string_view, std::string_view> EmptySocketText(
    const std::uint32_t color) {
  switch (color) {
    case 1u: return {"EMPTY_SOCKET_META", "Meta Socket"};
    case 2u: return {"EMPTY_SOCKET_RED", "Red Socket"};
    case 4u: return {"EMPTY_SOCKET_YELLOW", "Yellow Socket"};
    case 8u: return {"EMPTY_SOCKET_BLUE", "Blue Socket"};
    default: return {"EMPTY_SOCKET_PRISMATIC", "Prismatic Socket"};
  }
}

void AddSockets(std::vector<TooltipLine>& lines,
                const openwow::game::ItemTemplate& item,
                const openwow::game::ItemDefinitions& item_definitions,
                const openwow::data::dbc::DbcLoader* const dbc,
                const TooltipItemInstanceData* const instance) {
  bool all_sockets_match = true;
  bool has_socket = false;
  for (std::size_t index = 0; index < item.sockets.size(); ++index) {
    const auto socket_color = item.sockets[index].color;
    if (socket_color == 0u) {
      continue;
    }
    has_socket = true;
    const auto gem_item_id = instance != nullptr
                                 ? instance->gem_item_ids[index]
                                 : 0u;
    const auto gem = ResolveGemPresentation(item_definitions, gem_item_id, dbc);
    if (!gem.has_value() || gem->description.empty()) {
      const auto [key, fallback] = EmptySocketText(socket_color);
      lines.push_back(MakeLine(Localized(key, fallback), kGray));
      all_sockets_match = false;
      continue;
    }
    lines.push_back(MakeWrappedLine(gem->description, kGreen));
    const bool matches = socket_color == 1u
                             ? (gem->color & 1u) != 0u
                             : (gem->color & socket_color) != 0u;
    all_sockets_match = all_sockets_match && matches;
  }

  if (has_socket && item.socket_bonus != 0u && dbc != nullptr) {
    if (const auto* const bonus =
            dbc->spell_item_enchantment().LookupEntry(item.socket_bonus);
        bonus != nullptr && !bonus->description.empty()) {
      lines.push_back(MakeWrappedLine(
          FormatLocalized("ITEM_SOCKET_BONUS", "Socket Bonus: %s",
                          {std::string(bonus->description)}),
          all_sockets_match ? kGreen : kGray));
    }
  }
}

void AddPermanentEnchant(std::vector<TooltipLine>& lines,
                         const openwow::data::dbc::DbcLoader* const dbc,
                         const TooltipItemInstanceData* const instance) {
  if (dbc == nullptr || instance == nullptr ||
      instance->permanent_enchant_id == 0u) {
    return;
  }
  if (const auto* const enchant = dbc->spell_item_enchantment().LookupEntry(
          instance->permanent_enchant_id);
      enchant != nullptr && !enchant->description.empty()) {
    lines.push_back(
        MakeWrappedLine(std::string(enchant->description), kGreen));
  }
}

std::string SpellTriggerPrefix(const std::uint32_t trigger) {
  switch (trigger) {
    case 0u:
      return Localized("ITEM_SPELL_TRIGGER_ONUSE", "Use: ");
    case 1u:
      return Localized("ITEM_SPELL_TRIGGER_ONEQUIP", "Equip: ");
    case 2u:
      return Localized("ITEM_SPELL_TRIGGER_ONPROC", "Chance on hit: ");
    case 6u:
      return Localized("ITEM_SPELL_TRIGGER_LEARN", "Use: Teaches you ");
    default:
      return {};
  }
}

std::string ResolveSpellTooltipText(
    const std::uint32_t spell_id,
    const openwow::data::dbc::DbcLoader* const dbc) {
  if (spell_id == 0u) {
    return {};
  }
  if (dbc != nullptr) {
    openwow::game::BindSpellTextFormatterDbcLoader(dbc);
    if (auto expanded = openwow::game::ExpandSpellDescription(spell_id);
        !expanded.empty()) {
      return expanded;
    }
    if (const auto* const spell = dbc->spell().LookupEntry(spell_id);
        spell != nullptr) {
      if (!spell->description.empty()) {
        return std::string(spell->description);
      }
      if (!spell->tooltip.empty()) {
        return std::string(spell->tooltip);
      }
      return std::string(spell->spell_name);
    }
  }
  if (const auto query = openwow::game::SpellQueryBridge::Get().Query(spell_id);
      query.has_value()) {
    return !query->description.empty() ? query->description : query->name;
  }
  return {};
}

void AddItemSpells(std::vector<TooltipLine>& lines,
                   const openwow::game::ItemTemplate& item,
                   const openwow::data::dbc::DbcLoader* const dbc,
                   const TooltipItemInstanceData* const instance) {
  for (std::size_t index = 0; index < item.spells.size(); ++index) {
    const auto& item_spell = item.spells[index];
    if (item_spell.spell_id == 0u) {
      continue;
    }
    auto spell_text = ResolveSpellTooltipText(item_spell.spell_id, dbc);
    if (spell_text.empty()) {
      continue;
    }
    spell_text = SpellTriggerPrefix(item_spell.trigger) + spell_text;

    if (item_spell.charges != 0) {
      const auto charges = instance != nullptr && instance->live_item
                               ? instance->spell_charges[index]
                               : item_spell.charges;
      if (charges == 0) {
        spell_text += " " + Localized("ITEM_SPELL_CHARGES_NONE", "No Charges");
      } else {
        const auto magnitude = charges < 0
                                   ? -static_cast<std::int64_t>(charges)
                                   : static_cast<std::int64_t>(charges);
        spell_text += " " + FormatLocalized(
                                "ITEM_SPELL_CHARGES", "%s Charges",
                                {std::to_string(magnitude)});
      }
    }
    lines.push_back(MakeWrappedLine(
        std::move(spell_text), item_spell.trigger == 6u ? kWhite : kGreen));

    if (item_spell.trigger == 0u) {
      const auto cooldown = std::max(item_spell.cooldown,
                                     item_spell.category_cooldown);
      if (cooldown > 0) {
        lines.push_back(MakeLine(
            FormatDuration(static_cast<std::uint64_t>(cooldown) / 1000u,
                           "ITEM_COOLDOWN_TOTAL", "Cooldown: %s"),
            kGreen));
      }
    }
  }
}

std::vector<std::string> AllowedClassNames(
    const std::int32_t mask,
    const openwow::data::dbc::DbcLoader* const dbc) {
  std::vector<std::pair<std::uint32_t, std::string>> sorted;
  if (dbc != nullptr) {
    for (const auto& entry : dbc->chr_classes()) {
      if (entry.id != 0u && entry.id <= 32u &&
          (static_cast<std::uint32_t>(mask) & (1u << (entry.id - 1u))) != 0u) {
        sorted.emplace_back(entry.id, entry.name);
      }
    }
  }
  if (sorted.empty()) {
    static constexpr std::array<std::string_view, 11> kFallback = {
        "Warrior", "Paladin", "Hunter", "Rogue", "Priest", "Death Knight",
        "Shaman", "Mage", "Warlock", "", "Druid"};
    for (std::uint32_t index = 0; index < kFallback.size(); ++index) {
      if (!kFallback[index].empty() &&
          (static_cast<std::uint32_t>(mask) & (1u << index)) != 0u) {
        sorted.emplace_back(index + 1u, kFallback[index]);
      }
    }
  }
  std::sort(sorted.begin(), sorted.end());
  std::vector<std::string> names;
  names.reserve(sorted.size());
  for (auto& [id, name] : sorted) {
    (void)id;
    names.push_back(std::move(name));
  }
  return names;
}

std::vector<std::string> AllowedRaceNames(
    const std::int32_t mask,
    const openwow::data::dbc::DbcLoader* const dbc) {
  std::vector<std::pair<std::uint32_t, std::string>> sorted;
  if (dbc != nullptr) {
    for (const auto& entry : dbc->chr_races()) {
      if (entry.id != 0u && entry.id <= 32u &&
          (static_cast<std::uint32_t>(mask) & (1u << (entry.id - 1u))) != 0u) {
        sorted.emplace_back(entry.id, entry.name);
      }
    }
  }
  if (sorted.empty()) {
    static constexpr std::array<std::string_view, 12> kFallback = {
        "Human", "Orc", "Dwarf", "Night Elf", "Undead", "Tauren",
        "Gnome", "Troll", "", "Blood Elf", "Draenei", ""};
    for (std::uint32_t index = 0; index < kFallback.size(); ++index) {
      if (!kFallback[index].empty() &&
          (static_cast<std::uint32_t>(mask) & (1u << index)) != 0u) {
        sorted.emplace_back(index + 1u, kFallback[index]);
      }
    }
  }
  std::sort(sorted.begin(), sorted.end());
  std::vector<std::string> names;
  names.reserve(sorted.size());
  for (auto& [id, name] : sorted) {
    (void)id;
    names.push_back(std::move(name));
  }
  return names;
}

void AddRequirements(std::vector<TooltipLine>& lines,
                     const openwow::game::ItemTemplate& item,
                     const std::uint32_t player_level,
                     const std::uint32_t player_class_mask,
                     const std::uint32_t player_race_mask,
                     const openwow::data::dbc::DbcLoader* const dbc,
                     const openwow::game::WorldSession* const session) {
  const auto* const player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;

  if (item.required_level > 1u) {
    lines.push_back(MakeLine(
        FormatLocalized("ITEM_MIN_LEVEL", "Requires Level %s",
                        {std::to_string(item.required_level)}),
        player_level >= item.required_level ? kWhite : kRed));
  }

  if (item.allowable_class > 0 && item.allowable_class != -1) {
    const auto class_names = JoinNames(AllowedClassNames(item.allowable_class, dbc));
    if (!class_names.empty()) {
      lines.push_back(MakeWrappedLine(
          FormatLocalized("ITEM_CLASSES_ALLOWED", "Classes: %s", {class_names}),
          (static_cast<std::uint32_t>(item.allowable_class) & player_class_mask) != 0u
              ? kWhite
              : kRed));
    }
  }
  if (item.allowable_race > 0 && item.allowable_race != -1) {
    const auto race_names = JoinNames(AllowedRaceNames(item.allowable_race, dbc));
    if (!race_names.empty()) {
      lines.push_back(MakeWrappedLine(
          FormatLocalized("ITEM_RACES_ALLOWED", "Races: %s", {race_names}),
          (static_cast<std::uint32_t>(item.allowable_race) & player_race_mask) != 0u
              ? kWhite
              : kRed));
    }
  }

  if (item.required_skill != 0u && dbc != nullptr) {
    if (const auto* const skill = dbc->skill_line().LookupEntry(item.required_skill);
        skill != nullptr && !skill->name.empty()) {
      std::uint32_t current_skill = item.required_skill_rank;
      bool have_player_skill = false;
      if (player != nullptr && item.required_skill <= 0xffffu) {
        current_skill = player->GetSkillValue(
                            static_cast<std::uint16_t>(item.required_skill)) +
                        player->GetSkillBonusValue(
                            static_cast<std::uint16_t>(item.required_skill));
        have_player_skill = true;
      }
      const auto text = item.required_skill_rank != 0u
                            ? FormatLocalized(
                                  "ITEM_MIN_SKILL", "Requires %s (%s)",
                                  {std::string(skill->name),
                                   std::to_string(item.required_skill_rank)})
                            : FormatLocalized("ITEM_REQ_SKILL", "Requires %s",
                                              {std::string(skill->name)});
      lines.push_back(MakeLine(
          text, !have_player_skill || current_skill >= item.required_skill_rank
                    ? kWhite
                    : kRed));
    }
  }

  if (item.required_spell != 0u && dbc != nullptr) {
    if (const auto* const spell = dbc->spell().LookupEntry(item.required_spell);
        spell != nullptr && !spell->spell_name.empty()) {
      const bool known = session == nullptr ||
                         session->spell_book().HasSpell(item.required_spell);
      lines.push_back(MakeLine(
          FormatLocalized("ITEM_SPELL_KNOWN", "Requires %s",
                          {std::string(spell->spell_name)}),
          known ? kWhite : kRed));
    }
  }

  if (item.required_reputation_faction != 0u && dbc != nullptr) {
    if (const auto* const faction =
            dbc->faction().LookupEntry(item.required_reputation_faction);
        faction != nullptr && !faction->name.empty()) {
      const auto standing_key = "FACTION_STANDING_LABEL" +
                                std::to_string(item.required_reputation_rank + 1u);
      const auto standing = Localized(standing_key, standing_key);
      const auto met = session == nullptr ||
                       openwow::game::ReputationInfo::Get().GetStandingLevel(
                           static_cast<std::int32_t>(
                               item.required_reputation_faction)) >=
                           static_cast<int>(item.required_reputation_rank);
      lines.push_back(MakeLine(
          FormatLocalized("ITEM_REQ_REPUTATION", "Requires %s - %s",
                          {std::string(faction->name), standing}),
          met ? kWhite : kRed));
    }
  }
}

void AddItemSet(std::vector<TooltipLine>& lines,
                const openwow::game::ItemTemplate& item,
                const openwow::game::ItemDefinitions& item_definitions,
                const openwow::game::PlayerInventoryReplica* inventory,
                const openwow::data::dbc::DbcLoader* const dbc) {
  if (dbc == nullptr || item.item_set == 0u) {
    return;
  }
  const auto* const item_set = dbc->item_set().LookupEntry(item.item_set);
  if (item_set == nullptr || item_set->name.empty()) {
    return;
  }

  std::vector<std::uint32_t> equipped_entries;
  if (inventory != nullptr) {
    equipped_entries.reserve(openwow::game::InventorySlots::kEquipEnd);
    for (std::uint8_t slot = openwow::game::InventorySlots::kEquipStart;
         slot < openwow::game::InventorySlots::kEquipEnd; ++slot) {
      if (const auto* const equipped = inventory->GetItemInSlot(slot);
          equipped != nullptr && equipped->entry != 0u) {
        equipped_entries.push_back(equipped->entry);
      }
    }
  }

  std::size_t set_size = 0;
  std::size_t equipped_count = 0;
  for (const auto item_id : item_set->item_id) {
    if (item_id == 0u) {
      continue;
    }
    ++set_size;
    const bool equipped =
        std::find(equipped_entries.begin(), equipped_entries.end(), item_id) !=
        equipped_entries.end();
    equipped_count += equipped ? 1u : 0u;
  }

  lines.push_back(MakeLine(FormatLocalized(
      "ITEM_SET_NAME", "%s (%s/%s)",
      {std::string(item_set->name), std::to_string(equipped_count),
       std::to_string(set_size)}),
      kGold));
  for (const auto item_id : item_set->item_id) {
    if (item_id == 0u) {
      continue;
    }
    const auto member = item_definitions.GetItemSnapshot(item_id);
    if (!member.has_value() || member->name.empty()) {
      continue;
    }
    const bool equipped =
        std::find(equipped_entries.begin(), equipped_entries.end(), item_id) !=
        equipped_entries.end();
    lines.push_back(MakeLine(member->name, equipped ? kGold : kGray));
  }

  std::array<std::size_t, 8> bonus_order{};
  for (std::size_t index = 0; index < bonus_order.size(); ++index) {
    bonus_order[index] = index;
  }
  std::stable_sort(bonus_order.begin(), bonus_order.end(),
                   [item_set](const std::size_t left, const std::size_t right) {
                     if (item_set->bonus_threshold[left] !=
                         item_set->bonus_threshold[right]) {
                       return item_set->bonus_threshold[left] <
                              item_set->bonus_threshold[right];
                     }
                     return left < right;
                   });
  for (const auto index : bonus_order) {
    const auto spell_id = item_set->bonus_spell[index];
    const auto threshold = item_set->bonus_threshold[index];
    if (spell_id == 0u || threshold == 0u) {
      continue;
    }
    const auto description = ResolveSpellTooltipText(spell_id, dbc);
    if (description.empty()) {
      continue;
    }
    lines.push_back(MakeWrappedLine(
        FormatLocalized("ITEM_SET_BONUS", "(%s) Set: %s",
                        {std::to_string(threshold), description}),
        equipped_count >= threshold ? kGreen : kGray));
  }
}

void AddDurability(std::vector<TooltipLine>& lines,
                   const openwow::game::ItemTemplate& item,
                   const TooltipItemInstanceData* const instance) {
  auto maximum = item.max_durability;
  auto current = maximum;
  if (instance != nullptr && instance->live_item &&
      instance->max_durability != 0u) {
    maximum = instance->max_durability;
    current = instance->durability;
  }
  if (maximum == 0u) {
    return;
  }
  lines.push_back(MakeLine(
      FormatLocalized("DURABILITY_TEMPLATE", "Durability %s / %s",
                      {std::to_string(current), std::to_string(maximum)}),
      current == 0u ? kRed : kWhite));
}

void AddLifecycleLines(std::vector<TooltipLine>& lines,
                       const openwow::game::ItemTemplate& item,
                       const TooltipItemInstanceData* const instance,
                       const openwow::game::WorldSession* const session) {
  if (instance != nullptr && instance->live_item && instance->locked) {
    lines.push_back(MakeLine(Localized("LOCKED", "Locked"), kRed));
  }
  if (item.start_quest != 0u) {
    lines.push_back(MakeLine(Localized("ITEM_STARTS_QUEST", "This Item Begins a Quest"),
                             kGold));
  }
  if ((item.flags & kItemFlagProspectable) != 0u) {
    lines.push_back(MakeLine(Localized("ITEM_PROSPECTABLE", "Prospectable"), kGreen));
  }
  if ((item.flags & kItemFlagMillable) != 0u) {
    lines.push_back(MakeLine(Localized("ITEM_MILLABLE", "Millable"), kGreen));
  }

  const auto duration = instance != nullptr && instance->live_item
                            ? instance->remaining_duration_seconds
                            : item.duration;
  if (duration != 0u) {
    lines.push_back(MakeLine(
        FormatDuration(duration, "ITEM_DURATION", "Duration: %s"), kGold));
  }

  if (instance == nullptr || !instance->live_item) {
    return;
  }
  if (session != nullptr &&
      (instance->runtime_flags & openwow::game::ItemFlags::kTradeWindow) != 0u) {
    const auto remaining = openwow::game::ResolveBoundTradeWindowRemainingSeconds(
        instance->create_played_time,
        session->misc().current_total_played_time());
    if (remaining.has_value()) {
      lines.push_back(MakeWrappedLine(FormatDuration(
          *remaining, "BIND_TRADE_TIME_REMAINING",
          "You may trade this item with eligible players for the next %s."),
          kGreen));
    }
  }
  if (instance->item_guid != 0u) {
    const auto* refund =
        session != nullptr
            ? session->item_interactions().refund_quote(
                  openwow::game::ObjectGuid(instance->item_guid))
            : nullptr;
    const auto refund_seconds = refund != nullptr ? refund->time_left : 0u;
    if (refund_seconds != 0u) {
      lines.push_back(MakeLine(
          FormatDuration(refund_seconds, "REFUND_TIME_REMAINING",
                         "Refundable for %s"),
          kGreen));
    }
  }
}

bool ShouldShowItemLevel(const openwow::game::ItemTemplate& item) {
  if (!openwow::ui::game::CVarSystem::Instance().GetCVarBool("showItemLevel")) {
    return false;
  }
  switch (item.item_class) {
    case openwow::game::ItemClass::Weapon:
    case openwow::game::ItemClass::Armor:
    case openwow::game::ItemClass::Reagent:
    case openwow::game::ItemClass::Projectile:
      return true;
    default:
      return false;
  }
}

}

std::vector<TooltipLine> TooltipBuilder::BuildItemTooltip(
    const openwow::game::ItemTemplate& item,
    const openwow::game::ItemDefinitions& item_definitions,
    const openwow::game::PlayerInventoryReplica* inventory,
    const std::uint32_t player_level,
    const std::uint32_t player_class_mask,
    const std::uint32_t player_race_mask,
    const openwow::data::dbc::DbcLoader* const dbc,
    const std::uint32_t scaling_level,
    const TooltipItemInstanceData* const instance_data,
    const openwow::game::WorldSession* const session) {
  std::vector<TooltipLine> lines;
  lines.reserve(40);

  AddItemIdentityMetadata(lines, item, instance_data);
  AddLimitCategory(lines, item, dbc);
  if (ShouldShowItemLevel(item)) {
    lines.push_back(MakeLine(FormatLocalized(
        "ITEM_LEVEL", "Item Level %s", {std::to_string(item.item_level)})));
  }
  AddSlotAndContainer(lines, item, dbc);

  const auto scaled = item.scaling_stat_distribution != 0u
                          ? BuildScaledTooltipData(item, dbc, scaling_level)
                          : std::nullopt;
  if (scaled.has_value() && scaled->has_scaled_damage) {
    AddScaledDamage(lines, item, scaled->weapon_dps);
  } else {
    AddBaseDamage(lines, item);
  }
  AddArmorAndBlock(lines,
                   scaled.has_value() && scaled->has_scaled_armor
                       ? scaled->armor
                       : static_cast<std::uint32_t>(std::max(item.armor, 0)),
                   item.block);
  if (scaled.has_value() && (!scaled->stats.empty() || scaled->spell_power != 0)) {
    AddScaledStats(lines, *scaled);
  } else {
    AddBaseStats(lines, item);
  }

  AddPermanentEnchant(lines, dbc, instance_data);
  AddSockets(lines, item, item_definitions, dbc, instance_data);
  AddItemSpells(lines, item, dbc, instance_data);
  AddItemSet(lines, item, item_definitions, inventory, dbc);
  AddRequirements(lines, item, player_level, player_class_mask,
                  player_race_mask, dbc, session);
  AddDurability(lines, item, instance_data);
  AddLifecycleLines(lines, item, instance_data, session);

  if (!item.description.empty()) {
    lines.push_back(
        MakeWrappedLine("\"" + item.description + "\"", kGold));
  }
  return lines;
}

std::vector<TooltipLine> TooltipBuilder::BuildSpellTooltip(
    const std::uint32_t , const std::string& name,
    const std::string& rank, const std::string& description,
    const std::uint32_t mana_cost, const float range,
    const float cast_time, const float cooldown) {
  std::vector<TooltipLine> lines;
  lines.reserve(8);
  lines.push_back(rank.empty() ? MakeLine(name) : MakeDoubleLine(name, rank));

  std::string cost;
  if (mana_cost != 0u) {
    cost = FormatLocalized("MANA_COST", "%s Mana",
                           {std::to_string(mana_cost)});
  }
  std::string range_text;
  if (range > 0.0f) {
    range_text = FormatLocalized(
        "SPELL_RANGE", "%s yd range",
        {std::to_string(static_cast<std::uint32_t>(range))});
  }
  if (!cost.empty() || !range_text.empty()) {
    lines.push_back(MakeDoubleLine(std::move(cost), std::move(range_text)));
  }

  std::string cast_text;
  if (cast_time > 0.0f) {
    std::array<char, 32> value{};
    std::snprintf(value.data(), value.size(), "%.1f", cast_time);
    cast_text = FormatLocalized("SPELL_CAST_TIME_SEC", "%s sec cast",
                                {value.data()});
  } else {
    cast_text = Localized("SPELL_CAST_TIME_INSTANT", "Instant");
  }
  std::string cooldown_text;
  if (cooldown > 0.0f) {
    cooldown_text = FormatDuration(
        static_cast<std::uint64_t>(std::ceil(cooldown)),
        "SPELL_RECAST_TIME", "Cooldown: %s");
  }
  lines.push_back(
      MakeDoubleLine(std::move(cast_text), std::move(cooldown_text)));
  if (!description.empty()) {
    lines.push_back(MakeWrappedLine(description, kGold));
  }
  return lines;
}

std::vector<TooltipLine> TooltipBuilder::BuildAchievementTooltip(
    const std::string& name, const std::string& description,
    const std::uint32_t points, const bool completed,
    const std::string& date) {
  std::vector<TooltipLine> lines;
  lines.reserve(6);
  lines.push_back(MakeLine(name));
  if (!description.empty()) {
    lines.push_back(MakeWrappedLine(description, kGold));
  }
  if (points != 0u) {
    lines.push_back(MakeLine(FormatLocalized(
        "ACHIEVEMENT_POINTS", "%s Points", {std::to_string(points)})));
  }
  if (completed) {
    auto status = Localized("ACHIEVEMENT_TOOLTIP_COMPLETE", "Completed");
    if (!date.empty()) {
      status += " (" + date + ")";
    }
    lines.push_back(MakeLine(std::move(status), kGreen));
  }
  return lines;
}

}
