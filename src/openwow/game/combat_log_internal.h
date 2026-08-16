
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openwow/game/combat_log.h"
#include "openwow/game/object_guid.h"

namespace openwow::game {

class ItemDefinitions;
class ObjectManager;

bool CombatLog_TestUnitFlags(std::uint32_t filter_flags,
                             std::uint32_t unit_flags);

namespace UnitFlag {

inline constexpr std::uint32_t kAffilMine       = 0x0001;
inline constexpr std::uint32_t kAffilParty      = 0x0002;
inline constexpr std::uint32_t kAffilRaid       = 0x0004;
inline constexpr std::uint32_t kAffilOutsider   = 0x0008;

inline constexpr std::uint32_t kReactionFriendly = 0x0010;
inline constexpr std::uint32_t kReactionNeutral  = 0x0020;
inline constexpr std::uint32_t kReactionHostile  = 0x0040;

inline constexpr std::uint32_t kControlPlayer   = 0x0100;
inline constexpr std::uint32_t kControlNPC      = 0x0200;

inline constexpr std::uint32_t kTypePlayer       = 0x0400;
inline constexpr std::uint32_t kTypeNPC          = 0x0800;
inline constexpr std::uint32_t kTypePet          = 0x1000;
inline constexpr std::uint32_t kTypeGuardian     = 0x2000;
inline constexpr std::uint32_t kTypeObject       = 0x4000;

inline constexpr std::uint32_t kMainTank         = 0x00040000;
inline constexpr std::uint32_t kMainAssist       = 0x00080000;

inline constexpr std::uint32_t kRaidTarget1      = 0x00100000;
inline constexpr std::uint32_t kRaidTarget8      = 0x08000000;

inline constexpr std::uint32_t kNone             = 0x80000000;
}

std::uint32_t CombatLog_BuildUnitFlags(std::uint64_t guid);

inline std::uint32_t CombatLog_BuildUnitFlags(ObjectGuid guid) {
  return CombatLog_BuildUnitFlags(guid.GetRawValue());
}

void CombatLog_FormatGuid(std::uint32_t lo, std::uint32_t hi, char* out);

void CombatLog_AppendHexField(char** buf_pos, std::uint32_t value,
                              std::uint32_t* remaining);

void CombatLog_AppendRawField(char** buf_pos, std::uint32_t* remaining,
                              const char* str);

void CombatLog_AppendQuotedField(char** buf_pos, const char* str,
                                 std::uint32_t* remaining);

void CombatLog_ResolveGUID(std::uint64_t& guid);

std::string CombatLog_ResolveName(std::uint64_t guid);

std::string CombatLog_BuildNameForGUID(std::uint64_t guid);

CombatLogEntry CombatLog_CreateEntry(std::uint64_t source_guid,
                                     std::uint64_t dest_guid,
                                     std::uint32_t event_index,
                                     std::uint32_t spell_id = 0,
                                     const std::string& spell_name = "");

void CombatLog_FinalizeEntry(CombatLog& log, CombatLogEntry& entry,
                             std::uint32_t timestamp_offset = 0);

void CombatLogEntry_Reset(CombatLogEntry& entry);

void CombatLogEntry_SetTimestamp(CombatLogEntry& entry,
                                 std::uint32_t server_time);

void CombatLog_InvalidateNameCache(const char* old_name);

bool CombatLogFilter_SetFilterCriteria(
    CombatLogEventFilter& filter,
    const char* event_list,
    std::uint64_t src_guid, std::uint32_t src_flags,
    std::uint64_t dst_guid, std::uint32_t dst_flags,
    std::uint32_t spell_id, const char* spell_name);

bool CombatLogFilter_TestSingleFilter(
    const CombatLogEventFilter& filter,
    const CombatLogEntry& entry);

bool CombatLogFilter_MatchEntry(
    const std::vector<CombatLogEventFilter>& filters,
    const CombatLogEntry& entry);

bool CombatLog_ShouldShowSpellMechanics(std::uint32_t spell_flags,
                                        std::uint64_t source_guid);

CombatLogEventType MapEventIndex(std::uint32_t index);

std::uint32_t CombatLog_DefaultSuffixFlags(const CombatLogEntry& entry);

bool CombatLog_EventHasSpellPrefix(CombatLogEventType type);

bool CombatLog_EventHasEnchantNameSuffix(CombatLogEventType type);

const std::string& CombatLog_GetFilterName(const CombatLogEntry& entry);

bool CombatLog_HandlePartyKillOpcode(CombatLog& log,
                                     const std::uint8_t* data,
                                     std::size_t len);
bool CombatLog_HandlePartyKillOpcode(CombatLog& log,
                                     PacketReader& reader,
                                     std::uint32_t timestamp_offset_ms = 0);

bool CombatLog_HandlePartyKill(CombatLog& log,
                               std::uint64_t killer_guid,
                               std::uint64_t victim_guid,
                               std::uint32_t timestamp_offset_ms = 0);

bool CombatLog_HandleEnchantLog(CombatLog& log,
                                std::uint64_t target_guid,
                                std::uint64_t caster_guid,
                                std::uint32_t item_id,
                                std::uint32_t enchant_id,
                                std::string item_name,
                                double timestamp = 0.0);

bool CombatLog_HandleEnchantOpcode(CombatLog& log,
                                   const ItemDefinitions& item_definitions,
                                   const std::uint8_t* data,
                                   std::size_t len);
bool CombatLog_HandleEnchantOpcode(CombatLog& log,
                                   const ItemDefinitions& item_definitions,
                                   PacketReader& reader,
                                   std::uint32_t timestamp_offset_ms = 0);

void CombatLog_HandleSpellEnergize(ObjectManager& objects,
                                   CombatLog& log,
                                   std::uint64_t entry_source_guid,
                                   std::uint64_t player_check_guid,
                                   std::uint64_t entry_dest_guid,
                                   std::int32_t amount,
                                   std::uint32_t spell_id,
                                   std::uint32_t timestamp_offset_ms = 0);

void CombatLog_HandleBuildingHeal(ObjectManager& objects,
                                  CombatLog& log,
                                  std::uint64_t entry_source_guid,
                                  std::uint64_t player_check_guid,
                                  std::uint64_t entry_dest_guid,
                                  std::int32_t amount,
                                  std::uint32_t spell_id,
                                  std::uint32_t timestamp_offset_ms = 0);

bool CombatLog_HandleHealOpcode(ObjectManager& objects,
                                CombatLog& log,
                                const std::uint8_t* data,
                                std::size_t len,
                                std::uint32_t timestamp_offset_ms = 0);
bool CombatLog_HandleHealOpcode(ObjectManager& objects,
                                CombatLog& log,
                                PacketReader& reader,
                                std::uint32_t timestamp_offset_ms = 0);

bool CombatLog_HandleHealOpcode(ObjectManager& objects,
                                CombatLog& log,
                                std::uint64_t target_guid,
                                std::uint64_t caster_guid,
                                std::uint64_t owner_guid,
                                std::int32_t amount,
                                std::uint32_t spell_id,
                                std::uint32_t timestamp_offset_ms = 0);

void CombatLog_UnregisterLuaFunctions();

void CombatLog_Shutdown(CombatLog& log);

inline constexpr std::size_t kCombatTextMsgTypeCount = 48;

const char* CombatTextMsgType_GetString(std::uint32_t index);

namespace CombatTextMsgIdx {
inline constexpr std::uint32_t kInterrupt              = 0;
inline constexpr std::uint32_t kDamageCrit             = 1;
inline constexpr std::uint32_t kDamage                 = 2;
inline constexpr std::uint32_t kMiss                   = 3;
inline constexpr std::uint32_t kDodge                  = 4;
inline constexpr std::uint32_t kParry                  = 5;
inline constexpr std::uint32_t kEvade                  = 6;
inline constexpr std::uint32_t kImmune                 = 7;
inline constexpr std::uint32_t kDeflect                = 8;
inline constexpr std::uint32_t kReflect                = 9;
inline constexpr std::uint32_t kResist                 = 10;
inline constexpr std::uint32_t kBlock                  = 11;
inline constexpr std::uint32_t kAbsorb                 = 12;
inline constexpr std::uint32_t kSpellDamageCrit        = 13;
inline constexpr std::uint32_t kSpellDamage            = 14;
inline constexpr std::uint32_t kSpellMiss              = 15;
inline constexpr std::uint32_t kSpellDodge             = 16;
inline constexpr std::uint32_t kSpellParry             = 17;
inline constexpr std::uint32_t kSpellEvade             = 18;
inline constexpr std::uint32_t kSpellImmune            = 19;
inline constexpr std::uint32_t kSpellDeflect           = 20;
inline constexpr std::uint32_t kSpellReflect           = 21;
inline constexpr std::uint32_t kSpellResist            = 22;
inline constexpr std::uint32_t kSpellBlock             = 23;
inline constexpr std::uint32_t kSpellAbsorb            = 24;
inline constexpr std::uint32_t kEnchantmentRemoved     = 25;
inline constexpr std::uint32_t kEnchantmentAdded       = 26;
inline constexpr std::uint32_t kPeriodicHeal           = 27;
inline constexpr std::uint32_t kEnergize               = 28;
inline constexpr std::uint32_t kPeriodicEnergize       = 29;
inline constexpr std::uint32_t kSpellCast              = 30;
inline constexpr std::uint32_t kSpellAuraEnd           = 31;
inline constexpr std::uint32_t kSpellAuraEndHarmful    = 32;
inline constexpr std::uint32_t kSpellAuraStart         = 33;
inline constexpr std::uint32_t kSpellAuraStartHarmful  = 34;
inline constexpr std::uint32_t kSpellActive            = 35;
inline constexpr std::uint32_t kFaction                = 36;
inline constexpr std::uint32_t kHealCrit               = 37;
inline constexpr std::uint32_t kHeal                   = 38;
inline constexpr std::uint32_t kDamageShield           = 39;
inline constexpr std::uint32_t kSpellDispelled         = 40;
inline constexpr std::uint32_t kExtraAttacks           = 41;
inline constexpr std::uint32_t kSplitDamage            = 42;
inline constexpr std::uint32_t kHonorGained            = 43;
inline constexpr std::uint32_t kPeriodicHealAbsorb     = 44;
inline constexpr std::uint32_t kHealCritAbsorb         = 45;
inline constexpr std::uint32_t kHealAbsorb             = 46;
inline constexpr std::uint32_t kArenaPointsGained      = 47;
}

bool CombatLog_IsActivePlayerTarget(std::uint64_t guid);

void CombatLog_FireCombatTextSSD(std::uint32_t msg_type_index,
                                 const char* name,
                                 std::int32_t amount);

void CombatLog_FireCombatTextSSDD(std::uint32_t msg_type_index,
                                  const char* name,
                                  std::int32_t amount,
                                  std::int32_t extra_amount);

void CombatLog_FireCombatTextSDD(std::uint32_t msg_type_index,
                                 std::int32_t amount,
                                 std::int32_t extra_amount);

void CombatLog_FireCombatTextSD(std::uint32_t msg_type_index,
                                std::int32_t amount);

void CombatLog_FireCombatTextSS(std::uint32_t msg_type_index,
                                const char* name);

void CombatLog_FireCombatTextSDS(std::uint32_t msg_type_index,
                                 std::int32_t amount,
                                 const char* power_type_string);

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
    std::uint32_t timestamp_offset);

void CombatLog_CreateEnergizeEntry(CombatLog& log,
                                   std::uint64_t source_guid,
                                   std::uint32_t power_type,
                                   bool periodic,
                                   std::uint64_t dest_guid,
                                   std::uint32_t spell_id,
                                   const std::string& spell_name,
                                   std::int32_t raw_amount,
                                   std::uint32_t timestamp_offset);

void CombatLog_CreateDrainEntry(CombatLog& log,
                                std::uint64_t target_guid,
                                CombatLogEventType event_type,
                                std::uint64_t caster_guid,
                                std::uint32_t spell_id,
                                const std::string& spell_name,
                                std::uint32_t drain_amount,
                                std::uint32_t power_type,
                                std::int32_t energize_amount,
                                std::uint32_t timestamp_offset);

void CombatLog_ProcessSplitDamage(CombatLog& log,
                                  std::uint64_t source_guid,
                                  std::uint64_t target_guid,
                                  std::uint32_t spell_id,
                                  std::int32_t damage,
                                  std::int32_t overkill,
                                  std::int32_t school_mask,
                                  std::int32_t absorb,
                                  std::int32_t resist,
                                  std::int32_t blocked,
                                  bool critical,
                                  std::uint32_t timestamp_offset);

struct SpellCombatLogData {
  std::uint32_t hit_flags{0};
  std::uint64_t attacker_guid{0};
  std::uint64_t victim_guid{0};
  std::uint32_t total_damage{0};
  std::uint32_t overkill{0};

  std::uint32_t damage[2]{0, 0};
  float         absorb_pct[2]{0, 0};
  std::uint32_t resist[2]{0, 0};
  std::uint32_t absorbed[2]{0, 0};
  std::uint32_t resisted[2]{0, 0};
  std::uint8_t  victim_state{0};
  std::uint32_t attacker_state{0};
  std::uint32_t melee_spell_id{0};
  std::uint32_t blocked{0};
  std::uint32_t rage_gained{0};

  std::uint32_t ext_unk{0};
  float         ext_floats[8]{0};
  float         ext_rolls[4]{0};
  std::uint32_t ext_extra{0};

  void Clear() {
    hit_flags      = 0;
    attacker_guid  = 0;
    victim_guid    = 0;
    total_damage   = 0;
    overkill       = 0;
    damage[0]      = 0;  damage[1]      = 0;
    absorb_pct[0]  = 0;  absorb_pct[1]  = 0;
    resist[0]      = 0;  resist[1]      = 0;
    absorbed[0]    = 0;  absorbed[1]    = 0;
    resisted[0]    = 0;  resisted[1]    = 0;
    victim_state   = 0;
    attacker_state = 0;
    melee_spell_id = 0;
    blocked        = 0;
    rage_gained    = 0;
    ext_unk        = 0;
    for (auto& f : ext_floats) f = 0.0f;
    for (auto& f : ext_rolls)  f = 0.0f;
    ext_extra      = 0;
  }
};

bool SpellCombatLog_ReadFromPacket(SpellCombatLogData& out,
                                   const std::uint8_t* data,
                                   std::size_t len,
                                   std::size_t& bytes_read);

[[nodiscard]] bool SpellCombatLog_ShouldLogSwing(
    const SpellCombatLogData& data);

}
