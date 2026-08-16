
#include "openwow/game/taxi_handler.h"

#include <cstring>

namespace openwow::game {

namespace {

class RetailTaxiPacketReader {
 public:
  RetailTaxiPacketReader(const std::uint8_t* data, const std::size_t size)
      : data_(data), size_(size) {}

  void ReadU32(std::uint32_t& value) { Read(value); }
  void ReadU64(std::uint64_t& value) { Read(value); }

 private:
  template <typename T>
  void Read(T& value) {
    if (overrun_ || size_ - position_ < sizeof(value)) {
      overrun_ = true;
      return;
    }
    std::memcpy(&value, data_ + position_, sizeof(value));
    position_ += sizeof(value);
  }

  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t position_ = 0;
  bool overrun_ = false;
};

[[nodiscard]] std::uint32_t CountSetTaxiNodes(
    const TaxiNodeDisplay& display) {
  std::uint32_t count = 0;
  for (const auto word : display.mask) {
    std::uint32_t bits = word;
    while (bits != 0) {
      count += bits & 1u;
      bits >>= 1;
    }
  }
  return count;
}

}

TaxiShowResult TaxiHandler::HandleShowTaxiNodes(const std::uint8_t* data,
                                                  std::size_t len) {
  RetailTaxiPacketReader reader(data, len);
  TaxiNodeDisplay display{};
  reader.ReadU32(display.window_info);

  if (display.window_info != 0) {
    reader.ReadU64(display.npc_guid);
    reader.ReadU32(display.current_node);
  }

  for (std::size_t word = 0; word < kTaxiMaskSize; word += 2) {
    std::uint64_t mask_pair = 0;
    reader.ReadU64(mask_pair);
    display.mask[word] = static_cast<std::uint32_t>(mask_pair);
    display.mask[word + 1] = static_cast<std::uint32_t>(mask_pair >> 32);
  }

  display_ = display;
  reachable_count_ = CountSetTaxiNodes(display_);

  if (display_.window_info != 0) {
    if (reachable_count_ >= 2) {
      selected_dest_ = 0;
      taxi_map_open_ = true;
      new_path_ = false;
      return TaxiShowResult::kOpenMap;
    } else {
      return TaxiShowResult::kErrorNoPath;
    }
  }

  return TaxiShowResult::kConsoleDump;
}

bool TaxiHandler::HandleActivateTaxiReply(const std::uint8_t* data,
                                            std::size_t len) {

  std::uint32_t reply = 0;
  if (data != nullptr && len >= sizeof(reply)) {
    reply = static_cast<std::uint32_t>(data[0]) |
            (static_cast<std::uint32_t>(data[1]) << 8) |
            (static_cast<std::uint32_t>(data[2]) << 16) |
            (static_cast<std::uint32_t>(data[3]) << 24);
  }
  reply_ = static_cast<TaxiReply>(reply);

  if (reply_ == TaxiReply::kOk) {
    taxi_map_open_ = false;
  }

  return true;
}

bool TaxiHandler::HandleNewTaxiPath(const std::uint8_t* ,
                                      std::size_t ) {
  new_path_ = true;

  return true;
}

bool TaxiHandler::HandleTaxiNodeStatus(const std::uint8_t* data,
                                         std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(status_.npc_guid)) return false;
  if (!r.ReadU8(status_.status)) return false;
  return true;
}

bool TaxiHandler::IsNodeKnown(std::uint32_t node_id) const {
  if (node_id == 0 || node_id > kMaxTaxiNodes) return false;
  const std::uint32_t zero_based = node_id - 1;
  std::uint32_t word_index = zero_based / 32;
  std::uint32_t bit_index  = zero_based % 32;
  if (word_index >= kTaxiMaskSize) return false;
  return (display_.mask[word_index] & (1u << bit_index)) != 0;
}

void TaxiHandler::SetNodeKnown(std::uint32_t node_id) {
  if (node_id == 0 || node_id > kMaxTaxiNodes) return;
  const std::uint32_t zero_based = node_id - 1;
  std::uint32_t word_index = zero_based / 32;
  std::uint32_t bit_index  = zero_based % 32;
  if (word_index >= kTaxiMaskSize) return;
  display_.mask[word_index] |= (1u << bit_index);
}

void TaxiHandler::ClearNodeKnown(std::uint32_t node_id) {
  if (node_id == 0 || node_id > kMaxTaxiNodes) return;
  const std::uint32_t zero_based = node_id - 1;
  std::uint32_t word_index = zero_based / 32;
  std::uint32_t bit_index  = zero_based % 32;
  if (word_index >= kTaxiMaskSize) return;
  display_.mask[word_index] &= ~(1u << bit_index);
}

std::uint32_t TaxiHandler::GetKnownNodeCount() const {
  std::uint32_t count = 0;
  for (std::size_t w = 0; w < kTaxiMaskSize; ++w) {

    std::uint32_t bits = display_.mask[w];
    while (bits) {
      count += bits & 1u;
      bits >>= 1;
    }
  }
  return count;
}

std::vector<std::uint32_t> TaxiHandler::GetKnownNodeIds() const {
  std::vector<std::uint32_t> ids;
  for (std::size_t w = 0; w < kTaxiMaskSize; ++w) {
    std::uint32_t bits = display_.mask[w];
    while (bits) {

      std::uint32_t bit = bits & (~bits + 1);
      std::uint32_t bit_index = 0;
      std::uint32_t tmp = bit;
      while (tmp >>= 1) ++bit_index;
      std::uint32_t node_id =
          static_cast<std::uint32_t>(w * 32) + bit_index + 1;
      ids.push_back(node_id);
      bits &= ~bit;
    }
  }
  return ids;
}

void TaxiHandler::SetSelectedDestination(std::uint32_t node_id) {
  selected_dest_ = node_id;
}

void TaxiHandler::SetInFlight(bool in_flight) {
  in_flight_ = in_flight;
  if (!in_flight) {

    selected_dest_ = 0;
  }
}

const char* TaxiHandler::TaxiReplyToString(TaxiReply reply) {
  switch (reply) {
    case TaxiReply::kOk:                   return "OK";
    case TaxiReply::kUnspecifiedFailure:   return "Unspecified failure";
    case TaxiReply::kNoSuchPath:           return "No such path";
    case TaxiReply::kNotEnoughMoney:       return "Not enough money";
    case TaxiReply::kTooFarAway:           return "Too far away";
    case TaxiReply::kNoVendorNearby:       return "No vendor nearby";
    case TaxiReply::kNotVisited:           return "Not visited";
    case TaxiReply::kPlayerBusy:           return "Player busy";
    case TaxiReply::kPlayerAlreadyMounted: return "Already mounted";
    case TaxiReply::kPlayerShapeShifted:   return "Shapeshifted";
    case TaxiReply::kPlayerMoving:         return "Player moving";
    case TaxiReply::kSameNode:             return "Same node";
    case TaxiReply::kNotStanding:          return "Not standing";
    default:                               return "Unknown";
  }
}

void TaxiHandler::Clear() {
  display_ = TaxiNodeDisplay{};
  reply_ = TaxiReply::kOk;
  new_path_ = false;
  status_ = TaxiNodeStatus{};
  taxi_map_open_ = false;
  selected_dest_ = 0;
  in_flight_ = false;
  reachable_count_ = 0;
}

}
