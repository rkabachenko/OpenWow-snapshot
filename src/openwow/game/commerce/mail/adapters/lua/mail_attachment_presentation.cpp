
#include "openwow/game/commerce/mail/adapters/lua/mail_attachment_presentation.h"
#include "openwow/game/commerce/mail/adapters/lua/mail_lua_adapter.h"
#include "openwow/game/commerce/mail/adapters/ui/mail_stationery_choices.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/inventory/items/item_trade_eligibility.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/inventory/operations/player_item_packet_location.h"
#include "openwow/game/query_cache.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_numeric.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace openwow::ui::game::detail {

::openwow::game::MailComposeState& RequireMailCompose(lua_State* L) {
  return RequireMailLuaAdapter(L).mail().compose();
}

std::optional<std::string>
TryResolveMailSenderName(lua_State *L, MailLuaAdapter *adapter,
                         const ::openwow::game::MailType type, const std::uint32_t sender_entry,
                         const std::uint64_t sender_guid, const std::uint32_t stationery_id) {
  if (adapter == nullptr) {
    return std::nullopt;
  }

  switch (type) {
  case ::openwow::game::MailType::kNormal:
  case ::openwow::game::MailType::kCalendar:
    if (stationery_id == 61) {
      return adapter->Localize("GM_EMAIL_NAME");
    }
    if (sender_guid == 0) {
      return std::nullopt;
    }
    if (const auto *player_name = adapter->queries().GetPlayerName(sender_guid);
        player_name != nullptr) {
      return player_name->name;
    }
    (void)adapter->queries().RequestNameQuery(sender_guid);
    return std::nullopt;
  case ::openwow::game::MailType::kAuction:
    if (const auto *dbc = RequireMailLuaAdapter(L).dbc(); dbc != nullptr) {
      if (const auto *auction_house = dbc->auction_house().LookupEntry(sender_entry);
          auction_house != nullptr) {
        return std::string(auction_house->name);
      }
    }
    return std::nullopt;
  case ::openwow::game::MailType::kCreature:
    if (const auto *creature = adapter->queries().GetOrRequestCreatureTemplate(sender_entry);
        creature != nullptr) {
      return creature->name;
    }
    return std::nullopt;
  case ::openwow::game::MailType::kGameObject:
    if (const auto *game_object =
            adapter->queries().GetOrRequestGameObjectTemplate(sender_entry);
        game_object != nullptr) {
      return game_object->name;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

void PushLatestMailSenderName(lua_State *L, MailLuaAdapter *adapter,
                                     const ::openwow::game::NextMailTimeSender &sender) {
  if (const auto resolved = TryResolveMailSenderName(
          L, adapter,
          static_cast<::openwow::game::MailType>(sender.message_type), sender.sender_entry,
          sender.sender_guid, sender.stationery);
      resolved.has_value()) {
    lua_pushlstring(L, resolved->data(), resolved->size());
    return;
  }

  lua_pushnil(L);
}

::openwow::game::AsyncQueryChannel::Callback
BuildInboxAsyncRefreshCallback(::openwow::game::MailInteraction* mail) {
  return [mail](const bool success) {
    if (success && mail != nullptr) {
      mail->QueueInboxUpdateEvent();
    }
  };
}

::openwow::game::QueryCache::QueryRequestOptions
BuildInboxAsyncRefreshQueryOptions(lua_State* L, const std::uint32_t cookie) {
  return ::openwow::game::QueryCache::QueryRequestOptions{
      .callback_key = ::openwow::game::AsyncQueryChannel::CallbackKey(
          reinterpret_cast<std::uintptr_t>(&BuildInboxAsyncRefreshCallback), cookie),
      .callback =
          BuildInboxAsyncRefreshCallback(&RequireMailLuaAdapter(L).mail()),
  };
}

bool LocalPlayerCanUseInboxItem(
    const MailLuaAdapter *adapter,
    const ::openwow::game::ItemUseRequirementView &item_template) {
  if (adapter == nullptr) {
    return true;
  }

  const auto *player = adapter->objects().GetActivePlayer();
  if (player == nullptr) {
    return true;
  }

  return adapter->MeetsItemRequirements(*player, item_template);
}

bool LocalPlayerCanUseInboxItem(lua_State* L,
                                       const MailLuaAdapter *adapter,
                                       const std::uint32_t item_id) {
  if (const auto *item = RequireMailLuaAdapter(L).items().GetItem(item_id); item != nullptr) {
    return LocalPlayerCanUseInboxItem(
        adapter, ::openwow::game::BuildItemUseRequirementView(*item));
  }

  if (adapter != nullptr) {
    if (const auto *item = adapter->queries().GetItemTemplate(item_id); item != nullptr) {
      return LocalPlayerCanUseInboxItem(
          adapter, ::openwow::game::BuildItemUseRequirementView(*item));
    }
  }

  return false;
}

void PushInboxItemEmptyResult(lua_State *L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
}

void PushInboxItemPendingResult(lua_State *L,
                                       const ::openwow::game::MailItemInfo & ) {
  const std::string name =
      RequireMailLuaAdapter(L).Localize("UNKNOWNOBJECT", "UNKNOWNOBJECT");
  lua_pushlstring(L, name.c_str(), name.size());
  lua_pushstring(L, "Interface\\Icons\\INV_Misc_QuestionMark");
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
}

bool HasPendingSendMailComposeLock(lua_State* L) {
  return RequireMailCompose(L).IsComposeLocked();
}

void ResetMailComposeUiState(MailLuaAdapter& adapter,
                             bool fire_script_events) {
  auto& mail = adapter.mail();
  mail.ResetCompose();

  if (!fire_script_events) {
    return;
  }

  adapter.Present(MailLuaEvent::kSendMoneyChanged);
  adapter.Present(MailLuaEvent::kSendCodChanged);
  adapter.Present(MailLuaEvent::kSendSucceeded);
}

void CloseMailInteraction(MailLuaAdapter& adapter) {
  auto& mail = adapter.mail();
  const bool had_mailbox = mail.mailbox_guid() != 0;

  mail.CloseMailbox(false);
  if (mail.ConsumeNextMailTimeQueryRequest()) {
    adapter.interaction().SendQueryNextMailTime();
  }

  if (had_mailbox) {
    adapter.Present(MailLuaEvent::kMailClosed);
  }
}

std::optional<SendMailAttachmentTemplateView>
ResolveSendMailAttachmentTemplate(lua_State* L,
                                  MailLuaAdapter *adapter,
                                  std::uint32_t item_id) {
  if (const auto *cached = RequireMailLuaAdapter(L).items().GetItem(item_id); cached != nullptr) {
    return SendMailAttachmentTemplateView{
        .flags = cached->flags,
        .bonding = cached->bonding,
        .area = cached->area,
        .map = cached->map,
        .duration = cached->duration,
        .holiday_id = cached->holiday_id,
    };
  }

  if (adapter != nullptr) {
    if (const auto *tmpl = adapter->queries().GetOrRequestItemTemplate(item_id);
        tmpl != nullptr) {
      return SendMailAttachmentTemplateView{
          .flags = tmpl->flags,
          .bonding = tmpl->bonding,
          .area = tmpl->area,
          .map = tmpl->map,
          .duration = tmpl->duration,
          .holiday_id = tmpl->holiday_id,
      };
    }
  }

  return std::nullopt;
}

bool IsEquippedMailSource(std::uint8_t source_bag, std::uint8_t source_slot) {
  return source_bag == ::openwow::game::InventorySlots::kMainBag &&
         source_slot < ::openwow::game::InventorySlots::kBackpackStart;
}

bool ItemHasRefundBlockingMailModification(const ::openwow::game::ItemInstance &item) {
  return ::openwow::game::ItemHasRefundBlockingEnchantmentState(item);
}

bool LuaHasNumericArgument(lua_State *L, int index) {
  return lua_isnumber(L, index) != 0;
}

std::uint32_t LuaCheckSaturatedU32(lua_State *L, int argument,
                                          const char *usage) {
  if (!LuaHasNumericArgument(L, argument)) {
    luaL_error(L, usage);
  }
  return ::openwow::ui::SaturateLuaNumberToU32(
      lua_tonumber(L, argument));
}

std::int32_t LuaCheckSignedI32(lua_State *L, int argument,
                                     const char *usage) {
  if (!LuaHasNumericArgument(L, argument)) {
    luaL_error(L, usage);
  }
  return TruncateLuaNumberToSseI32(lua_tonumber(L, argument));
}

std::uint32_t LuaCheckSaturatedOneBasedIndex(lua_State *L,
                                                    int argument,
                                                    const char *usage) {
  return LuaCheckSaturatedU32(L, argument, usage) - 1u;
}

std::uint32_t LuaOptionalSaturatedSlotAfterSubtract(
    lua_State *L, int argument, std::uint32_t default_slot) {
  if (!LuaHasNumericArgument(L, argument)) {
    return default_slot;
  }
  return ::openwow::ui::SaturateLuaNumberToU32(
      lua_tonumber(L, argument) - 1.0);
}

std::uint32_t LuaCheckMailCopperAmount(lua_State *L, const char *usage) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, usage);
  }

  return ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
}

bool CanPlayerAffordSendMailMoney(lua_State *L, std::uint32_t amount) {
  const auto *adapter = &RequireMailLuaAdapter(L);
  const auto *player = adapter ? adapter->objects().GetLocalPlayer() : nullptr;
  if (player != nullptr && player->GetUInt32(PLAYER_FIELD_COINAGE) >= amount) {
    return true;
  }

  RequireMailLuaAdapter(L).ShowSystemMessage(40);
  return false;
}

const ::openwow::game::MailAttachment *
GetDraftAttachmentBySlot(const ::openwow::game::MailDraft &draft, std::uint32_t slot) {
  if (slot >= draft.attachments.size()) {
    return nullptr;
  }

  const auto &attachment = draft.attachments[slot];
  if (attachment.item_guid == 0 && attachment.item_id == 0) {
    return nullptr;
  }

  return &attachment;
}

void TrimEmptyDraftAttachmentTail(::openwow::game::MailDraft &draft) {
  while (!draft.attachments.empty()) {
    const auto &attachment = draft.attachments.back();
    if (attachment.item_guid != 0 || attachment.item_id != 0) {
      break;
    }
    draft.attachments.pop_back();
  }
}

void SetDraftAttachmentSlot(::openwow::game::MailDraft &draft, std::uint32_t slot,
                                   const ::openwow::game::MailAttachment &attachment) {
  if (slot >= kSendMailLinkSlotCount) {
    return;
  }

  if (draft.attachments.size() <= slot) {
    draft.attachments.resize(slot + 1);
  }

  if (attachment.item_guid != 0) {
    for (std::size_t index = 0; index < draft.attachments.size(); ++index) {
      if (index != slot && draft.attachments[index].item_guid == attachment.item_guid) {
        draft.attachments[index] = {};
      }
    }
  }

  draft.attachments[slot] = attachment;
  TrimEmptyDraftAttachmentTail(draft);
}

void ClearDraftAttachmentSlot(::openwow::game::MailDraft &draft, std::uint32_t slot) {
  if (slot >= draft.attachments.size()) {
    return;
  }

  draft.attachments[slot] = {};
  TrimEmptyDraftAttachmentTail(draft);
}

::openwow::game::MailAttachment
BuildDraftAttachmentFromItemInstance(const ::openwow::game::ItemInstance &item, std::uint32_t slot) {
  return ::openwow::game::MailAttachment{
      .slot = slot,
      .item_id = item.entry,
      .item_count = std::max(item.count, 1u),
      .enchant_id = item.GetPermanentEnchant(),
      .gem_ids = {item.GetSocketEnchant(0), item.GetSocketEnchant(1), item.GetSocketEnchant(2)},
      .random_property_id = item.random_property,
      .suffix_factor = item.random_suffix,
      .charges = 0,
      .item_guid = item.guid,
  };
}

bool IsDraftAttachmentSlotOccupied(const ::openwow::game::MailDraft &draft, std::uint32_t slot) {
  const auto *attachment = GetDraftAttachmentBySlot(draft, slot);
  return attachment != nullptr && attachment->item_guid != 0;
}

std::optional<std::uint32_t>
FindFirstEmptySendMailAttachmentSlot(const ::openwow::game::MailDraft &draft) {
  for (std::uint32_t slot = 0; slot < kSendMailInteractiveSlotCount; ++slot) {
    if (!IsDraftAttachmentSlotOccupied(draft, slot)) {
      return slot;
    }
  }

  return std::nullopt;
}

std::size_t CountOccupiedSendMailAttachmentSlots(const ::openwow::game::MailDraft &draft) {
  std::size_t count = 0;
  for (std::uint32_t slot = 0; slot < kSendMailLinkSlotCount; ++slot) {
    if (IsDraftAttachmentSlotOccupied(draft, slot)) {
      ++count;
    }
  }
  return count;
}

void ClearInteractiveDraftAttachmentSlot(::openwow::game::MailDraft &draft, std::uint32_t slot) {
  if (slot >= kSendMailInteractiveSlotCount) {
    return;
  }

  ClearDraftAttachmentSlot(draft, slot);
}

std::optional<GuidBackedDraftAttachmentState>
ResolveGuidBackedDraftAttachmentState(
    lua_State* L, const ::openwow::game::MailDraft &draft,
    std::uint32_t slot) {
  const auto *attachment = GetDraftAttachmentBySlot(draft, slot);
  if (attachment == nullptr || attachment->item_guid == 0) {
    return std::nullopt;
  }

  const auto inventory_slot =
      RequireMailLuaAdapter(L).inventory().FindSlotByGuid(attachment->item_guid);
  if (inventory_slot < 0) {
    return std::nullopt;
  }

  const auto *item = RequireMailLuaAdapter(L).inventory().GetItemInSlot(
      static_cast<std::uint8_t>(inventory_slot));
  if (item == nullptr || item->IsEmpty() || item->guid != attachment->item_guid) {
    return std::nullopt;
  }

  GuidBackedDraftAttachmentState state;
  state.slot = slot;
  state.attachment = *attachment;
  state.item = *item;
  state.attachment.slot = slot;
  state.attachment.item_guid = item->guid;
  state.attachment.item_id = item->entry;
  state.attachment.item_count = std::max(item->count, 1u);
  state.attachment.enchant_id = item->GetPermanentEnchant();
  state.attachment.gem_ids = {item->GetSocketEnchant(0), item->GetSocketEnchant(1),
                              item->GetSocketEnchant(2)};
  state.attachment.random_property_id =
      (item->flags & ::openwow::game::ItemFlags::kWrapped) != 0 ? 0 : item->random_property;
  state.attachment.suffix_factor = item->random_suffix;
  return state;
}

const ::openwow::game::MailEntry *
GetInboxMailByZeroBasedIndex(const ::openwow::game::MailInteraction &mail_manager,
                             const std::size_t zero_based_index) {
  return mail_manager.GetInboxMail(zero_based_index);
}

::openwow::game::MailEntry *
GetMutableInboxMailByZeroBasedIndex(::openwow::game::MailInteraction &mail_manager,
                                    const std::size_t zero_based_index) {
  return mail_manager.GetMutableInboxMail(zero_based_index);
}

std::uint32_t GetOptionalInboxItemSlot(lua_State *L) {
  return LuaOptionalSaturatedSlotAfterSubtract(L, 2, 0xFFFFFFFFu);
}

std::optional<MailAttachmentLinkState>
ResolveSendMailAttachmentState(const GuidBackedDraftAttachmentState &attachment_state) {
  const auto &attachment = attachment_state.attachment;
  MailAttachmentLinkState state{
      .item_id = attachment.item_id,
      .enchant_id = attachment.enchant_id,
      .gem_ids = attachment.gem_ids,
      .random_property_id = attachment.random_property_id,
      .suffix_factor = attachment.suffix_factor,
  };

  if (state.item_id == 0) {
    return std::nullopt;
  }

  return state;
}

MailAttachmentLinkState
BuildSendMailAttachmentLinkState(const ::openwow::game::ItemInstance &item) {
  return MailAttachmentLinkState{
      .item_id = item.entry,
      .enchant_id = item.GetPermanentEnchant(),
      .gem_ids = {item.GetSocketEnchant(0), item.GetSocketEnchant(1), item.GetSocketEnchant(2)},
      .random_property_id =
          (item.flags & ::openwow::game::ItemFlags::kWrapped) != 0 ? 0 : item.random_property,
      .suffix_factor = item.random_suffix,
  };
}

void FireMailSendInfoUpdate(lua_State *L) {
  RequireMailLuaAdapter(L).Present(MailLuaEvent::kSendInfoChanged);
}

::openwow::game::AsyncQueryChannel::Callback
BuildSendInfoRefreshCallback(::openwow::game::MailInteraction* mail) {
  return [mail](const bool success) {
    if (success && mail != nullptr) {
      mail->QueueSendInfoUpdateEvent();
    }
  };
}

void FireMailUnlockSendItems(lua_State *L) {
  RequireMailLuaAdapter(L).Present(MailLuaEvent::kUnlockSendItems);
}

void FireMailLockSendItems(lua_State *L, std::uint32_t slot,
                                  const std::string &item_link) {
  RequireMailLuaAdapter(L).Present(
      MailLuaEvent::kLockSendItems, static_cast<int>(slot + 1), item_link);
}

MailAttachmentTemplateInfo
ResolveMailAttachmentTemplateInfo(lua_State* L,
                                  MailLuaAdapter *adapter,
                                  std::uint32_t item_id,
                                  ::openwow::game::QueryCache::QueryRequestOptions request_options) {
  if (const auto *item = RequireMailLuaAdapter(L).items().GetItem(item_id)) {
    return MailAttachmentTemplateInfo{
        .name = item->name,
        .quality = static_cast<std::uint32_t>(item->quality),
        .display_id = item->display_id,
        .inventory_type = static_cast<std::uint32_t>(item->inventory_type),
        .resolved = true,
    };
  }

  if (adapter) {
    if (const auto *item =
            adapter->queries().GetOrRequestItemTemplate(item_id, std::move(request_options))) {
      return MailAttachmentTemplateInfo{
          .name = item->name,
          .quality = static_cast<std::uint32_t>(item->quality),
          .display_id = item->display_id,
          .inventory_type =
              static_cast<std::uint32_t>(item->inventory_type),
          .resolved = true,
      };
    }
  }

  return {};
}

std::string ResolveMailAttachmentTexturePath(lua_State *L, std::uint32_t display_id) {
  const auto *dbc = RequireMailLuaAdapter(L).dbc();
  if (dbc && display_id != 0) {
    if (const auto *display = dbc->item_display_info().LookupEntry(display_id);
        display != nullptr && !std::string_view(display->inventory_icon).empty()) {
      return "Interface\\Icons\\" + std::string(display->inventory_icon);
    }
  }

  return "Interface\\Icons\\INV_Misc_QuestionMark";
}

std::string ResolveMailAttachmentDisplayName(lua_State *L,
                                                    const MailAttachmentTemplateInfo &info,
                                                    std::int32_t random_property_id) {
  return ::openwow::game::FormatItemDisplayNameWithRandomProperty(
      RequireMailLuaAdapter(L).localization(),
      RequireMailLuaAdapter(L).dbc(), info.name, random_property_id);
}

std::string ResolveMailAttachmentDisplayName(lua_State *L,
                                                    MailLuaAdapter *adapter,
                                                    const MailAttachmentLinkState &attachment) {
  const auto info =
      ResolveMailAttachmentTemplateInfo(L, adapter, attachment.item_id);
  return ResolveMailAttachmentDisplayName(L, info, attachment.random_property_id);
}

std::string ResolveMailItemDisplayName(lua_State *L, MailLuaAdapter *adapter,
                                              std::uint32_t item_id,
                                              std::int32_t random_property_id,
                                              ::openwow::game::QueryCache::QueryRequestOptions
                                                  request_options) {
  const auto info = ResolveMailAttachmentTemplateInfo(
      L, adapter, item_id, std::move(request_options));
  return ResolveMailAttachmentDisplayName(L, info, random_property_id);
}

std::string ResolveMailItemDisplayName(lua_State *L, MailLuaAdapter *adapter,
                                              std::uint32_t item_id,
                                              std::int32_t random_property_id) {
  return ResolveMailItemDisplayName(
      L, adapter, item_id, random_property_id,
      ::openwow::game::QueryCache::QueryRequestOptions{});
}

std::size_t CountInboxAttachments(const ::openwow::game::MailEntry &mail) {
  return static_cast<std::size_t>(
      std::count_if(mail.items.begin(), mail.items.end(), [](const auto &item) {
        return item.item_entry != 0;
      }));
}

const ::openwow::game::MailItemInfo *
GetFirstInboxAttachment(const ::openwow::game::MailEntry &mail) {
  const auto attachment = std::find_if(mail.items.begin(), mail.items.end(), [](const auto &item) {
    return item.item_entry != 0;
  });
  return attachment != mail.items.end() ? &*attachment : nullptr;
}

std::uint32_t ResolveEffectiveInboxStationeryId(lua_State *L,
                                                       const ::openwow::game::MailEntry &mail) {
  if (mail.stationery != 0) {
    return mail.stationery;
  }

  const auto *dbc = RequireMailLuaAdapter(L).dbc();
  if (dbc == nullptr || dbc->stationery().entries().empty()) {
    return 0;
  }

  return dbc->stationery().entries().front().id;
}

std::string ResolveInboxPackageIconPath(lua_State *L, MailLuaAdapter *adapter,
                                               const ::openwow::game::MailEntry &mail,
                                               std::size_t attachment_count) {
  if (mail.package_icon_id != 0) {
    if (const auto package = RequireMailCompose(L).GetPackageById(mail.package_icon_id);
        package.has_value() && !package->icon_path.empty()) {
      return package->icon_path;
    }
  }

  if (attachment_count > 1) {
    return ResolveMailAttachmentTexturePath(L, 7913u);
  }

  if (attachment_count == 1) {
    if (const auto *attachment = GetFirstInboxAttachment(mail); attachment != nullptr) {
      const auto info = ResolveMailAttachmentTemplateInfo(
          L, adapter, attachment->item_entry,
          BuildInboxAsyncRefreshQueryOptions(L, attachment->item_entry));
      if (info.resolved) {
        return ResolveMailAttachmentTexturePath(L, info.display_id);
      }
    }
  }

  return {};
}

std::string ResolveInboxStationeryIconPath(lua_State *L,
                                                  MailLuaAdapter *adapter,
                                                  const std::uint32_t stationery_id) {
  const auto *dbc = RequireMailLuaAdapter(L).dbc();
  if (dbc == nullptr) {
    return {};
  }

  if (stationery_id == 0) {
    return {};
  }

  const auto *stationery = dbc->stationery().LookupEntry(stationery_id);
  if (stationery == nullptr || stationery->item_id == 0) {
    return {};
  }

  const auto info = ResolveMailAttachmentTemplateInfo(
      L, adapter, stationery->item_id,
      BuildInboxAsyncRefreshQueryOptions(L, stationery->item_id));
  if (!info.resolved) {
    return {};
  }

  return ResolveMailAttachmentTexturePath(L, info.display_id);
}

std::string ResolveInboxStationeryIconPath(lua_State *L,
                                                  MailLuaAdapter *adapter,
                                                  const ::openwow::game::MailEntry &mail) {
  return ResolveInboxStationeryIconPath(L, adapter, ResolveEffectiveInboxStationeryId(L, mail));
}

void PushInboxSenderName(lua_State *L, MailLuaAdapter *adapter,
                                const ::openwow::game::MailEntry &mail,
                                std::uint32_t effective_stationery_id) {
  if (const auto resolved =
          TryResolveMailSenderName(L, adapter, mail.message_type, mail.sender_entry,
                                   mail.sender_guid, effective_stationery_id);
      resolved.has_value()) {
    lua_pushlstring(L, resolved->data(), resolved->size());
    return;
  }

  lua_pushnil(L);
}

std::uint32_t ResolveMailAttachmentLinkLevel(MailLuaAdapter *adapter) {
  if (!adapter) {
    return 0;
  }

  if (const auto *player = adapter->objects().GetActivePlayer(); player != nullptr) {
    return player->State().GetLevel();
  }

  return 0;
}

std::string BuildMailAttachmentLink(lua_State *L, MailLuaAdapter *adapter,
                                           const MailAttachmentLinkState &attachment) {
  const auto info =
      ResolveMailAttachmentTemplateInfo(L, adapter, attachment.item_id);
  const auto quality = info.quality < 8 ? info.quality : 1u;

  return ::openwow::game::HyperlinkParser::Build(
      "item", attachment.item_id, ResolveMailAttachmentDisplayName(L, adapter, attachment),
      ::openwow::game::HyperlinkParser::GetQualityColor(quality),
      {
          std::to_string(attachment.enchant_id),
          std::to_string(attachment.gem_ids[0]),
          std::to_string(attachment.gem_ids[1]),
          std::to_string(attachment.gem_ids[2]),
          "0",
          std::to_string(attachment.random_property_id),
          std::to_string(attachment.suffix_factor),
          std::to_string(ResolveMailAttachmentLinkLevel(adapter)),
      });
}

std::optional<MailAttachmentLinkState>
ResolveInboxAttachmentLinkState(const ::openwow::game::MailItemInfo &attachment) {
  if (attachment.item_entry == 0) {
    return std::nullopt;
  }

  using ::openwow::game::EnchantmentSlot;

  return MailAttachmentLinkState{
      .item_id = attachment.item_entry,
      .enchant_id = attachment.enchants[static_cast<std::size_t>(EnchantmentSlot::Permanent)]
                        .enchant_id,
      .gem_ids =
          {
              attachment.enchants[static_cast<std::size_t>(EnchantmentSlot::Socket1)].enchant_id,
              attachment.enchants[static_cast<std::size_t>(EnchantmentSlot::Socket2)].enchant_id,
              attachment.enchants[static_cast<std::size_t>(EnchantmentSlot::Socket3)].enchant_id,
          },
      .random_property_id = attachment.random_property_id,
      .suffix_factor = attachment.suffix_factor,
  };
}

std::vector<::openwow::game::MailStationeryListing>
GetStationeryListings(lua_State *L,
                      MailLuaAdapter *adapter) {
  return RequireMailLuaAdapter(L).stationery().Refresh(
      RequireMailLuaAdapter(L).dbc(), adapter->queries(),
      RequireMailLuaAdapter(L).items(),
      RequireMailLuaAdapter(L).inventory());
}

const ::openwow::game::MailStationeryListing *FindSelectedStationery(const std::vector<::openwow::game::MailStationeryListing> &listings,
                                                       std::uint32_t selected_stationery_id) {
  for (const auto &listing : listings) {
    if (listing.stationery_id == selected_stationery_id) {
      return &listing;
    }
  }
  return nullptr;
}

std::uint32_t GetSelectedStationeryId(lua_State* L) {
  return RequireMailCompose(L).GetDraft().stationery;
}

void SetSelectedStationeryId(lua_State* L,
                                    std::uint32_t stationery_id) {
  auto &compose = RequireMailCompose(L);
  auto draft = compose.GetDraft();
  draft.stationery = stationery_id;
  compose.SetDraft(draft);
}

}
