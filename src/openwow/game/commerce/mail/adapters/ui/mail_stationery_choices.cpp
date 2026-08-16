#include "openwow/game/commerce/mail/adapters/ui/mail_stationery_choices.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/query_cache.h"

#include <algorithm>
#include <optional>
#include <string_view>

namespace openwow::game {
namespace {

struct StationeryItemData {
  std::string name;
  std::uint32_t display_id = 0;
  std::uint32_t buy_price = 0;
};

std::optional<StationeryItemData> ResolveStationeryItem(
    ItemDefinitions& item_definitions, QueryCache& queries, std::uint32_t item_id) {
  if (const auto* item = item_definitions.GetItem(item_id); item != nullptr) {
    return StationeryItemData{
        .name = item->name,
        .display_id = item->display_id,
        .buy_price = item->buy_price,
    };
  }

  if (const auto* item = queries.GetItemTemplate(item_id);
      item != nullptr) {
    return StationeryItemData{
        .name = item->name,
        .display_id = item->display_id,
        .buy_price = item->buy_price,
    };
  }

  return std::nullopt;
}

std::string ResolveStationeryIconPath(const data::dbc::DbcLoader* dbc,
                                      std::uint32_t display_id) {
  if (dbc != nullptr && display_id != 0) {
    if (const auto* display =
            dbc->item_display_info().LookupEntry(display_id);
        display != nullptr &&
        !std::string_view(display->inventory_icon).empty()) {
      return "Interface\\Icons\\" + std::string(display->inventory_icon);
    }
  }

  return "Interface\\Icons\\INV_Misc_QuestionMark";
}

bool CompareListings(const MailStationeryListing& lhs,
                     const MailStationeryListing& rhs) {
  if (lhs.is_default != rhs.is_default) {
    return lhs.is_default && !rhs.is_default;
  }
  if (lhs.is_owned != rhs.is_owned) {
    return lhs.is_owned && !rhs.is_owned;
  }
  if (lhs.buy_price != rhs.buy_price) {
    return lhs.buy_price > rhs.buy_price;
  }
  return core::SStrCmpNoCaseCollate(
             lhs.name.c_str(), rhs.name.c_str(), 0x7FFFFFFF) < 0;
}

bool PlayerOwnsStationeryItem(const PlayerInventoryReplica& inventory,
                              std::uint32_t item_id) {
  if (item_id == 0) {
    return false;
  }

  for (std::uint8_t slot = 0;
       slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (const auto* item = inventory.GetBackpackSlot(slot);
        item != nullptr && item->entry == item_id) {
      return true;
    }
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto slot_count = inventory.GetContainerNumSlots(bag);
    for (std::size_t slot = 0; slot < slot_count; ++slot) {
      if (const auto* item =
              inventory.GetContainerSlot(bag, static_cast<std::uint8_t>(slot));
          item != nullptr && item->entry == item_id) {
        return true;
      }
    }
  }

  return false;
}

}

MailStationeryChoices::~MailStationeryChoices() {
  Reset();
}

void MailStationeryChoices::Prime(
    const data::dbc::DbcLoader* dbc, QueryCache& queries,
    ItemDefinitions& item_definitions, PlayerInventoryReplica& inventory) {
  static_cast<void>(Refresh(dbc, queries, item_definitions, inventory));
}

std::vector<MailStationeryListing> MailStationeryChoices::Refresh(
    const data::dbc::DbcLoader* dbc, QueryCache& queries,
    ItemDefinitions& item_definitions, PlayerInventoryReplica& inventory) {
  if (dbc == nullptr) {
    Reset();
    return {};
  }

  std::lock_guard lock(mutex_);
  const auto signature = ComputeDefinitionSignature(*dbc);
  if (dbc_signature_ != signature || dbc_ != dbc || queries_ != &queries ||
      item_definitions_ != &item_definitions || inventory_ != &inventory) {
    ConfigureLocked(dbc, queries, item_definitions, inventory, signature);
  }

  QueueMissingTemplatesLocked();
  if (pending_item_ids_.empty()) {
    visible_listings_ = BuildVisibleListingsLocked();
  }
  return visible_listings_;
}

void MailStationeryChoices::Reset() {
  std::lock_guard lock(mutex_);
  if (queries_ != nullptr) {
    for (const auto item_id : pending_item_ids_) {
      queries_->CancelItemTemplateCallback(
          item_id,
          AsyncQueryChannel::CallbackKey(
              reinterpret_cast<std::uintptr_t>(this), item_id));
    }
  }
  ResetLocked();
}

std::uint64_t MailStationeryChoices::ComputeDefinitionSignature(
    const data::dbc::DbcLoader& dbc) {
  std::uint64_t hash = 1469598103934665603ull;
  const auto append = [&hash](std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
  };

  for (const auto& entry : dbc.stationery().entries()) {
    append(entry.id);
    append(entry.item_id);
    append(entry.flags);
    append(entry.texture.size());
    for (const char ch : entry.texture) {
      append(static_cast<unsigned char>(ch));
    }
  }
  return hash;
}

void MailStationeryChoices::ResetLocked() {
  dbc_ = nullptr;
  queries_ = nullptr;
  item_definitions_ = nullptr;
  inventory_ = nullptr;
  dbc_signature_ = 0;
  definitions_.clear();
  pending_item_ids_.clear();
  visible_listings_.clear();
  ++generation_;
}

void MailStationeryChoices::ConfigureLocked(
    const data::dbc::DbcLoader* dbc, QueryCache& queries,
    ItemDefinitions& item_definitions, PlayerInventoryReplica& inventory,
    std::uint64_t signature) {
  dbc_ = dbc;
  queries_ = &queries;
  item_definitions_ = &item_definitions;
  inventory_ = &inventory;
  dbc_signature_ = signature;
  definitions_.clear();
  definitions_.reserve(dbc->stationery().size());
  for (const auto& entry : dbc->stationery().entries()) {
    definitions_.push_back(Definition{
        .stationery_id = entry.id,
        .item_id = entry.item_id,
        .texture = std::string(entry.texture),
        .is_default = entry.flags != 0,
    });
  }
  pending_item_ids_.clear();
  visible_listings_.clear();
  ++generation_;
}

void MailStationeryChoices::QueueMissingTemplatesLocked() {
  if (queries_ == nullptr || item_definitions_ == nullptr) {
    return;
  }

  bool started_pending_refresh = false;
  for (const auto& definition : definitions_) {
    if (definition.item_id == 0 ||
        ResolveStationeryItem(
            *item_definitions_, *queries_, definition.item_id).has_value()) {
      continue;
    }
    if (std::find(pending_item_ids_.begin(), pending_item_ids_.end(),
                  definition.item_id) != pending_item_ids_.end()) {
      continue;
    }

    if (!started_pending_refresh && pending_item_ids_.empty()) {
      visible_listings_.clear();
      started_pending_refresh = true;
    }
    pending_item_ids_.push_back(definition.item_id);
    static_cast<void>(queries_->GetOrRequestItemTemplate(
        definition.item_id,
        QueryCache::QueryRequestOptions{
            .callback_key = AsyncQueryChannel::CallbackKey(
                reinterpret_cast<std::uintptr_t>(this), definition.item_id),
            .dedupe_callbacks = false,
            .callback =
                [this, generation = generation_,
                 item_id = definition.item_id](bool) {
                  OnItemTemplateResolved(generation, item_id);
                },
        }));
  }
}

std::vector<MailStationeryListing>
MailStationeryChoices::BuildVisibleListingsLocked() const {
  if (item_definitions_ == nullptr || inventory_ == nullptr) {
    return {};
  }

  std::vector<MailStationeryListing> listings;
  listings.reserve(definitions_.size());
  for (const auto& definition : definitions_) {
    const auto item =
        ResolveStationeryItem(*item_definitions_, *queries_, definition.item_id);
    if (!item.has_value()) {
      continue;
    }

    const bool is_owned =
        PlayerOwnsStationeryItem(*inventory_, definition.item_id);
    if (!definition.is_default && !is_owned) {
      continue;
    }

    listings.push_back(MailStationeryListing{
        .stationery_id = definition.stationery_id,
        .item_id = definition.item_id,
        .name = item->name,
        .icon_path = ResolveStationeryIconPath(dbc_, item->display_id),
        .selected_texture = definition.texture,
        .buy_price = item->buy_price,
        .is_default = definition.is_default,
        .is_owned = is_owned,
    });
  }

  std::sort(listings.begin(), listings.end(), CompareListings);
  return listings;
}

void MailStationeryChoices::OnItemTemplateResolved(
    std::uint64_t generation, std::uint32_t item_id) {
  std::lock_guard lock(mutex_);
  if (generation != generation_) {
    return;
  }

  const auto it =
      std::find(pending_item_ids_.begin(), pending_item_ids_.end(), item_id);
  if (it != pending_item_ids_.end()) {
    pending_item_ids_.erase(it);
  }
  if (pending_item_ids_.empty() && dbc_ != nullptr) {
    visible_listings_ = BuildVisibleListingsLocked();
  }
}

}
