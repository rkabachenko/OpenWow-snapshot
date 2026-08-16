
#include "openwow/game/combat_log_internal.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/combat_log_messages.h"
#include "openwow/game/group_manager.h"
#include "openwow/game/group_system.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstring>
#include <optional>

namespace openwow::game {

namespace {

struct ResolvedCombatLogSpell {
  std::string name;
  std::uint32_t attributes = 0;
  std::uint32_t school_mask = 0;
  bool has_dbc_entry = false;
};

std::optional<ResolvedCombatLogSpell> ResolveCombatLogSpell(
    const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return std::nullopt;
  }

  if (const auto* dbc = SpellbookSystem::Get().GetDbcLoader(); dbc != nullptr) {
    if (const auto* spell = dbc->spell().LookupEntry(spell_id); spell != nullptr) {
      return ResolvedCombatLogSpell{
          .name = std::string(spell->spell_name),
          .attributes = spell->attributes,
          .school_mask = spell->school_mask,
          .has_dbc_entry = true,
      };
    }
  }

  if (const auto spell = SpellQueryBridge::Get().Query(spell_id)) {
    return ResolvedCombatLogSpell{
        .name = spell->name,
        .attributes = spell->attributes,
        .school_mask = spell->schoolMask,
        .has_dbc_entry = false,
    };
  }
  return std::nullopt;
}

std::uint32_t BuildTrackedPartyAssignmentCombatLogFlags(std::uint64_t guid) {
  const auto active_player_guid = CombatText_GetActiveUnitGuid();
  if (guid == 0 || active_player_guid == 0) {
    return 0;
  }

  const auto assignment_flags =
      GroupSystem::Get().GetTrackedPartyAssignmentFlags(guid, active_player_guid);
  if ((assignment_flags &
       static_cast<std::uint8_t>(GroupMemberFlag::kMainTank)) != 0) {
    return UnitFlag::kMainTank;
  }
  if ((assignment_flags &
       static_cast<std::uint8_t>(GroupMemberFlag::kMainAssist)) != 0) {
    return UnitFlag::kMainAssist;
  }
  return 0;
}

const data::dbc::SpellItemEnchantmentEntry* LookupCombatLogEnchantment(
    const std::uint32_t enchant_id) {
  if (enchant_id == 0) {
    return nullptr;
  }

  const auto* dbc = SpellbookSystem::Get().GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }

  return dbc->spell_item_enchantment().LookupEntry(enchant_id);
}

}

void CombatLog_FormatGuid(std::uint32_t lo, std::uint32_t hi, char* out) {
  out[0] = '0';

  out[1] = 'x';

  out[18] = '\0';

  char* p = out + 17;
  for (int i = 0; i < 16; ++i) {
    std::uint8_t nibble = lo & 0xF;
    char ch = (nibble >= 0xA) ? static_cast<char>(nibble + 55)
                              : static_cast<char>(nibble + '0');
    *p-- = ch;

    lo = (lo >> 4) | (hi << 28);
    hi >>= 4;
  }
}

void CombatLog_AppendHexField(char** buf_pos, std::uint32_t value,
                              std::uint32_t* remaining) {
  if (*remaining > 1u) {
    *(*buf_pos)++ = ',';
    --(*remaining);

  }

  if (*remaining < 10u) return;

  *(*buf_pos)++ = '0';
  *(*buf_pos)++ = 'x';
  *remaining -= 2;

  if (value != 0) {

    std::uint32_t mask = 0xF0000000u;
    int digit_count = 8;
    while ((mask & value) == 0) {

      mask >>= 4;

      --digit_count;
    }

    *buf_pos += digit_count;

    std::uint32_t v = value;
    char* p = *buf_pos;
    do {
      --p;

      std::uint8_t nibble = v & 0xF;

      char ch;
      if (nibble >= 0xA) {

        ch = static_cast<char>(nibble + 87);
      } else {
        ch = static_cast<char>(nibble + '0');
      }
      v >>= 4;

      *p = ch;

    } while (v != 0);

    *remaining -= static_cast<std::uint32_t>(digit_count);
  } else {
    *(*buf_pos)++ = '0';
    --(*remaining);

  }
}

void CombatLog_AppendRawField(char** buf_pos, std::uint32_t* remaining,
                              const char* str) {
  if (*remaining > 1u) {
    *(*buf_pos)++ = ',';

    --(*remaining);

  }

  const char* s = str;
  while (*s != '\0') {
    if (*remaining == 0) break;

    *(*buf_pos)++ = *s;

    --(*remaining);

    ++s;

  }
}

void CombatLog_AppendQuotedField(char** buf_pos, const char* str,
                                 std::uint32_t* remaining) {
  if (*remaining > 1u) {
    *(*buf_pos)++ = ',';
    if (--(*remaining) > 1u) {

      *(*buf_pos)++ = '"';
      --(*remaining);

    }
  }

  const char* s = str;
  while (*s != '\0') {
    if (*remaining == 0) break;

    char ch = *s;
    if ((ch == '"' || ch == '\\') && *remaining > 1u) {

      *(*buf_pos)++ = '\\';
      --(*remaining);

    }
    *(*buf_pos)++ = *s;

    --(*remaining);

    ++s;

  }

  if (*remaining > 1u) {
    *(*buf_pos)++ = '"';
    --(*remaining);

  }
}

bool CombatLog_TestUnitFlags(std::uint32_t filter_flags,
                             std::uint32_t unit_flags) {
  std::uint32_t v2 = filter_flags & unit_flags;

  if ((v2 & 0xFFFF0000u) != 0) return true;

  if ((v2 & 0x000Fu) != 0 &&
      (v2 & 0x00F0u) != 0 &&
      (v2 & 0x0300u) != 0) {
    return (v2 & 0xFC00u) != 0;
  }
  return false;
}

std::uint32_t CombatLog_BuildUnitFlags(std::uint64_t guid) {
  if (guid == 0) return UnitFlag::kNone;

  ObjectGuid og(guid);

  std::uint32_t flags = 0;

  std::uint32_t hi = static_cast<std::uint32_t>(guid >> 32);
  std::uint32_t type_mask = hi & 0xF0F00000u;

  if ((hi & 0xF0000000u) == 0 && (hi & 0x0F07FFFFu) != 0) {

    flags |= UnitFlag::kTypePlayer | UnitFlag::kControlPlayer;
  } else if (type_mask == 0xF0300000u || type_mask == 0xF0500000u) {

    flags |= UnitFlag::kTypeNPC | UnitFlag::kControlNPC;
  } else if (type_mask == 0xF0400000u) {

    flags |= UnitFlag::kTypePet | UnitFlag::kControlNPC;
  } else if (type_mask == 0xF0100000u) {

    flags |= UnitFlag::kTypeObject;
  } else {

    flags |= UnitFlag::kTypeNPC | UnitFlag::kControlNPC;
  }

  flags |= UnitFlag::kAffilOutsider;
  flags |= UnitFlag::kReactionHostile;

  if ((flags & 0x0Fu) == 0) flags |= UnitFlag::kAffilOutsider;
  if ((flags & 0xF0u) == 0) flags |= UnitFlag::kReactionNeutral;

  flags |= BuildTrackedPartyAssignmentCombatLogFlags(guid);

  return flags;
}

void CombatLog_ResolveGUID(std::uint64_t& guid) {
  std::uint32_t hi = static_cast<std::uint32_t>(guid >> 32);
  std::uint32_t type_mask = hi & 0xF0F00000u;

  if (type_mask != 0xF0300000u && type_mask != 0xF0500000u) return;

}

std::string CombatLog_ResolveName(std::uint64_t guid) {
  if (guid == 0) return "UNKNOWNOBJECT";

  return "UNKNOWNOBJECT";
}

std::string CombatLog_BuildNameForGUID(std::uint64_t guid) {
  return CombatLog_ResolveName(guid);
}

CombatLogEventType MapEventIndex(std::uint32_t index) {
  switch (index) {
    case 0:  return CombatLogEventType::ENVIRONMENTAL_DAMAGE;
    case 1:  return CombatLogEventType::SWING_DAMAGE;
    case 2:  return CombatLogEventType::SWING_MISSED;
    case 3:  return CombatLogEventType::RANGE_DAMAGE;
    case 4:  return CombatLogEventType::RANGE_MISSED;
    case 5:  return CombatLogEventType::SPELL_CAST_START;
    case 6:  return CombatLogEventType::SPELL_CAST_SUCCESS;
    case 7:  return CombatLogEventType::SPELL_CAST_FAILED;
    case 8:  return CombatLogEventType::SPELL_MISSED;
    case 9:  return CombatLogEventType::SPELL_DAMAGE;
    case 10: return CombatLogEventType::SPELL_HEAL;
    case 11: return CombatLogEventType::SPELL_ENERGIZE;
    case 12: return CombatLogEventType::SPELL_DRAIN;
    case 13: return CombatLogEventType::SPELL_LEECH;
    case 14: return CombatLogEventType::SPELL_INSTAKILL;
    case 15: return CombatLogEventType::SPELL_SUMMON;
    case 16: return CombatLogEventType::SPELL_CREATE;
    case 17: return CombatLogEventType::SPELL_INTERRUPT;
    case 18: return CombatLogEventType::SPELL_EXTRA_ATTACKS;
    case 19: return CombatLogEventType::SPELL_DURABILITY_DAMAGE;
    case 20: return CombatLogEventType::SPELL_DURABILITY_DAMAGE_ALL;
    case 21: return CombatLogEventType::SPELL_AURA_APPLIED;
    case 22: return CombatLogEventType::SPELL_AURA_APPLIED_DOSE;
    case 23: return CombatLogEventType::SPELL_AURA_REMOVED_DOSE;
    case 24: return CombatLogEventType::SPELL_AURA_REMOVED;
    case 25: return CombatLogEventType::SPELL_AURA_REFRESH;
    case 26: return CombatLogEventType::SPELL_DISPEL;
    case 27: return CombatLogEventType::SPELL_STOLEN;
    case 28: return CombatLogEventType::SPELL_AURA_BROKEN;
    case 29: return CombatLogEventType::SPELL_AURA_BROKEN_SPELL;
    case 30: return CombatLogEventType::DAMAGE_AURA_BROKEN;
    case 31: return CombatLogEventType::ENCHANT_APPLIED;
    case 32: return CombatLogEventType::ENCHANT_REMOVED;
    case 33: return CombatLogEventType::SPELL_PERIODIC_MISSED;
    case 34: return CombatLogEventType::SPELL_PERIODIC_DAMAGE;
    case 35: return CombatLogEventType::SPELL_PERIODIC_HEAL;
    case 36: return CombatLogEventType::SPELL_PERIODIC_ENERGIZE;
    case 37: return CombatLogEventType::SPELL_PERIODIC_DRAIN;
    case 38: return CombatLogEventType::SPELL_PERIODIC_LEECH;
    case 39: return CombatLogEventType::SPELL_DISPEL_FAILED;
    case 40: return CombatLogEventType::DAMAGE_SHIELD;
    case 41: return CombatLogEventType::DAMAGE_SHIELD_MISSED;
    case 42: return CombatLogEventType::DAMAGE_SPLIT;
    case 43: return CombatLogEventType::PARTY_KILL;
    case 44: return CombatLogEventType::UNIT_DIED;
    case 45: return CombatLogEventType::UNIT_DESTROYED;
    case 46: return CombatLogEventType::SPELL_RESURRECT;
    case 47: return CombatLogEventType::SPELL_BUILDING_DAMAGE;
    case 48: return CombatLogEventType::SPELL_BUILDING_HEAL;
    case 49: return CombatLogEventType::UNIT_DISSIPATES;
    default:
      return CombatLogEventType::INVALID;
  }
}

std::uint32_t CombatLog_DefaultSuffixFlags(const CombatLogEntry& entry) {
  using namespace CombatLogSuffixFlag;

  switch (entry.type) {
    case CombatLogEventType::SWING_DAMAGE:
    case CombatLogEventType::RANGE_DAMAGE:
    case CombatLogEventType::SPELL_DAMAGE:
    case CombatLogEventType::SPELL_PERIODIC_DAMAGE:
    case CombatLogEventType::DAMAGE_SPLIT:
    case CombatLogEventType::SPELL_BUILDING_DAMAGE:
    case CombatLogEventType::DAMAGE_SHIELD:

      return kDamage;

    case CombatLogEventType::ENVIRONMENTAL_DAMAGE:

      return kEnvironmentalType | kDamage;

    case CombatLogEventType::SWING_MISSED:
    case CombatLogEventType::RANGE_MISSED:
    case CombatLogEventType::SPELL_MISSED:
    case CombatLogEventType::SPELL_PERIODIC_MISSED:
    case CombatLogEventType::DAMAGE_SHIELD_MISSED:
      return kMissType |
             (entry.miss_type == "RESIST"
                  ? kResisted
                  : 0u) |
             (entry.miss_type == "BLOCK"
                  ? kBlocked
                  : 0u) |
             (entry.miss_type == "ABSORB"
                  ? kAbsorbed
                  : 0u);

    case CombatLogEventType::SPELL_HEAL:
    case CombatLogEventType::SPELL_PERIODIC_HEAL:
    case CombatLogEventType::SPELL_BUILDING_HEAL:
      return kHeal;

    case CombatLogEventType::SPELL_ENERGIZE:
    case CombatLogEventType::SPELL_PERIODIC_ENERGIZE:
      return kEnergize;

    case CombatLogEventType::SPELL_DRAIN:
    case CombatLogEventType::SPELL_LEECH:
    case CombatLogEventType::SPELL_PERIODIC_DRAIN:
    case CombatLogEventType::SPELL_PERIODIC_LEECH:
      return kDrain;

    case CombatLogEventType::SPELL_CAST_FAILED:
      return kString;

    case CombatLogEventType::SPELL_EXTRA_ATTACKS:
      return kAmount;

    case CombatLogEventType::SPELL_AURA_APPLIED:
    case CombatLogEventType::SPELL_AURA_REMOVED:
    case CombatLogEventType::SPELL_AURA_REFRESH:
    case CombatLogEventType::SPELL_AURA_BROKEN:
    case CombatLogEventType::DAMAGE_AURA_BROKEN:
      return kAuraType;

    case CombatLogEventType::SPELL_AURA_APPLIED_DOSE:
    case CombatLogEventType::SPELL_AURA_REMOVED_DOSE:
      return kAuraType | kNumber;

    case CombatLogEventType::SPELL_AURA_BROKEN_SPELL:
    case CombatLogEventType::SPELL_DISPEL:
    case CombatLogEventType::SPELL_STOLEN:
      return kExtraSpell | kAuraType;

    case CombatLogEventType::SPELL_INTERRUPT:
    case CombatLogEventType::SPELL_DISPEL_FAILED:
      return kExtraSpell;

    case CombatLogEventType::SPELL_DURABILITY_DAMAGE:

      return kNumberAndString;

    default:
      return 0;
  }
}

bool CombatLog_EventHasSpellPrefix(const CombatLogEventType type) {
  return (type >= CombatLogEventType::SPELL_DAMAGE &&
          type <= CombatLogEventType::SPELL_RESURRECT) ||
         type == CombatLogEventType::RANGE_DAMAGE ||
         type == CombatLogEventType::RANGE_MISSED ||
         type == CombatLogEventType::DAMAGE_SHIELD ||
         type == CombatLogEventType::DAMAGE_SHIELD_MISSED ||
         type == CombatLogEventType::DAMAGE_SPLIT;
}

bool CombatLog_EventHasEnchantNameSuffix(const CombatLogEventType type) {
  return type == CombatLogEventType::ENCHANT_APPLIED ||
         type == CombatLogEventType::ENCHANT_REMOVED;
}

const std::string& CombatLog_GetFilterName(const CombatLogEntry& entry) {
  if (CombatLog_EventHasEnchantNameSuffix(entry.type)) {
    return entry.enchant_name;
  }

  return entry.spell_name;
}

CombatLogEntry CombatLog_CreateEntry(std::uint64_t source_guid,
                                     std::uint64_t dest_guid,
                                     std::uint32_t event_index,
                                     std::uint32_t spell_id,
                                     const std::string& spell_name) {
  CombatLogEntry entry;
  entry.type = MapEventIndex(event_index);

  entry.source_guid = source_guid;
  if (source_guid != 0) {
    entry.source_name = CombatLog_BuildNameForGUID(source_guid);
    entry.source_flags = CombatLog_BuildUnitFlags(source_guid);
  }

  entry.dest_guid = dest_guid;
  if (dest_guid != 0) {
    entry.dest_name = CombatLog_BuildNameForGUID(dest_guid);
    entry.dest_flags = CombatLog_BuildUnitFlags(dest_guid);
  }

  entry.spell_id = spell_id;
  entry.spell_name = spell_name;

  return entry;
}

void CombatLog_FinalizeEntry(CombatLog& log, CombatLogEntry& entry,
                             const std::uint32_t timestamp_offset) {

  CombatLog_ResolveGUID(entry.source_guid);
  CombatLog_ResolveGUID(entry.dest_guid);
  if (entry.timestamp == 0.0) {
    entry.timestamp = log.TimestampWithOffsetMs(timestamp_offset);
  }

  log.AddLogEntry(std::move(entry));
}

void CombatLogEntry_Reset(CombatLogEntry& entry) {
  entry = CombatLogEntry{};
  entry.source_flags = UnitFlag::kNone;
  entry.dest_flags = UnitFlag::kNone;
}

void CombatLogEntry_SetTimestamp(CombatLogEntry& entry,
                                 std::uint32_t server_time) {

  entry.timestamp = static_cast<double>(server_time) / 1000.0;
}

void CombatLog_InvalidateNameCache(const char* ) {

}

bool CombatLogFilter_SetFilterCriteria(
    CombatLogEventFilter& filter,
    const char* event_list,
    std::uint64_t src_guid, std::uint32_t src_flags,
    std::uint64_t dst_guid, std::uint32_t dst_flags,
    std::uint32_t spell_id, const char* spell_name) {

  filter.events.clear();
  if (event_list != nullptr) {
    filter.all_events = false;
    const auto add_event = [&](const std::string& token) {
      for (int i = 0;
           i <= static_cast<int>(CombatLogEventType::UNIT_DISSIPATES); ++i) {
        const auto type = static_cast<CombatLogEventType>(i);
        if (openwow::core::SStrCmpNoCase(
                token.c_str(), CombatLog::GetEventName(type), 0x7FFFFFFFu) == 0) {
          filter.events.push_back(type);
          return;
        }
      }
    };

    std::string token;
    const char* p = event_list;
    while (*p) {
      if (*p == ',' || *p == ' ') {
        if (!token.empty()) {
          add_event(token);
          token.clear();
        }
        ++p;
      } else {
        token += *p++;
      }
    }
    if (!token.empty()) {
      add_event(token);
    }
  } else {
    filter.all_events = true;
  }

  if (src_guid != 0) {
    filter.src_any = false;
    filter.src_guid = src_guid;
    filter.src_flags = 0;
  } else if (src_flags != 0) {
    filter.src_any = false;
    filter.src_guid = 0;
    filter.src_flags = src_flags;
  } else {
    filter.src_any = true;
  }

  if (dst_guid != 0) {
    filter.dst_any = false;
    filter.dst_guid = dst_guid;
    filter.dst_flags = 0;
  } else if (dst_flags != 0) {
    filter.dst_any = false;
    filter.dst_guid = 0;
    filter.dst_flags = dst_flags;
  } else {
    filter.dst_any = true;
  }

  filter.spell_id = spell_id;
  filter.spell_name = (spell_name && spell_id == 0) ? spell_name : "";

  return true;
}

bool CombatLogFilter_TestSingleFilter(
    const CombatLogEventFilter& filter,
    const CombatLogEntry& entry) {

  if (!filter.all_events) {
    bool found = false;
    for (auto t : filter.events) {
      if (t == entry.type) { found = true; break; }
    }
    if (!found) return false;
  }

  if (!filter.src_any) {
    if (filter.src_guid != 0) {
      if (entry.source_guid != filter.src_guid) return false;
    } else {
      if (!CombatLog_TestUnitFlags(filter.src_flags, entry.source_flags))
        return false;
    }
  }

  if (!filter.dst_any) {
    if (filter.dst_guid != 0) {
      if (entry.dest_guid != filter.dst_guid) return false;
    } else {
      if (!CombatLog_TestUnitFlags(filter.dst_flags, entry.dest_flags))
        return false;
    }
  }

  if (filter.spell_id != 0) {
    if (entry.spell_id != filter.spell_id) return false;
  } else if (!filter.spell_name.empty()) {
    const std::string& entry_name = CombatLog_GetFilterName(entry);
    if (entry_name.empty() ||
        openwow::core::SStrCmpUTF8NoCase(entry_name.c_str(),
                                         filter.spell_name.c_str(),
                                         0x7FFFFFFFu) != 0) {
      return false;
    }
  }

  return true;
}

bool CombatLogFilter_MatchEntry(
    const std::vector<CombatLogEventFilter>& filters,
    const CombatLogEntry& entry) {
  if (filters.empty()) return true;
  for (const auto& f : filters) {
    if (CombatLogFilter_TestSingleFilter(f, entry)) return true;
  }
  return false;
}

bool CombatLog_ShouldShowSpellMechanics(const std::uint32_t spell_flags,
                                        const std::uint64_t source_guid) {
  if ((spell_flags & 0x200000u) == 0) return false;

  const auto& cvars = ui::game::CVarSystem::Instance();
  if (cvars.GetCVarBool("fctAllSpellMechanics")) {
    return true;
  }
  if (!cvars.GetCVarBool("fctSpellMechanics")) {
    return false;
  }
  if (source_guid == CombatText_GetActiveUnitGuid()) {
    return true;
  }
  return cvars.GetCVarBool("fctSpellMechanicsOther");
}

bool CombatLog_HandlePartyKillOpcode(CombatLog& log,
                                     const std::uint8_t* data,
                                     std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid killer;
  ObjectGuid victim;
  (void)r.ReadGuid(killer);
  (void)r.ReadGuid(victim);
  return CombatLog_HandlePartyKill(log, killer.GetRawValue(),
                                   victim.GetRawValue());
}

bool CombatLog_HandlePartyKillOpcode(CombatLog& log,
                                     PacketReader& r,
                                     const std::uint32_t timestamp_offset_ms) {

  ObjectGuid killer, victim;
  if (!r.ReadGuid(killer) || !r.ReadGuid(victim)) return false;

  return CombatLog_HandlePartyKill(log, killer.GetRawValue(),
                                   victim.GetRawValue(), timestamp_offset_ms);
}

bool CombatLog_HandlePartyKill(CombatLog& log,
                               const std::uint64_t killer_guid,
                               const std::uint64_t victim_guid,
                               const std::uint32_t timestamp_offset_ms) {
  constexpr std::uint32_t kRetailPartyKillEventIndex = 43u;
  CombatLogEntry entry = CombatLog_CreateEntry(
      killer_guid, victim_guid, kRetailPartyKillEventIndex);

  CombatLog_FinalizeEntry(log, entry, timestamp_offset_ms);
  return true;
}

bool CombatLog_HandleEnchantLog(CombatLog& log,
                                const std::uint64_t target_guid,
                                const std::uint64_t caster_guid,
                                const std::uint32_t item_id,
                                const std::uint32_t enchant_id,
                                std::string item_name,
                                const double timestamp) {
  const auto* enchant = LookupCombatLogEnchantment(enchant_id);
  if (enchant == nullptr || (enchant->slot & 2u) != 0) return true;

  CombatLogEntry entry = CombatLog_CreateEntry(
      caster_guid,
      target_guid,
      caster_guid == 0 ? 32u : 31u,
      0,
      {});
  entry.enchant_name = std::string(enchant->description);
  entry.enchant_item_id = item_id;
  entry.enchant_item_name = std::move(item_name);
  entry.timestamp = timestamp;

  CombatLog_FinalizeEntry(log, entry);
  return true;
}

bool CombatLog_HandleEnchantOpcode(CombatLog& log,
                                   const ItemDefinitions& item_definitions,
                                   const std::uint8_t* data,
                                   std::size_t len) {
  PacketReader r(data, len);
  return CombatLog_HandleEnchantOpcode(log, item_definitions, r);
}

bool CombatLog_HandleEnchantOpcode(CombatLog& log,
                                   const ItemDefinitions& item_definitions,
                                   PacketReader& r,
                                   const std::uint32_t timestamp_offset_ms) {

  ObjectGuid target, caster;
  if (!r.ReadPackedGuid(target) || !r.ReadPackedGuid(caster)) return false;

  std::uint32_t item_id{0}, enchant_id{0};
  if (!r.ReadU32(item_id) || !r.ReadU32(enchant_id)) return false;

  auto item_name = item_definitions.GetItemNameSnapshot(item_id);
  return CombatLog_HandleEnchantLog(
      log,
      target.GetRawValue(),
      caster.GetRawValue(),
      item_id,
      enchant_id,
      item_name.value_or(std::string{}),
      log.TimestampWithOffsetMs(timestamp_offset_ms));
}

void CombatLog_HandleSpellEnergize(ObjectManager& objects,
                                   CombatLog& log,
                                   std::uint64_t entry_source_guid,
                                   std::uint64_t player_check_guid,
                                   std::uint64_t entry_dest_guid,
                                   std::int32_t amount,
                                   std::uint32_t spell_id,
                                   const std::uint32_t timestamp_offset_ms) {
  const auto spell = ResolveCombatLogSpell(spell_id);
  if (!spell.has_value() || !spell->has_dbc_entry) return;

  if ((spell->attributes & 0x180u) != 0u) return;

  if (spell->name.empty()) return;

  CombatLogEntry entry = CombatLog_CreateEntry(
      entry_source_guid, entry_dest_guid, 47, spell_id, spell->name);
  entry.spell_school = spell->school_mask;

  entry.amount = amount;
  entry.overkill = 0;
  entry.school = spell->school_mask;
  entry.resisted = 0;
  entry.blocked = 0;
  entry.absorbed = 0;

  CombatLog_FinalizeEntry(log, entry, timestamp_offset_ms);

  if (CombatLog_IsActivePlayerTarget(player_check_guid)) {
    ObjectGuid target_guid(entry_dest_guid);
    auto* unit = objects.GetMutableUnit(target_guid);
    if (unit) {
      unit->DisplayPowerGain(amount);
    }
  }
}

void CombatLog_HandleBuildingHeal(ObjectManager& objects,
                                  CombatLog& log,
                                  std::uint64_t entry_source_guid,
                                  std::uint64_t player_check_guid,
                                  std::uint64_t entry_dest_guid,
                                  std::int32_t amount,
                                  std::uint32_t spell_id,
                                  const std::uint32_t timestamp_offset_ms) {
  const auto spell = ResolveCombatLogSpell(spell_id);
  if (!spell.has_value() || !spell->has_dbc_entry) return;

  if ((spell->attributes & 0x180u) != 0u) return;

  if (spell->name.empty()) return;

  CombatLogEntry entry = CombatLog_CreateEntry(
      entry_source_guid, entry_dest_guid, 48, spell_id, spell->name);
  entry.spell_school = spell->school_mask;

  entry.amount = amount;
  entry.overheal = 0;
  entry.absorbed = 0;
  entry.critical = false;

  CombatLog_FinalizeEntry(log, entry, timestamp_offset_ms);

  if (CombatLog_IsActivePlayerTarget(player_check_guid)) {
    ObjectGuid target_guid(entry_dest_guid);
    auto* unit = objects.GetMutableUnit(target_guid);
    if (unit) {
      unit->DisplayHealing(amount);
    }
  }
}

bool CombatLog_HandleHealOpcode(ObjectManager& objects,
                                CombatLog& log,
                                const std::uint8_t* data,
                                std::size_t len,
                                const std::uint32_t timestamp_offset_ms) {
  PacketReader r(data, len);

  return CombatLog_HandleHealOpcode(objects, log, r, timestamp_offset_ms);
}

bool CombatLog_HandleHealOpcode(ObjectManager& objects,
                                CombatLog& log,
                                PacketReader& r,
                                const std::uint32_t timestamp_offset_ms) {

  ObjectGuid guid1, guid2, guid3;
  if (!r.ReadPackedGuid(guid1)) return false;
  if (!r.ReadPackedGuid(guid2)) return false;
  if (!r.ReadPackedGuid(guid3)) return false;

  std::int32_t amount{0};
  std::uint32_t spell_id{0};
  if (!r.ReadI32(amount) || !r.ReadU32(spell_id)) return false;

  return CombatLog_HandleHealOpcode(
      objects, log, guid1.GetRawValue(), guid2.GetRawValue(),
      guid3.GetRawValue(), amount, spell_id, timestamp_offset_ms);
}

bool CombatLog_HandleHealOpcode(ObjectManager& objects,
                                CombatLog& log,
                                const std::uint64_t target_guid,
                                const std::uint64_t caster_guid,
                                const std::uint64_t owner_guid,
                                const std::int32_t amount,
                                const std::uint32_t spell_id,
                                const std::uint32_t timestamp_offset_ms) {

  const auto src = caster_guid;
  const auto chk = owner_guid;
  const auto dst = target_guid;

  if (amount > 0) {
    CombatLog_HandleSpellEnergize(objects, log, src, chk, dst, amount,
                                  spell_id, timestamp_offset_ms);
  } else {
    CombatLog_HandleBuildingHeal(objects, log, src, chk, dst, -amount,
                                 spell_id, timestamp_offset_ms);
  }
  return true;
}

void CombatLog_UnregisterLuaFunctions() {
}

void CombatLog_Shutdown(CombatLog& log) {

  log.Reset();
  log.ResetEventFilters();
}

bool SpellCombatLog_ReadFromPacket(SpellCombatLogData& out,
                                   const std::uint8_t* data,
                                   std::size_t len,
                                   std::size_t& bytes_read) {
  PacketReader r(data, len);

  if (!r.ReadU32(out.hit_flags)) return false;

  {
    ObjectGuid g;
    if (!r.ReadPackedGuid(g)) return false;
    out.attacker_guid = g.GetRawValue();
  }

  {
    ObjectGuid g;
    if (!r.ReadPackedGuid(g)) return false;
    out.victim_guid = g.GetRawValue();
  }

  if (!r.ReadU32(out.total_damage) || !r.ReadU32(out.overkill)) return false;

  std::uint8_t num_schools{0};
  if (!r.ReadU8(num_schools)) return false;

  for (std::uint8_t i = 0; i < num_schools && i < 2; ++i) {
    if (!r.ReadU32(out.damage[i])) return false;
    float absorb_pct;
    if (!r.ReadFloat(absorb_pct)) return false;
    out.absorb_pct[i] = absorb_pct;
    if (!r.ReadU32(out.resist[i])) return false;
  }

  if ((out.hit_flags & 0x60) != 0) {
    for (std::uint8_t i = 0; i < num_schools && i < 2; ++i) {
      if (!r.ReadU32(out.absorbed[i])) return false;
    }
  }

  if ((out.hit_flags & 0x180) != 0) {
    for (std::uint8_t i = 0; i < num_schools && i < 2; ++i) {
      if (!r.ReadU32(out.resisted[i])) return false;
    }
  }

  std::uint8_t victim_state{0};
  if (!r.ReadU8(victim_state)) return false;
  out.victim_state = victim_state;

  if (!r.ReadU32(out.attacker_state) || !r.ReadU32(out.melee_spell_id))
    return false;

  if ((out.hit_flags & 0x2000) != 0) {
    if (!r.ReadU32(out.blocked)) return false;
  }

  if ((out.hit_flags & 0x800000) != 0) {
    if (!r.ReadU32(out.rage_gained)) return false;
  }

  if ((out.hit_flags & 1) != 0) {
    if (!r.ReadU32(out.ext_unk)) return false;
    for (int i = 0; i < 8; ++i) {
      if (!r.ReadFloat(out.ext_floats[i])) return false;
    }
    for (int i = 0; i < 4; ++i) {
      if (!r.ReadFloat(out.ext_rolls[i])) return false;
    }
    if (!r.ReadU32(out.ext_extra)) return false;
  }

  bytes_read = r.Position();
  return true;
}

bool SpellCombatLog_ShouldLogSwing(const SpellCombatLogData& data) {

  return data.melee_spell_id == 0 || data.victim_state == 1;
}

namespace {

const char* const kCombatTextMsgTypes[kCombatTextMsgTypeCount] = {
     "INTERRUPT",
     "DAMAGE_CRIT",
     "DAMAGE",
     "MISS",
     "DODGE",
     "PARRY",
     "EVADE",
     "IMMUNE",
     "DEFLECT",
     "REFLECT",
     "RESIST",
     "BLOCK",
     "ABSORB",
     "SPELL_DAMAGE_CRIT",
     "SPELL_DAMAGE",
     "SPELL_MISS",
     "SPELL_DODGE",
     "SPELL_PARRY",
     "SPELL_EVADE",
     "SPELL_IMMUNE",
     "SPELL_DEFLECT",
     "SPELL_REFLECT",
     "SPELL_RESIST",
     "SPELL_BLOCK",
     "SPELL_ABSORB",
     "ENCHANTMENT_REMOVED",
     "ENCHANTMENT_ADDED",
     "PERIODIC_HEAL",
     "ENERGIZE",
     "PERIODIC_ENERGIZE",
     "SPELL_CAST",
     "SPELL_AURA_END",
     "SPELL_AURA_END_HARMFUL",
     "SPELL_AURA_START",
     "SPELL_AURA_START_HARMFUL",
     "SPELL_ACTIVE",
     "FACTION",
     "HEAL_CRIT",
     "HEAL",
     "DAMAGE_SHIELD",
     "SPELL_DISPELLED",
     "EXTRA_ATTACKS",
     "SPLIT_DAMAGE",
     "HONOR_GAINED",
     "PERIODIC_HEAL_ABSORB",
     "HEAL_CRIT_ABSORB",
     "HEAL_ABSORB",
     "ARENA_POINTS_GAINED",
};

}

const char* CombatTextMsgType_GetString(std::uint32_t index) {
  if (index >= kCombatTextMsgTypeCount) return nullptr;
  return kCombatTextMsgTypes[index];
}

bool CombatLog_IsActivePlayerTarget(std::uint64_t guid) {
  const auto active = CombatText_GetActiveUnitGuid();
  return guid == active;
}

void CombatLog_FireCombatTextSSD(std::uint32_t msg_type_index,
                                 const char* name,
                                 std::int32_t amount) {
  const char* type_str = CombatTextMsgType_GetString(msg_type_index);
  if (!type_str) return;
  auto& dispatcher = ui::game::ScriptEventDispatch::Get();
  dispatcher.FireCombatTextUpdate(type_str, name ? name : "", amount);
}

void CombatLog_FireCombatTextSSDD(std::uint32_t msg_type_index,
                                  const char* name,
                                  std::int32_t amount,
                                  std::int32_t extra_amount) {
  const char* type_str = CombatTextMsgType_GetString(msg_type_index);
  if (!type_str) return;
  auto& dispatcher = ui::game::ScriptEventDispatch::Get();
  dispatcher.FireCombatTextUpdate(type_str, name ? name : "",
                                  amount, extra_amount);
}

void CombatLog_FireCombatTextSDD(std::uint32_t msg_type_index,
                                 std::int32_t amount,
                                 std::int32_t extra_amount) {
  const char* type_str = CombatTextMsgType_GetString(msg_type_index);
  if (!type_str) return;
  auto& dispatcher = ui::game::ScriptEventDispatch::Get();
  dispatcher.FireCombatTextUpdate(type_str, amount, extra_amount);
}

void CombatLog_FireCombatTextSD(std::uint32_t msg_type_index,
                                std::int32_t amount) {
  const char* type_str = CombatTextMsgType_GetString(msg_type_index);
  if (!type_str) return;
  auto& dispatcher = ui::game::ScriptEventDispatch::Get();
  dispatcher.FireCombatTextUpdate(type_str, amount);
}

void CombatLog_FireCombatTextSS(std::uint32_t msg_type_index,
                                const char* name) {
  const char* type_str = CombatTextMsgType_GetString(msg_type_index);
  if (!type_str) return;
  auto& dispatcher = ui::game::ScriptEventDispatch::Get();
  dispatcher.FireCombatTextUpdate(type_str, name ? name : "");
}

void CombatLog_FireCombatTextSDS(std::uint32_t msg_type_index,
                                 std::int32_t amount,
                                 const char* power_type_string) {
  const char* type_str = CombatTextMsgType_GetString(msg_type_index);
  if (!type_str) return;
  auto& dispatcher = ui::game::ScriptEventDispatch::Get();
  dispatcher.FireCombatTextUpdate(type_str, amount,
                                  power_type_string ? power_type_string : "");
}

void CombatLog_ProcessSpellHealDisplay(
    ObjectManager& objects,
    std::uint64_t source_guid,
    std::uint64_t target_guid,
    std::uint32_t spell_id,
    std::int32_t heal_amount,
    std::int32_t overheal,
    std::int32_t absorb,
    bool critical,
    bool periodic,
    std::uint32_t ) {
  if (heal_amount == 0 && overheal == 0 && absorb == 0) {
    return;
  }

  if (heal_amount != 0) {
    ObjectGuid tgt(target_guid);
    if (auto* unit = objects.GetMutableUnit(tgt)) {
      (void)unit->Vitals().AdjustHealth(*unit, heal_amount);
    }
  }

  const auto spell = ResolveCombatLogSpell(spell_id);
  if (!spell.has_value() || !spell->has_dbc_entry) {
    return;
  }
  if ((spell->attributes & 0x180u) != 0u) {
    return;
  }
  if (spell->name.empty()) {
    return;
  }

  if (!CombatLog_IsActivePlayerTarget(target_guid)) {
    return;
  }

  const std::string source_name = CombatLog_ResolveName(source_guid);
  const char* name = source_name.c_str();

  if (periodic) {
    if (absorb != 0) {
      CombatLog_FireCombatTextSSDD(CombatTextMsgIdx::kPeriodicHealAbsorb,
                                   name, heal_amount, absorb);
    } else {
      CombatLog_FireCombatTextSSD(CombatTextMsgIdx::kPeriodicHeal,
                                  name, heal_amount);
    }
  } else {
    if (absorb != 0) {
      const auto idx = critical ? CombatTextMsgIdx::kHealCritAbsorb
                                : CombatTextMsgIdx::kHealAbsorb;
      CombatLog_FireCombatTextSSDD(idx, name, heal_amount, absorb);
    } else {
      const auto idx = critical ? CombatTextMsgIdx::kHealCrit
                                : CombatTextMsgIdx::kHeal;
      CombatLog_FireCombatTextSSD(idx, name, heal_amount);
    }
  }
}

void CombatLog_CreateEnergizeEntry(CombatLog& log,
                                   std::uint64_t source_guid,
                                   std::uint32_t power_type,
                                   bool periodic,
                                   std::uint64_t dest_guid,
                                   std::uint32_t spell_id,
                                   const std::string& spell_name,
                                   std::int32_t raw_amount,
                                   std::uint32_t timestamp_offset) {
  const std::uint32_t divisor = PowerType_GetDisplayValueDivisor(
      static_cast<std::int32_t>(power_type));
  const std::int32_t display_amount =
      (divisor > 0) ? (raw_amount / static_cast<std::int32_t>(divisor))
                     : raw_amount;

  const std::uint32_t event_index = periodic ? 36u : 11u;
  CombatLogEntry entry = CombatLog_CreateEntry(
      source_guid, dest_guid, event_index, spell_id, spell_name);

  entry.type = periodic ? CombatLogEventType::SPELL_PERIODIC_ENERGIZE
                        : CombatLogEventType::SPELL_ENERGIZE;

  entry.power_amount = display_amount;
  entry.power_type = static_cast<std::int32_t>(power_type);

  CombatLog_FinalizeEntry(log, entry, timestamp_offset);

  if (CombatLog_IsActivePlayerTarget(dest_guid)) {
    const char* power_str = PowerTypeToString(power_type);
    const std::uint32_t text_idx =
        periodic ? CombatTextMsgIdx::kPeriodicEnergize
                 : CombatTextMsgIdx::kEnergize;
    CombatLog_FireCombatTextSDS(text_idx, display_amount, power_str);
  }
}

void CombatLog_CreateDrainEntry(CombatLog& log,
                                std::uint64_t target_guid,
                                CombatLogEventType event_type,
                                std::uint64_t caster_guid,
                                std::uint32_t spell_id,
                                const std::string& spell_name,
                                std::uint32_t drain_amount,
                                std::uint32_t power_type,
                                std::int32_t energize_amount,
                                std::uint32_t timestamp_offset) {
  const std::uint32_t divisor = PowerType_GetDisplayValueDivisor(
      static_cast<std::int32_t>(power_type));
  const std::int32_t display_drain =
      (divisor > 0) ? static_cast<std::int32_t>(drain_amount / divisor)
                    : static_cast<std::int32_t>(drain_amount);
  const std::int32_t display_energize =
      (divisor > 0) ? (energize_amount / static_cast<std::int32_t>(divisor))
                    : energize_amount;

  std::uint32_t event_index;
  switch (event_type) {
    case CombatLogEventType::SPELL_DRAIN:
      event_index = 12u;
      break;
    case CombatLogEventType::SPELL_LEECH:
      event_index = 13u;
      break;
    case CombatLogEventType::SPELL_PERIODIC_DRAIN:
      event_index = 37u;
      break;
    case CombatLogEventType::SPELL_PERIODIC_LEECH:
      event_index = 38u;
      break;
    default:

      return;
  }
  CombatLogEntry entry = CombatLog_CreateEntry(
      caster_guid, target_guid, event_index, spell_id, spell_name);
  entry.type = event_type;

  entry.power_amount = display_drain;
  entry.power_type = static_cast<std::int32_t>(power_type);
  entry.energize_amount = display_energize;

  CombatLog_FinalizeEntry(log, entry, timestamp_offset);

  if (CombatLog_IsActivePlayerTarget(target_guid) && display_drain != 0) {
    const char* power_str = PowerTypeToString(power_type);
    CombatLog_FireCombatTextSDS(CombatTextMsgIdx::kEnergize,
                                -display_drain, power_str);
  }

  if (CombatLog_IsActivePlayerTarget(caster_guid) && display_energize != 0) {
    const char* power_str = PowerTypeToString(power_type);
    const bool periodic_leech =
        (event_type == CombatLogEventType::SPELL_PERIODIC_LEECH);
    const std::uint32_t text_idx =
        periodic_leech ? CombatTextMsgIdx::kPeriodicEnergize
                       : CombatTextMsgIdx::kEnergize;
    CombatLog_FireCombatTextSDS(text_idx, display_energize, power_str);
  }
}

void CombatLog_ProcessSplitDamage(CombatLog& log,
                                  const std::uint64_t source_guid,
                                  const std::uint64_t target_guid,
                                  const std::uint32_t spell_id,
                                  const std::int32_t damage,
                                  const std::int32_t overkill,
                                  const std::int32_t school_mask,
                                  const std::int32_t absorb,
                                  const std::int32_t resist,
                                  const std::int32_t blocked,
                                  const bool critical,
                                  const std::uint32_t timestamp_offset) {

  const auto spell = ResolveCombatLogSpell(spell_id);
  if (!spell.has_value() || !spell->has_dbc_entry) return;

  if ((spell->attributes & 0x180u) != 0u) return;

  if (spell->name.empty()) return;

  CombatLogEntry entry = CombatLog_CreateEntry(
      source_guid, target_guid, 42, spell_id, spell->name);
  entry.type = CombatLogEventType::DAMAGE_SPLIT;
  entry.spell_school = spell->school_mask;
  entry.amount = damage;
  entry.overkill = overkill;
  entry.school = static_cast<std::uint32_t>(school_mask);
  entry.absorbed = absorb;
  entry.resisted = resist;
  entry.blocked = blocked;
  entry.critical = critical;
  entry.glancing = false;
  entry.crushing = false;

  CombatLog_FinalizeEntry(log, entry, timestamp_offset);

  if (CombatLog_IsActivePlayerTarget(target_guid)) {
    CombatLog_FireCombatTextSD(CombatTextMsgIdx::kSplitDamage, damage);
  }
}

}
