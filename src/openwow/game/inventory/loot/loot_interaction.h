
#pragma once

#include "openwow/game/inventory/loot/loot_roll_messages.h"
#include "openwow/game/inventory/loot/loot_state.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "openwow/game/object_guid.h"
namespace openwow::game {

class CharacterMapRuntime;

enum class LootType : std::uint8_t {
  kNone = 0,
  kCorpse = 1,
  kPickpocketing = 2,
  kFishing = 3,
  kDisenchanting = 4,
  kSkinning = 6,
  kProspecting = 7,
  kMilling = 8,
};

enum class LootResponseError : std::uint8_t {
  kTooFar = 4,
  kBadFacing = 5,
  kLocked = 6,
  kNotStanding = 8,
  kStunned = 9,
  kPlayerNotFound = 10,
  kPlayTimeExceeded = 11,
  kMasterInvFull = 12,
  kMasterUniqueItem = 13,
  kMasterOther = 14,
  kShapeshifted = 16,
};

struct LootErrorMapping {
  int system_message_id;
  bool clears_active_loot;
};
[[nodiscard]] LootErrorMapping MapLootResponseError(std::uint8_t sub_type);

enum class LootSlotType : std::uint8_t {
  kAllowLoot = 0,
  kRollOngoing = 1,
  kMaster = 2,
  kLocked = 3,
  kOwner = 4,
};

enum class LootRollType : std::uint8_t {
  kPass = 0,
  kNeed = 1,
  kGreed = 2,
  kDisenchant = 3,
};

enum class LootItemTemplateState : std::uint8_t {
  kReady = 0,
  kPendingOpen = 1,
  kPendingSlotChanged = 2,
};

struct LootItem {
  std::uint8_t slot_index = 0;
  std::uint8_t display_index = 0;
  std::uint32_t item_id = 0;
  std::uint32_t count = 0;
  std::uint32_t display_info_id = 0;
  std::uint32_t random_suffix = 0;
  std::uint32_t random_property_id = 0;
  LootSlotType slot_type = LootSlotType::kAllowLoot;
  LootItemTemplateState template_state = LootItemTemplateState::kReady;
};

struct LootWindow {
  ObjectGuid source_guid;
  LootType loot_type = LootType::kNone;
  std::uint8_t error_sub_type = 0;
  std::uint32_t gold = 0;

  bool gold_slot_reserved = false;
  std::vector<LootItem> items;
};

struct LootMoneyNotify {
  std::uint32_t copper = 0;
  bool is_solo = false;
};

struct LootStartRoll {
  ObjectGuid item_guid;
  std::uint32_t map_id = 0;
  std::uint32_t item_slot = 0;
  std::uint32_t item_id = 0;
  std::uint32_t random_suffix = 0;
  std::uint32_t random_property_id = 0;
  std::uint32_t item_count = 0;
  std::uint32_t countdown_ms = 0;
  std::uint8_t roll_vote_mask = 0;
};

struct LootRollWon {
  std::uint64_t source_guid = 0;
  std::uint32_t slot = 0;
  std::uint32_t item_id = 0;
  std::uint32_t random_suffix = 0;
  std::uint32_t random_property_id = 0;
  std::uint64_t winner_guid = 0;
  std::uint8_t roll_number = 0;
  std::uint8_t roll_type = 0;
};

struct LootItemNotify {
  std::uint64_t looter_guid = 0;
  std::uint8_t slot = 0;
  std::uint8_t unknown_byte = 0;
  std::uint32_t item_id = 0;
  std::string text;
};

struct LootList {
  std::uint64_t creature_guid = 0;
  ObjectGuid master_looter;
  ObjectGuid group_looter;
};

struct LootMasterList {
  std::vector<std::uint64_t> player_guids;
};

struct LootSlotChanged {
  std::uint64_t loot_guid = 0;
  std::uint8_t slot = 0;
  std::uint32_t item_id = 0;
  std::uint32_t display_info_id = 0;
  std::int32_t suffix_factor = 0;
  std::int32_t random_property_id = 0;
  std::uint32_t count = 0;
};

class LootInteraction {
 public:
  LootInteraction(PlayerInventoryReplica& inventory, ItemDefinitions& item_definitions,
                  CharacterMapRuntime& map_runtime)
      : state_(inventory, item_definitions),
        item_definitions_(item_definitions),
        map_runtime_(map_runtime) {}
  [[nodiscard]] LootState& state() noexcept { return state_; }
  [[nodiscard]] const LootState& state() const noexcept { return state_; }
  void BindDbc(const openwow::data::dbc::DbcLoader* dbc) {
    state_.BindDbc(dbc);
  }
  static constexpr std::size_t kMaxLootSlots = 18;

  struct BindConfirmation {
    std::uint32_t item_id = 0;
    std::uint32_t count = 0;
    int ui_slot = 0;
  };

  struct AutoLootPlan {
    bool loot_money = false;
    std::vector<std::uint8_t> loot_slots;
    std::vector<BindConfirmation> bind_confirmations;
    bool remains_open = false;
  };

  struct ItemQueryRequest {
    std::uint64_t request_id = 0;
    std::uint32_t item_id = 0;
  };

  struct LootSlotChangedResult {
    bool applied = false;
    std::optional<int> immediate_changed_ui_slot;
    std::optional<ItemQueryRequest> item_query;
  };

  struct ItemQueryResolution {
    std::vector<int> changed_ui_slots;
    bool fire_loot_opened = false;
  };

  struct LootReleaseResponseResult {
    bool should_close = false;
  };

  struct LootClearMoneyResult {
    bool cleared_gold = false;
    bool should_release_and_close = false;
  };

  void SetLootWindow(LootWindow loot_window);
  void BeginLootRequest(ObjectGuid source);
  void ExpectServerLootResponse(ObjectGuid source) { BeginLootRequest(source); }
  [[nodiscard]] bool ConsumeLootResponse(ObjectGuid source);
  [[nodiscard]] LootReleaseResponseResult HandleLootReleaseResponse(
      ObjectGuid source_guid, bool accepted) const;
  bool HandleLootRemoved(std::uint8_t wire_slot);
  [[nodiscard]] LootClearMoneyResult HandleLootClearMoney();
  void HandleLootMoneyNotify(LootMoneyNotify notify);
  void HandleLootRollWon(LootRollWon result);
  void HandleLootItemNotify(LootItemNotify notify);
  void HandleLootList(LootList list);
  void HandleLootMasterList(LootMasterList list);
  void HandleLootSlotChanged(LootSlotChanged changed);
  bool HandleDynamicDropRollResult();

  bool SetSlotAllowLootForActiveSource(std::uint64_t loot_guid,
                                       std::uint32_t slot);

  std::vector<ItemQueryRequest> BeginLootWindowItemQueries(
      const std::function<bool(std::uint32_t)>& has_item_template);
  LootSlotChangedResult ApplyLootSlotChanged(const LootSlotChanged& changed,
                                             bool has_item_template);
  ItemQueryResolution ResolveItemQuery(std::uint64_t request_id,
                                       bool success);

  [[nodiscard]] bool is_looting() const { return loot_window_.has_value(); }
  [[nodiscard]] const LootWindow& loot_window() const {
    return loot_window_.value();
  }
  [[nodiscard]] const std::optional<LootMoneyNotify>& last_money_notify()
      const {
    return last_money_notify_;
  }
  [[nodiscard]] const std::optional<LootRollWon>& last_roll_won() const {
    return last_roll_won_;
  }
  [[nodiscard]] const std::optional<LootItemNotify>& last_item_notify() const {
    return last_item_notify_;
  }
  [[nodiscard]] const std::optional<LootList>& last_loot_list() const {
    return last_loot_list_;
  }
  [[nodiscard]] const std::optional<LootMasterList>& last_master_list() const {
    return last_master_list_;
  }
  [[nodiscard]] const std::optional<LootSlotChanged>& last_slot_changed() const {
    return last_slot_changed_;
  }
  [[nodiscard]] bool dynamic_drop_received() const { return dynamic_drop_received_; }
  [[nodiscard]] bool pending_auto_loot() const { return pending_auto_loot_; }
  [[nodiscard]] LootType cached_loot_type() const { return cached_loot_type_; }
  [[nodiscard]] bool HasPendingItemQueries() const {
    return !pending_item_queries_.empty();
  }

  [[nodiscard]] static const LootItem* FindItemByWireSlot(
      const LootWindow& loot_window, std::uint8_t wire_slot);
  [[nodiscard]] static LootItem* FindItemByWireSlot(
      LootWindow& loot_window, std::uint8_t wire_slot);
  [[nodiscard]] static const LootItem* FindItemByDisplayIndex(
      const LootWindow& loot_window, std::size_t display_index);
  [[nodiscard]] static std::optional<std::size_t> FindDisplayIndexForWireSlot(
      const LootWindow& loot_window, std::uint8_t wire_slot);
  [[nodiscard]] static std::size_t GetDisplaySlotCount(
      const LootWindow& loot_window);

  [[nodiscard]] bool HasLootItems() const;

  void CloseLootWindow();

  [[nodiscard]] ObjectGuid TakePendingReleaseGuid() noexcept;

  void SetPendingAutoLoot(bool enabled);
  [[nodiscard]] AutoLootPlan TakePendingAutoLootPlan();

  void Clear();

 private:
  LootState state_;
  ItemDefinitions& item_definitions_;
  CharacterMapRuntime& map_runtime_;
  struct PendingItemQuery {
    std::uint64_t generation = 0;
    std::uint32_t item_id = 0;
    std::uint8_t slot_index = 0;
  };

  [[nodiscard]] std::optional<std::size_t> FindFirstFreeDisplayIndex() const;
  [[nodiscard]] ItemQueryRequest RegisterItemQuery(const LootItem& item);
  void ResetItemQueries();
  void RemoveLocalLootSlot(std::uint8_t slot);
  void RemovePendingQueriesForSlot(std::uint8_t slot);

  std::optional<LootWindow> loot_window_;
  ObjectGuid pending_release_guid_;
  std::optional<ObjectGuid> pending_source_;
  std::optional<LootMoneyNotify> last_money_notify_;
  std::optional<LootRollWon> last_roll_won_;
  std::optional<LootItemNotify> last_item_notify_;
  std::optional<LootList> last_loot_list_;
  std::optional<LootMasterList> last_master_list_;
  std::optional<LootSlotChanged> last_slot_changed_;
  bool dynamic_drop_received_ = false;
  bool pending_auto_loot_ = false;
  LootType cached_loot_type_ = LootType::kNone;
  std::array<std::optional<LootSlotType>, kMaxLootSlots> wire_slot_types_{};
  std::unordered_map<std::uint64_t, PendingItemQuery> pending_item_queries_;
  std::uint64_t loot_generation_ = 0;
  std::uint64_t next_item_query_request_id_ = 1;
  friend struct LootInteractionTestAccess;
};

}
