#include "openwow/game/readable_text.h"

#include "openwow/game/chat_display.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <array>
#include <string>

namespace openwow::game {

namespace {

constexpr std::size_t kReadableTextBufferLimit = 8000;

const ItemTemplate* ResolveActiveReadableItemTemplate(
    const WorldSession& session) {
  const auto item_guid =
      session.item_interactions().readable().has_value()
          ? session.item_interactions().readable()->item
          : ObjectGuid{};
  if (!item_guid) {
    return nullptr;
  }

  const auto* item = session.objects().GetItem(item_guid);
  if (item == nullptr) {
    return nullptr;
  }

  const auto item_entry = item->GetEntry();
  if (item_entry == 0) {
    return nullptr;
  }

  return session.query_cache().GetItemTemplate(item_entry);
}

const std::string* ResolveActiveReadableItemResponse(
    const WorldSession& session) {
  if (!session.item_interactions().readable().has_value()) {
    return nullptr;
  }
  return session.item_interactions().cached_text(
      session.item_interactions().readable()->item);
}

const CGGameObject_C* ResolveActiveReadableGameObject(
    const WorldSession& session) {
  const auto guid = session.item_interactions().readable().has_value()
                        ? session.item_interactions().readable()->item
                        : ObjectGuid{};
  if (!guid) {
    return nullptr;
  }

  return session.objects().GetGameObject(guid);
}

std::uint32_t ResolveReadableGameObjectPageTextId(
    const CGGameObject_C& game_object) {
  return game_object.GetReadablePageTextId();
}

std::uint32_t ResolveActiveReadablePageTextId(const WorldSession& session) {
  if (const auto* item_template = ResolveActiveReadableItemTemplate(session);
      item_template != nullptr) {
    return item_template->page_text;
  }

  if (const auto* game_object = ResolveActiveReadableGameObject(session);
      game_object != nullptr) {
    return ResolveReadableGameObjectPageTextId(*game_object);
  }

  return 0;
}

void EnsureReadablePageTextQuery(WorldSession& session, const std::uint32_t page_id) {
  if (page_id == 0 || session.misc().FindCachedPageText(page_id) != nullptr ||
      session.misc().IsPageTextQueryPending(page_id)) {
    return;
  }

  session.misc().MarkPageTextQueryPending(page_id);
  session.interaction().SendPageTextQuery(page_id);
}

void EnsureReadableItemTextQuery(WorldSession& session, const std::uint64_t item_guid) {
  if (!session.item_interactions().begin_text_query(ObjectGuid(item_guid))) {
    return;
  }

  session.interaction().SendItemTextQuery(item_guid);
}

std::string ExpandReadableText(const WorldSession& session,
                               const char* raw_text,
                               const bool apply_mature_filter) {
  if (raw_text == nullptr) {
    return {};
  }

  std::array<char, kReadableTextBufferLimit> expanded{};
  BindSpellTextFormatterDbcLoader(session.GetDbcLoader());
  BindSpellTextFormatterWorldSession(&session);
  SpellTextFormatter::ExpandObjectTextVariables(
      raw_text, expanded.data(), static_cast<std::uint32_t>(expanded.size()),
      session.objects().GetActivePlayerGuid().GetRawValue(), nullptr, 0);

  std::string text = expanded[0] != '\0' ? std::string(expanded.data())
                                         : std::string(raw_text);
  if (apply_mature_filter) {
    BindChatDisplayDbcLoader(session.GetDbcLoader());
    ChatFrame_MatureLanguageFilter(text, false);
  }
  return text;
}

std::uint32_t ResolveReadableLanguageId(const WorldSession& session) {
  if (const auto* item_template = ResolveActiveReadableItemTemplate(session);
      item_template != nullptr) {
    return item_template->language_id;
  }

  if (const auto* game_object = ResolveActiveReadableGameObject(session);
      game_object != nullptr) {
    return game_object->GetReadableLanguageId();
  }

  return 0;
}

std::uint32_t ResolveReadableLanguageComprehensionValue(
    const WorldSession& session, const std::uint32_t language_id,
    const bool include_active_player_context) {
  if (!include_active_player_context || language_id == 0) {
    return 0;
  }

  const auto* dbc = session.GetDbcLoader();
  const auto* active_player = session.objects().GetActivePlayer();
  if (dbc == nullptr || active_player == nullptr) {
    return 0;
  }

  return GetChatLanguageComprehensionValue(*active_player, *dbc, language_id);
}

bool UsesDirectItemText(const WorldSession& session) {
  const auto* item_template = ResolveActiveReadableItemTemplate(session);
  return item_template != nullptr && item_template->page_text == 0;
}

bool HasCurrentReadablePageData(const WorldSession& session) {
  if (!session.item_interactions().readable().has_value()) {
    return false;
  }
  const auto* item_template = ResolveActiveReadableItemTemplate(session);
  const auto* game_object = ResolveActiveReadableGameObject(session);
  if (item_template == nullptr && game_object == nullptr) {
    return false;
  }

  if (item_template != nullptr && item_template->page_text == 0) {
    return ResolveActiveReadableItemResponse(session) != nullptr;
  }

  const auto first_page_id = ResolveActiveReadablePageTextId(session);
  if (first_page_id == 0) {
    return false;
  }

  const auto& item_mod = *session.item_interactions().readable();
  const auto current_page_id =
      (item_mod.pages.empty() ? 0 : item_mod.pages[0]) != first_page_id
          ? first_page_id
          : (item_mod.page < item_mod.pages.size() ? item_mod.pages[item_mod.page] : 0);
  if (current_page_id == 0) {
    return false;
  }

  return session.misc().FindCachedPageText(current_page_id) != nullptr;
}

void EnsureReadableTextOpened(WorldSession& session) {
  auto& item_mod = session.item_interactions().readable();
  if (!item_mod.has_value() || item_mod->opened) {
    return;
  }

  auto interaction_guid = item_mod->item.GetRawValue();
  if (interaction_guid == 0) {
    return;
  }

  ui::game::SetNpcInteractionTarget(ObjectGuid(interaction_guid));
  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::ITEM_TEXT_BEGIN);
  item_mod->opened = true;
}

}

void CloseReadableObjectInteraction(WorldSession& session) {
  session.item_interactions().close_readable();
  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::ITEM_TEXT_CLOSED);
}

bool ReadableTextHasNextPage(const WorldSession& session) {
  if (UsesDirectItemText(session)) {
    return false;
  }

  const auto& readable = session.item_interactions().readable();
  return readable.has_value() && readable->page + 1 < readable->pages.size() &&
         readable->pages[readable->page + 1] != 0;
}

std::uint32_t GetReadableGameObjectFirstPageId(const WorldSession& session,
                                               const std::uint64_t guid) {
  const auto* game_object = session.objects().GetGameObject(ObjectGuid(guid));
  if (game_object == nullptr) {
    return 0;
  }

  return ResolveReadableGameObjectPageTextId(*game_object);
}

std::uint64_t GetActiveReadableInteractionGuid(const WorldSession& session) {
  const auto guid = session.item_interactions().readable().has_value()
                        ? session.item_interactions().readable()->item
                        : ObjectGuid{};
  if (!guid) {
    return 0;
  }

  if (session.objects().GetUnit(guid) != nullptr ||
      session.objects().GetGameObject(guid) != nullptr) {
    return guid.GetRawValue();
  }

  return 0;
}

bool ToggleOrBeginReadableObjectInteraction(WorldSession& session,
                                            const std::uint64_t guid) {
  if (guid == 0) {
    return false;
  }

  auto& item_mod = session.item_interactions();
  const auto active_guid = item_mod.readable().has_value()
                               ? item_mod.readable()->item.GetRawValue()
                               : 0;

  if (active_guid == guid) {
    ui::game::HandleNpcInteractionLoss(
        session, ObjectGuid(guid),
        ui::game::NpcInteractionClosureCause::UnitUnavailable);
    return false;
  }

  if (active_guid != 0) {
    ui::game::HandleNpcInteractionLoss(
        session, ObjectGuid(active_guid),
        ui::game::NpcInteractionClosureCause::UnitUnavailable);
  }

  item_mod.begin_readable(ObjectGuid(guid));
  return true;
}

void HandleReadItemOk(WorldSession& session, const std::uint64_t guid) {
  if (session.objects().GetActivePlayer() == nullptr ||
      !session.item_interactions().readable().has_value() ||
      session.item_interactions().readable()->item.GetRawValue() != guid) {
    return;
  }

  LoadCurrentReadableTextPage(session, true);
}

void HandleReadItemFailed(WorldSession& session, const std::uint64_t guid,
                          const std::uint32_t status,
                          const std::uint32_t translation_delay_ms) {
  if (session.objects().GetActivePlayer() == nullptr ||
      !session.item_interactions().readable().has_value() ||
      session.item_interactions().readable()->item.GetRawValue() != guid) {
    return;
  }

  if (status == 0) {
    LoadCurrentReadableTextPage(session, true);
    return;
  }

  if (status == 1) {
    LoadCurrentReadableTextPage(session, false);
    ui::game::ScriptEventDispatch::Get().FireEventArgs(
        ui::game::events::ITEM_TEXT_TRANSLATION,
        {static_cast<double>(translation_delay_ms) * 0.001});
    return;
  }

  CloseReadableObjectInteraction(session);
}

void ReloadReadableObjectAfterAsyncDependency(WorldSession& session,
                                              const std::uint64_t owner_guid) {
  if (owner_guid == 0 ||
      !session.item_interactions().readable().has_value() ||
      session.item_interactions().readable()->item.GetRawValue() != owner_guid) {
    return;
  }

  if (!HasCurrentReadablePageData(session)) {
    LoadCurrentReadableTextPage(session, true);
    return;
  }

  if (!session.item_interactions().readable()->opened) {
    LoadCurrentReadableTextPage(session, true);
    return;
  }

  auto interaction_guid = owner_guid;
  ui::game::SetNpcInteractionTarget(ObjectGuid(interaction_guid));
  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::ITEM_TEXT_BEGIN);
  LoadCurrentReadableTextPage(session, true);
}

void LoadCurrentReadableTextPage(WorldSession& session,
                                 const bool include_active_player_context) {
  const auto* item_template = ResolveActiveReadableItemTemplate(session);
  const auto* game_object = ResolveActiveReadableGameObject(session);
  if (item_template == nullptr && game_object == nullptr) {
    return;
  }

  auto& item_mod = *session.item_interactions().readable();

  if (item_template != nullptr && item_template->page_text == 0) {
    const auto* response = ResolveActiveReadableItemResponse(session);
    if (response == nullptr) {
      EnsureReadableItemTextQuery(session, item_mod.item.GetRawValue());
      return;
    }

    item_mod.text = ExpandReadableText(session, response->c_str(), true);
    EnsureReadableTextOpened(session);
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::ITEM_TEXT_READY);
    return;
  }

  const auto first_page_id = ResolveActiveReadablePageTextId(session);
  if (first_page_id == 0) {
    return;
  }

  if (item_mod.pages.empty() || item_mod.pages[0] != first_page_id) {
    item_mod.pages.assign(1, first_page_id);
    item_mod.page = 0;
  }

  const auto page_index = item_mod.page;
  const auto current_page_id =
      page_index < item_mod.pages.size() ? item_mod.pages[page_index] : 0;
  if (current_page_id == 0) {
    return;
  }

  const auto* page = session.misc().FindCachedPageText(current_page_id);
  if (page == nullptr) {
    EnsureReadablePageTextQuery(session, current_page_id);
    return;
  }

  const std::uint32_t language_id = ResolveReadableLanguageId(session);
  const std::uint32_t comprehension_value =
      ResolveReadableLanguageComprehensionValue(
          session, language_id, include_active_player_context);
  if (const auto* dbc = session.GetDbcLoader(); dbc != nullptr) {
    BindChatDisplayDbcLoader(dbc);
  }

  if (item_mod.pages.size() <= page_index + 1) {
    item_mod.pages.resize(page_index + 2);
  }
  item_mod.pages[page_index + 1] = page->next_page;
  item_mod.text = ChatFrame_FormatMessage(
      session.objects(), language_id, comprehension_value,
      ExpandReadableText(session, page->text.c_str(), false),
      {.output_limit = kReadableTextBufferLimit,
       .preserve_angle_bracket_spans = true,
       .preserve_separators = true});
  EnsureReadableTextOpened(session);
  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::ITEM_TEXT_READY);
}

}
