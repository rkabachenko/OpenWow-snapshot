#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/ui/game/loot_tooltip_support.h"

#include <algorithm>
#include <optional>

namespace openwow::ui::game::detail {
namespace {

constexpr std::uint32_t kSelfResurrectionItemEffect = 94;

bool IsSelfResurrectionItem(
    lua_State* state, const openwow::game::ItemTemplate& item) {
  for (const auto& item_spell : item.spells) {
    if (item_spell.spell_id == 0 || item_spell.trigger != 0) {
      continue;
    }
    const auto* spell = LookupSpellEntry(state, item_spell.spell_id);
    if (spell != nullptr &&
        std::find(
            spell->effect.begin(), spell->effect.end(),
            kSelfResurrectionItemEffect) != spell->effect.end()) {
      return true;
    }
  }
  return false;
}

const openwow::game::ItemInstance* FindSelfResurrectionItem(
    lua_State* state, ItemLuaAdapter& adapter) {
  return openwow::game::FindFirstDefaultCarriedInventoryItem(
      adapter.inventory(),
      [state, &adapter](const openwow::game::ItemInstance& item) {
        const auto* definition =
            item.entry == 0
                ? nullptr
                : adapter.queries().GetOrRequestItemTemplate(item.entry);
        return definition != nullptr &&
               IsSelfResurrectionItem(state, *definition);
      });
}

std::optional<std::string> SelfResurrectionName(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  const auto* death = GetDeathManager(state);
  const auto* player = adapter.objects().GetLocalPlayer();
  if (death == nullptr || !death->IsDead() || player == nullptr) {
    return std::nullopt;
  }

  const auto spell_id = player->GetUInt32(PLAYER_SELF_RES_SPELL);
  if (spell_id != 0) {
    const auto* spell = LookupSpellEntry(state, spell_id);
    return spell != nullptr && !spell->spell_name.empty()
               ? std::optional<std::string>{spell->spell_name}
               : std::optional<std::string>{"UNKNOWN"};
  }

  const auto* item = FindSelfResurrectionItem(state, adapter);
  const auto* definition =
      item == nullptr
          ? nullptr
          : adapter.queries().GetOrRequestItemTemplate(item->entry);
  if (definition == nullptr || definition->name.empty()) {
    return std::nullopt;
  }
  return ResolveLootItemDisplayName(
      GetDbcLoader(state), definition->name, item->random_property);
}

}

int LuaHasSoulstone(lua_State* state) {
  const auto name = SelfResurrectionName(state);
  if (name.has_value()) {
    lua_pushlstring(state, name->data(), name->size());
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaUseSoulstone(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  const auto* player = adapter.objects().GetLocalPlayer();
  if (player == nullptr) {
    return 0;
  }
  if (player->GetUInt32(PLAYER_SELF_RES_SPELL) != 0) {

    static_cast<void>(adapter.interaction().SendSelfResurrect());
    return 0;
  }
  const auto* item = FindSelfResurrectionItem(state, adapter);
  if (item != nullptr) {
    openwow::game::CancelPendingCastsForActivePlayer(
        adapter.world_session());
    adapter.interaction().SendUseItemByGuid(item->guid, 0);
  }
  return 0;
}

}
