#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct lua_State;

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {
class BattlefieldInfo;
class GroupSystem;
class InstanceHandler;
class InteractionSender;
class ItemInteractionSession;
class ItemUseRequirementSources;
class ItemDefinitions;
class Localization;
class MiscHandler;
class ObjectManager;
class PlayerInventoryReplica;
class QueryCache;
class SessionHandler;
class SpellCastRuntime;
class TradeInteraction;
class WorldSession;
struct ItemInstance;
struct ItemUseRequirementView;
class CGPlayer_C;
namespace actions::held_cursor {
class HeldCursor;
}
}

namespace openwow::ui::game {

class ScriptEventDispatch;
class SecureExecution;
}
namespace openwow::ui {
class UIErrorManager;
}

namespace openwow::ui::game {

enum class TradeLuaEvent : std::uint8_t {
  kAcceptChanged,
  kPlayerItemChanged,
  kPlayerMoneyChanged,
  kClosed,
};

class TradeLuaAdapter final {
 public:
  struct Dependencies {
    openwow::game::TradeInteraction& trade;
    openwow::game::InteractionSender& interaction;
    openwow::game::WorldSession& world_session;
    openwow::game::PlayerInventoryReplica& inventory;
    openwow::game::ItemDefinitions& items;
    openwow::game::Localization& localization;
    openwow::game::QueryCache& queries;
    openwow::game::ItemInteractionSession& item_interactions;
    openwow::game::SpellCastRuntime& spells;
    const openwow::game::ItemUseRequirementSources& item_requirements;
    openwow::game::SessionHandler& session_state;
    openwow::game::MiscHandler& play_time;
    openwow::game::GroupSystem& group;
    openwow::game::BattlefieldInfo& battlefield;
    openwow::game::InstanceHandler& instance;
    openwow::ui::game::ScriptEventDispatch& events;
    openwow::ui::game::SecureExecution& security;
    openwow::ui::UIErrorManager& ui_errors;
    openwow::game::actions::held_cursor::HeldCursor* cursor;
    const openwow::data::dbc::DbcLoader* dbc;
  };

  void Bind(Dependencies);
  [[nodiscard]] bool bound() const noexcept;
  [[nodiscard]] openwow::game::TradeInteraction& trade() const;
  [[nodiscard]] openwow::game::InteractionSender& interaction() const;

  [[nodiscard]] openwow::game::ObjectManager& objects() const;
  [[nodiscard]] openwow::game::PlayerInventoryReplica& inventory() const;
  [[nodiscard]] openwow::game::ItemDefinitions& items() const;
  [[nodiscard]] openwow::game::Localization& localization() const;
  [[nodiscard]] openwow::game::QueryCache& queries() const;
  [[nodiscard]] openwow::game::actions::held_cursor::HeldCursor* cursor()
      const noexcept;
  [[nodiscard]] const openwow::data::dbc::DbcLoader* dbc() const noexcept;

  void ShowSystemMessage(int message) const;
  void Present(TradeLuaEvent event, int first = 0, int second = 0) const;
  void LeaveItemMouseover(std::uint64_t guid) const;
  void HoldTradeItem(const openwow::game::ItemInstance& item,
                     std::uint8_t source_bag, std::uint8_t source_slot) const;
  [[nodiscard]] std::uint64_t ResolveTradeTarget(std::string_view token) const;
  [[nodiscard]] std::uint32_t CurrentPlayedTime() const;

  [[nodiscard]] bool CanPerformProtectedAction(int action_kind) const;
  [[nodiscard]] bool ReplayPendingItemCast(bool require_live_item) const;
  [[nodiscard]] bool ConfirmTradeEnchant() const;
  [[nodiscard]] bool SendSocketingGems() const;
  void ClickTargetEnchantSlot() const;
  [[nodiscard]] bool MeetsItemRequirements(
      const openwow::game::CGPlayer_C& player,
      const openwow::game::ItemUseRequirementView& requirements) const;
  void EndBoundItemEnchant() const;
  void EndBoundSpellEnchant() const;

 private:
  [[nodiscard]] const Dependencies& deps() const;
  std::optional<Dependencies> dependencies_;
};

[[nodiscard]] TradeLuaAdapter& RequireTradeLuaAdapter(lua_State* state);
}
