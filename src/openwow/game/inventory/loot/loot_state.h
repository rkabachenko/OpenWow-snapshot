
#pragma once

#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class ItemDefinitions;
class PlayerInventoryReplica;

struct PendingRollEntry {
  std::uint64_t roll_id = 0;
  std::uint64_t loot_guid = 0;
  std::uint32_t loot_slot = 0;
  std::uint32_t item_id = 0;
  std::uint32_t item_count = 0;
  std::uint32_t random_suffix = 0;
  std::int32_t random_property_id = 0;
  std::uint32_t countdown_ms = 0;
  std::uint8_t roll_vote_mask = 0;
  std::int8_t reason_need = 0;
  std::int8_t reason_greed = 0;
  std::int8_t reason_disenchant = 0;
  std::uint32_t deadline_tick_ms = 0;
  bool item_template_ready = false;
  bool queued = false;
  bool is_visible = false;
  bool response_submitted = false;
  std::uint64_t lifetime_token = 0;
};

struct PendingRollHandle {
  std::uint64_t roll_id = 0;
  std::uint64_t lifetime_token = 0;
};

struct PendingRollStartEvent {
  std::uint64_t roll_id = 0;
  std::uint32_t countdown_ms = 0;
};

enum class PendingRollPrompt : std::uint8_t {
  kNone = 0,
  kConfirmLoot = 1,
  kConfirmDisenchant = 2,
};

struct PendingRollSubmission {
  std::uint64_t roll_id = 0;
  std::uint64_t loot_guid = 0;
  std::uint32_t loot_slot = 0;
  std::uint8_t roll_type = 0;
  PendingRollPrompt prompt = PendingRollPrompt::kNone;
  bool fire_cancel_event = false;
  std::vector<PendingRollStartEvent> start_events;
};

struct PendingRollRemoval {
  std::uint64_t roll_id = 0;
  bool fire_cancel_event = false;
  std::vector<PendingRollStartEvent> start_events;
};

struct PendingRollResetAction {
  std::uint64_t roll_id = 0;
  std::uint64_t loot_guid = 0;
  std::uint32_t loot_slot = 0;
  std::uint8_t roll_type = 0;
  bool fire_cancel_event = false;
};

void ComputePendingRollReasonCodes(PendingRollEntry& entry,
                                   const PlayerInventoryReplica& inventory,
                                   const ItemDefinitions& item_definitions,
                                   const openwow::data::dbc::DbcLoader* dbc);

class LootState {
 public:
  static constexpr std::size_t kMaxMasterLootCandidateSlots = 40;
  static constexpr std::size_t kMaxVisibleLootRolls = 4;

  LootState(PlayerInventoryReplica& inventory, ItemDefinitions& item_definitions)
      : inventory_(inventory), item_definitions_(item_definitions) {}

  void Reset();
  void BindDbc(const openwow::data::dbc::DbcLoader* dbc) { dbc_ = dbc; }

  [[nodiscard]] std::optional<PendingRollStartEvent> AddPendingRoll(
      PendingRollEntry entry);
  void RemovePendingRoll(std::uint64_t roll_id);
  [[nodiscard]] std::optional<PendingRollEntry> GetPendingRoll(
      std::uint64_t roll_id) const;
  [[nodiscard]] std::vector<std::uint64_t> GetActivePendingRollIDs() const;
  [[nodiscard]] std::optional<PendingRollSubmission> SubmitPendingRoll(
      std::uint64_t roll_id, std::uint32_t roll_type, bool is_confirmed);
  [[nodiscard]] std::optional<PendingRollHandle>
  FindPendingRollBySourceAndSlot(std::uint64_t loot_guid,
                                 std::uint32_t loot_slot) const;
  [[nodiscard]] std::optional<PendingRollRemoval>
  CompletePendingRoll(PendingRollHandle handle);
  void DiscardPendingRollAfterCacheFailure(PendingRollHandle handle);
  [[nodiscard]] std::optional<PendingRollRemoval>
  RemovePendingRollBySourceAndSlot(std::uint64_t loot_guid,
                                   std::uint32_t loot_slot);
  [[nodiscard]] std::vector<PendingRollStartEvent> NotifyItemTemplateReady(
      std::uint32_t item_id);
  void NotifyItemTemplateFailed(std::uint32_t item_id);
  [[nodiscard]] std::uint32_t GetPendingRollTimeLeft(
      std::uint64_t roll_id, std::uint32_t now_tick_ms) const;
  [[nodiscard]] std::vector<PendingRollResetAction>
  DrainPendingRollsForReset();
  void ClearPendingRolls();

  void SetMasterLootCandidates(std::vector<std::uint64_t> guids);
  [[nodiscard]] std::vector<std::uint64_t> GetMasterLootCandidates() const;
  [[nodiscard]] std::uint64_t GetMasterLootCandidateGuid(
      std::size_t zero_based_index) const;

  void SetOptOut(bool opt_out);
  [[nodiscard]] bool GetOptOut() const;

  void SetPendingConfirmSlot(int slot);
  [[nodiscard]] int GetPendingConfirmSlot() const;
  void ClearPendingConfirmSlot();

 private:
  PlayerInventoryReplica& inventory_;
  ItemDefinitions& item_definitions_;
  const openwow::data::dbc::DbcLoader* dbc_ = nullptr;

  std::list<PendingRollEntry> pending_rolls_;
  std::uint32_t next_pending_roll_id_ = 0;
  std::uint64_t next_pending_roll_lifetime_token_ = 1;
  std::uint32_t visible_pending_roll_count_ = 0;

  std::vector<std::uint64_t> master_loot_candidates_;
  bool opt_out_ = false;
  int pending_confirm_slot_ = -1;

  [[nodiscard]] std::optional<PendingRollStartEvent> TryActivatePendingRoll(
      PendingRollEntry& entry);
  [[nodiscard]] std::vector<PendingRollStartEvent> PromoteQueuedPendingRolls();
  [[nodiscard]] PendingRollRemoval CompletePendingRollEntry(
      std::list<PendingRollEntry>::iterator entry);
  void ClearPendingRollsInternal();
};

}
