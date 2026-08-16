#include "openwow/game/commerce/auctions/auction_state.h"

#include <algorithm>
#include <utility>

namespace openwow::game {

void AuctionState::SetSellItemSelection(AuctionSellItemSelection selection) {
  sell_item_selection_ = selection;
}

void AuctionState::ClearSellItemSelection() {
  sell_item_selection_ = {};
}

std::optional<std::uint64_t> AuctionState::TakeSellItemSelection() {
  const auto item = sell_item_selection_.item_guid;
  sell_item_selection_ = {};
  return item == 0 ? std::nullopt
                   : std::optional<std::uint64_t>{item};
}

AuctionSellItemSelection AuctionState::GetSellItemSelection() const {
  return sell_item_selection_;
}

bool AuctionState::HasSellItemSelection() const {
  return !sell_item_selection_.IsEmpty();
}

void AuctionState::BeginMultiSell(
    const std::uint64_t auctioneer_guid, const std::uint32_t min_bid,
    const std::uint32_t buyout, const std::uint32_t duration_minutes,
    const std::uint32_t stack_size, const std::uint32_t total_stacks,
    std::vector<AuctionMultiSellSource> sources) {
  multi_sell_ = {
      .active = true,
      .auctioneer_guid = auctioneer_guid,
      .min_bid = min_bid,
      .buyout = buyout,
      .duration_minutes = duration_minutes,
      .stack_size = stack_size,
      .stacks_remaining = total_stacks,
      .total_stacks = total_stacks,
      .sources = std::move(sources),
  };
}

std::vector<std::uint64_t> AuctionState::AbortMultiSell() {
  return EndMultiSell();
}

std::vector<std::uint64_t> AuctionState::CompleteMultiSell() {
  return EndMultiSell();
}

std::optional<AuctionMultiSellRequest>
AuctionState::PrepareNextMultiSellRequest() {
  if (!multi_sell_.active || multi_sell_.stacks_remaining == 0 ||
      multi_sell_.stack_size == 0 || multi_sell_.sources.empty()) {
    return std::nullopt;
  }

  auto& sources = multi_sell_.sources;
  auto remaining = multi_sell_.stack_size;
  auto boundary = sources.size() - 1;
  while (remaining > sources[boundary].remaining_count) {
    remaining -= sources[boundary].remaining_count;
    if (boundary == 0) {
      return std::nullopt;
    }
    --boundary;
  }

  AuctionMultiSellRequest request{
      .auctioneer_guid = multi_sell_.auctioneer_guid,
      .min_bid = multi_sell_.min_bid,
      .buyout = multi_sell_.buyout,
      .duration_minutes = multi_sell_.duration_minutes,
  };
  request.items.reserve(sources.size() - boundary);
  for (std::size_t index = sources.size(); index-- > boundary + 1;) {
    request.items.emplace_back(
        sources[index].item_guid, sources[index].remaining_count);
  }
  request.items.emplace_back(sources[boundary].item_guid, remaining);

  sources[boundary].remaining_count -= remaining;
  sources.erase(sources.begin() + boundary + 1, sources.end());
  if (sources[boundary].remaining_count == 0) {
    sources.erase(sources.begin() + boundary);
  }
  --multi_sell_.stacks_remaining;
  return request;
}

bool AuctionState::HasActiveMultiSell() const {
  return multi_sell_.active;
}

bool AuctionState::IsTrackedMultiSellSource(
    const std::uint64_t item_guid) const {
  return multi_sell_.active && item_guid != 0 &&
         std::any_of(
             multi_sell_.sources.begin(), multi_sell_.sources.end(),
             [item_guid](const AuctionMultiSellSource& source) {
               return source.item_guid == item_guid;
             });
}

std::uint32_t AuctionState::GetMultiSellCompletedStacks() const {
  return multi_sell_.total_stacks - multi_sell_.stacks_remaining;
}

std::uint32_t AuctionState::GetMultiSellTotalStacks() const {
  return multi_sell_.total_stacks;
}

void AuctionState::SetDepositCost(const std::uint32_t copper) {
  deposit_ = copper;
}

std::uint32_t AuctionState::GetDepositCost() const {
  return deposit_;
}

}
