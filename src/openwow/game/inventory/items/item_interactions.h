#pragma once

#include "openwow/game/object_guid.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::game {

struct RefundCost {
  std::uint32_t item_id = 0;
  std::uint32_t count = 0;
};

struct RefundQuote {
  ObjectGuid item;
  std::uint32_t money = 0;
  std::uint32_t honor = 0;
  std::uint32_t arena = 0;
  std::array<RefundCost, 5> required_items{};
  std::uint32_t time_left = 0;
};

struct RefundResult {
  ObjectGuid item;
  std::uint32_t error = 0;
  std::uint32_t money = 0;
  std::uint32_t honor = 0;
  std::uint32_t arena = 0;
  std::array<RefundCost, 5> returned_items{};
};

struct PendingSocketGem {
  ObjectGuid item;
  ObjectGuid container;
  std::uint8_t source_slot = 0;
  std::uint32_t item_id = 0;
  std::uint32_t gem_properties = 0;
  std::uint8_t color = 0;
  std::string name;
  std::string icon_path;
};

struct SocketDraft {
  ObjectGuid item;
  std::array<ObjectGuid, 3> gems{};
  std::array<std::uint8_t, 3> socket_masks{};
  std::array<std::optional<PendingSocketGem>, 3> pending_gems{};
  std::uint8_t socket_count = 0;
  std::uint64_t generation = 0;
};

struct ReadableItem {
  ObjectGuid item;
  ObjectGuid creator;
  std::vector<std::uint32_t> pages;
  std::size_t page = 0;
  std::string text;
  std::uint32_t material = 0;
  std::uint64_t generation = 0;
  bool opened = false;
};

class ItemInteractionSession {
 public:
  void reset(std::uint64_t player_generation);
  [[nodiscard]] std::uint64_t player_generation() const noexcept {
    return player_generation_;
  }

  [[nodiscard]] std::uint64_t begin_request();
  [[nodiscard]] std::uint64_t request_generation() const noexcept {
    return request_generation_;
  }
  [[nodiscard]] bool current_request(std::uint64_t generation) const noexcept;

  void begin_socket(ObjectGuid item,
                    std::array<std::uint8_t, 3> socket_masks = {},
                    std::uint8_t socket_count = 0);
  [[nodiscard]] const std::optional<SocketDraft>& socket() const noexcept {
    return socket_;
  }
  bool set_socket_gem(std::size_t socket, ObjectGuid gem);
  [[nodiscard]] std::optional<PendingSocketGem> place_socket_gem(
      std::size_t socket, std::optional<PendingSocketGem> gem);
  bool apply_socket_result(ObjectGuid item);
  void cancel_socket();

  [[nodiscard]] bool apply_refund_quote(std::uint64_t request_generation,
                                        RefundQuote quote);
  [[nodiscard]] bool apply_refund_result(std::uint64_t request_generation,
                                         RefundResult result);
  [[nodiscard]] const RefundQuote* refund_quote(ObjectGuid item) const;
  [[nodiscard]] const std::optional<RefundResult>& refund_result() const {
    return refund_result_;
  }
  [[nodiscard]] std::uint64_t begin_refund_request(ObjectGuid item);

  void begin_readable(ObjectGuid item);
  [[nodiscard]] const std::optional<ReadableItem>& readable() const noexcept {
    return readable_;
  }
  [[nodiscard]] std::optional<ReadableItem>& readable() noexcept {
    return readable_;
  }
  bool begin_text_query(ObjectGuid item);
  [[nodiscard]] bool cache_text(ObjectGuid item, std::string text);
  [[nodiscard]] const std::string* cached_text(ObjectGuid item) const;
  void clear_text_cache();
  void close_readable();

  void invalidate_item(ObjectGuid item);
  void set_pending_modification(ObjectGuid item) {
    pending_modification_item_ = item;
  }
  [[nodiscard]] ObjectGuid pending_modification() const {
    return pending_modification_item_;
  }
  void set_pending_enchant_spell(std::uint32_t spell) {
    pending_enchant_spell_ = spell;
  }
  [[nodiscard]] std::uint32_t consume_pending_enchant_spell() {
    const auto spell = pending_enchant_spell_;
    pending_enchant_spell_ = 0;
    return spell;
  }

 private:
  std::uint64_t player_generation_ = 0;
  std::uint64_t request_generation_ = 0;
  std::optional<SocketDraft> socket_;
  std::unordered_map<ObjectGuid, RefundQuote, ObjectGuid::Hash> refund_quotes_;
  std::optional<RefundResult> refund_result_;
  std::optional<std::pair<ObjectGuid, std::uint64_t>> pending_refund_;
  std::optional<ReadableItem> readable_;
  std::unordered_map<ObjectGuid, std::string, ObjectGuid::Hash> item_text_;
  std::unordered_set<ObjectGuid, ObjectGuid::Hash> pending_text_;
  ObjectGuid pending_modification_item_;
  std::uint32_t pending_enchant_spell_ = 0;
};

}
