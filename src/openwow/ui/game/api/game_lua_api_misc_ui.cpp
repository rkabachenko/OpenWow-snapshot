
#include "openwow/ui/game/api/game_lua_api_misc_ui.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/lua_mouse_button_context.h"
#include "openwow/ui/game/cursor_texture_resolver.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_base_methods.h"
#include "openwow/ui/game/saved_variables.h"
#include "openwow/ui/game/ui_coordination.h"
#include "openwow/ui/framexml_debug.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/script_locale.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/name_declension_lua.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/aura_lua_bridge.h"
#include "openwow/game/aura_tracker.h"
#include "openwow/game/active_player_environment.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/client_config.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/group_system.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/adapters/ui/item_cursor_pickup_controller.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/minigame_system.h"
#include "openwow/game/money_display.h"
#include "openwow/game/name_declension.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/combat/death/adapters/ui/area_spirit_healer_controller.h"
#include "openwow/game/socket_color_match.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spellbook_catalog.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/update_fields.h"
#include "openwow/net/client_services.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/event_dispatcher.h"
#include "openwow/ui/surfaces/game/runtime/corpse_position_query.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/lua_client_environment.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/game/inventory/equipment/adapters/lua/equipment_lua_api.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/world_map_system.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>
#include <optional>
#include <string_view>

namespace openwow::ui::game::detail {

namespace item_cursor = ::openwow::game::inventory::ui;

namespace {

constexpr std::array<std::uint32_t, 3> kPossessBarAuraTypes{2u, 128u, 6u};

constexpr int kNoAttackTargetSystemMessage = 174;
constexpr int kInvalidAttackTargetSystemMessage = 175;
constexpr int kAttackStunnedSystemMessage = 176;
constexpr int kAttackPacifiedSystemMessage = 177;
constexpr int kAttackMountedSystemMessage = 178;
constexpr int kAttackFleeingSystemMessage = 179;
constexpr int kAttackConfusedSystemMessage = 180;
constexpr int kAttackCharmedSystemMessage = 181;
constexpr int kAttackDeadSystemMessage = 182;
constexpr int kAttackPreventedByMechanicSystemMessage = 183;
constexpr int kAttackChannelSystemMessage = 184;
constexpr int kAttackOutOfRangeSystemMessage = 134;
constexpr int kClientLockedOutSystemMessage = 136;
constexpr std::uint32_t kPossessBarHiddenAuraType = 236u;
constexpr std::uint32_t kSpellEffectSummon = 28u;
constexpr std::uint32_t kPossessBarReleaseSpellId = 693u;
constexpr std::uint32_t kPossessBarSummonPropertiesType = 3u;

void DisplayAttackStartFailure(
    openwow::game::WorldSession& session,
    const openwow::game::AttackStartOutcome& outcome) {
  if (outcome.blocking_mechanic != 0) {
    std::string mechanic_name;
    if (const auto* const dbc = session.GetDbcLoader(); dbc != nullptr) {
      if (const auto* const mechanic =
              dbc->spell_mechanic().LookupEntry(outcome.blocking_mechanic);
          mechanic != nullptr) {
        mechanic_name = mechanic->name;
      }
    }
    DisplaySystemMessage(kAttackPreventedByMechanicSystemMessage, mechanic_name.c_str());
    return;
  }

  switch (outcome.result) {
    case openwow::game::AttackStartResult::kInvalidTarget:
      DisplaySystemMessage(kInvalidAttackTargetSystemMessage);
      break;
    case openwow::game::AttackStartResult::kStunned:
      DisplaySystemMessage(kAttackStunnedSystemMessage);
      break;
    case openwow::game::AttackStartResult::kPacified:
      DisplaySystemMessage(kAttackPacifiedSystemMessage);
      break;
    case openwow::game::AttackStartResult::kMounted:
      DisplaySystemMessage(kAttackMountedSystemMessage);
      break;
    case openwow::game::AttackStartResult::kFleeing:
      DisplaySystemMessage(kAttackFleeingSystemMessage);
      break;
    case openwow::game::AttackStartResult::kConfused:
      DisplaySystemMessage(kAttackConfusedSystemMessage);
      break;
    case openwow::game::AttackStartResult::kCharmed:
      DisplaySystemMessage(kAttackCharmedSystemMessage);
      break;
    case openwow::game::AttackStartResult::kChanneling:
      DisplaySystemMessage(kAttackChannelSystemMessage);
      break;
    case openwow::game::AttackStartResult::kDead:
      DisplaySystemMessage(kAttackDeadSystemMessage);
      break;
    case openwow::game::AttackStartResult::kClientLockedOut:
      DisplaySystemMessage(kClientLockedOutSystemMessage);
      break;
    case openwow::game::AttackStartResult::kRangeRejected:
      DisplaySystemMessage(kAttackOutOfRangeSystemMessage);
      break;
    case openwow::game::AttackStartResult::kStarted:
    case openwow::game::AttackStartResult::kNoAction:
      break;
  }
}

struct PossessBarSelection {
  std::uint32_t spell_id{0};
  bool hidden_by_aura_236{false};
};

[[nodiscard]] bool SpellHasAuraType(const std::array<std::uint32_t, 3> &effect_apply_aura,
                                    const std::uint32_t aura_type) {
  return std::find(effect_apply_aura.begin(), effect_apply_aura.end(), aura_type) !=
         effect_apply_aura.end();
}

[[nodiscard]] std::optional<std::array<std::uint32_t, 3>> ResolveSpellEffectApplyAura(
    const openwow::game::WorldSession &session, const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return std::nullopt;
  }

  if (const auto *dbc = session.GetDbcLoader(); dbc != nullptr) {
    if (const auto *spell = dbc->spell().LookupEntry(spell_id); spell != nullptr) {
      return spell->effect_apply_aura;
    }
  }

  if (const auto query = openwow::game::SpellQueryBridge::Get().Query(spell_id);
      query.has_value()) {
    return query->effectApplyAura;
  }

  return std::nullopt;
}

[[nodiscard]] bool SpellMatchesPossessBarAuraSelection(
    const std::array<std::uint32_t, 3> &effect_apply_aura,
    const bool allow_hidden_aura_selection) {
  for (const auto aura_type : kPossessBarAuraTypes) {
    if (SpellHasAuraType(effect_apply_aura, aura_type)) {
      return true;
    }
  }

  return allow_hidden_aura_selection &&
         SpellHasAuraType(effect_apply_aura, kPossessBarHiddenAuraType);
}

[[nodiscard]] bool SpellMatchesPossessBarSummonSelection(
    const openwow::game::WorldSession &session, const std::uint32_t spell_id) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr || spell_id == 0) {
    return false;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return false;
  }

  for (std::size_t effect_index = 0; effect_index < spell->effect.size(); ++effect_index) {
    if (spell->effect[effect_index] != kSpellEffectSummon) {
      continue;
    }

    const auto summon_properties_id =
        static_cast<std::uint32_t>(spell->effect_misc_value_b[effect_index]);
    if (summon_properties_id == 0) {
      continue;
    }

    const auto *summon_properties =
        dbc->summon_properties().LookupEntry(summon_properties_id);
    if (summon_properties != nullptr &&
        summon_properties->type == kPossessBarSummonPropertiesType) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] PossessBarSelection ResolvePossessBarSelection(
    const openwow::game::WorldSession &session) {
  const auto *local_player = session.objects().GetLocalPlayerTyped();
  if (local_player == nullptr) {
    return {};
  }

  const bool allow_hidden_aura_selection =
      !local_player->GetActiveControlGuid().IsEmpty() &&
      local_player->GetActiveControlGuid() == local_player->State().GetPrimaryControlledUnitGUID();

  PossessBarSelection selection;
  openwow::game::AuraTracker::Get().ForEachAura(
      local_player->GetGuid(),
      [&](const std::uint8_t, const openwow::game::AuraData &aura) {
        if (selection.spell_id != 0 || aura.spell_id == 0 ||
            !aura.IsHelpfulByOriginalFlags()) {
          return;
        }

        const auto effect_apply_aura =
            ResolveSpellEffectApplyAura(session, aura.spell_id);
        if (!effect_apply_aura.has_value()) {
          return;
        }

        if (!SpellMatchesPossessBarAuraSelection(*effect_apply_aura,
                                                 allow_hidden_aura_selection) &&
            !SpellMatchesPossessBarSummonSelection(session, aura.spell_id)) {
          return;
        }

        selection.spell_id = aura.spell_id;
        selection.hidden_by_aura_236 =
            SpellHasAuraType(*effect_apply_aura, kPossessBarHiddenAuraType);
      });

  return selection;
}

void PushNilPossessInfo(lua_State *L) {
  FrameScript_PushNil(L);
  FrameScript_PushNil(L);
  FrameScript_PushNil(L);
}

[[nodiscard]] std::string ResolvePossessBarSpellName(
    const openwow::game::WorldSession &session, const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return {};
  }

  if (const auto query = openwow::game::SpellQueryBridge::Get().Query(spell_id);
      query.has_value() && !query->name.empty()) {
    return query->name;
  }

  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return {};
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr || std::string_view(spell->spell_name).empty()) {
    return {};
  }

  return std::string(spell->spell_name);
}

[[nodiscard]] std::string ResolvePossessBarSpellTexturePath(lua_State *L,
                                                            const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return {};
  }

  if (const auto query = openwow::game::SpellQueryBridge::Get().Query(spell_id);
      query.has_value() && query->iconId != 0) {
    if (auto texture =
            cursor_texture::ResolveSpellIconPath(L, spell_id, query->iconId);
        !texture.empty()) {
      return texture;
    }
  }

  return cursor_texture::ResolveSpellIconPath(L, spell_id);
}

[[nodiscard]] bool ResolvePossessBarReleaseUsable(const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return true;
  }

  if (const auto query = openwow::game::SpellQueryBridge::Get().Query(spell_id);
      query.has_value()) {

    return (query->attributes & openwow::game::kSpellAttr0HiddenClientside) == 0;
  }

  return true;
}

}

static constexpr const char *kGameUiMgrKey = "openwow.world_ui_runtime_context";

static EventDispatcher *GetEvents(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kGameUiMgrKey);
  auto *mgr = static_cast<runtime::WorldUiRuntimeContext *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return mgr ? &mgr->frame_events().dispatcher() : nullptr;
}

static runtime::WorldUiRuntimeContext *GetGameUiManager(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kGameUiMgrKey);
  auto *mgr = static_cast<runtime::WorldUiRuntimeContext *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return mgr;
}

static openwow::game::BindingProfiles *GetBindingProfiles(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.key_binding_manager");
  auto *mgr = static_cast<openwow::game::BindingProfiles *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return mgr;
}

static const openwow::data::dbc::DbcLoader *GetDbcLoaderLocal(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto *dbc = static_cast<const openwow::data::dbc::DbcLoader *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

static int ReadLuaNumberAsWrappedFtol2SseMinusOne(lua_State *L, int argument_index) {
  const auto raw_value =
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, argument_index)));
  return openwow::ui::SignedI32FromU32Bits(raw_value - 1u);
}

constexpr int kRetailSocketDraftSlotCount = 3;

static bool PushNamedClickFrame(lua_State *L, const std::string_view frame_name) {
  return PushNamedFrameLikeObject(L, frame_name);
}

static std::int32_t ToScriptCoinAmount(lua_Number amount) {
  const double truncated = std::trunc(static_cast<double>(amount));
  if (!std::isfinite(truncated))
    return 0;

  constexpr double kMinInt64 = static_cast<double>(std::numeric_limits<std::int64_t>::min());
  constexpr double kMaxInt64 = static_cast<double>(std::numeric_limits<std::int64_t>::max());
  if (truncated < kMinInt64 || truncated > kMaxInt64)
    return 0;

  const auto wide = static_cast<std::int64_t>(truncated);
  const auto low = static_cast<std::uint32_t>(static_cast<std::uint64_t>(wide));
  if (low >= 0x80000000u) {
    return static_cast<std::int32_t>(-static_cast<std::int64_t>(0x100000000ull - low));
  }
  return static_cast<std::int32_t>(low);
}

constexpr std::uint32_t kPlayerFlagsHideHelm = 0x400u;
constexpr std::uint32_t kPlayerFlagsHideCloak = 0x800u;
constexpr std::int32_t kWintergraspWaitTimeEnabledWorldState = 3801;
constexpr std::int32_t kWintergraspQueueEnabledWorldState = 4375;

void PushLocalPlayerShownFlag(lua_State *L, std::uint32_t hidden_flag) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    return;
  }

  const auto *player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    lua_pushnil(L);
    return;
  }

  lua_pushwowbool(L, (player->GetPlayerFlags() & hidden_flag) == 0);
}

namespace {

std::uint32_t ScriptMouseButtonIndexToBitmask(int button_index) {
  return openwow::ui::widgets::MouseButtonScriptOrdinalToFlag(button_index);
}

std::uint32_t GetLiveScriptMouseButtonMask(lua_State *L) {
  if (const auto override_mask = GetCurrentMouseButtonMaskOverride(L);
      override_mask.has_value()) {
    return *override_mask;
  }

  return GetCurrentScriptMouseButtonMask();
}

struct ParsedItemPayload {
  std::uint32_t item_id = 0;
  std::uint32_t enchant_id = 0;
  std::array<std::uint32_t, 3> gem_enchant_ids{};
};

struct CachedItemTemplate {
  std::string name;
  std::uint32_t quality = 1;
};

std::optional<ParsedItemPayload> ParseItemPayload(std::string_view raw_text) {
  const auto parsed_link = ::openwow::game::ItemLinkParser::Parse(std::string(raw_text));
  if (!parsed_link.has_value()) {
    return std::nullopt;
  }

  ParsedItemPayload parsed{};
  parsed.item_id = parsed_link->itemId;
  parsed.enchant_id = parsed_link->enchantId;
  parsed.gem_enchant_ids = parsed_link->gemIds;
  return parsed;
}

std::optional<CachedItemTemplate> LookupCachedItemTemplate(
    lua_State* L, std::uint32_t item_id) {
  if (item_id == 0) {
    return std::nullopt;
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return std::nullopt;
  }
  const auto *item =
      session->query_cache().GetOrRequestItemTemplate(item_id);
  if (item == nullptr) {
    return std::nullopt;
  }

  CachedItemTemplate info{};
  info.name = item->name;
  info.quality = static_cast<std::uint32_t>(item->quality);
  return info;
}

std::string ResolveInventoryDisplayName(lua_State *L, std::string_view base_name,
                                        std::int32_t random_property_id) {
  return ::openwow::game::FormatItemDisplayNameWithRandomProperty(
      ::openwow::game::Localization::Get(), GetDbcLoaderLocal(L), base_name,
      random_property_id);
}

std::optional<::openwow::game::ItemInstance>
FindCarriedItemByDisplayName(lua_State *L, std::string_view requested_name) {
  if (requested_name.empty()) {
    return std::nullopt;
  }

  const auto matches_name = [L, requested_name](const ::openwow::game::ItemInstance *item)
      -> std::optional<::openwow::game::ItemInstance> {
    if (item == nullptr || item->IsEmpty()) {
      return std::nullopt;
    }

    const auto cached_item = LookupCachedItemTemplate(L, item->entry);
    if (!cached_item.has_value()) {
      return std::nullopt;
    }

    const auto display_name =
        ResolveInventoryDisplayName(L, cached_item->name, item->random_property);
    if (display_name != requested_name) {
      return std::nullopt;
    }

    return *item;
  };

  auto &inventory = RequirePlayerInventoryReplica(L);
  for (std::uint8_t slot = 0; slot < ::openwow::game::PlayerInventoryReplica::kMaxEquipSlots; ++slot) {
    if (const auto item = matches_name(inventory.GetEquipSlot(slot)); item.has_value()) {
      return item;
    }
  }

  for (std::uint8_t slot = 0; slot < ::openwow::game::PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (const auto item = matches_name(inventory.GetBackpackSlot(slot)); item.has_value()) {
      return item;
    }
  }

  for (std::uint8_t bag = 1; bag <= ::openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto slot_count = inventory.GetContainerNumSlots(bag);
    for (std::uint8_t slot = 0; slot < static_cast<std::uint8_t>(slot_count); ++slot) {
      if (const auto item = matches_name(inventory.GetBagSlot(bag, slot)); item.has_value()) {
        return item;
      }
    }
  }

  return std::nullopt;
}

std::uint32_t ResolveSocketGemItemId(lua_State *L, std::uint32_t socket_enchant_id) {
  return ResolveSpellItemEnchantmentGemId(L, socket_enchant_id);
}

std::uint32_t GetActivePlayerLevelOrZero(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto *player = session->objects().GetActivePlayer();
  return player != nullptr ? player->State().GetLevel() : 0;
}

std::string BuildItemGemLink(const CachedItemTemplate &item, std::uint32_t item_id,
                             std::uint32_t link_level) {

  constexpr std::uint32_t kRetailItemQualityCount = 8;
  constexpr std::uint32_t kRetailFallbackItemQuality = 1;
  const auto retail_quality = item.quality < kRetailItemQualityCount
                                  ? item.quality
                                  : kRetailFallbackItemQuality;
  const auto color_info = ::openwow::game::ItemTemplate::GetQualityColorInfo(retail_quality);
  return std::string(color_info.hyperlink_color) + "|Hitem:" + std::to_string(item_id) +
         ":0:0:0:0:0:0:0:" + std::to_string(link_level) + "|h[" + item.name + "]|h|r";
}

std::string BuildSocketItemLinkFromObject(lua_State *L,
                                          const ::openwow::game::ItemInstance &item,
                                          std::string_view base_name,
                                          std::uint32_t quality) {
  ::openwow::game::ItemLinkData link_data;
  link_data.itemId = item.entry;
  link_data.enchantId = item.GetPermanentEnchant();
  link_data.gemIds[0] = item.GetSocketEnchant(0);
  link_data.gemIds[1] = item.GetSocketEnchant(1);
  link_data.gemIds[2] = item.GetSocketEnchant(2);
  link_data.randomPropertyId = item.random_property;
  link_data.suffixFactor = static_cast<std::int32_t>(item.random_suffix);
  link_data.linkLevel = GetActivePlayerLevelOrZero(L);
  link_data.quality = quality < 8 ? static_cast<std::uint8_t>(quality) : 1;
  if (!base_name.empty()) {
    link_data.name = ResolveInventoryDisplayName(L, base_name, item.random_property);
  }
  return ::openwow::game::ItemLinkParser::Generate(link_data);
}

struct SocketItemTemplateView {
  std::uint32_t entry = 0;
  std::string name;
  std::uint32_t display_id = 0;
  std::uint32_t quality = 0;
  std::uint32_t gem_properties = 0;
  std::array<std::uint32_t, 3> socket_colors{};
};

void FireSocketInfoUpdateOnResolvedItemTemplate(bool success) {
  if (!success) {
    return;
  }

  openwow::ui::game::ScriptEventDispatch::Get().FireEvent("SOCKET_INFO_UPDATE");
}

openwow::game::AsyncQueryChannel::CallbackKey BuildSocketInfoUpdateCallbackKey(
    std::uint32_t item_entry) {
  return openwow::game::AsyncQueryChannel::CallbackKey(
      reinterpret_cast<std::uintptr_t>(&FireSocketInfoUpdateOnResolvedItemTemplate), item_entry);
}

std::optional<SocketItemTemplateView> ResolveSocketItemTemplateView(
    lua_State *L, std::uint32_t item_entry, bool request_if_missing,
    const openwow::game::QueryCache::QueryRequestOptions *request_options = nullptr) {
  if (item_entry == 0) {
    return std::nullopt;
  }

  const auto copy_template =
      [](const ::openwow::game::ItemTemplate &item) -> SocketItemTemplateView {
    SocketItemTemplateView view{};
    view.entry = item.entry;
    view.name = item.name;
    view.display_id = item.display_id;
    view.quality = static_cast<std::uint32_t>(item.quality);
    view.gem_properties = item.gem_properties;
    for (std::size_t index = 0; index < view.socket_colors.size(); ++index) {
      view.socket_colors[index] = item.sockets[index].color;
    }
    return view;
  };

  if (auto *session = GetWorldSession(L); session != nullptr) {
    if (const auto *item_template = session->query_cache().GetItemTemplate(item_entry);
        item_template != nullptr) {
      return copy_template(*item_template);
    }

    if (request_if_missing) {
      if (const auto *item_template =
              request_options != nullptr
                  ? session->query_cache().GetOrRequestItemTemplate(item_entry, *request_options)
                  : session->query_cache().GetOrRequestItemTemplate(item_entry);
          item_template != nullptr) {
        return copy_template(*item_template);
      }
    }
  }

  return std::nullopt;
}

const ::openwow::game::ItemInstance *FindInventoryItemByGuid(
    lua_State* L, std::uint64_t item_guid) {
  if (item_guid == 0) {
    return nullptr;
  }

  auto &inventory = RequirePlayerInventoryReplica(L);
  for (std::uint8_t slot = 0; slot < ::openwow::game::PlayerInventoryReplica::kMaxEquipSlots;
       ++slot) {
    if (const auto *item = inventory.GetEquipSlot(slot);
        item != nullptr && item->guid == item_guid) {
      return item;
    }
  }

  for (std::uint8_t slot = 0; slot < ::openwow::game::PlayerInventoryReplica::kBackpackSize;
       ++slot) {
    if (const auto *item = inventory.GetBackpackSlot(slot);
        item != nullptr && item->guid == item_guid) {
      return item;
    }
  }

  for (std::uint8_t bag = 1; bag <= ::openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto slot_count = inventory.GetContainerNumSlots(bag);
    for (std::uint8_t slot = 0; slot < static_cast<std::uint8_t>(slot_count); ++slot) {
      if (const auto *item = inventory.GetBagSlot(bag, slot);
          item != nullptr && item->guid == item_guid) {
        return item;
      }
    }
  }

  if (const auto* session = GetWorldSession(L);
      session != nullptr && session->held_cursor() != nullptr) {
    const auto* item = session->held_cursor()->live_item();
    if (item != nullptr && item->item.guid == item_guid) {
      return &item->item;
    }
  }

  return nullptr;
}

std::array<std::uint8_t, 3> BuildSocketMasks(
    const SocketItemTemplateView &item_template) {
  std::array<std::uint8_t, 3> socket_masks{};
  for (std::size_t index = 0; index < socket_masks.size(); ++index) {
    socket_masks[index] = static_cast<std::uint8_t>(item_template.socket_colors[index]);
  }
  return socket_masks;
}

std::uint8_t CountSocketMaskEntries(const std::array<std::uint8_t, 3> &socket_masks) {
  return static_cast<std::uint8_t>(std::count_if(
      socket_masks.begin(), socket_masks.end(),
      [](const std::uint8_t socket_mask) { return socket_mask != 0; }));
}

std::uint32_t GetSocketUiVisibleCount(
    lua_State *L,
    const ::openwow::game::ItemInstance &item,
    const std::array<std::uint8_t, 3> &socket_masks) {
  const auto base_socket_count = CountSocketMaskEntries(socket_masks);

  constexpr std::uint32_t kQuestItemFlag = 0x2000u;
  if ((item.flags & kQuestItemFlag) != 0u) {
    return base_socket_count;
  }

  constexpr std::uint32_t kPrismaticSocketEnchantEffectType = 8u;
  const auto prismatic_enchant_id =
      item.enchantments[static_cast<std::size_t>(
                            ::openwow::game::EnchantmentSlot::Prismatic)]
          .id;
  const auto *const dbc = GetDbcLoaderLocal(L);
  if (prismatic_enchant_id == 0u || dbc == nullptr) {
    return base_socket_count;
  }

  const auto *const enchantment =
      dbc->spell_item_enchantment().LookupEntry(prismatic_enchant_id);
  if (enchantment == nullptr) {
    return base_socket_count;
  }

  for (std::size_t effect_index = 0; effect_index < enchantment->type.size(); ++effect_index) {
    if (enchantment->type[effect_index] == kPrismaticSocketEnchantEffectType) {
      return static_cast<std::uint32_t>(base_socket_count) +
             static_cast<std::uint32_t>(enchantment->amount[effect_index]);
    }
  }

  return base_socket_count;
}

std::optional<std::uint8_t> GetActiveSocketMask(lua_State* L,
                                                std::uint64_t item_guid,
                                                int index) {
  if (item_guid == 0 || index < 0 || index >= kRetailSocketDraftSlotCount) {
    return std::nullopt;
  }
  const auto* session = GetWorldSession(L);
  if (session == nullptr || !session->item_interactions().socket().has_value() ||
      session->item_interactions().socket()->item.GetRawValue() != item_guid) {
    return std::nullopt;
  }
  return session->item_interactions().socket()->socket_masks[index];
}

std::uint32_t ResolveGemColorMask(lua_State *L, std::uint32_t gem_properties) {
  if (gem_properties == 0) {
    return 0;
  }

  const auto *dbc = GetDbcLoaderLocal(L);
  if (dbc == nullptr) {
    return 0;
  }

  const auto *gem_properties_entry = dbc->gem_properties().LookupEntry(gem_properties);
  return gem_properties_entry != nullptr ? gem_properties_entry->type : 0;
}

struct SocketGemView {
  std::uint32_t item_id = 0;
  std::string name;
  std::string icon_path;
  std::uint32_t quality = 0;
  std::uint32_t color_mask = 0;
};

std::optional<SocketGemView> ResolveSocketGemView(lua_State *L,
                                                  std::uint32_t item_id,
                                                  bool request_if_missing,
                                                  const openwow::game::QueryCache::QueryRequestOptions
                                                      *request_options = nullptr) {
  const auto item_template =
      ResolveSocketItemTemplateView(L, item_id, request_if_missing, request_options);
  if (!item_template.has_value()) {
    return std::nullopt;
  }

  SocketGemView gem{};
  gem.item_id = item_id;
  gem.name = item_template->name;
  gem.icon_path = ResolveItemEntryIconTexturePathOrFallback(L, item_id);
  gem.quality = item_template->quality;
  gem.color_mask = ResolveGemColorMask(L, item_template->gem_properties);
  return gem;
}

std::optional<SocketGemView> ResolveExistingSocketGemView(
    lua_State *L, const ::openwow::game::ItemInstance &item,
    std::uint8_t socket_index, bool request_if_missing,
    bool fire_socket_info_update_on_resolve = false) {
  const auto gem_item_id =
      ResolveSocketGemItemId(L, item.GetSocketEnchant(socket_index));
  if (gem_item_id == 0) {
    return std::nullopt;
  }

  openwow::game::QueryCache::QueryRequestOptions request_options{};
  const auto *request_options_ptr =
      static_cast<const openwow::game::QueryCache::QueryRequestOptions *>(nullptr);
  if (fire_socket_info_update_on_resolve) {
    request_options.callback_key = BuildSocketInfoUpdateCallbackKey(gem_item_id);
    request_options.callback = FireSocketInfoUpdateOnResolvedItemTemplate;
    request_options_ptr = &request_options;
  }

  return ResolveSocketGemView(L, gem_item_id, request_if_missing, request_options_ptr);
}

struct HeldSocketGemBuildResult {
  bool valid = false;
  std::optional<::openwow::game::PendingSocketGem> gem;
};

HeldSocketGemBuildResult BuildHeldSocketGem(
    lua_State* L,
    const ::openwow::game::actions::held_cursor::LiveItem* held_item) {
  if (held_item == nullptr || held_item->item.guid == 0) {
    return HeldSocketGemBuildResult{.valid = true, .gem = std::nullopt};
  }

  const auto *item = FindInventoryItemByGuid(L, held_item->item.guid);
  if (item == nullptr) {
    return {};
  }

  const auto item_template = ResolveSocketItemTemplateView(L, item->entry, true);
  if (!item_template.has_value() || item_template->gem_properties == 0) {
    return {};
  }

  return HeldSocketGemBuildResult{
      .valid = true,
      .gem =
          ::openwow::game::PendingSocketGem{
              .item = ::openwow::game::ObjectGuid(held_item->item.guid),
              .container =
                  ::openwow::game::ObjectGuid(held_item->source_container_guid),
              .source_slot = held_item->source_slot,
              .item_id = item->entry,
              .gem_properties = item_template->gem_properties,
              .color = static_cast<std::uint8_t>(
                  ResolveGemColorMask(L, item_template->gem_properties)),
              .name = item_template->name,
              .icon_path = ResolveItemEntryIconTexturePathOrFallback(L, item->entry),
          }};
}

void FireSocketEvent(lua_State *L, const char *event_name) {
  if (auto *dispatcher = GetEvents(L); dispatcher != nullptr) {
    dispatcher->FireEvent(event_name);
  }
}

struct SocketUiLayout {
  std::array<std::uint8_t, 3> socket_masks{};
  std::uint32_t visible_socket_count = 0;
};

std::optional<SocketUiLayout> ResolveSocketUiLayout(
    lua_State *L, const ::openwow::game::ItemInstance *item) {
  if (item == nullptr || item->guid == 0) {
    return std::nullopt;
  }

  const auto item_template = ResolveSocketItemTemplateView(L, item->entry, true);
  if (!item_template.has_value()) {
    return std::nullopt;
  }

  const auto socket_masks = BuildSocketMasks(*item_template);
  return SocketUiLayout{
      .socket_masks = socket_masks,
      .visible_socket_count = GetSocketUiVisibleCount(L, *item, socket_masks),
  };
}

bool CanOpenSocketUiForItem(lua_State *L,
                            const ::openwow::game::ItemInstance *item) {
  const auto socket_layout = ResolveSocketUiLayout(L, item);
  return socket_layout.has_value() && socket_layout->visible_socket_count != 0;
}

bool OpenSocketUiForItem(lua_State *L,
                         const ::openwow::game::ItemInstance *item) {
  auto* session = GetWorldSession(L);

  if (item == nullptr || item->guid == 0) {
    if (session != nullptr) {
      session->item_interactions().cancel_socket();
    }
    FireSocketEvent(L, "SOCKET_INFO_CLOSE");
    return false;
  }

  const auto socket_layout = ResolveSocketUiLayout(L, item);
  if (session == nullptr || !socket_layout.has_value()) {
    if (session != nullptr) {
      session->item_interactions().cancel_socket();
    }
    FireSocketEvent(L, "SOCKET_INFO_CLOSE");
    return false;
  }

  session->item_interactions().begin_socket(
      ::openwow::game::ObjectGuid(item->guid), socket_layout->socket_masks,
      static_cast<std::uint8_t>(std::min<std::uint32_t>(
          socket_layout->visible_socket_count,
          static_cast<std::uint32_t>(socket_layout->socket_masks.size()))));
  FireSocketEvent(L, "SOCKET_INFO_UPDATE");
  return true;
}

}

int LuaGetCurrencyListSize(lua_State *L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }
  if (const auto *dbc = detail::GetDbcLoaderLocal(L)) {
    ::openwow::game::CurrencySystem::Get().CacheDbcData(*dbc);
  }
  ::openwow::game::CurrencySystem::Get().RebuildCurrencyList(session->objects());
  lua_pushnumber(
      L, static_cast<lua_Number>(::openwow::game::CurrencySystem::Get().GetCurrencyListSize()));
  return 1;
}

int LuaGetCurrencyListInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetCurrencyListInfo(index)");
  }
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    for (int i = 0; i < 9; ++i) {
      lua_pushnil(L);
    }
    return 9;
  }
  const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;

  if (const auto *dbc = detail::GetDbcLoaderLocal(L)) {
    ::openwow::game::CurrencySystem::Get().CacheDbcData(*dbc);
  }
  auto &currency_system = ::openwow::game::CurrencySystem::Get();
  currency_system.RebuildCurrencyList(session->objects());

  const auto *entry = currency_system.GetCurrencyListEntryByIndex(index);
  if (!entry) {
    for (int i = 0; i < 9; ++i) {
      lua_pushnil(L);
    }
    return 9;
  }

  if (!entry->displayName.empty()) {
    lua_pushstring(L, entry->displayName.c_str());
  } else {
    lua_pushnil(L);
  }
  lua_pushboolean(L, entry->isHeader);
  lua_pushboolean(L, entry->isExpanded);
  lua_pushboolean(L, currency_system.IsCurrencyUnused(index));
  lua_pushboolean(L, entry->showInBackpack);

  std::uint32_t count = 0;
  int extra_currency_type = 0;
  if (const auto *player = session->objects().GetLocalPlayer()) {
      if (entry->itemId == 43307) {
        count = player->GetUInt32(PLAYER_FIELD_ARENA_CURRENCY);
        extra_currency_type = 1;
      } else if (entry->itemId == 43308) {
        count = player->GetUInt32(PLAYER_FIELD_HONOR_CURRENCY);
        extra_currency_type = 2;
      } else {
        count = ::openwow::game::CurrencySystem::Get().GetAmount(entry->itemId);
      }
  }
  lua_pushnumber(L, static_cast<lua_Number>(count));
  lua_pushinteger(L, extra_currency_type);

  const auto *item = RequireItemDefinitions(L).GetItem(entry->itemId);
  const auto *dbc = detail::GetDbcLoaderLocal(L);
  if (item && dbc) {
    const auto icon_path =
        ::openwow::game::ResolveItemInventoryIconTexturePath(
            dbc, item->display_id);
    lua_pushstring(L, icon_path.c_str());
  } else {
    lua_pushnil(L);
  }
  lua_pushinteger(L, static_cast<lua_Integer>(entry->itemId));
  return 9;
}

int LuaGetMapLandmarkInfo(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetMapLandmarkInfo(index)");
  }

  const auto index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto *wm = WorldMapStateOrNull(L);
  const auto *lm = wm != nullptr ? wm->GetLandmark(static_cast<std::size_t>(index)) : nullptr;
  if (!lm) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    return 7;
  }
  lua_pushstring(L, lm->name.c_str());
  lua_pushstring(L, lm->description.c_str());
  lua_pushnumber(L, static_cast<lua_Integer>(lm->texture_index));
  lua_pushnumber(L, static_cast<lua_Number>(lm->x));
  lua_pushnumber(L, static_cast<lua_Number>(lm->y));
  if (lm->map_link_id != 0) {
    lua_pushnumber(L, static_cast<lua_Integer>(lm->map_link_id));
  } else {
    lua_pushnil(L);
  }
  lua_pushwowbool(L, lm->show_in_battle_map);
  return 7;
}

int LuaGetNumMapLandmarks(lua_State *L) {
  const auto *wm = WorldMapStateOrNull(L);
  lua_pushnumber(L, static_cast<lua_Integer>(
                        wm != nullptr ? wm->GetNumLandmarks() : 0u));
  return 1;
}

int LuaProcessMapClick(lua_State *L) {
  if (lua_isnumber(L, 1) == 0 || lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "Usage: ProcessMapClick(x, y)");
  }

  const float click_x = static_cast<float>(lua_tonumber(L, 1));
  const float click_y = static_cast<float>(lua_tonumber(L, 2));

  if (auto *wm = WorldMapStateOrNull(L); wm != nullptr) {
    wm->ProcessMapClick(click_x, click_y);
  }

  return 0;
}

int LuaSetMapZoom(lua_State *L) {
  auto *wm = WorldMapStateOrNull(L);

  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: SetMapZoom(continentIndex [,zoneIndex])");
  }

  const int continent_token = ReadLuaNumberAsWrappedFtol2SseMinusOne(L, 1);
  const int continent_count =
      wm != nullptr && wm->IsDbcInitialized()
          ? static_cast<int>(wm->GetContinentCount())
          : -1;
  if (continent_token < -2 ||
      (continent_token > 0 && continent_count >= 0 &&
       static_cast<std::uint32_t>(continent_token) >=
           static_cast<std::uint32_t>(continent_count))) {
    return luaL_error(
        L, "Usage: SetMapZoom(continentIndex [,zoneIndex]) .. requested continent out of bounds");
  }

  int zone_token = -1;
  if (lua_isnumber(L, 2) != 0) {
    zone_token = ReadLuaNumberAsWrappedFtol2SseMinusOne(L, 2);
  }

  if (wm != nullptr) {
    (void)wm->CommitSelectionAndRefreshLandmarks(continent_token, zone_token, -1);
  }
  return 0;
}

int LuaGetLocale(lua_State *L) {
  const std::string locale = ::openwow::ui::ScriptActiveLocaleName();
  lua_pushstring(L, locale.c_str());
  return 1;
}

int LuaGetWintergraspWaitTime(lua_State *L) {
  const auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto &world_states = session->world_states();
  if (world_states.GetWorldState(kWintergraspWaitTimeEnabledWorldState) == 0) {
    return 0;
  }

  std::int32_t remaining_seconds =
      world_states.GetWorldState(openwow::game::WorldStateId::kWGTimeToNextBattle) -
      world_states.world_state_ui_current_time_seconds(std::time(nullptr));
  if (remaining_seconds < 0) {
    remaining_seconds = 0;
  }

  FrameScript_PushNumberFromInt(L, remaining_seconds);
  return 1;
}

int LuaCanQueueForWintergrasp(lua_State *L) {
  const auto *session = GetWorldSession(L);
  if (session == nullptr) {
    FrameScript_PushNil(L);
    return 1;
  }

  if (session->world_states().GetWorldState(kWintergraspQueueEnabledWorldState) != 0) {
    FrameScript_PushNumber(L, 1.0);
  } else {
    FrameScript_PushNil(L);
  }
  return 1;
}

int LuaCanShowResetInstances(lua_State *L) {
  if (openwow::game::CGPlayer_C::CanShowResetInstances(GetWorldSession(L))) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetGuildBankWithdrawMoney(lua_State *L) {
  lua_pushnumber(
      L,
      static_cast<lua_Number>(
          openwow::game::GuildSystem::Get().GetGuildBankMoneyWithdrawRemaining()));
  return 1;
}

int LuaIsXPUserDisabled(lua_State *L) {
  constexpr std::uint32_t kPlayerFlagsNoXpGain = 0x02000000u;
  const auto *session = GetWorldSession(L);
  const auto *player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (player == nullptr) {
    return 0;
  }

  lua_pushboolean(L, (player->GetUInt32(PLAYER_FLAGS) & kPlayerFlagsNoXpGain) != 0);
  return 1;
}

int LuaGetCursorMoney(lua_State *L) {
  std::uint32_t amount = 0;
  if (const auto* session = GetWorldSession(L);
      session != nullptr && session->held_cursor() != nullptr) {
    if (const auto* money =
            session->held_cursor()
                ->get_if<::openwow::game::actions::held_cursor::PlayerMoney>()) {
      amount = money->amount;
    } else if (const auto* guild_bank_money =
                   session->held_cursor()
                       ->get_if<::openwow::game::actions::held_cursor::
                                   GuildBankMoney>()) {
      amount = guild_bank_money->amount;
    }
  }
  lua_pushnumber(L, static_cast<lua_Number>(amount));
  return 1;
}

int LuaGetCurrentKeyBoardFocus(lua_State *L) {
  if (runtime::WorldUiRuntimeContext *const manager = GetGameUiManager(L); manager != nullptr &&
      manager->input_router().PushFocusedFrame(L)) {
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetMouseFocus(lua_State *L) {
  if (runtime::WorldUiRuntimeContext *const manager = GetGameUiManager(L); manager != nullptr &&
      manager->input_router().PushMouseoverFrame(L)) {
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetMouseButtonClicked(lua_State *L) {
  if (const auto mouse_button = lua_adapter::CurrentMouseButtonOverride(L);
      mouse_button.has_value()) {
    lua_pushlstring(L, mouse_button->data(), mouse_button->size());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsMouseButtonDown(lua_State *L) {
  const std::uint32_t live_mask = GetLiveScriptMouseButtonMask(L);
  std::uint32_t query_mask = 0;

  if (lua_isnumber(L, 1)) {
    query_mask = ScriptMouseButtonIndexToBitmask(static_cast<int>(lua_tonumber(L, 1)));
  } else if (lua_isstring(L, 1)) {
    query_mask = openwow::game::MiddleButton_NameToBitmask(lua_tostring(L, 1));
  } else {
    lua_pushwowbool(L, live_mask != 0);
    return 1;
  }

  lua_pushwowbool(L, (live_mask & query_mask) != 0);
  return 1;
}

int LuaGetPVPRankInfo(lua_State *L) {
  static constexpr const char* kUsage = "Usage: GetPVPRankInfo(rank [, unit])";
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "%s", kUsage);
  }

  const auto rank =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  if (rank < 1 || rank > 18) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 2;
  }

  int gender = 0;
  if (lua_isnumber(L, 2) != 0) {
    gender = openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 2));
  } else if (auto *session = GetWorldSession(L); session != nullptr) {
    if (lua_isstring(L, 2) != 0) {
      if (const auto *unit = ResolveUnit(session, UnitIdArg(L, 2));
          unit != nullptr && unit->IsPlayer()) {
        gender = static_cast<int>(GetUnitGender(unit));
      }
    } else if (const auto *player = session->objects().GetActivePlayer(); player != nullptr) {
      gender = static_cast<int>(GetUnitGender(player));
    }
  }

  char token[32];
  std::snprintf(token, sizeof(token), "PVP_RANK_%d_%d", rank, gender);
  const auto rank_name = ::openwow::game::ResolveLocalizedGlobalString(
      L, token, -1, gender);
  if (rank_name.empty()) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 2;
  }

  int rank_number = rank - 5;
  if (rank > 4) {
    rank_number = rank - 4;
  }

  lua_pushstring(L, rank_name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(rank_number));
  return 2;
}

int LuaGetItemGem(lua_State *L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetItemGem(\"name\"|\"itemlink\", index)");
  }

  const auto socket_index = static_cast<int>(lua_tonumber(L, 2)) - 1;
  if (socket_index < 0 || socket_index > 2) {
    return 0;
  }

  const char *raw_text = lua_tostring(L, 1);
  if (raw_text == nullptr || *raw_text == '\0') {
    return 0;
  }

  std::uint32_t socket_enchant_id = 0;
  if (const auto parsed_item = ParseItemPayload(raw_text); parsed_item.has_value()) {
    socket_enchant_id = parsed_item->gem_enchant_ids[static_cast<std::size_t>(socket_index)];
  } else if (const auto matched_item = FindCarriedItemByDisplayName(L, raw_text);
             matched_item.has_value()) {
    socket_enchant_id = matched_item->GetSocketEnchant(static_cast<std::uint8_t>(socket_index));
  } else {
    return 0;
  }

  const auto gem_item_id = ResolveSocketGemItemId(L, socket_enchant_id);
  if (gem_item_id == 0) {
    return 0;
  }

  const auto cached_gem = LookupCachedItemTemplate(L, gem_item_id);
  if (!cached_gem.has_value()) {
    return 0;
  }

  lua_pushstring(L, cached_gem->name.c_str());
  const auto gem_link = BuildItemGemLink(*cached_gem, gem_item_id, GetActivePlayerLevelOrZero(L));
  lua_pushstring(L, gem_link.c_str());
  return 2;
}

int LuaGetSocketItemInfo(lua_State *L) {
  const auto* session = GetWorldSession(L);
  if (session == nullptr || !session->item_interactions().socket().has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);
    return 3;
  }

  const auto item_guid =
      session->item_interactions().socket()->item.GetRawValue();
  const auto *item = FindInventoryItemByGuid(L, item_guid);
  const auto item_template =
      item != nullptr ? ResolveSocketItemTemplateView(L, item->entry, false)
                      : std::nullopt;
  if (item == nullptr || !item_template.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);
    return 3;
  }

  const auto display_name =
      ResolveInventoryDisplayName(L, item_template->name, item->random_property);
  lua_pushstring(L, display_name.c_str());
  const auto icon_path = ResolveItemEntryIconTexturePathOrFallback(L, item->entry);
  lua_pushstring(L, icon_path.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(item_template->quality));
  return 3;
}

int LuaGetExistingSocketInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetExistingSocketInfo(index)");
  }

  const int index = ReadLuaNumberAsWrappedFtol2SseMinusOne(L, 1);

  const auto* session = GetWorldSession(L);
  if (session == nullptr || !session->item_interactions().socket().has_value() ||
      index < 0 || index >= 3) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const auto item_guid =
      session->item_interactions().socket()->item.GetRawValue();
  const auto *item = FindInventoryItemByGuid(L, item_guid);
  const auto gem =
      item != nullptr ? ResolveExistingSocketGemView(
                            L, *item, static_cast<std::uint8_t>(index), true, true)
                      : std::nullopt;
  const auto socket_mask = GetActiveSocketMask(L, item_guid, index);
  if (item == nullptr || !gem.has_value() || !socket_mask.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  lua_pushstring(L, gem->name.c_str());
  lua_pushstring(L, gem->icon_path.c_str());

  if (::openwow::game::SocketMaskMatchesGemColorMask(
          *socket_mask,
          gem->color_mask)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  return 3;
}

int LuaGetExistingSocketLink(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetExistingSocketLink(index)");
  }

  const int index = ReadLuaNumberAsWrappedFtol2SseMinusOne(L, 1);

  const auto* session = GetWorldSession(L);
  if (session == nullptr || !session->item_interactions().socket().has_value() ||
      index < 0 || index >= 3) {
    return 0;
  }

  const auto *item = FindInventoryItemByGuid(
      L, session->item_interactions().socket()->item.GetRawValue());
  const auto gem =
      item != nullptr ? ResolveExistingSocketGemView(
                            L, *item, static_cast<std::uint8_t>(index), true, true)
                      : std::nullopt;
  if (item == nullptr || !gem.has_value()) {
    return 0;
  }

  const CachedItemTemplate gem_item{
      .name = gem->name,
      .quality = gem->quality,
  };
  const auto link =
      BuildItemGemLink(gem_item, gem->item_id, GetActivePlayerLevelOrZero(L));
  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetNewSocketInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetNewSocketInfo(index)");
  }

  const int index = ReadLuaNumberAsWrappedFtol2SseMinusOne(L, 1);
  const auto* session = GetWorldSession(L);
  if (session == nullptr || !session->item_interactions().socket().has_value() ||
      index < 0 || index >= 3) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const auto item_guid =
      session->item_interactions().socket()->item.GetRawValue();
  const auto& pending_gem =
      session->item_interactions().socket()->pending_gems[index];
  const auto *item = FindInventoryItemByGuid(L, item_guid);
  const auto socket_mask = GetActiveSocketMask(L, item_guid, index);
  if (!pending_gem.has_value() || item == nullptr || !socket_mask.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const auto *gem_item =
      FindInventoryItemByGuid(L, pending_gem->item.GetRawValue());
  const auto gem_template =
      gem_item != nullptr ? ResolveSocketItemTemplateView(L, gem_item->entry, true) : std::nullopt;
  if (gem_item == nullptr || !gem_template.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const auto gem_name =
      ResolveInventoryDisplayName(L, gem_template->name, gem_item->random_property);
  lua_pushstring(L, gem_name.c_str());
  const auto icon_path = ResolveItemEntryIconTexturePathOrFallback(L, gem_item->entry);
  lua_pushstring(L, icon_path.c_str());
  if (::openwow::game::SocketMaskMatchesGemColorMask(
          *socket_mask,
          ResolveGemColorMask(L, gem_template->gem_properties))) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 3;
}

int LuaGetNewSocketLink(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetNewSocketLink(index)");
  }

  const int index = ReadLuaNumberAsWrappedFtol2SseMinusOne(L, 1);
  const auto* session = GetWorldSession(L);
  if (session == nullptr || !session->item_interactions().socket().has_value() ||
      index < 0 || index >= 3) {
    return 0;
  }

  const auto& pending_gem =
      session->item_interactions().socket()->pending_gems[index];
  if (!pending_gem.has_value() || pending_gem->item.IsEmpty()) {
    return 0;
  }

  const auto *gem_item =
      FindInventoryItemByGuid(L, pending_gem->item.GetRawValue());
  if (gem_item == nullptr) {
    return 0;
  }

  const auto gem_template = ResolveSocketItemTemplateView(L, gem_item->entry, true);
  const auto quality = gem_template.has_value() ? gem_template->quality : 1u;
  const std::string_view base_name =
      gem_template.has_value() && !gem_template->name.empty() ? std::string_view(gem_template->name)
                                                              : std::string_view();
  const auto link = BuildSocketItemLinkFromObject(L, *gem_item, base_name, quality);
  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetNumSockets(lua_State *L) {
  const auto* session = GetWorldSession(L);
  std::uint32_t socket_count = 0;
  if (session != nullptr && session->item_interactions().socket().has_value()) {
    const auto item_guid =
        session->item_interactions().socket()->item.GetRawValue();
    if (const auto* const item = FindInventoryItemByGuid(L, item_guid);
        item != nullptr) {
      if (const auto socket_layout = ResolveSocketUiLayout(L, item);
          socket_layout.has_value()) {

        socket_count = socket_layout->visible_socket_count;
      }
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(socket_count));
  return 1;
}

int LuaClickSocketButton(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: ClickSocketButton(index)");
  }

  const auto socket_index = ReadLuaNumberAsWrappedFtol2SseMinusOne(L, 1);
  if (socket_index < 0 || socket_index >= kRetailSocketDraftSlotCount) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr || session->held_cursor() == nullptr) {
    return 0;
  }
  auto& cursor = *session->held_cursor();
  const auto held_gem = BuildHeldSocketGem(L, cursor.live_item());
  if (!held_gem.valid) {
    return 0;
  }

  if (!session->item_interactions().socket().has_value() ||
      socket_index >= session->item_interactions().socket()->socket_count) {
    return 0;
  }
  const auto displaced_gem = session->item_interactions().place_socket_gem(
      static_cast<std::uint8_t>(socket_index), held_gem.gem);

  FireSocketEvent(L, "SOCKET_INFO_UPDATE");
  cursor.Clear({
      .release_source_lease = false,
      .publish_money_owner_update = true,
  });

  if (displaced_gem.has_value() && !displaced_gem->item.IsEmpty()) {
    (void)item_cursor::PickupItemCursor(
        cursor, session->inventory_replica(), session->item_definitions(),
        session->GetDbcLoader(), session->objects().GetActivePlayerGuid(),
        displaced_gem->item,
        {
            .source =
                {
                    .container =
                        displaced_gem->container,
                    .slot = static_cast<std::int32_t>(displaced_gem->source_slot),
                },
            .source_policy = item_cursor::ItemCursorSourcePolicy::kProvidedOrigin,
        });
  }

  return 0;
}

int LuaSocketInventoryItem(lua_State *L) {
  int slot = 0;
  if (!ResolveInventorySlotArgument(L, 1, &slot) || slot == -1) {
    return 0;
  }

  const auto *item = GetLocalInventoryItemInstanceByAbsoluteSlot(L, slot);
  const auto* session = GetWorldSession(L);
  const auto* live_item =
      session != nullptr && item != nullptr
          ? session->objects().GetItem(
                ::openwow::game::ObjectGuid(item->guid))
          : nullptr;
  if (item == nullptr || live_item == nullptr || live_item->IsLocked() ||
      !CanOpenSocketUiForItem(L, item)) {
    return 0;
  }

  OpenSocketUiForItem(L, item);
  return 0;
}

int LuaSocketContainerItem(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SocketContainerItem(index, slot)");
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const int bag = static_cast<int>(lua_tonumber(L, 1));
  const int zero_based_slot = static_cast<int>(lua_tonumber(L, 2)) - 1;
  if (bag < 0 || bag > ::openwow::game::PlayerInventoryReplica::kMaxBags ||
      zero_based_slot < 0) {
    return 0;
  }

  const auto *item = RequirePlayerInventoryReplica(L).GetContainerSlot(
      static_cast<std::uint8_t>(bag),
      static_cast<std::uint8_t>(zero_based_slot));
  if (!CanOpenSocketUiForItem(L, item)) {
    return 0;
  }

  OpenSocketUiForItem(L, item);
  return 0;
}

int LuaCloseSocketInfo(lua_State *L) {
  if (auto* session = GetWorldSession(L); session != nullptr) {
    session->item_interactions().cancel_socket();
  }
  FireSocketEvent(L, "SOCKET_INFO_CLOSE");
  return 0;
}

int LuaAcceptSockets(lua_State *L) {
  if (auto *session = GetWorldSession(L); session != nullptr) {
    const auto& socket = session->item_interactions().socket();
    if (socket.has_value()) {
      session->interaction().SendSocketGems(
          socket->item.GetRawValue(),
          socket->gems[0].GetRawValue(), socket->gems[1].GetRawValue(),
          socket->gems[2].GetRawValue());
    }
  }
  return 0;
}

int LuaFollowUnit(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *targeting = GetTargetingSystem(L);
  if (!session || !targeting)
    return 0;

  const std::string unit_id = SafeLuaString(L, 1);
  const bool exact_match = ReadClientBoolArgOrDefault(L, 2, false);

  auto guid =
      ResolveGameUiLookup(session, unit_id, openwow::game::kTypeMaskPlayer, 3, exact_match, false);
  if (guid.IsEmpty()) {
    guid =
        ResolveGameUiLookup(session, unit_id, openwow::game::kTypeMaskUnit, 0, exact_match, false);
  }

  if (!guid.IsEmpty()) {
    targeting->StartFollow(guid.GetRawValue());
  } else if (!unit_id.empty() &&
             openwow::core::SStrCmpNoCase(unit_id.c_str(), "target",
                                          0x7FFFFFFF) != 0) {
    DisplaySystemMessage(314);
  } else {
    DisplaySystemMessage(199);
  }
  return 0;
}

int LuaAttackTarget(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *targeting = GetTargetingSystem(L);
  if (!session || !targeting)
    return 0;
  if (session->objects().GetActivePlayer() == nullptr)
    return 0;

  if (targeting->IsAttackFollowing() || targeting->IsAttackSwingActive()) {
    targeting->StopAttack(true);
    return 0;
  }

  const auto attack_precondition = targeting->ValidateAttackStart();
  if (attack_precondition.result != openwow::game::AttackStartResult::kNoAction) {
    DisplayAttackStartFailure(*session, attack_precondition);
    return 0;
  }

  const auto target_guid =
      ResolvePlayerAttackCommandTargetGuid(L, *session, 0, false);
  if (target_guid == 0) {
    DisplaySystemMessage(kNoAttackTargetSystemMessage);
    return 0;
  }

  const auto attack_outcome = targeting->StartAttack(target_guid);
  DisplayAttackStartFailure(*session, attack_outcome);
  return 0;
}

int LuaStartAttack(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *targeting = GetTargetingSystem(L);
  if (!session || !targeting)
    return 0;

  std::uint64_t target_guid = 0;
  bool has_explicit_target = false;
  if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
    std::string unit_id = SafeLuaString(L, 1);
    if (!unit_id.empty()) {
      has_explicit_target = true;
      const bool exact_match = ReadClientBoolArgOrDefault(L, 2, false);
      const auto guid =
          ResolveGameUiLookup(session, unit_id, openwow::game::kTypeMaskUnit, 1, exact_match, true);
      if (!guid.IsEmpty()) {
        target_guid = guid.GetRawValue();
      }
    }
  }

  target_guid =
      ResolvePlayerAttackCommandTargetGuid(L, *session, target_guid, has_explicit_target);
  if (target_guid != 0) {
    targeting->StartAttack(target_guid);
  }
  return 0;
}

int LuaStopAttack(lua_State *L) {
  StopPlayerAttackFromScript(GetWorldSession(L), GetTargetingSystem(L));
  return 0;
}

int LuaIsOutdoors(lua_State *L) {
  const auto* const session = GetWorldSession(L);
  const auto* const player =
      session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (player != nullptr && openwow::game::IsPlayerOutdoors(*player)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaRandomRoll(lua_State *L) {
  if (lua_isstring(L, 1) == 0 || lua_isstring(L, 2) == 0) {
    return luaL_error(L, "Usage: RandomRoll(\"max\") or RandomRoll(\"min\", \"max\")");
  }

  const auto min_val = static_cast<std::int32_t>(
      openwow::core::ParseSignedDecimalLikeSub76F0D0(lua_tostring(L, 1)));
  const auto max_val = static_cast<std::int32_t>(
      openwow::core::ParseSignedDecimalLikeSub76F0D0(lua_tostring(L, 2)));
  if ((min_val == 0 && max_val == 0) || min_val < 0 || max_val < min_val ||
      max_val > 1000000) {
    return 0;
  }

  openwow::net::wotlk::WorldPacket pkt(openwow::net::wotlk::Opcode::MSG_RANDOM_ROLL);
  pkt.AppendU32(static_cast<std::uint32_t>(min_val));
  pkt.AppendU32(static_cast<std::uint32_t>(max_val));
  (void)openwow::net::ClientServices__SendPacket(pkt);
  return 0;
}

int LuaDeclineInvite(lua_State *L) {
  return LuaSingleChannelCommand(
      L, "DeclineInvite",
      static_cast<std::uint16_t>(openwow::net::wotlk::Opcode::CMSG_DECLINE_CHANNEL_INVITE));
}

int LuaEquipPendingItem(lua_State *L) {
  return ResolvePendingItemDecision(L, "Usage: EquipPendingItem(index)", true);
}

int LuaSetChannelWatch(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: %s(\"channel\")", "SetChannelWatch");
  }

  const char *raw_channel = lua_tostring(L, 1);
  const auto channel_name = ResolveChannelNameOrIndex(raw_channel);
  if (!channel_name) {
    return 0;
  }

  SendWatchedChannelSelection(L, *channel_name, false);
  return 0;
}

int LuaClearChannelWatch(lua_State *L) {
  ClearWatchedChannelSelection(L);
  return 0;
}

int LuaSetAllowLowLevelRaid(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const bool allow = ScriptReadBoolArgOrDefault(L, 1, true);
  session->interaction().SendSetAllowLowLevelRaid(allow);
  return 0;
}

int LuaCanResetTutorials(lua_State *L) {
  if (openwow::game::TutorialSystem::Instance().CanResetTutorials()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaEquipmentSetContainsLockedItems(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: EquipmentSetContainsLockedItems(\"setName\")");
  }

  const char *set_name = lua_tostring(L, 1);
  (void)RequireEquipmentSets(L).find(set_name != nullptr ? set_name : "");
  return 0;
}

int LuaFrameXML_Debug(lua_State *L) {
  int debug_level = openwow::ui::GetFrameXMLDebugLevel();
  if (lua_isnumber(L, 1) != 0) {
    debug_level = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
    openwow::ui::SetFrameXMLDebugLevel(debug_level);
  }
  lua_pushnumber(L, static_cast<lua_Number>(debug_level));
  return 1;
}

int LuaGetAccountExpansionLevel(lua_State *L) {
  return openwow::ui::PushLuaLiveExpansionLevel(L);
}

int LuaGetAllowLowLevelRaid(lua_State *L) {
  return PushActivePlayerFlagAsLegacyNumberOrNil(L, 0x10000u);
}

int LuaGetClickFrame(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetClickFrame(\"name\")");
  }

  const char *frame_name = lua_tostring(L, 1);
  if (frame_name != nullptr && PushNamedClickFrame(L, frame_name)) {
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetMinigameType(lua_State *L) {
  if (const auto type_name = ::openwow::game::MinigameSystem::Get().GetLuaTypeName();
      type_name.has_value()) {
    lua_pushlstring(L, type_name->data(), type_name->size());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetMinigameState(lua_State *L) {
  if (const auto state = ::openwow::game::MinigameSystem::Get().GetLuaState();
      state.has_value()) {
    const int result_count = openwow::ui::ReserveLuaResultCapacity(
        L, state->size(), "minigame state values");
    for (const auto cell : *state) {
      lua_pushnumber(L, static_cast<lua_Number>(cell));
    }
    return result_count;
  }

  return 0;
}

int LuaGetPossessInfo(lua_State *L) {
  const auto raw_index = lua_tonumber(L, 1);
  if (raw_index == 0.0) {
    return luaL_error(L, "Usage: GetPossessInfo(index)");
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    PushNilPossessInfo(L);
    return 3;
  }

  const auto selection = ResolvePossessBarSelection(*session);
  if (selection.spell_id == 0) {
    PushNilPossessInfo(L);
    return 3;
  }

  const auto spell_name = ResolvePossessBarSpellName(*session, selection.spell_id);
  const auto index = static_cast<std::int64_t>(raw_index);

  const auto push_texture_and_name = [&](const std::uint32_t spell_id) {
    const auto texture = ResolvePossessBarSpellTexturePath(L, spell_id);
    if (texture.empty()) {
      FrameScript_PushNil(L);
    } else {
      lua_pushstring(L, texture.c_str());
    }

    if (spell_name.empty()) {
      FrameScript_PushNil(L);
    } else {
      lua_pushstring(L, spell_name.c_str());
    }
  };

  if (index == 1) {
    push_texture_and_name(selection.spell_id);
    FrameScript_PushBoolean(L, true);
    return 3;
  }

  if (index != 2) {
    PushNilPossessInfo(L);
    return 3;
  }

  push_texture_and_name(kPossessBarReleaseSpellId);
  FrameScript_PushBoolean(L, ResolvePossessBarReleaseUsable(selection.spell_id));
  return 3;
}

int LuaGetRealNumPartyMembers(lua_State *L) {
  lua_pushnumber(
      L, static_cast<lua_Number>(::openwow::game::GroupSystem::Get().GetRealPartyMemberCount()));
  return 1;
}

int LuaGetRealNumRaidMembers(lua_State *L) {
  lua_pushnumber(
      L, static_cast<lua_Number>(::openwow::game::GroupSystem::Get().GetRealRaidMemberCount()));
  return 1;
}

namespace {

}

int LuaGetUnitHealthModifier(lua_State *L) {
  const LuaCallFrame call{L};
  const auto token = call.require_string(1, "Usage: GetUnitHealthModifier(\"unit\")");
  return call.number(LuaDerivedStatQuery(call.state(), token).health_modifier());
}

int LuaGetUnitPowerModifier(lua_State *L) {
  const LuaCallFrame call{L};
  const auto token = call.require_string(1, "Usage: GetUnitPowerModifier(\"unit\")");
  return call.number(LuaDerivedStatQuery(call.state(), token).power_modifier());
}

int LuaHasKey(lua_State *L) {
  const auto &inv = RequirePlayerInventoryReplica(L);
  for (std::uint8_t i = 0; i < ::openwow::game::PlayerInventoryReplica::kKeyringSlots; ++i) {
    const auto *item = inv.GetKeyringSlot(i);
    if (item && !item->IsEmpty()) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

int LuaIsPossessBarVisible(lua_State *L) {
  bool is_visible = false;
  if (const auto *session = GetWorldSession(L); session != nullptr) {
    const auto selection = ResolvePossessBarSelection(*session);
    is_visible = selection.spell_id != 0 && !selection.hidden_by_aura_236;
  }

  lua_pushboolean(L, is_visible ? 1 : 0);
  return 1;
}

int LuaAcceptAreaSpiritHeal(lua_State *L) {
  if (auto *session = GetWorldSession(L); session != nullptr) {
    (void)::openwow::game::combat::death::ui::AcceptAreaSpiritHeal(*session);
  }
  return 0;
}

int LuaCancelAreaSpiritHeal(lua_State *L) {
  if (auto *session = GetWorldSession(L); session != nullptr) {
    ::openwow::game::combat::death::ui::CancelAreaSpiritHeal(*session);
  }
  return 0;
}

int LuaGetAreaSpiritHealerTime(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto remaining =
      session != nullptr
          ? ::openwow::game::combat::death::ui::GetAreaSpiritHealerRemainingTime(
                *session)
          : std::chrono::seconds::zero();
  lua_pushnumber(L, static_cast<lua_Number>(remaining.count()));
  return 1;
}

int LuaGetCorpseMapPosition(lua_State *L) {
  const auto corpse = [](lua_State *state) {
    if (auto *session = GetWorldSession(state); session != nullptr) {
      return GameUI_GetCorpsePositionData(*session);
    }
    return GameUI_GetCorpsePositionData();
  }(L);

  float corpse_map_x = 0.0f;
  float corpse_map_y = 0.0f;
  auto *wm = WorldMapStateOrNull(L);
  if (corpse.map_id >= 0 && wm != nullptr) {

    const auto mc = wm->WorldToMapForCurrentSelection(static_cast<std::uint32_t>(corpse.map_id),
                                                      corpse.position.x, corpse.position.y,
                                                      corpse.position.z);
    corpse_map_x = mc.x;
    corpse_map_y = mc.y;
  }

  lua_pushnumber(L, corpse_map_x);
  lua_pushnumber(L, corpse_map_y);
  return 2;
}

int LuaGetDeathReleasePosition(lua_State *L) {
  const auto death_release = openwow::game::GetDeathReleasePosition();
  auto *world_map = WorldMapStateOrNull(L);
  if (world_map == nullptr) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 2;
  }

  const auto map_coords = world_map->WorldToMapForCurrentSelection(
      static_cast<std::uint32_t>(death_release.map_id), death_release.x,
      death_release.y, death_release.z);
  lua_pushnumber(L, map_coords.x);
  lua_pushnumber(L, map_coords.y);
  return 2;
}

int LuaStartDuel(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const std::string unit_id = SafeLuaString(L, 1);
  const bool exact_match = ReadClientBoolArgOrDefault(L, 2, false);
  auto guid =
      ResolveGameUiLookup(session, unit_id, openwow::game::kTypeMaskPlayer, 0, exact_match, false);
  if (!guid.IsEmpty()) {
    session->interaction().SendCastSpell(openwow::game::DuelSystem::kDuelRequestSpellId, 0,
                                         guid.GetRawValue());
    session->duel().Challenge(guid, {});
  }
  return 0;
}

int LuaGetCoinIcon(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetCoinIcon(amount)");
  }

  const auto amount = ToScriptCoinAmount(lua_tonumber(L, 1));
  const auto icon_path = openwow::game::MoneyDisplay::GetCoinIconPath(amount);
  lua_pushstring(L, icon_path.c_str());
  return 1;
}

int LuaRegisterForSavePerCharacter(lua_State *L) {
  if (!SecureExecution::Get().IsSecure(L)) {
    return luaL_error(L,
                      "RegisterForSavePerCharacter() is only available to Blizzard scripts");
  }

  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage: RegisterForSavePerCharacter(\"variable\")");
  }

  openwow::ui::game::RegisterSavedVariableName(
      SavedVariableRegistrationScope::kPerCharacter, lua_tostring(L, 1));
  return 0;
}

int LuaDeclineName(lua_State *L) {
  return openwow::ui::LuaDeclineName(L);
}

int LuaFillLocalizedClassList(lua_State *L) {
  if (lua_type(L, 1) != LUA_TTABLE) {
    return luaL_error(L, "Usage: FillLocalizedClassList(classTable[, isFemale])");
  }

  const bool is_female = ScriptReadBoolArgOrDefault(L, 2, false);

  lua_settop(L, 1);

  const auto *dbc = GetDbcLoaderLocal(L);
  if (dbc != nullptr) {
    const std::uint32_t sex = is_female ? 1u : 0u;
    for (const auto &entry : dbc->chr_classes()) {
      lua_pushlstring(L, entry.client_file_string.data(), entry.client_file_string.size());
      const auto display_name = entry.DisplayNameForSex(sex);
      lua_pushlstring(L, display_name.data(), display_name.size());
      lua_settable(L, -3);
    }
  }

  return 1;
}

int LuaGetFactionForRace(lua_State *L) {
  int race_id = lua_gettop(L) >= 1 ? static_cast<int>(lua_tonumber(L, 1)) : 0;

  switch (race_id) {
  case 1:
  case 3:
  case 4:
  case 7:
  case 11:
    lua_pushstring(L, "Alliance");
    lua_pushstring(L, "A");
    return 2;
  case 2:
  case 5:
  case 6:
  case 8:
  case 10:
    lua_pushstring(L, "Horde");
    lua_pushstring(L, "H");
    return 2;
  default:
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }
}

int LuaShowingHelm(lua_State *L) {
  PushLocalPlayerShownFlag(L, kPlayerFlagsHideHelm);
  return 1;
}

int LuaShowingCloak(lua_State *L) {
  PushLocalPlayerShownFlag(L, kPlayerFlagsHideCloak);
  return 1;
}

int LuaShowHelm(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  const bool show = ScriptReadBoolArgOrDefault(L, 1, true);
  session->interaction().SendShowingHelm(show);
  return 0;
}

int LuaShowCloak(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  const bool show = ScriptReadBoolArgOrDefault(L, 1, true);
  session->interaction().SendShowingCloak(show);
  return 0;
}

int LuaGetNumFrames(lua_State *L) {
  const auto *mgr = GetGameUiManager(L);
  FrameScript_PushNumberFromInt(
      L, mgr != nullptr
             ? static_cast<int>(mgr->frame_store().enumerable_frame_count())
             : 0);
  return 1;
}

int LuaEnumerateFrames(lua_State *L) {
  auto *mgr = GetGameUiManager(L);
  if (mgr == nullptr) {
    FrameScript_PushNil(L);
    return 1;
  }

  if (lua_type(L, 1) == LUA_TTABLE) {
    lua_rawgeti(L, 1, 0);
    void *const object_token = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (object_token == nullptr) {
      return luaL_error(L, "EnumerateFrames: Couldn't find 'this' in current object");
    }

    const auto& frame_store = mgr->frame_store();
    if (!frame_store.ContainsEnumerableFrameNativeIdentity(object_token)) {
      return luaL_error(L, "EnumerateFrames: Wrong current object type, expected frame");
    }

    if (frame_store.PushNextEnumerableFrame(L, object_token)) {
      return 1;
    }

    FrameScript_PushNil(L);
    return 1;
  }

  if (mgr->frame_store().PushNewestEnumerableFrame(L)) {
    return 1;
  }

  FrameScript_PushNil(L);
  return 1;
}

int LuaGetModifiedClickAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetModifiedClickAction(index)");
  }

  const int index =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  auto *mgr = GetBindingProfiles(L);
  if (mgr == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto action =
      openwow::game::actions::bindings::adapters::retail::
          ModifiedClickActionAt(*mgr, index);
  if (!action) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, action->value().c_str());
  }
  return 1;
}

}
