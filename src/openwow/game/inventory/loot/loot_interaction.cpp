
#include "openwow/game/inventory/loot/loot_interaction.h"

#include "openwow/game/character_map_runtime.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/object_manager.h"

#include <algorithm>

namespace openwow::game {

namespace {

bool IsPendingTemplateState(const LootItemTemplateState state) {
  return state == LootItemTemplateState::kPendingOpen ||
         state == LootItemTemplateState::kPendingSlotChanged;
}

}

const LootItem* LootInteraction::FindItemByWireSlot(const LootWindow& loot_window,
                                                const std::uint8_t wire_slot) {
  const auto it = std::find_if(
      loot_window.items.begin(), loot_window.items.end(),
      [wire_slot](const LootItem& item) { return item.slot_index == wire_slot; });
  return it != loot_window.items.end() ? &*it : nullptr;
}

LootItem* LootInteraction::FindItemByWireSlot(LootWindow& loot_window,
                                          const std::uint8_t wire_slot) {
  const auto it = std::find_if(
      loot_window.items.begin(), loot_window.items.end(),
      [wire_slot](const LootItem& item) { return item.slot_index == wire_slot; });
  return it != loot_window.items.end() ? &*it : nullptr;
}

const LootItem* LootInteraction::FindItemByDisplayIndex(
    const LootWindow& loot_window,
    const std::size_t display_index) {
  const auto it =
      std::find_if(loot_window.items.begin(), loot_window.items.end(),
                   [display_index](const LootItem& item) {
                     return item.display_index == display_index;
                   });
  return it != loot_window.items.end() ? &*it : nullptr;
}

std::optional<std::size_t> LootInteraction::FindDisplayIndexForWireSlot(
    const LootWindow& loot_window,
    const std::uint8_t wire_slot) {
  const auto* item = FindItemByWireSlot(loot_window, wire_slot);
  if (!item) {
    return std::nullopt;
  }
  return item->display_index;
}

std::size_t LootInteraction::GetDisplaySlotCount(const LootWindow& loot_window) {
  std::size_t highest_slot_count = 0;
  for (const LootItem& item : loot_window.items) {
    highest_slot_count =
        std::max(highest_slot_count,
                 static_cast<std::size_t>(item.display_index) + 1);
  }
  return highest_slot_count;
}

bool LootInteraction::HasLootItems() const {
  if (!loot_window_.has_value()) {
    return false;
  }

  const LootWindow& loot_window = *loot_window_;
  if (loot_window.source_guid.IsEmpty() ||
      map_runtime_.objects().Get(loot_window.source_guid) == nullptr) {
    return false;
  }

  if (loot_window.gold > 0) {
    return true;
  }

  return std::any_of(loot_window.items.begin(), loot_window.items.end(),
                     [](const LootItem& item) { return item.item_id != 0; });
}

LootErrorMapping MapLootResponseError(const std::uint8_t sub_type) {
  switch (sub_type) {
    case 4:  return {139, true};
    case 5:  return {141, true};
    case 6:  return {138, true};
    case 8:  return {142, true};
    case 9:  return {143, true};
    case 10: return {444, false};
    case 11: return {489, true};
    case 12: return {508, false};
    case 13: return {509, false};
    case 14: return {510, false};
    case 16: return {449, false};
    default: return {140, true};
  }
}

void LootInteraction::SetLootWindow(LootWindow loot_window) {
  ResetItemQueries();
  ++loot_generation_;
  wire_slot_types_.fill(std::nullopt);
  cached_loot_type_ = loot_window.loot_type;
  loot_window.gold_slot_reserved =
      loot_window.gold_slot_reserved || loot_window.gold > 0;
  for (LootItem& item : loot_window.items) {
    item.template_state = LootItemTemplateState::kReady;
    if (item.slot_index < kMaxLootSlots) {
      wire_slot_types_[item.slot_index] = item.slot_type;
    }
  }
  loot_window_ = std::move(loot_window);

  pending_release_guid_ = loot_window_->source_guid;
}

ObjectGuid LootInteraction::TakePendingReleaseGuid() noexcept {
  const ObjectGuid guid = pending_release_guid_;
  pending_release_guid_ = ObjectGuid{};
  return guid;
}

void LootInteraction::BeginLootRequest(const ObjectGuid source) {
  pending_source_ = source;
}

bool LootInteraction::ConsumeLootResponse(const ObjectGuid source) {
  if (!pending_source_.has_value() || *pending_source_ != source) {
    return false;
  }
  pending_source_.reset();
  return true;
}

LootInteraction::LootReleaseResponseResult LootInteraction::HandleLootReleaseResponse(
    const ObjectGuid source_guid, const bool accepted) const {
  LootReleaseResponseResult result;
  result.should_close =
      accepted && loot_window_.has_value() && loot_window_->source_guid == source_guid;
  return result;
}

bool LootInteraction::HandleLootRemoved(const std::uint8_t wire_slot) {
  if (wire_slot < kMaxLootSlots) {
    wire_slot_types_[wire_slot].reset();
  }

  if (loot_window_) {
    RemovePendingQueriesForSlot(wire_slot);
    auto& items = loot_window_->items;
    const std::size_t old_size = items.size();
    items.erase(
        std::remove_if(items.begin(), items.end(),
                       [wire_slot](const LootItem& i) {
                         return i.slot_index == wire_slot;
                       }),
        items.end());
    if (items.size() == old_size) {
      return true;
    }
    if (!HasLootItems()) {
      CloseLootWindow();
    }
  }
  return true;
}

LootInteraction::LootClearMoneyResult LootInteraction::HandleLootClearMoney() {
  LootClearMoneyResult result;
  if (!loot_window_ || loot_window_->gold == 0) {
    return result;
  }

  loot_window_->gold = 0;
  result.cleared_gold = true;
  result.should_release_and_close = !HasLootItems();
  return result;
}

void LootInteraction::HandleLootMoneyNotify(LootMoneyNotify notify) {
  last_money_notify_ = notify;
}

void LootInteraction::HandleLootRollWon(LootRollWon result) {
  last_roll_won_ = result;
}

bool LootInteraction::SetSlotAllowLootForActiveSource(const std::uint64_t loot_guid,
                                                  const std::uint32_t slot) {
  if (slot >= kMaxLootSlots || !loot_window_ ||
      loot_window_->source_guid.GetRawValue() != loot_guid) {
    return false;
  }

  wire_slot_types_[slot] = LootSlotType::kAllowLoot;
  if (LootItem* item =
          FindItemByWireSlot(*loot_window_, static_cast<std::uint8_t>(slot));
      item != nullptr) {
    item->slot_type = LootSlotType::kAllowLoot;
  }

  return true;
}

void LootInteraction::HandleLootItemNotify(LootItemNotify notify) {
  last_item_notify_ = std::move(notify);
}

void LootInteraction::HandleLootList(LootList list) {
  last_loot_list_ = std::move(list);
}

void LootInteraction::HandleLootMasterList(LootMasterList list) {
  last_master_list_ = std::move(list);
}

void LootInteraction::HandleLootSlotChanged(LootSlotChanged changed) {
  last_slot_changed_ = changed;
}

std::vector<LootInteraction::ItemQueryRequest> LootInteraction::BeginLootWindowItemQueries(
    const std::function<bool(std::uint32_t)>& has_item_template) {
  std::vector<ItemQueryRequest> requests;
  if (!loot_window_) {
    return requests;
  }

  for (LootItem& item : loot_window_->items) {
    item.template_state = LootItemTemplateState::kReady;
    if (has_item_template(item.item_id)) {
      continue;
    }

    item.template_state = LootItemTemplateState::kPendingOpen;
    requests.push_back(RegisterItemQuery(item));
  }

  return requests;
}

LootInteraction::LootSlotChangedResult LootInteraction::ApplyLootSlotChanged(
    const LootSlotChanged& changed,
    const bool has_item_template) {
  last_slot_changed_ = changed;

  LootSlotChangedResult result;
  if (!loot_window_ ||
      loot_window_->source_guid.GetRawValue() != changed.loot_guid) {
    return result;
  }

  LootItem* item = FindItemByWireSlot(*loot_window_, changed.slot);
  if (!item) {
    const auto free_display_index = FindFirstFreeDisplayIndex();
    if (!free_display_index) {
      return result;
    }

    LootItem new_item;
    new_item.slot_index = changed.slot;
    new_item.display_index = static_cast<std::uint8_t>(*free_display_index);
    if (changed.slot < kMaxLootSlots && wire_slot_types_[changed.slot].has_value()) {
      new_item.slot_type = *wire_slot_types_[changed.slot];
    }
    loot_window_->items.push_back(new_item);
    item = &loot_window_->items.back();
  }

  result.applied = true;
  item->item_id = changed.item_id;
  item->display_info_id = changed.display_info_id;
  item->count = changed.count;
  item->random_suffix = static_cast<std::uint32_t>(changed.suffix_factor);
  item->random_property_id =
      static_cast<std::uint32_t>(changed.random_property_id);
  if (changed.slot < kMaxLootSlots && wire_slot_types_[changed.slot].has_value()) {
    item->slot_type = *wire_slot_types_[changed.slot];
  }
  item->template_state = LootItemTemplateState::kReady;

  if (!has_item_template) {
    item->template_state = LootItemTemplateState::kPendingSlotChanged;
    result.item_query = RegisterItemQuery(*item);
    return result;
  }

  if (!HasPendingItemQueries()) {
    result.immediate_changed_ui_slot =
        static_cast<int>(item->display_index) + 1 +
        (loot_window_->gold_slot_reserved ? 1 : 0);
  }

  return result;
}

LootInteraction::ItemQueryResolution LootInteraction::ResolveItemQuery(
    const std::uint64_t request_id,
    const bool success) {
  ItemQueryResolution resolution;
  const auto pending_it = pending_item_queries_.find(request_id);
  if (pending_it == pending_item_queries_.end()) {
    return resolution;
  }

  const PendingItemQuery pending = pending_it->second;
  pending_item_queries_.erase(pending_it);

  if (pending.generation != loot_generation_ || !loot_window_) {
    return resolution;
  }

  if (success) {
    for (LootItem& item : loot_window_->items) {
      if (item.item_id != pending.item_id) {
        continue;
      }

      if (item.template_state == LootItemTemplateState::kPendingSlotChanged) {
        resolution.changed_ui_slots.push_back(
            static_cast<int>(item.display_index) + 1 +
            (loot_window_->gold_slot_reserved ? 1 : 0));
      }

      if (IsPendingTemplateState(item.template_state)) {
        item.template_state = LootItemTemplateState::kReady;
      }
    }
  }

  if (!HasPendingItemQueries()) {
    resolution.fire_loot_opened = true;
  }

  return resolution;
}

bool LootInteraction::HandleDynamicDropRollResult() {
  dynamic_drop_received_ = true;
  return true;
}

void LootInteraction::CloseLootWindow() {
  ResetItemQueries();
  ++loot_generation_;
  wire_slot_types_.fill(std::nullopt);
  loot_window_.reset();
}

void LootInteraction::SetPendingAutoLoot(bool enabled) {
  pending_auto_loot_ = enabled;
}

LootInteraction::AutoLootPlan LootInteraction::TakePendingAutoLootPlan() {
  AutoLootPlan plan;
  if (!pending_auto_loot_ || !loot_window_) {
    plan.remains_open = loot_window_.has_value();
    return plan;
  }

  pending_auto_loot_ = false;
  if (loot_window_->loot_type == LootType::kNone) {
    plan.remains_open = true;
    return plan;
  }

  const bool had_gold = loot_window_->gold > 0;
  const bool has_gold_slot = loot_window_->gold_slot_reserved;
  if (had_gold) {
    plan.loot_money = true;
    loot_window_->gold = 0;
  }

  const std::vector<LootItem> items = loot_window_->items;
  for (const LootItem& item : items) {
    if (item.slot_type != LootSlotType::kAllowLoot &&
        item.slot_type != LootSlotType::kOwner) {
      continue;
    }

    const ItemTemplate* tmpl = item_definitions_.GetItem(item.item_id);
    const bool requires_confirm =
        item.slot_type != LootSlotType::kOwner &&
        tmpl != nullptr &&
        tmpl->bonding == 1u &&
        static_cast<std::uint32_t>(tmpl->quality) >= 2u &&
        (tmpl->flags & 0x800u) == 0u;
    if (requires_confirm) {
      plan.bind_confirmations.push_back(BindConfirmation{
          .item_id = item.item_id,
          .count = item.count,
          .ui_slot =
              static_cast<int>(item.display_index) + (has_gold_slot ? 2 : 1),
      });
      continue;
    }

    plan.loot_slots.push_back(item.slot_index);
    RemoveLocalLootSlot(item.slot_index);
  }

  if (!HasLootItems()) {
    CloseLootWindow();
  }

  plan.remains_open = HasLootItems();
  return plan;
}

void LootInteraction::Clear() {
  state_.Reset();
  CloseLootWindow();
  pending_release_guid_ = ObjectGuid{};
  last_money_notify_.reset();
  last_roll_won_.reset();
  last_item_notify_.reset();
  last_loot_list_.reset();
  last_master_list_.reset();
  last_slot_changed_.reset();
  dynamic_drop_received_ = false;
  pending_auto_loot_ = false;
  cached_loot_type_ = LootType::kNone;
  pending_source_.reset();
}

void LootInteraction::RemoveLocalLootSlot(std::uint8_t slot) {
  if (!loot_window_) {
    return;
  }

  RemovePendingQueriesForSlot(slot);
  auto& items = loot_window_->items;
  items.erase(
      std::remove_if(items.begin(), items.end(),
                     [slot](const LootItem& item) {
                       return item.slot_index == slot;
                     }),
      items.end());
}

std::optional<std::size_t> LootInteraction::FindFirstFreeDisplayIndex() const {
  if (!loot_window_) {
    return std::nullopt;
  }

  std::array<bool, kMaxLootSlots> used_slots{};
  for (const LootItem& item : loot_window_->items) {
    if (item.display_index < kMaxLootSlots) {
      used_slots[item.display_index] = true;
    }
  }

  for (std::size_t slot = 0; slot < kMaxLootSlots; ++slot) {
    if (!used_slots[slot]) {
      return slot;
    }
  }

  return std::nullopt;
}

LootInteraction::ItemQueryRequest LootInteraction::RegisterItemQuery(
    const LootItem& item) {
  const std::uint64_t request_id = next_item_query_request_id_++;
  pending_item_queries_.emplace(
      request_id,
      PendingItemQuery{
          .generation = loot_generation_,
          .item_id = item.item_id,
          .slot_index = item.slot_index,
      });
  return ItemQueryRequest{.request_id = request_id, .item_id = item.item_id};
}

void LootInteraction::ResetItemQueries() {
  pending_item_queries_.clear();
}

void LootInteraction::RemovePendingQueriesForSlot(const std::uint8_t slot) {
  for (auto it = pending_item_queries_.begin();
       it != pending_item_queries_.end();) {
    if (it->second.slot_index == slot) {
      it = pending_item_queries_.erase(it);
      continue;
    }
    ++it;
  }
}

}
