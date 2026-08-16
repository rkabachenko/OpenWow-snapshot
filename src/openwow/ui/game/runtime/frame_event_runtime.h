#pragma once

#include "openwow/ui/game/event_dispatcher.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct lua_State;

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game::runtime {

class FrameStore;
class FrameTraversalIndex;

class FrameEventRuntime final {
 public:
  struct Dependencies {
    FrameStore& frames;
    FrameTraversalIndex& traversal;
    std::function<void(const char*)> fire_world_entry_phase;
  };

  explicit FrameEventRuntime(Dependencies dependencies);
  ~FrameEventRuntime();
  FrameEventRuntime(const FrameEventRuntime&) = delete;
  FrameEventRuntime& operator=(const FrameEventRuntime&) = delete;

  void Initialize(lua_State* lua, openwow::game::WorldSession* session);
  void Shutdown();
  void Update(float elapsed_seconds);

  void RegisterOnUpdate(const std::string& name, std::uint8_t strata,
                        std::int32_t level);
  void UnregisterOnUpdate(const std::string& name);
  void MarkOnUpdateOrderDirty() noexcept;

  [[nodiscard]] EventDispatcher& dispatcher() noexcept { return dispatcher_; }
  [[nodiscard]] const EventDispatcher& dispatcher() const noexcept {
    return dispatcher_;
  }

  void OnPlayerLogin();
  void OnPlayerLogout();
  void OnPlayerEnteringWorld();
  void OnPlayerLeavingWorld();
  void OnUnitHealth(const std::string& unit_id);
  void OnUnitMana(const std::string& unit_id);
  void OnUnitAura(const std::string& unit_id);
  void OnUnitTarget(const std::string& unit_id);
  void OnPlayerTargetChanged();
  void OnPlayerFocusChanged();
  void OnChatMessage(const std::string& event_name, const std::string& message,
                     const std::string& sender, const std::string& channel);
  void OnQuestLogUpdate();
  void OnQuestAccepted(int quest_id);
  void OnQuestDetail();
  void OnQuestComplete();
  void OnQuestGreeting();
  void OnLootOpened();
  void OnLootClosed();
  void OnBagUpdate(int bag);
  void OnPlayerMoneyChanged();
  void OnPlayerEquipmentChanged(int inventory_slot, bool has_item);
  void OnSpellsChanged();
  void OnSpellUpdateCooldown();
  void OnActionbarUpdateState();
  void OnActionbarSlotChanged(int slot);
  void OnGossipShow();
  void OnMerchantShow();
  void OnMerchantClosed();
  void OnTrainerShow();
  void OnTrainerClosed();
  void OnBankOpened();
  void OnBankClosed();
  void OnMailShow();
  void OnMailClosed();
  void OnAuctionShow();
  void OnAuctionClosed();
  void OnZoneChanged();
  void OnPartyMembersChanged();
  void OnGuildRosterUpdate();
  void OnFriendlistUpdate();
  void OnPlayerDead();
  void OnPlayerAlive();
  void OnResurrectRequest(const std::string& offerer_name);
  void OnConfirmXpLoss();
  void OnConfirmXpLoss(int xp_loss);
  void OnUpdateShapeshiftForm();
  void OnSpellcastStart(const std::string& unit_id);
  void OnSpellcastStop(const std::string& unit_id);
  void OnSpellcastFailed(const std::string& unit_id);
  void OnUiErrorMessage(const std::string& message);
  void OnUiInfoMessage(const std::string& message);
  void OnVariablesLoaded();
  void OnAddonLoaded(const std::string& addon_name);

 private:
  struct OnUpdateEntry {
    int lua_ref{-2};
    std::uint8_t strata{3};
    std::int32_t level{0};
    std::string name;

    std::uint64_t handle{~0ULL};
    bool registered{true};
  };

  Dependencies dependencies_;
  EventDispatcher dispatcher_;
  lua_State* lua_{nullptr};
  std::vector<OnUpdateEntry> on_update_entries_;
  std::vector<OnUpdateEntry> pending_on_update_entries_;
  bool on_update_order_dirty_{false};
  bool dispatching_on_update_{false};
  bool initialized_{false};
};

}
