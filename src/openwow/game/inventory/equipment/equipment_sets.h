#pragma once

#include "openwow/game/inventory/player_inventory_replica.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

inline constexpr std::size_t kEquipmentSlotCount = 19;
inline constexpr std::size_t kMaximumEquipmentSets = 10;

struct EquipmentSet {
  ObjectGuid guid;
  std::uint32_t id = 0;
  std::string name;
  std::string icon;
  std::array<std::optional<ObjectGuid>, kEquipmentSlotCount> items{};
  std::bitset<kEquipmentSlotCount> ignored;
};

struct EquipmentSetSave {
  ObjectGuid guid;
  std::uint32_t id = 0;
  std::string name;
  std::string icon;
  std::array<std::optional<ObjectGuid>, kEquipmentSlotCount> items{};
  std::bitset<kEquipmentSlotCount> ignored;
};

struct EquipmentSetUseItem {
  ObjectGuid item;
  std::uint8_t source_bag = 0;
  std::uint8_t source_slot = 0;
};

struct EquipmentSetUse {
  std::array<EquipmentSetUseItem, kEquipmentSlotCount> items{};
};

struct EquipmentSetUseOutcome {
  bool success = false;
  bool bags_full = false;
  std::string set_name;
};

class EquipmentSets {
 public:
  void reset();
  void apply_list(std::vector<EquipmentSet> sets);
  void apply_saved(std::uint32_t id, ObjectGuid guid);
  [[nodiscard]] EquipmentSetUseOutcome apply_use_result(std::uint8_t result);

  [[nodiscard]] std::size_t size() const noexcept { return sets_.size(); }
  [[nodiscard]] bool received_list() const noexcept { return received_list_; }
  [[nodiscard]] const std::vector<EquipmentSet>& all() const noexcept {
    return sets_;
  }
  [[nodiscard]] const EquipmentSet* at(std::size_t index) const;
  [[nodiscard]] const EquipmentSet* find(std::uint32_t id) const;
  [[nodiscard]] const EquipmentSet* find(std::string_view name) const;
  [[nodiscard]] std::string names_containing(ObjectGuid item,
                                             std::string_view delimiter,
                                             std::size_t maximum_length) const;

  void ignore_next_save_slot(std::size_t slot, bool ignored);
  [[nodiscard]] bool next_save_slot_ignored(std::size_t slot) const;
  void clear_next_save_ignored_slots();

  [[nodiscard]] std::optional<EquipmentSetSave> prepare_save(
      std::string name, std::string icon,
      const PlayerInventoryReplica& inventory) const;
  [[nodiscard]] std::optional<EquipmentSetSave> prepare_rename(
      std::string_view old_name, std::string new_name) const;
  [[nodiscard]] std::optional<EquipmentSetUse> prepare_use(
      std::uint32_t id, const PlayerInventoryReplica& inventory,
      bool bank_open) const;
  [[nodiscard]] std::optional<std::uint32_t> pending_use() const noexcept {
    return pending_use_;
  }
  void mark_use_pending(std::uint32_t id);

 private:
  [[nodiscard]] std::optional<std::uint32_t> free_id() const;

  std::vector<EquipmentSet> sets_;
  std::bitset<kEquipmentSlotCount> ignored_next_save_;
  std::optional<std::uint32_t> pending_use_;
  bool received_list_ = false;
};

[[nodiscard]] std::string equipment_set_icon_path(std::string_view icon);

}
