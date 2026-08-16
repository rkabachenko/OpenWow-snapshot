#pragma once

#include "openwow/game/aura_manager.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/pet_manager.h"
#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class ActionAssignmentRuntime;
class CharacterMapRuntime;
class SpellCastRuntime;
namespace actions::held_cursor {
class HeldCursor;
}

struct SpellBookEffects {
  std::function<actions::held_cursor::HeldCursor*()> held_cursor;
  std::function<void(std::uint32_t, bool, std::uint32_t)> spell_learned;
  std::function<void(std::uint32_t)> spell_forgotten;
  std::function<void(std::uint32_t)> multi_cast_slot_mask_changed;
  std::function<void()> finalize_initial_companion_catalog;
  std::function<void()> refresh_active_player_mutation_ui;
  std::function<void()> trainer_spellbook_changed;

  std::function<std::int32_t(const data::dbc::SpellEntry&, SpellModOp,
                             std::int32_t)>
      apply_spell_modifier;

  std::function<std::uint32_t()> main_hand_weapon_delay_ms;

  std::function<void(const PetCooldown&)> insert_pet_cooldown;
};

struct SpellCooldown {
  std::uint32_t spell_id{0};
  std::uint16_t item_id{0};
  std::uint16_t category{0};
  std::uint32_t cooldown_ms{0};
  std::uint32_t category_cooldown_ms{0};

  double start_time_s{0.0};
};

enum class SpellTargetFlag : std::uint32_t {
  kNone           = 0x00000000,
  kUnit           = 0x00000002,
  kUnitRaid       = 0x00000004,
  kUnitParty      = 0x00000008,
  kItem           = 0x00000010,
  kSourceLocation = 0x00000020,
  kDestLocation   = 0x00000040,
  kUnitEnemy      = 0x00000080,
  kUnitAlly       = 0x00000100,
  kCorpseEnemy    = 0x00000200,
  kUnitDead       = 0x00000400,
  kGameobject     = 0x00000800,
  kTradeItem      = 0x00001000,
  kString         = 0x00002000,
  kGoItem         = 0x00004000,
  kCorpseAlly     = 0x00008000,
  kUnitMinipet    = 0x00010000,
  kGlyphSlot      = 0x00020000,
  kDestTarget     = 0x00040000,
};

inline constexpr SpellTargetFlag operator|(SpellTargetFlag a,
                                           SpellTargetFlag b) noexcept {
  return static_cast<SpellTargetFlag>(
      static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline constexpr SpellTargetFlag operator&(SpellTargetFlag a,
                                           SpellTargetFlag b) noexcept {
  return static_cast<SpellTargetFlag>(
      static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline constexpr bool HasFlag(SpellTargetFlag field,
                              SpellTargetFlag flag) noexcept {
  return (static_cast<std::uint32_t>(field) & static_cast<std::uint32_t>(flag)) != 0;
}

struct SpellPacketTargets {
  SpellTargetFlag target_mask{SpellTargetFlag::kNone};
  ObjectGuid unit_target;
  ObjectGuid item_target;
  float src_x{0}, src_y{0}, src_z{0};
  ObjectGuid src_transport;
  float dst_x{0}, dst_y{0}, dst_z{0};
  ObjectGuid dst_transport;
  std::string string_target;
};

struct TotemCreated {
  std::uint8_t slot = 0;
  std::uint64_t totem_guid = 0;
  std::uint32_t duration_ms = 0;
  std::uint32_t spell_id = 0;
};

struct TotemSlotState {
  std::uint8_t slot = 0;
  std::uint64_t totem_guid = 0;
  std::uint32_t spell_id = 0;
  std::uint32_t start_time_ms = 0;
  std::uint32_t duration_ms = 0;

  [[nodiscard]] bool has_totem() const { return totem_guid != 0; }

  [[nodiscard]] std::uint32_t RemainingTimeMs(
      std::uint32_t current_time_ms) const {
    if (!has_totem()) {
      return 0;
    }

    const std::uint64_t expiration_time_ms =
        static_cast<std::uint64_t>(start_time_ms) + duration_ms;
    if (expiration_time_ms <= current_time_ms) {
      return 0;
    }

    return static_cast<std::uint32_t>(expiration_time_ms - current_time_ms);
  }

  void Reset() {
    totem_guid = 0;
    spell_id = 0;
    start_time_ms = 0;
    duration_ms = 0;
  }
};

struct ResumeCastBar {
  ObjectGuid caster{0};
  ObjectGuid target{0};
  std::uint32_t spell_id = 0;

  std::uint32_t time_remaining = 0;

  std::uint32_t cast_time = 0;
};

struct TalentWipeConfirm {
  std::uint64_t npc_guid = 0;
  std::uint32_t cost = 0;
};

struct SpellChainTargets {
  std::uint64_t caster_guid = 0;
  std::uint32_t spell_id = 0;
  std::vector<std::uint64_t> targets;
};

struct DestLocSpellCast {
  ObjectGuid record_guid{0};
  std::uint32_t payload_word_2 = 0;
  std::uint32_t payload_word_3 = 0;
  std::uint32_t spell_id = 0;
  float source_x = 0.0f;
  float source_y = 0.0f;
  float source_z = 0.0f;
  float destination_x = 0.0f;
  float destination_y = 0.0f;
  float destination_z = 0.0f;
  float trajectory_pitch = 0.0f;
  float trajectory_speed = 0.0f;
  std::uint32_t duration_ms = 0;

  std::uint8_t progression_rank = 0;

  std::uint8_t missile_cast_count = 0;
  std::array<std::uint8_t, 6> trailing_bytes{};
};

enum class SpellCooldownFlag : std::uint8_t {
  kNone                = 0x0,
  kIncludeGcd          = 0x1,
  kIncludeEventCooldowns = 0x2,
};

class SpellBook {
 public:
  SpellBook(const openwow::data::dbc::DbcLoader& dbc_loader,
             CharacterMapRuntime& map_runtime,
             SpellCastRuntime& spell_cast_runtime,
             ActionAssignmentRuntime& action_assignments,
             SpellBookEffects effects);

  [[nodiscard]] const openwow::data::dbc::DbcLoader* dbc_loader() const {
    return &dbc_loader_;
  }
  void NotifyMultiCastSlotMaskChanged(std::uint32_t slot_mask) const;

  bool HandleInitialSpells(const std::uint8_t* data, std::size_t len);

  bool HandleLearnedSpell(const std::uint8_t* data, std::size_t len);

  bool HandleRemovedSpell(const std::uint8_t* data, std::size_t len);

  bool HandleSupercededSpell(const std::uint8_t* data, std::size_t len);

  bool HandleSpellCooldown(const std::uint8_t* data, std::size_t len);

  void StartGlobalCooldown(std::uint32_t spell_id);

  void StartPetGlobalCooldown(std::uint32_t spell_id,
                              const ObjectGuid& pet_guid);

  void RecordPetSpellGoCooldown(std::uint32_t spell_id,
                                const ObjectGuid& pet_guid,
                                std::uint32_t cast_flags);
  void CancelGlobalCooldown(std::uint32_t spell_id);
  [[nodiscard]] std::int32_t ApplySpellModifier(
      const data::dbc::SpellEntry& spell, SpellModOp op,
      std::int32_t value) const;

  void InsertInitialSpellCooldown(const SpellCooldown& parsed);
  void InsertGlobalCooldown(std::uint32_t spell_id, std::uint32_t category,
                            std::uint32_t duration_ms, double start_time_s);
  void RecordSuccessfulCastRecovery(std::uint32_t spell_id);
  void ClearCooldown(std::uint32_t spell_id);
  void ModifyCooldown(std::uint32_t spell_id, std::int32_t delta_ms);

  bool HandleTotemCreated(const std::uint8_t* data, std::size_t len,
                          std::uint32_t client_time_ms = 0);
  bool HandleResumeCastBar(const std::uint8_t* data, std::size_t len);
  bool HandleTalentWipeConfirm(const std::uint8_t* data, std::size_t len);
  bool HandleSummonCancel();
  bool HandleSpellUpdateChainTargets(const std::uint8_t* data, std::size_t len);
  bool HandleNotifyDestLocSpellCast(const std::uint8_t* data,
                                    std::size_t len,
                                    std::uint32_t client_time_ms = 0,
                                    std::optional<DestLocSpellCast>* dispatched_record = nullptr);
  bool HandlePetLearnedSpell(const std::uint8_t* data, std::size_t len);
  bool HandlePetUnlearnedSpell(const std::uint8_t* data, std::size_t len);
  void Update(std::uint32_t client_time_ms);

  static net::wotlk::WorldPacket BuildCastSpell(
      std::uint32_t spell_id,
      const ObjectGuid& target = ObjectGuid());

  static net::wotlk::WorldPacket BuildCastSpellFull(
      std::uint32_t spell_id,
      std::uint8_t cast_count,
      const SpellPacketTargets& targets);

  [[nodiscard]] bool HasSpell(std::uint32_t spell_id) const;
  [[nodiscard]] const std::unordered_set<std::uint32_t>& spells() const { return spells_; }
  [[nodiscard]] std::size_t spell_count() const { return spells_.size(); }

  [[nodiscard]] const std::unordered_map<std::uint32_t, SpellCooldown>& cooldowns() const {
    return cooldowns_;
  }
  [[nodiscard]] const std::unordered_map<std::uint32_t, SpellCooldown>&
  category_cooldowns() const {
    return category_cooldowns_;
  }

  [[nodiscard]] const std::unordered_map<std::uint32_t, SpellCooldown>&
  global_cooldowns() const {
    return global_cooldowns_;
  }
  [[nodiscard]] bool IsOnCooldown(std::uint32_t spell_id) const;

  [[nodiscard]] const std::optional<TotemCreated>& last_totem_created() const {
    return last_totem_created_;
  }
  [[nodiscard]] std::optional<TotemSlotState> GetTotemSlot(
      std::uint8_t slot) const;
  [[nodiscard]] std::optional<std::uint8_t> FindTotemSlotByGuid(
      std::uint64_t guid) const;
  [[nodiscard]] bool ClearTotemSlot(std::uint8_t slot);
  [[nodiscard]] std::optional<std::uint8_t> ClearTotemByGuid(
      std::uint64_t guid);
  [[nodiscard]] const std::optional<ResumeCastBar>& last_resume_cast_bar() const {
    return last_resume_cast_bar_;
  }
  [[nodiscard]] const std::optional<TalentWipeConfirm>& last_talent_wipe_confirm() const {
    return last_talent_wipe_confirm_;
  }
  [[nodiscard]] bool summon_cancelled() const { return summon_cancelled_; }
  [[nodiscard]] const std::optional<SpellChainTargets>& last_chain_targets() const {
    return last_chain_targets_;
  }
  [[nodiscard]] const std::optional<DestLocSpellCast>& last_dest_loc_cast() const {
    return last_dest_loc_cast_;
  }
  [[nodiscard]] std::size_t dest_loc_spell_cast_cache_size() const {
    return dest_loc_spell_cast_cache_.size();
  }
  [[nodiscard]] std::uint32_t last_pet_learned_spell() const {
    return last_pet_learned_spell_;
  }
  [[nodiscard]] std::uint32_t last_pet_unlearned_spell() const {
    return last_pet_unlearned_spell_;
  }
  [[nodiscard]] std::size_t pending_initial_spell_count() const {
    return pending_initial_spells_.size();
  }
  [[nodiscard]] bool HasPendingInitialSpell(std::uint32_t spell_id) const;
  void ApplyPendingInitialSpellSideEffects();

  void ApplyPendingSpellbookReinitialization();

  void Clear();

 private:

  [[nodiscard]] std::int32_t ConditionCategoryCooldown(
      const data::dbc::SpellEntry& spell, std::int32_t duration_ms,
      bool pet_list) const;

  [[nodiscard]] float PetSpellHaste(const ObjectGuid& pet_guid) const;

  struct PendingInitialSpell {
    std::uint32_t spell_id{0};
    std::uint16_t slot_id{0};
  };

  std::unordered_set<std::uint32_t> spells_;
  std::unordered_map<std::uint32_t, SpellCooldown> cooldowns_;
  std::unordered_map<std::uint32_t, SpellCooldown> category_cooldowns_;
  std::unordered_map<std::uint32_t, SpellCooldown> global_cooldowns_;
  std::vector<PendingInitialSpell> pending_initial_spells_;

  bool spellbook_reinitialization_pending_ = false;
  const openwow::data::dbc::DbcLoader& dbc_loader_;
  CharacterMapRuntime& map_runtime_;
  SpellCastRuntime& spell_cast_runtime_;
  ActionAssignmentRuntime& action_assignments_;
  SpellBookEffects effects_;

  std::optional<TotemCreated> last_totem_created_;
  std::array<TotemSlotState, 4> totem_slots_{{
      TotemSlotState{0},
      TotemSlotState{1},
      TotemSlotState{2},
      TotemSlotState{3},
  }};
  std::optional<ResumeCastBar> last_resume_cast_bar_;
  std::optional<TalentWipeConfirm> last_talent_wipe_confirm_;
  bool summon_cancelled_ = false;
  std::optional<SpellChainTargets> last_chain_targets_;
  struct DestLocSpellCastCacheEntry {
    std::uint8_t progression_rank = 0;
    std::uint32_t expires_at_ms = 0;
  };
  std::optional<DestLocSpellCast> last_dest_loc_cast_;
  std::unordered_map<std::uint64_t, DestLocSpellCastCacheEntry>
      dest_loc_spell_cast_cache_;
  std::uint32_t last_pet_learned_spell_ = 0;
  std::uint32_t last_pet_unlearned_spell_ = 0;

  static void WriteTargets(net::wotlk::WorldPacket& pkt,
                           const SpellPacketTargets& targets);

public:

  static bool ReadTargets(PacketReader& reader, SpellPacketTargets& out);
};

}
