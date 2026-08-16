
#include "openwow/game/inventory/loot/loot_state.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/model/item_instance.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"

#include <algorithm>

namespace openwow::game {

namespace {

std::uint32_t CountLimitCategory(
    const PlayerInventoryReplica& inventory, const ItemDefinitions& items,
    const std::uint32_t category) {
  std::uint32_t count = 0;
  const auto accumulate = [&](const ItemInstance* item) {
    if (item == nullptr || item->IsEmpty()) {
      return;
    }
    const auto* definition = items.GetItem(item->entry);
    if (definition != nullptr &&
        definition->item_limit_category == category) {
      count += item->count;
    }
  };
  for (std::uint16_t slot = 0; slot < InventorySlots::kTotalSlots; ++slot) {
    accumulate(inventory.GetItemInSlot(static_cast<std::uint8_t>(slot)));
  }
  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    if (const auto* contents = inventory.GetBag(bag); contents != nullptr) {
      for (const auto& item : contents->slots) {
        accumulate(&item);
      }
    }
  }
  for (std::uint8_t bag = 0; bag < PlayerInventoryReplica::kMaxBankBags;
       ++bag) {
    if (const auto* contents = inventory.GetBankBag(bag); contents != nullptr) {
      for (const auto& item : contents->slots) {
        accumulate(&item);
      }
    }
  }
  return count;
}

bool CanStoreRollItem(const PlayerInventoryReplica& inventory,
                      const ItemDefinitions& items,
                      const openwow::data::dbc::DbcLoader* dbc,
                      const ItemTemplate& item,
                      const std::uint32_t additional) {
  if (item.max_count != 0 &&
      inventory.CountItemsOfEntry(item.entry) + additional >
          static_cast<std::uint32_t>(item.max_count)) {
    return false;
  }
  if (item.item_limit_category == 0 || dbc == nullptr) {
    return true;
  }
  const auto* category =
      dbc->item_limit_category().LookupEntry(item.item_limit_category);
  return category == nullptr ||
         CountLimitCategory(inventory, items, item.item_limit_category) +
                 additional <=
             category->quantity;
}

}

void ComputePendingRollReasonCodes(PendingRollEntry& entry,
                                   const PlayerInventoryReplica& inventory,
                                   const ItemDefinitions& item_definitions,
                                   const openwow::data::dbc::DbcLoader* dbc) {
  const std::uint8_t vm = entry.roll_vote_mask;
  const ItemTemplate* item = item_definitions.GetItem(entry.item_id);

  if ((vm & 0x02u) != 0u) {
    entry.reason_need = 0;
  } else if (item == nullptr ||
             !CanStoreRollItem(inventory, item_definitions, dbc, *item,
                               entry.item_count)) {
    entry.reason_need = 2;
  } else {
    entry.reason_need = (item->flags & 0x100u) != 0u ? 5 : 1;
  }

  entry.reason_greed = static_cast<std::int8_t>(~(vm >> 1) & 2);

  if (vm & 0x08) {
    entry.reason_disenchant = 0;
  } else {
    const auto de_skill = item != nullptr
                              ? static_cast<std::int32_t>(
                                    item->required_disenchant_skill)
                              : -1;
    entry.reason_disenchant =
        static_cast<std::int8_t>(de_skill < 0 ? 3 : 4);
  }
}

namespace {

constexpr std::uint32_t kBindOnPickup = 1;
constexpr std::uint8_t kDisenchantRollType = 3;
constexpr std::uint32_t kRareQuality = 3;

}

void LootState::Reset() {
  ClearPendingRollsInternal();
  master_loot_candidates_.clear();
  opt_out_ = false;
  pending_confirm_slot_ = -1;
}

std::optional<PendingRollStartEvent> LootState::AddPendingRoll(
    PendingRollEntry entry) {

  if (entry.roll_id == 0) {
    entry.roll_id = next_pending_roll_id_++;
  } else if (entry.roll_id >= next_pending_roll_id_) {
    next_pending_roll_id_ =
        static_cast<std::uint32_t>(entry.roll_id + 1u);
  }

  if (entry.deadline_tick_ms == 0) {
    entry.deadline_tick_ms =
        core::GameClock::GetTickCount32() + entry.countdown_ms;
  }
  entry.queued = false;
  entry.is_visible = false;
  entry.response_submitted = false;
  entry.lifetime_token = next_pending_roll_lifetime_token_++;

  pending_rolls_.push_front(std::move(entry));
  if (pending_rolls_.front().item_template_ready) {
    ComputePendingRollReasonCodes(
        pending_rolls_.front(), inventory_, item_definitions_, dbc_);
  }
  return TryActivatePendingRoll(pending_rolls_.front());
}

void LootState::RemovePendingRoll(std::uint64_t roll_id) {
  const auto it = std::find_if(
      pending_rolls_.begin(), pending_rolls_.end(),
      [roll_id](const PendingRollEntry& entry) {
        return entry.roll_id == roll_id;
      });
  if (it == pending_rolls_.end()) {
    return;
  }

  if (it->is_visible && visible_pending_roll_count_ > 0) {
    --visible_pending_roll_count_;
  }

  pending_rolls_.erase(it);
  (void)PromoteQueuedPendingRolls();
}

std::optional<PendingRollEntry> LootState::GetPendingRoll(
    std::uint64_t roll_id) const {
  const auto it = std::find_if(
      pending_rolls_.begin(), pending_rolls_.end(),
      [roll_id](const PendingRollEntry& entry) {
        return entry.roll_id == roll_id;
      });
  if (it == pending_rolls_.end()) return std::nullopt;
  return *it;
}

std::vector<std::uint64_t> LootState::GetActivePendingRollIDs() const {
  std::vector<std::uint64_t> ids;
  ids.reserve(std::min<std::size_t>(pending_rolls_.size(),
                                    visible_pending_roll_count_));
  for (const PendingRollEntry& entry : pending_rolls_) {
    if (entry.is_visible) {
      ids.push_back(entry.roll_id);
    }
  }
  return ids;
}

std::optional<PendingRollSubmission> LootState::SubmitPendingRoll(
    const std::uint64_t roll_id,
    const std::uint32_t roll_type,
    const bool is_confirmed) {
  const auto it = std::find_if(
      pending_rolls_.begin(), pending_rolls_.end(),
      [roll_id](const PendingRollEntry& entry) {
        return entry.roll_id == roll_id;
      });
  if (it == pending_rolls_.end() || it->response_submitted) {
    return std::nullopt;
  }

  PendingRollEntry& entry = *it;
  PendingRollSubmission submission;
  submission.roll_id = roll_id;
  submission.loot_guid = entry.loot_guid;
  submission.loot_slot = entry.loot_slot;
  submission.roll_type = static_cast<std::uint8_t>(roll_type);

  if (roll_type != 0 && !is_confirmed) {
    if (const ItemTemplate* item = item_definitions_.GetItem(entry.item_id);
        item != nullptr) {
      if (roll_type == kDisenchantRollType &&
          static_cast<std::uint32_t>(item->quality) >= kRareQuality) {
        submission.prompt = PendingRollPrompt::kConfirmDisenchant;
        return submission;
      } else if (roll_type != kDisenchantRollType &&
                 item->bonding == kBindOnPickup) {
        submission.prompt = PendingRollPrompt::kConfirmLoot;
        return submission;
      }
    }
  }

  if (((static_cast<std::uint32_t>(entry.roll_vote_mask) >>
        (roll_type & 0x1fu)) &
       1u) == 0u) {
    return std::nullopt;
  }

  entry.response_submitted = true;
  submission.fire_cancel_event = true;
  entry.is_visible = false;

  --visible_pending_roll_count_;
  submission.start_events = PromoteQueuedPendingRolls();
  return submission;
}

std::optional<PendingRollRemoval> LootState::RemovePendingRollBySourceAndSlot(
    const std::uint64_t loot_guid,
    const std::uint32_t loot_slot) {

  for (auto it = pending_rolls_.begin(); it != pending_rolls_.end(); ++it) {
    PendingRollEntry& entry = *it;
    if (entry.loot_guid != loot_guid || entry.loot_slot != loot_slot) {
      continue;
    }

    return CompletePendingRollEntry(it);
  }

  return std::nullopt;
}

std::optional<PendingRollHandle> LootState::FindPendingRollBySourceAndSlot(
    const std::uint64_t loot_guid, const std::uint32_t loot_slot) const {
  const auto it = std::find_if(
      pending_rolls_.begin(), pending_rolls_.end(),
      [loot_guid, loot_slot](const PendingRollEntry& entry) {
        return entry.loot_guid == loot_guid && entry.loot_slot == loot_slot;
      });
  return it == pending_rolls_.end()
             ? std::nullopt
             : std::optional<PendingRollHandle>(PendingRollHandle{
                   .roll_id = it->roll_id,
                   .lifetime_token = it->lifetime_token,
               });
}

std::optional<PendingRollRemoval> LootState::CompletePendingRoll(
    const PendingRollHandle handle) {
  const auto it = std::find_if(
      pending_rolls_.begin(), pending_rolls_.end(),
      [handle](const PendingRollEntry& entry) {
        return entry.roll_id == handle.roll_id &&
               entry.lifetime_token == handle.lifetime_token;
      });
  if (it == pending_rolls_.end()) {
    return std::nullopt;
  }

  return CompletePendingRollEntry(it);
}

void LootState::DiscardPendingRollAfterCacheFailure(
    const PendingRollHandle handle) {
  const auto it = std::find_if(
      pending_rolls_.begin(), pending_rolls_.end(),
      [handle](const PendingRollEntry& entry) {
        return entry.roll_id == handle.roll_id &&
               entry.lifetime_token == handle.lifetime_token;
      });
  if (it != pending_rolls_.end()) {
    pending_rolls_.erase(it);
  }
}

std::vector<PendingRollStartEvent> LootState::NotifyItemTemplateReady(
    const std::uint32_t item_id) {
  std::vector<PendingRollStartEvent> events;
  for (PendingRollEntry& entry : pending_rolls_) {
    if (entry.item_id == item_id && !entry.item_template_ready &&
        !entry.response_submitted) {
      entry.item_template_ready = true;
      ComputePendingRollReasonCodes(entry, inventory_, item_definitions_, dbc_);
      if (const auto event = TryActivatePendingRoll(entry)) {
        events.push_back(*event);
      }
    }
  }
  return events;
}

void LootState::NotifyItemTemplateFailed(const std::uint32_t item_id) {
  std::erase_if(pending_rolls_, [item_id](const PendingRollEntry& entry) {
    return entry.item_id == item_id && !entry.item_template_ready;
  });
}

std::uint32_t LootState::GetPendingRollTimeLeft(
    const std::uint64_t roll_id, const std::uint32_t now_tick_ms) const {
  const auto it = std::find_if(
      pending_rolls_.begin(), pending_rolls_.end(),
      [roll_id](const PendingRollEntry& entry) {
        return entry.roll_id == roll_id;
      });
  return it == pending_rolls_.end()
             ? 0u
             : static_cast<std::uint32_t>(it->deadline_tick_ms - now_tick_ms);
}

std::vector<PendingRollResetAction> LootState::DrainPendingRollsForReset() {
  std::vector<PendingRollResetAction> actions;
  actions.reserve(pending_rolls_.size());
  for (const PendingRollEntry& entry : pending_rolls_) {
    if (entry.response_submitted) {
      continue;
    }
    actions.push_back(PendingRollResetAction{
        .roll_id = entry.roll_id,
        .loot_guid = entry.loot_guid,
        .loot_slot = entry.loot_slot,
        .roll_type = 0,
        .fire_cancel_event = !entry.queued,
    });
  }
  ClearPendingRollsInternal();
  return actions;
}

void LootState::ClearPendingRolls() {
  ClearPendingRollsInternal();
}

void LootState::SetMasterLootCandidates(std::vector<std::uint64_t> guids) {
  if (guids.size() > kMaxMasterLootCandidateSlots) {
    guids.resize(kMaxMasterLootCandidateSlots);
  }
  master_loot_candidates_ = std::move(guids);
}

std::vector<std::uint64_t> LootState::GetMasterLootCandidates() const {
  return master_loot_candidates_;
}

std::uint64_t LootState::GetMasterLootCandidateGuid(
    const std::size_t zero_based_index) const {
  if (zero_based_index >= master_loot_candidates_.size()) {
    return 0;
  }

  return master_loot_candidates_[zero_based_index];
}

void LootState::SetOptOut(bool opt_out) {
  opt_out_ = opt_out;
}

bool LootState::GetOptOut() const {
  return opt_out_;
}

void LootState::SetPendingConfirmSlot(int slot) {
  pending_confirm_slot_ = slot;
}

int LootState::GetPendingConfirmSlot() const {
  return pending_confirm_slot_;
}

void LootState::ClearPendingConfirmSlot() {
  pending_confirm_slot_ = -1;
}

std::optional<PendingRollStartEvent> LootState::TryActivatePendingRoll(
    PendingRollEntry& entry) {
  if (!entry.item_template_ready || entry.is_visible ||
      entry.response_submitted) {
    return std::nullopt;
  }

  if (visible_pending_roll_count_ >= kMaxVisibleLootRolls) {
    entry.queued = true;
    return std::nullopt;
  }

  entry.queued = false;
  entry.is_visible = true;
  ++visible_pending_roll_count_;
  return PendingRollStartEvent{entry.roll_id, entry.countdown_ms};
}

std::vector<PendingRollStartEvent> LootState::PromoteQueuedPendingRolls() {
  std::vector<PendingRollStartEvent> events;
  if (visible_pending_roll_count_ >= kMaxVisibleLootRolls) {
    return events;
  }

  for (PendingRollEntry& entry : pending_rolls_) {
    if (visible_pending_roll_count_ >= kMaxVisibleLootRolls) {
      break;
    }

    if (!entry.queued) {
      continue;
    }

    entry.queued = false;
    entry.is_visible = true;
    ++visible_pending_roll_count_;
    events.push_back(
        PendingRollStartEvent{entry.roll_id, entry.countdown_ms});
  }

  return events;
}

PendingRollRemoval LootState::CompletePendingRollEntry(
    const std::list<PendingRollEntry>::iterator entry) {
  PendingRollRemoval removal;
  removal.roll_id = entry->roll_id;
  removal.fire_cancel_event = !entry->response_submitted;
  if (removal.fire_cancel_event) {
    --visible_pending_roll_count_;
  }
  pending_rolls_.erase(entry);
  removal.start_events = PromoteQueuedPendingRolls();
  return removal;
}

void LootState::ClearPendingRollsInternal() {
  pending_rolls_.clear();
  visible_pending_roll_count_ = 0;

}

}
