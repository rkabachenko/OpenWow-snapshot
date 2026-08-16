#include "openwow/game/commerce/auctions/auction_state.h"

#include "openwow/runtime/time/game_clock.h"

#include <algorithm>

namespace openwow::game {

bool AuctionState::CanSendAuctionQuery(const std::uint32_t query_type) const {
  return CanSendAuctionQueryInternal(query_type);
}

bool AuctionState::CanSendGetAllAuctionQuery(
    const std::uint32_t query_type) const {
  if (!CanSendAuctionQueryInternal(query_type)) {
    return false;
  }
  if (browse_get_all_last_time_ == 0) {
    return true;
  }
  const auto now = core::GameClock::GetTickCount32();
  return static_cast<std::int32_t>(
             now - browse_get_all_last_time_ -
             kGetAllQueryCooldownMs) >= 0;
}

bool AuctionState::CanSendAuctionQueryInternal(
    const std::uint32_t query_type) const {
  if (query_type >= kMaxQueryTypes || query_pending_[query_type] > 0) {
    return false;
  }
  const auto last = query_last_time_[query_type];
  return last == 0 ||
         static_cast<std::int32_t>(
             core::GameClock::GetTickCount32() - last - query_cooldown_ms_) >= 0;
}

void AuctionState::MarkQuerySentInternal(
    const std::uint32_t query_type, const std::uint32_t pending_packets,
    const std::uint32_t now) {
  if (query_type >= kMaxQueryTypes) {
    return;
  }
  query_pending_[query_type] = std::max(pending_packets, 1u);
  query_last_time_[query_type] = now;
}

void AuctionState::MarkQuerySent(
    const std::uint32_t query_type, const std::uint32_t pending_packets) {
  MarkQuerySentInternal(
      query_type, pending_packets, core::GameClock::GetTickCount32());
}

void AuctionState::MarkBrowseQuerySent(const bool get_all) {
  const auto now = core::GameClock::GetTickCount32();
  MarkQuerySentInternal(0, 1, now);
  if (get_all) {
    browse_get_all_last_time_ = now;
  }
}

void AuctionState::MarkQueryPacketReceived(const std::uint32_t query_type) {
  if (query_type >= kMaxQueryTypes) {
    return;
  }
  if (query_pending_[query_type] > 0) {
    --query_pending_[query_type];
  }
  query_last_time_[query_type] = core::GameClock::GetTickCount32();
}

void AuctionState::MarkQueryComplete(const std::uint32_t query_type) {
  if (query_type >= kMaxQueryTypes) {
    return;
  }
  query_pending_[query_type] = 0;
  query_last_time_[query_type] = core::GameClock::GetTickCount32();
}

void AuctionState::SetQueryCooldown(const std::uint32_t milliseconds) {
  query_cooldown_ms_ = milliseconds;
}

}
