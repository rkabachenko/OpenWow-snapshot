
#include "openwow/game/commerce/mail/adapters/lua/mail_compose_attachment_lua.h"
#include "openwow/game/commerce/mail/adapters/lua/mail_lua_adapter.h"
#include "openwow/game/commerce/mail/adapters/ui/mail_stationery_choices.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/inventory/items/item_trade_eligibility.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/inventory/operations/player_item_packet_location.h"
#include "openwow/game/query_cache.h"
#include "openwow/core/storm_string.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <mutex>
#include <string_view>
#include "openwow/game/commerce/mail/adapters/lua/mail_attachment_presentation.h"

namespace openwow::ui::game::detail {

namespace {

bool IsEquippedMailSource(const std::uint8_t source_bag,
                          const std::uint8_t source_slot) {
  return source_bag == ::openwow::game::InventorySlots::kMainBag &&
         source_slot < ::openwow::game::InventorySlots::kBackpackStart;
}

bool ItemHasRefundBlockingMailModification(
    const ::openwow::game::ItemInstance& item) {
  return ::openwow::game::ItemHasRefundBlockingEnchantmentState(item);
}

}

int LuaSendMail(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: SendMail(target, subject, body)");
  }
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter) {
    return 0;
  }
  auto& mail = RequireMailLuaAdapter(L).mail();
  if (mail.HasPendingMailboxOperation()) {
    return 0;
  }

  const auto mailbox_guid = mail.mailbox_guid();
  if (mailbox_guid == 0) {
    return 0;
  }

  const char *recipient = lua_tostring(L, 1);
  if (recipient == nullptr || *recipient == '\0') {
    return 0;
  }
  if (!lua_isstring(L, 2)) {
    return 0;
  }

  const char *subject = lua_tostring(L, 2);
  if (subject == nullptr || *subject == '\0') {
    return 0;
  }

  const char *body = lua_isstring(L, 3) ? lua_tostring(L, 3) : "";
  auto &ms = RequireMailCompose(L);
  auto draft = ms.GetDraft();
  if (draft.stationery == 0) {
    return 0;
  }

  std::vector<::openwow::game::MailSendAttachment> packet_attachments;
  packet_attachments.reserve(kSendMailLinkSlotCount);
  for (std::uint32_t slot = 0; slot < kSendMailLinkSlotCount; ++slot) {
    const auto *attachment = GetDraftAttachmentBySlot(draft, slot);
    if (attachment == nullptr || attachment->item_guid == 0) {
      continue;
    }

    const auto resolved_attachment =
        ResolveGuidBackedDraftAttachmentState(L, draft, slot);
    if (!resolved_attachment.has_value() || adapter->objects().GetLocalPlayer() == nullptr) {
      RequireMailLuaAdapter(L).ShowSystemMessage(23);
      ClearInteractiveDraftAttachmentSlot(draft, slot);
      ms.SetDraft(draft);
      FireMailSendInfoUpdate(L);
      return 0;
    }

    packet_attachments.push_back({
        .slot = static_cast<std::uint8_t>(slot),
        .item_guid = resolved_attachment->attachment.item_guid,
    });
  }

  if (draft.money != 0 && draft.cod != 0) {
    return 0;
  }
  if (draft.money == 0 && draft.cod != 0 && packet_attachments.empty()) {
    return 0;
  }
  if (!mail.TryStartMailboxAction()) {
    return 0;
  }

  const auto package_id = packet_attachments.empty() ? 0u : draft.package_id;
  adapter->interaction().SendSendMail(mailbox_guid, recipient, subject, body ? body : "",
                                      draft.money, draft.cod, draft.stationery,
                                      packet_attachments, package_id);
  return 0;
}

int LuaSetSendMailMoney(lua_State *L) {
  const auto amount = LuaCheckMailCopperAmount(L, "Usage: SetSendMailMoney(amount)");
  if (!CanPlayerAffordSendMailMoney(L, amount)) {
    lua_pushnil(L);
    return 1;
  }

  auto &ms = RequireMailCompose(L);
  auto draft = ms.GetDraft();
  draft.money = amount;
  ms.SetDraft(draft);

  RequireMailLuaAdapter(L).Present(MailLuaEvent::kSendMoneyChanged);

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaSetSendMailCOD(lua_State *L) {

  const auto amount = LuaCheckMailCopperAmount(L, "Usage: SetSendMailCOD(amount)");

  auto &ms = RequireMailCompose(L);
  auto draft = ms.GetDraft();

  if (CountOccupiedSendMailAttachmentSlots(draft) != 0) {
    draft.cod = amount;
    ms.SetDraft(draft);

    RequireMailLuaAdapter(L).Present(MailLuaEvent::kSendCodChanged);
  }
  return 0;
}

int LuaGetSendMailPrice(lua_State *L) {
  auto *adapter = &RequireMailLuaAdapter(L);
  auto &ms = RequireMailCompose(L);
  auto draft = ms.GetDraft();
  int num_att = static_cast<int>(CountOccupiedSendMailAttachmentSlots(draft));
  int cost = (num_att > 0) ? 30 * num_att : 30;
  const auto listings = GetStationeryListings(L, adapter);
  if (const auto *stationery = FindSelectedStationery(listings, draft.stationery);
      stationery != nullptr && !stationery->is_owned) {
    cost += static_cast<int>(stationery->buy_price);
  }
  if (const auto package = ms.GetPackageById(draft.package_id); package.has_value()) {
    cost += static_cast<int>(package->price);
  }
  lua_pushnumber(L, static_cast<lua_Number>(cost));
  return 1;
}

static void MaybeLockSendMailAttachment(lua_State *L, MailLuaAdapter *adapter,
                                        std::uint32_t slot,
                                        const ::openwow::game::ItemInstance &item) {
  const auto* refund_info = RequireMailLuaAdapter(L).item_interactions().refund_quote(
      ::openwow::game::ObjectGuid(item.guid));
  if (refund_info == nullptr || refund_info->time_left == 0 ||
      ItemHasRefundBlockingMailModification(item)) {
    return;
  }

  RequireMailCompose(L).SetComposeLocked(true);
  FireMailLockSendItems(L, slot,
                        BuildMailAttachmentLink(L, adapter,
                                                BuildSendMailAttachmentLinkState(item)));
}

static bool TryAttachSendMailItemToDraft(lua_State *L, MailLuaAdapter *adapter,
                                         const ::openwow::game::ItemInstance &item,
                                         std::uint8_t source_bag, std::uint8_t source_slot,
                                         int requested_slot,
                                         std::uint32_t *attached_slot_out = nullptr) {
  if (HasPendingSendMailComposeLock(L)) {
    RequireMailLuaAdapter(L).ShowSystemMessage(kMailComposeItemLockedMessage);
    return false;
  }

  const auto tmpl = ResolveSendMailAttachmentTemplate(L, adapter, item.entry);
  if (!tmpl.has_value()) {
    RequireMailLuaAdapter(L).ShowSystemMessage(kMailTemplatePendingMessage);
    return false;
  }

  if (item.IsSoulbound()) {
    if (tmpl->bonding == 4) {
      RequireMailLuaAdapter(L).ShowSystemMessage(kMailQuestItemMessage);
      return false;
    }
    if ((tmpl->flags & kAccountBoundMailTemplateFlag) == 0) {
      RequireMailLuaAdapter(L).ShowSystemMessage(kMailBoundItemMessage);
      return false;
    }
  }

  if (item.IsConjured() ||
      tmpl->area != 0 || tmpl->map != 0) {
    RequireMailLuaAdapter(L).ShowSystemMessage(kMailRestrictedItemMessage);
    return false;
  }

  if (item.duration != 0 || tmpl->duration != 0 || tmpl->holiday_id != 0) {
    RequireMailLuaAdapter(L).ShowSystemMessage(kMailTemporaryItemMessage);
    return false;
  }

  if (item.guid != 0) {
    if (const auto location =
            ::openwow::game::ResolvePlayerItemPacketLocationByGuid(
                RequireMailLuaAdapter(L).inventory(), item.guid);
        location.has_value()) {
      source_bag = location->packet_bag;
      source_slot = location->packet_slot;
    }
  }

  if (IsEquippedMailSource(source_bag, source_slot)) {
    RequireMailLuaAdapter(L).ShowSystemMessage(kMailEquippedItemMessage);
    return false;
  }

  auto &compose = RequireMailCompose(L);
  auto draft = compose.GetDraft();
  std::uint32_t target_slot = 0;
  if (requested_slot < 0) {
    const auto first_empty_slot = FindFirstEmptySendMailAttachmentSlot(draft);
    if (!first_empty_slot.has_value()) {
      RequireMailLuaAdapter(L).ShowSystemMessage(kMailComposeFullMessage);
      return false;
    }
    target_slot = *first_empty_slot;
  } else {
    if (requested_slot >= static_cast<int>(kSendMailInteractiveSlotCount)) {
      RequireMailLuaAdapter(L).ShowSystemMessage(kMailComposeFullMessage);
      return false;
    }
    target_slot = static_cast<std::uint32_t>(requested_slot);
  }

  SetDraftAttachmentSlot(draft, target_slot,
                         BuildDraftAttachmentFromItemInstance(item, target_slot));
  compose.SetDraft(draft);
  RequireMailLuaAdapter(L).EnterItemMouseover(item.guid);
  MaybeLockSendMailAttachment(L, adapter, target_slot, item);
  FireMailSendInfoUpdate(L);

  if (attached_slot_out != nullptr) {
    *attached_slot_out = target_slot;
  }
  return true;
}

int LuaClickSendMailItemButton(lua_State *L) {
  int slot_index = -1;
  if (LuaHasNumericArgument(L, 1)) {
    slot_index = static_cast<int>(lua_tonumber(L, 1)) - 1;
  }

  auto* adapter = &RequireMailLuaAdapter(L);
  if (adapter == nullptr || adapter->held_cursor() == nullptr) {
    return 0;
  }
  auto& held_cursor = *adapter->held_cursor();
  auto &compose = RequireMailCompose(L);
  const auto draft = compose.GetDraft();
  const auto* held_item = held_cursor.live_item();
  const bool has_cursor_item = held_item != nullptr;
  const bool remove_item = lua_gettop(L) >= 2 && lua_toboolean(L, 2) != 0;

  const ::openwow::game::MailAttachment *slot_attachment = nullptr;
  if (slot_index >= 0 && slot_index < static_cast<int>(kSendMailLinkSlotCount)) {
    slot_attachment = GetDraftAttachmentBySlot(draft, static_cast<std::uint32_t>(slot_index));
  }

  if (remove_item) {
    if (slot_index < 0 || slot_index >= static_cast<int>(kSendMailLinkSlotCount) ||
        slot_attachment == nullptr || slot_attachment->item_guid == 0) {
      return 0;
    }

    auto updated_draft = draft;
    ClearInteractiveDraftAttachmentSlot(updated_draft, static_cast<std::uint32_t>(slot_index));
    compose.SetDraft(updated_draft);
    RequireMailLuaAdapter(L).LeaveItemMouseover(slot_attachment->item_guid);
    FireMailSendInfoUpdate(L);
    return 0;
  }

  const auto attachment_guid = slot_attachment != nullptr ? slot_attachment->item_guid : 0;
  const auto held_guid = has_cursor_item ? held_item->item.guid : 0;
  if (attachment_guid == held_guid) {
    held_cursor.Clear();
    return 0;
  }

  if (!has_cursor_item) {
    if (slot_attachment == nullptr || slot_attachment->item_guid == 0) {
      held_cursor.Clear();
      return 0;
    }

    auto updated_draft = draft;
    if (slot_index >= 0) {
      ClearInteractiveDraftAttachmentSlot(updated_draft, static_cast<std::uint32_t>(slot_index));
    }
    compose.SetDraft(updated_draft);
    adapter->PickupAttachment(slot_attachment->item_guid, slot_index);
    FireMailSendInfoUpdate(L);
    return 0;
  }

  if (!TryAttachSendMailItemToDraft(
          L, adapter, held_item->item, held_item->source_bag,
          held_item->source_slot, slot_index)) {
    return 0;
  }

  if (attachment_guid != 0) {
    adapter->PickupAttachment(attachment_guid, slot_index);
  } else {
    held_cursor.Clear({
        .release_source_lease = false,
        .publish_money_owner_update = true,
    });
  }
  return 0;
}

bool TryAttachSendMailContainerItem(lua_State *L,
                                    const ::openwow::game::ItemInstance &item,
                                    std::uint8_t source_bag, std::uint8_t source_slot) {

  auto* const adapter = TryGetMailLuaAdapter(L);
  if (adapter == nullptr || adapter->mail().mailbox_guid() == 0 ||
      item.IsEmpty()) {
    return false;
  }

  (void)TryAttachSendMailItemToDraft(L, adapter, item, source_bag, source_slot, -1);
  return true;
}

int LuaGetSendMailItem(lua_State *L) {
  const auto slot = LuaOptionalSaturatedSlotAfterSubtract(L, 1, 0);
  if (slot >= kSendMailLinkSlotCount) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 4;
  }

  const auto draft = RequireMailCompose(L).GetDraft();
  const auto attachment = ResolveGuidBackedDraftAttachmentState(L, draft, slot);
  if (!attachment.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 4;
  }

  auto *adapter = &RequireMailLuaAdapter(L);
  const auto info = ResolveMailAttachmentTemplateInfo(
      L, adapter, attachment->item.entry,
      ::openwow::game::QueryCache::QueryRequestOptions{
          .dedupe_callbacks = false,
          .callback = BuildSendInfoRefreshCallback(&RequireMailLuaAdapter(L).mail()),
      });
  if (!info.resolved) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 4;
  }

  const auto link_state = ResolveSendMailAttachmentState(*attachment);
  if (!link_state.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 4;
  }

  lua_pushstring(L, ResolveMailAttachmentDisplayName(L, adapter, *link_state).c_str());
  lua_pushstring(L, ResolveMailAttachmentTexturePath(L, info.display_id).c_str());
  lua_pushnumber(L, static_cast<lua_Number>(std::max(attachment->item.count, 1u)));
  lua_pushnumber(L, static_cast<lua_Number>(info.quality));
  return 4;
}

int LuaClearSendMail(lua_State *L) {

  ResetMailComposeUiState(RequireMailLuaAdapter(L), true);
  return 0;
}

int LuaGetNumPackages(lua_State *L) {
  lua_pushnumber(L, static_cast<lua_Number>(RequireMailCompose(L).GetPackageCount()));
  return 1;
}

int LuaGetPackageInfo(lua_State *L) {
  const auto one_based_index =
      LuaCheckSaturatedU32(L, 1, "Usage: GetPackageInfo(index)");
  if (const auto package =
          RequireMailCompose(L).GetPackageByIndex(one_based_index);
      package.has_value()) {
    lua_pushstring(L, package->name.c_str());
    lua_pushstring(L, package->icon_path.c_str());
    lua_pushnumber(L, static_cast<lua_Number>(package->price));
    return 3;
  }

  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  return 3;
}

int LuaSelectPackage(lua_State *L) {
  const auto index =
      LuaCheckSaturatedU32(L, 1, "Usage: SelectPackage(index)");
  RequireMailCompose(L).SelectPackageByIndex(index);
  return 0;
}

int LuaGetSendMailItemLink(lua_State *L) {
  const auto slot = LuaOptionalSaturatedSlotAfterSubtract(L, 1, 0);
  if (slot >= kSendMailLinkSlotCount) {
    return 0;
  }

  const auto draft = RequireMailCompose(L).GetDraft();
  const auto attachment = ResolveGuidBackedDraftAttachmentState(L, draft, slot);
  if (!attachment.has_value()) {
    return 0;
  }

  const auto link_state = ResolveSendMailAttachmentState(*attachment);
  if (!link_state.has_value()) {
    return 0;
  }

  const std::string link = BuildMailAttachmentLink(L, &RequireMailLuaAdapter(L), *link_state);
  lua_pushlstring(L, link.c_str(), link.size());
  return 1;
}

int LuaGetSendMailMoney(lua_State *L) {
  auto &ms = RequireMailCompose(L);
  auto draft = ms.GetDraft();
  lua_pushnumber(L, static_cast<lua_Integer>(draft.money));
  return 1;
}

int LuaGetSendMailCOD(lua_State *L) {
  auto &ms = RequireMailCompose(L);
  auto draft = ms.GetDraft();
  lua_pushnumber(L, static_cast<lua_Integer>(draft.cod));
  return 1;
}

int LuaGetNumStationeries(lua_State *L) {
  auto *adapter = &RequireMailLuaAdapter(L);
  lua_pushnumber(
      L, static_cast<lua_Number>(GetStationeryListings(L, adapter).size()));
  return 1;
}

int LuaGetSelectedStationeryTexture(lua_State *L) {
  const auto *dbc = RequireMailLuaAdapter(L).dbc();
  if (!dbc) {
    lua_pushnil(L);
    return 1;
  }

  if (const auto *entry =
          dbc->stationery().LookupEntry(GetSelectedStationeryId(L));
      entry != nullptr) {
    lua_pushlstring(L, entry->texture.data(), entry->texture.size());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaSelectStationery(lua_State *L) {
  auto *adapter = &RequireMailLuaAdapter(L);
  const auto index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: SelectStationery(index)");
  const auto listings = GetStationeryListings(L, adapter);

  if (index < listings.size()) {
    SetSelectedStationeryId(L, listings[index].stationery_id);
  } else {
    SetSelectedStationeryId(L, 0);
  }
  return 0;
}

int LuaRespondMailLockSendItem(lua_State *L) {
  const auto slot = static_cast<std::uint32_t>(LuaCheckSignedI32(
                        L, 1, "Usage: RespondMailLockSendItem(slot, keepItem)")) -
                    1u;
  const bool keep_item = lua_toboolean(L, 2) != 0;

  if (!HasPendingSendMailComposeLock(L)) {
    return 0;
  }

  RequireMailCompose(L).SetComposeLocked(false);
  if (keep_item) {
    FireMailUnlockSendItems(L);
    return 0;
  }

  auto &compose = RequireMailCompose(L);
  const auto draft = compose.GetDraft();
  const ::openwow::game::MailAttachment *attachment = nullptr;
  if (slot < kSendMailLinkSlotCount) {
    attachment = GetDraftAttachmentBySlot(draft, slot);
  }

  auto updated_draft = draft;
  ClearInteractiveDraftAttachmentSlot(updated_draft, slot);
  compose.SetDraft(updated_draft);

  if (attachment != nullptr && attachment->item_guid != 0) {
    RequireMailLuaAdapter(L).LeaveItemMouseover(attachment->item_guid);
  }
  FireMailSendInfoUpdate(L);
  FireMailUnlockSendItems(L);
  return 0;
}

int LuaSetSendMailShowing(lua_State *L) {
  if (lua_type(L, 1) != LUA_TBOOLEAN) {
    return luaL_error(L, "Usage: SetSendMailShowing(bool)");
  }
  RequireMailCompose(L).SetSendMailShowing(
      lua_toboolean(L, 1) != 0);
  return 0;
}

int LuaGetStationeryInfo(lua_State *L) {
  auto *adapter = &RequireMailLuaAdapter(L);
  const auto index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: GetStationeryInfo(index)");
  const auto listings = GetStationeryListings(L, adapter);

  if (index < listings.size()) {
    const auto &listing = listings[index];
    lua_pushstring(L, listing.name.c_str());
    lua_pushstring(L, listing.icon_path.c_str());
    if (listing.is_owned) {
      lua_pushnil(L);
    } else {
      lua_pushnumber(L, static_cast<lua_Number>(listing.buy_price));
    }
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
  }
  return 3;
}

}
