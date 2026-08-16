#pragma once

#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/inventory/model/item_instance.h"
#include "openwow/game/query_cache.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct lua_State;

namespace openwow::game {
struct MailStationeryListing;
}

namespace openwow::ui::game {
class MailLuaAdapter;
namespace detail {

inline constexpr std::uint32_t kSendMailLinkSlotCount = 16;
inline constexpr std::uint32_t kSendMailInteractiveSlotCount = 12;
inline constexpr std::uint32_t kAccountBoundMailTemplateFlag = 0x08000000u;
inline constexpr int kMailComposeItemLockedMessage = 136;
inline constexpr int kMailTemplatePendingMessage = 25;
inline constexpr int kMailQuestItemMessage = 375;
inline constexpr int kMailBoundItemMessage = 376;
inline constexpr int kMailRestrictedItemMessage = 377;
inline constexpr int kMailEquippedItemMessage = 378;
inline constexpr int kMailComposeFullMessage = 584;
inline constexpr int kMailTemporaryItemMessage = 678;

struct MailAttachmentLinkState {
  std::uint32_t item_id = 0;
  std::uint32_t enchant_id = 0;
  std::array<std::uint32_t, 3> gem_ids{};
  std::int32_t random_property_id = 0;
  std::uint32_t suffix_factor = 0;
};

struct MailAttachmentTemplateInfo {
  std::string name;
  std::uint32_t quality = 1;
  std::uint32_t display_id = 0;
  std::uint32_t inventory_type = 0;
  bool resolved = false;
};

enum class AuctionInvoiceType : std::uint32_t {
  kBuyer = 1,
  kSeller = 2,
  kSellerTempInvoice = 6,
};

struct ParsedAuctionInvoiceSubject {
  std::uint32_t item_id = 0;
  std::int32_t random_property_id = 0;
  AuctionInvoiceType invoice_type = AuctionInvoiceType::kBuyer;
};

struct ParsedInboxInvoiceBody {
  std::uint64_t player_guid = 0;
  std::array<std::int32_t, 6> values{};
};

struct SendMailAttachmentTemplateView {
  std::uint32_t flags = 0;
  std::uint32_t bonding = 0;
  std::uint32_t area = 0;
  std::uint32_t map = 0;
  std::uint32_t duration = 0;
  std::uint32_t holiday_id = 0;
};

struct GuidBackedDraftAttachmentState {
  std::uint32_t slot = 0;
  openwow::game::MailAttachment attachment;
  openwow::game::ItemInstance item;
};

openwow::game::MailComposeState& RequireMailCompose(lua_State* state);
std::optional<std::string> TryResolveMailSenderName(
    lua_State*, MailLuaAdapter*, openwow::game::MailType, std::uint32_t,
    std::uint64_t, std::uint32_t);
void PushLatestMailSenderName(lua_State*, MailLuaAdapter*,
                              const openwow::game::NextMailTimeSender&);
openwow::game::QueryCache::QueryRequestOptions
BuildInboxAsyncRefreshQueryOptions(lua_State*, std::uint32_t);
void ResetMailComposeUiState(MailLuaAdapter& adapter,
                             bool fire_script_events);
void CloseMailInteraction(MailLuaAdapter& adapter);
bool LocalPlayerCanUseInboxItem(lua_State* state, const MailLuaAdapter* adapter,
                                std::uint32_t item_id);
void PushInboxItemEmptyResult(lua_State* state);
void PushInboxItemPendingResult(lua_State* state,
                                const openwow::game::MailItemInfo& attachment);
bool HasPendingSendMailComposeLock(lua_State* state);
bool LuaHasNumericArgument(lua_State* state, int index);
std::uint32_t LuaCheckSaturatedU32(lua_State*, int, const char*);
std::int32_t LuaCheckSignedI32(lua_State*, int, const char*);
std::uint32_t LuaCheckSaturatedOneBasedIndex(lua_State*, int, const char*);
std::uint32_t LuaOptionalSaturatedSlotAfterSubtract(lua_State*, int,
                                                    std::uint32_t);
std::uint32_t LuaCheckMailCopperAmount(lua_State*, const char*);
bool CanPlayerAffordSendMailMoney(lua_State*, std::uint32_t);
const openwow::game::MailAttachment* GetDraftAttachmentBySlot(
    const openwow::game::MailDraft&, std::uint32_t);
void SetDraftAttachmentSlot(openwow::game::MailDraft&, std::uint32_t,
                            const openwow::game::MailAttachment&);
void ClearInteractiveDraftAttachmentSlot(openwow::game::MailDraft&,
                                         std::uint32_t);
openwow::game::MailAttachment BuildDraftAttachmentFromItemInstance(
    const openwow::game::ItemInstance&, std::uint32_t);
std::optional<std::uint32_t> FindFirstEmptySendMailAttachmentSlot(
    const openwow::game::MailDraft&);
std::size_t CountOccupiedSendMailAttachmentSlots(
    const openwow::game::MailDraft&);
std::optional<GuidBackedDraftAttachmentState>
ResolveGuidBackedDraftAttachmentState(lua_State*,
                                      const openwow::game::MailDraft&,
                                      std::uint32_t);
const openwow::game::MailEntry* GetInboxMailByZeroBasedIndex(
    const openwow::game::MailInteraction&, std::size_t);
openwow::game::MailEntry* GetMutableInboxMailByZeroBasedIndex(
    openwow::game::MailInteraction&, std::size_t);
std::uint32_t GetOptionalInboxItemSlot(lua_State*);
std::optional<MailAttachmentLinkState> ResolveSendMailAttachmentState(
    const GuidBackedDraftAttachmentState&);
MailAttachmentLinkState BuildSendMailAttachmentLinkState(
    const openwow::game::ItemInstance&);
void FireMailSendInfoUpdate(lua_State*);
openwow::game::AsyncQueryChannel::Callback BuildSendInfoRefreshCallback(
    openwow::game::MailInteraction*);
void FireMailUnlockSendItems(lua_State*);
void FireMailLockSendItems(lua_State*, std::uint32_t, const std::string&);
std::optional<SendMailAttachmentTemplateView>
ResolveSendMailAttachmentTemplate(lua_State*, MailLuaAdapter*, std::uint32_t);
MailAttachmentTemplateInfo ResolveMailAttachmentTemplateInfo(
    lua_State*, MailLuaAdapter*, std::uint32_t,
    openwow::game::QueryCache::QueryRequestOptions = {});
std::string ResolveMailAttachmentTexturePath(lua_State*, std::uint32_t);
std::string ResolveMailAttachmentDisplayName(
    lua_State*, MailLuaAdapter*, const MailAttachmentLinkState&);
std::string ResolveMailAttachmentDisplayName(
    lua_State*, const MailAttachmentTemplateInfo&, std::int32_t);
std::string ResolveMailItemDisplayName(
    lua_State*, MailLuaAdapter*, std::uint32_t, std::int32_t,
    openwow::game::QueryCache::QueryRequestOptions = {});
std::size_t CountInboxAttachments(const openwow::game::MailEntry&);
const openwow::game::MailItemInfo* GetFirstInboxAttachment(
    const openwow::game::MailEntry&);
std::uint32_t ResolveEffectiveInboxStationeryId(
    lua_State*, const openwow::game::MailEntry&);
std::string ResolveInboxPackageIconPath(
    lua_State*, MailLuaAdapter*, const openwow::game::MailEntry&, std::size_t);
std::string ResolveInboxStationeryIconPath(
    lua_State*, MailLuaAdapter*, const openwow::game::MailEntry&);
std::string ResolveInboxStationeryIconPath(
    lua_State*, MailLuaAdapter*, std::uint32_t);
void PushInboxSenderName(lua_State*, MailLuaAdapter*,
                         const openwow::game::MailEntry&, std::uint32_t);
void PushInboxSubject(lua_State*, MailLuaAdapter*,
                      const openwow::game::MailEntry&);
std::optional<std::string> ResolveInboxBodyText(
    lua_State*, MailLuaAdapter*, const openwow::game::MailEntry&);
bool CanCreateInboxTextItem(const openwow::game::MailEntry&);
std::optional<ParsedAuctionInvoiceSubject> ParseAuctionInvoiceSubject(
    std::string_view);
ParsedInboxInvoiceBody ParseInboxInvoiceBody(std::string_view);
std::pair<std::uint32_t, std::uint32_t> DecodePackedHourMinute(std::uint32_t);
const char* GetAuctionInvoiceLuaType(AuctionInvoiceType);
std::string ResolveInvoicePlayerName(
    lua_State*, MailLuaAdapter*, std::uint64_t);
int PushInboxInvoiceFallback(lua_State*);
std::string BuildMailAttachmentLink(lua_State*, MailLuaAdapter*,
                                    const MailAttachmentLinkState&);
std::optional<MailAttachmentLinkState> ResolveInboxAttachmentLinkState(
    const openwow::game::MailItemInfo&);
std::vector<openwow::game::MailStationeryListing> GetStationeryListings(
    lua_State*, MailLuaAdapter*);
const openwow::game::MailStationeryListing* FindSelectedStationery(
    const std::vector<openwow::game::MailStationeryListing>&, std::uint32_t);
std::uint32_t GetSelectedStationeryId(lua_State*);
void SetSelectedStationeryId(lua_State*, std::uint32_t);

}
}
