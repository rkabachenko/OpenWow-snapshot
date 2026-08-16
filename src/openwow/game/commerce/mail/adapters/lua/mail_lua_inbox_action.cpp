
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
#include <cstdint>
#include <mutex>
#include <string_view>
#include "openwow/game/commerce/mail/adapters/lua/mail_attachment_presentation.h"

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint8_t kMailComplaintType = 0;
constexpr std::uint32_t kMailComplaintAuxiliaryWord = 0;
constexpr std::uint32_t kMailComplaintTrailingWord = 0;

}

int LuaTakeInboxItem(lua_State *L) {
  const auto mail_index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: TakeInboxItem(messageIndex, attachIndex)");
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter)
    return 0;
  auto &mail_manager = RequireMailLuaAdapter(L).mail();
  if (mail_manager.HasPendingMailboxOperation())
    return 0;

  const auto mailbox_guid = mail_manager.mailbox_guid();
  if (mailbox_guid == 0)
    return 0;

  const auto *mail = GetInboxMailByZeroBasedIndex(mail_manager, mail_index);
  if (!mail)
    return 0;

  const auto *item = mail_manager.GetMailItem(*mail, GetOptionalInboxItemSlot(L));
  if (!item || item->item_guid_low == 0)
    return 0;
  const auto *player = adapter->objects().GetLocalPlayer();
  if (!player || player->GetUInt32(PLAYER_FIELD_COINAGE) < mail->cod) {
    RequireMailLuaAdapter(L).ShowSystemMessage(40);
    return 0;
  }
  if (!mail_manager.TryStartMailboxAction())
    return 0;

  adapter->interaction().SendMailTakeItem(mailbox_guid, mail->message_id, item->item_guid_low);
  return 0;
}

int LuaTakeInboxMoney(lua_State *L) {
  const auto mail_index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: TakeInboxMoney(index)");
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter)
    return 0;
  auto &mail_manager = RequireMailLuaAdapter(L).mail();
  if (mail_manager.HasPendingMailboxOperation())
    return 0;

  const auto mailbox_guid = mail_manager.mailbox_guid();
  if (mailbox_guid == 0)
    return 0;

  const auto *mail = GetInboxMailByZeroBasedIndex(mail_manager, mail_index);
  if (!mail || mail->money == 0)
    return 0;
  if (!mail_manager.TryStartMailboxAction())
    return 0;

  adapter->interaction().SendMailTakeMoney(mailbox_guid, mail->message_id);
  return 0;
}

int LuaTakeInboxTextItem(lua_State *L) {
  const auto mail_index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: TakeInboxTextItem(index)");
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter)
    return 0;
  auto &mail_manager = RequireMailLuaAdapter(L).mail();
  if (mail_manager.HasPendingMailboxOperation())
    return 0;

  const auto mailbox_guid = mail_manager.mailbox_guid();
  if (mailbox_guid == 0)
    return 0;

  const auto *mail = GetInboxMailByZeroBasedIndex(mail_manager, mail_index);
  if (!mail)
    return 0;
  if ((mail->body.empty() && mail->mail_template_id == 0) ||
      (mail->checked & ::openwow::game::kMailCheckedCopied) != 0) {
    return 0;
  }
  if (!mail_manager.TryStartMailboxAction())
    return 0;

  adapter->interaction().SendMailCreateTextItem(mailbox_guid, mail->message_id);
  return 0;
}

int LuaDeleteInboxItem(lua_State *L) {
  const auto mail_index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: DeleteInboxItem(index)");
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter)
    return 0;
  auto &mail_manager = RequireMailLuaAdapter(L).mail();
  if (mail_manager.HasPendingMailboxOperation())
    return 0;

  const auto mailbox_guid = mail_manager.mailbox_guid();
  if (mailbox_guid == 0)
    return 0;

  auto *mail = GetMutableInboxMailByZeroBasedIndex(mail_manager, mail_index);
  if (!mail || !::openwow::game::CanDeleteInboxMail(*mail))
    return 0;

  if (!mail_manager.TryStartInboxRemovalAction(*mail))
    return 0;

  adapter->interaction().SendMailDelete(mailbox_guid, mail->message_id,
                                        ::openwow::game::MailDeleteReason::kManual);
  return 0;
}

int LuaReturnInboxItem(lua_State *L) {
  const auto mail_index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: ReturnInboxItem(index)");
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter)
    return 0;
  auto &mail_manager = RequireMailLuaAdapter(L).mail();
  if (mail_manager.HasPendingMailboxOperation())
    return 0;

  const auto mailbox_guid = mail_manager.mailbox_guid();
  if (mailbox_guid == 0)
    return 0;

  auto *mail = GetMutableInboxMailByZeroBasedIndex(mail_manager, mail_index);
  if (!mail || !::openwow::game::CanReturnInboxMail(*mail))
    return 0;
  if (!mail_manager.TryStartInboxRemovalAction(*mail))
    return 0;

  adapter->interaction().SendMailReturnToSender(mailbox_guid, mail->message_id, mail->sender_guid);
  return 0;
}

int LuaInboxItemCanDelete(lua_State *L) {
  const auto mail_index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: InboxItemCanDelete(index)");
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter) {
    lua_pushnil(L);
    return 1;
  }

  const auto *mail = RequireMailLuaAdapter(L).mail().GetInboxMail(mail_index);
  lua_pushwowbool(L, mail != nullptr && ::openwow::game::CanDeleteInboxMail(*mail));
  return 1;
}

int LuaInboxItemCanComplain(lua_State* L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: InboxItemCanComplain(index)");
  }
  auto& adapter = RequireMailLuaAdapter(L);
  const std::uint32_t mail_index =
      openwow::ui::ClampLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto* player = adapter.objects().GetActivePlayer();
  const bool can_complain =
      player != nullptr &&
      adapter.mail().CanComplainInboxItem(
          static_cast<std::size_t>(mail_index),
          player->GetGuid().GetRawValue(), adapter.social());
  if (can_complain) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaComplainInboxItem(lua_State* L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: ComplainInboxItem(index)");
  }
  auto& adapter = RequireMailLuaAdapter(L);
  auto& mail = adapter.mail();
  const std::uint32_t mail_index =
      openwow::ui::ClampLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  if (mail.HasPendingMailboxOperation()) {
    return 0;
  }

  if (adapter.social().complaint_status() == 0) {
    return 0;
  }
  if (mail.mailbox_guid() == 0) {
    return 0;
  }
  const auto* player = adapter.objects().GetLocalPlayer();
  if (player == nullptr ||
      !mail.CanComplainInboxItem(
          static_cast<std::size_t>(mail_index),
          player->GetGuid().GetRawValue(), adapter.social())) {
    return 0;
  }
  const auto* entry =
      mail.GetInboxMail(static_cast<std::size_t>(mail_index));
  if (entry == nullptr) {
    return 0;
  }
  if (adapter.social().HasRecentComplaintGuid(entry->sender_guid)) {
    adapter.ShowSystemText(
        adapter.Localize("COMPLAINT_ADDED", "COMPLAINT_ADDED"));
    return 0;
  }
  adapter.social().RememberRecentComplaintGuid(entry->sender_guid);
  adapter.interaction().SendComplain(
      kMailComplaintType, entry->sender_guid, kMailComplaintAuxiliaryWord,
      entry->message_id, kMailComplaintTrailingWord);

  ScriptEventDispatch::Get().FireEventArgs(
      events::CLOSE_INBOX_ITEM, {static_cast<int>(mail_index) + 1});
  adapter.Present(MailLuaEvent::kInboxChanged);
  adapter.interaction().SendGetMailList(mail.mailbox_guid());
  return 0;
}

int LuaAutoLootMailItem(lua_State *L) {
  const auto mail_index = LuaCheckSaturatedOneBasedIndex(
      L, 1, "Usage: AutoLootMailItem(index)");
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter) {
    return 0;
  }
  auto &mail_manager = RequireMailLuaAdapter(L).mail();
  if (mail_manager.HasPendingMailboxOperation()) {
    return 0;
  }

  const auto mailbox_guid = mail_manager.mailbox_guid();
  if (mailbox_guid == 0) {
    return 0;
  }

  const auto *mail = GetInboxMailByZeroBasedIndex(mail_manager, mail_index);
  if (mail == nullptr) {
    return 0;
  }

  const auto *player = adapter->objects().GetLocalPlayer();
  const bool can_take_attachments =
      player != nullptr && player->GetUInt32(PLAYER_FIELD_COINAGE) >= mail->cod;
  auto result = mail_manager.StartAutoLootMailSequence(*mail, can_take_attachments);
  for (const auto &command : result.followups) {
    adapter->interaction().SendMailFollowup(command);
  }
  if (result.show_attachment_autoloot_error) {
    RequireMailLuaAdapter(L).ShowSystemMessage(40);
  }
  return 0;
}

int LuaCheckInbox(lua_State *L) {
  auto *adapter = &RequireMailLuaAdapter(L);
  if (!adapter)
    return 0;

  auto& mail = RequireMailLuaAdapter(L).mail();
  switch (mail.TryStartInboxRefresh()) {
  case ::openwow::game::InboxRefreshRequestResult::kStarted:
    adapter->interaction().SendGetMailList(mail.mailbox_guid());
    break;
  case ::openwow::game::InboxRefreshRequestResult::kThrottled:
    adapter->Present(MailLuaEvent::kInboxChanged);
    break;
  case ::openwow::game::InboxRefreshRequestResult::kBlockedPending:
  case ::openwow::game::InboxRefreshRequestResult::kBlockedClosed:
    break;
  }
  return 0;
}

int LuaCloseMail([[maybe_unused]] lua_State *L) {

  auto *adapter = &RequireMailLuaAdapter(L);
  CloseMailInteraction(*adapter);
  return 0;
}

}
