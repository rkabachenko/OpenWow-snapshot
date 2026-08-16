
#include "openwow/game/petition_frame.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/inventory/adapters/ui/item_cursor_pickup_controller.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/localization.h"
#include "openwow/game/name_validation.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace openwow::game {

namespace {

uint64_t g_auctionItemGuid   = 0;
uint32_t g_auctionContainerId = 0;
uint32_t g_auctionSlotId      = 0;
uint8_t  g_auctionFlags       = 0;

constexpr std::uint32_t kGuildPetitionType = 0;
constexpr std::uint32_t kArenaPetitionKind = 1;
constexpr std::uint8_t kArenaTeamSlotCount = 3;

constexpr int kItemNotFoundSystemMessage = 0x85;
constexpr std::size_t kPetitionBuyNameMaxBytes = 0x100 - 1;
constexpr int kArenaPetitionNotFoundSystemMessage = 547;
constexpr int kTabardPreviewUnavailableMessage = 323;
constexpr std::array<std::uint32_t, 5> kDefaultTurnInPetitionFields{
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};

enum class GuildCharterItemCheckResult {
  kContinueSearch,
  kTurnedIn,
  kStopWithoutMessage,
};

GuildCharterItemCheckResult TryTurnInGuildCharterItem(
    WorldSession& session, const ItemInstance* item) {
  if (item == nullptr || item->guid == 0 ||
      (item->flags & ItemFlags::kQuestItem) == 0) {
    return GuildCharterItemCheckResult::kContinueSearch;
  }

  const auto* petition = session.petition().FindCachedPetitionQuery(item->entry);
  if (petition == nullptr) {
    return GuildCharterItemCheckResult::kStopWithoutMessage;
  }

  if (petition->petition_type != kGuildPetitionType) {
    return GuildCharterItemCheckResult::kContinueSearch;
  }

  session.interaction().SendTurnInPetition(item->guid);
  return GuildCharterItemCheckResult::kTurnedIn;
}

bool IsPetitionInventoryItem(WorldSession& session, const ItemInstance* item,
                             PetitionQueryResponse* out_query = nullptr) {
  if (item == nullptr || item->guid == 0 ||
      (item->flags & ItemFlags::kQuestItem) == 0) {
    return false;
  }

  const auto* petition = session.petition().FindCachedPetitionQuery(item->entry);
  if (petition == nullptr) {
    return false;
  }

  if (out_query != nullptr) {
    *out_query = *petition;
  }
  return true;
}

template <typename Predicate>
const ItemInstance* FindInventoryPetitionItem(WorldSession& session,
                                              Predicate&& predicate) {
  auto& inventory = session.inventory_replica();

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    const auto* item = inventory.GetBackpackSlot(slot);
    PetitionQueryResponse petition{};
    if (IsPetitionInventoryItem(session, item, &petition) &&
        predicate(*item, petition)) {
      return item;
    }
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      const auto* item = inventory.GetBagSlot(bag, slot);
      PetitionQueryResponse petition{};
      if (IsPetitionInventoryItem(session, item, &petition) &&
          predicate(*item, petition)) {
        return item;
      }
    }
  }

  return nullptr;
}

[[nodiscard]] bool CanRequestTabardVendorActivate(const CGPlayer_C& player) {
  return player.Presentation().CurrentDisplayId() == player.Presentation().NativeDisplayId();
}

[[nodiscard]] std::string ClampPetitionBuyName(const char* name) {
  return std::string(name, std::min(std::strlen(name), kPetitionBuyNameMaxBytes));
}

[[nodiscard]] bool IsArenaPetitionOffer(const PetitionType& offer) {
  return offer.unk_value == kArenaPetitionKind;
}

[[nodiscard]] bool IsGuildPetitionOffer(const PetitionType& offer) {
  return offer.unk_value == 0;
}

[[nodiscard]] std::uint32_t ArenaPetitionTeamSize(const PetitionType& offer) {
  return offer.required_signatures + 1;
}

[[nodiscard]] bool IsArenaPetitionLevelAllowed(const std::uint32_t level) {
  return level == 60 || level == 70 || level == 80;
}

[[nodiscard]] std::uint32_t RequiredArenaPetitionLevel(const std::uint32_t level) {
  return level > 70 && level < 80 ? 80u : 70u;
}

[[nodiscard]] bool IsSelectablePetitionCursorItem(const CGItem_C& item) {
  return item.HasItemFlag(ItemFlags::kQuestItem);
}

[[nodiscard]] bool ActivePlayerHasArenaTeamOfSize(WorldSession& session,
                                                  const CGPlayer_C& player,
                                                  const std::uint32_t team_size) {
  for (std::uint8_t slot = 0; slot < kArenaTeamSlotCount; ++slot) {
    const auto team_info = player.GetArenaTeamInfo(slot);
    if (team_info.team_id == 0) {
      continue;
    }

    const auto* team_query = session.arena().FindArenaTeamQuery(team_info.team_id);
    if (team_query != nullptr && team_query->team_type == team_size) {
      return true;
    }
  }

  return false;
}

}

int PetitionFrame_ValidateRename(const char* newName) {
  const auto validation_result =
      newName == nullptr
          ? NameValidationResult::kNoName
          : ValidatePetitionName(Localization::Get().GetLocaleIndex(), newName);
  if (validation_result == NameValidationResult::kOk) {
    return 0;
  }

  ::openwow::ui::game::PetNameCache_HandlePetRenameResult(
      static_cast<int>(validation_result));
  return 1;
}

void PetitionFrame_BuyGuildCharter(WorldSession& session, const char* guildName) {
  if (guildName == nullptr || guildName[0] == '\0') {
    return;
  }

  const auto& registrar = session.petition().guild_registrar();
  if (registrar.npc_guid == 0) {
    return;
  }

  auto* const player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  if (player->GetGuildID() != 0) {
    ::openwow::ui::game::DisplaySystemMessage(kPetitionMsg_AlreadyInGuild);
    return;
  }

  if (player->GetMoney() < registrar.charter_offer.cost) {
    ::openwow::ui::game::DisplaySystemMessage(kPetitionMsg_NotEnoughMoney);
    return;
  }

  session.interaction().SendPetitionBuy(registrar.npc_guid,
                                        registrar.charter_offer.index,
                                        ClampPetitionBuyName(guildName));
}

void PetitionFrame_BuyPetition(WorldSession& session, const std::uint32_t selection_index,
                               const char* petitionName) {
  if (petitionName == nullptr || petitionName[0] == '\0') {
    return;
  }

  const auto& show_list = session.petition().last_petition_list();
  if (show_list.npc_guid == 0 || selection_index >= show_list.types.size()) {
    return;
  }

  auto* const player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  const PetitionType& offer = show_list.types[selection_index];
  if (IsGuildPetitionOffer(offer) && player->GetGuildID() != 0) {
    ::openwow::ui::game::DisplaySystemMessage(kPetitionMsg_AlreadyInGuild);
    return;
  }

  if (IsArenaPetitionOffer(offer)) {
    const auto team_size = ArenaPetitionTeamSize(offer);
    if (ActivePlayerHasArenaTeamOfSize(session, *player, team_size)) {
      ::openwow::ui::game::DisplaySystemMessage(kPetitionMsg_AlreadyInArenaTeam);
      return;
    }

    if (!IsArenaPetitionLevelAllowed(player->State().GetLevel())) {
      ::openwow::ui::game::DisplaySystemMessage(
          kPetitionMsg_ArenaRequiresLevel,
          RequiredArenaPetitionLevel(player->State().GetLevel()));
      return;
    }
  }

  if (player->GetMoney() < offer.cost) {
    ::openwow::ui::game::DisplaySystemMessage(kPetitionMsg_NotEnoughMoney);
    return;
  }

  session.interaction().SendPetitionBuy(show_list.npc_guid, offer.index,
                                        ClampPetitionBuyName(petitionName));
}

void PetitionFrame_ClickPetitionButton(WorldSession& session) {
  auto* cursor = session.held_cursor();
  if (session.petition().petition_vendor_guid() == 0 || cursor == nullptr) {
    return;
  }

  const auto* held_item = cursor->live_item();
  if (held_item == nullptr || held_item->item.guid == 0) {
    session.petition().ClearSelectedPetitionCursor();
    ::openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        ::openwow::ui::game::events::PETITION_VENDOR_UPDATE);
    return;
  }

  const auto* item_object =
      session.objects().GetItem(ObjectGuid(held_item->item.guid));
  if (item_object == nullptr || !IsSelectablePetitionCursorItem(*item_object)) {
    return;
  }

  const auto previous_selection = session.petition().selected_petition_cursor();
  session.petition().SetSelectedPetitionCursor(
      held_item->item.guid, held_item->source_container_guid,
      held_item->source_slot);
  ::openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
      ::openwow::ui::game::events::PETITION_VENDOR_UPDATE);

  cursor->Clear({
      .release_source_lease = false,
      .publish_money_owner_update = true,
  });
  if (previous_selection.item_guid != 0) {
    (void)inventory::ui::PickupItemCursor(
        *cursor, session.inventory_replica(), session.item_definitions(),
        session.GetDbcLoader(), session.objects().GetActivePlayerGuid(),
        ObjectGuid(previous_selection.item_guid),
        {
            .source =
                {
                    .container = ObjectGuid(previous_selection.container_guid),
                    .slot = static_cast<std::int32_t>(previous_selection.source_slot),
                },
            .source_policy = inventory::ui::ItemCursorSourcePolicy::kProvidedOrigin,
        });
  }
}

bool PetitionFrame_TurnInGuildCharter(WorldSession& session) {
  if (session.objects().GetActivePlayer() == nullptr) {
    return false;
  }

  auto& inventory = session.inventory_replica();

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    switch (TryTurnInGuildCharterItem(session, inventory.GetBackpackSlot(slot))) {
    case GuildCharterItemCheckResult::kContinueSearch:
      break;
    case GuildCharterItemCheckResult::kTurnedIn:
      return true;
    case GuildCharterItemCheckResult::kStopWithoutMessage:
      return false;
    }
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      switch (TryTurnInGuildCharterItem(session, inventory.GetBagSlot(bag, slot))) {
      case GuildCharterItemCheckResult::kContinueSearch:
        break;
      case GuildCharterItemCheckResult::kTurnedIn:
        return true;
      case GuildCharterItemCheckResult::kStopWithoutMessage:
        return false;
      }
    }
  }

  ::openwow::ui::game::DisplaySystemMessage(kItemNotFoundSystemMessage);
  return false;
}

void PetitionFrame_TurnInSelectedPetition(WorldSession& session) {
  if (session.petition().petition_vendor_guid() == 0) {
    return;
  }

  const auto& selected = session.petition().selected_petition_cursor();
  if (selected.item_guid == 0) {
    return;
  }

  session.interaction().SendTurnInPetition(selected.item_guid,
                                           kDefaultTurnInPetitionFields);
}

bool PetitionFrame_HasFilledArenaPetition(WorldSession& session) {
  if (session.objects().GetActivePlayer() == nullptr) {
    return false;
  }

  return FindInventoryPetitionItem(
             session,
             [](const ItemInstance&, const PetitionQueryResponse& petition) {
               return petition.petition_type == kArenaPetitionKind;
             }) != nullptr;
}

bool PetitionFrame_TurnInArenaPetition(
    WorldSession& session, const std::uint32_t team_size,
    const std::array<std::uint32_t, 5>& extra_fields) {

  if (session.objects().GetActivePlayer() == nullptr) {
    return false;
  }

  const auto* item = FindInventoryPetitionItem(
      session, [team_size](const ItemInstance&, const PetitionQueryResponse& petition) {
        return petition.petition_type == kArenaPetitionKind &&
               petition.max_signatures + 1 == team_size;
      });
  if (item == nullptr) {
    ::openwow::ui::game::DisplaySystemMessage(
        kArenaPetitionNotFoundSystemMessage);
    return false;
  }

  session.interaction().SendTurnInPetition(item->guid, extra_fields);
  return true;
}

void PetitionFrame_RequestTabardInfo(WorldSession& session) {
  const auto registrar_guid = session.petition().guild_registrar_guid();
  (void)PetitionFrame_RequestTabardVendorActivate(session, registrar_guid);

  if (registrar_guid != 0) {
    session.CloseGuildRegistrarInteraction();
  }
}

bool PetitionFrame_RequestTabardVendorActivate(
    WorldSession& session, const std::uint64_t vendor_guid) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr ||
      session.petition().tabard_vendor_guid() == vendor_guid) {
    return false;
  }

  if (!CanRequestTabardVendorActivate(*player)) {
    ::openwow::ui::game::DisplaySystemMessage(
        kTabardPreviewUnavailableMessage);
    return false;
  }

  session.interaction().SendTabardVendorActivate(vendor_guid);
  return true;
}

void SetAuctionContainerItem(uint64_t itemGuid, uint32_t containerId,
                              uint32_t slotId, uint8_t flags) {
    g_auctionItemGuid    = itemGuid;
    g_auctionContainerId = containerId;
    g_auctionSlotId      = slotId;
    g_auctionFlags       = flags;
}

AuctionContainerState GetAuctionContainerItem() {
    return {g_auctionItemGuid, g_auctionContainerId,
            g_auctionSlotId, g_auctionFlags};
}

}
