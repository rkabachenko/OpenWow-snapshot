
#include "openwow/game/commerce/trade/trade_interaction.h"

#include "openwow/game/interaction_sender.h"

namespace openwow::game {

namespace {

std::uint32_t GetSlot7TextId(const std::optional<TradeWindow> &window) {
  return window ? window->slot7_text_id : 0;
}

bool ShouldClearTradeInitiationState(const std::uint32_t status_value) {
  switch (status_value) {
  case 0:
  case 3:
  case 5:
  case 6:
  case 8:
  case 10:
  case 11:
  case 12:
  case 13:
  case 14:
  case 15:
  case 16:
  case 17:
  case 18:
  case 19:
  case 20:
  case 21:
    return true;
  default:
    return false;
  }
}

bool HasObservableTradeSlotChanged(const TradeSlotItem& previous,
                                   const TradeSlotItem& current) {
  return previous.item_id != current.item_id ||
         previous.stack_count != current.stack_count ||
         previous.permanent_enchant != current.permanent_enchant ||
         previous.socket_enchants[0] != current.socket_enchants[0] ||
         previous.socket_enchants[1] != current.socket_enchants[1] ||
         previous.socket_enchants[2] != current.socket_enchants[2] ||
         previous.creator != current.creator ||
         previous.is_wrapped != current.is_wrapped ||
         previous.gift_creator != current.gift_creator ||
         previous.spell_charges != current.spell_charges ||
         previous.suffix_factor != current.suffix_factor ||
         previous.random_property_id != current.random_property_id ||
         previous.lock_id != current.lock_id ||
         previous.max_durability != current.max_durability ||
         previous.durability != current.durability;
}

}

void TradeInteraction::HandleTradeStatus(const TradeStatusMessage& message) {
  const std::uint32_t status_value = message.status;
  last_status_context_ = {};
  last_status_context_.partner_guid = begin_trade_guid_;
  last_status_context_.local_initiate_active = local_initiate_active_;
  last_status_code_ = status_value;
  last_status_ = static_cast<TradeStatus>(status_value);

  switch (status_value) {
  case static_cast<std::uint32_t>(TradeStatus::kBeginTrade): {
    if (local_initiate_active_) {
      last_status_context_.auto_busy_begin_trade = true;
    } else {
      begin_trade_guid_ = message.partner_guid;
      last_status_context_.partner_guid = message.partner_guid;
    }
    break;
  }
  case static_cast<std::uint32_t>(TradeStatus::kOpenWindow): {
    ResetTradeRuntimeState();
    open_window_trade_id_ = message.trade_session_id;
    accept_trade_cookie_ = kAcceptTradeCookieAfterOpen;
    local_mutation_index_ = 1;
    server_state_indexes_.fill(1);
    is_open_ = true;
    break;
  }
  case static_cast<std::uint32_t>(TradeStatus::kCloseWindow): {
    last_status_context_.reason_code = message.reason_code;
    last_status_context_.reason_has_alternate_message =
        message.reason_has_alternate_message;
    last_status_context_.item_id = message.item_id;
    ReleaseAllLocalTradeOffers();
    ResetTradeRuntimeState();
    begin_trade_guid_ = 0;
    trade_close_reason_ = 0;
    break;
  }
  case static_cast<std::uint32_t>(TradeStatus::kTradeCanceled):
  case static_cast<std::uint32_t>(TradeStatus::kTradeComplete):
    ReleaseAllLocalTradeOffers();
    ResetTradeRuntimeState();
    begin_trade_guid_ = 0;
    trade_close_reason_ =
        status_value == static_cast<std::uint32_t>(TradeStatus::kTradeComplete) ? 1 : 0;
    break;
  case 13:
    ResetTradeRuntimeState();
    begin_trade_guid_ = 0;
    break;
  case static_cast<std::uint32_t>(TradeStatus::kOnlyConjured):
  case static_cast<std::uint32_t>(TradeStatus::kNotEligible): {
    last_status_context_.slot = message.slot;
    if (last_status_context_.slot == 0xFF) {
      ClearLocalTradeMoney();
    } else {
      ClearLocalTradeOffer(last_status_context_.slot);
    }
    break;
  }
  default:
    break;
  }

  if (ShouldClearTradeInitiationState(status_value)) {
    begin_trade_guid_ = 0;
    ClearLocalInitiateRequest();
  }
}

TradeExtendedDisposition TradeInteraction::ClassifyTradeExtended(
    const TradeExtendedPrefix& prefix) {
  last_trade_extended_update_ = {};
  if (prefix.trade_session_id != open_window_trade_id_) {
    last_trade_extended_update_.cancel_mismatched_trade = true;
    return TradeExtendedDisposition::kCancelMismatchedTrade;
  }

  if (prefix.side_value > static_cast<std::uint8_t>(TradeSide::kTarget)) {
    return TradeExtendedDisposition::kInvalidPrefix;
  }

  if (prefix.side_value == static_cast<std::uint8_t>(TradeSide::kPlayer) &&
      prefix.echoed_local_mutation_index < local_mutation_index_) {
    last_trade_extended_update_.ignored_stale_player_update = true;
    return TradeExtendedDisposition::kIgnoredStale;
  }
  return TradeExtendedDisposition::kApplySnapshot;
}

void TradeInteraction::ApplyTradeExtendedSnapshot(
    TradeExtendedSnapshot snapshot) {
  TradeWindow& w = snapshot.window;
  const auto side_index = static_cast<std::size_t>(snapshot.side);
  const auto previous_own_gold = trade_gold_own_;
  const auto previous_trader_gold = trade_gold_trader_;
  const auto previous_player_slot7_text_id = GetSlot7TextId(own_window_);
  const auto previous_target_slot7_text_id = GetSlot7TextId(trader_window_);

  server_state_indexes_[side_index] = snapshot.server_state_index;
  if (w.is_trader_data) {
    trade_gold_trader_ = w.gold;
    trader_window_ = w;
  } else {
    if (own_window_) {
      for (std::size_t slot = 0; slot < w.slots.size(); ++slot) {
        if (own_window_->slots[slot].item_id != 0 && w.slots[slot].item_id == 0) {
          ClearLocalTradeOffer(static_cast<std::uint8_t>(slot));
          last_trade_extended_update_.player_slot_removed[slot] = true;
        }
      }
    }
    trade_gold_own_ = w.gold;
    own_window_ = w;
  }

  if (trader_window_) {
    UpdateTargetSlots(*trader_window_);
  }

  last_trade_extended_update_.player_slot7_text_changed =
      previous_player_slot7_text_id != GetSlot7TextId(own_window_);
  last_trade_extended_update_.target_slot7_text_changed =
      previous_target_slot7_text_id != GetSlot7TextId(trader_window_);
  last_trade_extended_update_.player_money_changed = previous_own_gold != trade_gold_own_;
  last_trade_extended_update_.target_money_changed = previous_trader_gold != trade_gold_trader_;
}

void TradeInteraction::ResetLocalPlayerTradeSlots() {
  local_player_slots_.fill(LocalPlayerTradeSlot{});
}

void TradeInteraction::ResetTradeRuntimeState() {
  is_open_ = false;
  own_window_.reset();
  trader_window_.reset();
  open_window_trade_id_ = 0;
  local_mutation_index_ = 0;
  server_state_indexes_.fill(0);
  trade_gold_own_ = 0;
  trade_gold_trader_ = 0;
  trade_accept_own_ = 0;
  trade_accept_trader_ = 0;
  trade_close_reason_ = 0;
  trade_mouseover_guid_ = 0;
  last_trade_extended_update_ = {};
  ResetLocalPlayerTradeSlots();

  cached_target_slots_.fill(TradeSlotItem{});
}

void TradeInteraction::ReleaseAllLocalTradeOffers() {
  for (const auto& slot : local_player_slots_) {
    if (slot.item_guid != 0) {
      changes_.released_item_guids.push_back(slot.item_guid);
    }
  }
}

void TradeInteraction::ClearLocalTradeOffer(std::uint8_t slot) {
  if (slot >= kTradeSlotCount) {
    return;
  }

  if (local_player_slots_[slot].item_guid != 0) {
    changes_.released_item_guids.push_back(
        local_player_slots_[slot].item_guid);
  }
  local_player_slots_[slot] = LocalPlayerTradeSlot{};
  if (own_window_) {
    own_window_->slots[slot] = TradeSlotItem{};
    own_window_->slots[slot].slot_index = slot;
  }
}

void TradeInteraction::ClearLocalTradeMoney() {
  trade_gold_own_ = 0;
  if (own_window_) {
    own_window_->gold = 0;
  }
}

void TradeInteraction::ClearLocalInitiateRequest() {
  local_initiate_active_ = false;
  local_initiate_defer_cursor_item_placement_ = false;
}

void TradeInteraction::Clear() {
  last_status_ = TradeStatus::kBusy;
  last_status_code_ = 0;
  begin_trade_guid_ = 0;
  last_status_context_ = {};
  ClearLocalInitiateRequest();
  ResetTradeRuntimeState();
}

void TradeInteraction::SetPendingOwnGold(std::uint32_t gold) {
  trade_gold_own_ = gold;
  if (own_window_) {
    own_window_->gold = gold;
  }
  AdvanceLocalMutationIndex();
}

void TradeInteraction::MarkLocalInitiateRequest(std::uint64_t target_guid,
                                            bool defer_cursor_item_placement) {
  if (target_guid == 0) {
    return;
  }

  begin_trade_guid_ = target_guid;
  local_initiate_active_ = true;
  local_initiate_defer_cursor_item_placement_ = defer_cursor_item_placement;
}

TradeAcceptTransition TradeInteraction::SetTraderAcceptedState(bool accepted) {
  const std::uint32_t next_state = accepted ? 1u : 0u;
  if (trade_accept_trader_ == next_state) {
    return {};
  }

  trade_accept_trader_ = next_state;
  TradeAcceptTransition transition;
  TradeAcceptUpdateEvent event;
  event.player_accepted = static_cast<int>(trade_accept_own_);
  event.trader_accepted = static_cast<int>(trade_accept_trader_);
  transition.events.push_back(event);
  return transition;
}

TradeAcceptTransition TradeInteraction::SetPlayerAcceptedState(bool accepted) {
  const std::uint32_t next_state = accepted ? 1u : 0u;
  if (trade_accept_own_ == next_state) {
    return {};
  }

  trade_accept_own_ = next_state;
  TradeAcceptTransition transition;
  TradeAcceptUpdateEvent event;
  event.player_accepted = static_cast<int>(trade_accept_own_);
  event.trader_accepted = static_cast<int>(trade_accept_trader_);
  transition.events.push_back(event);
  return transition;
}

TradeAcceptTransition TradeInteraction::ResetAcceptedStateForTradeChange(bool allow_local_unaccept) {
  TradeAcceptTransition transition;

  if (allow_local_unaccept && trade_accept_own_ != 0) {
    trade_accept_own_ = 0;
    transition.send_unaccept_packet = true;
    TradeAcceptUpdateEvent event;
    event.player_accepted = 0;
    event.trader_accepted = static_cast<int>(trade_accept_trader_);
    transition.events.push_back(event);
  }

  if (trade_accept_trader_ != 0) {
    trade_accept_trader_ = 0;
    TradeAcceptUpdateEvent event;
    event.player_accepted = static_cast<int>(trade_accept_own_);
    event.trader_accepted = 0;
    transition.events.push_back(event);
  }

  return transition;
}

bool TradeInteraction::SetLocalPlayerTradeSlot(std::uint8_t trade_slot, std::uint64_t item_guid,
                                           std::uint8_t source_bag, std::uint8_t source_slot) {
  if (trade_slot >= kTradeSlotCount || item_guid == 0) {
    return false;
  }

  LocalPlayerTradeSlot slot;
  slot.item_guid = item_guid;
  slot.source_bag = source_bag;
  slot.source_slot = source_slot;
  local_player_slots_[trade_slot] = slot;
  AdvanceLocalMutationIndex();
  return true;
}

bool TradeInteraction::ClearLocalPlayerTradeSlot(std::uint8_t trade_slot) {
  if (trade_slot >= kTradeSlotCount) {
    return false;
  }

  local_player_slots_[trade_slot] = LocalPlayerTradeSlot{};
  AdvanceLocalMutationIndex();
  return true;
}

std::optional<LocalPlayerTradeSlot>
TradeInteraction::GetLocalPlayerTradeSlot(std::size_t trade_slot) const {
  if (trade_slot >= kTradeSlotCount) {
    return std::nullopt;
  }

  const auto &slot = local_player_slots_[trade_slot];
  if (slot.item_guid == 0) {
    return std::nullopt;
  }

  return slot;
}

std::optional<std::uint8_t>
TradeInteraction::RemoveLocalPlayerTradeItemByGuid(const std::uint64_t item_guid) {
  if (item_guid == 0) {
    return std::nullopt;
  }

  for (std::uint8_t slot = 0; slot < kTradeSlotCount; ++slot) {
    if (local_player_slots_[slot].item_guid != item_guid) {
      continue;
    }

    local_player_slots_[slot] = LocalPlayerTradeSlot{};
    AdvanceLocalMutationIndex();
    return slot;
  }

  return std::nullopt;
}

void TradeInteraction::AdvanceLocalMutationIndex() {
  ++local_mutation_index_;
}

std::optional<std::uint8_t>
TradeInteraction::SelectCursorDropTradeSlot(std::uint64_t item_guid,
                                        bool use_will_not_be_traded_slot) const {
  if (item_guid == 0) {
    return std::nullopt;
  }

  if (use_will_not_be_traded_slot) {
    if (GetLocalPlayerTradeSlot(kTradeWillNotBeTradedSlot).has_value()) {
      return std::nullopt;
    }
    return kTradeWillNotBeTradedSlot;
  }

  std::optional<std::uint8_t> first_empty_slot;
  for (std::uint8_t slot = 0; slot < kTradeSlotTradedCount; ++slot) {
    const auto cached_slot = GetLocalPlayerTradeSlot(slot);
    if (cached_slot && cached_slot->item_guid == item_guid) {
      return std::nullopt;
    }

    if (!cached_slot && !first_empty_slot.has_value()) {
      first_empty_slot = slot;
    }
  }

  return first_empty_slot;
}

bool TradeInteraction::IsLocalPlayerTradeItemGuid(std::uint64_t item_guid) const {
  if (!is_open_ || begin_trade_guid_ == 0 || item_guid == 0) {
    return false;
  }

  for (const auto &slot : local_player_slots_) {
    if (slot.item_guid == item_guid) {
      return true;
    }
  }

  return false;
}

std::optional<std::uint64_t>
TradeInteraction::GetRemoteTradeSlotItemGuid(std::size_t trade_slot) const {
  if (trade_slot >= kTradeSlotCount) {
    return std::nullopt;
  }

  const auto guid = cached_target_slots_[trade_slot].creator;
  if (guid == 0) {
    return std::nullopt;
  }

  return guid;
}

std::uint32_t TradeInteraction::GetTargetPermanentEnchantId(unsigned int slot) const {
  if (slot > 6) {
    return 0;
  }
  return cached_target_slots_[slot].permanent_enchant;
}

void TradeInteraction::ResetTradeState() {
  last_status_ = TradeStatus::kBusy;
  begin_trade_guid_ = 0;
  last_status_context_ = {};
  ClearLocalInitiateRequest();
  ResetTradeRuntimeState();
}

bool TradeInteraction::ShouldFirePlayerTradeMoneyOnScriptClose() const {
  return trade_close_reason_ == 0;
}

ScriptCloseTradeResult TradeInteraction::CloseFromScript() {
  ScriptCloseTradeResult result;

  if (begin_trade_guid_ == 0) {
    return result;
  }

  if (trade_mouseover_guid_ != 0) {
    changes_.released_item_guids.push_back(trade_mouseover_guid_);
    trade_mouseover_guid_ = 0;
  }

  trade_close_reason_ = 0;
  begin_trade_guid_ = 0;
  ClearLocalInitiateRequest();
  is_open_ = false;
  result.trade_closed = true;
  changes_.closed = true;
  return result;
}

void TradeInteraction::UpdateTargetSlots(const TradeWindow& window) {
  last_trade_extended_update_.target_slot_changed.fill(false);

  for (std::size_t index = 0; index < window.slots.size(); ++index) {
    if (HasObservableTradeSlotChanged(cached_target_slots_[index],
                                      window.slots[index])) {
      cached_target_slots_[index] = window.slots[index];
      last_trade_extended_update_.target_slot_changed[index] = true;
      if (index == kTradeWillNotBeTradedSlot && trade_mouseover_guid_ != 0) {
        trade_mouseover_guid_ = 0;
      }
    }
  }
}

TradeOpenCloseResult TradeInteraction::HandleTradeOpenClose(std::uint64_t guid, int reason) {
  TradeOpenCloseResult result;

  if (guid != 0) {

    begin_trade_guid_ = guid;
    ResetTradeRuntimeState();
    local_mutation_index_ = 1;
    server_state_indexes_.fill(1);
    is_open_ = true;

  } else {

    if (begin_trade_guid_ != 0) {
      if (trade_mouseover_guid_ != 0) {
        changes_.released_item_guids.push_back(trade_mouseover_guid_);
        trade_mouseover_guid_ = 0;
      }

      trade_close_reason_ = reason;

      result.npc_death_guid = begin_trade_guid_;

      begin_trade_guid_ = 0;

      changes_.closed = true;
      result.fired_trade_closed = true;
    }

    result.send_cancel_packet = true;
  }

  return result;
}

void TradeInteraction::HandleWorldLogout(InteractionSender &interaction) {
  ReleaseAllLocalTradeOffers();

  if (begin_trade_guid_ != 0) {
    if (trade_mouseover_guid_ != 0) {
      changes_.released_item_guids.push_back(trade_mouseover_guid_);
      trade_mouseover_guid_ = 0;
    }

    trade_close_reason_ = 0;
    begin_trade_guid_ = 0;
    ClearLocalInitiateRequest();
    ResetTradeRuntimeState();
    changes_.closed = true;
  }

  interaction.SendCancelTrade();
}

TradeChanges TradeInteraction::TakeChanges() {
  return std::exchange(changes_, {});
}

}
