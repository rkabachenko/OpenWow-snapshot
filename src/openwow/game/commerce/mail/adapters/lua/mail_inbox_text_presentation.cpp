#include "openwow/game/commerce/mail/adapters/lua/mail_attachment_presentation.h"
#include "openwow/game/commerce/mail/adapters/lua/mail_lua_adapter.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/core/storm_string.h"

#include <lua.hpp>

#include <charconv>
#include <string_view>

namespace openwow::ui::game::detail {

namespace {

struct ParsedAuctionSubject {
  std::uint32_t item_id = 0;
  std::int32_t random_property_id = 0;
  std::uint32_t subject_type = 0;
};

}

std::optional<std::string_view> ConsumeColonToken(std::string_view *remaining) {
  if (remaining == nullptr || remaining->empty()) {
    return std::nullopt;
  }

  const auto separator = remaining->find(':');
  if (separator == std::string_view::npos) {
    const auto token = *remaining;
    remaining->remove_prefix(remaining->size());
    return token;
  }

  const auto token = remaining->substr(0, separator);
  remaining->remove_prefix(separator + 1);
  return token;
}

std::string ExpandInboxBodyText(MailLuaAdapter &adapter,
                                       std::string_view raw_text) {
  return adapter.ExpandBody(raw_text);
}

std::string ResolveCalendarInboxDisplaySubject(lua_State *L,
                                                      const ::openwow::game::MailEntry &mail) {
  auto remaining = std::string_view(mail.subject);
  (void)ConsumeColonToken(&remaining);

  std::string display_subject(remaining);
  RequireMailLuaAdapter(L).FilterMatureLanguage(display_subject);
  return display_subject;
}

template <typename Integer> static bool ParseDecimalToken(std::string_view token, Integer *value) {
  if (value == nullptr || token.empty()) {
    return false;
  }

  Integer parsed_value = 0;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed_value, 10);
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
    return false;
  }

  *value = parsed_value;
  return true;
}

bool ParseHexGuidToken(std::string_view token, std::uint64_t *value) {
  if (value == nullptr || token.empty()) {
    return false;
  }

  std::uint64_t parsed_value = 0;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed_value, 16);
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
    return false;
  }

  *value = parsed_value;
  return true;
}

std::optional<AuctionInvoiceType> ParseAuctionInvoiceType(std::uint32_t value) {
  switch (value) {
  case static_cast<std::uint32_t>(AuctionInvoiceType::kBuyer):
    return AuctionInvoiceType::kBuyer;
  case static_cast<std::uint32_t>(AuctionInvoiceType::kSeller):
    return AuctionInvoiceType::kSeller;
  case static_cast<std::uint32_t>(AuctionInvoiceType::kSellerTempInvoice):
    return AuctionInvoiceType::kSellerTempInvoice;
  default:
    return std::nullopt;
  }
}

std::optional<ParsedAuctionSubject> ParseAuctionMailSubject(std::string_view subject) {
  ParsedAuctionSubject parsed{};

  auto remaining = subject;
  if (const auto token = ConsumeColonToken(&remaining); token.has_value()) {
    (void)ParseDecimalToken(*token, &parsed.item_id);
  }

  if (const auto token = ConsumeColonToken(&remaining); token.has_value()) {
    (void)ParseDecimalToken(*token, &parsed.random_property_id);
  }

  if (const auto token = ConsumeColonToken(&remaining); token.has_value()) {
    (void)ParseDecimalToken(*token, &parsed.subject_type);
  }

  if (parsed.item_id == 0) {
    return std::nullopt;
  }

  return parsed;
}

std::optional<ParsedAuctionInvoiceSubject>
ParseAuctionInvoiceSubject(std::string_view subject) {
  const auto parsed_mail_subject = ParseAuctionMailSubject(subject);
  if (!parsed_mail_subject.has_value()) {
    return std::nullopt;
  }

  if (const auto invoice_type = ParseAuctionInvoiceType(parsed_mail_subject->subject_type);
      invoice_type.has_value()) {
    return ParsedAuctionInvoiceSubject{
        .item_id = parsed_mail_subject->item_id,
        .random_property_id = parsed_mail_subject->random_property_id,
        .invoice_type = *invoice_type,
    };
  }

  return std::nullopt;
}

const char *GetAuctionMailSubjectFormatKey(const std::uint32_t subject_type) {
  switch (subject_type) {
  case 0:
    return "AUCTION_OUTBID_MAIL_SUBJECT";
  case 1:
    return "AUCTION_WON_MAIL_SUBJECT";
  case 2:
    return "AUCTION_SOLD_MAIL_SUBJECT";
  case 3:
    return "AUCTION_EXPIRED_MAIL_SUBJECT";
  case 4:
  case 5:
    return "AUCTION_REMOVED_MAIL_SUBJECT";
  case 6:
    return "AUCTION_INVOICE_MAIL_SUBJECT";
  default:
    return nullptr;
  }
}

std::string ResolveFormattedInboxSubject(MailLuaAdapter& adapter,
                                                const std::string &format,
                                                const std::vector<std::string> &args) {
  if (format.empty()) {
    return {};
  }
  return adapter.Format(format, args);
}

std::string ResolveAuctionInboxSubject(lua_State *L,
                                              MailLuaAdapter *adapter,
                                              const ::openwow::game::MailEntry &mail) {
  const auto parsed = ParseAuctionMailSubject(mail.subject);
  if (!parsed.has_value()) {
    return {};
  }

  const char *format_key = GetAuctionMailSubjectFormatKey(parsed->subject_type);
  if (format_key == nullptr ||
      !adapter->HasLocalization(format_key)) {
    return {};
  }

  const auto item_name = ResolveMailItemDisplayName(
      L, adapter, parsed->item_id, parsed->random_property_id,
      BuildInboxAsyncRefreshQueryOptions(L, parsed->item_id));
  if (item_name.empty()) {
    return {};
  }

  return ResolveFormattedInboxSubject(
      *adapter, adapter->Localize(format_key), {item_name});
}

std::string ResolveCalendarInboxSubject(
    MailLuaAdapter& adapter, const ::openwow::game::MailEntry &mail) {
  auto remaining = std::string_view(mail.subject);
  (void)ConsumeColonToken(&remaining);

  const char *format_key = (mail.checked & ::openwow::game::kMailCheckedCalendarInviteRemoved) != 0
                               ? "CALENDAR_INVITE_REMOVED_MAIL_SUBJECT"
                               : "CALENDAR_EVENT_REMOVED_MAIL_SUBJECT";
  if (!adapter.HasLocalization(format_key)) {
    return {};
  }

  std::string calendar_subject(remaining);
  adapter.FilterMatureLanguage(calendar_subject);
  return ResolveFormattedInboxSubject(
      adapter, adapter.Localize(format_key), {calendar_subject});
}

std::string ResolveInboxSubjectText(lua_State *L, MailLuaAdapter *adapter,
                                           const ::openwow::game::MailEntry &mail) {
  std::string resolved_subject;
  bool used_mail_template_subject = false;

  if (const auto *dbc = RequireMailLuaAdapter(L).dbc(); dbc != nullptr && mail.mail_template_id != 0) {
    if (const auto *mail_template = dbc->mail_template().LookupEntry(mail.mail_template_id);
        mail_template != nullptr) {
      resolved_subject.assign(mail_template->subject.data(), mail_template->subject.size());
      used_mail_template_subject = true;
    }
  }

  if (!used_mail_template_subject) {
    switch (mail.message_type) {
    case ::openwow::game::MailType::kAuction:
      resolved_subject = ResolveAuctionInboxSubject(L, adapter, mail);
      break;
    case ::openwow::game::MailType::kCalendar:
      resolved_subject = ResolveCalendarInboxSubject(*adapter, mail);
      break;
    default:
      resolved_subject = mail.subject;
      break;
    }
  }

  adapter->FilterMatureLanguage(resolved_subject);

  if ((mail.checked & ::openwow::game::kMailCheckedCodPayment) != 0 &&
      adapter->HasLocalization("COD_PAYMENT")) {
    resolved_subject = ResolveFormattedInboxSubject(
        *adapter, adapter->Localize("COD_PAYMENT"), {resolved_subject});
  }

  return resolved_subject;
}

void PushInboxSubject(lua_State *L, MailLuaAdapter *adapter,
                             const ::openwow::game::MailEntry &mail) {
  const auto subject = ResolveInboxSubjectText(L, adapter, mail);
  lua_pushlstring(L, subject.c_str(), subject.size());
}

std::optional<std::string> ResolveInboxBodyText(lua_State *L,
                                                       MailLuaAdapter *adapter,
                                                       const ::openwow::game::MailEntry &mail) {
  if (adapter == nullptr) {
    return std::nullopt;
  }

  if (!mail.body.empty()) {
    switch (mail.message_type) {
    case ::openwow::game::MailType::kAuction:
      return std::nullopt;
    case ::openwow::game::MailType::kNormal: {
      std::string filtered_body = mail.body;
      adapter->FilterMatureLanguage(filtered_body);
      return filtered_body;
    }
    case ::openwow::game::MailType::kCalendar: {
      std::uint32_t packed_time = 0;
      (void)ParseDecimalToken(std::string_view(mail.body), &packed_time);

      const auto date_text = ::openwow::game::CalendarSystem::FormatCalendarDateTime(
          packed_time, adapter->UseLongTimeFormat());
      const char *format_key =
          (mail.checked & ::openwow::game::kMailCheckedCalendarInviteRemoved) != 0
              ? "CALENDAR_INVITE_REMOVED_MAIL_BODY"
              : "CALENDAR_EVENT_REMOVED_MAIL_BODY";
      const auto format = adapter->Localize(format_key);
      if (format.empty()) {
        return std::string();
      }

      const auto sender_name =
          TryResolveMailSenderName(L, adapter, mail.message_type, mail.sender_entry,
                                   mail.sender_guid, mail.stationery)
                                   .value_or(std::string());
      const auto subject = ResolveCalendarInboxDisplaySubject(L, mail);
      return adapter->Format(format, {sender_name, subject, date_text});
    }
    default:
      return ExpandInboxBodyText(*adapter, mail.body);
    }
  }

  if (const auto *dbc = RequireMailLuaAdapter(L).dbc(); dbc != nullptr && mail.mail_template_id != 0) {
    if (const auto *mail_template = dbc->mail_template().LookupEntry(mail.mail_template_id);
        mail_template != nullptr) {
      return ExpandInboxBodyText(*adapter, mail_template->body);
    }
  }

  return std::nullopt;
}

bool CanCreateInboxTextItem(const ::openwow::game::MailEntry &mail) {
  if ((mail.body.empty() && mail.mail_template_id == 0) ||
      (mail.checked & ::openwow::game::kMailCheckedCopied) != 0 ||
      mail.stationery == 61 || mail.message_type == ::openwow::game::MailType::kCalendar) {
    return false;
  }

  return mail.message_type != ::openwow::game::MailType::kAuction || mail.mail_template_id != 0;
}

ParsedInboxInvoiceBody ParseInboxInvoiceBody(std::string_view body) {
  ParsedInboxInvoiceBody parsed{};
  auto remaining = body;

  if (const auto token = ConsumeColonToken(&remaining); token.has_value()) {
    (void)ParseHexGuidToken(*token, &parsed.player_guid);
  }

  for (auto &value : parsed.values) {
    if (const auto token = ConsumeColonToken(&remaining); token.has_value()) {
      (void)ParseDecimalToken(*token, &value);
    }
  }

  return parsed;
}

std::pair<std::uint32_t, std::uint32_t> DecodePackedHourMinute(std::uint32_t packed_time) {
  return {
      (packed_time >> 6) & 0x1Fu,
      packed_time & 0x3Fu,
  };
}

const char *GetAuctionInvoiceLuaType(AuctionInvoiceType invoice_type) {
  switch (invoice_type) {
  case AuctionInvoiceType::kSeller:
    return "seller";
  case AuctionInvoiceType::kSellerTempInvoice:
    return "seller_temp_invoice";
  case AuctionInvoiceType::kBuyer:
  default:
    return "buyer";
  }
}

std::string ResolveInvoicePlayerName(
    lua_State* L, MailLuaAdapter *adapter, std::uint64_t guid) {
  if (adapter == nullptr || guid == 0) {
    return {};
  }

  if (const auto *player_name = adapter->queries().GetOrRequestPlayerName(
          guid, BuildInboxAsyncRefreshQueryOptions(L, 0u));
      player_name != nullptr) {
    return player_name->name;
  }

  return {};
}

int PushInboxInvoiceFallback(lua_State *L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  return 7;
}

}
