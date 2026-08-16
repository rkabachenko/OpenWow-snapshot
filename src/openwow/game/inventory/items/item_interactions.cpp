#include "openwow/game/inventory/items/item_interactions.h"

#include <algorithm>

namespace openwow::game {

void ItemInteractionSession::reset(const std::uint64_t player_generation) {
  player_generation_ = player_generation;
  ++request_generation_;
  socket_.reset();
  refund_quotes_.clear();
  refund_result_.reset();
  pending_refund_.reset();
  readable_.reset();
  item_text_.clear();
  pending_text_.clear();
  pending_modification_item_ = {};
  pending_enchant_spell_ = 0;
}

std::uint64_t ItemInteractionSession::begin_request() {
  return ++request_generation_;
}

bool ItemInteractionSession::current_request(
    const std::uint64_t generation) const noexcept {
  return generation != 0 && generation == request_generation_;
}

void ItemInteractionSession::begin_socket(
    const ObjectGuid item,
    const std::array<std::uint8_t, 3> socket_masks,
    const std::uint8_t socket_count) {
  socket_ = SocketDraft{
      .item = item,
      .socket_masks = socket_masks,
      .socket_count =
          std::min<std::uint8_t>(socket_count, socket_masks.size()),
      .generation = begin_request(),
  };
}

bool ItemInteractionSession::set_socket_gem(const std::size_t socket,
                                             const ObjectGuid gem) {
  if (!socket_.has_value() || socket >= socket_->gems.size()) {
    return false;
  }
  socket_->gems[socket] = gem;
  return true;
}

std::optional<PendingSocketGem> ItemInteractionSession::place_socket_gem(
    const std::size_t socket, std::optional<PendingSocketGem> gem) {
  if (!socket_.has_value() || socket >= socket_->socket_count) {
    return std::nullopt;
  }
  auto displaced = std::move(socket_->pending_gems[socket]);
  socket_->gems[socket] = gem.has_value() ? gem->item : ObjectGuid{};
  socket_->pending_gems[socket] = std::move(gem);
  return displaced;
}
bool ItemInteractionSession::apply_socket_result(const ObjectGuid item) {
  if (!socket_.has_value() || socket_->item != item) {
    return false;
  }
  socket_.reset();
  return true;
}

void ItemInteractionSession::cancel_socket() {
  socket_.reset();
}

bool ItemInteractionSession::apply_refund_quote(
    const std::uint64_t request_generation, RefundQuote quote) {
  if (!pending_refund_.has_value() ||
      pending_refund_->second != request_generation ||
      pending_refund_->first != quote.item || quote.item.IsEmpty()) {
    return false;
  }
  refund_quotes_.insert_or_assign(quote.item, std::move(quote));
  return true;
}

bool ItemInteractionSession::apply_refund_result(
    const std::uint64_t request_generation, RefundResult result) {
  if (!pending_refund_.has_value() ||
      pending_refund_->second != request_generation ||
      pending_refund_->first != result.item || result.item.IsEmpty()) {
    return false;
  }
  if (result.error == 0) {
    refund_quotes_.erase(result.item);
  }
  refund_result_ = std::move(result);
  pending_refund_.reset();
  return true;
}

std::uint64_t ItemInteractionSession::begin_refund_request(
    const ObjectGuid item) {
  const auto generation = begin_request();
  pending_refund_ = std::pair(item, generation);
  return generation;
}

const RefundQuote* ItemInteractionSession::refund_quote(
    const ObjectGuid item) const {
  const auto found = refund_quotes_.find(item);
  return found == refund_quotes_.end() ? nullptr : &found->second;
}

void ItemInteractionSession::begin_readable(const ObjectGuid item) {
  readable_ = ReadableItem{
      .item = item,
      .generation = begin_request(),
  };
}

bool ItemInteractionSession::begin_text_query(const ObjectGuid item) {
  if (item.IsEmpty() || item_text_.contains(item) ||
      pending_text_.contains(item)) {
    return false;
  }
  pending_text_.insert(item);
  return true;
}

bool ItemInteractionSession::cache_text(const ObjectGuid item,
                                        std::string text) {
  if (item.IsEmpty() || !pending_text_.erase(item)) {
    return false;
  }
  item_text_.insert_or_assign(item, std::move(text));
  return true;
}

const std::string* ItemInteractionSession::cached_text(
    const ObjectGuid item) const {
  const auto found = item_text_.find(item);
  return found == item_text_.end() ? nullptr : &found->second;
}

void ItemInteractionSession::clear_text_cache() {
  item_text_.clear();
  pending_text_.clear();
}

void ItemInteractionSession::close_readable() {
  readable_.reset();
}

void ItemInteractionSession::invalidate_item(const ObjectGuid item) {
  if (socket_.has_value() && socket_->item == item) {
    socket_.reset();
  }
  refund_quotes_.erase(item);
  if (pending_refund_.has_value() && pending_refund_->first == item) {
    pending_refund_.reset();
  }
  if (refund_result_.has_value() && refund_result_->item == item) {
    refund_result_.reset();
  }
  if (readable_.has_value() && readable_->item == item) {
    readable_.reset();
  }
  pending_text_.erase(item);
  if (pending_modification_item_ == item) {
    pending_modification_item_ = {};
    pending_enchant_spell_ = 0;
  }
}

}
