#include "openwow/game/inventory/equipment/equipment_sets.h"

#include "openwow/core/storm_string.h"

#include <algorithm>

namespace openwow::game {
namespace {

bool same_name(const std::string_view left, const std::string_view right) {
  return openwow::core::SStrCmpI(left.data(), right.data(),
                                 0x7fffffffu) == 0;
}

std::optional<EquipmentSetUseItem> locate_item(
    const PlayerInventoryReplica& inventory, const ObjectGuid item,
    const bool bank_open) {
  const auto absolute = inventory.FindSlotByGuid(item.GetRawValue());
  if (absolute >= 0 &&
      !(absolute >= InventorySlots::kBuybackStart &&
        absolute < InventorySlots::kBuybackEnd) &&
      (bank_open || absolute < InventorySlots::kBankStart ||
       absolute >= InventorySlots::kBankEnd)) {
    return EquipmentSetUseItem{
        .item = item,
        .source_bag = InventorySlots::kMainBag,
        .source_slot = static_cast<std::uint8_t>(absolute),
    };
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* contents = inventory.GetBag(bag);
    if (contents == nullptr) {
      continue;
    }
    for (std::size_t slot = 0; slot < contents->slots.size(); ++slot) {
      if (contents->slots[slot].guid == item.GetRawValue()) {
        return EquipmentSetUseItem{
            .item = item,
            .source_bag = static_cast<std::uint8_t>(
                InventorySlots::kBagSlotsStart + bag - 1),
            .source_slot = static_cast<std::uint8_t>(slot),
        };
      }
    }
  }

  if (!bank_open) {
    return std::nullopt;
  }
  for (std::uint8_t bag = 0;
       bag < PlayerInventoryReplica::kMaxBankBags; ++bag) {
    const auto* contents = inventory.GetBankBag(bag);
    if (contents == nullptr) {
      continue;
    }
    for (std::size_t slot = 0; slot < contents->slots.size(); ++slot) {
      if (contents->slots[slot].guid == item.GetRawValue()) {
        return EquipmentSetUseItem{
            .item = item,
            .source_bag = static_cast<std::uint8_t>(
                InventorySlots::kBankBagStart + bag),
            .source_slot = static_cast<std::uint8_t>(slot),
        };
      }
    }
  }
  return std::nullopt;
}

}

void EquipmentSets::reset() {
  sets_.clear();
  ignored_next_save_.reset();
  pending_use_.reset();
  received_list_ = false;
}

void EquipmentSets::apply_list(std::vector<EquipmentSet> sets) {
  if (sets.size() > kMaximumEquipmentSets) {
    sets.resize(kMaximumEquipmentSets);
  }
  sets_ = std::move(sets);
  pending_use_.reset();
  received_list_ = true;
}

void EquipmentSets::apply_saved(const std::uint32_t id,
                                const ObjectGuid guid) {

  if (guid.IsEmpty()) {
    std::erase_if(sets_, [id](const EquipmentSet& set) { return set.id == id; });
    return;
  }
  if (auto found = std::ranges::find(sets_, id, &EquipmentSet::id);
      found != sets_.end()) {
    found->guid = guid;
  }
}

EquipmentSetUseOutcome EquipmentSets::apply_use_result(const std::uint8_t result) {

  EquipmentSetUseOutcome outcome;
  outcome.success = result == 0;
  outcome.bags_full = result == 4;

  const std::uint32_t pending_id = pending_use_.value_or(0);
  if (const auto* found = find(pending_id); found != nullptr) {
    outcome.set_name = found->name;
  }

  pending_use_.reset();
  return outcome;
}

const EquipmentSet* EquipmentSets::at(const std::size_t index) const {
  return index < sets_.size() ? &sets_[index] : nullptr;
}

const EquipmentSet* EquipmentSets::find(const std::uint32_t id) const {
  const auto found = std::ranges::find(sets_, id, &EquipmentSet::id);
  return found == sets_.end() ? nullptr : &*found;
}

const EquipmentSet* EquipmentSets::find(const std::string_view name) const {
  const auto found = std::ranges::find_if(
      sets_, [name](const EquipmentSet& set) {
        return same_name(set.name, name);
      });
  return found == sets_.end() ? nullptr : &*found;
}

std::string EquipmentSets::names_containing(
    const ObjectGuid item, const std::string_view delimiter,
    const std::size_t maximum_length) const {
  std::string result;
  for (const auto& set : sets_) {
    if (std::ranges::find(set.items, std::optional(item)) == set.items.end()) {
      continue;
    }
    const auto addition = (result.empty() ? 0 : delimiter.size()) + set.name.size();
    if (result.size() + addition > maximum_length) {
      break;
    }
    if (!result.empty()) {
      result.append(delimiter);
    }
    result.append(set.name);
  }
  return result;
}

std::string equipment_set_icon_path(const std::string_view icon) {
  if (icon.empty()) {
    return "Interface\\Icons\\INV_Misc_QuestionMark";
  }
  if (icon.find('\\') != std::string_view::npos ||
      icon.find('/') != std::string_view::npos) {
    return std::string(icon);
  }
  return "Interface\\Icons\\" + std::string(icon);
}

void EquipmentSets::ignore_next_save_slot(const std::size_t slot,
                                           const bool ignored) {
  if (slot < ignored_next_save_.size()) {
    ignored_next_save_.set(slot, ignored);
  }
}

bool EquipmentSets::next_save_slot_ignored(const std::size_t slot) const {
  return slot < ignored_next_save_.size() &&
         ignored_next_save_.test(slot);
}

void EquipmentSets::clear_next_save_ignored_slots() {
  ignored_next_save_.reset();
}

std::optional<std::uint32_t> EquipmentSets::free_id() const {
  for (std::uint32_t id = 0; id < kMaximumEquipmentSets; ++id) {
    if (find(id) == nullptr) {
      return id;
    }
  }
  return std::nullopt;
}

std::optional<EquipmentSetSave> EquipmentSets::prepare_save(
    std::string name, std::string icon,
    const PlayerInventoryReplica& inventory) const {
  const auto* existing = find(name);
  const auto id = existing != nullptr
                      ? std::optional(existing->id)
                      : free_id();
  if (!id.has_value()) {
    return std::nullopt;
  }

  EquipmentSetSave request{
      .guid = existing != nullptr ? existing->guid : ObjectGuid{},
      .id = *id,
      .name = std::move(name),
      .icon = std::move(icon),
  };
  for (std::size_t slot = 0; slot < request.items.size(); ++slot) {
    if (ignored_next_save_.test(slot)) {
      request.ignored.set(slot);
    } else if (const auto* item =
                   inventory.GetEquipSlot(static_cast<std::uint8_t>(slot));
               item != nullptr && !item->IsEmpty()) {
      request.items[slot] = ObjectGuid(item->guid);
    }
  }
  return request;
}

std::optional<EquipmentSetUse> EquipmentSets::prepare_use(
    const std::uint32_t id, const PlayerInventoryReplica& inventory,
    const bool bank_open) const {
  const auto* set = find(id);
  if (set == nullptr) {
    return std::nullopt;
  }

  EquipmentSetUse request;
  for (std::size_t slot = 0; slot < set->items.size(); ++slot) {
    if (set->ignored.test(slot)) {
      request.items[slot].item = ObjectGuid(1);
      continue;
    }
    if (!set->items[slot].has_value()) {
      continue;
    }
    if (const auto source =
            locate_item(inventory, *set->items[slot], bank_open);
        source.has_value()) {
      request.items[slot] = *source;
    }
  }
  return request;
}

std::optional<EquipmentSetSave> EquipmentSets::prepare_rename(
    const std::string_view old_name, std::string new_name) const {
  const auto* set = find(old_name);
  if (set == nullptr) {
    return std::nullopt;
  }
  return EquipmentSetSave{
      .guid = set->guid,
      .id = set->id,
      .name = std::move(new_name),
      .icon = set->icon,
      .items = set->items,
      .ignored = set->ignored,
  };
}

void EquipmentSets::mark_use_pending(const std::uint32_t id) {
  if (find(id) != nullptr) {
    pending_use_ = id;
  }
}

}
