
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

struct lua_State;

namespace openwow::game {

struct AuraSlotInfo;
struct DispelFailed;
struct SpellBreakLog;
struct SpellDamageShield;
struct SpellDispelLog;
struct SpellInstaKillLog;
struct SpellLogExecuteResurrect;
struct SpellLogExecuteDrain;
struct SpellLogExecuteExtraAttacks;
struct SpellLogExecuteInterrupt;
struct SpellLogExecuteSummon;
struct SpellLogMiss;
struct SpellOrDamageImmune;
struct ProcResist;
struct SpellLogExecuteDurabilityDamage;
struct SpellLogExecuteDurabilityDamageAll;
struct AttackerStateUpdate;
class ObjectManager;

enum class HitInfo : std::uint32_t {
  kNormalSwing = 0x00000000,
  kAffectsVictim = 0x00000002,
  kOffhand = 0x00000004,
  kMiss = 0x00000010,
  kFullAbsorb = 0x00000020,
  kPartialAbsorb = 0x00000040,
  kFullResist = 0x00000080,
  kPartialResist = 0x00000100,
  kCriticalHit = 0x00000200,
  kBlock = 0x00002000,
  kGlancing = 0x00010000,
  kCrushing = 0x00020000,
  kNoAnimation = 0x00040000,
  kRageGain = 0x00800000,
};

namespace SpellHitType {
inline constexpr std::uint32_t kSwingExt    = 0x01;
inline constexpr std::uint32_t kCrit        = 0x02;
inline constexpr std::uint32_t kRangedExt   = 0x04;

inline constexpr std::uint32_t kSplit       = 0x08;
inline constexpr std::uint32_t kMiss        = 0x10;
inline constexpr std::uint32_t kFullExt     = 0x20;
}

inline HitInfo operator|(HitInfo a, HitInfo b) {
  return static_cast<HitInfo>(
      static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline HitInfo operator&(HitInfo a, HitInfo b) {
  return static_cast<HitInfo>(
      static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasHitInfo(HitInfo val, HitInfo flag) {
  return (static_cast<uint32_t>(val) & static_cast<uint32_t>(flag)) != 0;
}

enum class CombatEventType : std::uint8_t {
  kMeleeAttack,
  kSpellDamage,
  kSpellMiss,
  kSpellHeal,
  kSpellEnergize,
  kPeriodicDamage,
  kPeriodicHeal,
  kPeriodicEnergize,
  kSpellMechanic,
  kXpGain,
  kAttackStart,
  kAttackStop,

  kBuildingDamage,
  kBuildingHeal,

  kHonorKill,
};

enum class MissType : std::uint8_t {
  Miss,
  Dodge,
  Parry,
  Block,
  Resist,
  Absorb,
  Deflect,
  Immune,
  Evade,
  Reflect,
};

struct CombatEvent {
  CombatEventType type;
  ObjectGuid source;
  ObjectGuid target;

  std::uint32_t spell_id = 0;
  std::uint32_t amount = 0;
  std::uint32_t overkill = 0;
  std::uint8_t school_mask = 0;
  std::uint32_t absorb = 0;
  std::uint32_t resist = 0;
  std::uint32_t blocked = 0;
  bool critical = false;
  MissType miss_type = MissType::Miss;

  std::uint8_t xp_type = 0;
  float group_rate = 1.0f;

  std::uint32_t power_type = 0;
  bool power_drain = false;

  std::uint32_t honor_rank = 0;
  std::string honor_rank_title;

  std::string mechanic_text;

  HitInfo hit_info = HitInfo::kNormalSwing;
};

enum class CombatLogEventType : std::uint8_t {
  SWING_DAMAGE,
  SWING_MISSED,
  RANGE_DAMAGE,
  RANGE_MISSED,
  SPELL_DAMAGE,
  SPELL_MISSED,
  SPELL_HEAL,
  SPELL_ENERGIZE,
  SPELL_DRAIN,
  SPELL_LEECH,
  SPELL_PERIODIC_DAMAGE,
  SPELL_PERIODIC_HEAL,
  SPELL_PERIODIC_ENERGIZE,
  SPELL_PERIODIC_DRAIN,
  SPELL_PERIODIC_LEECH,
  SPELL_PERIODIC_MISSED,
  SPELL_AURA_APPLIED,
  SPELL_AURA_REMOVED,
  SPELL_AURA_APPLIED_DOSE,
  SPELL_AURA_REMOVED_DOSE,
  SPELL_AURA_REFRESH,
  SPELL_AURA_BROKEN,
  SPELL_AURA_BROKEN_SPELL,
  SPELL_CAST_START,
  SPELL_CAST_SUCCESS,
  SPELL_CAST_FAILED,
  SPELL_INTERRUPT,
  SPELL_DISPEL,
  SPELL_STOLEN,
  SPELL_EXTRA_ATTACKS,
  SPELL_INSTAKILL,
  SPELL_DURABILITY_DAMAGE,
  SPELL_CREATE,
  SPELL_SUMMON,
  SPELL_RESURRECT,
  DAMAGE_SHIELD,
  DAMAGE_SHIELD_MISSED,
  DAMAGE_SPLIT,
  PARTY_KILL,
  UNIT_DIED,
  UNIT_DESTROYED,
  ENVIRONMENTAL_DAMAGE,

  SPELL_DURABILITY_DAMAGE_ALL,
  DAMAGE_AURA_BROKEN,
  ENCHANT_APPLIED,
  ENCHANT_REMOVED,
  SPELL_DISPEL_FAILED,
  SPELL_BUILDING_DAMAGE,
  SPELL_BUILDING_HEAL,
  UNIT_DISSIPATES,

  INVALID,
};

namespace CombatLogSuffixFlag {
inline constexpr std::uint32_t kAmount = 0x0001u;
inline constexpr std::uint32_t kString = 0x0002u;
inline constexpr std::uint32_t kMissType = 0x0004u;

inline constexpr std::uint32_t kEnvironmentalType = 0x0008u;
inline constexpr std::uint32_t kDamage = 0x0010u;
inline constexpr std::uint32_t kHeal = 0x0020u;
inline constexpr std::uint32_t kEnergize = 0x0040u;
inline constexpr std::uint32_t kDrain = 0x0080u;
inline constexpr std::uint32_t kExtraSpell = 0x0100u;
inline constexpr std::uint32_t kNumberAndString = 0x0200u;
inline constexpr std::uint32_t kAuraType = 0x0400u;
inline constexpr std::uint32_t kNumber = 0x0800u;
inline constexpr std::uint32_t kStringAtOffset58 = 0x1000u;

inline constexpr std::uint32_t kAbsorbed = 0x2000u;
inline constexpr std::uint32_t kResisted = 0x4000u;
inline constexpr std::uint32_t kBlocked = 0x8000u;
}

enum class DamageSchool : std::uint8_t {
  Physical = 0,
  Holy     = 1,
  Fire     = 2,
  Nature   = 3,
  Frost    = 4,
  Shadow   = 5,
  Arcane   = 6,
};

struct CombatLogEventFilter {

  std::vector<CombatLogEventType> events;
  bool all_events{true};

  bool     src_any{true};
  std::uint64_t src_guid{0};
  std::uint32_t src_flags{0};

  bool     dst_any{true};
  std::uint64_t dst_guid{0};
  std::uint32_t dst_flags{0};

  std::uint32_t spell_id{0};
  std::string   spell_name;
};

struct CombatLogEntry {
  CombatLogEventType type{CombatLogEventType::SWING_DAMAGE};
  double timestamp{0.0};

  std::uint64_t source_guid{0};
  std::string source_name;

  std::uint32_t source_flags{0x80000000u};
  std::uint32_t source_raid_flags{0};

  std::uint64_t dest_guid{0};
  std::string dest_name;
  std::uint32_t dest_flags{0x80000000u};
  std::uint32_t dest_raid_flags{0};

  std::uint32_t spell_id{0};
  std::string spell_name;
  std::uint32_t spell_school{0};

  std::uint32_t suffix_flags{0};

  std::string enchant_name;
  std::uint32_t enchant_item_id{0};
  std::string enchant_item_name;

  std::int32_t amount{0};
  std::int32_t overkill{-1};
  std::uint32_t school{0};
  std::int32_t resisted{0};
  std::int32_t blocked{0};
  std::int32_t absorbed{0};
  bool critical{false};
  bool glancing{false};
  bool crushing{false};

  std::string miss_type;

  std::int32_t overheal{0};

  std::string aura_type;
  std::int32_t aura_amount{0};

  std::string env_type;

  std::int32_t power_amount{0};
  std::int32_t power_type{0};
  std::int32_t energize_amount{0};

  std::uint32_t extra_spell_id{0};
  std::string extra_spell_name;
  std::uint32_t extra_spell_school{0};

  std::string failed_message;
};

class CombatLog {
 public:
  static constexpr std::size_t kDefaultMaxEntries = 5000;

  static constexpr std::size_t kMaxEntries = 500;

  bool HandleAttackStart(const std::uint8_t* data, std::size_t len);
  bool HandleAttackStop(const std::uint8_t* data, std::size_t len);
  bool HandleSpellNonMeleeDamageLog(const std::uint8_t* data, std::size_t len);
  bool HandleSpellNonMeleeDamageLog(PacketReader& reader,
                                    std::uint32_t timestamp_offset_ms = 0);
  bool HandleSpellHealLog(ObjectManager& objects,
                          const std::uint8_t* data, std::size_t len);
  bool HandleSpellHealLog(ObjectManager& objects, PacketReader& reader,
                          std::uint32_t timestamp_offset_ms = 0);
  bool HandleSpellEnergizeLog(const std::uint8_t* data, std::size_t len);
  bool HandleSpellEnergizeLog(PacketReader& reader,
                              std::uint32_t timestamp_offset_ms = 0);

  void HandleSpellPowerDrain(ObjectManager& objects,
                             std::uint64_t caster_guid,
                             std::uint64_t target_guid,
                             std::uint32_t spell_id,
                             std::uint32_t power_type,
                             std::uint32_t drain_amount,
                             float leech_coefficient,
                             bool is_periodic,
                             std::uint32_t timestamp_offset_ms = 0);
  bool HandleLogXpGain(const std::uint8_t* data, std::size_t len);
  bool HandleAttackerStateUpdate(const std::uint8_t* data, std::size_t len);
  bool HandleAttackerStateUpdate(const AttackerStateUpdate& update,
                                std::uint32_t timestamp_offset_ms = 0);
  bool HandlePeriodicAuraLog(ObjectManager& objects,
                             const std::uint8_t* data, std::size_t len);
  bool HandlePeriodicAuraLog(ObjectManager& objects, PacketReader& reader,
                             std::uint32_t timestamp_offset_ms = 0);
  bool HandleSpellDispelOrSteal(const SpellDispelLog& log);
  bool HandleSpellDispelOrSteal(const SpellDispelLog& log,
                                std::uint32_t timestamp_offset_ms);
  bool HandleSpellAuraBroken(const SpellBreakLog& log);
  bool HandleSpellAuraBroken(const SpellBreakLog& log,
                             std::uint32_t timestamp_offset_ms);
  bool HandleSpellLogExecuteResurrect(const SpellLogExecuteResurrect& log);
  bool HandleSpellLogExecuteResurrect(const SpellLogExecuteResurrect& log,
                                      std::uint32_t timestamp_offset_ms);
  bool HandleSpellLogExecuteExtraAttacks(
      const SpellLogExecuteExtraAttacks& log,
      std::uint32_t timestamp_offset_ms = 0);
  bool HandleSpellLogExecuteInterrupt(
      const SpellLogExecuteInterrupt& log,
      std::uint32_t timestamp_offset_ms = 0);
  bool HandleSpellLogExecuteSummon(
      const SpellLogExecuteSummon& log,
      std::uint32_t timestamp_offset_ms = 0);

  bool HandleSpellLogExecuteDurabilityDamage(const SpellLogExecuteDurabilityDamage& log);
  bool HandleSpellLogExecuteDurabilityDamage(const SpellLogExecuteDurabilityDamage& log,
                                             std::uint32_t timestamp_offset_ms);

  bool HandleSpellLogExecuteDurabilityDamageAll(const SpellLogExecuteDurabilityDamageAll& log);
  bool HandleSpellLogExecuteDurabilityDamageAll(const SpellLogExecuteDurabilityDamageAll& log,
                                                std::uint32_t timestamp_offset_ms);
  bool HandleDispelFailed(const DispelFailed& log,
                          std::uint32_t timestamp_offset_ms = 0);
  bool HandleProcResist(const ProcResist& log,
                        std::uint32_t timestamp_offset_ms = 0);
  bool HandleSpellLogMiss(const SpellLogMiss& log);
  bool HandleSpellLogMiss(const SpellLogMiss& log,
                          std::uint32_t timestamp_offset_ms);
  bool HandleSpellOrDamageImmune(const SpellOrDamageImmune& log,
                                 std::uint32_t timestamp_offset_ms = 0);
  bool HandleSpellInstaKill(const SpellInstaKillLog& log,
                            std::uint32_t timestamp_offset_ms = 0);

  bool HandleSpellDamageShield(const SpellDamageShield& log,
                               std::uint32_t timestamp_offset_ms = 0);
  bool HandleSpellCastStart(const ObjectManager& objects,
                            std::uint64_t caster_guid,
                            std::uint32_t spell_id,
                            std::uint8_t cast_count,
                            std::uint32_t cast_time_ms);
  bool HandleSpellCastSuccess(const ObjectManager& objects,
                              std::uint64_t caster_guid,
                              std::uint64_t target_guid,
                              std::uint32_t spell_id,
                              std::uint8_t cast_count);
  bool HandleSpellCastFailed(std::uint64_t caster_guid,
                             std::uint32_t spell_id,
                             const std::string& failed_message);
  bool HandleAuraStateTransition(std::uint64_t target_guid,
                                 const AuraSlotInfo* old_aura,
                                 const AuraSlotInfo* new_aura,
                                 std::size_t active_aura_count);
  bool HandleEnvironmentalDamageLog(const std::uint8_t* data, std::size_t len);
  bool HandleEnvironmentalDamageLog(PacketReader& reader,
                                    std::uint32_t timestamp_offset_ms = 0);

  [[nodiscard]] const std::deque<CombatEvent>& events() const {
    return events_;
  }
  [[nodiscard]] std::size_t event_count() const { return events_.size(); }

  using OnEventFn = std::function<void(const CombatEvent&)>;
  void SetOnEvent(OnEventFn fn) { on_event_ = std::move(fn); }

  using ObjectManagerProvider = std::function<ObjectManager*()>;
  void SetObjectManagerProvider(ObjectManagerProvider provider) {
    object_manager_provider_ = std::move(provider);
  }

  void AddLogEntry(CombatLogEntry entry);

  [[nodiscard]] const std::deque<CombatLogEntry>& log_entries() const {
    return log_entries_;
  }

  [[nodiscard]] std::size_t log_entry_count() const {
    return log_entries_.size();
  }

  [[nodiscard]] std::uint64_t log_entry_serial() const {
    return log_entry_serial_;
  }

  [[nodiscard]] const CombatLogEntry* current_entry() const;

  bool AdvanceEntry();

  [[nodiscard]] std::size_t current_index() const { return current_index_; }

  bool SetCurrentIndex(std::size_t idx) {
    current_index_ = idx;
    return idx < log_entries_.size();
  }

  void SetMaxLogEntries(std::size_t max) { max_log_entries_ = max; }
  [[nodiscard]] std::size_t max_log_entries() const { return max_log_entries_; }

  void SetRetentionTime(float seconds) { retention_time_s_ = seconds; }
  [[nodiscard]] float retention_time() const { return retention_time_s_; }

  void Update(double currentTime);

  static int PushEntryToLua(lua_State* L, const CombatLogEntry& entry);

  int PushCurrentEntryToLua(lua_State* L) const;

  [[nodiscard]] static const char* GetEventName(CombatLogEventType type);

  void SetTimestampFn(std::function<double()> fn) {
    timestamp_fn_ = std::move(fn);
  }
  [[nodiscard]] double TimestampWithOffsetMs(std::uint32_t offset_ms) const;

  void AddEntry(CombatLogEntry entry) { AddLogEntry(std::move(entry)); }

  [[nodiscard]] std::vector<CombatLogEntry> GetEntries() const;
  [[nodiscard]] std::vector<CombatLogEntry> GetRecentEntries(
      std::uint32_t count) const;
  [[nodiscard]] std::vector<CombatLogEntry> GetEntriesByType(
      CombatLogEventType type) const;
  [[nodiscard]] std::vector<CombatLogEntry> GetEntriesForUnit(
      ObjectGuid guid) const;
  [[nodiscard]] std::uint32_t GetEntryCount() const {
    return static_cast<std::uint32_t>(log_entries_.size());
  }

  void SetMaxEntries(std::uint32_t max) { max_log_entries_ = max; }
  [[nodiscard]] std::uint32_t GetMaxEntries() const {
    return static_cast<std::uint32_t>(max_log_entries_);
  }

  void SetFilter(std::uint32_t filterFlags) { filter_flags_ = filterFlags; }
  [[nodiscard]] std::uint32_t GetFilter() const { return filter_flags_; }
  [[nodiscard]] bool IsFiltered(CombatLogEventType type) const {
    auto idx = static_cast<std::uint32_t>(type);
    return idx < 32 && (filter_flags_ & (1u << idx)) != 0;
  }

  void AddEventFilter(CombatLogEventFilter f);

  void ResetEventFilters();

  [[nodiscard]] bool MatchesEventFilters(const CombatLogEntry& e) const;

  [[nodiscard]] std::size_t event_filter_count() const {
    return event_filters_.size();
  }

  [[nodiscard]] std::size_t CountFilteredEntries(bool ignore_filter = false) const;

  bool SetCurrentEntryLua(int n, bool ignore_filter = false);

  bool AdvanceEntryLua(int count, bool ignore_filter = false);

  [[nodiscard]] std::int64_t GetDamageDone(ObjectGuid source,
                                            float timePeriod) const;
  [[nodiscard]] std::int64_t GetHealingDone(ObjectGuid source,
                                             float timePeriod) const;
  [[nodiscard]] std::int64_t GetDamageTaken(ObjectGuid dest,
                                             float timePeriod) const;

  void Reset();

  void Clear();

  void AddEvent(CombatEvent&& evt);

 private:
  std::deque<CombatEvent> events_;
  OnEventFn on_event_;

  std::deque<CombatLogEntry> log_entries_;
  std::uint64_t log_entry_serial_{0};
  struct PendingSpellCastStartKey {
    std::uint64_t caster_guid = 0;
    std::uint32_t spell_id = 0;
    std::uint8_t cast_count = 0;
  };
  std::deque<PendingSpellCastStartKey> pending_spell_cast_starts_;
  std::size_t max_log_entries_{kDefaultMaxEntries};
  float retention_time_s_{120.0f};
  std::size_t current_index_{0};
  std::uint32_t filter_flags_{0};
  std::vector<CombatLogEventFilter> event_filters_;

  std::function<double()> timestamp_fn_;
  ObjectManagerProvider object_manager_provider_;
  [[nodiscard]] double Now() const;

  [[nodiscard]] CombatLogEntry ToLogEntry(const CombatEvent& evt,
                                          CombatLogEventType type) const;
};

void CombatLog_FireEventString(const char* event_string);

std::uint32_t CombatLog_ParseHexString(const char* hex_string);

[[nodiscard]] const char* CombatLog_GetEnvironmentalDamageTypeName(
    std::uint8_t type);
[[nodiscard]] std::uint32_t CombatLog_GetEnvironmentalDamageSchoolMask(
    std::uint8_t type);

void CombatText_SetActiveUnitGuid(std::uint64_t guid);
[[nodiscard]] std::uint64_t CombatText_GetActiveUnitGuid();
[[nodiscard]] bool CombatText_IsActiveUnit(std::uint64_t guid);

}
