#pragma once

#include "openwow/game/inventory/equipment/equipment_sets.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

struct lua_State;

namespace openwow::game {
class CooldownTracker;
class CursorSurface;
class ArenaHandler;
class BattlefieldInfo;
class GroupSystem;
class GossipManager;
class InstanceHandler;
class MiscHandler;
class QuestManager;
class SessionHandler;
class GuildSystem;
class InteractionSender;
class CGPlayer_C;
namespace actions::held_cursor {
class HeldCursor;
}
class ItemDefinitions;
struct ItemInstance;
struct ItemTemplate;
class ItemInteractionSession;
class ObjectManager;
class PlayerInventoryReplica;
class QueryCache;
class ReputationInfo;
class SpellBook;
class SpellCastRuntime;
class TradeInteraction;
class WorldSession;
class Localization;
}
namespace openwow::data::dbc {
class DbcLoader;
struct SpellEntry;
}

namespace openwow::ui::game {

class EventDispatcher;
class ScriptEventDispatch;
class SecureExecution;
}
namespace openwow::ui {
class UIErrorManager;
}

namespace openwow::ui::game {

class ItemLuaAdapter final {
 public:
  struct InventoryAlertState {
    std::uint64_t player = 0;
    bool initialized = false;
    std::array<int, 12> statuses{};
  };
  struct Dependencies {
    openwow::game::EquipmentSets& equipment;
    openwow::game::CooldownTracker& cooldowns;
    openwow::game::CursorSurface& cursor;
    const openwow::data::dbc::DbcLoader* dbc;
    EventDispatcher& events;
    openwow::game::GuildSystem& guild;
    openwow::game::actions::held_cursor::HeldCursor* held_cursor;
    openwow::game::InteractionSender& interaction;
    openwow::game::PlayerInventoryReplica& inventory;
    openwow::game::ItemDefinitions& items;
    openwow::game::ItemInteractionSession& item_interactions;
    openwow::game::Localization& localization;
    openwow::game::QueryCache& queries;
    openwow::game::ReputationInfo& reputation;
    openwow::game::SpellBook& spell_book;
    openwow::game::SpellCastRuntime& spells;
    openwow::game::TradeInteraction& trade;
    openwow::game::WorldSession& world_session;
    openwow::game::SessionHandler& session_state;
    openwow::game::ArenaHandler& arena;
    openwow::game::GroupSystem& group;
    openwow::game::BattlefieldInfo& battlefield;
    openwow::game::InstanceHandler& instance;
    openwow::game::GossipManager& gossip;
    openwow::game::QuestManager& quests;
    openwow::game::MiscHandler& play_time;
    ScriptEventDispatch& script_events;
    SecureExecution& security;
    openwow::ui::UIErrorManager& ui_errors;
  };

  void Bind(Dependencies dependencies);
  [[nodiscard]] bool bound() const noexcept;
  [[nodiscard]] openwow::game::EquipmentSets& equipment() const;
  [[nodiscard]] openwow::game::CooldownTracker& cooldowns() const;
  [[nodiscard]] const openwow::data::dbc::DbcLoader* dbc() const noexcept;
  [[nodiscard]] EventDispatcher& events() const;
  [[nodiscard]] openwow::game::GuildSystem& guild() const;
  [[nodiscard]] openwow::game::actions::held_cursor::HeldCursor*
  held_cursor() const noexcept;
  [[nodiscard]] openwow::game::InteractionSender& interaction() const;
  [[nodiscard]] openwow::game::PlayerInventoryReplica& inventory() const;
  [[nodiscard]] openwow::game::ItemDefinitions& items() const;
  [[nodiscard]] openwow::game::ItemInteractionSession& item_interactions() const;
  [[nodiscard]] openwow::game::Localization& localization() const;

  [[nodiscard]] openwow::game::ObjectManager& objects() const;
  [[nodiscard]] openwow::game::QueryCache& queries() const;
  [[nodiscard]] openwow::game::ReputationInfo& reputation() const;
  [[nodiscard]] openwow::game::SpellBook& spell_book() const;
  [[nodiscard]] openwow::game::SpellCastRuntime& spells() const;
  [[nodiscard]] openwow::game::TradeInteraction& trade() const;
  [[nodiscard]] openwow::game::WorldSession& world_session() const;
  [[nodiscard]] std::optional<std::string> ResolveVisibleSlotIcon(
      lua_State* state, std::uint8_t slot) const;
  void SaveSet(
      const openwow::game::EquipmentSetSave& request) const;
  void DeleteSet(openwow::game::ObjectGuid set) const;
  void UseSet(const openwow::game::EquipmentSetUse& request) const;
  void PresentSetsChanged() const;
  [[nodiscard]] bool bank_frame_open() const;
  void CloseBank() const;
  void ShowSystemMessage(int message) const;
  void PresentTradeItemChanged(int slot) const;
  [[nodiscard]] bool CurrentMapIsArena() const;
  [[nodiscard]] std::uint32_t ProficiencyMask(std::uint8_t item_class) const;
  [[nodiscard]] std::optional<std::string> ReadableCreator(
      openwow::game::ObjectGuid item) const;
  [[nodiscard]] std::optional<std::string> ReadableName(
      lua_State* state, openwow::game::ObjectGuid item) const;
  [[nodiscard]] std::optional<std::string> ReadableMaterial(
      lua_State* state, openwow::game::ObjectGuid item) const;
  void LoadReadablePage(bool publish) const;
  void PresentReadableClosed() const;
  [[nodiscard]] openwow::game::ObjectGuid ResolveUnit(
      std::string_view unit) const;
  [[nodiscard]] std::uint64_t InspectTarget() const;
  [[nodiscard]] std::optional<std::pair<std::uint32_t, bool>>
  RepairQuote() const;
  [[nodiscard]] std::optional<std::string> VisibleItemIcon(
      lua_State* state, const openwow::game::CGPlayer_C& player,
      std::uint8_t slot) const;
  [[nodiscard]] bool RepairItem(
      const openwow::game::ItemInstance& item) const;
  [[nodiscard]] bool DepositGuildBank(
      lua_State* state, int bag, int slot) const;
  void PromptItemTarget(openwow::game::ObjectGuid item) const;
  void ConfirmItemTarget(openwow::game::ObjectGuid item) const;
  [[nodiscard]] bool StartItemTargeting(
      const openwow::game::ItemInstance& item,
      const openwow::game::ItemTemplate& definition) const;
  [[nodiscard]] std::uint64_t MerchantVendor() const;
  [[nodiscard]] std::optional<std::uint64_t> ResolveItemUseTarget(
      std::string_view unit) const;
  [[nodiscard]] std::optional<bool> ItemInRange(
      std::uint32_t spell, openwow::game::ObjectGuid target) const;
  [[nodiscard]] std::uint32_t CurrentPlayedTime() const;
  [[nodiscard]] bool HasSpellPower(
      const openwow::data::dbc::SpellEntry& spell,
      const openwow::game::CGPlayer_C& player) const;
  [[nodiscard]] std::uint32_t MonotonicMilliseconds() const;
  [[nodiscard]] InventoryAlertState& inventory_alerts() noexcept {
    return inventory_alerts_;
  }

 private:
  openwow::game::EquipmentSets* equipment_ = nullptr;
  openwow::game::CooldownTracker* cooldowns_ = nullptr;
  openwow::game::CursorSurface* cursor_ = nullptr;
  const openwow::data::dbc::DbcLoader* dbc_ = nullptr;
  EventDispatcher* events_ = nullptr;
  openwow::game::GuildSystem* guild_ = nullptr;
  openwow::game::actions::held_cursor::HeldCursor* held_cursor_ = nullptr;
  openwow::game::InteractionSender* interaction_ = nullptr;
  openwow::game::PlayerInventoryReplica* inventory_ = nullptr;
  openwow::game::ItemDefinitions* items_ = nullptr;
  openwow::game::ItemInteractionSession* item_interactions_ = nullptr;
  openwow::game::Localization* localization_ = nullptr;
  openwow::game::QueryCache* queries_ = nullptr;
  openwow::game::ReputationInfo* reputation_ = nullptr;
  openwow::game::SpellBook* spell_book_ = nullptr;
  openwow::game::SpellCastRuntime* spells_ = nullptr;
  openwow::game::TradeInteraction* trade_ = nullptr;
  openwow::game::WorldSession* world_session_ = nullptr;
  openwow::game::SessionHandler* session_state_{nullptr};
  openwow::game::ArenaHandler* arena_{nullptr};
  openwow::game::GroupSystem* group_{nullptr};
  openwow::game::BattlefieldInfo* battlefield_{nullptr};
  openwow::game::InstanceHandler* instance_{nullptr};
  openwow::game::GossipManager* gossip_{nullptr};
  openwow::game::QuestManager* quests_{nullptr};
  openwow::game::MiscHandler* play_time_{nullptr};
  ScriptEventDispatch* script_events_{nullptr};
  SecureExecution* security_{nullptr};
  openwow::ui::UIErrorManager* ui_errors_{nullptr};
  InventoryAlertState inventory_alerts_;
};

[[nodiscard]] ItemLuaAdapter& RequireItemLuaAdapter(lua_State* state);

}
