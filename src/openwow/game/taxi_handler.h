
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "openwow/game/packet_reader.h"

namespace openwow::game {

enum class TaxiReply : std::uint32_t {
  kOk                   = 0,
  kUnspecifiedFailure   = 1,
  kNoSuchPath           = 2,
  kNotEnoughMoney       = 3,
  kTooFarAway           = 4,
  kNoVendorNearby       = 5,
  kNotVisited           = 6,
  kPlayerBusy           = 7,
  kPlayerAlreadyMounted = 8,
  kPlayerShapeShifted   = 9,
  kPlayerMoving         = 10,
  kSameNode             = 11,
  kNotStanding          = 12,
};

inline constexpr int kTaxiReplySystemMessages[] = {
    730,
    187,
    186,
    188,
    189,
    190,
    191,
    192,
    193,
    194,
    195,
    185,
    197,
};

inline constexpr std::size_t kTaxiReplySystemMessagesCount =
    sizeof(kTaxiReplySystemMessages) / sizeof(kTaxiReplySystemMessages[0]);

inline constexpr std::size_t kTaxiMaskSize = 14;

inline constexpr std::size_t kMaxTaxiNodes = kTaxiMaskSize * 32;

struct TaxiNodeDisplay {
  std::uint32_t window_info = 0;
  std::uint64_t npc_guid = 0;
  std::uint32_t current_node = 0;
  std::array<std::uint32_t, kTaxiMaskSize> mask{};
};

struct TaxiNodeStatus {
  std::uint64_t npc_guid = 0;
  std::uint8_t status = 0;
};

struct TaxiNodeInfo {
  std::uint32_t node_id = 0;
  std::string name;
  std::uint32_t map_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  std::uint32_t mount_display_id = 0;
};

enum class TaxiShowResult {
  kOpenMap,
  kErrorNoPath,
  kConsoleDump,
  kParseError,
};

class TaxiHandler {
 public:

  TaxiShowResult HandleShowTaxiNodes(const std::uint8_t* data, std::size_t len);
  bool HandleActivateTaxiReply(const std::uint8_t* data, std::size_t len);
  bool HandleNewTaxiPath(const std::uint8_t* data, std::size_t len);
  bool HandleTaxiNodeStatus(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] const TaxiNodeDisplay& last_display() const { return display_; }
  [[nodiscard]] TaxiReply last_reply() const { return reply_; }
  [[nodiscard]] bool new_path_received() const { return new_path_; }
  [[nodiscard]] const TaxiNodeStatus& last_status() const { return status_; }

  [[nodiscard]] bool IsNodeKnown(std::uint32_t node_id) const;

  void SetNodeKnown(std::uint32_t node_id);

  void ClearNodeKnown(std::uint32_t node_id);

  [[nodiscard]] std::uint32_t GetKnownNodeCount() const;

  [[nodiscard]] std::vector<std::uint32_t> GetKnownNodeIds() const;

  [[nodiscard]] std::uint32_t GetCurrentNode() const { return display_.current_node; }

  [[nodiscard]] std::uint64_t GetFlightMasterGuid() const { return display_.npc_guid; }

  [[nodiscard]] bool IsTaxiMapOpen() const { return taxi_map_open_; }

  [[nodiscard]] std::uint32_t GetReachableNodeCount() const { return reachable_count_; }

  void SetSelectedDestination(std::uint32_t node_id);
  [[nodiscard]] std::uint32_t GetSelectedDestination() const { return selected_dest_; }

  void SetInFlight(bool in_flight);
  [[nodiscard]] bool IsInFlight() const { return in_flight_; }

  [[nodiscard]] static const char* TaxiReplyToString(TaxiReply reply);

  [[nodiscard]] bool WasLastReplyOk() const { return reply_ == TaxiReply::kOk; }
  void CloseTaxiMap() {
    display_.npc_guid = 0;
    display_.current_node = 0;
    display_.window_info = 0;
    taxi_map_open_ = false;
    selected_dest_ = 0;
    reachable_count_ = 0;
  }

  void Clear();

 private:
  TaxiNodeDisplay display_{};
  TaxiReply reply_ = TaxiReply::kOk;
  bool new_path_ = false;
  TaxiNodeStatus status_{};

  bool taxi_map_open_ = false;
  std::uint32_t selected_dest_ = 0;
  bool in_flight_ = false;
  std::uint32_t reachable_count_ = 0;
};

}
