#include "openwow/ui/game/runtime/frame_event_runtime.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/frame_traversal_index.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/lua_c_api_convenience.h"

#include <lua.hpp>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

namespace openwow::ui::game::runtime {

FrameEventRuntime::FrameEventRuntime(Dependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

FrameEventRuntime::~FrameEventRuntime() { Shutdown(); }

void FrameEventRuntime::Initialize(lua_State* lua,
                                   openwow::game::WorldSession* session) {
  if (initialized_) Shutdown();
  lua_ = lua;
  dispatcher_.Initialize(lua_);
  ScriptEventDispatch::Get().Initialize(&dispatcher_, session);
  initialized_ = true;
}

void FrameEventRuntime::Shutdown() {
  if (!initialized_) return;
  ScriptEventDispatch::Get().Shutdown();
  dispatcher_.Shutdown();
  lua_ = nullptr;
  on_update_entries_.clear();
  pending_on_update_entries_.clear();
  on_update_order_dirty_ = false;
  dispatching_on_update_ = false;
  initialized_ = false;
}

void FrameEventRuntime::Update(const float elapsed_seconds) {
  if (lua_ == nullptr) return;

  if (on_update_order_dirty_) {
    std::stable_sort(
        on_update_entries_.begin(), on_update_entries_.end(),
        [](const OnUpdateEntry& left, const OnUpdateEntry& right) {
          if (left.strata != right.strata) return left.strata < right.strata;
          return left.level < right.level;
        });
    on_update_order_dirty_ = false;
  }

  dispatching_on_update_ = true;
  for (auto& entry : on_update_entries_) {
    if (!entry.registered) continue;

    const int top = lua_gettop(lua_);
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, entry.lua_ref);
    if (lua_istable(lua_, -1) == 0) {
      lua_settop(lua_, top);
      continue;
    }

    if (!dependencies_.traversal.IsEffectivelyVisible(entry.handle)) {
      lua_settop(lua_, top);
      continue;
    }

    const int frame_index = lua_absindex(lua_, -1);
    lua_pushnumber(lua_, static_cast<double>(elapsed_seconds));
    const auto invocation =
        InvokeFrameScriptHandler(lua_, frame_index, "OnUpdate", 1);
    if (invocation.status != LUA_OK) {
      const char* error = lua_tostring(lua_, -1);
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          std::string("FrameEventRuntime: OnUpdate error: ") +
              (error != nullptr ? error : "(null)"));
      lua_pop(lua_, 1);
    }
    lua_settop(lua_, top);
  }
  dispatching_on_update_ = false;

  std::erase_if(on_update_entries_,
                [](const OnUpdateEntry& entry) { return !entry.registered; });
  if (!pending_on_update_entries_.empty()) {
    on_update_entries_.insert(
        on_update_entries_.end(),
        std::make_move_iterator(pending_on_update_entries_.begin()),
        std::make_move_iterator(pending_on_update_entries_.end()));
    pending_on_update_entries_.clear();
    on_update_order_dirty_ = true;
  }
}

void FrameEventRuntime::RegisterOnUpdate(const std::string& name,
                                         const std::uint8_t strata,
                                         const std::int32_t level) {
  const auto ref = dependencies_.frames.FindLuaRef(name);
  if (!ref.has_value()) return;

  const auto update_existing = [&](std::vector<OnUpdateEntry>& entries) {
    for (auto& entry : entries) {
      if (entry.lua_ref != *ref || !entry.registered) continue;
      if (entry.name != name) {
        entry.name = name;

        entry.handle = dependencies_.frames.HandleOf(name);
      }
      if (entry.strata != strata || entry.level != level) {
        entry.strata = strata;
        entry.level = level;
        on_update_order_dirty_ = true;
      }
      return true;
    }
    return false;
  };
  if (update_existing(on_update_entries_) ||
      update_existing(pending_on_update_entries_)) {
    return;
  }

  OnUpdateEntry entry{
      .lua_ref = *ref,
      .strata = strata,
      .level = level,
      .name = name,
      .handle = dependencies_.frames.HandleOf(name),
  };
  if (dispatching_on_update_) {
    pending_on_update_entries_.push_back(std::move(entry));
  } else {
    on_update_entries_.push_back(std::move(entry));
    on_update_order_dirty_ = true;
  }
}

void FrameEventRuntime::UnregisterOnUpdate(const std::string& name) {
  const auto ref = dependencies_.frames.FindLuaRef(name);
  if (!ref.has_value()) return;

  std::erase_if(pending_on_update_entries_,
                [ref](const OnUpdateEntry& entry) {
                  return entry.lua_ref == *ref;
                });
  if (dispatching_on_update_) {
    for (auto& entry : on_update_entries_) {
      if (entry.lua_ref == *ref) entry.registered = false;
    }
  } else {
    std::erase_if(on_update_entries_, [ref](const OnUpdateEntry& entry) {
      return entry.lua_ref == *ref;
    });
  }
  on_update_order_dirty_ = true;
}

void FrameEventRuntime::MarkOnUpdateOrderDirty() noexcept {
  on_update_order_dirty_ = true;
}

void FrameEventRuntime::OnPlayerLogin() {
  dependencies_.fire_world_entry_phase(events::PLAYER_LOGIN);
}
void FrameEventRuntime::OnPlayerLogout() {
  dispatcher_.FireEvent(events::PLAYER_LOGOUT);
}
void FrameEventRuntime::OnPlayerEnteringWorld() {
  dependencies_.fire_world_entry_phase(events::PLAYER_ENTERING_WORLD);
}
void FrameEventRuntime::OnPlayerLeavingWorld() {
  dispatcher_.FireEvent(events::PLAYER_LEAVING_WORLD);
}
void FrameEventRuntime::OnUnitHealth(const std::string& unit_id) {
  dispatcher_.FireEvent(events::UNIT_HEALTH, unit_id);
}
void FrameEventRuntime::OnUnitMana(const std::string& unit_id) {
  dispatcher_.FireEvent(events::UNIT_MANA, unit_id);
}
void FrameEventRuntime::OnUnitAura(const std::string& unit_id) {
  dispatcher_.FireEvent(events::UNIT_AURA, unit_id);
}
void FrameEventRuntime::OnUnitTarget(const std::string& unit_id) {
  dispatcher_.FireEvent(events::UNIT_TARGET, unit_id);
}
void FrameEventRuntime::OnPlayerTargetChanged() {
  dispatcher_.FireEvent(events::PLAYER_TARGET_CHANGED);
}
void FrameEventRuntime::OnPlayerFocusChanged() {
  dispatcher_.FireEvent(events::PLAYER_FOCUS_CHANGED);
}
void FrameEventRuntime::OnChatMessage(const std::string& event_name,
                                      const std::string& message,
                                      const std::string& sender,
                                      const std::string& channel) {
  dispatcher_.FireEvent(event_name, message, sender, channel);
}
void FrameEventRuntime::OnQuestLogUpdate() {
  dispatcher_.FireEvent(events::QUEST_LOG_UPDATE);
}
void FrameEventRuntime::OnQuestAccepted(const int quest_id) {
  dispatcher_.FireEvent(events::QUEST_ACCEPTED, quest_id);
}
void FrameEventRuntime::OnQuestDetail() {
  dispatcher_.FireEvent(events::QUEST_DETAIL);
}
void FrameEventRuntime::OnQuestComplete() {
  dispatcher_.FireEvent(events::QUEST_COMPLETE);
}
void FrameEventRuntime::OnQuestGreeting() {
  dispatcher_.FireEvent(events::QUEST_GREETING);
}
void FrameEventRuntime::OnLootOpened() {
  dispatcher_.FireEvent(events::LOOT_OPENED);
}
void FrameEventRuntime::OnLootClosed() {
  dispatcher_.FireEvent(events::LOOT_CLOSED);
}
void FrameEventRuntime::OnBagUpdate(const int bag) {
  ScriptEventDispatch::Get().FireBagUpdate(bag);
}
void FrameEventRuntime::OnPlayerMoneyChanged() {
  dispatcher_.FireEvent(events::PLAYER_MONEY);
}
void FrameEventRuntime::OnPlayerEquipmentChanged(const int inventory_slot,
                                                 const bool has_item) {
  if (inventory_slot < 0 || inventory_slot >= 19) return;
  ScriptEventDispatch::Get().FirePlayerEquipmentChanged(
      static_cast<std::uint8_t>(inventory_slot), has_item);
}
void FrameEventRuntime::OnSpellsChanged() {
  dispatcher_.FireEvent(events::SPELLS_CHANGED);
}
void FrameEventRuntime::OnSpellUpdateCooldown() {
  dispatcher_.FireEvent(events::SPELL_UPDATE_COOLDOWN);
}
void FrameEventRuntime::OnActionbarUpdateState() {
  dispatcher_.FireEvent(events::ACTIONBAR_UPDATE_STATE);
}
void FrameEventRuntime::OnActionbarSlotChanged(const int slot) {
  dispatcher_.FireEvent(events::ACTIONBAR_SLOT_CHANGED, slot);
}
void FrameEventRuntime::OnGossipShow() {
  dispatcher_.FireEvent(events::GOSSIP_SHOW);
}
void FrameEventRuntime::OnMerchantShow() {
  dispatcher_.FireEvent(events::MERCHANT_SHOW);
}
void FrameEventRuntime::OnMerchantClosed() {
  dispatcher_.FireEvent(events::MERCHANT_CLOSED);
}
void FrameEventRuntime::OnTrainerShow() {
  dispatcher_.FireEvent(events::TRAINER_SHOW);
}
void FrameEventRuntime::OnTrainerClosed() {
  dispatcher_.FireEvent(events::TRAINER_CLOSED);
}
void FrameEventRuntime::OnBankOpened() {
  dispatcher_.FireEvent(events::BANKFRAME_OPENED);
}
void FrameEventRuntime::OnBankClosed() {
  dispatcher_.FireEvent(events::BANKFRAME_CLOSED);
}
void FrameEventRuntime::OnMailShow() {
  dispatcher_.FireEvent(events::MAIL_SHOW);
}
void FrameEventRuntime::OnMailClosed() {
  dispatcher_.FireEvent(events::MAIL_CLOSED);
}
void FrameEventRuntime::OnAuctionShow() {
  dispatcher_.FireEvent(events::AUCTION_HOUSE_SHOW);
}
void FrameEventRuntime::OnAuctionClosed() {
  dispatcher_.FireEvent(events::AUCTION_HOUSE_CLOSED);
}
void FrameEventRuntime::OnZoneChanged() {
  dispatcher_.FireEvent(events::ZONE_CHANGED_NEW_AREA);
}
void FrameEventRuntime::OnPartyMembersChanged() {
  dispatcher_.FireEvent(events::PARTY_MEMBERS_CHANGED);
}
void FrameEventRuntime::OnGuildRosterUpdate() {
  dispatcher_.FireEvent(events::GUILD_ROSTER_UPDATE);
}
void FrameEventRuntime::OnFriendlistUpdate() {
  dispatcher_.FireEvent(events::FRIENDLIST_UPDATE);
}
void FrameEventRuntime::OnPlayerDead() {
  dispatcher_.FireEvent(events::PLAYER_DEAD);
}
void FrameEventRuntime::OnPlayerAlive() {
  dispatcher_.FireEvent(events::PLAYER_ALIVE);
}
void FrameEventRuntime::OnResurrectRequest(const std::string& offerer_name) {
  if (!offerer_name.empty()) {
    dispatcher_.FireEvent(events::RESURRECT_REQUEST, offerer_name);
  }
}
void FrameEventRuntime::OnConfirmXpLoss() {
  dispatcher_.FireEvent(events::CONFIRM_XP_LOSS);
}
void FrameEventRuntime::OnConfirmXpLoss(const int xp_loss) {
  dispatcher_.FireEvent(events::CONFIRM_XP_LOSS, xp_loss);
}
void FrameEventRuntime::OnUpdateShapeshiftForm() {
  dispatcher_.FireEvent(events::UPDATE_SHAPESHIFT_FORM);
}
void FrameEventRuntime::OnSpellcastStart(const std::string& unit_id) {
  dispatcher_.FireEvent(events::UNIT_SPELLCAST_START, unit_id);
}
void FrameEventRuntime::OnSpellcastStop(const std::string& unit_id) {
  dispatcher_.FireEvent(events::UNIT_SPELLCAST_STOP, unit_id);
}
void FrameEventRuntime::OnSpellcastFailed(const std::string& unit_id) {
  dispatcher_.FireEvent(events::UNIT_SPELLCAST_FAILED, unit_id);
}
void FrameEventRuntime::OnUiErrorMessage(const std::string& message) {
  dispatcher_.FireEvent(events::UI_ERROR_MESSAGE, message);
}
void FrameEventRuntime::OnUiInfoMessage(const std::string& message) {
  dispatcher_.FireEvent(events::UI_INFO_MESSAGE, message);
}
void FrameEventRuntime::OnVariablesLoaded() {
  dependencies_.fire_world_entry_phase(events::VARIABLES_LOADED);
}
void FrameEventRuntime::OnAddonLoaded(const std::string& addon_name) {
  dispatcher_.FireEvent(events::ADDON_LOADED, addon_name);
}

}
