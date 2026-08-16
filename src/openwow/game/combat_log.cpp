
#include "openwow/game/combat_log.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/aura_manager.h"
#include "openwow/game/combat_handler.h"
#include "openwow/game/client_text_log_files.h"
#include "openwow/game/combat_manager.h"
#include "openwow/game/combat_log_display.h"
#include "openwow/game/combat_log_internal.h"
#include "openwow/game/combat_log_messages.h"
#include "openwow/game/group_system.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_log.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/ui/game/script_event_dispatch.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::game {
namespace {

std::string ResolveCombatLogUnitName(ObjectManager& objects,
                                     const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return {};
  }

  if (guid.IsPlayer()) {

    if (const auto* cached = objects.query_cache().GetOrRequestPlayerName(
            guid.GetRawValue());
        cached != nullptr && !cached->name.empty()) {
      if (cached->realm_name.empty()) {
        return cached->name;
      }
      return cached->name + "-" + cached->realm_name;
    }
  }

  if (const auto* object = objects.Get(guid); object != nullptr &&
                                                !object->GetName().empty()) {
    return object->GetName();
  }
  if (guid.IsPlayer()) {
    if (const std::string cached = objects.GetPlayerName(guid);
        !cached.empty()) {
      return cached;
    }
  } else if (guid.IsCreatureOrPetOrVehicle()) {
    if (const auto* cached = objects.query_cache().GetOrRequestCreatureTemplate(
            guid.GetEntry(), guid.GetRawValue());
        cached != nullptr && !cached->name.empty()) {
      return cached->name;
    }
  } else if (guid.IsAnyTypeGameObject()) {
    if (const auto* cached = objects.query_cache().GetOrRequestGameObjectTemplate(
            guid.GetEntry(), guid.GetRawValue());
        cached != nullptr && !cached->name.empty()) {
      return cached->name;
    }
  }

  return CombatLog_BuildNameForGUID(guid.GetRawValue());
}

std::uint32_t ResolveCombatLogUnitFlags(const ObjectManager& objects,
                                        const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return UnitFlag::kNone;
  }

  std::uint32_t flags = CombatLog_BuildUnitFlags(guid) & 0xFFFF0000u;
  const auto* unit = objects.GetUnit(guid);

  if (guid.IsPlayer()) {
    flags |= UnitFlag::kTypePlayer;
  } else if (guid.IsPet()) {
    flags |= UnitFlag::kTypePet;
  } else if (guid.IsCreature() || guid.IsVehicle()) {
    const bool player_owned =
        unit != nullptr && unit->Interaction().IsPlayerControlled();
    flags |= player_owned ? UnitFlag::kTypeGuardian : UnitFlag::kTypeNPC;
  } else {
    flags |= UnitFlag::kTypeObject;
  }

  const bool player_controlled =
      guid.IsPlayer() || (unit != nullptr && unit->Interaction().IsPlayerControlled());
  flags |= player_controlled ? UnitFlag::kControlPlayer : UnitFlag::kControlNPC;

  const ObjectGuid local_player = objects.GetLocalPlayerGuid();
  ObjectGuid affiliation_guid = guid;
  if (unit != nullptr) {
    const ObjectGuid controller = unit->Interaction().GetControllingPlayerGuid();
    if (!controller.IsEmpty()) {
      affiliation_guid = controller;
    }
  }

  if (affiliation_guid == local_player) {
    flags |= UnitFlag::kAffilMine;
  } else if (GroupSystem::Get().IsActivePlayerOrPartyMemberGuid(
                 affiliation_guid.GetRawValue())) {
    flags |= UnitFlag::kAffilParty;
  } else if (GroupSystem::Get().IsRaidMemberGuid(
                 affiliation_guid.GetRawValue())) {
    flags |= UnitFlag::kAffilRaid;
  } else {
    flags |= UnitFlag::kAffilOutsider;
  }

  if (guid == local_player || affiliation_guid == local_player) {
    flags |= UnitFlag::kReactionFriendly;
  } else if (const auto* player = objects.GetLocalPlayerTyped();
             player != nullptr && unit != nullptr) {
    const ReactionType reaction = player->Interaction().GetReaction(*unit);
    if (reaction >= ReactionType::kFriendly) {
      flags |= UnitFlag::kReactionFriendly;
    } else if (reaction <= ReactionType::kHostile) {
      flags |= UnitFlag::kReactionHostile;
    } else {
      flags |= UnitFlag::kReactionNeutral;
    }
  } else {
    flags |= UnitFlag::kReactionNeutral;
  }

  if (guid == objects.GetTargetGuid()) {
    flags |= 0x00010000u;
  }
  if (guid == objects.GetFocusTargetGuid()) {
    flags |= 0x00020000u;
  }
  if (guid == GroupSystem::Get().GetMainTank()) {
    flags |= UnitFlag::kMainTank;
  }
  if (guid == GroupSystem::Get().GetMainAssist()) {
    flags |= UnitFlag::kMainAssist;
  }
  const std::uint8_t raid_target =
      GroupSystem::Get().GetRaidTargetIndex(guid.GetRawValue());
  if (raid_target < 8u) {
    flags |= 0x00100000u << raid_target;
  }

  return flags;
}

std::uint64_t NormalizeCombatLogGuid(const ObjectManager& objects,
                                     const std::uint64_t raw_guid) {
  const ObjectGuid guid(raw_guid);
  if (!guid.IsCreature() && !guid.IsVehicle()) {
    return raw_guid;
  }

  const auto* creature =
      objects.query_cache().GetCreatureTemplate(guid.GetEntry());
  if (creature == nullptr || (creature->type_flags & 0x40u) == 0u) {
    return raw_guid;
  }

  return raw_guid & 0xFFFFFFFFFF000000ull;
}

void PopulateCombatLogIdentity(ObjectManager& objects,
                               CombatLogEntry& entry) {
  if (entry.source_guid != 0) {
    const ObjectGuid source(entry.source_guid);
    entry.source_name = ResolveCombatLogUnitName(objects, source);
    entry.source_flags = ResolveCombatLogUnitFlags(objects, source);
  }
  if (entry.dest_guid != 0) {
    const ObjectGuid destination(entry.dest_guid);
    entry.dest_name = ResolveCombatLogUnitName(objects, destination);
    entry.dest_flags = ResolveCombatLogUnitFlags(objects, destination);
  }
}

class CombatLogCsvLineBuilder {
public:
  void AppendGuid(const std::uint64_t value) {
    char buffer[19];
    std::snprintf(buffer, sizeof(buffer), "0x%016llX",
                  static_cast<unsigned long long>(value));
    AppendRaw(buffer);
  }

  void AppendHex(const std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%x", value);
    AppendRaw(buffer);
  }

  void AppendInt(const std::int32_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    AppendRaw(buffer);
  }

  void AppendUInt(const std::uint32_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%u", value);
    AppendRaw(buffer);
  }

  void AppendNil() {
    AppendRaw("nil");
  }

  void AppendOneOrNil(const bool value) {
    if (value) {
      AppendRaw("1");
      return;
    }
    AppendNil();
  }

  void AppendQuotedOrNil(const std::string& value) {
    if (value.empty()) {
      AppendNil();
      return;
    }

    BeginField();
    output_.push_back('"');
    for (const char ch : value) {
      if (ch == '"' || ch == '\\') {
        output_.push_back('\\');
      }
      output_.push_back(ch);
    }
    output_.push_back('"');
  }

  void AppendRaw(const std::string_view value) {
    BeginField();
    output_.append(value.data(), value.size());
  }

  [[nodiscard]] const std::string& str() const {
    return output_;
  }

private:
  void BeginField() {
    if (!output_.empty()) {
      output_.push_back(',');
    }
  }

  std::string output_;
};

struct CombatLogSpellData {
  std::uint32_t spell_id = 0;
  std::string name;
  std::uint32_t school = 0;
  std::uint32_t mechanic = 0;
  std::uint32_t attributes = 0;
  std::uint32_t attributes_ex4 = 0;
  std::uint32_t attributes_ex6 = 0;
  std::array<std::uint32_t, 3> effect_ids{};
  std::array<std::uint32_t, 3> effect_trigger_spells{};
  std::uint32_t stack_amount = 0;
  bool has_dbc_entry = false;
};

std::optional<CombatLogSpellData> LookupCombatLogSpellData(
    const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return std::nullopt;
  }

  CombatLogSpellData result;
  result.spell_id = spell_id;

  if (const auto query = SpellQueryBridge::Get().Query(spell_id)) {
    result.name = query->name;
    result.attributes = query->attributes;
    result.attributes_ex4 = query->attributesEx4;
    result.attributes_ex6 = query->attributesEx6;
    result.effect_ids = query->effectIds;
  } else {
    result.name = SpellQueryBridge::Get().GetSpellName(spell_id);
  }

  if (const auto* dbc = SpellbookSystem::Get().GetDbcLoader()) {
    if (const auto* spell = dbc->spell().LookupEntry(spell_id)) {
      result.has_dbc_entry = true;
      if (result.name.empty()) {
        result.name = std::string(spell->spell_name);
      }
      result.school = spell->school_mask;
      result.mechanic = spell->mechanic;
      result.attributes = spell->attributes;
      result.attributes_ex4 = spell->attributes_ex4;
      result.attributes_ex6 = spell->attributes_ex6;
      result.effect_ids = spell->effect;
      result.effect_trigger_spells = spell->effect_trigger_spell;
      result.stack_amount = spell->stack_amount;
    }
  }

  if (result.name.empty()) {
    return std::nullopt;
  }

  return result;
}

void PopulateCombatLogSpellMetadata(CombatLogEntry& entry) {
  if (entry.spell_id != 0) {
    if (const auto spell = LookupCombatLogSpellData(entry.spell_id);
        spell.has_value()) {
      if (entry.spell_name.empty()) {
        entry.spell_name = spell->name;
      }
      if (spell->has_dbc_entry) {
        entry.spell_school = spell->school;
      }
    }
  }

  if (entry.extra_spell_id != 0) {
    if (const auto spell = LookupCombatLogSpellData(entry.extra_spell_id);
        spell.has_value()) {
      if (entry.extra_spell_name.empty()) {
        entry.extra_spell_name = spell->name;
      }
      if (spell->has_dbc_entry) {
        entry.extra_spell_school = spell->school;
      }
    }
  }
}

bool CanCreateSpellMissEntry(const CombatLogSpellData& spell) {

  return spell.has_dbc_entry && (spell.attributes & 0x180u) == 0;
}

bool SpellHasEffectId(const CombatLogSpellData& spell, const std::uint32_t effect_id) {
  return std::find(
             spell.effect_ids.begin(),
             spell.effect_ids.end(),
             effect_id) != spell.effect_ids.end();
}

bool CanCreateSpellCastEntry(const CombatLogSpellData& spell) {

  return spell.has_dbc_entry &&
         !spell.name.empty() &&
         (spell.attributes & 0x180u) == 0u &&
         !SpellHasEffectId(spell, 36u) &&
         (spell.attributes_ex4 & 0x1u) == 0u;
}

bool CanCreateCombatLogSpellEntry(const CombatLogSpellData& spell) {
  return spell.has_dbc_entry && !spell.name.empty() &&
         (spell.attributes & 0x180u) == 0u;
}

bool CanCreateAuraChangeEntry(const CombatLogSpellData& spell) {
  return CanCreateCombatLogSpellEntry(spell) &&
         (spell.attributes & 0x40u) == 0u &&
         (spell.attributes_ex6 & 0x400u) == 0u;
}

constexpr std::uint32_t kAreaSpiritHealWaitingSpellId = 2584u;

bool CanCreateAuraDoseEntry(const CombatLogSpellData& spell,
                            const std::size_t active_aura_count) {
  return CanCreateCombatLogSpellEntry(spell) && active_aura_count > 1u;
}

bool CanCreateAuraRefreshEntry(const CombatLogSpellData& spell) {
  return spell.has_dbc_entry &&
         !spell.name.empty() &&
         (spell.attributes & 0x1C0u) == 0u &&
         (spell.attributes_ex6 & 0x400u) == 0u;
}

const char* AuraTypeName(const AuraSlotInfo& aura) {
  return HasFlag(aura.flags, AuraFlag::kNegative) ? "DEBUFF" : "BUFF";
}

void EmitSpellMechanicDisplay(CombatLog& log,
                              const std::uint64_t target_guid,
                              const std::uint64_t source_guid,
                              const CombatLogSpellData& spell) {
  if (!CombatLog_ShouldShowSpellMechanics(spell.attributes_ex6, source_guid)) {
    return;
  }

  const auto* const dbc = SpellbookSystem::Get().GetDbcLoader();
  if (dbc == nullptr || spell.mechanic == 0u) {
    return;
  }
  const auto* const mechanic = dbc->spell_mechanic().LookupEntry(spell.mechanic);
  if (mechanic == nullptr || mechanic->name.empty()) {
    return;
  }

  std::string text(mechanic->name);
  if (!CombatLog_IsActivePlayerTarget(source_guid)) {
    const std::string source_name = CombatLog_BuildNameForGUID(source_guid);
    if (!source_name.empty()) {
      text += " (";
      text += source_name;
      text += ")";
    }
  }

  CombatEvent event{};
  event.type = CombatEventType::kSpellMechanic;
  event.source = ObjectGuid(source_guid);
  event.target = ObjectGuid(target_guid);
  event.spell_id = spell.spell_id;
  event.mechanic_text = std::move(text);
  log.AddEvent(std::move(event));
}

void EmitAuraTriggeredSpellMechanics(CombatLog& log,
                                     const std::uint64_t target_guid,
                                     const std::uint64_t source_guid,
                                     const CombatLogSpellData& parent_spell) {

  if (!CanCreateCombatLogSpellEntry(parent_spell) ||
      SpellHasEffectId(parent_spell, 0x24u) ||
      SpellHasEffectId(parent_spell, 0x21u) ||
      (parent_spell.attributes_ex4 & 0x1u) != 0u) {
    return;
  }

  std::array<std::uint32_t, 3> emitted{};
  std::size_t emitted_count = 0;
  for (const std::uint32_t trigger_spell_id : parent_spell.effect_trigger_spells) {
    if (trigger_spell_id == 0u ||
        std::find(emitted.begin(), emitted.begin() + emitted_count,
                  trigger_spell_id) != emitted.begin() + emitted_count) {
      continue;
    }
    emitted[emitted_count++] = trigger_spell_id;

    const auto trigger_spell = LookupCombatLogSpellData(trigger_spell_id);
    if (!trigger_spell.has_value() ||
        !CanCreateCombatLogSpellEntry(*trigger_spell)) {
      continue;
    }

    if (trigger_spell->effect_ids[0] == 0x1au) {
      continue;
    }
    EmitSpellMechanicDisplay(log, target_guid, source_guid, *trigger_spell);
  }
}

bool CanCreateSpellLogExecuteResurrectEntry(const CombatLogSpellData& spell) {
  return CanCreateCombatLogSpellEntry(spell);
}

const char* LookupSpellMissTypeName(const std::uint8_t miss_info) {
  switch (miss_info) {
    case 1:  return "MISS";
    case 2:  return "RESIST";
    case 3:  return "DODGE";
    case 4:  return "PARRY";
    case 5:  return "BLOCK";
    case 6:  return "EVADE";
    case 7:  return "IMMUNE";
    case 8:  return "IMMUNE";
    case 9:  return "DEFLECT";
    case 10: return "ABSORB";
    case 11: return "REFLECT";
    default: return nullptr;
  }
}

MissType MapSpellMissInfoToMissType(const std::uint8_t miss_info) {
  switch (miss_info) {
    case 2:  return MissType::Resist;
    case 3:  return MissType::Dodge;
    case 4:  return MissType::Parry;
    case 5:  return MissType::Block;
    case 6:  return MissType::Evade;
    case 7:
    case 8:  return MissType::Immune;
    case 9:  return MissType::Deflect;
    case 10: return MissType::Absorb;
    case 11: return MissType::Reflect;
    default: return MissType::Miss;
  }
}

constexpr std::array<const char*, 6> kEnvironmentalDamageTypeNames{
    "FATIGUE",
    "DROWNING",
    "FALLING",
    "LAVA",
    "SLIME",
    "FIRE",
};

constexpr std::array<std::uint32_t, 6> kEnvironmentalDamageSchoolMasks{
    0x1u,
    0x1u,
    0x1u,
    0x4u,
    0x8u,
    0x4u,
};

std::uint64_t g_combat_text_active_unit_guid = 0;

CombatLogEntry BuildSpellMissEntry(const CombatLogSpellData& spell,
                                   const std::uint64_t source_guid,
                                   const std::uint64_t dest_guid,
                                   const CombatLogEventType type,
                                   const char* const miss_type) {
  CombatLogEntry entry = CombatLog_CreateEntry(
      source_guid, dest_guid, 8, spell.spell_id, spell.name);
  entry.type = type;
  entry.spell_school = spell.school;
  entry.miss_type = miss_type;
  return entry;
}

void PopulateExtraSpellSuffix(CombatLogEntry& entry,
                              const CombatLogSpellData& spell) {
  entry.extra_spell_id = spell.spell_id;
  entry.extra_spell_name = spell.name;
  entry.extra_spell_school = spell.school;
}

void FireAuraCombatText(const std::uint64_t target_guid,
                        const CombatLogSpellData& spell,
                        const bool is_apply,
                        const bool is_harmful) {
  if (!CombatLog_IsActivePlayerTarget(target_guid)) {
    return;
  }

  if (is_apply) {
    if (is_harmful) {
      CombatLog_FireCombatTextSS(34, spell.name.c_str());
    } else if ((spell.attributes_ex6 & 0x40u) == 0u) {

      CombatLog_FireCombatTextSS(33, spell.name.c_str());
    }
  } else {
    if (is_harmful) {
      CombatLog_FireCombatTextSS(32, spell.name.c_str());
    } else {
      CombatLog_FireCombatTextSS(31, spell.name.c_str());
    }
  }
}

void FireLocalSpellDispelledText(const std::uint64_t victim_guid,
                                 const std::string_view aura_name) {
  if (aura_name.empty()) {
    return;
  }

  if (!CombatText_IsActiveUnit(victim_guid)) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireCombatTextUpdate(
      std::string("SPELL_DISPELLED") + std::string(aura_name));
}

void FireEnvironmentalDamageCombatText(const std::uint64_t target_guid,
                                       const std::int32_t amount,
                                       const std::int32_t absorbed,
                                       const std::int32_t resisted) {
  if (!CombatText_IsActiveUnit(target_guid)) {
    return;
  }

  auto& dispatcher = ui::game::ScriptEventDispatch::Get();
  if (resisted != 0) {
    dispatcher.FireCombatTextUpdate("RESIST", amount, resisted);
    return;
  }
  if (absorbed != 0) {
    dispatcher.FireCombatTextUpdate("ABSORB", amount, absorbed);
    return;
  }
  dispatcher.FireCombatTextUpdate("DAMAGE", amount);
}

std::string FormatCombatLogFileLine(const CombatLogEntry& entry) {
  CombatLogCsvLineBuilder builder;
  builder.AppendRaw(CombatLog::GetEventName(entry.type));
  builder.AppendGuid(entry.source_guid);
  builder.AppendQuotedOrNil(entry.source_name);
  builder.AppendHex(entry.source_flags);
  builder.AppendGuid(entry.dest_guid);
  builder.AppendQuotedOrNil(entry.dest_name);
  builder.AppendHex(entry.dest_flags);

  if (entry.spell_id != 0) {
    builder.AppendUInt(entry.spell_id);
    builder.AppendQuotedOrNil(entry.spell_name);
    builder.AppendHex(entry.spell_school);
  }

  const auto suffix_flags = entry.suffix_flags != 0
                                ? entry.suffix_flags
                                : CombatLog_DefaultSuffixFlags(entry);
  const auto append_aura_type = [&builder, &entry] {
    if (entry.aura_type == "DEBUFF") {
      builder.AppendRaw("DEBUFF");
    } else {
      builder.AppendRaw("BUFF");
    }
  };

  if ((suffix_flags & CombatLogSuffixFlag::kAmount) != 0u) {
    builder.AppendInt(entry.amount);
  }
  if ((suffix_flags & CombatLogSuffixFlag::kString) != 0u) {
    builder.AppendQuotedOrNil(entry.failed_message);
  }
  if ((suffix_flags & CombatLogSuffixFlag::kMissType) != 0u) {
    if (entry.miss_type.empty()) {
      builder.AppendNil();
    } else {
      builder.AppendRaw(entry.miss_type);
    }
  }
  if ((suffix_flags & CombatLogSuffixFlag::kEnvironmentalType) != 0u) {
    builder.AppendQuotedOrNil(entry.env_type);
  }
  if ((suffix_flags & CombatLogSuffixFlag::kDamage) != 0u) {
    builder.AppendInt(entry.amount);
    builder.AppendInt(entry.overkill);
    builder.AppendUInt(entry.school);
    builder.AppendInt(entry.resisted);
    builder.AppendInt(entry.blocked);
    builder.AppendInt(entry.absorbed);
    builder.AppendOneOrNil(entry.critical);
    builder.AppendOneOrNil(entry.glancing);
    builder.AppendOneOrNil(entry.crushing);
  }

  if ((suffix_flags & CombatLogSuffixFlag::kAbsorbed) != 0u) {
    builder.AppendInt(entry.absorbed);
  }
  if ((suffix_flags & CombatLogSuffixFlag::kResisted) != 0u) {
    builder.AppendInt(entry.resisted);
  }
  if ((suffix_flags & CombatLogSuffixFlag::kBlocked) != 0u) {
    builder.AppendInt(entry.blocked);
  }
  if ((suffix_flags & CombatLogSuffixFlag::kHeal) != 0u) {
    builder.AppendInt(entry.amount);
    builder.AppendInt(entry.overheal);
    builder.AppendInt(entry.absorbed);
    builder.AppendOneOrNil(entry.critical);
  }
  if ((suffix_flags & CombatLogSuffixFlag::kEnergize) != 0u) {
    builder.AppendInt(entry.power_amount);
    builder.AppendInt(entry.power_type);
  }
  if ((suffix_flags & CombatLogSuffixFlag::kDrain) != 0u) {
    builder.AppendInt(entry.power_amount);
    builder.AppendInt(entry.power_type);
    if (entry.energize_amount == 0) {
      builder.AppendNil();
    } else {
      builder.AppendInt(entry.energize_amount);
    }
  }
  if ((suffix_flags & CombatLogSuffixFlag::kExtraSpell) != 0u) {
    builder.AppendUInt(entry.extra_spell_id);
    builder.AppendQuotedOrNil(entry.extra_spell_name);
    builder.AppendHex(entry.extra_spell_school);
  }
  if ((suffix_flags & CombatLogSuffixFlag::kStringAtOffset58) != 0u) {
    builder.AppendQuotedOrNil(entry.aura_type);
  }
  if ((suffix_flags & CombatLogSuffixFlag::kNumberAndString) != 0u) {
    if (entry.type == CombatLogEventType::SPELL_DURABILITY_DAMAGE) {
      builder.AppendUInt(entry.extra_spell_id);
      builder.AppendQuotedOrNil(entry.extra_spell_name);
    } else {
      builder.AppendInt(entry.aura_amount);
      builder.AppendQuotedOrNil(entry.aura_type);
    }
  }
  if ((suffix_flags & CombatLogSuffixFlag::kAuraType) != 0u) {
    append_aura_type();
  }
  if ((suffix_flags & CombatLogSuffixFlag::kNumber) != 0u) {
    builder.AppendInt(entry.aura_amount);
  }

  if (CombatLog_EventHasEnchantNameSuffix(entry.type)) {
    builder.AppendQuotedOrNil(entry.enchant_name);
    builder.AppendUInt(entry.enchant_item_id);
    builder.AppendQuotedOrNil(entry.enchant_item_name);
  }

  return builder.str();
}

}

const char* CombatLog_GetEnvironmentalDamageTypeName(const std::uint8_t type) {
  const auto index = static_cast<std::size_t>(type);
  if (index >= kEnvironmentalDamageTypeNames.size()) {
    return nullptr;
  }
  return kEnvironmentalDamageTypeNames[index];
}

std::uint32_t CombatLog_GetEnvironmentalDamageSchoolMask(const std::uint8_t type) {
  const auto index = static_cast<std::size_t>(type);
  if (index >= kEnvironmentalDamageSchoolMasks.size()) {
    return 0;
  }
  return kEnvironmentalDamageSchoolMasks[index];
}

void CombatText_SetActiveUnitGuid(const std::uint64_t guid) {
  g_combat_text_active_unit_guid = guid;
}

std::uint64_t CombatText_GetActiveUnitGuid() {
  return g_combat_text_active_unit_guid;
}

bool CombatText_IsActiveUnit(const std::uint64_t guid) {
  return CombatText_GetActiveUnitGuid() == guid;
}

double CombatLog::Now() const {
  if (timestamp_fn_) return timestamp_fn_();

  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(now.time_since_epoch()).count();
}

double CombatLog::TimestampWithOffsetMs(const std::uint32_t offset_ms) const {
  return Now() - (static_cast<double>(offset_ms) / 1000.0);
}

void CombatLog::AddEvent(CombatEvent&& evt) {
  if (on_event_) on_event_(evt);
  events_.push_back(std::move(evt));
  while (events_.size() > kMaxEntries) events_.pop_front();
}

void CombatLog::AddLogEntry(CombatLogEntry entry) {
  ObjectManager* const objects =
      object_manager_provider_ ? object_manager_provider_() : nullptr;
  if (objects != nullptr) {
    PopulateCombatLogIdentity(*objects, entry);
    entry.source_guid = NormalizeCombatLogGuid(*objects, entry.source_guid);
    entry.dest_guid = NormalizeCombatLogGuid(*objects, entry.dest_guid);
  }
  PopulateCombatLogSpellMetadata(entry);
  if (entry.suffix_flags == 0) {
    entry.suffix_flags = CombatLog_DefaultSuffixFlags(entry);
  }
  if (entry.timestamp == 0.0) entry.timestamp = Now();

  Update(Now());

  const bool had_current_entry = current_index_ < log_entries_.size();
  const std::string file_line = FormatCombatLogFileLine(entry);
  log_entries_.push_back(std::move(entry));
  ++log_entry_serial_;
  while (log_entries_.size() > max_log_entries_) {
    log_entries_.pop_front();
    if (current_index_ > 0) --current_index_;
  }
  if (!had_current_entry) {
    current_index_ = log_entries_.size();
  }

  if (!log_entries_.empty()) {
    AppendClientTextLogLine(ClientTextLogKind::Combat, file_line);

    const CombatLogEntry dispatch_entry = log_entries_.back();
    ui::game::ScriptEventDispatch::Get().FireCombatLogEvents(dispatch_entry);
  }
}

const CombatLogEntry* CombatLog::current_entry() const {
  if (log_entries_.empty()) return nullptr;
  if (current_index_ >= log_entries_.size()) return nullptr;
  return &log_entries_[current_index_];
}

bool CombatLog::AdvanceEntry() {
  if (current_index_ + 1 >= log_entries_.size()) return false;
  ++current_index_;
  return true;
}

void CombatLog::Update(double currentTime) {
  if (retention_time_s_ <= 0.0f) return;
  const double cutoff = currentTime - static_cast<double>(retention_time_s_);
  while (!log_entries_.empty() && log_entries_.front().timestamp < cutoff) {
    log_entries_.pop_front();
    if (current_index_ > 0) --current_index_;
  }
}

void CombatLog::Clear() {
  events_.clear();
  log_entries_.clear();
  pending_spell_cast_starts_.clear();
  current_index_ = log_entries_.size();
}

CombatLogEntry CombatLog::ToLogEntry(const CombatEvent& evt,
                                     CombatLogEventType type) const {
  CombatLogEntry e;
  e.type = type;
  e.timestamp = Now();
  e.source_guid = evt.source.IsEmpty() ? 0 : evt.source.GetRawValue();
  e.dest_guid = evt.target.IsEmpty() ? 0 : evt.target.GetRawValue();
  e.spell_id = evt.spell_id;
  e.amount = static_cast<std::int32_t>(evt.amount);
  e.overkill = static_cast<std::int32_t>(evt.overkill);
  e.school = evt.school_mask;
  e.spell_school = evt.school_mask;
  e.absorbed = static_cast<std::int32_t>(evt.absorb);
  e.resisted = static_cast<std::int32_t>(evt.resist);
  e.blocked = static_cast<std::int32_t>(evt.blocked);
  e.critical = evt.critical;
  e.glancing = HasHitInfo(evt.hit_info, HitInfo::kGlancing);
  e.crushing = HasHitInfo(evt.hit_info, HitInfo::kCrushing);
  return e;
}

const char* CombatLog::GetEventName(CombatLogEventType type) {
  switch (type) {
    case CombatLogEventType::SWING_DAMAGE:             return "SWING_DAMAGE";
    case CombatLogEventType::SWING_MISSED:             return "SWING_MISSED";
    case CombatLogEventType::RANGE_DAMAGE:             return "RANGE_DAMAGE";
    case CombatLogEventType::RANGE_MISSED:             return "RANGE_MISSED";
    case CombatLogEventType::SPELL_DAMAGE:             return "SPELL_DAMAGE";
    case CombatLogEventType::SPELL_MISSED:             return "SPELL_MISSED";
    case CombatLogEventType::SPELL_HEAL:               return "SPELL_HEAL";
    case CombatLogEventType::SPELL_ENERGIZE:           return "SPELL_ENERGIZE";
    case CombatLogEventType::SPELL_DRAIN:              return "SPELL_DRAIN";
    case CombatLogEventType::SPELL_LEECH:              return "SPELL_LEECH";
    case CombatLogEventType::SPELL_PERIODIC_DAMAGE:    return "SPELL_PERIODIC_DAMAGE";
    case CombatLogEventType::SPELL_PERIODIC_HEAL:      return "SPELL_PERIODIC_HEAL";
    case CombatLogEventType::SPELL_PERIODIC_ENERGIZE:  return "SPELL_PERIODIC_ENERGIZE";
    case CombatLogEventType::SPELL_PERIODIC_DRAIN:     return "SPELL_PERIODIC_DRAIN";
    case CombatLogEventType::SPELL_PERIODIC_LEECH:     return "SPELL_PERIODIC_LEECH";
    case CombatLogEventType::SPELL_PERIODIC_MISSED:    return "SPELL_PERIODIC_MISSED";
    case CombatLogEventType::SPELL_AURA_APPLIED:       return "SPELL_AURA_APPLIED";
    case CombatLogEventType::SPELL_AURA_REMOVED:       return "SPELL_AURA_REMOVED";
    case CombatLogEventType::SPELL_AURA_APPLIED_DOSE:  return "SPELL_AURA_APPLIED_DOSE";
    case CombatLogEventType::SPELL_AURA_REMOVED_DOSE:  return "SPELL_AURA_REMOVED_DOSE";
    case CombatLogEventType::SPELL_AURA_REFRESH:       return "SPELL_AURA_REFRESH";
    case CombatLogEventType::SPELL_AURA_BROKEN:        return "SPELL_AURA_BROKEN";
    case CombatLogEventType::SPELL_AURA_BROKEN_SPELL:  return "SPELL_AURA_BROKEN_SPELL";
    case CombatLogEventType::SPELL_CAST_START:         return "SPELL_CAST_START";
    case CombatLogEventType::SPELL_CAST_SUCCESS:       return "SPELL_CAST_SUCCESS";
    case CombatLogEventType::SPELL_CAST_FAILED:        return "SPELL_CAST_FAILED";
    case CombatLogEventType::SPELL_INTERRUPT:          return "SPELL_INTERRUPT";
    case CombatLogEventType::SPELL_DISPEL:             return "SPELL_DISPEL";
    case CombatLogEventType::SPELL_STOLEN:             return "SPELL_STOLEN";
    case CombatLogEventType::SPELL_EXTRA_ATTACKS:      return "SPELL_EXTRA_ATTACKS";
    case CombatLogEventType::SPELL_INSTAKILL:          return "SPELL_INSTAKILL";
    case CombatLogEventType::SPELL_DURABILITY_DAMAGE:  return "SPELL_DURABILITY_DAMAGE";
    case CombatLogEventType::SPELL_CREATE:             return "SPELL_CREATE";
    case CombatLogEventType::SPELL_SUMMON:             return "SPELL_SUMMON";
    case CombatLogEventType::SPELL_RESURRECT:          return "SPELL_RESURRECT";
    case CombatLogEventType::DAMAGE_SHIELD:            return "DAMAGE_SHIELD";
    case CombatLogEventType::DAMAGE_SHIELD_MISSED:     return "DAMAGE_SHIELD_MISSED";
    case CombatLogEventType::DAMAGE_SPLIT:             return "DAMAGE_SPLIT";
    case CombatLogEventType::PARTY_KILL:               return "PARTY_KILL";
    case CombatLogEventType::UNIT_DIED:                return "UNIT_DIED";
    case CombatLogEventType::UNIT_DESTROYED:           return "UNIT_DESTROYED";
    case CombatLogEventType::ENVIRONMENTAL_DAMAGE:     return "ENVIRONMENTAL_DAMAGE";
    case CombatLogEventType::SPELL_DURABILITY_DAMAGE_ALL: return "SPELL_DURABILITY_DAMAGE_ALL";
    case CombatLogEventType::DAMAGE_AURA_BROKEN:       return "DAMAGE_AURA_BROKEN";
    case CombatLogEventType::ENCHANT_APPLIED:          return "ENCHANT_APPLIED";
    case CombatLogEventType::ENCHANT_REMOVED:          return "ENCHANT_REMOVED";
    case CombatLogEventType::SPELL_DISPEL_FAILED:      return "SPELL_DISPEL_FAILED";
    case CombatLogEventType::SPELL_BUILDING_DAMAGE:    return "SPELL_BUILDING_DAMAGE";
    case CombatLogEventType::SPELL_BUILDING_HEAL:      return "SPELL_BUILDING_HEAL";
    case CombatLogEventType::UNIT_DISSIPATES:          return "UNIT_DISSIPATES";

    case CombatLogEventType::INVALID:
      break;
  }
  return "UNKNOWN";
}

int CombatLog::PushEntryToLua(lua_State* L, const CombatLogEntry& e) {
  if (L == nullptr) {
    return 0;
  }

  const auto push_optional_string = [L](const std::string& value) {
    if (value.empty()) {
      lua_pushnil(L);
    } else {
      lua_pushlstring(L, value.data(), value.size());
    }
  };
  const auto push_truth = [L](const bool value) {
    if (value) {

      lua_pushnumber(L, 1.0);
    } else {
      lua_pushnil(L);
    }
  };

  char src_guid_str[32];
  char dst_guid_str[32];
  std::snprintf(src_guid_str, sizeof(src_guid_str), "0x%016llX",
                static_cast<unsigned long long>(e.source_guid));
  std::snprintf(dst_guid_str, sizeof(dst_guid_str), "0x%016llX",
                static_cast<unsigned long long>(e.dest_guid));

  lua_pushnumber(L, e.timestamp);
  lua_pushstring(L, GetEventName(e.type));
  lua_pushstring(L, src_guid_str);
  push_optional_string(e.source_name);
  lua_pushnumber(L, static_cast<lua_Number>(e.source_flags));
  lua_pushstring(L, dst_guid_str);
  push_optional_string(e.dest_name);
  lua_pushnumber(L, static_cast<lua_Number>(e.dest_flags));

  int count = 8;

  if (e.spell_id != 0) {
    lua_pushnumber(L, static_cast<lua_Number>(e.spell_id));
    push_optional_string(e.spell_name);
    lua_pushnumber(L, static_cast<lua_Number>(e.spell_school));
    count += 3;
  }

  const auto suffix_flags = e.suffix_flags != 0
                                ? e.suffix_flags
                                : CombatLog_DefaultSuffixFlags(e);
  const auto push_aura_type = [L, &e] {
    lua_pushstring(L, e.aura_type == "DEBUFF" ? "DEBUFF" : "BUFF");
  };

  if ((suffix_flags & CombatLogSuffixFlag::kAmount) != 0u) {
    lua_pushnumber(L, static_cast<lua_Number>(e.amount));
    ++count;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kString) != 0u) {
    push_optional_string(e.failed_message);
    ++count;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kMissType) != 0u) {
    push_optional_string(e.miss_type);
    ++count;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kEnvironmentalType) != 0u) {
    push_optional_string(e.env_type);
    ++count;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kDamage) != 0u) {
    lua_pushnumber(L, static_cast<lua_Number>(e.amount));
    lua_pushnumber(L, static_cast<lua_Number>(e.overkill));
    lua_pushnumber(L, static_cast<lua_Number>(e.school));
    if (e.resisted != 0) lua_pushnumber(L, static_cast<lua_Number>(e.resisted));
    else lua_pushnil(L);
    if (e.blocked != 0) lua_pushnumber(L, static_cast<lua_Number>(e.blocked));
    else lua_pushnil(L);
    if (e.absorbed != 0) lua_pushnumber(L, static_cast<lua_Number>(e.absorbed));
    else lua_pushnil(L);
    push_truth(e.critical);
    push_truth(e.glancing);
    push_truth(e.crushing);
    count += 9;
  }

  if ((suffix_flags & CombatLogSuffixFlag::kAbsorbed) != 0u) {
    lua_pushnumber(L, static_cast<lua_Number>(e.absorbed));
    ++count;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kResisted) != 0u) {
    lua_pushnumber(L, static_cast<lua_Number>(e.resisted));
    ++count;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kBlocked) != 0u) {
    lua_pushnumber(L, static_cast<lua_Number>(e.blocked));
    ++count;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kHeal) != 0u) {
    lua_pushnumber(L, static_cast<lua_Number>(e.amount));
    lua_pushnumber(L, static_cast<lua_Number>(e.overheal));
    lua_pushnumber(L, static_cast<lua_Number>(e.absorbed));
    push_truth(e.critical);
    count += 4;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kEnergize) != 0u) {
    lua_pushnumber(L, static_cast<lua_Number>(e.power_amount));
    lua_pushnumber(L, static_cast<lua_Number>(e.power_type));
    count += 2;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kDrain) != 0u) {
    lua_pushnumber(L, static_cast<lua_Number>(e.power_amount));
    lua_pushnumber(L, static_cast<lua_Number>(e.power_type));
    if (e.energize_amount != 0) {
      lua_pushnumber(L, static_cast<lua_Number>(e.energize_amount));
    } else {
      lua_pushnil(L);
    }
    count += 3;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kExtraSpell) != 0u) {
    lua_pushnumber(L, static_cast<lua_Number>(e.extra_spell_id));
    push_optional_string(e.extra_spell_name);
    lua_pushnumber(L, static_cast<lua_Number>(e.extra_spell_school));
    count += 3;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kStringAtOffset58) != 0u) {
    push_optional_string(e.aura_type);
    ++count;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kNumberAndString) != 0u) {
    if (e.type == CombatLogEventType::SPELL_DURABILITY_DAMAGE) {
      lua_pushnumber(L, static_cast<lua_Number>(e.extra_spell_id));
      push_optional_string(e.extra_spell_name);
    } else {
      lua_pushnumber(L, static_cast<lua_Number>(e.aura_amount));
      push_optional_string(e.aura_type);
    }
    count += 2;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kAuraType) != 0u) {
    push_aura_type();
    ++count;
  }
  if ((suffix_flags & CombatLogSuffixFlag::kNumber) != 0u) {
    lua_pushnumber(L, static_cast<lua_Number>(e.aura_amount));
    ++count;
  }

  if (CombatLog_EventHasEnchantNameSuffix(e.type)) {
    push_optional_string(e.enchant_name);
    lua_pushnumber(L, static_cast<lua_Number>(e.enchant_item_id));
    push_optional_string(e.enchant_item_name);
    count += 3;
  }

  return count;
}

int CombatLog::PushCurrentEntryToLua(lua_State* L) const {
  const CombatLogEntry* const entry = current_entry();
  return entry != nullptr ? PushEntryToLua(L, *entry) : 0;
}

bool CombatLog::HandleAttackStart(const std::uint8_t* data, std::size_t len) {
  PacketReader r(data, len);
  CombatEvent evt;
  evt.type = CombatEventType::kAttackStart;
  if (!r.ReadGuid(evt.source) || !r.ReadGuid(evt.target)) return false;
  AddEvent(std::move(evt));
  return true;
}

bool CombatLog::HandleAttackStop(const std::uint8_t* data, std::size_t len) {
  PacketReader r(data, len);
  CombatEvent evt;
  evt.type = CombatEventType::kAttackStop;
  if (!r.ReadPackedGuid(evt.source)) return false;
  if (r.Remaining() > 4) {
    if (!r.ReadPackedGuid(evt.target)) return false;
  }
  AddEvent(std::move(evt));
  return true;
}

bool CombatLog::HandleSpellNonMeleeDamageLog(const std::uint8_t* data,
                                             std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellNonMeleeDamageLog(r);
}

bool CombatLog::HandleSpellNonMeleeDamageLog(PacketReader& r,
                                             const std::uint32_t timestamp_offset_ms) {
  CombatEvent evt;
  evt.type = CombatEventType::kSpellDamage;
  if (!r.ReadPackedGuid(evt.target) || !r.ReadPackedGuid(evt.source))
    return false;
  if (!r.ReadU32(evt.spell_id) || !r.ReadU32(evt.amount) ||
      !r.ReadU32(evt.overkill))
    return false;
  if (!r.ReadU8(evt.school_mask)) return false;
  if (!r.ReadU32(evt.absorb) || !r.ReadU32(evt.resist)) return false;

  std::uint8_t physical_log, unused;
  if (!r.ReadU8(physical_log) || !r.ReadU8(unused)) return false;
  if (!r.ReadU32(evt.blocked)) return false;

  std::uint32_t spell_hit_type;
  if (!r.ReadU32(spell_hit_type)) return false;
  evt.critical = (spell_hit_type & SpellHitType::kCrit) != 0;

  std::uint8_t has_ext;
  if (!r.ReadU8(has_ext)) return false;

  if (has_ext != 0) {
    float discard;
    if ((spell_hit_type & SpellHitType::kSwingExt) != 0) {
      (void)r.ReadFloat(discard);
      (void)r.ReadFloat(discard);
    }
    if ((spell_hit_type & SpellHitType::kRangedExt) != 0) {
      (void)r.ReadFloat(discard);
      (void)r.ReadFloat(discard);
    }
    if ((spell_hit_type & SpellHitType::kFullExt) != 0) {
      (void)r.ReadFloat(discard);
      (void)r.ReadFloat(discard);
      (void)r.ReadFloat(discard);
      (void)r.ReadFloat(discard);
      (void)r.ReadFloat(discard);
      (void)r.ReadFloat(discard);
    }
  }

  if (evt.amount == 0) {
    if (evt.absorb != 0)
      evt.miss_type = MissType::Absorb;
    else if (evt.blocked != 0)
      evt.miss_type = MissType::Block;
    else if (evt.resist != 0)
      evt.miss_type = MissType::Resist;
    if (evt.absorb != 0 || evt.blocked != 0 || evt.resist != 0)
      evt.type = CombatEventType::kSpellMiss;
  }
  AddEvent(CombatEvent(evt));

  const auto spell = LookupCombatLogSpellData(evt.spell_id);
  const bool can_create_entry =
      spell.has_value() && CanCreateCombatLogSpellEntry(*spell);

  if ((spell_hit_type & SpellHitType::kSplit) != 0) {

    CombatLog_ProcessSplitDamage(
        *this,
        evt.source.GetRawValue(),
        evt.target.GetRawValue(),
        evt.spell_id,
        static_cast<std::int32_t>(evt.amount),
        static_cast<std::int32_t>(evt.overkill),
        static_cast<std::int32_t>(evt.school_mask),
        static_cast<std::int32_t>(evt.absorb),
        static_cast<std::int32_t>(evt.resist),
        static_cast<std::int32_t>(evt.blocked),
        evt.critical,
        timestamp_offset_ms);

    if (can_create_entry) {
      DispatchWoundEvent(
          evt.target,
          static_cast<int>(evt.amount),
          static_cast<int>(evt.school_mask),
          BuildUnitCombatHitFlags(
              evt.critical, evt.absorb != 0, evt.resist != 0,
              evt.blocked != 0));
    }
  } else if (evt.amount != 0 && can_create_entry) {
    CombatLogEntry le = CombatLog_CreateEntry(
        evt.source.GetRawValue(), evt.target.GetRawValue(), 9,
        spell->spell_id, spell->name);
    le.type = CombatLogEventType::SPELL_DAMAGE;
    le.spell_school = spell->school;
    le.amount = static_cast<std::int32_t>(evt.amount);
    le.overkill = static_cast<std::int32_t>(evt.overkill);
    le.school = evt.school_mask;
    le.absorbed = static_cast<std::int32_t>(evt.absorb);
    le.resisted = static_cast<std::int32_t>(evt.resist);
    le.blocked = static_cast<std::int32_t>(evt.blocked);
    le.critical = evt.critical;
    le.timestamp = TimestampWithOffsetMs(timestamp_offset_ms);
    AddLogEntry(std::move(le));
    DispatchWoundEvent(
        evt.target,
        static_cast<int>(evt.amount),
        static_cast<int>(evt.school_mask),
        BuildUnitCombatHitFlags(
            evt.critical, evt.absorb != 0, evt.resist != 0,
            evt.blocked != 0));
  } else if ((evt.absorb != 0 || evt.blocked != 0 || evt.resist != 0) &&
             can_create_entry) {
    const char* miss_str = "ABSORB";
    if (evt.absorb != 0)
      miss_str = "ABSORB";
    else if (evt.blocked != 0)
      miss_str = "BLOCK";
    else
      miss_str = "RESIST";

    CombatLogEntry le = BuildSpellMissEntry(
        *spell, evt.source.GetRawValue(), evt.target.GetRawValue(),
        CombatLogEventType::SPELL_MISSED,
        miss_str);
    le.timestamp = TimestampWithOffsetMs(timestamp_offset_ms);
    le.absorbed = static_cast<std::int32_t>(evt.absorb);
    le.resisted = static_cast<std::int32_t>(evt.resist);
    le.blocked = static_cast<std::int32_t>(evt.blocked);
    AddLogEntry(std::move(le));
  }
  return true;
}

bool CombatLog::HandleSpellHealLog(ObjectManager& objects,
                                   const std::uint8_t* data,
                                   std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellHealLog(objects, r);
}

bool CombatLog::HandleSpellHealLog(ObjectManager& objects,
                                   PacketReader& r,
                                   const std::uint32_t timestamp_offset_ms) {
  CombatEvent evt;
  evt.type = CombatEventType::kSpellHeal;
  if (!r.ReadPackedGuid(evt.target) || !r.ReadPackedGuid(evt.source))
    return false;
  if (!r.ReadU32(evt.spell_id) || !r.ReadU32(evt.amount)) return false;

  std::uint32_t overheal;
  if (!r.ReadU32(overheal) || !r.ReadU32(evt.absorb)) return false;
  evt.overkill = overheal;

  std::uint8_t crit, unused;
  if (!r.ReadU8(crit) || !r.ReadU8(unused)) return false;
  evt.critical = crit != 0;

  AddEvent(CombatEvent(evt));

  if (evt.amount == 0 && overheal == 0 && evt.absorb == 0) {
    return true;
  }
  const auto spell = LookupCombatLogSpellData(evt.spell_id);
  if (!spell.has_value() || !CanCreateCombatLogSpellEntry(*spell)) {
    return true;
  }

  CombatLogEntry le = CombatLog_CreateEntry(
      evt.source.GetRawValue(), evt.target.GetRawValue(), 10,
      spell->spell_id, spell->name);
  le.type = CombatLogEventType::SPELL_HEAL;
  le.spell_school = spell->school;
  le.amount = static_cast<std::int32_t>(evt.amount);
  le.absorbed = static_cast<std::int32_t>(evt.absorb);
  le.critical = evt.critical;
  le.timestamp = TimestampWithOffsetMs(timestamp_offset_ms);
  le.overheal = static_cast<std::int32_t>(overheal);
  AddLogEntry(std::move(le));
  DispatchHealEvent(evt.target,
                    static_cast<int>(evt.amount),
                    static_cast<int>(evt.absorb),
                    evt.critical);

  CombatLog_ProcessSpellHealDisplay(
      objects,
      evt.source.GetRawValue(),
      evt.target.GetRawValue(),
      evt.spell_id,
      static_cast<std::int32_t>(evt.amount),
      static_cast<std::int32_t>(overheal),
      static_cast<std::int32_t>(evt.absorb),
      evt.critical,
      false,
      timestamp_offset_ms);
  return true;
}

bool CombatLog::HandleSpellEnergizeLog(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellEnergizeLog(r);
}

bool CombatLog::HandleSpellEnergizeLog(PacketReader& r,
                                       const std::uint32_t timestamp_offset_ms) {
  CombatEvent evt;
  evt.type = CombatEventType::kSpellEnergize;
  if (!r.ReadPackedGuid(evt.target) || !r.ReadPackedGuid(evt.source))
    return false;
  if (!r.ReadU32(evt.spell_id) || !r.ReadU32(evt.power_type) ||
      !r.ReadU32(evt.amount))
    return false;

  AddEvent(CombatEvent(evt));

  if (evt.amount == 0) {
    return true;
  }
  const auto spell = LookupCombatLogSpellData(evt.spell_id);
  if (!spell.has_value() || !CanCreateCombatLogSpellEntry(*spell)) {
    return true;
  }

  CombatLog_CreateEnergizeEntry(
      *this,
      evt.source.GetRawValue(),
      evt.power_type,
      false,
      evt.target.GetRawValue(),
      evt.spell_id,
      spell->name,
      static_cast<std::int32_t>(evt.amount),
      timestamp_offset_ms);
  return true;
}

void CombatLog::HandleSpellPowerDrain(
    ObjectManager& objects,
    const std::uint64_t caster_guid,
    const std::uint64_t target_guid,
    const std::uint32_t spell_id,
    const std::uint32_t power_type,
    const std::uint32_t drain_amount,
    const float leech_coefficient,
    const bool is_periodic,
    const std::uint32_t timestamp_offset_ms) {
  if (drain_amount == 0) return;

  if (power_type == 4) {
    const std::uint32_t divisor = PowerType_GetDisplayValueDivisor(4);
    const std::int32_t display_amount =
        static_cast<std::int32_t>(drain_amount / divisor);

    const auto source_name = CombatLog_BuildNameForGUID(caster_guid);
    const auto target_name = CombatLog_BuildNameForGUID(target_guid);
    const bool is_self =
        CombatLog_IsActivePlayerTarget(caster_guid);

    DisplayHappinessDrain(objects, caster_guid, target_guid, display_amount,
                          source_name, target_name, is_self);
    return;
  }

  if (auto* unit = objects.GetMutableUnit(
          ObjectGuid(target_guid));
      unit != nullptr) {
    (void)unit->Vitals().ModifyDisplayedPower(
        *unit, static_cast<std::uint8_t>(power_type),
        -static_cast<std::int32_t>(drain_amount));
  }

  const auto spell = LookupCombatLogSpellData(spell_id);
  if (!spell.has_value() || !CanCreateCombatLogSpellEntry(*spell)) return;

  const auto drain_info = ComputeDrainEventInfo(
      drain_amount, leech_coefficient, is_periodic);

  CombatLog_CreateDrainEntry(
      *this,
      target_guid,
      drain_info.event_type,
      caster_guid,
      spell_id,
      spell->name,
      drain_amount,
      power_type,
      drain_info.energize_amount,
      timestamp_offset_ms);
}

bool CombatLog::HandleLogXpGain(const std::uint8_t* data, std::size_t len) {
  PacketReader r(data, len);
  CombatEvent evt;
  evt.type = CombatEventType::kXpGain;
  if (!r.ReadGuid(evt.source)) return false;
  if (!r.ReadU32(evt.amount)) return false;
  if (!r.ReadU8(evt.xp_type)) return false;

  if (evt.xp_type == 0) {
    std::uint32_t base_xp;
    if (!r.ReadU32(base_xp) || !r.ReadFloat(evt.group_rate)) return false;
  }

  std::uint8_t is_raf;
  if (r.HasBytes(1)) (void)r.ReadU8(is_raf);

  AddEvent(std::move(evt));

  return true;
}

bool CombatLog::HandleAttackerStateUpdate(const std::uint8_t* data,
                                          std::size_t len) {
  CombatManager parser;
  PacketReader reader(data, len);
  if (!parser.HandleAttackerStateUpdate(reader) ||
      !parser.last_state_update().has_value()) {
    return false;
  }

  return HandleAttackerStateUpdate(*parser.last_state_update());
}

bool CombatLog::HandleAttackerStateUpdate(
    const AttackerStateUpdate& update,
    const std::uint32_t timestamp_offset_ms) {
  const auto has_hit_info = [&](const std::uint32_t flag) {
    return (update.hit_info & flag) != 0u;
  };

  const char* miss_type = nullptr;
  MissType compact_miss_type = MissType::Miss;
  if (has_hit_info(HitInfoFlag::kMiss)) {
    miss_type = "MISS";
  } else if (has_hit_info(HitInfoFlag::kFullAbsorb)) {
    miss_type = "ABSORB";
    compact_miss_type = MissType::Absorb;
  } else if (has_hit_info(HitInfoFlag::kFullResist)) {
    miss_type = "RESIST";
    compact_miss_type = MissType::Resist;
  } else {
    switch (update.victim_state) {
      case VictimState::kDodge:
        miss_type = "DODGE";
        compact_miss_type = MissType::Dodge;
        break;
      case VictimState::kParry:
        miss_type = "PARRY";
        compact_miss_type = MissType::Parry;
        break;
      case VictimState::kBlock:
        miss_type = "BLOCK";
        compact_miss_type = MissType::Block;
        break;
      case VictimState::kEvade:
        miss_type = "EVADE";
        compact_miss_type = MissType::Evade;
        break;
      case VictimState::kImmune:
        miss_type = "IMMUNE";
        compact_miss_type = MissType::Immune;
        break;
      case VictimState::kDeflect:
        miss_type = "DEFLECT";
        compact_miss_type = MissType::Deflect;
        break;
      default:

        break;
    }
  }

  CombatEvent evt;

  evt.type = miss_type != nullptr && update.melee_spell_id == 0
                 ? CombatEventType::kSpellMiss
                 : CombatEventType::kMeleeAttack;

  evt.hit_info = static_cast<HitInfo>(update.hit_info);
  evt.source = update.attacker;
  evt.target = update.victim;
  evt.spell_id = update.melee_spell_id;
  evt.amount = update.total_damage;
  evt.overkill = update.overkill;
  evt.blocked = update.blocked_amount;
  evt.critical = (update.hit_info & HitInfoFlag::kCriticalHit) != 0u;

  std::uint32_t school_mask = 0;
  std::uint32_t absorbed_total = 0;
  std::uint32_t resisted_total = 0;
  for (const auto& sub_damage : update.sub_damages) {
    school_mask |= sub_damage.school_mask;
    absorbed_total += sub_damage.absorbed;
    resisted_total += sub_damage.resisted;
  }
  evt.absorb = absorbed_total;
  evt.resist = resisted_total;
  evt.school_mask = static_cast<std::uint8_t>(school_mask & 0xFFu);
  evt.miss_type = compact_miss_type;
  AddEvent(std::move(evt));

  if (update.melee_spell_id != 0 &&
      update.victim_state != VictimState::kHit) {
    return true;
  }

  if (miss_type != nullptr) {

    switch (compact_miss_type) {
      case MissType::Absorb:
        DispatchHitIndicator(update.victim, "WOUND", 0,
                             static_cast<int>(school_mask),
                             BuildUnitCombatHitFlags(false, true, false, false));
        break;
      case MissType::Resist:
        DispatchHitIndicator(update.victim, "WOUND", 0,
                             static_cast<int>(school_mask),
                             BuildUnitCombatHitFlags(false, false, true, false));
        break;
      case MissType::Miss:
        DispatchIndexedUnitCombatEvent(update.victim, IndexedUnitCombatSchool::kMiss, 0, 0, 0);
        break;
      case MissType::Dodge:
        DispatchIndexedUnitCombatEvent(update.victim, IndexedUnitCombatSchool::kDodge, 0, 0, 0);
        break;
      case MissType::Parry:
        DispatchIndexedUnitCombatEvent(update.victim, IndexedUnitCombatSchool::kParry, 0, 0, 0);
        break;
      case MissType::Block:
        DispatchIndexedUnitCombatEvent(update.victim, IndexedUnitCombatSchool::kBlock, 0, 0, 0);
        break;
      case MissType::Evade:
        DispatchIndexedUnitCombatEvent(update.victim, IndexedUnitCombatSchool::kEvade, 0, 0, 0);
        break;
      case MissType::Immune:
        DispatchIndexedUnitCombatEvent(update.victim, IndexedUnitCombatSchool::kImmune, 0, 0, 0);
        break;
      case MissType::Deflect:
        DispatchIndexedUnitCombatEvent(update.victim, IndexedUnitCombatSchool::kDeflect, 0, 0, 0);
        break;
      case MissType::Reflect:

        break;
    }

    CombatLogEntry entry = CombatLog_CreateEntry(
        update.attacker.GetRawValue(), update.victim.GetRawValue(), 2);
    entry.type = CombatLogEventType::SWING_MISSED;
    entry.miss_type = miss_type;
    entry.absorbed = static_cast<std::int32_t>(
        std::min<std::uint32_t>(
            absorbed_total,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())));
    entry.resisted = static_cast<std::int32_t>(
        std::min<std::uint32_t>(
            resisted_total,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())));
    entry.blocked = static_cast<std::int32_t>(
        std::min<std::uint32_t>(
            update.blocked_amount,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())));
    CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
    return true;
  }

  if ((update.victim_state == VictimState::kHit ||
       update.victim_state == VictimState::kEvade) &&
      update.total_damage != 0) {
    CombatLogEntry entry = CombatLog_CreateEntry(
        update.attacker.GetRawValue(), update.victim.GetRawValue(), 1);
    entry.type = CombatLogEventType::SWING_DAMAGE;
    entry.amount = static_cast<std::int32_t>(
        std::min<std::uint32_t>(
            update.total_damage,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())));
    entry.overkill = static_cast<std::int32_t>(
        std::min<std::uint32_t>(
            update.overkill,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())));
    entry.school = school_mask;
    entry.absorbed = static_cast<std::int32_t>(
        std::min<std::uint32_t>(
            absorbed_total,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())));
    entry.resisted = static_cast<std::int32_t>(
        std::min<std::uint32_t>(
            resisted_total,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())));
    entry.blocked = static_cast<std::int32_t>(
        std::min<std::uint32_t>(
            update.blocked_amount,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())));
    entry.critical = has_hit_info(HitInfoFlag::kCriticalHit);
    entry.glancing = has_hit_info(HitInfoFlag::kGlancing);
    entry.crushing = has_hit_info(HitInfoFlag::kCrushing);
    CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  }

  return true;
}

bool CombatLog::HandlePeriodicAuraLog(ObjectManager& objects,
                                      const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);
  return HandlePeriodicAuraLog(objects, r);
}

bool CombatLog::HandlePeriodicAuraLog(ObjectManager& objects,
                                      PacketReader& r,
                                      const std::uint32_t timestamp_offset_ms) {
  ObjectGuid target_guid, caster_guid;
  if (!r.ReadPackedGuid(target_guid) || !r.ReadPackedGuid(caster_guid))
    return false;

  std::uint32_t spell_id = 0;
  std::uint32_t count = 0;
  if (!r.ReadU32(spell_id) || !r.ReadU32(count))
    return false;

  const auto spell = LookupCombatLogSpellData(spell_id);
  const bool can_create_entry =
      spell.has_value() && CanCreateCombatLogSpellEntry(*spell);

  for (std::uint32_t record_index = 0; record_index < count;
       ++record_index) {
    std::uint32_t aura_type = 0;
    if (!r.ReadU32(aura_type)) return false;

    CombatEvent evt;
    evt.source = caster_guid;
    evt.target = target_guid;
    evt.spell_id = spell_id;

    CombatLogEventType log_type{};
    switch (aura_type) {
      case 3:
      case 89:
        evt.type = CombatEventType::kPeriodicDamage;
        if (!r.ReadU32(evt.amount) || !r.ReadU32(evt.overkill)) return false;
        {
          std::uint32_t school = 0;
          if (!r.ReadU32(school)) return false;
          evt.school_mask = static_cast<std::uint8_t>(school);
        }
        if (!r.ReadU32(evt.absorb) || !r.ReadU32(evt.resist)) return false;
        {
          std::uint8_t critical = 0;
          if (!r.ReadU8(critical)) return false;
          evt.critical = critical != 0;
        }
        log_type = CombatLogEventType::SPELL_PERIODIC_DAMAGE;
        break;

      case 8:
      case 20:
        evt.type = CombatEventType::kPeriodicHeal;
        if (!r.ReadU32(evt.amount) || !r.ReadU32(evt.overkill) ||
            !r.ReadU32(evt.absorb)) {
          return false;
        }
        {
          std::uint8_t critical = 0;
          if (!r.ReadU8(critical)) return false;
          evt.critical = critical != 0;
        }
        log_type = CombatLogEventType::SPELL_PERIODIC_HEAL;
        break;

      case 21:
      case 24:
        evt.type = CombatEventType::kPeriodicEnergize;
        if (!r.ReadU32(evt.power_type) || !r.ReadU32(evt.amount)) return false;
        log_type = CombatLogEventType::SPELL_PERIODIC_ENERGIZE;
        break;

      case 64: {
        evt.type = CombatEventType::kPeriodicEnergize;
        evt.power_drain = true;
        if (!r.ReadU32(evt.power_type) || !r.ReadU32(evt.amount)) return false;
        float multiplier = 0.0f;
        if (!r.ReadFloat(multiplier)) return false;
        AddEvent(CombatEvent(evt));
        HandleSpellPowerDrain(
            objects, caster_guid.GetRawValue(), target_guid.GetRawValue(),
            spell_id, evt.power_type, evt.amount, multiplier,
            true, timestamp_offset_ms);
        continue;
      }

      default:

        return true;
    }

    AddEvent(CombatEvent(evt));

    if (log_type == CombatLogEventType::SPELL_PERIODIC_ENERGIZE) {
      if (evt.amount != 0 && can_create_entry) {
        CombatLog_CreateEnergizeEntry(
            *this, caster_guid.GetRawValue(), evt.power_type,
            true, target_guid.GetRawValue(), spell_id,
            spell->name, static_cast<std::int32_t>(evt.amount),
            timestamp_offset_ms);
      }
      continue;
    }

    if (log_type == CombatLogEventType::SPELL_PERIODIC_HEAL &&
        evt.amount == 0 && evt.overkill == 0 && evt.absorb == 0) {
      continue;
    }
    if (log_type == CombatLogEventType::SPELL_PERIODIC_DAMAGE &&
        evt.amount == 0 && evt.absorb == 0 && evt.resist == 0) {
      continue;
    }
    if (!can_create_entry) continue;

    if (log_type == CombatLogEventType::SPELL_PERIODIC_DAMAGE &&
        evt.amount == 0) {
      const char* const miss_type =
          evt.absorb != 0 ? "ABSORB" : "RESIST";
      CombatLogEntry miss = BuildSpellMissEntry(
          *spell, caster_guid.GetRawValue(), target_guid.GetRawValue(),
          CombatLogEventType::SPELL_PERIODIC_MISSED, miss_type);
      miss.absorbed = static_cast<std::int32_t>(evt.absorb);
      miss.resisted = static_cast<std::int32_t>(evt.resist);
      miss.timestamp = TimestampWithOffsetMs(timestamp_offset_ms);
      AddLogEntry(std::move(miss));
      continue;
    }

    CombatLogEntry entry = CombatLog_CreateEntry(
        caster_guid.GetRawValue(), target_guid.GetRawValue(),
        log_type == CombatLogEventType::SPELL_PERIODIC_DAMAGE ? 34u : 35u,
        spell->spell_id, spell->name);
    entry.type = log_type;
    entry.spell_school = spell->school;
    entry.amount = static_cast<std::int32_t>(evt.amount);
    entry.school = evt.school_mask;
    entry.absorbed = static_cast<std::int32_t>(evt.absorb);
    entry.resisted = static_cast<std::int32_t>(evt.resist);
    entry.critical = evt.critical;
    entry.timestamp = TimestampWithOffsetMs(timestamp_offset_ms);
    if (log_type == CombatLogEventType::SPELL_PERIODIC_HEAL) {
      entry.overheal = static_cast<std::int32_t>(evt.overkill);
    }
    AddLogEntry(std::move(entry));

    if (log_type == CombatLogEventType::SPELL_PERIODIC_DAMAGE) {
      DispatchWoundEvent(
          evt.target, static_cast<int>(evt.amount),
          static_cast<int>(evt.school_mask),
          BuildUnitCombatHitFlags(evt.critical, evt.absorb != 0,
                                  evt.resist != 0, false));
    } else {
      DispatchHealEvent(evt.target, static_cast<int>(evt.amount),
                        static_cast<int>(evt.absorb), evt.critical);
      CombatLog_ProcessSpellHealDisplay(
          objects, caster_guid.GetRawValue(), target_guid.GetRawValue(),
          spell_id, static_cast<std::int32_t>(evt.amount),
          static_cast<std::int32_t>(evt.overkill),
          static_cast<std::int32_t>(evt.absorb), evt.critical,
          true, timestamp_offset_ms);
    }
  }
  return true;
}

bool CombatLog::HandleSpellDispelOrSteal(const SpellDispelLog& log) {
  return HandleSpellDispelOrSteal(log, 0);
}

bool CombatLog::HandleSpellDispelOrSteal(const SpellDispelLog& log,
                                         const std::uint32_t timestamp_offset_ms) {
  const auto main_spell = LookupCombatLogSpellData(log.spell_id);
  if (!main_spell.has_value() || !CanCreateCombatLogSpellEntry(*main_spell)) {
    return false;
  }

  bool added_entry = false;
  const auto event_type = log.is_steal ? CombatLogEventType::SPELL_STOLEN
                                       : CombatLogEventType::SPELL_DISPEL;

  constexpr std::uint32_t kSpellDispelEventIndex = 26u;
  constexpr std::uint32_t kSpellStolenEventIndex = 27u;
  const auto event_index =
      log.is_steal ? kSpellStolenEventIndex : kSpellDispelEventIndex;

  for (const DispelEntry& dispelled_aura : log.entries) {
    const auto removed_spell = LookupCombatLogSpellData(dispelled_aura.spell_id);
    if (!removed_spell.has_value() ||
        !CanCreateCombatLogSpellEntry(*removed_spell)) {
      continue;
    }

    CombatLogEntry entry = CombatLog_CreateEntry(
        log.caster.GetRawValue(), log.victim.GetRawValue(), event_index,
        main_spell->spell_id, main_spell->name);
    entry.type = event_type;
    entry.spell_school = main_spell->school;
    PopulateExtraSpellSuffix(entry, *removed_spell);

    entry.aura_type =
        dispelled_aura.is_cleansed != 0u ? "DEBUFF" : "BUFF";
    CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
    FireLocalSpellDispelledText(log.victim.GetRawValue(), removed_spell->name);
    added_entry = true;
  }

  return added_entry;
}

bool CombatLog::HandleSpellAuraBroken(const SpellBreakLog& log) {
  return HandleSpellAuraBroken(log, 0);
}

bool CombatLog::HandleSpellAuraBroken(const SpellBreakLog& log,
                                      const std::uint32_t timestamp_offset_ms) {
  bool added_entry = false;

  for (const BrokenAuraEntry& broken_aura : log.broken_auras) {
    const auto aura_spell = LookupCombatLogSpellData(broken_aura.spell_id);
    if (!aura_spell.has_value() ||
        !CanCreateCombatLogSpellEntry(*aura_spell)) {
      continue;
    }

    std::optional<CombatLogSpellData> breaking_spell;
    const auto event_type =
        (log.breaking_spell_id != 0) ? CombatLogEventType::SPELL_AURA_BROKEN_SPELL
                                     : CombatLogEventType::SPELL_AURA_BROKEN;
    const auto event_index = (event_type == CombatLogEventType::SPELL_AURA_BROKEN_SPELL)
                                 ? 29u
                                 : 28u;
    if (log.breaking_spell_id != 0) {
      breaking_spell = LookupCombatLogSpellData(log.breaking_spell_id);
      if (!breaking_spell.has_value() ||
          !CanCreateCombatLogSpellEntry(*breaking_spell)) {
        continue;
      }
    }

    CombatLogEntry entry = CombatLog_CreateEntry(
        log.caster, log.victim, event_index, aura_spell->spell_id, aura_spell->name);
    entry.type = event_type;
    entry.spell_school = aura_spell->school;
    entry.aura_type = broken_aura.is_debuff ? "DEBUFF" : "BUFF";
    if (breaking_spell.has_value()) {
      PopulateExtraSpellSuffix(entry, *breaking_spell);
    }
    CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
    FireLocalSpellDispelledText(log.victim, aura_spell->name);
    added_entry = true;
  }

  return added_entry;
}

bool CombatLog::HandleSpellLogExecuteResurrect(
    const SpellLogExecuteResurrect& log) {
  return HandleSpellLogExecuteResurrect(log, 0);
}

bool CombatLog::HandleSpellLogExecuteResurrect(
    const SpellLogExecuteResurrect& log,
    const std::uint32_t timestamp_offset_ms) {
  const auto spell = LookupCombatLogSpellData(log.spell_id);
  if (!spell.has_value() || !CanCreateSpellLogExecuteResurrectEntry(*spell)) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      log.caster_guid, log.target_guid, 46, spell->spell_id, spell->name);
  entry.type = CombatLogEventType::SPELL_RESURRECT;
  entry.spell_school = spell->school;
  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  return true;
}

bool CombatLog::HandleSpellLogExecuteExtraAttacks(
    const SpellLogExecuteExtraAttacks& log,
    const std::uint32_t timestamp_offset_ms) {
  const auto spell = LookupCombatLogSpellData(log.spell_id);
  if (!spell.has_value() || !CanCreateCombatLogSpellEntry(*spell) ||
      log.amount == 0) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      log.caster_guid, log.target_guid, 18, spell->spell_id, spell->name);
  entry.type = CombatLogEventType::SPELL_EXTRA_ATTACKS;
  entry.spell_school = spell->school;
  entry.amount = static_cast<std::int32_t>(
      std::min<std::uint32_t>(log.amount,
                              static_cast<std::uint32_t>(
                                  std::numeric_limits<std::int32_t>::max())));
  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  return true;
}

bool CombatLog::HandleSpellLogExecuteInterrupt(
    const SpellLogExecuteInterrupt& log,
    const std::uint32_t timestamp_offset_ms) {
  const auto main_spell = LookupCombatLogSpellData(log.spell_id);
  const auto extra_spell = LookupCombatLogSpellData(log.extra_spell_id);
  if (!main_spell.has_value() || !extra_spell.has_value() ||
      !CanCreateCombatLogSpellEntry(*main_spell) ||
      !CanCreateCombatLogSpellEntry(*extra_spell)) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      log.caster_guid, log.target_guid, 17,
      main_spell->spell_id, main_spell->name);
  entry.type = CombatLogEventType::SPELL_INTERRUPT;
  entry.spell_school = main_spell->school;
  PopulateExtraSpellSuffix(entry, *extra_spell);
  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  return true;
}

bool CombatLog::HandleSpellLogExecuteSummon(
    const SpellLogExecuteSummon& log,
    const std::uint32_t timestamp_offset_ms) {
  const auto spell = LookupCombatLogSpellData(log.spell_id);
  if (!spell.has_value() || !CanCreateCombatLogSpellEntry(*spell)) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      log.caster_guid, log.target_guid, 15, spell->spell_id, spell->name);
  entry.type = CombatLogEventType::SPELL_SUMMON;
  entry.spell_school = spell->school;
  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  return true;
}

bool CombatLog::HandleSpellLogExecuteDurabilityDamage(
    const SpellLogExecuteDurabilityDamage& log) {
  return HandleSpellLogExecuteDurabilityDamage(log, 0);
}

bool CombatLog::HandleSpellLogExecuteDurabilityDamage(
    const SpellLogExecuteDurabilityDamage& log,
    const std::uint32_t timestamp_offset_ms) {
  const auto spell = LookupCombatLogSpellData(log.damage_spell_id);
  const auto trigger_spell = LookupCombatLogSpellData(log.trigger_spell_id);
  if (!spell.has_value() || !trigger_spell.has_value() ||
      !CanCreateCombatLogSpellEntry(*spell) ||
      !CanCreateCombatLogSpellEntry(*trigger_spell)) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      log.caster_guid, log.target_guid, 19u, spell->spell_id, spell->name);
  entry.type = CombatLogEventType::SPELL_DURABILITY_DAMAGE;
  entry.spell_school = spell->school;
  entry.extra_spell_id = log.trigger_spell_id;
  entry.extra_spell_name = trigger_spell->name;
  entry.extra_spell_school = trigger_spell->school;
  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  return true;
}

bool CombatLog::HandleSpellLogExecuteDurabilityDamageAll(
    const SpellLogExecuteDurabilityDamageAll& log) {
  return HandleSpellLogExecuteDurabilityDamageAll(log, 0);
}

bool CombatLog::HandleSpellLogExecuteDurabilityDamageAll(
    const SpellLogExecuteDurabilityDamageAll& log,
    const std::uint32_t timestamp_offset_ms) {
  const auto spell = LookupCombatLogSpellData(log.spell_id);
  if (!spell.has_value() || !CanCreateCombatLogSpellEntry(*spell)) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      log.caster_guid, log.target_guid, 20u, spell->spell_id, spell->name);
  entry.type = CombatLogEventType::SPELL_DURABILITY_DAMAGE_ALL;
  entry.spell_school = spell->school;
  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  return true;
}

bool CombatLog::HandleSpellLogMiss(const SpellLogMiss& log) {
  return HandleSpellLogMiss(log, 0);
}

bool CombatLog::HandleDispelFailed(
    const DispelFailed& log, const std::uint32_t timestamp_offset_ms) {
  const auto main_spell = LookupCombatLogSpellData(log.spell_id);
  if (!main_spell.has_value() || !CanCreateCombatLogSpellEntry(*main_spell)) {
    return false;
  }

  bool added_entry = false;

  for (const std::uint32_t failed_spell_id : log.failed_spells) {
    const auto failed_spell = LookupCombatLogSpellData(failed_spell_id);
    if (!failed_spell.has_value() ||
        !CanCreateCombatLogSpellEntry(*failed_spell)) {
      continue;
    }

    CombatLogEntry entry = CombatLog_CreateEntry(
        log.caster_guid, log.victim_guid, 39,
        main_spell->spell_id, main_spell->name);
    entry.type = CombatLogEventType::SPELL_DISPEL_FAILED;
    entry.spell_school = main_spell->school;

    PopulateExtraSpellSuffix(entry, *failed_spell);

    CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
    added_entry = true;
  }

  return added_entry;
}

bool CombatLog::HandleProcResist(
    const ProcResist& log, const std::uint32_t timestamp_offset_ms) {
  const auto spell = LookupCombatLogSpellData(log.spell_id);
  if (!spell.has_value() || !CanCreateSpellMissEntry(*spell)) {
    return false;
  }

  if (CombatText_IsActiveUnit(log.target)) {
    ui::game::ScriptEventDispatch::Get().FireCombatTextUpdate("SPELL_RESIST");
  }

  CombatEvent event;
  event.type = CombatEventType::kSpellMiss;
  event.source = ObjectGuid(log.caster);
  event.target = ObjectGuid(log.target);
  event.spell_id = spell->spell_id;
  event.miss_type = MissType::Resist;
  AddEvent(std::move(event));

  CombatLogEntry entry = BuildSpellMissEntry(
      *spell, log.caster, log.target, CombatLogEventType::SPELL_MISSED, "RESIST");
  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  return true;
}

bool CombatLog::HandleSpellLogMiss(const SpellLogMiss& log,
                                   const std::uint32_t timestamp_offset_ms) {
  if (!log.allow_client_miss_feedback) {
    return false;
  }

  const auto spell = LookupCombatLogSpellData(log.spell_id);
  if (!spell.has_value() || !CanCreateSpellMissEntry(*spell)) {
    return false;
  }

  bool added_entry = false;

  for (const SpellMissTarget& target : log.targets) {
    const char* const miss_type = LookupSpellMissTypeName(target.miss_info);
    if (miss_type == nullptr) {
      continue;
    }

    if (CombatText_IsActiveUnit(target.target_guid)) {
      ui::game::ScriptEventDispatch::Get().FireCombatTextUpdate(miss_type);
    }

    CombatEvent event;
    event.type = CombatEventType::kSpellMiss;
    event.source = ObjectGuid(log.caster_guid);
    event.target = ObjectGuid(target.target_guid);
    event.spell_id = spell->spell_id;
    event.miss_type = MapSpellMissInfoToMissType(target.miss_info);
    AddEvent(std::move(event));

    CombatLogEntry entry = BuildSpellMissEntry(
        *spell, log.caster_guid, target.target_guid,
        CombatLogEventType::DAMAGE_SHIELD_MISSED, miss_type);
    CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
    added_entry = true;
  }

  return added_entry;
}

bool CombatLog::HandleSpellOrDamageImmune(
    const SpellOrDamageImmune& log,
    const std::uint32_t timestamp_offset_ms) {
  const auto spell = LookupCombatLogSpellData(log.spell_id);
  if (!spell.has_value() || !CanCreateSpellMissEntry(*spell)) {
    return false;
  }

  if (CombatText_IsActiveUnit(log.target_guid)) {
    ui::game::ScriptEventDispatch::Get().FireCombatTextUpdate("IMMUNE");
  }

  CombatEvent event;
  event.type = CombatEventType::kSpellMiss;
  event.source = ObjectGuid(log.caster_guid);
  event.target = ObjectGuid(log.target_guid);
  event.spell_id = spell->spell_id;
  event.miss_type = MissType::Immune;
  AddEvent(std::move(event));

  const CombatLogEventType type = log.is_periodic != 0
                                      ? CombatLogEventType::SPELL_PERIODIC_MISSED
                                      : CombatLogEventType::DAMAGE_SHIELD_MISSED;
  CombatLogEntry entry = BuildSpellMissEntry(
      *spell, log.caster_guid, log.target_guid, type, "IMMUNE");
  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  return true;
}

bool CombatLog::HandleSpellInstaKill(
    const SpellInstaKillLog& log,
    const std::uint32_t timestamp_offset_ms) {
  const auto spell = LookupCombatLogSpellData(log.spell_id);
  if (!spell.has_value() || !CanCreateCombatLogSpellEntry(*spell)) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      log.caster_guid, log.target_guid, 14, spell->spell_id, spell->name);
  entry.type = CombatLogEventType::SPELL_INSTAKILL;
  entry.spell_school = spell->school;
  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  return true;
}

bool CombatLog::HandleSpellDamageShield(
    const SpellDamageShield& log,
    const std::uint32_t timestamp_offset_ms) {
  const auto spell = LookupCombatLogSpellData(log.spell_id);
  if (!spell.has_value() || !CanCreateCombatLogSpellEntry(*spell)) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      log.victim_guid, log.attacker_guid, 40, spell->spell_id, spell->name);
  entry.type = CombatLogEventType::DAMAGE_SHIELD;
  entry.spell_school = spell->school;
  entry.school = spell->school;

  entry.amount = static_cast<std::int32_t>(log.damage);
  entry.absorbed = static_cast<std::int32_t>(log.absorb_amount);
  entry.resisted = static_cast<std::int32_t>(log.resist_amount);
  entry.overkill = 0;
  entry.blocked = 0;
  entry.critical = false;
  entry.glancing = false;
  entry.crushing = false;

  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);

  if (CombatLog_IsActivePlayerTarget(log.attacker_guid)) {
    CombatLog_FireCombatTextSD(CombatTextMsgIdx::kDamageShield,
                               static_cast<std::int32_t>(log.damage));
  }

  return true;
}

bool CombatLog::HandleSpellCastStart(const ObjectManager& objects,
                                     const std::uint64_t caster_guid,
                                     const std::uint32_t spell_id,
                                     const std::uint8_t cast_count,
                                     const std::uint32_t cast_time_ms) {
  if (cast_time_ms == 0) {
    return false;
  }

  if (objects.GetUnit(ObjectGuid(caster_guid)) == nullptr) {
    return false;
  }

  const auto spell = LookupCombatLogSpellData(spell_id);
  if (!spell.has_value() || !CanCreateSpellCastEntry(*spell)) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      caster_guid, 0, 5, spell->spell_id, spell->name);
  entry.type = CombatLogEventType::SPELL_CAST_START;
  entry.spell_school = spell->school;
  CombatLog_FinalizeEntry(*this, entry);

  pending_spell_cast_starts_.erase(
      std::remove_if(
          pending_spell_cast_starts_.begin(),
          pending_spell_cast_starts_.end(),
          [caster_guid, spell_id, cast_count](const PendingSpellCastStartKey& key) {
            return key.caster_guid == caster_guid &&
                   key.spell_id == spell_id &&
                   key.cast_count == cast_count;
          }),
      pending_spell_cast_starts_.end());
  pending_spell_cast_starts_.push_back({caster_guid, spell_id, cast_count});

  constexpr std::size_t kMaxPendingSpellCastStarts = 64;
  while (pending_spell_cast_starts_.size() > kMaxPendingSpellCastStarts) {
    pending_spell_cast_starts_.pop_front();
  }

  return true;
}

bool CombatLog::HandleSpellCastSuccess(const ObjectManager& objects,
                                       const std::uint64_t caster_guid,
                                       const std::uint64_t target_guid,
                                       const std::uint32_t spell_id,
                                       const std::uint8_t cast_count) {
  if (objects.GetUnit(ObjectGuid(caster_guid)) == nullptr) {
    return false;
  }

  const auto spell = LookupCombatLogSpellData(spell_id);
  if (!spell.has_value() || !CanCreateSpellCastEntry(*spell)) {
    return false;
  }

  const auto pending = std::find_if(
      pending_spell_cast_starts_.begin(),
      pending_spell_cast_starts_.end(),
      [caster_guid, spell_id, cast_count](const PendingSpellCastStartKey& key) {
        return key.caster_guid == caster_guid &&
               key.spell_id == spell_id &&
               key.cast_count == cast_count;
      });
  if (pending != pending_spell_cast_starts_.end()) {
    pending_spell_cast_starts_.erase(pending);
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      caster_guid, target_guid, 6, spell->spell_id, spell->name);
  entry.type = CombatLogEventType::SPELL_CAST_SUCCESS;
  entry.spell_school = spell->school;
  CombatLog_FinalizeEntry(*this, entry);

  if (CombatLog_IsActivePlayerTarget(caster_guid) &&
      (spell->attributes_ex6 & 0x40u) != 0u) {
    CombatLog_FireCombatTextSS(CombatTextMsgIdx::kSpellCast,
                               spell->name.c_str());
  }

  return true;
}

bool CombatLog::HandleSpellCastFailed(const std::uint64_t caster_guid,
                                      const std::uint32_t spell_id,
                                      const std::string& failed_message) {
  if (failed_message.empty()) {
    return false;
  }

  const auto spell = LookupCombatLogSpellData(spell_id);
  if (!spell.has_value() || !CanCreateCombatLogSpellEntry(*spell)) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(
      caster_guid, 0, 7, spell->spell_id, spell->name);
  entry.type = CombatLogEventType::SPELL_CAST_FAILED;
  entry.spell_school = spell->school;

  entry.failed_message = failed_message;

  CombatLog_FinalizeEntry(*this, entry);
  return true;
}

bool CombatLog::HandleAuraStateTransition(const std::uint64_t target_guid,
                                          const AuraSlotInfo* const old_aura,
                                          const AuraSlotInfo* const new_aura,
                                          const std::size_t active_aura_count) {
  const auto old_active =
      old_aura != nullptr &&
      old_aura->spell_id != 0;
  const auto new_active =
      new_aura != nullptr &&
      new_aura->spell_id != 0;

  const bool aura_removed_or_replaced =
      old_active && (!new_active || old_aura->spell_id != new_aura->spell_id);

  if (aura_removed_or_replaced &&
      old_aura->spell_id == kAreaSpiritHealWaitingSpellId) {

    const ObjectManager* const objects =
        object_manager_provider_ ? object_manager_provider_() : nullptr;
    if (objects != nullptr &&
        objects->GetActivePlayerGuid().GetRawValue() == target_guid) {
      ui::game::ScriptEventDispatch::Get().FireEvent(
          "AREA_SPIRIT_HEALER_OUT_OF_RANGE");
    }
  }

  if (aura_removed_or_replaced && active_aura_count > 0u) {
    const auto spell = LookupCombatLogSpellData(old_aura->spell_id);
    if (spell.has_value() && CanCreateAuraChangeEntry(*spell)) {
      const bool is_harmful = HasFlag(old_aura->flags, AuraFlag::kNegative);
      CombatLogEntry entry = CombatLog_CreateEntry(
          old_aura->caster_guid.GetRawValue(), target_guid, 24u,
          spell->spell_id, spell->name);
      entry.type = CombatLogEventType::SPELL_AURA_REMOVED;
      entry.spell_school = spell->school;
      entry.aura_type = AuraTypeName(*old_aura);
      CombatLog_FinalizeEntry(*this, entry);
      FireAuraCombatText(target_guid, *spell, false, is_harmful);
    }
  }

  if (!new_active || active_aura_count == 0u) {
    return old_active;
  }

  const auto spell = LookupCombatLogSpellData(new_aura->spell_id);
  if (!spell.has_value()) {
    return old_active;
  }

  if (!old_active || old_aura->spell_id != new_aura->spell_id) {
    if (!CanCreateAuraChangeEntry(*spell)) {
      return old_active;
    }

    CombatLogEntry entry = CombatLog_CreateEntry(
        new_aura->caster_guid.GetRawValue(), target_guid, 21u,
        spell->spell_id, spell->name);
    entry.type = CombatLogEventType::SPELL_AURA_APPLIED;
    entry.spell_school = spell->school;
    entry.aura_type = AuraTypeName(*new_aura);
    CombatLog_FinalizeEntry(*this, entry);
    const bool is_harmful = HasFlag(new_aura->flags, AuraFlag::kNegative);
    FireAuraCombatText(target_guid, *spell, true, is_harmful);
    EmitAuraTriggeredSpellMechanics(
        *this, target_guid, new_aura->caster_guid.GetRawValue(), *spell);
    return true;
  }

  if (old_aura->stack_or_charges != new_aura->stack_or_charges) {
    if (!CanCreateAuraDoseEntry(*spell, active_aura_count)) {
      return false;
    }

    const auto event_index =
        (new_aura->stack_or_charges > old_aura->stack_or_charges) ? 22u : 23u;
    CombatLogEntry entry = CombatLog_CreateEntry(
        new_aura->caster_guid.GetRawValue(), target_guid, event_index,
        spell->spell_id, spell->name);
    entry.type = (event_index == 22u)
                     ? CombatLogEventType::SPELL_AURA_APPLIED_DOSE
                     : CombatLogEventType::SPELL_AURA_REMOVED_DOSE;
    entry.spell_school = spell->school;
    entry.aura_type = AuraTypeName(*new_aura);
    entry.aura_amount = new_aura->stack_or_charges;
    CombatLog_FinalizeEntry(*this, entry);
    return true;
  }

  if (old_aura->remaining_duration != new_aura->remaining_duration &&
      CanCreateAuraRefreshEntry(*spell)) {
    CombatLogEntry entry = CombatLog_CreateEntry(
        new_aura->caster_guid.GetRawValue(), target_guid, 25u,
        spell->spell_id, spell->name);
    entry.type = CombatLogEventType::SPELL_AURA_REFRESH;
    entry.spell_school = spell->school;
    entry.aura_type = AuraTypeName(*new_aura);
    CombatLog_FinalizeEntry(*this, entry);
    EmitAuraTriggeredSpellMechanics(
        *this, target_guid, new_aura->caster_guid.GetRawValue(), *spell);
    return true;
  }

  return false;
}

bool CombatLog::HandleEnvironmentalDamageLog(const std::uint8_t* data,
                                             const std::size_t len) {
  PacketReader reader(data, len);

  return HandleEnvironmentalDamageLog(reader, 0);
}

bool CombatLog::HandleEnvironmentalDamageLog(
    PacketReader& reader, const std::uint32_t timestamp_offset_ms) {

  ObjectGuid target;
  std::uint8_t env_type = 0;
  std::uint32_t amount = 0;
  std::uint32_t absorbed = 0;
  std::uint32_t resisted = 0;
  if (!reader.ReadGuid(target) || !reader.ReadU8(env_type) || !reader.ReadU32(amount) ||
      !reader.ReadU32(absorbed) || !reader.ReadU32(resisted)) {
    return false;
  }

  if (amount == 0 && absorbed == 0 && resisted == 0) {
    return true;
  }

  const char* const env_type_name = CombatLog_GetEnvironmentalDamageTypeName(env_type);
  const std::uint32_t school_mask =
      CombatLog_GetEnvironmentalDamageSchoolMask(env_type);
  if (env_type_name == nullptr || school_mask == 0) {
    return false;
  }

  CombatLogEntry entry = CombatLog_CreateEntry(0, target.GetRawValue(), 0);
  entry.type = CombatLogEventType::ENVIRONMENTAL_DAMAGE;
  entry.env_type = env_type_name;
  entry.amount = static_cast<std::int32_t>(amount);
  entry.overkill = 0;
  entry.school = school_mask;
  entry.resisted = static_cast<std::int32_t>(resisted);
  entry.blocked = 0;
  entry.absorbed = static_cast<std::int32_t>(absorbed);
  CombatLog_FinalizeEntry(*this, entry, timestamp_offset_ms);
  FireEnvironmentalDamageCombatText(
      target.GetRawValue(),
      static_cast<std::int32_t>(amount),
      static_cast<std::int32_t>(absorbed),
      static_cast<std::int32_t>(resisted));
  return true;
}

std::vector<CombatLogEntry> CombatLog::GetEntries() const {
  return {log_entries_.begin(), log_entries_.end()};
}

std::vector<CombatLogEntry> CombatLog::GetRecentEntries(
    std::uint32_t count) const {
  if (count >= log_entries_.size()) {
    return {log_entries_.begin(), log_entries_.end()};
  }
  auto start =
      log_entries_.end() - static_cast<std::ptrdiff_t>(count);
  return {start, log_entries_.end()};
}

std::vector<CombatLogEntry> CombatLog::GetEntriesByType(
    CombatLogEventType type) const {
  std::vector<CombatLogEntry> result;
  for (const auto& e : log_entries_) {
    if (e.type == type) result.push_back(e);
  }
  return result;
}

std::vector<CombatLogEntry> CombatLog::GetEntriesForUnit(
    ObjectGuid guid) const {
  std::vector<CombatLogEntry> result;
  auto raw = guid.GetRawValue();
  for (const auto& e : log_entries_) {
    if (e.source_guid == raw || e.dest_guid == raw) {
      result.push_back(e);
    }
  }
  return result;
}

void CombatLog::Reset() {
  Clear();
  filter_flags_ = 0;
  event_filters_.clear();
  max_log_entries_ = kDefaultMaxEntries;
  retention_time_s_ = 120.0f;
}

namespace {

bool IsDamageEventType(CombatLogEventType t) {
  switch (t) {
    case CombatLogEventType::SWING_DAMAGE:
    case CombatLogEventType::RANGE_DAMAGE:
    case CombatLogEventType::SPELL_DAMAGE:
    case CombatLogEventType::SPELL_PERIODIC_DAMAGE:
    case CombatLogEventType::DAMAGE_SHIELD:
    case CombatLogEventType::DAMAGE_SPLIT:
    case CombatLogEventType::ENVIRONMENTAL_DAMAGE:
      return true;
    default:
      return false;
  }
}

bool IsHealEventType(CombatLogEventType t) {
  switch (t) {
    case CombatLogEventType::SPELL_HEAL:
    case CombatLogEventType::SPELL_PERIODIC_HEAL:
      return true;
    default:
      return false;
  }
}

}

std::int64_t CombatLog::GetDamageDone(ObjectGuid source,
                                       float timePeriod) const {
  double cutoff = Now() - static_cast<double>(timePeriod);
  auto raw = source.GetRawValue();
  std::int64_t total = 0;
  for (const auto& e : log_entries_) {
    if (e.timestamp >= cutoff && e.source_guid == raw &&
        IsDamageEventType(e.type)) {
      total += e.amount;
    }
  }
  return total;
}

std::int64_t CombatLog::GetHealingDone(ObjectGuid source,
                                        float timePeriod) const {
  double cutoff = Now() - static_cast<double>(timePeriod);
  auto raw = source.GetRawValue();
  std::int64_t total = 0;
  for (const auto& e : log_entries_) {
    if (e.timestamp >= cutoff && e.source_guid == raw &&
        IsHealEventType(e.type)) {
      total += e.amount;
    }
  }
  return total;
}

std::int64_t CombatLog::GetDamageTaken(ObjectGuid dest,
                                        float timePeriod) const {
  double cutoff = Now() - static_cast<double>(timePeriod);
  auto raw = dest.GetRawValue();
  std::int64_t total = 0;
  for (const auto& e : log_entries_) {
    if (e.timestamp >= cutoff && e.dest_guid == raw &&
        IsDamageEventType(e.type)) {
      total += e.amount;
    }
  }
  return total;
}

void CombatLog::AddEventFilter(CombatLogEventFilter f) {
  event_filters_.push_back(std::move(f));
}

void CombatLog::ResetEventFilters() {
  event_filters_.clear();
}

bool CombatLog::MatchesEventFilters(const CombatLogEntry& e) const {
  return CombatLogFilter_MatchEntry(event_filters_, e);
}

std::size_t CombatLog::CountFilteredEntries(bool ignore_filter) const {
  if (ignore_filter || event_filters_.empty()) return log_entries_.size();
  std::size_t count = 0;
  for (const auto& e : log_entries_) {
    if (MatchesEventFilters(e)) ++count;
  }
  return count;
}

bool CombatLog::SetCurrentEntryLua(int n, bool ignore_filter) {

  current_index_ = log_entries_.size();
  const auto matches = [&](const std::size_t index) {
    return ignore_filter || MatchesEventFilters(log_entries_[index]);
  };

  if (n > 0) {
    for (std::size_t i = log_entries_.size(); i > 0;) {
      --i;
      if (matches(i) && --n == 0) {
        current_index_ = i;
        return true;
      }
    }
    return false;
  }

  for (std::size_t i = 0; i < log_entries_.size(); ++i) {
    if (matches(i) && ++n == 1) {
      current_index_ = i;
      return true;
    }
  }
  return false;
}

bool CombatLog::AdvanceEntryLua(int count, bool ignore_filter) {
  if (current_index_ >= log_entries_.size()) {
    return false;
  }

  const auto matches = [&](const std::size_t index) {
    return ignore_filter || MatchesEventFilters(log_entries_[index]);
  };

  if (count > 0) {
    for (std::size_t i = current_index_ + 1; i > 0;) {
      --i;
      if (matches(i) && --count == -1) {
        current_index_ = i;
        return true;
      }
    }
  } else {
    for (std::size_t i = current_index_; i < log_entries_.size(); ++i) {
      if (matches(i) && ++count == 1) {
        current_index_ = i;
        return true;
      }
    }
  }

  current_index_ = log_entries_.size();
  return false;
}

void CombatLog_FireEventString(const char* event_string) {
  if (!event_string) {
    return;
  }
  AppendClientTextLogLine(ClientTextLogKind::Combat, event_string);
}

std::uint32_t CombatLog_ParseHexString(const char* hex_string) {
  if (!hex_string) return 0;

  const char* p = hex_string;

  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
  }

  std::uint32_t result = 0;
  int remaining = 16;

  do {
    char ch = *p;
    --remaining;
    ++p;

    int digit;
    std::uint8_t d0 = static_cast<std::uint8_t>(ch - '0');
    if (d0 <= 9) {
      digit = ch - '0';
    } else if (static_cast<std::uint8_t>(ch - 'a') <= 5) {
      digit = ch - 'a' + 10;
    } else if (static_cast<std::uint8_t>(ch - 'A') <= 5) {
      digit = ch - 'A' + 10;
    } else {
      return result;
    }

    result = (result << 4) | static_cast<std::uint32_t>(digit);
  } while (remaining);

  return result;
}

}
