#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/tooltip_internal.h"

#include "openwow/core/storm_containers.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/client_config.h"
#include "openwow/game/container_slot_mapping.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/quest_dialog_text.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_query_bridge.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/shapeshift_form_resolver.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/spellbook_system.h"

#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/commerce/trade/trade_item_location.h"
#include "openwow/game/unit_level_display.h"
#include "openwow/game/world_session.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_craft.h"
#include "openwow/ui/game/api/game_lua_api_quest.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_tradeskill_state.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/quest_special_item.h"
#include "openwow/ui/game/quest_leaderboard_builder.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/trade_cursor_utils.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/game/aura_lua_bridge.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui::game {
using namespace tooltip_internal;

namespace tooltip_internal {

void AppendBoundedString(char *dest, const std::size_t capacity,
                         const std::string_view text) {
  if (dest == nullptr || capacity == 0 || text.empty()) {
    return;
  }

  std::size_t length = std::strlen(dest);
  if (length >= capacity - 1) {
    dest[capacity - 1] = '\0';
    return;
  }

  const std::size_t available = capacity - length - 1;
  const std::size_t copy_size = std::min(available, text.size());
  std::memcpy(dest + length, text.data(), copy_size);
  dest[length + copy_size] = '\0';
}

bool BuildPetFoodTypeList(char *dest, const std::size_t capacity,
                          const openwow::data::dbc::DbcLoader &dbc,
                          const openwow::data::dbc::CreatureFamilyEntry &family) {
  if (dest == nullptr || capacity == 0 || family.pet_food_mask == 0) {
    return false;
  }

  dest[0] = '\0';
  bool has_food_type = false;
  for (const auto &pet_food : dbc.item_pet_food().entries()) {
    if (pet_food.id == 0 || pet_food.id > 32) {
      continue;
    }

    const auto food_bit = static_cast<std::uint32_t>(1u << (pet_food.id - 1u));
    if ((family.pet_food_mask & food_bit) == 0) {
      continue;
    }

    if (has_food_type) {
      AppendBoundedString(dest, capacity, ", ");
    }
    AppendBoundedString(dest, capacity, pet_food.name);
    has_food_type = true;
  }

  return has_food_type;
}

bool BuildPetSpellList(char *dest, const std::size_t capacity,
                       const openwow::data::dbc::DbcLoader &dbc,
                       const openwow::data::dbc::CreatureFamilyEntry &family) {
  if (dest == nullptr || capacity == 0) {
    return false;
  }

  dest[0] = '\0';
  bool has_spell = false;
  const std::array<std::uint32_t, 2> skill_lines = {
      family.skill_line_0,
      family.skill_line_1,
  };

  for (const auto skill_line_id : skill_lines) {
    if (skill_line_id == 0) {
      continue;
    }

    for (const auto &ability : dbc.skill_line_ability().entries()) {
      if (ability.skill_id != skill_line_id || ability.superseded_by_spell != 0 ||
          ability.acquire_method != 2) {
        continue;
      }

      const auto *spell = dbc.spell().LookupEntry(ability.spell_id);
      if (spell == nullptr || (spell->attributes & kPetTooltipSpellAttributeMask) != 0 ||
          spell->spell_name.empty()) {
        continue;
      }

      if (has_spell) {
        AppendBoundedString(dest, capacity, ", ");
      }
      AppendBoundedString(dest, capacity, spell->spell_name);
      has_spell = true;
    }
  }

  return has_spell;
}

enum class SummonTitleKind {
  kNone,
  kFallback,
  kSpecific,
};

struct SummonTitleSelection {
  SummonTitleKind kind = SummonTitleKind::kFallback;
  std::uint32_t suffix = 0;
};

const openwow::data::dbc::DbcLoader *ResolveTooltipDbc(
    const openwow::data::dbc::DbcLoader *dbc) {
  if (dbc != nullptr) {
    return dbc;
  }

  if (const auto *session =
          openwow::ui::game::TooltipSystem::Get().GetWorldSession();
      session != nullptr) {
    return session->GetDbcLoader();
  }

  return nullptr;
}

const openwow::game::ObjectManager *ResolveTooltipObjects(
    const openwow::game::ObjectManager *objects) {
  if (objects != nullptr) {
    return objects;
  }

  return openwow::ui::game::TooltipSystem::Get().GetObjectManager();
}

openwow::game::ObjectGuid GetImmediateControllerOrCreatorGuid(
    const openwow::game::CGUnit_C &unit) {
  const auto charmed_by = unit.State().GetCharmedBy();
  if (!charmed_by.IsEmpty()) {
    return charmed_by;
  }

  return unit.State().GetCreatedBy();
}

openwow::game::ObjectGuid ResolveSummonTitleOwnerGuid(
    const openwow::game::CGUnit_C &unit,
    const openwow::game::ObjectManager *objects) {
  const auto *resolved_objects = ResolveTooltipObjects(objects);
  const auto owner_guid = GetImmediateControllerOrCreatorGuid(unit);
  if (owner_guid.IsEmpty() || resolved_objects == nullptr) {
    return owner_guid;
  }

  const auto *owner_unit = resolved_objects->GetUnit(owner_guid);
  if (owner_unit == nullptr) {
    return owner_guid;
  }

  const auto chained_guid = GetImmediateControllerOrCreatorGuid(*owner_unit);
  return chained_guid.IsEmpty() ? owner_guid : chained_guid;
}

std::string ResolveSummonTitleOwnerName(
    const openwow::game::ObjectGuid &owner_guid,
    const openwow::game::ObjectManager *objects) {
  if (owner_guid.IsEmpty()) {
    return {};
  }

  if (const auto *resolved_objects = ResolveTooltipObjects(objects);
      resolved_objects != nullptr) {
    if (const auto *object = resolved_objects->Get(owner_guid); object != nullptr) {
      const std::string object_name = object->GetName();
      if (!object_name.empty()) {
        return object_name;
      }
    }
  }

  if (const auto *session =
          openwow::ui::game::TooltipSystem::Get().GetWorldSession();
      session != nullptr) {
    if (const auto *player_name = session->query_cache().GetPlayerName(owner_guid.GetRawValue());
        player_name != nullptr && !player_name->name.empty()) {
      return player_name->name;
    }

    const std::string object_name = session->objects().GetPlayerName(owner_guid);
    if (!object_name.empty()) {
      return object_name;
    }
  }

  return GetTooltipString("UNKNOWNOBJECT");
}

SummonTitleSelection ResolveSummonTitleSelection(
    const openwow::game::CGUnit_C &unit,
    const openwow::data::dbc::DbcLoader *dbc) {
  const auto *resolved_dbc = ResolveTooltipDbc(dbc);
  if (resolved_dbc == nullptr) {
    return {};
  }

  const auto *spell =
      resolved_dbc->spell().LookupEntry(unit.Casts().GetCreatedBySpell(unit));
  if (spell == nullptr) {
    return {};
  }

  for (std::size_t effect_index = 0; effect_index < spell->effect.size();
       ++effect_index) {
    if (spell->effect[effect_index] != kSpellEffectSummon) {
      continue;
    }

    const auto summon_properties_id =
        static_cast<std::uint32_t>(spell->effect_misc_value_b[effect_index]);
    const auto *summon_properties =
        resolved_dbc->summon_properties().LookupEntry(summon_properties_id);
    if (summon_properties == nullptr || summon_properties->type == 0xFFFFFFFFu) {
      return {};
    }

    if (summon_properties->type == 0u) {
      return {.kind = SummonTitleKind::kNone};
    }

    return {.kind = SummonTitleKind::kSpecific, .suffix = summon_properties->type};
  }

  return {};
}

std::uint32_t ResolveSpellTargetCreatureTypeId(
    const openwow::game::CGUnit_C &unit) {
  if (unit.IsPlayer()) {
    return kHumanoidCreatureTypeId;
  }

  const auto explicit_type = static_cast<std::uint32_t>(unit.State().GetCreatureType());
  if (explicit_type != 0) {
    return explicit_type;
  }

  if (const auto *session =
          openwow::ui::game::TooltipSystem::Get().GetWorldSession();
      session != nullptr) {
    return openwow::game::SpellTargetValidator::GetSpellTargetCreatureTypeId(
        *session, unit);
  }

  return 0;
}

std::string FormatSummonTitleText(
    const openwow::game::CGUnit_C &unit,
    const openwow::data::dbc::DbcLoader *dbc,
    const openwow::game::ObjectManager *objects) {
  const auto owner_guid = ResolveSummonTitleOwnerGuid(unit, objects);
  if (owner_guid.IsEmpty()) {
    return {};
  }

  const auto selection = ResolveSummonTitleSelection(unit, dbc);
  if (selection.kind == SummonTitleKind::kNone) {
    return {};
  }

  std::string title_token;
  if (selection.kind == SummonTitleKind::kSpecific) {
    char token[64];
    std::snprintf(token, sizeof(token), "UNITNAME_SUMMON_TITLE%u", selection.suffix);
    title_token = token;
  } else {
    title_token = ResolveSpellTargetCreatureTypeId(unit) == kHumanoidCreatureTypeId
                      ? kSummonTitleHumanoidToken
                      : kSummonTitleNonHumanoidToken;
  }

  const std::string title_format = GetTooltipString(title_token);
  if (title_format.empty()) {
    return {};
  }

  return openwow::game::Localization::Get().FormatString(
      title_format, {ResolveSummonTitleOwnerName(owner_guid, objects)});
}

}

int ItemTooltip_GetSubClassFlag([[maybe_unused]] void *itemData) {
  return 0;
}

void CGTooltip_BuildUnitStatLines([[maybe_unused]] void *tooltip,
                                  [[maybe_unused]] const void *unitData,
                                  [[maybe_unused]] char *buf,
                                  [[maybe_unused]] unsigned int bufSize) {
  const auto *unit = static_cast<const openwow::game::CGUnit_C *>(unitData);
  if (unit == nullptr) {
    return;
  }

  char local_buffer[1024];
  char diet_buffer[kDietListBufferSize] = {};
  char spell_buffer[kPetSpellListBufferSize] = {};
  char *const line_buffer = (buf != nullptr && bufSize > 0) ? buf : local_buffer;
  const auto line_buffer_size =
      static_cast<std::size_t>(buf != nullptr && bufSize > 0 ? bufSize : sizeof(local_buffer));

  auto &tooltip_system = TooltipSystem::Get();
  const auto *dbc = ResolveTooltipDbcLoader();
  const auto *creature_template = ResolveTooltipCreatureTemplate(*unit);
  const auto *family = ResolveTooltipCreatureFamily(*unit, dbc, creature_template);

  const int minimum_damage = static_cast<int>(unit->State().GetMinDamage());
  const int maximum_damage = static_cast<int>(std::ceil(unit->State().GetMaxDamage()));
  const std::string damage_template =
      minimum_damage == maximum_damage ? GetTooltipString("SINGLE_DAMAGE_TEMPLATE")
                                       : GetTooltipString("DAMAGE_TEMPLATE");
  if (minimum_damage == maximum_damage) {
    FormatTooltipTextFromGlobalString(line_buffer, line_buffer_size,
                                      damage_template.c_str(), minimum_damage);
  } else {
    FormatTooltipTextFromGlobalString(line_buffer, line_buffer_size,
                                      damage_template.c_str(), minimum_damage,
                                      maximum_damage);
  }
  tooltip_system.AddLine(line_buffer);

  const int armor_index = openwow::data::DBClient_GetArmorResistanceIndex(dbc);
  const std::int32_t armor_value =
      armor_index >= 0 ? std::max<std::int32_t>(unit->State().GetResistance(armor_index), 0) : 0;
  const std::string armor_template = GetTooltipString("ARMOR_TEMPLATE");
  FormatTooltipTextFromGlobalString(line_buffer, line_buffer_size,
                                    armor_template.c_str(), armor_value);
  tooltip_system.AddLine(line_buffer);

  const std::string health_template = GetTooltipString("HP_TEMPLATE");
  FormatTooltipTextFromGlobalString(line_buffer, line_buffer_size, health_template.c_str(),
                                    unit->State().GetHealth());
  tooltip_system.AddLine(line_buffer);

  for (std::uint8_t school = 0; school < 7; ++school) {
    if (static_cast<int>(school) == armor_index) {
      continue;
    }

    const auto resistance_value = unit->State().GetResistance(school);
    if (resistance_value <= 0) {
      continue;
    }

    char resistance_key[32];
    std::snprintf(resistance_key, sizeof(resistance_key), "RESISTANCE%d_NAME", school);
    const std::string resistance_name =
        openwow::game::Localization::Get().GetString(resistance_key, "");
    if (resistance_name.empty()) {
      continue;
    }

    const std::string resistance_template = GetTooltipString("RESISTANCE_TEMPLATE");
    FormatTooltipTextFromGlobalString(line_buffer, line_buffer_size,
                                      resistance_template.c_str(), resistance_value,
                                      resistance_name.c_str());
    tooltip_system.AddLine(line_buffer);
  }

  if (family != nullptr && dbc != nullptr &&
      BuildPetFoodTypeList(diet_buffer, sizeof(diet_buffer), *dbc, *family)) {
    const std::string diet_template = GetTooltipString("PET_DIET_TEMPLATE");
    FormatTooltipTextFromGlobalString(line_buffer, line_buffer_size, diet_template.c_str(),
                                      diet_buffer);
    tooltip_system.AddLine(line_buffer);
  }

  if (creature_template != nullptr &&
      (creature_template->type_flags & kCreatureTemplateTypeFlagTameable) != 0 &&
      creature_template->creature_family != 0) {
    tooltip_system.AddLine(
        (creature_template->type_flags & kCreatureTemplateTypeFlagExotic) != 0
            ? GetTooltipString("TAMEABLE_EXOTIC")
            : GetTooltipString("TAMEABLE"));

    if (family != nullptr && dbc != nullptr &&
        BuildPetSpellList(spell_buffer, sizeof(spell_buffer), *dbc, *family)) {
      const std::string spell_template = GetTooltipString("PET_SPELLS_TEMPLATE");
      FormatTooltipTextFromGlobalString(line_buffer, line_buffer_size, spell_template.c_str(),
                                        spell_buffer);
      tooltip_system.AddLine(line_buffer);
    }
    return;
  }

  tooltip_system.AddLine(GetTooltipString("NOT_TAMEABLE"));
}

void CGTooltip_BuildUnitSkinningLine([[maybe_unused]] void *tooltip,
                                     [[maybe_unused]] void *unitData, [[maybe_unused]] char *buf,
                                     [[maybe_unused]] unsigned int bufSize) {
  const auto *unit = static_cast<const openwow::game::CGUnit_C *>(unitData);
  if (unit == nullptr) {
    return;
  }

  char local_buffer[1024];
  char *const line_buffer = (buf != nullptr && bufSize > 0) ? buf : local_buffer;
  const auto line_buffer_size =
      static_cast<std::size_t>(buf != nullptr && bufSize > 0 ? bufSize : sizeof(local_buffer));

  auto &tooltip_system = TooltipSystem::Get();
  const auto *creature_template = ResolveTooltipCreatureTemplate(*unit);

  using openwow::game::SkinnableResourceType;
  auto resource_type = SkinnableResourceType::Leather;
  if (creature_template != nullptr) {
    const auto tf = creature_template->type_flags;
    if ((tf & kCreatureTypeFlagHerbSkinnable) != 0) {
      resource_type = SkinnableResourceType::Herb;
    } else if ((tf & kCreatureTypeFlagMiningSkinnable) != 0) {
      resource_type = SkinnableResourceType::Rock;
    } else if ((tf & kCreatureTypeFlagEngineeringSkinnable) != 0) {
      resource_type = SkinnableResourceType::Bolts;
    }
  }

  std::size_t color_index = 4;

  const auto &spellbook = openwow::game::SpellbookSystem::Get();
  const std::uint32_t spell_id =
      spellbook.GetSkinnableGatherInteractionSpellId(resource_type);

  if (spell_id != 0) {
    const auto *dbc = ResolveTooltipDbcLoader();
    if (dbc != nullptr) {
      const auto *spell = dbc->spell().LookupEntry(spell_id);
      if (spell != nullptr) {
        for (std::size_t i = 0; i < spell->effect.size(); ++i) {
          if (spell->effect[i] != kSpellEffectSkinning) {
            continue;
          }

          const int spell_value = spell->effect_base_points[i] + 1;
          if (spell_value <= 0) {
            break;
          }

          const int unit_level = static_cast<int>(unit->State().GetLevel());
          int required_skill;
          if (unit_level > 10) {
            required_skill = (unit_level >= 20) ? 5 * unit_level
                                                : 2 * (5 * unit_level - 50);
          } else {
            required_skill = 1;
          }

          if (spell_value >= required_skill + 100) {
            color_index = 0;
          } else if (spell_value >= required_skill + 50) {
            color_index = 1;
          } else if (spell_value >= required_skill + 25) {
            color_index = 2;
          } else if (spell_value >= required_skill) {
            color_index = 3;
          }

          break;
        }
      }
    }
  }

  const char *skinnable_key = nullptr;
  switch (resource_type) {
    case SkinnableResourceType::Leather:
      skinnable_key = "UNIT_SKINNABLE_LEATHER";
      break;
    case SkinnableResourceType::Herb:
      skinnable_key = "UNIT_SKINNABLE_HERB";
      break;
    case SkinnableResourceType::Rock:
      skinnable_key = "UNIT_SKINNABLE_ROCK";
      break;
    case SkinnableResourceType::Bolts:
      skinnable_key = "UNIT_SKINNABLE_BOLTS";
      break;
  }

  if (skinnable_key == nullptr) {
    return;
  }

  const std::string skinnable_text = GetTooltipString(skinnable_key);
  std::snprintf(line_buffer, line_buffer_size, "%s", skinnable_text.c_str());

  if (line_buffer[0] != '\0') {
    const auto &dc = kSkinningDifficultyColors[color_index];
    tooltip_system.AddLine(line_buffer, dc.r, dc.g, dc.b);
  }
}

bool BuildUnitTooltipForUnit(openwow::ui::game::TooltipSystem &tooltip_system,
                             const openwow::game::CGUnit_C &unit,
                             const openwow::game::ObjectManager *objects,
                             const openwow::data::dbc::DbcLoader *dbc,
                             const bool hideStatus) {
  tooltip_system.ClearLines();
  tooltip_system.SetUnitGuid(unit.GetGuid().GetRawValue());

  const auto *viewer = objects != nullptr ? objects->GetPlayer(objects->GetLocalPlayerGuid())
                                          : nullptr;
  int displayed_level = static_cast<int>(unit.State().GetLevel());
  if (viewer != nullptr) {
    displayed_level = openwow::game::ApplyHostileDrunkLevelMask(*viewer, unit, displayed_level);
  }

  const auto *tooltip_session = tooltip_system.GetWorldSession();
  const std::string display_name =
      tooltip_session != nullptr ? unit.ResolveRetailName(*tooltip_session) : unit.GetName();
  tooltip_system.AddLine(display_name, 1.0f, 1.0f, 1.0f);

  if (const std::string summon_title = BuildSummonTitleText(unit, dbc, objects);
      !summon_title.empty()) {
    tooltip_system.AddLine("<" + summon_title + ">", 1.0f, 1.0f, 1.0f);
  }

  tooltip_system.AddLine("Level " + std::to_string(displayed_level),
                         kTooltipGoldR, kTooltipGoldG, kTooltipGoldB);

  if (!hideStatus && (unit.State().GetDynamicFlags() & openwow::game::kUnitDynFlagSpecialInfo) != 0u) {
    char special_info_buffer[1024];
    CGTooltip_BuildUnitStatLines(nullptr, &unit, special_info_buffer,
                                 static_cast<unsigned int>(sizeof(special_info_buffer)));
  }

  tooltip_system.Show();
  return true;
}

void BuildCorpseTooltip(TooltipSystem& tooltip_system, std::uint64_t corpseGuid) {
  tooltip_system.ClearLines();

  const auto* const obj_mgr = tooltip_system.GetObjectManager();
  if (obj_mgr == nullptr) {
    tooltip_system.Hide();
    return;
  }
  const auto *corpse = obj_mgr->GetCorpse(openwow::game::ObjectGuid(corpseGuid));
  if (corpse == nullptr) {
    tooltip_system.Hide();
    return;
  }

  const auto owner_guid = corpse->GetOwner();

  const std::string name = obj_mgr->GetPlayerName(owner_guid);
  if (name.empty()) {
    return;
  }

  const std::string line = FormatTooltipLine("CORPSE_TOOLTIP", name.c_str());
  tooltip_system.AddLine(line);
  tooltip_system.Show();
}

}
