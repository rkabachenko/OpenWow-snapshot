#include "openwow/game/inventory/operations/inventory_commands.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/character_map_runtime.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_trade_eligibility.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/inventory/operations/player_item_packet_location.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/session_handler.h"

#include <utility>

namespace openwow::game {
namespace {

[[nodiscard]] QueryCache::CallbackKey CallbackKey(
    const InventoryCommands* commands) {
  return {reinterpret_cast<std::uintptr_t>(commands), 0};
}

[[nodiscard]] bool MeetsRequirements(
    const ObjectManager& objects, const ItemTemplate& item_template,
    const ItemUseRequirementSources& sources,
    const SessionHandler& session) {
  const auto* player = objects.GetActivePlayer();
  const auto requirements = BuildItemUseRequirementView(item_template);
  return player != nullptr &&
         PlayerMeetsItemUseRequirements(
             *player, requirements, sources,
             session.GetProficiencyMask(
                 static_cast<std::uint8_t>(requirements.item_class)));
}

[[nodiscard]] bool IsEffectivelySoulboundForAutoEquip(
    const ObjectManager& objects, const ItemInstance& item) {
  return ItemIsEffectivelySoulbound(
      item, [&objects](const std::uint32_t enchantment_id) {
        return objects.dbc_loader().spell_item_enchantment().LookupEntry(
            enchantment_id);
      });
}

[[nodiscard]] bool IsProjectileItemClass(const ObjectManager& objects,
                                         const std::uint32_t item_entry) {
  const auto* const row = objects.dbc_loader().item().LookupEntry(item_entry);
  return row != nullptr &&
         row->class_id == static_cast<std::uint32_t>(ItemClass::Projectile);
}

[[nodiscard]] bool NeedsBindConfirmation(
    const ObjectManager& objects, const ItemTemplate* item_template,
    const ItemInstance& item, const bool skip_bind_check,
    const ItemUseRequirementSources& requirements,
    const SessionHandler& session) {
  if (skip_bind_check || item_template == nullptr ||
      item_template->bonding != 2 ||
      IsEffectivelySoulboundForAutoEquip(objects, item)) {
    return false;
  }
  return MeetsRequirements(objects, *item_template, requirements, session);
}

}

InventoryCommands::~InventoryCommands() {
  async_lifetime_->generation.fetch_add(1, std::memory_order_acq_rel);
}

void InventoryCommands::SetProtectionGate(ProtectionGate gate) {
  protection_gate_ = std::move(gate);
}

void InventoryCommands::BindHeldCursor(
    actions::held_cursor::HeldCursor* cursor) noexcept {
  held_cursor_ = cursor;
}

bool InventoryCommands::CanMutate() const {
  return protection_gate_ && protection_gate_();
}

std::uint32_t InventoryCommands::StorePending(PendingEquip pending) {
  for (std::uint32_t index = 0; index < pending_.size(); ++index) {
    if (pending_[index].item_guid == 0) {
      pending_[index] = pending;
      return index;
    }
  }
  pending_.push_back(pending);
  return static_cast<std::uint32_t>(pending_.size() - 1);
}

void InventoryCommands::SendAutoEquip(const PendingEquip& pending) {
  const auto& objects = map_runtime_.objects();
  const auto player_guid = objects.GetActivePlayerGuid().GetRawValue();
  const auto location = ResolvePlayerItemPacketLocation(
      inventory_, player_guid, pending.source_container_guid,
      pending.source_slot);
  if (!location.has_value() || location->item.guid != pending.item_guid ||
      location->item.entry != pending.item_entry) {
    return;
  }

  if (IsProjectileItemClass(objects, pending.item_entry)) {
    interaction_.SendSetAmmo(pending.item_entry);
    if (auto* cursor = held_cursor_; cursor != nullptr) {
      cursor->Clear();
    }
    return;
  }

  changes_.mouseover_items.push_back(pending.item_guid);
  interaction_.SendAutoEquipItem(location->packet_bag, location->packet_slot);
  if (auto* cursor = held_cursor_; cursor != nullptr) {
    cursor->Clear({.release_source_lease = false,
                   .publish_money_owner_update = true});
  }
}

void InventoryCommands::PlaceHeldItem(const bool skip_bind_check) {
  if (!CanMutate()) {
    return;
  }

  auto* cursor = held_cursor_;
  if (cursor == nullptr) {
    return;
  }
  if (const auto* ammo = cursor->get_if<actions::held_cursor::AmmoItem>();
      ammo != nullptr) {
    if (ammo->item_entry != 0) {
      interaction_.SendSetAmmo(ammo->item_entry);
      cursor->Clear();
    }
    return;
  }

  const auto* held = cursor->live_item();
  if (held == nullptr || held->item.guid == 0) {
    return;
  }

  if (held->source_container_guid == 0) {
    cursor->Clear();
    return;
  }

  const PendingEquip request{
      .item_guid = held->item.guid,
      .source_container_guid = held->source_container_guid,
      .source_slot = held->source_slot,
      .item_entry = held->item.entry,
      .inventory_revision = inventory_.revision(),
  };
  const auto player_guid =
      map_runtime_.objects().GetActivePlayerGuid().GetRawValue();
  if (request.source_container_guid == player_guid &&
      request.source_slot < InventorySlots::kBackpackStart) {
    cursor->Clear();
    return;
  }

  const auto location = ResolvePlayerItemPacketLocation(
      inventory_, player_guid, request.source_container_guid,
      request.source_slot);
  if (!location.has_value() || location->item.guid != request.item_guid) {
    return;
  }

  const auto send_known_item = [&](const ItemTemplate* item_template) {
    if (!NeedsBindConfirmation(map_runtime_.objects(), item_template,
                               location->item, skip_bind_check, requirements_,
                               session_)) {
      SendAutoEquip(request);
      return true;
    }
    return false;
  };

  if (const auto* item_template = item_definitions_.GetItem(request.item_entry);
      item_template != nullptr) {
    if (!send_known_item(item_template)) {
      const auto index = StorePending(request);
      changes_.bind_confirmations.push_back(index);
    }
    return;
  }

  QueryCache::QueryRequestOptions options;
  options.callback_key = CallbackKey(this);
  options.dedupe_callbacks = false;
  const auto lifetime = std::weak_ptr<AsyncLifetime>(async_lifetime_);
  const auto generation =
      async_lifetime_->generation.load(std::memory_order_acquire);
  options.callback = [this, lifetime, generation, request](const bool success) {
    const auto state = lifetime.lock();
    if (!success || state == nullptr ||
        state->generation.load(std::memory_order_acquire) != generation ||
        inventory_.revision() != request.inventory_revision) {
      return;
    }
    const auto* cursor = held_cursor_;
    const auto* held = cursor != nullptr ? cursor->live_item() : nullptr;
    if (held == nullptr || held->item.guid != request.item_guid ||
        held->source_container_guid != request.source_container_guid ||
        held->source_slot != request.source_slot) {
      return;
    }
    PlaceHeldItem(false);
  };
  (void)query_cache_.GetOrRequestItemTemplate(
      request.item_entry, std::move(options));
}

void InventoryCommands::SendSwapToSlot(const PendingEquip& pending) {
  const auto& objects = map_runtime_.objects();
  const auto player_guid = objects.GetActivePlayerGuid().GetRawValue();
  const auto location = ResolvePlayerItemPacketLocation(
      inventory_, player_guid, pending.source_container_guid,
      pending.source_slot);
  if (!location.has_value() || location->item.guid != pending.item_guid ||
      location->item.entry != pending.item_entry) {
    return;
  }

  const auto dest_slot = static_cast<std::uint8_t>(pending.dest_slot);
  if (location->packet_bag == InventorySlots::kMainBag) {
    interaction_.SendSwapInvItem(dest_slot, location->packet_slot);
  } else {
    interaction_.SendSwapItem(InventorySlots::kMainBag, dest_slot,
                              location->packet_bag, location->packet_slot);
  }
  if (auto* cursor = held_cursor_; cursor != nullptr) {
    cursor->Clear({.release_source_lease = false,
                   .publish_money_owner_update = true});
  }
}

void InventoryCommands::SendEquipItemToSlot(const PendingEquip& pending) {
  const auto& objects = map_runtime_.objects();
  const auto player_guid = objects.GetActivePlayerGuid().GetRawValue();
  const auto location = ResolvePlayerItemPacketLocation(
      inventory_, player_guid, pending.source_container_guid,
      pending.source_slot);
  if (!location.has_value() || location->item.guid != pending.item_guid ||
      location->item.entry != pending.item_entry) {
    return;
  }

  changes_.mouseover_items.push_back(pending.item_guid);
  interaction_.SendEquipItem(pending.item_guid,
                             static_cast<std::uint8_t>(pending.dest_slot));
}

bool InventoryCommands::DispatchOrDefer(const ItemPlacementSource& source,
                                        const PendingKind kind,
                                        const std::uint32_t dest_slot) {
  if (!CanMutate()) {
    return false;
  }

  const auto player_guid =
      map_runtime_.objects().GetActivePlayerGuid().GetRawValue();
  const auto location = ResolvePlayerItemPacketLocation(
      inventory_, player_guid, source.source_container_guid,
      source.source_slot);
  if (!location.has_value() || location->item.guid != source.item_guid ||
      location->item.entry != source.item_entry) {
    return false;
  }

  const PendingEquip request{
      .kind = kind,
      .item_guid = source.item_guid,
      .source_container_guid = source.source_container_guid,
      .source_slot = source.source_slot,
      .item_entry = source.item_entry,
      .inventory_revision = inventory_.revision(),
      .dest_slot = dest_slot,
  };

  const auto* item_template = item_definitions_.GetItem(source.item_entry);
  if (NeedsBindConfirmation(map_runtime_.objects(), item_template,
                            location->item, false,
                            requirements_, session_)) {
    const auto index = StorePending(request);
    if (kind == PendingKind::kAutoEquip) {
      changes_.bind_confirmations.push_back(index);
    } else {
      changes_.swap_bind_confirmations.push_back(index);
    }
    return true;
  }

  switch (kind) {
    case PendingKind::kAutoEquip:
      SendAutoEquip(request);
      break;
    case PendingKind::kSwapToSlot:
      SendSwapToSlot(request);
      break;
    case PendingKind::kEquipItemSlot:
      SendEquipItemToSlot(request);
      break;
  }
  return false;
}

bool InventoryCommands::PlaceHeldItemInSlot(const std::uint32_t dest_slot) {
  if (!CanMutate()) {
    return true;
  }

  auto* cursor = held_cursor_;
  if (cursor == nullptr) {
    return true;
  }
  const auto* held = cursor->live_item();
  if (held == nullptr || held->item.guid == 0 ||
      held->source_container_guid == 0) {
    return true;
  }

  const ItemPlacementSource source{
      .item_guid = held->item.guid,
      .source_container_guid = held->source_container_guid,
      .source_slot = held->source_slot,
      .item_entry = held->item.entry,
  };

  const bool deferred =
      DispatchOrDefer(source, PendingKind::kSwapToSlot, dest_slot);
  return !deferred;
}

void InventoryCommands::RequestAutoEquip(const ItemPlacementSource source) {
  (void)DispatchOrDefer(source, PendingKind::kAutoEquip, 0);
}

void InventoryCommands::RequestEquipInSlot(const ItemPlacementSource source,
                                           const std::uint32_t dest_slot) {
  (void)DispatchOrDefer(source, PendingKind::kEquipItemSlot, dest_slot);
}

void InventoryCommands::ResolvePending(const std::uint32_t index,
                                       const bool accepted) {
  if (index >= pending_.size()) {
    return;
  }
  const auto pending = pending_[index];
  pending_[index] = {};
  if (!accepted || pending.item_guid == 0 || !CanMutate()) {
    return;
  }
  switch (pending.kind) {
    case PendingKind::kAutoEquip:
      SendAutoEquip(pending);
      break;
    case PendingKind::kSwapToSlot:
      SendSwapToSlot(pending);
      break;
    case PendingKind::kEquipItemSlot:
      SendEquipItemToSlot(pending);
      break;
  }
}

void InventoryCommands::Reset() {
  query_cache_.CancelItemTemplateCallbacks(CallbackKey(this));
  async_lifetime_->generation.fetch_add(1, std::memory_order_acq_rel);
  pending_.clear();
  changes_ = {};
}

}
