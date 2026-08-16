
#include "openwow/game/commerce/mail/adapters/lua/mail_lua_adapter.h"
#include "openwow/game/commerce/mail/adapters/ui/mail_stationery_choices.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_interactions.h"
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

int LuaHasNewMail(lua_State *L) {
  auto *adapter = &RequireMailLuaAdapter(L);
  if (adapter && RequireMailLuaAdapter(L).mail().HasPendingMail()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetLatestThreeSenders(lua_State *L) {
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter) {
    return 0;
  }

  int pushed = 0;
  for (const auto &sender : RequireMailLuaAdapter(L).mail().next_mail_senders()) {
    if (sender.sender_guid == 0 && sender.sender_entry == 0) {
      continue;
    }
    if (sender.time_left > 0.0f) {
      continue;
    }

    PushLatestMailSenderName(L, adapter, sender);
    ++pushed;
  }

  return pushed;
}

int LuaGetInboxNumItems(lua_State *L) {

  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }
  const auto &mail = RequireMailLuaAdapter(L).mail();
  const auto &inbox = mail.inbox();
  lua_pushnumber(L, static_cast<lua_Number>(inbox.size()));
  lua_pushnumber(L, static_cast<lua_Number>(mail.real_count()));
  return 2;
}

int LuaGetInboxHeaderInfo(lua_State *L) {
  const auto raw_idx = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: GetInboxHeaderInfo(index)");
  auto *adapter = &RequireMailLuaAdapter(L);
  const auto& mail_interaction = RequireMailLuaAdapter(L).mail();
  const bool valid = adapter && raw_idx < mail_interaction.inbox().size();

  if (!valid) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 14;
  }

  const auto &mail = mail_interaction.inbox()[raw_idx];
  const auto attachment_count = CountInboxAttachments(mail);
  const auto effective_stationery_id = ResolveEffectiveInboxStationeryId(L, mail);

  if (const auto package_icon =
          ResolveInboxPackageIconPath(L, adapter, mail, attachment_count);
      !package_icon.empty()) {
    lua_pushstring(L, package_icon.c_str());
  } else {
    lua_pushnil(L);
  }

  if (const auto stationery_icon = ResolveInboxStationeryIconPath(L, adapter, mail);
      !stationery_icon.empty()) {
    lua_pushstring(L, stationery_icon.c_str());
  } else {
    lua_pushnil(L);
  }

  PushInboxSenderName(L, adapter, mail, effective_stationery_id);
  PushInboxSubject(L, adapter, mail);
  lua_pushnumber(L, static_cast<lua_Number>(mail.money));
  lua_pushnumber(L, static_cast<lua_Number>(mail.cod));
  lua_pushnumber(L, static_cast<lua_Number>(mail.expiration_time));

  if (attachment_count != 0) {
    lua_pushnumber(L, static_cast<lua_Number>(attachment_count));
  } else {
    lua_pushnil(L);
  }

  lua_pushwowbool(L, (mail.checked & ::openwow::game::kMailCheckedRead) != 0);
  lua_pushwowbool(L, (mail.checked & ::openwow::game::kMailCheckedReturned) != 0 ||
                         (mail.checked & ::openwow::game::kMailCheckedReturnedAlt) != 0);
  lua_pushwowbool(L, (mail.checked & ::openwow::game::kMailCheckedCopied) != 0);
  lua_pushwowbool(L, mail.message_type == ::openwow::game::MailType::kNormal &&
                         effective_stationery_id != 61);
  lua_pushwowbool(L, effective_stationery_id == 61);

  if (attachment_count == 1) {
    if (const auto *attachment = GetFirstInboxAttachment(mail); attachment != nullptr) {
      lua_pushnumber(L, static_cast<lua_Number>(attachment->stack_count));
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushnil(L);
  }

  return 14;
}

int LuaGetInboxText(lua_State *L) {
  const auto raw_index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: GetInboxText(index)");
  auto *adapter = &RequireMailLuaAdapter(L);
  const bool valid =
      adapter != nullptr &&
      raw_index < RequireMailLuaAdapter(L).mail().inbox().size();

  if (!valid) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 4;
  }

  auto &mail_manager = RequireMailLuaAdapter(L).mail();
  const auto opened_mail = mail_manager.OpenInboxMail(static_cast<std::size_t>(raw_index));
  const auto &mail = mail_manager.inbox()[static_cast<std::size_t>(raw_index)];

  if (opened_mail.marked_read) {
    adapter->interaction().SendMailMarkAsRead(mail_manager.mailbox_guid(), mail.message_id);
    if (opened_mail.cleared_pending_mail) {
      adapter->Present(MailLuaEvent::kPendingMailChanged);
    }
    adapter->Present(MailLuaEvent::kInboxChanged);
  }

  if (const auto body = ResolveInboxBodyText(L, adapter, mail); body.has_value()) {
    lua_pushlstring(L, body->c_str(), body->size());
  } else {
    lua_pushnil(L);
  }

  if (const auto stationery_icon = ResolveInboxStationeryIconPath(L, adapter, mail.stationery);
      !stationery_icon.empty()) {
    lua_pushstring(L, stationery_icon.c_str());
  } else {
    lua_pushnil(L);
  }

  lua_pushwowbool(L, CanCreateInboxTextItem(mail));
  lua_pushwowbool(L, mail.mail_template_id != 0);
  return 4;
}

int LuaGetInboxItem(lua_State *L) {
  const auto mail_index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: GetInboxItem(messageIndex, attachIndex)");
  auto *adapter = &RequireMailLuaAdapter(L);
  if (adapter == nullptr) {
    PushInboxItemEmptyResult(L);
    return 5;
  }

  const auto &mail_manager = RequireMailLuaAdapter(L).mail();
  const auto *mail = GetInboxMailByZeroBasedIndex(mail_manager, mail_index);
  if (mail == nullptr) {
    PushInboxItemEmptyResult(L);
    return 5;
  }

  const auto *attachment = mail_manager.GetMailItem(*mail, GetOptionalInboxItemSlot(L));
  if (attachment == nullptr || attachment->item_entry == 0) {
    PushInboxItemEmptyResult(L);
    return 5;
  }

  const auto info = ResolveMailAttachmentTemplateInfo(
      L, adapter, attachment->item_entry,
      BuildInboxAsyncRefreshQueryOptions(L, attachment->item_entry));
  if (!info.resolved) {
    PushInboxItemPendingResult(L, *attachment);
    return 5;
  }

  const auto item_name =
      ResolveMailAttachmentDisplayName(L, info, attachment->random_property_id);
  const auto texture = ResolveMailAttachmentTexturePath(L, info.display_id);

  lua_pushlstring(L, item_name.c_str(), item_name.size());
  lua_pushstring(L, texture.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(attachment->stack_count));
  const auto displayed_quality =
      info.inventory_type != 0 ? static_cast<std::int32_t>(info.quality) : -1;
  lua_pushnumber(L, static_cast<lua_Number>(displayed_quality));
  lua_pushwowbool(
      L, LocalPlayerCanUseInboxItem(L, adapter, attachment->item_entry));
  return 5;
}

int LuaGetInboxItemLink(lua_State *L) {
  constexpr auto kUsage =
      "Usage: GetInboxItemLink(messageIndex, attachIndex)";
  const auto mail_index = LuaCheckSaturatedOneBasedIndex(L, 1, kUsage);
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter) {
    return 0;
  }

  const auto &mail_manager = RequireMailLuaAdapter(L).mail();
  const auto *mail = mail_manager.GetInboxMail(mail_index);
  if (!mail) {
    return 0;
  }

  const auto *attachment = mail_manager.GetMailItem(*mail, GetOptionalInboxItemSlot(L));
  if (!attachment || attachment->item_entry == 0) {
    return 0;
  }

  const auto link_state = ResolveInboxAttachmentLinkState(*attachment);
  if (!link_state.has_value()) {
    return 0;
  }

  const auto info = ResolveMailAttachmentTemplateInfo(
      L, adapter, attachment->item_entry,
      BuildInboxAsyncRefreshQueryOptions(L, attachment->item_entry));
  if (!info.resolved) {
    return 0;
  }

  const std::string link = BuildMailAttachmentLink(L, adapter, *link_state);
  lua_pushlstring(L, link.c_str(), link.size());
  return 1;
}

int LuaGetInboxInvoiceInfo(lua_State *L) {
  const auto idx = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: GetInboxInvoiceInfo(index)");
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter) {
    return PushInboxInvoiceFallback(L);
  }

  const auto &inbox = RequireMailLuaAdapter(L).mail().inbox();
  if (idx >= inbox.size()) {
    return PushInboxInvoiceFallback(L);
  }

  const auto &mail = inbox[idx];
  if (mail.message_type != ::openwow::game::MailType::kAuction) {
    return PushInboxInvoiceFallback(L);
  }

  if (mail.body.empty()) {
    return PushInboxInvoiceFallback(L);
  }

  const auto invoice_subject = ParseAuctionInvoiceSubject(mail.subject);
  if (!invoice_subject.has_value()) {
    return PushInboxInvoiceFallback(L);
  }

  const auto item_name = ResolveMailItemDisplayName(L, adapter, invoice_subject->item_id,
                                                    invoice_subject->random_property_id);
  if (item_name.empty()) {
    return PushInboxInvoiceFallback(L);
  }

  const auto invoice_body = ParseInboxInvoiceBody(mail.body);
  const auto player_name =
      ResolveInvoicePlayerName(L, adapter, invoice_body.player_guid);
  const auto [delivery_hour, delivery_minute] =
      DecodePackedHourMinute(static_cast<std::uint32_t>(invoice_body.values[5]));

  lua_pushstring(L, GetAuctionInvoiceLuaType(invoice_subject->invoice_type));
  lua_pushstring(L, item_name.c_str());
  lua_pushstring(L, player_name.c_str());
  lua_pushnumber(L, invoice_body.values[0]);
  lua_pushnumber(L, invoice_body.values[1]);
  lua_pushnumber(L, invoice_body.values[2]);
  lua_pushnumber(L, invoice_body.values[3]);
  lua_pushnumber(L, invoice_body.values[4]);
  lua_pushnumber(L, delivery_hour);
  lua_pushnumber(L, delivery_minute);
  return 10;
}

}
