#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openwow/game/object_guid.h"
namespace openwow::game {

class InteractionSender;

inline constexpr int kTradeSlotCount = 7;
inline constexpr int kTradeSlotTradedCount = 6;
inline constexpr std::uint8_t kTradeWillNotBeTradedSlot = 6;
inline constexpr int kMaxGemSockets = 3;

enum class TradeStatus : std::uint32_t {
  kBusy = 0,
  kBeginTrade = 1,
  kOpenWindow = 2,
  kTradeCanceled = 3,
  kTradeAccept = 4,
  kBusy2 = 5,
  kNoTarget = 6,
  kBackToTrade = 7,
  kTradeComplete = 8,
  kTradeUnaccept = 9,
  kTargetTooFar = 10,
  kWrongFaction = 11,
  kCloseWindow = 12,

  kIgnoreYou = 14,
  kYouStunned = 15,
  kTargetStunned = 16,
  kYouDead = 17,
  kTargetDead = 18,
  kYouLogout = 19,
  kTargetLogout = 20,
  kTrialAccount = 21,
  kOnlyConjured = 22,
  kNotEligible = 23,
};

enum class TradeSide : std::uint8_t {
  kPlayer = 0,
  kTarget = 1,
};

struct TradeSlotItem {
  std::uint8_t slot_index = 0;
  std::uint32_t item_id = 0;
  std::uint32_t display_info_id = 0;
  std::uint32_t stack_count = 0;
  std::uint32_t is_wrapped = 0;
  std::uint64_t gift_creator = 0;
  std::uint32_t permanent_enchant = 0;
  std::uint32_t socket_enchants[kMaxGemSockets] = {};
  std::uint64_t creator = 0;
  std::uint32_t spell_charges = 0;
  std::uint32_t suffix_factor = 0;
  std::int32_t random_property_id = 0;
  std::uint32_t lock_id = 0;
  std::uint32_t max_durability = 0;
  std::uint32_t durability = 0;
};

struct TradeWindow {
  bool is_trader_data = false;
  std::uint32_t gold = 0;
  std::uint32_t slot7_text_id = 0;
  std::array<TradeSlotItem, kTradeSlotCount> slots{};
};

struct TradeAcceptUpdateEvent {
  int player_accepted = 0;
  int trader_accepted = 0;
};

struct TradeExtendedUiUpdate {
  std::array<bool, kTradeSlotCount> target_slot_changed{};
  std::array<bool, kTradeSlotCount> player_slot_removed{};
  bool target_slot7_text_changed = false;
  bool player_slot7_text_changed = false;
  bool player_money_changed = false;
  bool target_money_changed = false;
  bool ignored_stale_player_update = false;
  bool cancel_mismatched_trade = false;

  [[nodiscard]] bool HasAnyChanges() const {
    for (std::size_t slot = 0; slot < target_slot_changed.size(); ++slot) {
      if (target_slot_changed[slot] || player_slot_removed[slot]) {
        return true;
      }
    }

    return target_slot7_text_changed || player_slot7_text_changed || player_money_changed ||
           target_money_changed;
  }
};

struct LocalPlayerTradeSlot {
  std::uint64_t item_guid = 0;
  std::uint8_t source_bag = 0xFF;
  std::uint8_t source_slot = 0;
};

struct TradeAutoPlacement {
  std::uint8_t trade_slot = 0;
  std::uint8_t source_bag = 0;
  std::uint8_t source_slot = 0;
  std::uint64_t item_guid = 0;
};

struct TradeAcceptTransition {
  bool send_unaccept_packet = false;
  std::vector<TradeAcceptUpdateEvent> events;

  [[nodiscard]] bool HasEvents() const {
    return !events.empty();
  }
};

struct ScriptCloseTradeResult {
  bool trade_closed = false;
};

struct TradeOpenCloseResult {

  bool fired_trade_closed = false;

  std::uint64_t npc_death_guid = 0;

  bool send_cancel_packet = false;
};

struct TradeChanges {
  std::vector<std::uint64_t> released_item_guids;
  bool closed = false;
};

struct TradeStatusContext {
  std::uint64_t partner_guid = 0;
  bool local_initiate_active = false;
  bool auto_busy_begin_trade = false;
  std::uint32_t reason_code = 0;
  bool reason_has_alternate_message = false;
  std::uint32_t item_id = 0;
  std::uint8_t slot = 0xFF;
};

struct TradeStatusMessage {
  std::uint32_t status = 0;
  std::uint64_t partner_guid = 0;
  std::uint32_t trade_session_id = 0;
  std::uint32_t reason_code = 0;
  bool reason_has_alternate_message = false;
  std::uint32_t item_id = 0;
  std::uint8_t slot = 0xFF;
};

struct TradeExtendedPrefix {
  std::uint8_t side_value = 0;
  std::uint32_t trade_session_id = 0;
  std::uint32_t echoed_local_mutation_index = 0;
};

struct TradeExtendedSnapshot {
  TradeExtendedPrefix prefix;
  TradeSide side = TradeSide::kPlayer;
  std::uint32_t server_state_index = 0;
  TradeWindow window;
};

enum class TradeExtendedDisposition {
  kApplySnapshot,
  kIgnoredStale,
  kCancelMismatchedTrade,
  kInvalidPrefix,
};

class TradeInteraction {
 public:
  [[nodiscard]] TradeChanges TakeChanges();
  void HandleTradeStatus(const TradeStatusMessage& message);
  [[nodiscard]] TradeExtendedDisposition ClassifyTradeExtended(
      const TradeExtendedPrefix& prefix);
  void ApplyTradeExtendedSnapshot(TradeExtendedSnapshot snapshot);

  [[nodiscard]] TradeStatus last_status() const {
    return last_status_;
  }
  [[nodiscard]] std::uint32_t last_status_code() const {
    return last_status_code_;
  }
  [[nodiscard]] std::uint64_t begin_trade_guid() const {
    return begin_trade_guid_;
  }
  [[nodiscard]] std::uint32_t open_window_trade_id() const {
    return open_window_trade_id_;
  }
  [[nodiscard]] std::uint32_t local_mutation_index() const {
    return local_mutation_index_;
  }
  [[nodiscard]] std::uint32_t server_state_index(TradeSide side) const {
    return server_state_indexes_[static_cast<std::size_t>(side)];
  }

  [[nodiscard]] std::uint32_t accept_state_index() const {
    return accept_trade_cookie_;
  }
  [[nodiscard]] const std::optional<TradeWindow> &own_window() const {
    return own_window_;
  }
  [[nodiscard]] const std::optional<TradeWindow> &trader_window() const {
    return trader_window_;
  }
  [[nodiscard]] bool is_open() const {
    return is_open_;
  }
  [[nodiscard]] std::uint32_t own_gold() const {
    return trade_gold_own_;
  }
  [[nodiscard]] std::uint32_t trader_gold() const {
    return trade_gold_trader_;
  }
  [[nodiscard]] int player_accept_state() const {
    return trade_accept_own_;
  }
  [[nodiscard]] int trader_accept_state() const {
    return trade_accept_trader_;
  }
  [[nodiscard]] const TradeExtendedUiUpdate &last_trade_extended_update() const {
    return last_trade_extended_update_;
  }
  [[nodiscard]] const TradeStatusContext &last_status_context() const {
    return last_status_context_;
  }
  void SetPendingOwnGold(std::uint32_t gold);
  void MarkLocalInitiateRequest(std::uint64_t target_guid,
                                bool defer_cursor_item_placement = false);

  void Clear();

  void ResetTradeState();

  [[nodiscard]] TradeAcceptTransition SetTraderAcceptedState(bool accepted);

  [[nodiscard]] TradeAcceptTransition SetPlayerAcceptedState(bool accepted);

  [[nodiscard]] TradeAcceptTransition ResetAcceptedStateForTradeChange(bool allow_local_unaccept);

  [[nodiscard]] bool ShouldFirePlayerTradeMoneyOnScriptClose() const;
  [[nodiscard]] ScriptCloseTradeResult CloseFromScript();

  [[nodiscard]] bool SetLocalPlayerTradeSlot(std::uint8_t trade_slot, std::uint64_t item_guid,
                                             std::uint8_t source_bag, std::uint8_t source_slot);
  [[nodiscard]] bool ClearLocalPlayerTradeSlot(std::uint8_t trade_slot);
  [[nodiscard]] std::optional<LocalPlayerTradeSlot>
  GetLocalPlayerTradeSlot(std::size_t trade_slot) const;

  [[nodiscard]] std::optional<std::uint8_t>
  RemoveLocalPlayerTradeItemByGuid(std::uint64_t item_guid);
  [[nodiscard]] std::optional<std::uint8_t>
  SelectCursorDropTradeSlot(std::uint64_t item_guid, bool use_will_not_be_traded_slot) const;

  [[nodiscard]] bool IsLocalPlayerTradeItemGuid(std::uint64_t item_guid) const;
  [[nodiscard]] std::optional<std::uint64_t>
  GetRemoteTradeSlotItemGuid(std::size_t trade_slot) const;

  [[nodiscard]] std::uint32_t GetTargetPermanentEnchantId(unsigned int slot) const;

  void UpdateTargetSlots(const TradeWindow& window);

  [[nodiscard]] TradeOpenCloseResult HandleTradeOpenClose(std::uint64_t guid, int reason);

  void HandleWorldLogout(InteractionSender &interaction);

  [[nodiscard]] bool HasLocalInitiateFlag() const {
    return local_initiate_active_;
  }

  [[nodiscard]] bool ShouldAutoPlaceHeldCursorItemOnOpen() const {
    return local_initiate_active_ && local_initiate_defer_cursor_item_placement_;
  }

  [[nodiscard]] bool IsInitiateThrottled(std::uint32_t now_ms) const {
    return initiate_throttle_deadline_ms_ != 0 &&
           static_cast<std::int32_t>(now_ms - initiate_throttle_deadline_ms_) < 0;
  }

  void ResetInitiateThrottle(std::uint32_t now_ms) {
    initiate_throttle_deadline_ms_ = now_ms + 1000;
  }

private:
  static constexpr std::uint32_t kAcceptTradeCookieAfterOpen = 1;

  TradeChanges changes_;
  TradeStatus last_status_ = TradeStatus::kBusy;
  std::uint32_t last_status_code_ = 0;
  std::uint64_t begin_trade_guid_ = 0;
  std::uint32_t open_window_trade_id_ = 0;
  std::uint32_t accept_trade_cookie_ = 0;
  bool is_open_ = false;
  bool local_initiate_active_ = false;
  bool local_initiate_defer_cursor_item_placement_ = false;
  std::uint32_t initiate_throttle_deadline_ms_ = 0;

  std::optional<TradeWindow> own_window_;
  std::optional<TradeWindow> trader_window_;

  std::array<TradeSlotItem, kTradeSlotCount> cached_target_slots_{};

  std::array<LocalPlayerTradeSlot, kTradeSlotCount> local_player_slots_{};

  std::uint32_t local_mutation_index_ = 0;
  std::array<std::uint32_t, 2> server_state_indexes_{};
  std::uint32_t trade_gold_own_ = 0;
  std::uint32_t trade_gold_trader_ = 0;
  std::uint32_t trade_accept_own_ = 0;

  std::uint32_t trade_accept_trader_ = 0;

  int trade_close_reason_ = 0;

  std::uint64_t trade_mouseover_guid_ = 0;

  TradeExtendedUiUpdate last_trade_extended_update_{};
  TradeStatusContext last_status_context_{};

  void ResetTradeRuntimeState();
  void ReleaseAllLocalTradeOffers();
  void ClearLocalTradeOffer(std::uint8_t slot);
  void ClearLocalTradeMoney();
  void ClearLocalInitiateRequest();
  void ResetLocalPlayerTradeSlots();
  void AdvanceLocalMutationIndex();
};

}
