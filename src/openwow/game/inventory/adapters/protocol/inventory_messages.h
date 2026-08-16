#pragma once
#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::game {

enum class InventoryResult : std::uint8_t {
  kOk                   = 0,
  kCantEquipLevel       = 1,
  kCantEquipSkill       = 2,
  kItemDoesntGoToSlot   = 3,
  kBagFull              = 4,
  kItemDoesntGoIntoBag  = 6,
  kItemNotFound         = 23,
  kNotEnoughMoney       = 29,
  kInventoryFull        = 50,
  kBankFull             = 51,
  kTooMuchGold          = 77,
  kAutoequipBindConfirm = 81,
  kMaxLimitCategoryCount = 84,
  kMaxLimitCategorySocketed = 85,
  kPurchaseLevelTooLow  = 87,
  kMaxLimitCategoryEquipped = 89,
};

[[nodiscard]] int InventoryResultCodeToSystemMessageId(std::uint32_t result_code);

struct ItemPushResult {
  ObjectGuid player_guid;
  std::uint32_t pushed{0};
  std::uint32_t created{0};
  std::uint32_t display_in_chat{0};
  std::uint8_t bag_slot{0};
  std::uint32_t item_slot{0};
  std::uint32_t item_entry{0};
  std::uint32_t suffix_factor{0};
  std::int32_t random_property_id{0};
  std::uint32_t count{0};
  std::uint32_t total_count{0};
};

struct InventoryChangeFailure {
  InventoryResult result{InventoryResult::kOk};
  ObjectGuid item1_guid;
  ObjectGuid item2_guid;
  std::uint8_t bag_type_subclass{0};
  std::uint32_t required_level{0};
};

namespace inventory_constants {
  constexpr std::uint8_t kMainBag = 255;
  constexpr std::uint8_t kEquipStart = 0;
  constexpr std::uint8_t kEquipEnd = 19;
  constexpr std::uint8_t kBagSlotsStart = 19;
  constexpr std::uint8_t kBagSlotsEnd = 23;
  constexpr std::uint8_t kBackpackStart = 23;
  constexpr std::uint8_t kBackpackEnd = 39;
  constexpr std::uint8_t kBankStart = 39;
  constexpr std::uint8_t kBankEnd = 67;
  constexpr std::uint8_t kBankBagStart = 67;
  constexpr std::uint8_t kBankBagEnd = 74;
  constexpr std::uint8_t kBuybackStart = 74;
  constexpr std::uint8_t kBuybackEnd = 86;
  constexpr std::uint8_t kKeyringStart = 86;
  constexpr std::uint8_t kKeyringEnd = 118;
}

class InventoryMessageState {
 public:
  InventoryMessageState() = default;

  static bool ParseItemPushResult(const std::uint8_t* data, std::size_t len,
                                  ItemPushResult& out);

  static bool ParseInventoryChangeFailure(const std::uint8_t* data, std::size_t len,
                                          InventoryChangeFailure& out);

  static net::wotlk::WorldPacket BuildAutoEquipItem(
      std::uint8_t src_bag, std::uint8_t src_slot);

  static net::wotlk::WorldPacket BuildSwapItem(
      std::uint8_t dest_bag, std::uint8_t dest_slot,
      std::uint8_t src_bag, std::uint8_t src_slot);

  static net::wotlk::WorldPacket BuildSwapInvItem(
      std::uint8_t dest_slot, std::uint8_t src_slot);

  static net::wotlk::WorldPacket BuildSplitItem(
      std::uint8_t src_bag, std::uint8_t src_slot,
      std::uint8_t dest_bag, std::uint8_t dest_slot,
      std::uint32_t count);

  void OnItemPushResult(const ItemPushResult& result);

  [[nodiscard]] const std::vector<ItemPushResult>& pending_push_results() const {
    return pending_push_results_;
  }

  void ClearPendingPushResults() { pending_push_results_.clear(); }

  void OnInventoryChangeFailure(const InventoryChangeFailure& failure);

  [[nodiscard]] const InventoryChangeFailure& last_failure() const {
    return last_failure_;
  }

 private:
  std::vector<ItemPushResult> pending_push_results_;
  InventoryChangeFailure last_failure_;
};

}
